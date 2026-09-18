// SevenPad 的纯原生验收：手工输入接触矢量，验证合力、电容和实例独立噪声重放。
// 这不是物理场景，不验证接触采集、模型绑定或真实传感器精度。
#include "orca_sensor_host.h"
#include "seven_pad_input.h"

#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {

using Output = std::array<double, 11>;

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void check(OrcaSensorStatus status) {
    if (status != ORCA_SENSOR_OK)
        throw std::runtime_error(orca_sensor_host_last_error());
}

struct Session {
    OrcaSensorLibraryHandle library = 0;
    std::array<OrcaSensorInstanceHandle, 3> sensors{};

    ~Session() {
        for (auto sensor : sensors) {
            if (sensor)
                orca_sensor_host_destroy(sensor);
        }
        if (library)
            orca_sensor_host_close(library);
    }
};

Output read_output(OrcaSensorInstanceHandle sensor) {
    Output result{};
    check(orca_sensor_host_copy_output(sensor, result.data(), result.size()));
    for (double value : result)
        require(std::isfinite(value), "SevenPad output must be finite");
    return result;
}

void check_unready(OrcaSensorInstanceHandle sensor) {
    Output result{};
    require(orca_sensor_host_copy_output(sensor, result.data(), result.size())
                == ORCA_SENSOR_INVALID_STATE,
            "SevenPad output must not be readable before publication or after reset");
}

Output process_and_publish(OrcaSensorInstanceHandle sensor,
                           const OrcaSensorStepInputV2& input) {
    check(orca_sensor_host_process(sensor, &input));
    check(orca_sensor_host_publish_batch(&sensor, 1));
    return read_output(sensor);
}

void check_noiseless(const Output& actual) {
    // pad 0 内先抵消部分矢量；pad 0/1 的法向力再相互抵消，剩余 pad 6 的 2 N。
    // 总切向力为 (3,-4,0)，故模长 5 N、方向处于第四象限。
    const Output expected{
        0.04, 2.0, 5.0, 2.0 * std::acos(-1.0) + std::atan2(-4.0, 3.0),
        1.0 + 0.1 * std::sqrt(13.0), 1.0 + 0.1 * std::sqrt(20.0),
        1.0, 1.0, 1.0, 1.0, 1.2};
    for (size_t i = 0; i < expected.size(); ++i) {
        require(std::abs(actual[i] - expected[i]) < 1e-12,
                "SevenPad vector aggregation, direction or capacitance differs");
    }
}

