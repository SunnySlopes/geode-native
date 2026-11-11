# 废弃矿井生成模块（geode 内嵌）

把废弃矿井（Mineshaft）的**完整生成结果**（piece 盒、入口盒、垂直偏移）提供给 geode 调用。

## 状态：可集成，生成层待最终对拍

| 项目 | 状态 |
|---|---|
| 库接口（`ms_generate` / `ms_probe`） | ✅ 完成 |
| 四个版本（1.21.11 / 26.1.2 / 26.2 / 26.3） | ✅ 已核对源码差异，均可运行 |
| RNG 层与 Java 逐位对拍 | ✅ 通过 |
| 暴力枚举器重构后结果一致性 | ✅ 与重构前逐字节相同（345416 盒） |
| **生成层与 Java 参考实现逐 piece 对拍** | ⚠️ **未通过**，见下 |

## ⚠️ 已知未解决问题

`tools/Bisect.java` + `MineshaftRef.java` 是我按 mojmap 源码**另写的一份独立 Java 参考实现**，用来对拍 C 版。目前两者在 piece 序列上仍不一致（同一 seed/chunk 下 70 vs 101 pieces）。

排查过程中已确认并修复的真实 bug：

1. **C 语言函数实参求值顺序未定义**：
   ```c
   // 错: 三个 nextInt(6) 在同一表达式里, 求值顺序未定义
   box = bbox_make(west, 50, north, west+7+nextInt(6), 54+nextInt(6), north+7+nextInt(6));
   ```
   这会让 x/y/z 三个跨度随机错配，房间形状与游戏不一致。已修复（先取临时变量）。

2. 我自己第一版 Java 参考实现也写错了两处（走廊 `addChildren` 顺序、把 `hasRails` 当岔道标志），已按源码修正。

**剩余差异尚未定位。** 由于两边都是手写翻译，"谁对"需要第三方权威，建议二选一：

- **推荐**：用 Gradle 构建 `guardian-mojmap` 源码树，写个测试直接跑 `MineshaftPieces` dump 盒列表（源码树当前无 class 文件，需联网拉依赖）。
- 或用一个已知种子的游戏内 `/locate structure mineshaft` 结果 + F3 校对。

在此之前，**不要把这个模块的结果当作与游戏一致的权威数据**。

## 版本差异考证（已完成）

逐行比对四个版本的 `MineshaftPieces.java` 与 `MineshaftStructure.java`：

| 版本 | MineshaftPieces | MineshaftStructure |
|---|---|---|
| 1.21.11 | 基准 | 基准 |
| 26.1.2 | **逐字节相同** | **逐字节相同** |
| 26.2 | 仅 `EntityType` → `EntityTypes` 改名 | **逐字节相同** |
| 26.3 | `builder` 参数位置前移（`random` 仍紧随其后）；RNG 调用序列不变 | **逐字节相同** |

关键 RNG 序列在 26.3 中与 1.21.11 完全一致：
`nextInt(100)` → `nextInt(4)`(十字) / `nextInt(3)+2`(走廊) → 碰撞通过后 `nextInt(3)` / `nextInt(23)`。

因此**四个版本共用同一套生成规则**，版本枚举只用于记录与取该版本的 `seaLevel`/`minY` 默认值（1.18+ 均为 63 / -64）。

## 接口

```c
#include "mineshaft_gen.h"

MineshaftGen *gen = malloc(sizeof(MineshaftGen));   /* ~400 KB */

MsGenOpts opts = {0};
opts.version  = MS_VERSION_26_3;
opts.type     = MS_TYPE_NORMAL;
opts.seaLevel = 63;
opts.minY     = -64;

if (ms_generate(gen, seed, chunkX, chunkZ, &opts) > 0) {
    /* gen->pieces[0..count)       piece 盒(depth-first, pieces[0] 恒为起点房间) */
    /* gen->entrances[0..n)        入口盒(不参与碰撞, 仅用于挖入口) */
    /* gen->result.structureBox    整体并集(已含垂直偏移 dy) */
    /* gen->result.dy              施加的垂直偏移 */
}

free(gen);
```

轻量查询（不持有 400 KB 工作区）：

```c
MsBox box; int pieces;
if (ms_probe(seed, chunkX, chunkZ, &opts, &box, &pieces) > 0) {
    /* 该区块有矿井, box 为整体范围 */
}
```

`MS_TYPE_MESA` 需要调用方提供 `opts.mesaSurface`（结构中心地表高度）——原版用 `chunkGenerator.getBaseHeight()`，本模块不建地形，**mesa 是近似模型**，normal 类型不受影响。

## 关键常量（源码依据）

| 项 | 值 | 来源 |
|---|---|---|
| `MAGIC_START_Y` | 50 | `MineshaftPieces.MAGIC_START_Y` |
| `MAX_DEPTH` | 8 | `generateAndAddPiece` 的 `depth > 8` |
| 80 格水平限制 | 80 | 同上，相对起点房间 `minX`/`minZ` |
| 房间盒 | `(west,50,north)`..`(west+7+r,54+r,north+7+r)`，r∈[0,6) | `MineShaftRoom` 构造 |
| 走廊长度 | `len ∈ {1,2,3,4}`（`nextInt(3)+2` 后递减），`blockLength=len*5` | `findCorridorSize` |
| 十字高度 | `y1 ∈ {2,6}`（`nextInt(4)==0` 取 6） | `findCrossing` |
| 楼梯盒 | `3×8×9` 或转置，固定 | `findStairs` |

**走廊长度易错点**：`nextInt(3)` 只调用一次然后递减，故 `blockLength ∈ {20,15,10,5}`，**包含 5**（单段走廊）。原版自检曾漏掉 5 导致 16 万次误报。

## 构建

```powershell
# geode 集成
cd tools/geode
cmake -S . -B build -DGEODE_BUILD_MINESHAFT_SELFTEST=ON
cmake --build build --target geode_mineshaft ms-selftest
```
