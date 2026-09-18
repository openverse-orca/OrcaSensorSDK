// 厂商样板：Orca 组装接触点，DLL 将接触点的法向力大小累加到平面网格。
// 这是教学用的力栅格化算法，不是压力分布、凝胶形变或经过标定的传感器模型。
#include "orca_sensor_abi.h"
// 由本型号的 contract.json 生成：输入结构、契约 ID、指纹和内存布局。
#include "touch_grid_input.h"

#include <algorithm>
#include <cmath>
#include <new>

namespace {

// 每次 Create 独立分配一份状态，返回给 Host 的 handle 就是其指针。
// 不同挂载实例不共享参数；参数值在创建时复制，不保留 info 中的借用指针。
struct TouchGridState {
    double extent_x;  // 网格沿局部 X 轴的完整宽度，单位 m。
    double extent_y;  // 网格沿局部 Y 轴的完整高度，单位 m。
    double gain;     // 法向力大小的无量纲倍率。
    uint64_t seed;   // 保存实例种子；本样板没有随机计算，因此不影响结果。
};

// 创建实例：global_parameters 的顺序与 provider.json 一致：
// [0] extent_x、[1] extent_y、[2] gain；seed 由 Host 单独传入。
// 失败时保持 *instance 为 nullptr，不能让 C++ 异常跨越 C ABI。
OrcaSensorStatus ORCA_SENSOR_CALL Create(
    const OrcaSensorCreateInfoV2* info, void** instance) noexcept {
    if (!instance)
        return ORCA_SENSOR_INVALID_ARGUMENT;

    *instance = nullptr;
    if (!info || info->struct_size != sizeof(*info) ||
        info->global_parameter_count != 3 || !info->global_parameters) {
        return ORCA_SENSOR_INVALID_ARGUMENT;
    }
    const double* p = info->global_parameters;
    if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2]) ||
        p[0] <= 0.0 || p[1] <= 0.0 || p[2] < 0.0) {
        return ORCA_SENSOR_INVALID_ARGUMENT;
    }
    try {
        *instance = new TouchGridState{
            p[0],
            p[1],
            p[2],
            info->seed
        };
        return ORCA_SENSOR_OK;
    } catch (...) {
        // 只转换可捕获的 C++ 异常；这不是进程级故障隔离。
        return ORCA_SENSOR_PROVIDER_ERROR;
    }
}

// 重置实例而不改变创建参数；带噪声的实现还应在这里重置自己的随机状态。
OrcaSensorStatus ORCA_SENSOR_CALL Reset(void* instance, uint64_t seed) noexcept {
    if (!instance)
        return ORCA_SENSOR_INVALID_ARGUMENT;

    static_cast<TouchGridState*>(instance)->seed = seed;
    return ORCA_SENSOR_OK;
}

// 单次计算：input 指向 Orca 已按契约组装的 TouchGridInput；无需 DLL 查询场景。
// 输入、输出地址只在本次回调期间借用，DLL 不保存这些指针。
// 输出由 Host 预分配：连续 float64[4][4]、行优先、单位 N。
// 回调通过写入 output 返回数据，函数返回值仅表示成功或错误状态。
OrcaSensorStatus ORCA_SENSOR_CALL Compute(
    void* instance, const void* input, uint64_t input_size,
    double* output, uint64_t output_count) noexcept {
    if (!instance || !input || input_size != sizeof(TouchGridInput) ||
        !output || output_count != 16) {
        return ORCA_SENSOR_INVALID_ARGUMENT;
    }
    try {
        const auto& state = *static_cast<TouchGridState*>(instance);
        const auto& sample = *static_cast<const TouchGridInput*>(input);
        // 固定容量是 64 条；只遍历 contacts_count 条有效记录，不处理未使用的槽位。
        if (sample.contacts_count > 64)
            return ORCA_SENSOR_CAPACITY_EXCEEDED;

        std::fill(output, output + output_count, 0.0);
        for (uint32_t i = 0; i < sample.contacts_count; ++i) {
            const auto& contact = sample.contacts[i];
            // 契约要求接触位置和力处于 surface_frame 坐标系。
            const double x = contact.position[0];
            const double y = contact.position[1];
            if (!std::isfinite(x) || !std::isfinite(y))
                return ORCA_SENSOR_INVALID_ARGUMENT;
            if (std::abs(x) > state.extent_x * 0.5 ||
                std::abs(y) > state.extent_y * 0.5)
                continue;

            // 列沿 +X、行沿 +Y；正边界归入最后一格，区域外的接触点不计入。
            const int column = std::min(
                3, static_cast<int>((x / state.extent_x + 0.5) * 4));
            const int row = std::min(
                3, static_cast<int>((y / state.extent_y + 0.5) * 4));
            const double* normal = contact.normal_force;
            const double force = std::hypot(normal[0], normal[1], normal[2]);
            if (!std::isfinite(force))
                return ORCA_SENSOR_INVALID_ARGUMENT;

            // 逐点取模后相加，不是先将所有力矢量相加再取模；不使用切向力。
            const double scaled_force = state.gain * force;
            if (!std::isfinite(scaled_force))
                return ORCA_SENSOR_PROVIDER_ERROR;
            const double accumulated_force = output[row * 4 + column] + scaled_force;
            if (!std::isfinite(accumulated_force))
                return ORCA_SENSOR_PROVIDER_ERROR;
            output[row * 4 + column] = accumulated_force;
        }
        return ORCA_SENSOR_OK;
    } catch (...) {
        return ORCA_SENSOR_PROVIDER_ERROR;
    }
}

