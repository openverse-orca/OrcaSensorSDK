# 采样信息：时间、子步时长、子步编号

这三项能力说明“当前这批输入来自哪个物理子步”，厂商按需选择，不需要绑定模型对象。

一个物理子步是物理引擎推进一次状态的过程。应用的一次控制或环境步可能包含多个物理子步；这里的时间、时长和编号都对应物理子步。

## 最小声明

以下每个 JSON 都是一项独立请求；把需要的项放进 `contract.json` 的 `inputs` 数组。

<a id="time"></a>

采样时间：

```json
{
  "name": "sample_time",
  "capability": "orca.sample.time.v1"
}
```

<a id="dt"></a>

物理子步时长：

```json
{
  "name": "sample_dt",
  "capability": "orca.sample.dt.v1"
}
```

<a id="index"></a>

物理子步编号：

```json
{
  "name": "sample_index",
  "capability": "orca.sample.index.v1"
}
```

## 可填参数

| 参数 | 必填 | 默认值 | 含义 |
|---|---|---|---|
| `name` | 是 | — | 动态库输入成员名，可自行命名 |
| `capability` | 是 | — | 上述三种能力之一 |
| `dtype` | 否 | 由能力决定 | 时间、时长为 `f64`，编号为 `u64`；显式填写只能与能力一致 |
| `shape` | 否 | `[]` | 都是单个值；显式填写只能为 `[]` |

不填写 `object`、`objects`、`frame` 或 `fields`。每项直接生成一个成员，不是包含子字段的记录。

## 动态库收到什么

| 能力 | 本例成员 | dtype / C 类型 | shape | 单位与含义 |
|---|---|---|---|---|
| `orca.sample.time.v1` | `data.sample_time` | `f64` / `double` | `[]` | s；当前输入对应的仿真源时间 `t_k` |
| `orca.sample.dt.v1` | `data.sample_dt` | `f64` / `double` | `[]` | s；一个物理子步的正时长 |
| `orca.sample.index.v1` | `data.sample_index` | `u64` / `uint64_t` | `[]` | 无量纲；reset 后从 0 开始的物理子步编号 |

## 时序怎么理解

假设 reset 的仿真时间为 0，物理子步时长为 0.002 s，连续三次输入的 `(time, dt, index)` 为：

```text
(0.000, 0.002, 0)
(0.002, 0.002, 1)
(0.004, 0.002, 2)
```

`time` 是物理源状态 `t_k` 的时间，不是步进返回后已经积分到的 `t_{k+1}`，更不是计算机的实际日期和时间。`dt` 和 `index` 都按物理子步计算；一次 `env.step` 包含多个物理子步时，不能把它们当作环境步时长和编号。

prepare（准备运行）和 reset（重置）本身不产生新的动态样本；第一个物理子步完成后才有第一批输入。例中第一批输入的 `time` 为源状态的 0.000 s，而不是步进返回时的时间。reset 会重置编号，采样时间则跟随仿真重置后的时间，不保证总是从 0 s 开始。

完整用例：[TouchGrid 契约](../../examples/touch_grid/contract.json)

[返回能力手册](index.md)
