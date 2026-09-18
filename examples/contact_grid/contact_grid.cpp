// 厂商样板：Orca 负责组装每格的力矢量，DLL 只计算各格矢量的模并乘以增益。
// 这是确定性的教学算法，不是压力、凝胶形变或经过标定的传感器模型。
#include "orca_sensor_abi.h"
// 由本型号的 contract.json 生成：输入结构、契约 ID、指纹和内存布局。
#include "contact_grid_input.h"

#include <algorithm>
#include <cmath>
#include <new>

namespace {

// 每次 Create 产生独立状态，返回的 handle 指向本结构；不同实例互不共享参数。
struct ContactGridState {
    double gain;    // 力大小的无量纲倍率，创建时从借用参数数组复制。
    uint64_t seed;  // 保存实例种子；本样板没有随机计算，因此不影响结果。
};

// 创建参数按 provider.json 的 global_parameters 顺序传入：这里只有 [0] gain。
// seed 由 Host 单独传入；创建失败时保持 *instance 为 nullptr。
OrcaSensorStatus ORCA_SENSOR_CALL Create(
    const OrcaSensorCreateInfoV2* info, void** instance) noexcept {
    if (!instance)
        return ORCA_SENSOR_INVALID_ARGUMENT;

    *instance = nullptr;
    if (!info || info->struct_size != sizeof(*info) ||
        info->global_parameter_count != 1 || !info->global_parameters) {
        return ORCA_SENSOR_INVALID_ARGUMENT;
    }
    const double gain = info->global_parameters[0];
    if (!std::isfinite(gain) || gain < 0.0 || gain > 1000000.0) {
        return ORCA_SENSOR_INVALID_ARGUMENT;
    }
    try {
        *instance = new ContactGridState{
            gain,
            info->seed
        };
        return ORCA_SENSOR_OK;
    } catch (...) {
        // 可捕获的 C++ 异常在 DLL 内转换成状态码，不跨越 C ABI。
        return ORCA_SENSOR_PROVIDER_ERROR;
    }
}

// 重置实例但保留创建参数；本例只记录新种子，没有需要重置的随机序列。
OrcaSensorStatus ORCA_SENSOR_CALL Reset(void* instance, uint64_t seed) noexcept {
    if (!instance)
        return ORCA_SENSOR_INVALID_ARGUMENT;

    static_cast<ContactGridState*>(instance)->seed = seed;
    return ORCA_SENSOR_OK;
}

// input 指向 Orca 按契约组装的 ContactGridInput，其中 force_grid 为 [4][4][3]。
// 每个三维矢量已在绑定 site 的局部坐标系中累加，DLL 无需查询场景或分配输入。
// output 由 Host 预分配，为连续、行优先的 float64[4][4]，单位 N。
// 输入/输出地址仅在回调期间借用；写 output 返回数据，返回值本身是状态码。
OrcaSensorStatus ORCA_SENSOR_CALL Compute(
    void* instance, const void* input, uint64_t input_size,
    double* output, uint64_t output_count) noexcept {
    if (!instance || !input || input_size != sizeof(ContactGridInput) ||
        !output || output_count != 16) {
        return ORCA_SENSOR_INVALID_ARGUMENT;
    }
    try {
        const auto& state = *static_cast<const ContactGridState*>(instance);
        const auto& sample = *static_cast<const ContactGridInput*>(input);
        if (!std::isfinite(sample.sample_time))
            return ORCA_SENSOR_INVALID_ARGUMENT;

        // 先在栈上计算完整结果，通过全部检查后才写入 Host 的输出缓冲区。
        double values[16]{};
        for (uint32_t row = 0; row < 4; ++row) {
            for (uint32_t column = 0; column < 4; ++column) {
                const double* force = sample.force_grid[row][column];
                if (!std::isfinite(force[0]) ||
                    !std::isfinite(force[1]) ||
                    !std::isfinite(force[2])) {
                    return ORCA_SENSOR_INVALID_ARGUMENT;
                }
                // 先累加再取模的网格力，不等同于逐接触点取模后相加。
                const double value = std::hypot(force[0], force[1], force[2]) * state.gain;
                if (!std::isfinite(value))
                    return ORCA_SENSOR_PROVIDER_ERROR;

                values[row * 4 + column] = value;
            }
        }
        // 一次提交已检查的结果：失败路径不会留下部分新输出。
        std::copy(values, values + 16, output);
        return ORCA_SENSOR_OK;
    } catch (...) {
        return ORCA_SENSOR_PROVIDER_ERROR;
    }
}

// 由创建实例的 DLL 释放状态；Host 不需要了解 ContactGridState 的布局。
void ORCA_SENSOR_CALL Destroy(void* instance) noexcept {
    delete static_cast<ContactGridState*>(instance);
}

// 类型描述符注册“型号”及其回调地址，不是实例；Create 每次返回独立 handle。
// 字段顺序对应公开 OrcaSensorTypeDescriptorV2，静态存储在 DLL 卸载前有效。
const OrcaSensorTypeDescriptorV2 kTypes[] = {
    {
        sizeof(OrcaSensorTypeDescriptorV2), // struct_size：型号描述符字节数。
        ORCA_SENSOR_ABI_VERSION,           // abi_version：C ABI 编号，不是 SDK 发布版本。
        "com.orca.examples.contact_grid",  // type_id：与 provider.json 一致的稳定型号 ID。
        ContactGridInput_contract_id,      // input_contract_id：生成的输入契约 ID。
        ContactGridInput_fingerprint,      // input_fingerprint：校验输入契约一致性。
        &ContactGridInput_layout,          // input_layout：生成的字段偏移、类型和容量。
        {
            sizeof(OrcaSensorOutputSpecV2), // output.struct_size：输出规格字节数。
            2,                             // output.ndim：二维输出。
            {4, 4, 0, 0}                   // output.shape：4×4；未使用维度补 0。
        },
        1,                                // global_parameter_count：只接收 gain。
        Create,                           // create：分配实例并返回 handle。
        Reset,                            // reset：重置该实例。
        Compute,                          // compute：读取输入并写入输出。
        Destroy,                          // destroy：释放该实例。
    }
};

// 厂商描述符汇总本 DLL 提供的型号；本例只有一个型号。
const OrcaSensorProviderDescriptorV2 kProvider = {
    sizeof(OrcaSensorProviderDescriptorV2), // struct_size：厂商描述符字节数。
    ORCA_SENSOR_ABI_VERSION,                // abi_version：Host 校验的 C ABI 编号。
    "com.orca.examples.contact_grid",       // provider_id：与清单一致的厂商包标识。
    "1.0.0",                               // provider_version：厂商包版本。
    1,                                     // type_count：型号数量。
    kTypes,                                // types：型号描述符数组地址。
};

}  // namespace

// 统一导出入口：Host 通过固定名称获得描述符，再读取其中的回调函数地址。
// extern "C" 保持 C 链接名称；此处不创建实例，也不执行单步计算。
extern "C" ORCA_SENSOR_EXPORT const OrcaSensorProviderDescriptorV2* ORCA_SENSOR_CALL
orca_sensor_get_provider(void) {
    return &kProvider;
}