// 释放本 DLL 创建的实例状态；Host 不需要了解 TouchGridState 的内部布局。
void ORCA_SENSOR_CALL Destroy(void* instance) noexcept {
    delete static_cast<TouchGridState*>(instance);
}

// 类型描述符注册的是“型号”，不是挂载实例。回调字段保存实际函数地址。
// 以下字段顺序由公开 OrcaSensorTypeDescriptorV2 定义，不能任意调换。
// 静态描述符和引用的字符串在 DLL 卸载前始终有效。
const OrcaSensorTypeDescriptorV2 kTypes[] = {
    {
        sizeof(OrcaSensorTypeDescriptorV2),  // struct_size：描述符字节数。
        ORCA_SENSOR_ABI_VERSION,            // abi_version：C ABI 编号，不是 SDK 发布版本。
        "com.orca.examples.touch_grid",     // type_id：与 provider.json 对应的稳定型号 ID。
        TouchGridInput_contract_id,         // input_contract_id：生成的输入契约 ID。
        TouchGridInput_fingerprint,         // input_fingerprint：用于校验输入契约一致性。
        &TouchGridInput_layout,             // input_layout：生成的字段偏移、类型和容量。
        {
            sizeof(OrcaSensorOutputSpecV2), // output.struct_size：输出规格的字节数。
            2,                             // output.ndim：输出为二维数组。
            {4, 4, 0, 0}                   // output.shape：4×4；未使用的维度补 0。
        },
        3,                                 // global_parameter_count：创建参数数量。
        Create,                            // create：分配并返回独立实例 handle。
        Reset,                             // reset：重置该实例。
        Compute,                           // compute：读取本次输入并写入输出。
        Destroy,                           // destroy：释放该实例。
    }
};

// 厂商描述符列出此 DLL 提供的全部型号；本 DLL 只注册上面一个型号。
const OrcaSensorProviderDescriptorV2 kProvider = {
    sizeof(OrcaSensorProviderDescriptorV2),  // struct_size：厂商描述符字节数。
    ORCA_SENSOR_ABI_VERSION,                // abi_version：Host 校验的 C ABI 编号。
    "com.orca.examples",                    // provider_id：与清单一致的厂商包标识。
    "1.0.0",                               // provider_version：与清单一致的包版本。
    1,                                     // type_count：kTypes 中的型号数量。
    kTypes,                                // types：型号描述符数组的地址。
};

}  // namespace

// 所有厂商 DLL 使用同一个导出名称；Host 找到它后取得描述符和回调函数地址。
// extern "C" 保持 C 链接名称；此入口只返回描述符，不创建实例或执行计算。
extern "C" ORCA_SENSOR_EXPORT const OrcaSensorProviderDescriptorV2* ORCA_SENSOR_CALL
orca_sensor_get_provider(void) {
    return &kProvider;
}
