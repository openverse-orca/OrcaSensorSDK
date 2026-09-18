# 测距：`orca.raycast.v1`

从型号内的一个参考系发出射线，由 Orca 查询距离；厂商不需要在 XML 中定义原生测距传感器。

这里的 `frame` 指型号中已有对象的坐标系，可以来自 body、geom 或 site；site 是带位置和方向的参考标记，见[模型术语](index.md#model-terms)。如果希望由场景为每个实例另选测量 site，使用 [site 测距](site_raycast.md)。

## 最小声明

把下面这一项放进 `contract.json` 的 `inputs` 数组。`range_frame` 是型号 XML 中的 site 名称，Orca 会在当前实例内找到它。

```json
{
  "name": "proximity",
  "capability": "orca.raycast.v1",
  "frame": "range_frame",
  "direction": [0, 0, 1],
  "max_distance": 0.1
}
```

含义：从 `range_frame` 的原点沿它的局部 +Z 方向测距，最远测 0.1 m，默认排除当前传感器实例自身。

## 可填参数

| 参数 | 必填 | 默认值 | 含义 |
|---|---|---|---|
| `name` | 是 | — | 动态库输入成员名，本例生成 `data.proximity` |
| `capability` | 是 | — | 固定填写 `orca.raycast.v1` |
| `frame` | 是 | — | 型号内部参考系名称；决定射线起点和方向参考系 |
| `direction` | 是 | — | 该参考系中的三个有限数值，不能全部为 0；Orca 自动归一化 |
| `max_distance` | 是 | — | 有限正数，单位 m；是量程，不是步长 |
| `exclude_self` | 否 | `true` | 是否排除当前传感器实例的全部几何，包括其它 body 和未采集的外壳 |

`exclude_self` 由 Orca 处理，不传入动态库。它不会排除其它传感器实例，也不会把安装它的整台机器人排除掉。若旧模型的手动绑定缺少完整实例几何归属，启用它会在准备阶段报错。

## 动态库收到什么

一个 `double`，即 `f64` / `float64`，shape 为 `[]`，单位 m。格式固定；不填写 `dtype`、`shape` 或 `fields`。

- 命中：到最近有效表面的距离，范围为 `[0, max_distance]`。
- 未命中，或最近表面超出量程：严格返回 `-1`，不是距离 0。
- 射线起点在封闭几何内部时，返回沿射线遇到的出口表面距离。

## 查询范围与限制

采集的是当前物理源状态的参考系和几何位置，与同批接触输入对齐。查询包含静态碰撞几何；仅显示、未启用碰撞的几何不参与，透明度本身不决定能否命中。

当前查询支持基本几何、mesh 和 heightfield。mesh 查询原始三角形表面，可能与接触计算使用的凸包不同；待查询的 SDF 等不支持几何及可碰撞 flex 会在准备阶段报错。

完整用例：[灵巧手七触面契约](../../examples/seven_pad/contract.json) · [型号 XML](../../examples/seven_pad/model.xml)

[返回能力手册](index.md)
