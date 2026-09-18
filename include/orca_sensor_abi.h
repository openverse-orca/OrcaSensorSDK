#ifndef ORCA_SENSOR_ABI_H
#define ORCA_SENSOR_ABI_H

/**
 * 厂商算法动态库与 Orca Host 之间的公共 C 接口。
 * 动态库在 Linux 上通常为 .so，在 Windows 上为 .dll，在 macOS 上为 .dylib。
 *
 * 阅读顺序：输入布局 -> 输出规格 -> 实例生命周期回调 -> 类型描述符 -> 厂商入口。
 * 厂商用描述符登记型号和回调；Host 组装输入、调用 compute，再收集输出。
 * 算法只处理契约规定的数据，不需要依赖 Python 或具体物理引擎。
 *
 * 约定：
 * - 名称中的 V2 表示 C ABI 2，与 SDK 的 1.0.0 发布版本不是一回事。
 *   例如，若后续 SDK 1.0.1 只修正文档而未改变 C 接口，结构名称仍使用 V2。
 * - 使用目标平台的自然 C 对齐；不要使用 #pragma pack 改变布局。
 * - 字符串使用 UTF-8，以 NUL 结尾；描述符及其引用的数据须存活到动态库卸载。
 */
#include <stdint.h>

/* 导出符号，使 Host 能按名称找到厂商入口。
 * _WIN32 同时覆盖 32 位和 64 位 Windows；ORCA_SENSOR_CALL 统一调用约定。
 */
#if defined(_WIN32)
#define ORCA_SENSOR_EXPORT __declspec(dllexport)
#define ORCA_SENSOR_CALL __cdecl
#else
#define ORCA_SENSOR_EXPORT __attribute__((visibility("default")))
#define ORCA_SENSOR_CALL
#endif

