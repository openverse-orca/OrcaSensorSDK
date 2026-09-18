#ifndef ORCA_SENSOR_HOST_H
#define ORCA_SENSOR_HOST_H

/*
 * Orca Sensor Host 的原生 C 接口。
 *
 * Host 负责加载厂商动态库、管理实例、按契约组装输入，以及暂存和发布输出。
 * 厂商算法接口及结构定义见 orca_sensor_abi.h；本文件面向 Host 的调用方。
 * 实现算法回调通常只需包含 orca_sensor_abi.h；原生接入程序才调用下方 Host API。
 * 所有结构体均沿用平台自然 C 对齐，不要使用额外的紧凑打包设置。
 *
 * 最小调用顺序：open -> descriptor -> create -> process -> publish_batch ->
 * copy_output -> destroy -> close。重复采样时，从 process 开始新的计算与发布。
 *
 * 仅加载可信的本机动态库。Host 与厂商代码运行在同一进程；状态码转换和
 * C++ 异常处理并非进程隔离，不能防止非法指针、越界访问或本机代码崩溃。
 */
#include "orca_sensor_abi.h"

/*
 * Windows 的 32 位和 64 位编译目标都会定义 _WIN32。
 * ORCA_SENSOR_HOST_BUILD 只由 Host 自身的构建定义，用于导出下方的 Host API；
 * 普通 C/C++ 使用方不要定义它，链接 Host 时使用 dllimport 声明。
 * Python ctypes 通过运行时加载 DLL、查找导出符号调用，不依靠 dllimport。
 * 非 Windows 平台复用 ABI 头文件中的默认符号可见性标记。
 * 函数调用约定仍统一由 ORCA_SENSOR_CALL 指定。
 */
#if defined(_WIN32)
#if defined(ORCA_SENSOR_HOST_BUILD)
#define ORCA_SENSOR_HOST_API __declspec(dllexport)
#else
#define ORCA_SENSOR_HOST_API __declspec(dllimport)
#endif
#else
#define ORCA_SENSOR_HOST_API ORCA_SENSOR_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 句柄与输入视图 ==================== */

/*
 * Host 分配的非零、不透明整数编号，不是内存地址、操作系统库句柄或函数指针。
 * 调用方只保存和传回编号，不得解引用，也不能跨 Host 生命周期复用。
 * 库句柄通过 close 注销；实例句柄通过 destroy 注销。二者不能混用。
 */
typedef uint64_t OrcaSensorLibraryHandle;
typedef uint64_t OrcaSensorInstanceHandle;

/*
 * 一个契约字段的连续数据视图；记录数组按“每个叶字段一列”分别传入。
 *
 * 固定字段：count 必须为 1，即使该字段本身是向量或矩阵。
 * 记录数组叶字段：count 是本次有效记录数，可为 0，但不能超过契约容量。
 * 同一记录数组的所有叶字段必须给出相同的 count。
 *
 * shape 来自契约，不在本结构中重复传递；components 是每项固定 shape 的
 * 元素总数。每项按行优先展开，各项连续排列，调用方数据中没有额外步长。
 * byte_size 必须精确等于 count * components * 对应 dtype 的单元素字节数。
 * 例如，3 条 normal[3] 记录的 count=3、components=3、byte_size=9*sizeof(double)；
 * 一个固定的 float64[4][4][3] 网格则为 count=1、components=48。
 *
 * data 及其内容由调用方持有，须保持有效直到 process 返回；Host 只读取并
 * 复制到自己的输入缓冲区，不接管或释放这些内存，也不保留调用方的指针。
 * data 仅在空记录数组时可以为 NULL。调用方缓冲区无需额外对齐，Host 使用
 * 字节复制；但非空指针必须确实指向可读的 byte_size 字节内存。
 */
typedef struct OrcaSensorFieldViewV2 {
    uint32_t field_id;     /* 当前契约内的字段编号，须与生成布局一致。 */
    uint32_t dtype;        /* ORCA_SENSOR_DTYPE_* 存储类型，须与布局一致。 */
    uint32_t count;        /* 固定字段为 1；记录数组为本次有效记录数。 */
    uint32_t components;   /* 每项固定 shape 的元素总数，须与布局一致。 */
    uint64_t byte_size;    /* data 中有效数据的精确字节数。 */
    const void* data;      /* 只读借用的连续数据；空记录数组允许 NULL。 */
} OrcaSensorFieldViewV2;

