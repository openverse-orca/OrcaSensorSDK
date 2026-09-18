// Original educational sample, MIT licensed. No calibrated product model.
// 原创七触垫教学样板：矢量合力、实例独立噪声和线性电容近似响应。
// 不表示任何真实产品的标定算法；raw 电容通道不是法拉值或真实电气仿真。
#include "orca_sensor_abi.h"
// 由本型号的 contract.json 生成：输入结构、契约 ID、指纹和内存布局。
#include "seven_pad_input.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace {

constexpr uint32_t kPadCount = 7;           // 对应 force_f1 到 force_f7，索引为 0 到 6。
constexpr uint32_t kContactCapacity = 1024;  // 输入接触数组的固定容量，不是每帧有效数量。
constexpr uint64_t kOutputCount = 11;       // 距离 + 法向力 + 切向力 + 方向 + 7 路 raw 电容。
constexpr double kTwoPi = 6.283185307179586476925286766559;

// Create 为每个实例分配独立状态，handle 指向本结构，不共享参数或随机序列。
// 参数在创建时复制；不保存 Host 借出的 info 或 global_parameters 指针。
struct SevenPadState {
    double noise_scale;        // 合力各分量的高斯噪声标准差，单位 N。
    double capacitance_base;   // 各触垫 raw 通道的基线。
    double capacitance_gain;   // 各触垫合力大小到 raw 值的线性系数。
    std::mt19937_64 random;     // 由实例 seed 初始化的随机引擎。
    // 标准正态分布还保存自身的采样缓存，所以 Reset 需要同时清除它。
    std::normal_distribution<double> gaussian{
        0.0,
        1.0
    };
};

// 输入与累加中间结果都必须是有限的三维矢量。
bool FiniteVector(const double* vector) noexcept {
    return std::isfinite(vector[0]) &&
           std::isfinite(vector[1]) &&
           std::isfinite(vector[2]);
}

// 矢量的欧氏长度；此处不是对各个接触点的标量力逐项相加。
double Magnitude(const double* vector) noexcept {
    return std::hypot(vector[0], vector[1], vector[2]);
}

// global_parameters 的顺序与 provider.json 完全一致：
// [0] noise_scale、[1] capacitance_base、[2] capacitance_gain；seed 单独传入。
// 创建失败时保持 *instance 为 nullptr，C++ 异常不能跨越 C ABI。
OrcaSensorStatus ORCA_SENSOR_CALL Create(
    const OrcaSensorCreateInfoV2* info, void** instance) noexcept {
    if (!instance)
        return ORCA_SENSOR_INVALID_ARGUMENT;

    *instance = nullptr;
    if (!info || info->struct_size != sizeof(*info) ||
        info->global_parameter_count != 3 || !info->global_parameters) {
        return ORCA_SENSOR_INVALID_ARGUMENT;
    }
    for (uint32_t i = 0; i < 3; ++i) {
        const double parameter = info->global_parameters[i];
        if (!std::isfinite(parameter) || parameter < 0.0 || parameter > 1000000.0) {
            return ORCA_SENSOR_INVALID_ARGUMENT;
        }
    }
    try {
        *instance = new SevenPadState{
            info->global_parameters[0],
            info->global_parameters[1],
            info->global_parameters[2],
            std::mt19937_64(info->seed)
        };
        return ORCA_SENSOR_OK;
    } catch (...) {
        return ORCA_SENSOR_PROVIDER_ERROR;
    }
}

// 同时重置随机引擎和正态分布缓存，保留创建参数。
// 同一实现、相同 seed 和相同输入序列可重放；不承诺跨不同标准库的噪声逐位一致。
OrcaSensorStatus ORCA_SENSOR_CALL Reset(void* instance, uint64_t seed) noexcept {
    if (!instance)
        return ORCA_SENSOR_INVALID_ARGUMENT;

    auto& state = *static_cast<SevenPadState*>(instance);
    state.random.seed(seed);
    state.gaussian.reset();
    return ORCA_SENSOR_OK;
}

