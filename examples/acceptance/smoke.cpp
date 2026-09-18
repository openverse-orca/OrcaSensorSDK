// TouchGrid 的纯原生接入验收：手工构造输入，检查 Host 与厂商 DLL 的完整调用链。
// 这不是实际物理场景，不验证接触采集、模型绑定或真实传感器精度。
#include "orca_sensor_host.h"
#include "touch_grid_input.h"

#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace {

// 将 Host 错误状态转为异常，使验收失败时返回非零退出码。
void check(OrcaSensorStatus status) {
    if (status != ORCA_SENSOR_OK) {
        throw std::runtime_error(orca_sensor_host_last_error());
    }
}

struct Session {
    OrcaSensorLibraryHandle library = 0;
    std::array<OrcaSensorInstanceHandle, 2> sensors{};

    // 正常结束和异常退出都先销毁实例，再关闭厂商库。
    ~Session() {
        for (auto sensor : sensors) {
            if (sensor)
                orca_sensor_host_destroy(sensor);
        }
        if (library)
            orca_sensor_host_close(library);
    }
};

// 直接调用 DLL 的回调，确保溢出由样板报告，不能依赖 Host 的输出检查兜底。
// 私有实例同样由创建它的 DLL 释放，且必须先于 Session 关闭库。
struct CallbackInstance {
    const OrcaSensorTypeDescriptorV2& type;
    void* state = nullptr;

    ~CallbackInstance() {
        if (state)
            type.destroy(state);
    }
};