/*
 * 一次 process 调用的输入。struct_size 须填写 sizeof(OrcaSensorStepInputV2)。
 * fields 必须恰好提供契约声明的全部字段；按 field_id 匹配，顺序可以不同，
 * 但不能重复、遗漏或额外添加字段。fields 数组同样借用至 process 返回。
 *
 * time、dt、step_index 是调用层元数据，不会自动写入厂商的契约输入结构。
 * Host 只检查 time 和 dt 有限且 dt > 0；不会按 step_index 排序、去重或
 * 自动推进步数。若契约需要采样时间、步索引等字段，调用方仍须在 fields
 * 中单独提供相应数据；Host 不检查它们与这里的元数据是否一致。
 */
typedef struct OrcaSensorStepInputV2 {
    uint32_t struct_size;                /* sizeof(OrcaSensorStepInputV2)。 */
    double time;                         /* 当前源状态的仿真时间，单位 s。 */
    double dt;                           /* 本次物理步长，单位 s，须大于 0。 */
    uint64_t step_index;                  /* 调用方提供的步索引，不由 Host 自动递增。 */
    uint32_t field_count;                 /* fields 数组长度，须等于契约字段数。 */
    const OrcaSensorFieldViewV2* fields;   /* 只读数据视图数组，借用至 process 返回。 */
} OrcaSensorStepInputV2;

/* ==================== 错误与 ABI 查询 ==================== */

/*
 * 返回 OrcaSensorStatus 的接口以 ORCA_SENSOR_OK 表示成功，其他值表示失败。
 * 厂商 create/reset/compute 返回非成功状态时，Host 转为 ORCA_SENSOR_PROVIDER_ERROR；
 * 详细文本由 last_error 提供。厂商回调应通过状态码报告失败，不应抛出 C++ 异常；
 * Host 的异常捕获不构成对非法内存访问或进程崩溃的隔离。
 */

/*
 * 返回当前调用线程的错误文本；UTF-8、以 NUL 结束，内容可能被截断。
 * 指针指向 Host 的线程局部存储，调用方不得修改或释放；需要保留时请复制。
 * 下方管理/计算接口的下一次调用会清除或改写本线程的错误文本，因此失败后
 * 应立即读取。last_error 和 abi_version 查询本身不会清除它。
 */
ORCA_SENSOR_HOST_API const char* ORCA_SENSOR_CALL
orca_sensor_host_last_error(void);

/* 返回 Host 支持的 C ABI 编号；这不是 SDK 的发行版本号。 */
ORCA_SENSOR_HOST_API uint32_t ORCA_SENSOR_CALL
orca_sensor_host_abi_version(void);

/* ==================== 厂商库的加载与关闭 ==================== */

/*
 * 使用 UTF-8、NUL 结尾的绝对路径加载厂商动态库，并校验导出入口与 ABI 布局。
 * library 必须指向可写的输出槽：成功写入非零句柄，失败时保持为 0。
 * 路径字符串只在本次调用期间借用。失败详情通过 last_error 获取。
 */
ORCA_SENSOR_HOST_API OrcaSensorStatus ORCA_SENSOR_CALL
orca_sensor_host_open(
    const char* absolute_library_path,
    OrcaSensorLibraryHandle* library);

/*
 * 借用厂商描述符及其嵌套数据，不复制、不转移所有权；调用方不得修改或释放。
 * 请在 close 前完成读取或自行复制，不依赖动态库延迟卸载来延长借用期限。
 * 句柄无效时返回 NULL，错误详情通过 last_error 获取。
 */
ORCA_SENSOR_HOST_API const OrcaSensorProviderDescriptorV2* ORCA_SENSOR_CALL
orca_sensor_host_descriptor(
    OrcaSensorLibraryHandle library);

/*
 * 注销库句柄；此后不能再用它查询描述符或创建实例。
 * 不会销毁已创建的实例：实例保留库引用，仍可使用各自的实例句柄。
 * 库的实际卸载可能延后至关联实例全部销毁，因此 close 成功不等于立即卸载。
 */
ORCA_SENSOR_HOST_API OrcaSensorStatus ORCA_SENSOR_CALL
orca_sensor_host_close(
    OrcaSensorLibraryHandle library);

/* ==================== 实例的创建、销毁与重置 ==================== */