// Orca 按契约组装 SevenPadInput：距离 proximity 和 contacts_count 条有效接触。
// 接触力已转换到共同的 range_frame，pad_index 对应契约中的七个对象顺序。
// Host 预分配连续 float64[11]：
//   [0] 距离 m（-1 未命中）；[1] 法向合力 N；[2] 切向合力 N；
//   [3] 切向合力的 XY 方向 rad；[4..10] 七个触垫的 raw 电容近似值。
// 输入/输出指针只在回调期间借用；写入 output 返回数据，函数返回状态码。
OrcaSensorStatus ORCA_SENSOR_CALL Compute(
    void* instance, const void* input, uint64_t input_size,
    double* output, uint64_t output_count) noexcept {
    if (!instance || !input || input_size != sizeof(SevenPadInput) ||
        !output || output_count != kOutputCount) {
        return ORCA_SENSOR_INVALID_ARGUMENT;
    }
    try {
        auto& state = *static_cast<SevenPadState*>(instance);
        const auto& sample = *static_cast<const SevenPadInput*>(input);
        if (!std::isfinite(sample.proximity) ||
            (sample.proximity != -1.0 && (sample.proximity < 0.0 || sample.proximity > 0.1))) {
            return ORCA_SENSOR_INVALID_ARGUMENT;
        }
        if (sample.contacts_count > kContactCapacity)
            return ORCA_SENSOR_CAPACITY_EXCEEDED;

        // 第一步：每个触垫分别累加法向和切向矢量，不先取模。
        double normals[kPadCount][3]{};
        double tangents[kPadCount][3]{};
        for (uint32_t i = 0; i < sample.contacts_count; ++i) {
            const auto& contact = sample.contacts[i];
            if (contact.pad_index >= kPadCount ||
                !FiniteVector(contact.normal) || !FiniteVector(contact.tangent)) {
                return ORCA_SENSOR_INVALID_ARGUMENT;
            }
            for (uint32_t axis = 0; axis < 3; ++axis) {
                normals[contact.pad_index][axis] += contact.normal[axis];
                tangents[contact.pad_index][axis] += contact.tangent[axis];
            }
            if (!FiniteVector(normals[contact.pad_index]) ||
                !FiniteVector(tangents[contact.pad_index])) {
                return ORCA_SENSOR_PROVIDER_ERROR;
            }
        }

        // 第二步：组合各触垫合力，并计算不加噪声的教学电容响应。
        // 暂存全部输出，检查成功前不写入 Host 的缓冲区。
        double values[kOutputCount]{};
        values[0] = sample.proximity;
        double total_normal[3]{};
        double total_tangent[3]{};
        for (uint32_t pad = 0; pad < kPadCount; ++pad) {
            double pad_force[3]{};
            for (uint32_t axis = 0; axis < 3; ++axis) {
                total_normal[axis] += normals[pad][axis];
                total_tangent[axis] += tangents[pad][axis];
                pad_force[axis] = normals[pad][axis] + tangents[pad][axis];
            }
            if (!FiniteVector(total_normal) ||
                !FiniteVector(total_tangent) ||
                !FiniteVector(pad_force)) {
                return ORCA_SENSOR_PROVIDER_ERROR;
            }
            // raw = 基线 + 系数 × |该触垫的法向矢量和 + 切向矢量和|。
            // 这是刻意简化的线性函数，不是经过标定的电容测量模型。
            values[4 + pad] = state.capacitance_base +
                              state.capacitance_gain * Magnitude(pad_force);
            if (!std::isfinite(values[4 + pad]))
                return ORCA_SENSOR_PROVIDER_ERROR;
        }

        // 第三步：先复制随机状态，再给法向/切向总矢量的各分量独立加噪声。
        // 计算失败不推进实例的随机序列；noise_scale 为 0 时不抽样。
        auto next_random = state.random;
        auto next_gaussian = state.gaussian;
        if (state.noise_scale != 0.0) {
            for (uint32_t axis = 0; axis < 3; ++axis) {
                total_normal[axis] += state.noise_scale * next_gaussian(next_random);
            }
            for (uint32_t axis = 0; axis < 3; ++axis) {
                total_tangent[axis] += state.noise_scale * next_gaussian(next_random);
            }
        }
        if (!FiniteVector(total_normal) || !FiniteVector(total_tangent))
            return ORCA_SENSOR_PROVIDER_ERROR;

        // 合力输出为加噪声后的矢量模；方向是切向合力在 XY 平面的方位角。
        values[1] = Magnitude(total_normal);
        values[2] = Magnitude(total_tangent);
        if (total_tangent[0] != 0.0 || total_tangent[1] != 0.0) {
            values[3] = std::atan2(total_tangent[1], total_tangent[0]);
            if (values[3] < 0.0)
                values[3] += kTwoPi;
            // 归一化到 [0, 2π)，排除浮点舍入产生的右端点；XY 零矢量保持方向为 0。
            if (values[3] >= kTwoPi)
                values[3] = 0.0;
        }
        for (double value : values) {
            if (!std::isfinite(value))
                return ORCA_SENSOR_PROVIDER_ERROR;
        }
        // 最后一起提交输出和随机状态，避免失败留下半更新的实例。
        std::copy(values, values + kOutputCount, output);
        state.random = next_random;
        state.gaussian = next_gaussian;
        return ORCA_SENSOR_OK;
    } catch (...) {
        return ORCA_SENSOR_PROVIDER_ERROR;
    }
}

