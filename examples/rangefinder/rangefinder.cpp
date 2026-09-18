// 厂商样板：Orca 查询射线距离，DLL 接收查询结果并施加可配置偏置。
// 这只是测距输入/输出链路的教学示例，不模拟光学、反射材质或真实测距误差。
#include "orca_sensor_abi.h"
// 由本型号的 contract.json 生成：输入结构、契约 ID、指纹和内存布局。
#include "rangefinder_input.h"

#include <algorithm>
#include <cmath>
#include <new>

namespace {

// 每次 Create 分配一份实例状态；handle 指向本结构，不同实例互不共享参数。
struct RangefinderState {
    double bias_m;  // 有效距离的加性偏置，单位 m；创建时复制参数值。
    uint64_t seed;  // 保存实例种子；本例没有随机计算，因此不影响输出。
};

// 参数数组按 provider.json 的 global_parameters 顺序排列：这里只有 [0] bias_m。
// seed 由 Host 单独传入；不保存 info 或参数数组地址，创建失败时实例指针保持为空。
OrcaSensorStatus ORCA_SENSOR_CALL Create(
    const OrcaSensorCreateInfoV2* info, void** instance) noexcept {
    if (!instance)
        return ORCA_SENSOR_INVALID_ARGUMENT;

    *instance = nullptr;
    if (!info || info->struct_size != sizeof(*info) ||
        info->global_parameter_count != 1 || !info->global_parameters) {
        return ORCA_SENSOR_INVALID_ARGUMENT;
    }
    const double bias = info->global_parameters[0];
    if (!std::isfinite(bias) || bias < -1.0 || bias > 1.0)
        return ORCA_SENSOR_INVALID_ARGUMENT;

    try {
        *instance = new RangefinderState{
            bias,
            info->seed
        };
        return ORCA_SENSOR_OK;
    } catch (...) {
        // 可捕获的 C++ 异常转换成状态码，不允许跨越 C ABI。
        return ORCA_SENSOR_PROVIDER_ERROR;
    }
}

// 重置种子但不改变偏置；本例无需重置随机序列。
OrcaSensorStatus ORCA_SENSOR_CALL Reset(void* instance, uint64_t seed) noexcept {
    if (!instance)
        return ORCA_SENSOR_INVALID_ARGUMENT;

    static_cast<RangefinderState*>(instance)->seed = seed;
    return ORCA_SENSOR_OK;
}

// Orca 按契约填好 RangefinderInput，包含采样时刻和 distance；DLL 不查询场景。
// output 是 Host 预分配的连续 float64[1]，单位 m，-1 表示未命中。
// 输入/输出指针仅在回调期间借用；结果写入 output[0]，函数返回的是状态码。
OrcaSensorStatus ORCA_SENSOR_CALL Compute(
    void* instance, const void* input, uint64_t input_size,
    double* output, uint64_t output_count) noexcept {
    if (!instance || !input || input_size != sizeof(RangefinderInput) ||
        !output || output_count != 1) {
        return ORCA_SENSOR_INVALID_ARGUMENT;
    }
    try {
        const auto& state = *static_cast<const RangefinderState*>(instance);
        const auto& sample = *static_cast<const RangefinderInput*>(input);
        if (!std::isfinite(sample.sample_time) || !std::isfinite(sample.distance) ||
            (sample.distance < 0.0 && sample.distance != -1.0)) {
            return ORCA_SENSOR_INVALID_ARGUMENT;
        }
        // 未命中的 -1 原样传回；有效距离加偏置后截断到非负值。
        // 因此负偏置不会制造“未命中”，正偏置则可能让输出超出射线的几何查询范围。
        const double value = sample.distance == -1.0
            ? -1.0
            : std::max(0.0, sample.distance + state.bias_m);
        if (!std::isfinite(value))
            return ORCA_SENSOR_PROVIDER_ERROR;

        output[0] = value;
        return ORCA_SENSOR_OK;
    } catch (...) {
        return ORCA_SENSOR_PROVIDER_ERROR;
    }
}

// 由本 DLL 释放实例状态，Host 不需要了解 RangefinderState 的内部布局。
void ORCA_SENSOR_CALL Destroy(void* instance) noexcept {
    delete static_cast<RangefinderState*>(instance);
}

// 型号描述符公开“契约 + 输出规格 + 回调地址”，并不代表某个挂载实例。
// 字段顺序来自 OrcaSensorTypeDescriptorV2；描述符静态存储，DLL 卸载前有效。
const OrcaSensorTypeDescriptorV2 kTypes[] = {
    {
        sizeof(OrcaSensorTypeDescriptorV2), // struct_size：型号描述符字节数。
        ORCA_SENSOR_ABI_VERSION,           // abi_version：C ABI 编号，不是 SDK 发布版本。
        "com.orca.examples.rangefinder",   // type_id：清单中的稳定型号 ID。
        RangefinderInput_contract_id,      // input_contract_id：生成的输入契约 ID。
        RangefinderInput_fingerprint,      // input_fingerprint：校验输入契约一致性。
        &RangefinderInput_layout,          // input_layout：生成的字段类型和内存偏移。
        {
            sizeof(OrcaSensorOutputSpecV2), // output.struct_size：输出规格字节数。
            1,                             // output.ndim：一维输出。
            {1, 0, 0, 0}                   // output.shape：[1]；未使用维度补 0。
        },
        1,                                // global_parameter_count：只接收 bias_m。
        Create,                           // create：分配实例并返回独立 handle。
        Reset,                            // reset：重置该实例。
        Compute,                          // compute：读取输入并写入输出。
        Destroy,                          // destroy：释放该实例。
    }
};

// 本 DLL 的厂商描述符，仅包含上面一个测距型号。
const OrcaSensorProviderDescriptorV2 kProvider = {
    sizeof(OrcaSensorProviderDescriptorV2), // struct_size：厂商描述符字节数。
    ORCA_SENSOR_ABI_VERSION,                // abi_version：Host 校验的 C ABI 编号。
    "com.orca.examples.rangefinder",        // provider_id：与清单一致的厂商包标识。
    "1.0.0",                               // provider_version：厂商包版本。
    1,                                     // type_count：型号数量。
    kTypes,                                // types：型号描述符数组地址。
};

}  // namespace

// 所有厂商包使用同名导出入口；Host 通过它找到描述符中的实际回调函数。
// extern "C" 保持 C 链接名称，此入口不负责创建实例或执行计算。
extern "C" ORCA_SENSOR_EXPORT const OrcaSensorProviderDescriptorV2* ORCA_SENSOR_CALL
orca_sensor_get_provider(void) {
    return &kProvider;
}
