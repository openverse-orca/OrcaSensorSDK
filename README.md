# Orca Sensor Provider SDK

本 SDK 用于把厂商的传感器算法接入 Orca。你负责定义算法需要的输入、实现计算并交付型号包；Orca 从仿真场景采集数据，再调用你的算法。

按你现在想做的事选择入口：

| 你想做什么 | 从哪里开始 |
| --- | --- |
| 编写、验证自己的传感器算法 | 先执行下面的三条构建命令，再选择一个[算法样板](#选择接近你需求的样板) |
| 先看传感器在完整五指灵巧手上的运行效果 | 跳到[完整灵巧手演示](#查看完整灵巧手演示)，在 OrcaPlayground 中运行 |

## 三条命令构建并验证

完整 SDK 已包含构建工具、公共 C 头文件、四个算法样板和预编译 Host。Host 是负责加载算法动态库、传递输入和取得输出的运行库。

当前平台为 **Linux x86_64、glibc 2.35+**，需要 CMake/CTest 3.20+ 和 C++17 编译器。下载或 clone 完整 SDK 后，在仓库根目录执行：

```bash
cmake -S . -B build/sensor-sdk -DCMAKE_BUILD_TYPE=Release
cmake --build build/sensor-sdk --config Release
ctest --test-dir build/sensor-sdk -C Release --output-on-failure
```

这会构建四个样板动态库并运行三个验收程序，预期 **3 项测试全部通过**。测试使用手工准备的输入，验证加载、计算、多实例隔离和重置重放，无需安装 Python、OrcaGym 或启动机器人仿真。详细结果见[原生验收说明](examples/acceptance/README.md)。

样板运行通过后，按[第 5 节](#5-构建并交付自己的型号包)开发自己的型号包；需要确定算法能取得哪些数据时，查阅[厂商输入能力手册](docs/capabilities/index.md)。

### 选择接近你需求的样板

| 你希望算法收到什么 | 样板及其计算方式 |
| --- | --- |
| Orca 已经汇总好的接触力网格 | [ContactGrid](examples/contact_grid/)：输入 `4×4×3` 力向量，输出每格合力的模长 |
| 从场景指定位置测得的距离 | [Rangefinder](examples/rangefinder/)：输入一个距离，添加示例偏差后输出 |
| 一块触面上的逐条接触位置和力 | [TouchGrid](examples/touch_grid/)：算法自行划分 `4×4` 网格并累加法向力大小 |
| 多块触面的接触力，以及一个测距值 | [七触面](examples/seven_pad/)：合力计算、实例噪声和简化电容响应 |

首次接入可先读 [ContactGrid 算法](examples/contact_grid/contact_grid.cpp)。这些样板展示接入方法，实际产品的响应模型和标定算法由厂商实现。

### 查看完整灵巧手演示

完整示例位于 [OrcaPlayground 官方仓库](https://github.com/openverse-orca/OrcaPlayground)的 **`examples/euler/sensor_provider/`** 目录，包含五指手部模型、物理场景、控制脚本、预编译型号包和实时图表。它将上述算法样板用到完整机器人上；你可以先运行它，了解输入采集、算法计算和读数展示如何配合。

同一只灵巧手提供两种展示方式：

| 运行模式 | 使用哪些 SDK 样板 | 你会看到什么 |
| --- | --- | --- |
| `hand` | 每指一个 SevenPad，五指共五个实例、35 块触面 | 五指各自的法向力、切向力曲线 |
| `hand_grid` | 每指一个 ContactGrid 和一个 Rangefinder，共十个实例 | 五指的接触力网格和测距 |

运行演示需要**支持传感器插件的官方 OrcaGym 发布版本**。按该目录的 `README.md` 安装示例依赖并取得模型资源；Host 随 OrcaGym 提供，型号包随演示提供，无需先编译 SDK。

准备完成后，在 **OrcaPlayground 仓库根目录**、`orca` 环境中选择一个入口运行：

```bash
# 五指受力曲线。
python -m examples.euler.sensor_provider.run --example hand

# 五指接触力网格和测距。
python -m examples.euler.sensor_provider.run --example hand_grid
```

两个入口都推进完整手部的物理场景，并显示实时图表。默认运行 10 秒仿真，结束后保留图表，关闭窗口退出。读数来自算法动态库；接触由模型与控制过程决定，示例不保证每根手指持续接触或抓取成功。

两种方式共用 `examples/euler/sensor_provider/scenes/dexhand/scene.xml`。查看安装位置、触面和测量 site 的绑定时，阅读该场景目录的 `README.md`；复制场景时一起保留相邻的 `meshes/` 资源。无界面运行与保存图表的方法见演示主目录的 `README.md`。

### 文档中的几个名称

| 名称 | 含义 |
| --- | --- |
| 型号包 | 可交付的文件目录，以 `provider.json` 为入口，包含契约、模型和算法动态库 |
| 实例 | 某个型号在场景中的一次使用；同一型号可以有多个实例，各自保存参数、输入输出和算法状态 |
| 契约 | `contract.json` 中的输入需求和输出说明；构建工具据此生成算法读取的 C 输入结构 |
| `site` | 模型中的位置和朝向标记，可作为安装基准或测量参考；标记本身不产生传感器读数 |

本文使用“动态库”指编译后的算法文件；当前 Linux 包使用 `.so`，Windows 对应 `.dll`。公共 C 接口见 [orca_sensor_abi.h](include/orca_sensor_abi.h)；只有编写 Host 调用代码时才需要查阅 [orca_sensor_host.h](include/orca_sensor_host.h)。

## 1. 一个型号包包含什么

| 交付项 | ContactGrid 文件 | 范围 |
| --- | --- | --- |
| 包入口（必需） | [provider.json](examples/contact_grid/provider.json) | 型号标识、文件路径、包版本和实例参数 |
| 型号资产 | [model.xml](examples/contact_grid/model.xml) | 外观、物理结构、安装基准 |
| 输入输出契约 | [contract.json](examples/contact_grid/contract.json) | 需要哪些物理量、输出布局与各通道含义 |
| 算法动态库 | `liborca_contact_grid.so` / `orca_contact_grid.dll`，源码见 [contact_grid.cpp](examples/contact_grid/contact_grid.cpp) | 类型描述符注册、实例状态、计算、重置与释放 |
| 可选展示配置 | ContactGrid 不要求；可参考七触面的 [presentation.json](examples/seven_pad/presentation.json) | 通用数值、曲线及触面着色规则，不参与计算 |

`provider.json` 是包入口，以 `type_id` 关联以上内容，使用 `contract_schema: "contract.json"` 指向唯一的输入输出契约；每实例的算法全局配置由 `global_parameters` 定义。还应随包提供运行库、算法资源和许可证。

## 2. 定义型号外形与安装基准

`model.xml` 定义传感器的外形、物理结构和安装基准，使用 MuJoCo 的 MJCF 模型格式，长度单位为米。其中 `body` 是刚体节点，`geom` 是用于碰撞或显示的几何形状，`site` 是位置和朝向标记。型号 XML 不包含原生 `sensor`、rangefinder 或 plugin；需要哪些测量数据由契约声明。

包入口中每个型号引用模型文件和安装 site：

```json
"model": {"asset": "model.xml", "mount_site": "sensor_base"}
```

`mount_site` 明确是安装 site 的名称，其局部位置和姿态在 XML 的 `sensor_base` site 定义。它说明资产如何安装，不等于强制所有查询都用它。

ContactGrid、Rangefinder 的输入由场景直接绑定测量 site，契约不引用型号内对象；型号资产可以用于放置外形，也可以在已经具有物理结构和 site 的场景上声明算法实例。场景加载不会替用户补装模型，几何与测量 site 须已存在，详见[场景绑定](#scene-site-binding)。

七触面、TouchGrid 的输入引用型号内部固定名称，不包含实例前缀。以七触面为例，`range_frame` 是查询参考系，不负责安装，也不会自行产生测距值。Orca 根据契约所需能力和 XML 中的精确名称生成内部对象索引，厂商不维护另一份对象清单：

| 契约引用 | XML 中的对象 | 解析约定 |
| --- | --- | --- |
| 接触输入的 `force_f4` | 同名 body 或 geom | 必须唯一；body 采集其直属 geom 的接触 |
| 测距输入的 `range_frame` | 同名参考系，本例为 site | 由 Orca 在该参考系执行射线查询 |
| 旋转或坐标系引用 | 同名 body、geom 或 site | 按所需能力选择可用类型，必须唯一 |
| `force_f4` 的着色目标 | `force_f4_shape` geom | 本例同一个 shape 同时参与接触和显示 |

对象缺失、类型不符或存在歧义时拒绝加载，不猜测关联。每个触面 body 只有一个 `force_fN_shape`；外壳 `housing_visual` 是明确的视觉专用几何，质量为零且不参与接触或射线命中。

具体机器人挂载目标、目标安装位姿由场景实例指定。使用型号资产挂载流程时，Orca 根据安装 site 对齐模型，并为不同实例的对象加上各自的前缀。例如 `A__force_f4` 和 `B__force_f4` 都来自型号的 `force_f4`。厂商继续使用契约中的原始名称，多个实例可以复用同一份型号包。直接绑定场景现有 site 的方式见[场景绑定](#scene-site-binding)。

本期资产适配器支持固定安装、基本几何和命名材质；姿态使用 `quat`（w、x、y、z）。不支持任意 mesh、texture、include、defaults 或原生插件。模型只包含传感器自身；地面、载荷和机器人属于示例场景。

TouchGrid 使用同一约定：[model.xml](examples/touch_grid/model.xml) 的根 body 为 `sensor_root`，安装 site 为 `sensor_base`，一个 `force_f1` body 下的 `force_f1_shape` 表达整块 0.06 m × 0.06 m 触面，`surface_frame` 位于上表面中心。4×4 是算法输出网格，不是在 XML 中放置 16 个 geom。

## 3. 一份契约定义输入与输出

`contract.json` 的 `inputs` 声明算法需要的数据，`output` 定义算法返回的数据。四个样板均使用 `schema_version: 4`；选择输入能力时，可以从场景指定的 site 采集，也可以引用型号 XML 中的固定对象名称。

### 直接请求 site 上的固定输入

[ContactGrid 契约](examples/contact_grid/contract.json) 请求采样时间和一个固定力网格。下面是其中的网格请求，应放入 `inputs` 数组：

```json
{
  "name": "force_grid",
  "capability": "orca.contact_grid.v1",
  "resolution": [4, 4],
  "fov_degrees": [120, 120]
}
```

Orca 按实例绑定 site 收集接触，转换成局部坐标，按角度分箱求三维矢量和，再交给动态库的 `force_grid[4][4][3]`。动态库不处理接触点列表，只计算每格 `norm(Fx, Fy, Fz) * gain`，输出 `float64[4,4]`。这是力（N），不是压力（Pa）。网格行随 +Y 仰角增大，列随 +X 方位角增大；`[120,120]` 是水平与垂直全角，只有前方 `z > 0` 的接触参与。

[Rangefinder 契约](examples/rangefinder/contract.json) 请求采样时间和一个距离：

```json
{
  "name": "distance",
  "capability": "orca.site_raycast.v1",
  "direction": [0, 0, 1],
  "max_distance": 0.1
}
```

Orca 从绑定 site 的原点沿局部 +Z 执行射线查询，动态库收到 `distance` 标量；未命中或超出 0.1 m 为 `-1`。样板动态库保留 `-1`，对有效距离返回 `max(0, distance + bias_m)`。

两个能力都以 site 所属的整个刚性焊接组为范围依据，也就是模型中无关节固定连接的刚体集合。接触网格只考虑该组与外部的接触，测距始终排除该组的全部几何。这个范围可能包含传感器外的其他固定触面；site 若属于世界固定组，射线还会排除同组地面、墙体等静态几何。完整规则见[接触力网格](docs/capabilities/contact_grid.md)与[site 测距](docs/capabilities/site_raycast.md)。

### 按型号内部对象请求数据

需要逐接触记录或明确指定某几个型号内触面时，可声明：

```json
{
  "schema_version": 4,
  "contract_id": "example.tactile.contract.v4",
  "c_struct": "TactileInput",
  "inputs": [{
    "name": "contacts",
    "capability": "orca.contact.v1",
    "objects": ["force_f4"],
    "capacity": 1024,
    "frame": "world",
    "exclude_internal": true,
    "fields": {
      "pad_index": "surface_index",
      "normal": "normal_force"
    }
  }],
  "output": {
    "dtype": "float64",
    "shape": [1],
    "channels": [{
      "name": "normal_force",
      "offset": 0,
      "unit": "N",
      "description": "Total normal force."
    }]
  }
}
```

`inputs[].objects` 是引用列表，不是对象定义。列表顺序决定从 0 开始的触面编号；这里 `force_f4` 的编号为 0。字段别名和容量由厂商选择；类型、形状、单位、坐标系和采样含义见 [接触数据手册](docs/capabilities/contact.md)。

七触面示例声明接触与测距两项输入。接触力由 Orca 统一转换到 `range_frame`，动态库不需要额外旋转矩阵或接触对方编号。测距需求为：

```json
{
  "name": "proximity",
  "capability": "orca.raycast.v1",
  "frame": "range_frame",
  "direction": [0, 0, 1],
  "max_distance": 0.1,
  "exclude_self": true
}
```

Orca 从 `range_frame` 原点沿其局部 +Z 方向，对场景的碰撞 shape 查询最近命中。只命中视觉几何不算命中；超出 0.1 m 或没有命中统一返回 `-1`。`exclude_self` 排除本传感器实例的全部几何，不仅是射线起点所在 body，也不排除整台机器人。射线、接触力与用于组装的姿态使用同一物理源状态。动态库接收 `proximity` 数值，不调用物理引擎 API，也不回调 Python 主动取值。

当前射线查询支持场景中的基本几何、mesh 和 heightfield，不支持 SDF 或可碰撞 flex；mesh 查询原始三角面，可能与接触计算采用的凸包不同。射线从闭合几何内部发出时取下一个出射交点。这些是查询语义，不是算法动态库的实现要求。

型号可以包含输入未使用的外壳或显示对象。工具会为输入定义生成一个摘要，称为“输入指纹”，用于注册时检查输入约定是否匹配。指纹包含输入字段定义、实际引用对象的语义及所用能力定义；调整外观或安装基准不改变结构布局，但仍须按型号版本管理资产。

### TouchGrid：动态库将逐接触记录转换为网格

[TouchGrid 契约](examples/touch_grid/contract.json) 声明采样时间 `sample_time`，以及 `force_f1` 的接触记录；接触位置、法向力和切向力在 `surface_frame` 中表达。Orca 按实例精确解析这两个固定名称，采集后组装为 `TouchGridInput`；动态库只读取结构中的数据，不自行查询模型。

动态库根据每个接触点的局部 x/y 位置选择网格，将法向力大小累加到对应格，返回 `float64[4,4]`。每格单位是 N，不是 Pa；网格总和是本例采样范围内、经过 `gain` 缩放的法向力总量。接触点超出算法采样范围时不计入网格。

包入口的 `type_id` 为 `com.orca.examples.touch_grid`，包版本为 `1.0.0`，`contract_id` 为 `com.orca.examples.touch_grid.input.v3`。其中 ID 的 `.v3`、契约格式 `schema_version: 4` 和 C ABI 2 分别管理契约身份、声明格式和二进制接口，不能混为同一个版本，也不随首次公开版本重置。

TouchGrid 的 `global_parameters` 为 `extent_x=0.06`、`extent_y=0.06`、`gain=1.0`。前两者是以 `surface_frame` 原点为中心的算法采样宽度，须与模型的有效触面范围保持一致；修改这些参数不会缩放 XML 几何。

### 输出与算法全局配置

同一份 `contract.json` 的 `output` 定义固定的 `float64` 输出布局，`channels` 解释其中各段：

```json
{
  "name": "capacitance",
  "offset": 4,
  "shape": [7],
  "unit": "raw",
  "objects": ["force_f1", "force_f2", "force_f3", "force_f4", "force_f5", "force_f6", "force_f7"],
  "description": "Seven synthetic capacitance readings in raw algorithm units."
}
```

`offset` 按输出元素计数，不是字节地址。`objects` 可选，用于指出七个分量对应哪些型号对象；标量通道可省略 `shape`。七触面总输出为距离、法向力模长、切向力模长、切向方向、七路电容，共 11 个数。电容 `raw` 不是压力，也不是标定后的 SI 电容。

本例先对各触面的法向、切向接触力分别作矢量求和，再跨触面求和；只对两个总矢量的分量叠加零均值高斯噪声，最后输出模长。方向为 `range_frame` 中切向合力的 `atan2(y, x)`。每触面的示例电容为 `capacitance_base + capacitance_gain * norm(法向合力 + 切向合力)`，不额外加电容噪声。这些是公开的教学近似公式，不代表任何实际产品的算法、标定或精度。

`provider.json` 中各型号的 `global_parameters` 包含配置名称、默认值和允许范围，例如：

```json
"global_parameters": [{"name": "noise_scale", "default": 0.01, "min": 0.0, "max": 1000000.0}]
```

“全局”指该实例算法的全局配置，不是进程级共享变量。Orca 在创建实例时传入，动态库保存到该 handle 并在各步复用；本期不支持热更新，`reset` 不改变这些配置。不同实例可使用不同值。

`contract.inputs` 声明从模型或物理后端取得的数据，`global_parameters` 声明用户选择的算法设置。输入可以是静态量，例如父坐标系旋转；不能仅按“每步是否变化”区分两者。

## 4. 类型注册与数据传递

动态库导出固定入口 `orca_sensor_get_provider`，返回描述厂商和型号信息的 `OrcaSensorProviderDescriptorV2`。其中 `types[]` 的每个 `OrcaSensorTypeDescriptorV2` 登记一个 `type_id`，以及该型号的 `create/reset/compute/destroy` 回调。注册时，Orca 校验包清单、输入布局与指纹；Host 从描述符取得函数地址，按型号调用算法。

每个实例拥有独立的输入输出内存和算法状态。每个物理子步，Orca 把采集的数据交给 Host；Host 按生成的布局填好输入结构和接触记录数，再调用厂商的计算回调。其中 `handle` 是厂商在 `create` 中分配并返回的实例状态：

```cpp
compute(handle, input, input_size, output, output_count);
```

七触面收到的 `SevenPadInput` 包含 `proximity`、`contacts_count`、`contacts[1024]`。接触记录仅含触面编号、三维法向力和切向力；固定名称 `force_f1…force_f7` 已被 Orca 解析为本实例的实际模型对象。没有逐步 JSON 序列化，也没有隐式传入 `mjData`。

<a id="scene-site-binding"></a>

### 给场景集成方：绑定现有的测量 site

本小节供将型号包接入 OrcaGym 场景的集成方参考。这里只介绍 ContactGrid、Rangefinder 使用的场景 site 方式；厂商开发和构建算法可继续阅读[第 5 节](#5-构建并交付自己的型号包)。

直接 site 输入使用与 MJCF 兼容的 `custom` 元素，不在 `<sensor>` 中注册原生传感器。以下片段放入主场景 XML；场景应已定义 `left_pad_site` 和 `left_range_site`：

```xml
<custom>
  <text name="orca.sensor.v1/left_pad/plugin"
        data="com.orca.examples.contact_grid"/>
  <tuple name="orca.sensor.v1/left_pad/site">
    <element objtype="site" objname="left_pad_site"/>
  </tuple>
  <numeric name="orca.sensor.v1/left_pad/config/gain" data="1"/>

  <text name="orca.sensor.v1/left_range/plugin"
        data="com.orca.examples.rangefinder"/>
  <tuple name="orca.sensor.v1/left_range/site">
    <element objtype="site" objname="left_range_site"/>
  </tuple>
  <numeric name="orca.sensor.v1/left_range/config/bias_m" data="0"/>
</custom>
```

`left_pad`、`left_range` 是实例 ID；`plugin` 的值是已注册型号的 `type_id`，不是动态库函数或文件路径。每实例恰好一个 `plugin` 文本项、一个包含单个 site 的 `tuple`；`numeric` 可选，只填写型号声明的标量 `global_parameters`，省略时用默认值。场景 site 名精确匹配，不要求厂商前缀。

应用向 `load_sensor_scene(..., provider_manifests=[...])` 提供可信包的清单路径。加载器注册型号、解析 site、准备查询和各实例的内存；随后 `runtime.step()` 统一采集组装并调用相应动态库，返回按实例 ID 索引的 NumPy 输出。两个实例可以绑定同一 site，但分别持有自己的动态库 handle 和参数。

这些 Python 接口由 OrcaGym 集成层提供。独立 `SensorRuntime` 的一次 `runtime.step(nstep=5)` 会逐子步计算五次，全部成功后发布最后结果；读取结果得到的是独立的 NumPy 副本。`host.output_for(type_id)` 可取得输出描述，`host.presentation_for(type_id)` 可取得可选展示配置。

名称前缀 `orca.sensor.v1/<实例>/...` 的 `v1` 仅表示场景声明格式，不是 SDK 版本或 C ABI。独立运行时继续支持现有 JSON 场景声明路径，但同一场景不能混用两种声明格式。OrcaGym 的 EulerEnv 通过 `sensor_provider_manifests` 选择可信厂商包，在环境现有物理子步中自动计算，以 `query_provider_sensor_data()` 读取；不额外调用独立 `runtime.step()`。支持该功能的平台安装包会自动提供 Host，`sensor_host_path` 仅用于可选的开发覆盖，不是普通用户的运行前提。该接入不需要 OrcaLab UI，也不改变本 SDK 的厂商 C ABI。

## 5. 构建并交付自己的型号包

你的交付物是一个包含算法动态库、契约和模型的型号目录，客户通过其中的 `provider.json` 加载传感器。下面以 ContactGrid 为例，说明如何从样板做出自己的包。

准备完整 SDK、CMake/CTest 3.20+ 和 C++17 编译器，使用 Linux x86_64（glibc 2.35+）。以下命令均在 **SDK 根目录**执行，无需安装 Python 或 OrcaGym。第一次使用时，可以先运行页首的三条命令，确认未修改的样板能在你的机器上构建并通过验证。

### 选择样板，修改你的型号定义和算法

从前面介绍的四个样板中选择输入方式接近你需求的一个。本文沿用 `examples/contact_grid/` 的目录和文件名，在这个样板上修改；保留它在 SDK 中的目录位置即可使用现成的构建配置。

| 你要定义什么 | 修改哪个文件 |
| --- | --- |
| 算法需要哪些输入，返回多少个数，以及各输出的单位与含义 | `contract.json`；输入能力查阅[厂商输入能力手册](docs/capabilities/index.md) |
| 传感器外形和安装基准 | `model.xml` |
| 实例参数、状态、计算和重置逻辑 | `contact_grid.cpp` 中的 `Create`、`Compute`、`Reset` 等回调 |
| 厂商及型号标识、包版本、文件路径和参数默认值 | `provider.json` |
| 动态库名称、参与编译的源码、需要随包复制的资源 | `CMakeLists.txt` |

将示例标识替换成自己的标识时，同步修改 `provider.json` 和 C++ 描述符中的 `provider_id`、`type_id`、包版本，并将 `contract.json` 的 `contract_id` 改为自己的契约标识。修改输出形状或实例参数时，也要同步调整 C++ 描述符和算法中的数量、顺序与读取方式。若更改安装 site 名称，更新 `provider.json` 的 `mount_site` 及 CMake 中对应的 `--mount-site`。版本字段的含义见[版本与兼容约定](docs/compatibility.md)。

### 构建这个型号包

```bash
cmake -S examples/contact_grid -B build/contact-grid -DCMAKE_BUILD_TYPE=Release
cmake --build build/contact-grid --config Release
```

CMake 会校验契约和模型、生成输入头文件及包清单、编译算法动态库，并将交付文件放到同一个目录。以后修改契约、模型或算法，再运行第二条命令即可。

算法实际使用的输入结构可在 `build/contact-grid/generated/contact_grid_input.h` 中查看。头文件由构建自动生成；修改输入需求时，编辑源目录中的 `contract.json`，再让 CMake 重新生成。如果调整了字段或数组尺寸，算法代码也要按新结构修改。

厂商动态库使用公共 C 接口和生成的输入结构。SDK 自带的 Host 负责加载和调用它，厂商无需实现 Host，也无需在算法里调用物理引擎接口。

### 验证你的输入和计算结果

参考[原生调用示例](examples/acceptance/site_providers_smoke.cpp)为自己的型号准备测试：加载构建出的库，传入几组可以手工核对的输入，检查实际输出，并验证多实例互不影响、reset 后能够重放、异常输入会返回错误。

SDK 自带的验收程序使用样板固定的型号 ID、输入布局和预期结果，构建与运行方法见[原生验收说明](examples/acceptance/README.md)。修改型号、契约或算法后，需要相应调整测试；未修改样板的测试通过，只能说明样板调用链正常。上面的单型号构建命令不会自动创建这些验收程序。

完成算法验证后，再与集成方检查实际场景中的安装、site 绑定和物理数据采集，接入方式见[场景绑定](#scene-site-binding)。真实传感器的标定和精度需要另行验证。

### 交付整个型号目录

沿用本文的文件名时，构建产物位于：

```text
build/contact-grid/providers/contact_grid/
├── provider.json
├── contract.json
├── model.xml
├── liborca_contact_grid.so
├── README.md
└── LICENSE
```

将这个目录作为一个整体交付，并补齐算法依赖的运行库、资源、许可证和使用说明。若采用七触面样板，还会包含可选的 `presentation.json`。变更动态库文件名时，同步更新 `provider.json` 中的路径。

厂商交付使用上面的 `providers/contact_grid/` 目录。`cmake --install` 仅安装 Host 和公共头文件，用于 Host 调用方的开发环境。

更名或删除交付文件后，使用新的构建目录重新打包，避免旧动态库或资源残留。交付前确认说明和型号标识已经换成自己的产品信息。厂商可以保留算法源码，只交付动态库及其所需文件；客户通过 `provider.json` 找到该型号的契约、模型和算法。

## 6. 接口约定与运行范围

### 算法回调需要遵守的约定

- 输入只读，输入输出地址仅在本次回调期间有效；不能交给后台线程继续使用。回调串行执行，不得从回调中重入 Host。
- 每个实例独立保存状态和参数；相同参数、随机种子和输入序列应能在重置后重放。
- 成功的计算必须写满输出，且每个数值都有限。失败时返回错误码，C++ 异常须在回调内处理。
- 同进程的动态库没有崩溃隔离，只加载可信厂商库。计算失败后不会发布不完整结果，实例须成功 reset 后再继续计算。
- 输入最多包含 64 个叶字段，总大小不超过 16 MiB；每个记录数组最多 65536 条。实际数量超过声明容量时报错，不截断数据。

内存归属、函数参数和错误码的完整定义见[公共 C 接口](include/orca_sensor_abi.h)。

### SDK、接口和型号包的版本

当前为 **SDK 1.0.0 / C ABI 2**：`1.0.0` 是整套开发包的发布版本，`2` 是动态库与 Host 之间的二进制接口版本。因此，SDK 1.0.0 的代码使用 `OrcaSensorTypeDescriptorV2` 等 `V2` 结构是正常的。

厂商在 `provider.json.version` 中管理自己的型号包版本；`sdk_version` 表示构建该包选择的 SDK 目标。四个样板当前均选择 `1.0.0`。各版本字段的填写和升级规则见[版本与兼容约定](docs/compatibility.md)。

SDK 版本记录在 [VERSION](VERSION)，也可运行 `bin/linux-x86_64/orca-sensor-tool --version` 查看工具版本。请保持同一 SDK 包中的工具、Host 和头文件配套；`sdk-manifest.json` 记录完整包的文件大小与校验和，`native-manifest.json` 记录原生产物。分发契约工具时须保留相邻的 `_internal/` 运行依赖和 `licenses/` 许可文件。

### 与场景集成方协作

| 阶段 | 使用什么 |
| --- | --- |
| 厂商开发和原生算法验证 | 本 SDK：构建型号包，手工提供测试输入，核对算法输出 |
| 在机器人仿真中使用型号包 | 支持传感器功能的 OrcaGym 正式版本：提供物理数据采集和 Host 加载 |
| 参考完整机器人、控制和图表示例 | OrcaPlayground：见[完整灵巧手演示](#查看完整灵巧手演示)的目录和运行入口 |

集成方应使用与本 SDK 配套的 OrcaGym 发布版本，具体兼容范围见 OrcaGym 发布说明。Host 随支持平台的安装包提供，`sensor_host_path` 仅用于开发时覆盖路径。

当前 EulerEnv 接入支持本文的场景 site 输入；按型号内部对象取得逐条接触数据的示例使用独立 `SensorRuntime`。采样支持 Euler、implicit、implicitfast，拒绝 RK4。输入来自同一个物理源状态；预览画面使用积分后的姿态，不能当作与当前力输出严格同状态的快照。完整时序见[采样信息](docs/capabilities/sampling.md)。

当前二进制仅交付 Linux x86_64（glibc 2.35+）；Windows/macOS 需要各自经过验证的 Host 和工具。动态库须匹配操作系统、CPU 架构和系统运行库。本 SDK 的原生验收不代表真实传感器精度或硬实时频率保证。

### 许可与分发

| 内容 | 适用许可 |
| --- | --- |
| 公共头文件、规格、文档和厂商样板源码 | [MIT 许可证](licenses/PUBLIC_SDK_LICENSE.txt)，可用于商业产品，分发时保留版权与许可声明 |
| Orca 提供的预编译 Host 与契约工具 | [公司协议原文](licenses/ORCA_BINARY_LICENSE.html)及 [SDK 补充条款](licenses/ORCA_BINARY_LICENSE_ADDENDUM.md)；补充条款明确允许商业开发、自动构建和其中约定的整包分发 |
| 工具自带的第三方运行组件 | `bin/<平台>/licenses/` 中各组件自己的许可证和版权声明 |

完整许可范围见 [LICENSE](LICENSE)。Host 和契约工具的内部实现源码不在 SDK 中；厂商自己的算法可按自身授权方式交付。再分发 Host 或工具时，一并保留公司协议、SDK 补充条款、第三方声明及所需运行文件。公司条款不限制第三方许可证或公开源码 MIT 许可证已授予的权利。

欢迎 Issue/PR；维护者会将改动纳入统一维护源，验证后随版本同步。
