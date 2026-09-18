# 厂商输入能力手册

这份手册回答：**Orca 能给我的算法哪些数据？我该填哪些参数？动态库怎样读取？**

正常接入不需要阅读 JSON Schema。先按需要选择下面的能力，参考该页的参数表填写 `contract.json`；样板的 CMake 构建会调用 SDK 工具检查契约并生成输入头文件。

本手册全部能力均属于首次公开发布的 **SDK 1.0.0**，也是当前唯一配置；能力 ID 中的 `.v1` 仍表示各项能力的协议身份，不随 SDK 发布版本重置。

<a id="model-terms"></a>

## 先区分模型中的几个名称

| 名称 | 在本手册中的含义 |
| --- | --- |
| `body`、`geom` | XML 中的刚体节点和几何形状。一个 body 可以包含多个 geom；geom 可用于碰撞或仅用于显示。 |
| `site` | XML 中带位置和方向的参考标记，随所属 body 运动；它自身不产生接触或测距数据。场景选择哪个 site，便确定该实例的测量原点和坐标轴。 |
| `frame` | 契约中的坐标系参数。它引用型号中已有的 body、geom 或 site 的名称；接触输入还可用 `world` 表示整个场景的世界坐标系。无需因此另建一个名为 `frame` 的对象。 |
| 型号、实例 | 型号是厂商交付的一套模型、契约和算法；实例是场景中实际使用的一份。一个型号可以创建多个实例。 |

安装 site 用来说明型号如何安装；测量 site 或 `frame` 用来说明从哪里查询、按哪些坐标轴表达数据。两者可以相同，也可以不同，不能仅凭 `sensor_base` 这样的名称判断测量位置。

## 按需要选择能力

