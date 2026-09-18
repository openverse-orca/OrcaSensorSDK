# 从绑定 site 测距：`orca.site_raycast.v1`

需要从场景指定的一个 site 发出射线、将距离交给动态库时，选择这个能力。厂商只声明局部方向和最大距离；不需要在型号 XML 中放置原生 rangefinder。

site 是模型中带位置和方向的参考标记，自身不会测距；射线查询由 Orca 执行。它由场景为每个实例选择，区别于 [型号参考系测距](raycast.md)在契约中指定固定的 `frame` 名称。

支持的 SDK 目标：`1.0.0`，契约使用 `schema_version: 4`。完整样板见 [Rangefinder](../../examples/rangefinder/README.md)。

## 最小声明

将下面一项放入 `contract.json` 的 `inputs`：

```json
{
  "name": "distance",
  "capability": "orca.site_raycast.v1",
  "direction": [0, 0, 1],
  "max_distance": 0.1
}
```

| 参数 | 必填 | 含义 |
| --- | --- | --- |
| `name` | 是 | 生成的 C 输入成员名，本例为 `distance` |
| `capability` | 是 | 固定填 `orca.site_raycast.v1` |
| `direction` | 是 | 绑定 site 局部坐标系中的非零三维方向，分量必须有限；Orca 自动归一化 |
| `max_distance` | 是 | 最大查询距离，单位 m，必须为有限正数 |

`[0, 0, 1]` 表示 site 的局部 +Z，不是固定世界方向。射线从 site 原点发出，随 site 的姿态变化。

不填写 `objects`、`frame`、`fields` 或 `exclude_self`。参考 site 由[场景实例绑定](../../README.md#scene-site-binding)；当前能力的自身排除规则固定如下，不另设可调开关。

## 动态库收到什么

生成一个标量成员 `double distance`，单位 m：

| 值 | 含义 |
| --- | --- |
| `0 <= distance <= max_distance` | 最近符合条件的碰撞几何表面距离 |
| `distance == -1` | 未命中，或最近有效表面超出最大距离 |

非有限输入或后端结果报错，不用 `NaN` 表示未命中。每个物理子步，查询与用于组装的 site 位姿使用一致的物理源状态；如需时间戳，同时声明 [采样时间](sampling.md#time)。

在厂商回调已检查输入大小并取得生成结构后，可直接读取：

```cpp
const auto& input = *static_cast<const RangefinderInput*>(input_data);
double distance_m = input.distance;
```

`RangefinderInput` 来自样板契约的 `c_struct`，`distance` 来自 `name`。动态库不执行射线查询，也不需要接收 site 名称、模型或物理引擎指针。

## 哪些几何会命中

Orca 查询碰撞几何，视觉专用几何不是目标。绑定 site 所属**整个刚性焊接组**的几何始终排除，不只是 site 的直接父 body；该组是模型中无关节固定连接的刚体集合。

特别注意：如果 site 属于世界固定组，排除范围会包含同组的所有世界固定几何，例如固定地面或墙体；这些物体不会被该射线测到。动态刚体上的 site 可以命中不属于其组的静态几何。选取安装位置时要核对刚性组关系，不能仅按 XML 中相邻的 body 判断。

当前后端支持基本几何、mesh 三角面和 heightfield；不支持 SDF 或可碰撞 flex 的场景会在准备阶段报错。mesh 查询原始三角面，可能与物理接触采用的凸包不同；从闭合几何内部发出时，距离对应下一个出射交点。

这个固定规则不是 [型号参考系射线测距](raycast.md) 的 `exclude_self`：后者可排除型号实例自身的几何，而这里始终排除绑定 site 的整个刚性组。两个能力使用不同标识，各自保留其明确语义。

## 样板动态库做什么

Rangefinder 样板保留未命中的 `-1`；有效命中返回 `max(0, distance + bias_m)`。`bias_m` 是创建实例时传入的算法参数，默认 0，范围 `[-1, 1]`，不会改变 Orca 的查询方向或距离上限。

下限裁为零，可避免负偏差把有效命中变成未命中标记。正偏差可能使最终输出超过查询上限：查询距离和算法输出是两个不同阶段。本例用于说明数据传递，不是经过实物标定的测距传感器。
