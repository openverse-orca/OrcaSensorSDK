// ContactGrid 与 Rangefinder 的纯原生接入验收：手工构造输入，不运行物理仿真。
// 这里验证契约、Host/DLL 调用和实例生命周期，不验证 site 绑定或真实传感器精度。
#include "orca_sensor_host.h"
#include "contact_grid_input.h"
#include "rangefinder_input.h"

#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {

// 任一断言或 Host 调用失败都抛出异常，由 main 返回非零退出码。
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void check(OrcaSensorStatus status) {
    if (status != ORCA_SENSOR_OK)
        throw std::runtime_error(orca_sensor_host_last_error());
}

struct Session {
    std::array<OrcaSensorLibraryHandle, 2> libraries{};
    std::array<OrcaSensorInstanceHandle, 4> sensors{};

    // 兜底清理：已显式释放的句柄会置零，异常路径仍释放其余实例和库。
    ~Session() {
        for (auto sensor : sensors) {
            if (sensor)
                orca_sensor_host_destroy(sensor);
        }
        for (auto library : libraries) {
            if (library)
                orca_sensor_host_close(library);
        }
    }
};

// 用生成头文件中的契约 ID 和指纹核对 DLL，避免只按名称判断输入布局一致。
void check_type(OrcaSensorLibraryHandle library, const char* type_id,
                const char* contract_id, const char* fingerprint, uint32_t ndim,
                uint32_t first_dimension, uint32_t second_dimension) {
    const auto* provider = orca_sensor_host_descriptor(library);
    require(provider && provider->abi_version == ORCA_SENSOR_ABI_VERSION &&
                provider->type_count == 1,
            "Expected one ABI-2 sample type per provider");
    const auto& type = provider->types[0];
    require(std::strcmp(type.type_id, type_id) == 0 &&
                std::strcmp(type.input_contract_id, contract_id) == 0 &&
                std::strcmp(type.input_fingerprint, fingerprint) == 0,
            "Provider type or generated input fingerprint differs");
    require(type.global_parameter_count == 1 && type.output.ndim == ndim &&
                type.output.shape[0] == first_dimension &&
                type.output.shape[1] == second_dimension,
            "Provider parameter count or output shape differs");
}

// copy_output 只读已发布结果，复制到调用方数组；不借用 DLL 的内部输出指针。
std::array<double, 16> read_grid(OrcaSensorInstanceHandle sensor) {
    std::array<double, 16> output{};
    check(orca_sensor_host_copy_output(sensor, output.data(), output.size()));
    return output;
}

double read_range(OrcaSensorInstanceHandle sensor) {
    double output = 0;
    check(orca_sensor_host_copy_output(sensor, &output, 1));
    return output;
}

void check_unready(const Session& session) {
    std::array<double, 16> output{};
    for (size_t i = 0; i < session.sensors.size(); ++i) {
        require(orca_sensor_host_copy_output(
                    session.sensors[i], output.data(), i < 2 ? 16 : 1) == ORCA_SENSOR_INVALID_STATE,
                "Output must not be readable before publication or after reset");
    }
}