void check_noise_channels(const Output& noisy, const Output& noiseless) {
    require(noisy[0] == noiseless[0], "Noise changed proximity");
    require(noisy[1] >= 0.0 && noisy[2] >= 0.0 &&
                noisy[3] >= 0.0 && noisy[3] < 2.0 * std::acos(-1.0),
            "Noisy force magnitudes or wrapped direction are invalid");
    for (size_t i = 4; i < noisy.size(); ++i)
        require(noisy[i] == noiseless[i], "Noise changed deterministic capacitance");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: orca_seven_pad_smoke /absolute/path/to/seven_pad_library\n";
        return 2;
    }
    try {
        Session session;
        check(orca_sensor_host_open(argv[1], &session.library));
        const auto* provider = orca_sensor_host_descriptor(session.library);
        require(provider && provider->abi_version == ORCA_SENSOR_ABI_VERSION &&
                    provider->type_count == 1,
                "Expected one ABI-2 SevenPad type");
        const auto& type = provider->types[0];
        require(std::strcmp(type.type_id, "com.orca.examples.seven_pad") == 0 &&
                    std::strcmp(type.input_contract_id, SevenPadInput_contract_id) == 0 &&
                    std::strcmp(type.input_fingerprint, SevenPadInput_fingerprint) == 0 &&
                    type.global_parameter_count == 3 && type.output.ndim == 1 &&
                    type.output.shape[0] == 11,
                "SevenPad identity, generated input contract or output shape differs");

        // 第一个实例关闭噪声；另两个实例使用相同参数和 seed，核对独立随机序列。
        for (size_t i = 0; i < session.sensors.size(); ++i) {
            const double parameters[] = {i == 0 ? 0.0 : 0.1, 1.0, 0.1};
            const OrcaSensorCreateInfoV2 info{sizeof(info), 42, 3, parameters};
            check(orca_sensor_host_create(
                session.library, type.type_id, &info, &session.sensors[i]));
            check_unready(session.sensors[i]);
        }

        double proximity = 0.04;
        uint32_t pad_index[] = {0, 0, 1, 6};
        const double normal[][3] = {{0, 0, 3}, {0, 0, -1}, {0, 0, -2}, {0, 0, 2}};
        const double tangent[][3] = {{4, 0, 0}, {-1, 0, 0}, {0, -4, 0}, {0, 0, 0}};
        // 记录数组按字段传入连续列，Host 用生成布局填充 contacts[i]。
        OrcaSensorFieldViewV2 fields[] = {
            {SevenPadInput_fields[0].field_id, ORCA_SENSOR_DTYPE_F64, 1, 1,
             sizeof(proximity), &proximity},
            {SevenPadInput_fields[1].field_id, ORCA_SENSOR_DTYPE_U32, 4, 1,
             sizeof(pad_index), pad_index},
            {SevenPadInput_fields[2].field_id, ORCA_SENSOR_DTYPE_F64, 4, 3,
             sizeof(normal), normal},
            {SevenPadInput_fields[3].field_id, ORCA_SENSOR_DTYPE_F64, 4, 3,
             sizeof(tangent), tangent},
        };
        const OrcaSensorStepInputV2 input{sizeof(input), 0.0, 0.002, 0, 4, fields};
        for (auto sensor : session.sensors) {
            check(orca_sensor_host_process(sensor, &input));
            check_unready(sensor);
        }
        check(orca_sensor_host_publish_batch(session.sensors.data(), 3));
        const auto noiseless = read_output(session.sensors[0]);
        check_noiseless(noiseless);
        const auto first_noisy = read_output(session.sensors[1]);
        require(first_noisy == read_output(session.sensors[2]),
                "Equal seeds and inputs must yield equal noise on independent instances");
        check_noise_channels(first_noisy, noiseless);

        // 保存连续两步，验证噪声确实推进；不假设不同 C++ 标准库生成同一组数值。
        const auto second_noisy = process_and_publish(session.sensors[1], input);
        require(second_noisy == process_and_publish(session.sensors[2], input),
                "Independent SevenPad noise sequences diverged");
        require(second_noisy != first_noisy, "SevenPad noise sequence did not advance");
        check_noise_channels(second_noisy, noiseless);

        // 只重置一个实例，完整重放两步；另一个实例的输出和后续随机序列应保持不变。
        check(orca_sensor_host_reset(session.sensors[1], 42));
        check_unready(session.sensors[1]);
        require(read_output(session.sensors[2]) == second_noisy,
                "Resetting SevenPad changed another instance's published output");
        require(process_and_publish(session.sensors[1], input) == first_noisy &&
                    process_and_publish(session.sensors[1], input) == second_noisy,
                "SevenPad reset failed to replay the noise sequence");
        const auto third_noisy = process_and_publish(session.sensors[1], input);
        require(third_noisy == process_and_publish(session.sensors[2], input),
                "Resetting SevenPad changed another instance's random state");
        check_noise_channels(third_noisy, noiseless);
        check(orca_sensor_host_reset(session.sensors[0], 42));
        check_unready(session.sensors[0]);
        require(process_and_publish(session.sensors[0], input) == noiseless,
                "Noiseless SevenPad reset changed the output");

        // 无效触面编号使 Host 进入故障态；恢复后仍可从原 seed 重放。
        pad_index[0] = 7;
        require(orca_sensor_host_process(session.sensors[1], &input)
                    == ORCA_SENSOR_PROVIDER_ERROR,
                "SevenPad must reject a pad index outside 0..6");
        check_unready(session.sensors[1]);
        pad_index[0] = 0;
        check(orca_sensor_host_reset(session.sensors[1], 42));
        require(process_and_publish(session.sensors[1], input) == first_noisy,
                "SevenPad failed to recover and replay after invalid input");

        // 未命中标记必须保留；空记录数组不读取任何接触槽位，力为零、电容为基线。
        proximity = -1.0;
        for (size_t i = 1; i < 4; ++i) {
            fields[i].count = 0;
            fields[i].byte_size = 0;
            fields[i].data = nullptr;
        }
        const auto unloaded = process_and_publish(session.sensors[0], input);
        require(unloaded[0] == -1.0 && unloaded[1] == 0.0 &&
                    unloaded[2] == 0.0 && unloaded[3] == 0.0,
                "Unloaded SevenPad force, direction or no-hit marker differs");
        for (size_t i = 4; i < unloaded.size(); ++i)
            require(unloaded[i] == 1.0, "Unloaded SevenPad capacitance is not the baseline");

        std::cout << "PASS: seven_pad vector aggregation, direction, capacitance, no-hit, "
                     "independent noise and reset/replay OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "seven_pad smoke failed: " << error.what() << '\n';
        return 1;
    }
}