| 我需要什么 | 填写的 `capability` | 参数与示例 |
| --- | --- | --- |
| Orca 按场景绑定的 site 汇总三维接触力网格 | `orca.contact_grid.v1` | [接触力网格](contact_grid.md) |
| 从场景绑定的 site 向外测距 | `orca.site_raycast.v1` | [site 测距](site_raycast.md) |
| 触面上的接触位置、法向力、切向力 | `orca.contact.v1` | [接触数据](contact.md) |
| 从模型上的参考系向外测距 | `orca.raycast.v1` | [射线测距](raycast.md) |
| 对象相对父坐标系的静态模型旋转 | `orca.frame.parent_rotation.v1` | [模型旋转](parent_rotation.md) |
| 当前输入对应的仿真源时间 | `orca.sample.time.v1` | [采样时间](sampling.md#time) |
| 一个物理子步的时间长度 | `orca.sample.dt.v1` | [子步时长](sampling.md#dt) |
| reset 后的物理子步编号 | `orca.sample.index.v1` | [子步编号](sampling.md#index) |
| 读取旧模型已经存在的测距通道 | `orca.range.v1` | [旧通道兼容](legacy_range.md)，新型号使用射线测距 |

`capability` 是 Orca 提供的采集能力名称，不是厂商型号或动态库函数名。右侧页面列出每项能力的必填项、可选项、默认值、返回类型和单位。

## 从哪开始填写

1. 打开完整 [ContactGrid 契约](../../examples/contact_grid/contract.json) 或 [Rangefinder 契约](../../examples/rangefinder/contract.json)。`inputs` 中每一项声明一项输入，`output` 描述动态库输出。
2. 使用 site 绑定的能力时，只填写网格或射线参数，由场景选择测量 site；不重复声明型号内部对象。如需指定触面和逐接触数据，参考 [TouchGrid 契约](../../examples/touch_grid/contract.json)，填写 `force_f1`、`surface_frame` 等固定内部名，不填实例前缀。
3. 按页面示例填写输入。各能力页的 JSON 片段应放入 `inputs` 数组，不是整个契约文件。
4. 按 [SDK README 的构建步骤](../../README.md#5-构建并交付自己的型号包)构建所选样板。CMake 会检查声明和对象引用，并自动生成动态库使用的 C 输入头文件；修改契约后重新构建即可。

如果需要先查看输入结构、再编写算法，也可以在 SDK 根目录单独生成头文件。下面的输出路径与 README 中 ContactGrid 的独立构建一致：

```bash
bin/linux-x86_64/orca-sensor-tool contract examples/contact_grid/contract.json \
  build/contact-grid/generated/contact_grid_input.h \
  --model examples/contact_grid/model.xml --mount-site sensor_base --sdk-version 1.0.0
```

此命令使用 Linux x86_64 SDK 自带的工具，不需要安装 Python，不会加载厂商动态库，也不需要安装 OrcaGym 或物理后端。当前 schema 4 命令行生成器仍通过 `--model` 校验型号资产；这个例子不从资产解析输入对象，实际测量 site 是否存在，在加载场景时检查。工具校验不代替物理场景编译和运行验收。

完整构建、注册与场景绑定见 [SDK README](../../README.md)。ContactGrid、Rangefinder、灵巧手七触面和 TouchGrid 的目标 SDK 及厂商包版本均为 1.0.0；生成器与清单须使用同一目标版本，详见 [兼容约定](../compatibility.md)。

## 哪些配置由谁处理

| 内容 | 谁来处理 | 动态库收到什么 |
| --- | --- | --- |
| `capability`、`objects` / `object`、`frame`、`exclude_internal` 等采集选项 | Orca 解析对象、执行查询与过滤 | 处理后的输入数据，不是这些配置字符串 |
| `fields` | Orca 选择标准物理量；SDK 根据厂商成员名生成布局 | C 结构体成员，不是字典 |
| `capacity` | SDK 确定记录数组最大长度，Orca 检查实际数量 | 数组及实际有效记录数，例如 `contacts_count` |
| 接触力网格的 `resolution`、`fov_degrees` | Orca 按绑定 site 分箱、求矢量和，SDK 固定输入形状 | `force_grid[行][列][3]`，不是配置字符串 |
| 场景中的 `plugin`、`site` | Orca 找到已注册型号和场景测量参考系 | 各实例分别组装的输入；动态库不解析 XML |
| `provider.json` 中的 `global_parameters` | Orca 在创建实例时传入，动态库保存到该实例 | 算法设置，例如 `gain`；不是每步采集的物理量 |

标量或固定数组输入用 `name` 直接生成成员，例如 `sample_time`、`distance`、`force_grid`；接触记录输入用 `fields` 定义每条记录的成员。`sample_time` 和 `contacts` 可同时存在，彼此同级。详见 [采样信息](sampling.md) 与 [接触数据](contact.md)。

site 绑定路径支持接触力网格、site 测距与采样信息；一个契约可同时请求这些输入，共用实例绑定的 site。当前同一个 site 绑定实例不能混入依赖型号内部对象的能力。输入是选择现有采集能力，不是任意查询脚本。

## 填错时如何检查

生成器遇到未知能力、错误参数类型、缺少必填项、未知标准字段或无法解析的型号对象，会报出对应位置。修正契约后重新构建；若单独生成头文件，则重新运行生成命令。不需要靠阅读 Schema 排错，也不要直接修改生成的头文件。

名称和数据类型遵循各页的约定；输入成员名使用字母开头的字母、数字、下划线组合，并避免 C/C++ 保留字。`name` 在同一份契约内不能重复。

各页的 `dtype` 表示数据类型，`shape` 表示数组各维长度：`[]` 是标量，`[3]` 是三个分量，`[3, 3]` 是 3×3 矩阵。“有限数值”排除 `NaN` 和正负无穷大。

记录数组容量为 1–65536；整个输入还受最多 64 个叶字段、16 MiB 的限制。这里的叶字段指实际传入的单个标量或固定数组成员，接触记录按其成员分别计数。实际接触为零时传零条记录；运行时超容量报错，不静默截断。不能通过声明新的能力字符串获得平台尚未实现的物理量。

## 手册、契约、Schema 的区别

| 文件 | 面向谁 | 用途 |
| --- | --- | --- |
| 本手册 | 厂商开发者 | 阅读参数、物理量含义和接入示例 |
| 厂商的 `contract.json` | 厂商填写，Orca 读取 | 声明型号的输入需求和输出布局 |
| SDK 的能力目录与 JSON Schema | 校验和生成工具 | 定义机器可读规则；厂商无需修改或逐项阅读 |

机器规则仍随 SDK 提供，供工具和维护者使用；本手册不要求厂商安装编辑器插件，也不假定已配置自动补全。