void check_overflow(const OrcaSensorTypeDescriptorV2& type) {
    for (const double gain : {1000.0, 1.0}) {
        const double parameters[] = {0.06, 0.06, gain};
        const OrcaSensorCreateInfoV2 info{sizeof(info), 42, 3, parameters};
        CallbackInstance instance{type};
        if (type.create(&info, &instance.state) != ORCA_SENSOR_OK || !instance.state)
            throw std::runtime_error("TouchGrid callback instance creation failed");

        TouchGridInput sample{};
        // gain=1000 时单次乘法溢出；gain=1 时每项有限，但三次同格累加溢出。
        sample.contacts_count = gain == 1000.0 ? 1 : 3;
        for (uint32_t i = 0; i < sample.contacts_count; ++i)
            sample.contacts[i].normal_force[2] = std::numeric_limits<double>::max() / 2.0;
        std::array<double, 16> output{};
        if (type.compute(instance.state, &sample, sizeof(sample), output.data(), output.size())
                != ORCA_SENSOR_PROVIDER_ERROR) {
            throw std::runtime_error(gain == 1000.0
                ? "TouchGrid callback must reject force scaling overflow"
                : "TouchGrid callback must reject cell accumulation overflow");
        }

        // 同一回调实例随后处理普通有限输入，确认边界检查未误拒正常计算。
        for (uint32_t i = 0; i < sample.contacts_count; ++i)
            sample.contacts[i].normal_force[2] = 2.0;
        if (type.compute(instance.state, &sample, sizeof(sample), output.data(), output.size())
                != ORCA_SENSOR_OK) {
            throw std::runtime_error("TouchGrid callback rejected finite force values");
        }
        for (size_t cell = 0; cell < output.size(); ++cell) {
            const double expected = cell == 10 ? 2.0 * sample.contacts_count * gain : 0.0;
            if (!std::isfinite(output[cell]) || output[cell] != expected)
                throw std::runtime_error("TouchGrid callback produced unexpected finite output");
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: orca_touch_grid_smoke /absolute/path/to/touch_grid_library\n";
        return 2;
    }
    try {
        // 1. 打开可信厂商库，按 type_id 找到型号并核对参数数量和输出形状。
        Session session;
        check(orca_sensor_host_open(argv[1], &session.library));
        const auto* provider = orca_sensor_host_descriptor(session.library);
        if (!provider)
            throw std::runtime_error(orca_sensor_host_last_error());

        const char* type_id = "com.orca.examples.touch_grid";
        const OrcaSensorTypeDescriptorV2* type = nullptr;
        for (uint32_t i = 0; i < provider->type_count; ++i) {
            if (std::strcmp(provider->types[i].type_id, type_id) == 0) {
                type = &provider->types[i];
                break;
            }
        }
        if (!type || type->global_parameter_count != 3 || type->output.ndim != 2 ||
            type->output.shape[0] != 4 || type->output.shape[1] != 4) {
            throw std::runtime_error(
                "Expected bundled touch_grid type, 3 global_parameters and 4x4 output");
        }
        if (std::strcmp(type->input_contract_id, TouchGridInput_contract_id) != 0 ||
            std::strcmp(type->input_fingerprint, TouchGridInput_fingerprint) != 0) {
            throw std::runtime_error("TouchGrid generated input contract differs");
        }
        check_overflow(*type);

        // 2. 同一型号创建两个独立实例：extent_x/extent_y 均为 0.06 m，gain 分别为 1、2。
        // 参数按清单顺序传入；相同 seed 用于随后验证 reset/replay。
        for (size_t i = 0; i < session.sensors.size(); ++i) {
            const double global_parameters[] = {0.06, 0.06, static_cast<double>(i + 1)};
            const OrcaSensorCreateInfoV2 info{sizeof(info), 42, 3, global_parameters};
            check(orca_sensor_host_create(
                session.library, type_id, &info, &session.sensors[i]));
        }

        // 3. 手工构造一条触面中心的接触：法向力为 (0, 0, -3) N，切向力为零。
        // 以下各列内存属于调用方，只需在 process 返回前保持有效。
        const double sample_time = 0.0;
        const uint32_t pad_index[] = {0};
        const uint64_t object_id[] = {7};
        const double position[][3] = {{0.0, 0.0, 0.0}};
        const double normal[][3] = {{0.0, 0.0, -3.0}};
        const double tangent[][3] = {{0.0, 0.0, 0.0}};
        // 字段编号仅属于本契约，按声明顺序对应时间、触面编号、对方编号、位置和两种力。
        // 每项依次是 field_id、dtype、记录数、每条分量数、字节数、数据指针；不使用全局数据源。
        const OrcaSensorFieldViewV2 fields[] = {
            {1, ORCA_SENSOR_DTYPE_F64, 1, 1, sizeof(sample_time), &sample_time},
            {2, ORCA_SENSOR_DTYPE_U32, 1, 1, sizeof(pad_index), pad_index},
            {3, ORCA_SENSOR_DTYPE_U64, 1, 1, sizeof(object_id), object_id},
            {4, ORCA_SENSOR_DTYPE_F64, 1, 3, sizeof(position), position},
            {5, ORCA_SENSOR_DTYPE_F64, 1, 3, sizeof(normal), normal},
            {6, ORCA_SENSOR_DTYPE_F64, 1, 3, sizeof(tangent), tangent},
        };
        const OrcaSensorStepInputV2 input{sizeof(input), 0.0, 0.002, 0, 6, fields};
        std::array<std::array<double, 16>, 2> first_outputs{};

        // 4. 首次计算后，用相同 seed 重置并重放相同输入，核对全部 16 个输出值。
        for (uint32_t replay = 0; replay < 2; ++replay) {
            for (auto sensor : session.sensors) {
                if (replay)
                    check(orca_sensor_host_reset(sensor, 42));

                // process 组装厂商输入、调用算法并暂存结果，此时尚未发布。
                check(orca_sensor_host_process(sensor, &input));
            }

            // 整批发布两个实例后，将结果复制到调用方拥有的连续数组。
            check(orca_sensor_host_publish_batch(session.sensors.data(), 2));
            for (size_t i = 0; i < session.sensors.size(); ++i) {
                std::array<double, 16> output{};
                check(orca_sensor_host_copy_output(
                    session.sensors[i], output.data(), output.size()));
                const double total = std::accumulate(output.begin(), output.end(), 0.0);

                // 输入法向力模长为 3 N，乘各自 gain 后，网格总和应分别为 3 N、6 N。
                // 第二轮不仅检查总和，还要求逐格结果与首次计算完全一致。
                if (std::abs(total - 3.0 * (i + 1)) > 1e-12 ||
                    (replay && output != first_outputs[i])) {
                    throw std::runtime_error("Unexpected tactile output or non-deterministic replay");
                }
                first_outputs[i] = output;
            }
        }
        std::cout << "touch_grid: left=3 N, right=6 N; reset/replay and overflow rejection OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "touch_grid smoke failed: " << error.what() << '\n';
        return 1;
    }
}
