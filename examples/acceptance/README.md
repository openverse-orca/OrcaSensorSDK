# 原生接入验证示例

本目录提供三个验收程序，验证随仓库提供的**预编译 Host + 厂商样板动态库**，
无需安装 OrcaGym、Python 或运行机器人仿真。这里只手工构造输入，验证调用链，
不验证真实场景的数据采集、模型绑定或传感器精度。

## 构建并验证

以下命令在 **SDK 仓库根目录**执行，需要 CMake/CTest 3.20+ 和支持 C++17 的
C/C++ 编译工具链。当前随包二进制仅完成 Linux x86_64 验收；其他平台需要对应的
Host 与契约工具，不能直接使用 Linux 二进制。请保留 `bin/linux-x86_64/` 下工具的
执行权限及完整 `_internal/` 目录，不要混用不同版本的文件。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

预期三个测试均通过。构建会用随包契约工具生成输入头文件，编译四个厂商样板和
三个验收程序；Host 直接使用 `lib/linux-x86_64/` 中的预编译库，不编译其内部源码。

## 最小调用链：TouchGrid

阅读 [smoke.cpp](smoke.cpp)，依次了解：加载动态库并读取已注册型号、创建两个实例、
用 `OrcaSensorFieldViewV2` 传入一条 3 N 接触记录、`process` 调用厂商 `compute`
并暂存结果、`publish_batch` 发布、`copy_output` 将输出复制到调用方数组，最后重置并重放。
两个实例的 `gain` 分别为 1、2，因此各自 4×4 网格的总和应为 3 N、6 N。
程序还直接调用厂商回调，验证缩放或累加溢出由算法自身返回错误，而非仅依靠 Host 拦截无效输出。

也可从 SDK 根目录单独运行：

```bash
./build/orca_touch_grid_smoke "$PWD/build/providers/touch_grid/liborca_touch_grid.so"
```

Host 要求动态库使用绝对路径，因此参数使用 `$PWD`，不能直接传 `./build/providers/...`。
成功时退出码为 0，输出：

```text
touch_grid: left=3 N, right=6 N; reset/replay and overflow rejection OK
```

## 两类厂商库与生命周期验证

[site_providers_smoke.cpp](site_providers_smoke.cpp) 同时验证 ContactGrid 与 Rangefinder：
每个型号创建两个不同参数的实例，核对契约指纹、发布前不可读取、实例间状态独立、
未命中标记 `-1`、重置重放，以及库关闭后存活实例与销毁后的无效句柄。

```bash
./build/orca_site_providers_smoke \
  "$PWD/build/providers/contact_grid/liborca_contact_grid.so" \
  "$PWD/build/providers/rangefinder/liborca_rangefinder.so"
```

成功时退出码为 0，输出：

```text
PASS: contact_grid=17/34 N; rangefinder=0.04/0.05 m; two instances per provider, no-hit, reset/replay and lifecycle OK
```

## 七触面输出与噪声重放

[seven_pad_smoke.cpp](seven_pad_smoke.cpp) 验证七触面的 11 通道输出和实例噪声，
包括同一触面内和跨触面的矢量抵消、切向方向、每触面电容、空接触和未命中标记。
它还检查各实例的随机序列独立，并核对相同 seed（随机种子）下 reset 后连续两步的重放结果。
在 SDK 根目录单独运行：

```bash
./build/orca_seven_pad_smoke "$PWD/build/providers/seven_pad/liborca_seven_pad.so"
```

成功时退出码为 0，输出：

```text
PASS: seven_pad vector aggregation, direction, capacitance, no-hit, independent noise and reset/replay OK
```

任一验收程序检查失败时，输出错误信息并以非零退出码结束。
