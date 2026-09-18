# 接触力网格：`orca.contact_grid.v1`

需要 Orca 先把接触力汇总成固定网格，再交给动态库计算时，选择这个能力。厂商声明网格大小和视场；场景绑定一个 site，确定测量原点、方向和接触候选范围。

site 是模型中带位置和方向的参考标记，自身不会产生接触力；见[模型术语](index.md#model-terms)。

支持的 SDK 目标：`1.0.0`，契约使用 `schema_version: 4`。完整样板见 [ContactGrid](../../examples/contact_grid/README.md)。

## 最小声明

将下面一项放入 `contract.json` 的 `inputs`：

```json
{
  "name": "force_grid",
  "capability": "orca.contact_grid.v1",
  "resolution": [4, 4],
  "fov_degrees": [120, 120]
}
```

| 参数 | 必填 | 含义 |
| --- | --- | --- |
| `name` | 是 | 生成的 C 输入成员名，本例为 `force_grid` |
| `capability` | 是 | 固定填 `orca.contact_grid.v1` |
| `resolution` | 是 | `[行数, 列数]`，每项为 1–256 的整数 |
| `fov_degrees` | 是 | `[水平全角, 垂直全角]`，单位度，每项大于 0 且不超过 180 |

不填写 `objects`、`frame`、`fields` 或 `capacity`。测量 site 由[场景实例绑定](../../README.md#scene-site-binding)，不是契约里重复填写的型号内部名称。网格形状由 `resolution` 确定，仍受整个输入的 ABI 大小限制。

## 动态库收到什么

本例生成固定成员 `double force_grid[4][4][3]`，行优先排列。每格三个数依次是 **Fx、Fy、Fz**，单位 N，均在绑定 site 的局部坐标系表达。没有 `force_grid_count`，也不是按名字查找的字典。

在厂商回调已检查输入大小并取得生成结构后，可直接读取：

```cpp
const auto& input = *static_cast<const ContactGridInput*>(input_data);
double fx = input.force_grid[row][col][0];
double fy = input.force_grid[row][col][1];
double fz = input.force_grid[row][col][2];
```

这里的 `ContactGridInput` 来自样板契约的 `c_struct`。成员名由厂商的 `name` 决定，类型和形状由能力定义及 `resolution` 决定。

每个物理子步，Orca 从一致的物理源状态采集接触、site 位姿和力，重新组装整张网格。没有有效接触时传全零网格；如需该输入的时间，同时声明 [采样时间](sampling.md#time)。非有限数据报错，不作为有效输出发布。

## Orca 如何选择和分箱

1. 找到 site 所属的刚性焊接组，即模型中无关节固定连接的刚体集合。
2. 只保留接触双方中**恰好一方**属于该组的已求解接触；双方都在组内或都不在组内的接触不计入。
3. 将接触位置，以及作用在该组上的法向力与切向力之和，转换到 site 局部坐标系。
4. 只保留 site 前方 `z > 0`、且在视场内的接触，按角度分箱；同一格的力按带正负号的三维向量求和。

网格是**等角度网格**，不是按毫米等分触面。对局部接触位置 `(x, y, z)`：

```text
水平角 = atan2(x, z)
垂直角 = atan2(y, sqrt(x*x + z*z))
```

`[120, 120]` 表示水平、垂直分别覆盖 `[-60°, +60°]`。列随朝 +X 的水平角增大，行随朝 +Y 的垂直角增大；+Z 为前方。外侧视场边界包含在内，落入第一格或最后一格。`z == 0` 的点不参与，因此绑定的测量 site 应位于有效触面后方，不能直接放在同一触面平面上并期望采到该平面的接触。这不要求修改型号用于安装的 `mount_site`。

## 接触范围与输出含义

刚性焊接组是**接触候选集合，不是传感器型号资产的边界**。例如多个触面固定在同一手指上，它们可能属于同一组；不同 site 依靠各自原点、姿态和视场形成查询，范围可能重叠。绑定不同实例可隔离动态库状态，但不会自动划分接触归属。此能力没有按指定 geom 过滤或按距离截断的参数；需要精确选择型号内触面并自行处理接触记录时，使用 [接触数据](contact.md)。

若 site 属于世界固定组，其他固定在世界上的几何也可能属于同一候选组，不能将其理解为只有 site 直接父 body。

平台传入的是每格合力，不是压力、胶面形变或真实传感器读数，也不保留逐接触位置。ContactGrid 样板动态库再计算：

```text
输出[row, col] = norm(该格合力) × gain
```

输入是先求矢量和、再由动态库取模，因此相反方向的力可能抵消；它不等于逐接触力模长之和。`gain` 是创建实例时传入的算法参数，不参与 Orca 分箱，也不会缩放模型。输出、噪声、标定和响应模型仍由厂商负责。