/* C++ 编译时使用 C 链接名称，避免入口名称被 C++ 编译器改写。 */
#ifdef __cplusplus
extern "C" {
#endif

#define ORCA_SENSOR_ABI_VERSION 2u  /* 当前二进制接口版本。 */
#define ORCA_SENSOR_MAX_DIMS 4u     /* 输出张量最多支持四个维度。 */

/* 返回值只表示执行状态；计算结果写入 output，不通过返回值传递。 */
typedef int32_t OrcaSensorStatus;

#define ORCA_SENSOR_OK                0  /* 成功。 */
#define ORCA_SENSOR_INVALID_ARGUMENT  1  /* 参数、指针或数据不符合要求。 */
#define ORCA_SENSOR_ABI_MISMATCH       2  /* ABI 版本或结构布局不匹配。 */
#define ORCA_SENSOR_NOT_FOUND          3  /* 库、型号、符号或句柄不存在。 */
#define ORCA_SENSOR_CAPACITY_EXCEEDED  4  /* 数据超过契约声明的容量。 */
#define ORCA_SENSOR_PROVIDER_ERROR    5  /* 厂商回调失败或输出无效。 */
#define ORCA_SENSOR_INVALID_STATE     6  /* 当前实例状态不允许此操作。 */
#define ORCA_SENSOR_INTERNAL_ERROR    7  /* Host 内部执行失败。 */

/* 存储类型编号只描述内存表示；力、距离等物理含义由契约声明。 */
#define ORCA_SENSOR_DTYPE_F64 1u  /* double：64 位浮点数。 */
#define ORCA_SENSOR_DTYPE_F32 2u  /* float：32 位浮点数。 */
#define ORCA_SENSOR_DTYPE_U32 3u  /* uint32_t：32 位无符号整数。 */
#define ORCA_SENSOR_DTYPE_U64 4u  /* uint64_t：64 位无符号整数。 */
#define ORCA_SENSOR_DTYPE_U8  5u  /* uint8_t：8 位无符号整数。 */

/* 固定字段没有动态计数字段，用该值标记 count_offset。 */
#define ORCA_SENSOR_NO_COUNT UINT32_MAX

/**
 * 输入结构中一个字段的内存布局，不是该字段本步的实际数值。
 *
 * 由契约工具生成。Host 根据 field_id 找到调用方提供的数据，再按偏移复制。
 * field_id 仅在当前输入契约内有意义，不是模型对象 ID 或全局传感器 ID。
 *
 * 固定字段：count_offset = ORCA_SENSOR_NO_COUNT，capacity = 1，stride = 0。
 * 记录数组内的字段：offset 指向第 0 条记录内的字段，stride 为 sizeof(记录)。
 * Host 在 count_offset 处填写有效记录数；同一数组的各字段必须共享记录数，
 * 并具有相同的 capacity、stride 和 count_offset。偏移与步长均以字节计。
 */
typedef struct OrcaSensorInputFieldV2 {
    uint32_t field_id;      /* 契约工具分配的非零字段编号。 */
    uint32_t dtype;         /* ORCA_SENSOR_DTYPE_* 存储类型。 */
    uint32_t offset;        /* 相对于整个输入结构起始地址的字节偏移。 */
    uint32_t stride;        /* 相邻记录中同一字段的字节间隔；固定字段为 0。 */
    uint32_t capacity;      /* 记录数组的最大条数；固定字段为 1。 */
    uint32_t components;    /* 每条数据的标量个数；例如三维力向量为 3。 */
    uint32_t count_offset;  /* uint32_t 有效记录数的位置；固定字段用 NO_COUNT。 */
} OrcaSensorInputFieldV2;

/**
 * 整个厂商输入结构的布局描述。
 * Host 在创建实例时，按 input_size 分配输入缓冲区；厂商无需手算偏移。
 * 使用生成头文件中的布局常量，fields 数组须随描述符保持有效。
 */
typedef struct OrcaSensorInputLayoutV2 {
    uint32_t struct_size;                 /* sizeof(OrcaSensorInputLayoutV2)。 */
    uint32_t input_size;                  /* 厂商输入结构的 sizeof，单位为字节。 */
    uint32_t field_count;                 /* fields 数组长度，不是接触记录条数。 */
    const OrcaSensorInputFieldV2* fields;  /* 只读字段布局数组。 */
} OrcaSensorInputLayoutV2;

/**
 * 型号的固定输出形状。当前输出仅支持连续、行优先的 float64 数据。
 * 例如 4 x 4 网格：ndim = 2，shape = {4, 4, 0, 0}，共 16 个 double。
 * Host 按形状分配输出缓冲区；输出含义、单位和通道由输出契约说明。
 */
typedef struct OrcaSensorOutputSpecV2 {
    uint32_t struct_size;                   /* sizeof(OrcaSensorOutputSpecV2)。 */
    uint32_t ndim;                          /* 有效维数，范围为 1..MAX_DIMS。 */
    uint32_t shape[ORCA_SENSOR_MAX_DIMS];    /* 有效维度须为正，未使用维度填 0。 */
} OrcaSensorOutputSpecV2;

/**
 * 创建一次传感器实例时传给厂商的配置，不是每步采集到的物理量。
 * global_parameters 按 provider.json 中 global_parameters 的声明顺序排列。
 * “全局”仅指当前实例的算法配置，不是所有传感器共享的进程级变量。
 * info 及参数数组只在 create 调用期间借用；厂商须把所需值复制到实例状态。
 */
typedef struct OrcaSensorCreateInfoV2 {
    uint32_t struct_size;              /* sizeof(OrcaSensorCreateInfoV2)。 */
    uint64_t seed;                     /* 本实例的初始随机种子。 */
    uint32_t global_parameter_count;   /* 参数个数，须与型号描述符一致。 */
    const double* global_parameters;   /* 只读配置数组；参数为 0 个时可以为 NULL。 */
} OrcaSensorCreateInfoV2;

/* 实例生命周期：create -> (compute / reset)* -> destroy。
 * 以下 typedef 定义函数指针类型；厂商把自己的函数地址填入型号描述符。
 * 所有回调都不得让 C++ 异常跨越 C ABI 边界。
 */

/**
 * 创建独立算法状态，通过 *instance 返回厂商私有指针。
 * 成功后，*instance 指向厂商分配的对象，由对应 destroy 释放，Host 不直接 free。
 * 成功时 *instance 必须非空；失败时必须为 NULL，厂商自行清理未交付的资源。
 */
typedef OrcaSensorStatus (ORCA_SENSOR_CALL *OrcaSensorCreateV2)(
    const OrcaSensorCreateInfoV2* info,
    void** instance);

/** 重置本实例的算法状态与随机种子；保留创建时的配置，不创建新实例。 */
typedef OrcaSensorStatus (ORCA_SENSOR_CALL *OrcaSensorResetV2)(
    void* instance,
    uint64_t seed);

/**
 * 计算一次：读取已组装的输入结构，写满 Host 提供的输出缓冲区。
 *
 * instance    ：create 返回的厂商私有状态，不是函数地址。
 * input       ：只读输入；转成契约生成的结构体指针后使用。
 * input_size  ：输入缓冲区大小，单位为字节。
 * output      ：Host 分配的 double 数组，厂商只写入，不释放或替换地址。
 * output_count：double 元素个数，不是字节数，等于输出 shape 的乘积。
 *
 * input/output 只在本次回调期间借用，不能保存指针留到后续步骤使用。
 * 成功时必须写入每个输出元素，并保证数值有限（不含 NaN 或正负无穷）；
 * 返回值只表示成功或失败。
 */
typedef OrcaSensorStatus (ORCA_SENSOR_CALL *OrcaSensorComputeV2)(
    void* instance,
    const void* input,
    uint64_t input_size,
    double* output,
    uint64_t output_count);

/** 释放 create 分配的实例及其私有资源；不得抛出异常。 */
typedef void (ORCA_SENSOR_CALL *OrcaSensorDestroyV2)(void* instance);

/**
 * 一个传感器型号的“注册说明”：固定输入输出约定 + 实例生命周期函数地址。
 *
 * 同一型号的多个挂载实例共用该描述符，但每次 create 返回独立状态。
 * input_contract_id、input_fingerprint 和 input_layout 取自契约生成的头文件。
 * 推荐使用 static const 保存描述符，不得在入口函数中返回栈上临时对象。
 */
typedef struct OrcaSensorTypeDescriptorV2 {
    uint32_t struct_size;                        /* sizeof(OrcaSensorTypeDescriptorV2)。 */
    uint32_t abi_version;                        /* ORCA_SENSOR_ABI_VERSION。 */
    const char* type_id;                         /* 稳定型号 ID，须与包清单一致。 */
    const char* input_contract_id;               /* 输入契约 ID，不是动态库路径。 */
    const char* input_fingerprint;               /* 生成的契约指纹，防止输入约定错配。 */
    const OrcaSensorInputLayoutV2* input_layout;  /* 厂商输入结构的布局描述。 */
    OrcaSensorOutputSpecV2 output;               /* 此型号的固定输出形状。 */
    uint32_t global_parameter_count;             /* 创建实例所需的配置值个数。 */

    OrcaSensorCreateV2 create;    /* 分配并初始化实例状态。 */
    OrcaSensorResetV2 reset;      /* 重置已有实例。 */
    OrcaSensorComputeV2 compute;  /* 根据输入计算输出。 */
    OrcaSensorDestroyV2 destroy;  /* 释放实例状态。 */
} OrcaSensorTypeDescriptorV2;

/**
 * 一个厂商算法动态库的入口描述符，可同时登记多个传感器型号。
 * Host 先取得此对象，再从 types 数组中按 type_id 找到对应型号和回调。
 * 描述符、types 数组及引用的字符串均由动态库持有，Host 只借用、不释放。
 */
typedef struct OrcaSensorProviderDescriptorV2 {
    uint32_t struct_size;                     /* sizeof(OrcaSensorProviderDescriptorV2)。 */
    uint32_t abi_version;                     /* ORCA_SENSOR_ABI_VERSION。 */
    const char* provider_id;                  /* 厂商插件包 ID，须与包清单一致。 */
    const char* provider_version;             /* 厂商包版本，不是 SDK 或 ABI 版本。 */
    uint32_t type_count;                      /* types 数组中的型号个数。 */
    const OrcaSensorTypeDescriptorV2* types;   /* 只读型号描述符数组。 */
} OrcaSensorProviderDescriptorV2;

/**
 * 厂商实现并导出的固定入口；定义时使用 ORCA_SENSOR_EXPORT 标记。
 * Host 按名称查找这一个符号，再从返回的描述符中取得各回调函数指针。
 * 返回对象必须在动态库卸载前一直有效；入口同样不得向外抛出异常。
 */
const OrcaSensorProviderDescriptorV2* ORCA_SENSOR_CALL
orca_sensor_get_provider(void);

/* Host 查找入口符号后，将其地址解释为这个函数指针类型并调用。 */
typedef const OrcaSensorProviderDescriptorV2* (ORCA_SENSOR_CALL
    *OrcaSensorGetProviderV2)(void);

#ifdef __cplusplus
}
#endif
#endif