// 释放实例及其随机状态；Host 不需要了解 SevenPadState 的内部布局。
void ORCA_SENSOR_CALL Destroy(void* instance) noexcept {
    delete static_cast<SevenPadState*>(instance);
}

// 注册型号的契约、输出规格和函数地址；每次 Create 才产生一个独立算法实例。
// 初始化顺序与公开 OrcaSensorTypeDescriptorV2 一致，静态存储在 DLL 卸载前有效。
const OrcaSensorTypeDescriptorV2 kTypes[] = {
    {
        sizeof(OrcaSensorTypeDescriptorV2), // struct_size：型号描述符字节数。
        ORCA_SENSOR_ABI_VERSION,           // abi_version：C ABI 编号，不是 SDK 发布版本。
        "com.orca.examples.seven_pad",     // type_id：清单中的稳定型号 ID。
        SevenPadInput_contract_id,         // input_contract_id：生成的输入契约 ID。
        SevenPadInput_fingerprint,         // input_fingerprint：校验输入契约一致性。
        &SevenPadInput_layout,             // input_layout：生成的字段偏移、类型和容量。
        {
            sizeof(OrcaSensorOutputSpecV2), // output.struct_size：输出规格字节数。
            1,                             // output.ndim：一维输出。
            {11, 0, 0, 0}                  // output.shape：[11]；各通道单位见契约。
        },
        3,                                // global_parameter_count：三个创建参数。
        Create,                           // create：分配实例并按 seed 初始化随机状态。
        Reset,                            // reset：重置随机引擎和分布缓存。
        Compute,                          // compute：读取输入、写入输出并推进随机状态。
        Destroy,                          // destroy：释放实例。
    }
};

// 厂商描述符列出 DLL 提供的型号，本样板只包含一个七触垫型号。
const OrcaSensorProviderDescriptorV2 kProvider = {
    sizeof(OrcaSensorProviderDescriptorV2), // struct_size：厂商描述符字节数。
    ORCA_SENSOR_ABI_VERSION,                // abi_version：Host 校验的 C ABI 编号。
    "com.orca.examples.seven_pad",          // provider_id：与清单一致的厂商包标识。
    "1.0.0",                               // provider_version：厂商包版本。
    1,                                     // type_count：型号数量。
    kTypes,                                // types：型号描述符数组地址。
};

}  // namespace

// 统一导出入口：Host 按固定名称查找它，再从描述符取得型号和回调函数地址。
// extern "C" 保持 C 链接名称；只返回描述符，不创建实例或执行计算。
extern "C" ORCA_SENSOR_EXPORT const OrcaSensorProviderDescriptorV2* ORCA_SENSOR_CALL
orca_sensor_get_provider(void) {
    return &kProvider;
}