// 非零网格只有两格：norm(3,4,0)=5 N，norm(0,0,-12)=12 N，合计 17 N。
// 两个 gain 分别为 1、2，因此总和为 17/34 N；逐格检查也验证行优先布局。
void check_results(const Session& session) {
    for (size_t i = 0; i < 2; ++i) {
        const auto output = read_grid(session.sensors[i]);
        for (size_t cell = 0; cell < output.size(); ++cell) {
            const double expected =
                (cell == 0 ? 5.0 : cell == 14 ? 12.0 : 0.0) * (i + 1);
            require(std::isfinite(output[cell]) && std::abs(output[cell] - expected) < 1e-12,
                    "ContactGrid force magnitude, layout or per-instance gain differs");
        }
    }

    // 相同 0.04 m 输入加各自的 0/0.01 m 偏差，期望距离为 0.04/0.05 m。
    const double left = read_range(session.sensors[2]);
    const double right = read_range(session.sensors[3]);
    require(std::isfinite(left) && std::isfinite(right) &&
                std::abs(left - 0.04) < 1e-12 && std::abs(right - 0.05) < 1e-12,
            "Rangefinder distance or per-instance bias differs");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr <<
            "Usage: orca_site_providers_smoke /absolute/contact_grid_library /absolute/rangefinder_library\n";
        return 2;
    }
    try {
        // 1. 打开两个可信厂商库，检查 Host ABI、型号、生成的输入指纹和输出形状。
        require(orca_sensor_host_abi_version() == ORCA_SENSOR_ABI_VERSION, "Host ABI mismatch");
        Session session;
        const char* type_ids[] = {
            "com.orca.examples.contact_grid", "com.orca.examples.rangefinder"};
        for (size_t i = 0; i < session.libraries.size(); ++i) {
            check(orca_sensor_host_open(argv[i + 1], &session.libraries[i]));
        }
        check_type(session.libraries[0], type_ids[0], ContactGridInput_contract_id,
                   ContactGridInput_fingerprint, 2, 4, 4);
        check_type(session.libraries[1], type_ids[1], RangefinderInput_contract_id,
                   RangefinderInput_fingerprint, 1, 1, 0);

        // 2. 每个型号创建两个实例：前两个使用 gain=1/2，后两个使用 bias_m=0/0.01。
        // 参数属于各自的实例；同一 DLL 的多个 handle 不应共享可变算法状态。
        const double parameters[] = {1.0, 2.0, 0.0, 0.01};
        for (size_t i = 0; i < session.sensors.size(); ++i) {
            const OrcaSensorCreateInfoV2 info{sizeof(info), 42, 1, &parameters[i]};
            check(orca_sensor_host_create(
                session.libraries[i / 2], type_ids[i / 2], &info, &session.sensors[i]));
        }

        // 错误型号不能在另一厂商库中创建句柄，也不能干扰已注册的型号。
        OrcaSensorInstanceHandle missing = 0;
        const OrcaSensorCreateInfoV2 info{sizeof(info), 42, 1, parameters};
        require(orca_sensor_host_create(session.libraries[0], type_ids[1], &info, &missing)
                    == ORCA_SENSOR_NOT_FOUND && missing == 0,
                "A type must resolve within its registered provider library");
        check_unready(session);

        // 3. 手工构造同一采样时刻的固定力网格和距离；这不是从真实场景采集的数据。
        ContactGridInput grid{};
        grid.sample_time = 0.125;
        grid.force_grid[0][0][0] = 3.0;
        grid.force_grid[0][0][1] = 4.0;
        grid.force_grid[3][2][2] = -12.0;
        RangefinderInput range{};
        range.sample_time = grid.sample_time;
        range.distance = 0.04;
        // 调用方提供连续的标准数据列，由 Host 按生成布局组装厂商结构。
        // 字段编号来自生成头文件；每项依次是编号、类型、记录数、分量数、字节数和数据指针。
        // 固定字段 count=1；4×4×3 网格展开为 48 个分量，内存只借用到 process 返回。
        const OrcaSensorFieldViewV2 grid_fields[] = {
            {ContactGridInput_fields[0].field_id, ORCA_SENSOR_DTYPE_F64, 1, 1,
             sizeof(grid.sample_time), &grid.sample_time},
            {ContactGridInput_fields[1].field_id, ORCA_SENSOR_DTYPE_F64, 1, 48,
             sizeof(grid.force_grid), grid.force_grid},
        };
        const OrcaSensorFieldViewV2 range_fields[] = {
            {RangefinderInput_fields[0].field_id, ORCA_SENSOR_DTYPE_F64, 1, 1,
             sizeof(range.sample_time), &range.sample_time},
            {RangefinderInput_fields[1].field_id, ORCA_SENSOR_DTYPE_F64, 1, 1,
             sizeof(range.distance), &range.distance},
        };
        const OrcaSensorStepInputV2 grid_input{
            sizeof(grid_input), grid.sample_time, 0.002, 0, 2, grid_fields};
        const OrcaSensorStepInputV2 range_input{
            sizeof(range_input), range.sample_time, 0.002, 0, 2, range_fields};

        // 4. 执行两轮相同输入；第二轮以相同 seed 重置，核对可重复结果与发布状态。
        for (uint32_t replay = 0; replay < 2; ++replay) {
            if (replay) {
                for (auto sensor : session.sensors)
                    check(orca_sensor_host_reset(sensor, 42));
                check_unready(session);
            }

            range.distance = 0.04;
            for (size_t i = 0; i < session.sensors.size(); ++i) {
                // process 只暂存结果，首次发布前 copy_output 仍必须返回未就绪。
                check(orca_sensor_host_process(
                    session.sensors[i], i < 2 ? &grid_input : &range_input));
            }
            check_unready(session);

            // 整批校验后发布四个实例，随后复制输出，检查每格力和每个距离。
            check(orca_sensor_host_publish_batch(session.sensors.data(), 4));
            check_results(session);

            // 只重置一个实例，不得改变同库另一个实例的状态和已发布输出。
            check(orca_sensor_host_reset(session.sensors[0], 99));
            require(read_grid(session.sensors[1])[14] == 24.0,
                    "Resetting one instance changed another instance");
            check(orca_sensor_host_process(session.sensors[0], &grid_input));
            check(orca_sensor_host_publish_batch(session.sensors.data(), 1));
            check_results(session);
        }

        // 5. 验证未命中标记：即使配置了正偏差，-1 也必须原样保留。
        range.distance = -1.0;
        for (size_t i = 2; i < 4; ++i)
            check(orca_sensor_host_process(session.sensors[i], &range_input));
        check(orca_sensor_host_publish_batch(session.sensors.data() + 2, 2));
        require(read_range(session.sensors[2]) == -1.0 && read_range(session.sensors[3]) == -1.0,
                "Rangefinder must preserve the no-hit marker");

        // 6. 库句柄可以先关闭；存活实例仍持有库资源，后续计算和结果读取必须可用。
        for (auto& library : session.libraries) {
            check(orca_sensor_host_close(library));
            library = 0;
        }
        check(orca_sensor_host_process(session.sensors[1], &grid_input));
        check(orca_sensor_host_publish_batch(session.sensors.data() + 1, 1));
        require(read_grid(session.sensors[1])[14] == 24.0,
                "Live instance lost its provider library");

        // 实例销毁后，旧句柄必须失效，不能再读取其结果。
        for (auto& sensor : session.sensors) {
            const auto old_handle = sensor;
            check(orca_sensor_host_destroy(sensor));
            sensor = 0;
            double value = 0;
            require(orca_sensor_host_copy_output(old_handle, &value, 1) == ORCA_SENSOR_NOT_FOUND,
                    "Destroyed handle must be invalid");
        }

        // 所有实例释放后重新打开并关闭两个库，验证注册资源可完整清理和重新使用。
        for (size_t i = 0; i < session.libraries.size(); ++i) {
            check(orca_sensor_host_open(argv[i + 1], &session.libraries[i]));
            require(orca_sensor_host_descriptor(session.libraries[i]) != nullptr,
                    "Provider failed to reopen");
            check(orca_sensor_host_close(session.libraries[i]));
            session.libraries[i] = 0;
        }
        std::cout << "PASS: contact_grid=17/34 N; rangefinder=0.04/0.05 m; "
                     "two instances per provider, no-hit, reset/replay and lifecycle OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "site provider smoke failed: " << error.what() << '\n';
        return 1;
    }
}
