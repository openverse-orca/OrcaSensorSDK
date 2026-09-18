# 接触数据：`orca.contact.v1`

让 Orca 收集指定触面上的接触位置和接触力，组装后交给厂商动态库。厂商不需要调用物理后端 API。

## 最小声明

把下面这个对象放进 `contract.json` 的 `inputs` 数组；它是一个输入请求，不是完整契约。
型号 XML 中必须存在名为 `force_f1` 的 body 或 geom，作为被采集的触面。

```json
{
  "name": "contacts",
  "capability": "orca.contact.v1",
  "objects": ["force_f1"],
  "capacity": 64,
  "fields": {
    "position": "position",
    "normal_force": "normal_force"
  }
}
```

这表示：收集 `force_f1` 的接触点位置和法向力，使用世界坐标系，最多容纳 64 条记录。
`force_f1` 是型号内部的固定名称；挂载多个实例时，Orca 分别解析，不需要厂商改名。

## 可以填写哪些参数

| 参数 | 必填 | 默认值 | 怎么填、有什么用 |
|---|---|---|---|
| `name` | 是 | — | 这组输入在动态库中的成员名，例如 `contacts`。使用合法的 C 标识符，且不能与其他输入重名。 |
| `capability` | 是 | — | 固定填写 `orca.contact.v1`，表示使用接触数据采集能力。 |
| `objects` | 是 | — | 型号内部触面名称的有序列表，不能为空或重复。顺序决定 `surface_index`。 |
| `capacity` | 是 | — | 接触记录数组容量，整数 `1..65536`，例如 `64`。不是触面或网格数量。 |
| `frame` | 否 | `"world"` | 位置和力的表达坐标系；填 `world`，或型号中的参考系名称，例如 `surface_frame`。 |
| `exclude_internal` | 否 | `true` | 是否过滤本请求所选触面之间的接触，详见下文。 |
| `fields` | 是 | — | 选择每条记录需要的物理量；左边是动态库成员名，右边是下表中的标准字段名。至少选一项。 |

这里的触面是一个逻辑感知表面，可以由一个或多个碰撞形状实现。
当前 XML 型号中，引用 body 时只包括该 body 直属的 geom，不会自动递归包含子 body。
例如 `objects: ["force_f1"]` 引用 TouchGrid 的 body，会采集其直属 `force_f1_shape` geom 上的接触。模型术语、大小和命名约束见[手册首页](index.md)。

## 每条接触记录可以包含什么

| 标准字段名（填在右边） | C 类型 | shape | 单位 | 含义 |
|---|---|---|---|---|
| `surface_index` | `uint32_t` | 标量 | 无 | 接收力的触面在 `objects` 中的序号，从 `0` 开始。只有 `force_f1` 时始终为 `0`。 |
| `counterpart_rigid_body_id` | `uint64_t` | 标量 | 无 | 接触另一侧的刚体编号，用于区分或归并物体；不是几何形状编号，仅在当前加载模型中稳定。 |
| `position` | `double[3]` | `[3]` | m | 接触点相对所选 `frame` 原点的位置 `[x,y,z]`。 |
| `normal_force` | `double[3]` | `[3]` | N | **作用在当前触面上**的法向力向量 `[Fx,Fy,Fz]`，不是力的大小。 |
| `tangential_force` | `double[3]` | `[3]` | N | **作用在当前触面上**的切向力向量 `[Fx,Fy,Fz]`，不是力的大小。 |

两个力向量也使用 `frame` 的坐标轴。法向和切向是按每次接触的法线区分的，并不固定对应所选坐标系的 Z 轴或 XY 平面。需要力的大小时，动态库自行计算向量模长；不能默认把某一个分量当成大小。
如果接触两侧属于不同传感器实例，各实例收到的是作用在自己触面上的力；换算到同一坐标系比较时，方向相反。

## 字段映射和局部坐标系

下面是 TouchGrid 示例使用的完整接触请求，仍然只是 `inputs` 数组中的一个对象：

```json
{
  "name": "contacts",
  "capability": "orca.contact.v1",
  "objects": ["force_f1"],
  "capacity": 64,
  "frame": "surface_frame",
  "exclude_internal": true,
  "fields": {
    "pad_index": "surface_index",
    "object_id": "counterpart_rigid_body_id",
    "position": "position",
    "normal_force": "normal_force",
    "tangent_force": "tangential_force"
  }
}
```

例如 `"tangent_force": "tangential_force"`：Orca 采集标准切向力，填入动态库的 `tangent_force` 成员。
左右同名也要保留映射；当前不支持用字段名列表代替 `fields` 对象。
映射只选择数据并命名，不改变单位、类型或数值；坐标转换由 `frame` 决定。
示例中的 `surface_frame` 是型号中已经定义的参考系，不是任意新起的名字。

## `exclude_internal` 到底过滤什么

这个选项由 Orca 的采集器使用，不会生成动态库输入成员，也不是 `global_parameters`。
以 `objects` 选择 `force_f1`、`force_f2` 为例：

- `true`：两触面各自与外部物体的接触保留；两触面互相接触的记录过滤掉。
- `false`：两触面互相接触也保留，并分别给两个接收触面生成记录，力方向相反。

“内部”严格指**本请求选中的触面覆盖范围**，不自动包含未选中的外壳、整个传感器或机器人。
它只过滤送给动态库的数据，**不会关闭物理碰撞，也不会改变物理计算**。

## 动态库怎么读取

`fields` 是声明语法，不会生成一个叫 `fields` 的字典。
假设完整契约的 `c_struct` 为 `TouchGridInput`，只包含本页最小接触请求，生成的结构类似：

```cpp
typedef struct TouchGridInput_contacts_Record {
    double position[3];
    double normal_force[3];
} TouchGridInput_contacts_Record;

typedef struct TouchGridInput {
    uint32_t contacts_count;
    TouchGridInput_contacts_Record contacts[64];
} TouchGridInput;
```

使用 SDK 生成的头文件，不要自行手写或猜测内存布局。动态库的计算函数在检查输入大小后读取：

```cpp
const auto& data = *static_cast<const TouchGridInput*>(input);
for (uint32_t i = 0; i < data.contacts_count; ++i) {
    const auto& contact = data.contacts[i];
    double x = contact.position[0];
    double force_z = contact.normal_force[2];
    // 根据位置和力进行厂商算法计算。
}
```

Orca 传入的是已经填好的 C 结构体内存，不是每步传 JSON，也不是让动态库按字符串查字典。
数组容量在生成头文件、编译动态库时确定，实际记录数 `contacts_count` 每步可能变化：

- 没有接触时为 `0`；动态库不应读取数组中的无效位置。
- 一块触面可能同时有多个接触点，不能把记录数当成触面数或 `4×4` 网格格数。
- 超过 `capacity` 会报错，不会静默截断；需调整容量、重新生成头文件并重新编译动态库。

TouchGrid 完整示例还单独声明了 `sample_time`，它与 `contacts` 同级，不是接触记录成员，也不是自动附加字段。
如何声明这个单值输入，见[时间与采样](sampling.md)；它不需要 `fields`。

## 完整可运行示例

查看 TouchGrid 的 [contract.json](../../examples/touch_grid/contract.json)、[model.xml](../../examples/touch_grid/model.xml) 和 [动态库算法](../../examples/touch_grid/touch_grid.cpp)。
完整示例用 `surface_frame` 中的接触位置划分 `4×4` 网格，再将法向力大小累加为输出。

[返回能力说明手册](index.md)