/*
 * 按 type_id 创建实例，调用厂商 create，并分配 Host 自有的输入/输出缓冲区。
 * info->struct_size 须匹配结构大小；参数数量须与类型声明一致，参数值须有限。
 * type_id、info 及 info 引用的参数只在 create 调用期间借用；厂商需要长期
 * 使用的参数应由其 create 回调复制到自己的实例状态。
 * instance 必须指向可写的输出槽：成功写入非零句柄，失败时保持为 0。
 * 新实例尚无可读取的已发布输出，需先 process，再 publish_batch。
 */
ORCA_SENSOR_HOST_API OrcaSensorStatus ORCA_SENSOR_CALL
orca_sensor_host_create(
    OrcaSensorLibraryHandle library,
    const char* type_id,
    const OrcaSensorCreateInfoV2* info,
    OrcaSensorInstanceHandle* instance);

/*
 * 注销实例并调用厂商 destroy；厂商私有内存由该回调释放，Host 管理自身缓冲区。
 * Host 先移除实例编号；即使厂商 destroy 违反约定抛出 C++ 异常，旧句柄也已
 * 失效，不能通过重试同一句柄再次销毁。
 */
ORCA_SENSOR_HOST_API OrcaSensorStatus ORCA_SENSOR_CALL
orca_sensor_host_destroy(
    OrcaSensorInstanceHandle instance);

/*
 * 清除暂存/已发布结果并向厂商 reset 传递 seed，供厂商重置状态及随机序列。
 * 成功后解除故障态，但不会产生可读输出；仍须重新 process 和 publish_batch。
 * reset 失败时实例保持故障态，旧输出也不可读；可再次 reset 或销毁实例。
 */
ORCA_SENSOR_HOST_API OrcaSensorStatus ORCA_SENSOR_CALL
orca_sensor_host_reset(
    OrcaSensorInstanceHandle instance,
    uint64_t seed);

/* ==================== 计算、发布与读取 ==================== */

/*
 * 校验字段并复制组装厂商输入，调用类型描述符中的 compute 函数指针，暂存结果。
 * Host 会检查浮点输入有限，并要求厂商写满全部输出且每项为有限值。
 *
 * 成功只更新暂存结果，不自动发布；连续 process 会覆盖上一次暂存结果，适合
 * 多个物理子步。若已有发布值，发布新结果前 copy_output 仍可读取上次发布值。
 * 有效实例的输入校验或计算失败后会进入故障态，暂存结果失效，旧发布值也被
 * 禁止读取；必须成功 reset 后才能继续计算。无效句柄则返回对应的查找错误。
 *
 * input、fields 和各 data 指针只借用至本次调用返回。
 */
ORCA_SENSOR_HOST_API OrcaSensorStatus ORCA_SENSOR_CALL
orca_sensor_host_process(
    OrcaSensorInstanceHandle instance,
    const OrcaSensorStepInputV2* input);

/*
 * 先校验整批实例均有效、未故障、有成功的暂存结果，且句柄没有重复；全部通过
 * 后才统一发布。校验失败不会发布其中任何实例，也不会额外使其他实例故障。
 * 成功时仅交换既有输出缓冲区，不调用厂商 compute，不分配新的输出缓冲区。
 * 发布会消耗暂存状态；再次发布前须先取得新的 process 成功结果。
 * instances 数组只在调用期间借用；instance_count 为 0 时允许 NULL，操作为空。
 */
ORCA_SENSOR_HOST_API OrcaSensorStatus ORCA_SENSOR_CALL
orca_sensor_host_publish_batch(
    const OrcaSensorInstanceHandle* instances,
    uint32_t instance_count);

/*
 * 将已发布的行优先 float64 张量复制到调用方拥有的连续内存，不返回内部指针。
 * output 必须非 NULL，并指向足够的可写存储；output_count 必须精确等于类型
 * 输出 shape 的元素总数，而不是字节数。Host 不接管或释放该缓冲区。
 * 仅在实例未故障且已有发布结果时可读；读取不消耗结果，可重复复制。
 * 本接口既不计算也不发布，尚未发布的 process 结果不会被读取。
 */
ORCA_SENSOR_HOST_API OrcaSensorStatus ORCA_SENSOR_CALL
orca_sensor_host_copy_output(
    OrcaSensorInstanceHandle instance,
    double* output,
    uint64_t output_count);

#ifdef __cplusplus
}
#endif
#endif
