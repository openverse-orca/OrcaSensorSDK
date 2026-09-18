# 模型局部旋转：`orca.frame.parent_rotation.v1`

读取一个触面或参考系在模型中定义的静态旋转，供动态库处理固定的安装方向或算法坐标约定。

## 最小声明

把下面这一项放进 `contract.json` 的 `inputs` 数组。`force_f1` 是型号内部的触面名称。

```json
{
  "name": "direction_rotation",
  "capability": "orca.frame.parent_rotation.v1",
  "object": "force_f1"
}
```

## 可填参数

| 参数 | 必填 | 默认值 | 含义 |
|---|---|---|---|
| `name` | 是 | — | 动态库输入成员名，本例为 `direction_rotation` |
| `capability` | 是 | — | 固定填写 `orca.frame.parent_rotation.v1` |
| `object` | 是 | — | 型号内部的触面或参考系名称 |
| `dtype` | 否 | `f64` | 可省略；显式填写时必须为 `f64` |
| `shape` | 否 | `[3, 3]` | 可省略；显式填写时必须为 `[3, 3]` |

这些类型与尺寸由能力确定，不需要厂商重复声明，也不能用它们改变返回格式。

## 动态库收到什么

一个 `double[3][3]`，即 `f64` / `float64`，shape 为 `[3, 3]`，无量纲。矩阵按行优先排列，读取方式为 `data.direction_rotation[row][column]`；无需 `fields`。

它将对象的局部向量转换到其直接父坐标系：`v_parent = R × v_local`。

“父坐标系”具体指：body 的父 body；geom 或 site 所属的 body。例如 `force_f1_shape` geom 写在 `force_f1` body 下时，返回的矩阵将该 geom 的局部向量转换到 `force_f1` 的坐标系。它不自动表示整个传感器的 `sensor_base`。这些 XML 对象的含义见[模型术语](index.md#model-terms)。

## 关键限制

这是绑定时读取并缓存的模型属性，**不是实时世界姿态，也不是关节运动后的旋转**。机器人运动不会让它变成动态姿态输入。

对象不存在，或所用物理后端不能提供该静态属性，会在准备阶段报错。

该能力供确实需要模型静态父坐标旋转的算法选择；若只需统一坐标系下的接触力，可直接在接触输入中指定 `frame`，由 Orca 完成变换。

[返回能力手册](index.md)
