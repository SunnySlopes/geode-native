# geode 要塞模块（1.18+）

要塞末影传送门与布局模拟，**内嵌在 geode 子模块内**（`tools/geode/src/stronghold_118/`）。

供 `src/structure_cover.h` 的覆盖接口调用，回答"某方块是否被要塞写入"。

## 为什么这里是自包含实现

geode 的 cubiomes 快照（`third_party/cubiomes/`）**只有 biomes/noise 那一半**：
没有 `finders.c`，没有 `features/`，也没有 `util.h`。`cmake/modules.json` 虽然
登记了 `Structures` 模块，但那些源文件从未导出 ——
`docs/说明/加载顺序与写入可见性.md` 记录了这个限制。

所以要塞相关代码放在本模块。它只消费 geode 已链接的 `GeodeCubiomes::Biomes`。

## 结构

```
src/stronghold_118/
├── sh1_18.h            公开接口
├── sh1_18.c            放置（同心环 + 112 格生物群系重定位）、精确 Y、传送门几何、眼数
├── sh_types.h          最小 cubiomes 表面（orientBox / Piece / salt / population seed）
├── sh118_selftest.c    与 standalone simulator 的真值对拍
├── sh118_ycheck.c      精确 Y 的专项验证
├── CMakeLists.txt
└── vendor/
    ├── stronghold_pieces.c   cubiomes features/stronghold.c 原样副本（+ 一处可选扩展）
    ├── stronghold_pieces.h
    └── piece.h
```

**布局生成器是内嵌的，不是重写的。** `vendor/stronghold_pieces.c` 是 cubiomes
`features/stronghold.c` 的原样副本（只改了 `#include` 行），那才是与 Java 版
逐位一致的实现。

对它的唯一功能性改动：新增 `getStrongholdPiecesEx()`，多一个可选的
`uint64_t *outRng` 输出参数，用于把布局结束时的 RNG 状态交回调用方（精确 Y
需要它）。`getStrongholdPieces()` 保持原签名，内部转发、行为完全不变。

## 版本范围

**1.18 – 26.3**。geode 的地形模型从 1.18 开始，本模块只服务该模型。

| 版本段 | 眼数 salt |
|---|---|
| 1.18 – 1.19.1 | `{5, 0}` |
| 1.19.2 – 1.19.3 | `{4, 8}` |
| 1.19.4 – 26.2 | `{4, 19}` |
| 26.3+ | `{4, 37}` |

## 关于 Y：**可以精确计算**

原版 `StrongholdStructure.generatePieces()` 第 42 行（1.21.11 / 26.1.2 / 26.2 /
26.3 **逐字节相同**）：

```java
do {
    ...
    startRoom.addChildren(startRoom, builder, context.random());
    while (!pendingChildren.isEmpty()) { ... }

    builder.moveBelowSeaLevel(context.chunkGenerator().getSeaLevel(),
                              context.chunkGenerator().getMinY(),
                              context.random(), 10);
} while (builder.isEmpty() || startRoom.portalRoomPiece == null);
```

这一行在**每轮无条件执行**，且只依赖 `seaLevel`（常量 63）与 `minY`（常量
-64），**不查地形高度图**。起点件 `StairsDown` 用
`makeBoundingBox(west, 64, north, ...)` 构造，Y 恒为 64，所以布局的相对 Y
也是确定的。

`StructurePiecesBuilder.moveBelowSeaLevel` 的实现：

```java
int maxY  = seaLevel - offset;                    // 53
int y1Pos = boundingBox.getYSpan() + minY + 1;    // ySpan - 63
if (y1Pos < maxY) y1Pos += random.nextInt(maxY - y1Pos);
int dy = y1Pos - boundingBox.maxY();
offsetPiecesVertically(dy);
```

那次 `nextInt` 取自**布局结束后**的 RNG 状态。本模块通过
`getStrongholdPiecesEx()` 拿到该状态后续算，因此 `dy` 精确可复现。

**结果**：`sh118_compute()` 返回的 `pieces[].nominalMinY/MaxY` 已经是**真实
世界 Y**（模块内部已施加 dy）。`dyExact` 恒为 1；`layoutMinY/layoutMaxY`
是施加 dy 之前的相对范围（对照用）。

实测（seed 123，每版本 24 座）：最高件 Y 落在 `[-30, 50]`，全部满足
`maxY <= seaLevel - 10 - 1 = 52`，且都在世界高度 `[-64, 319]` 内。
`sh118_ycheck` 逐项断言这些性质。

### cubiomes 为什么给不出

cubiomes 的 `features/stronghold.c` 把这段写成：

```c
// necessary for <=1.12.2 to simulate random calls
// optional for >1.12.2, could add flag for accurate heights
if (mc <= MC_1_12_2 && !env.portal) { ... }
```

只在 1.12 及更早、且**尚未**找到传送门房时执行 —— 对 1.13+ 是错的。
这就是"cubiomes 给不出要塞 Y"的来源。本模块按原版语义重算，**未修改共享
子模块**（避免影响 geode/trial/river 等其他引擎）。

## 集成

`tools/geode/CMakeLists.txt` 已挂好：

```cmake
option(GEODE_BUILD_STRONGHOLD "Build the 1.18+ stronghold portal simulator" ON)
if(GEODE_BUILD_STRONGHOLD)
    add_subdirectory(src/stronghold_118)
    add_library(GeodeStronghold::Sh118 ALIAS geode_stronghold_118)
endif()
```

消费者链接 `GeodeStronghold::Sh118`。`geode_core` 通过
`geode_structure_cover` 间接依赖它。

## 使用

```c
#include "sh1_18.h"

Sh118Stronghold p;
if (sh118_locateFirst(&p, MC_1_21_6, 123, generator) && p.ok) {
    /* generator 是 geode 已建好的 overworld Generator；传 NULL 则自建 */
    printf("portal (%d, %d), eyes %d/12, dy %d (exact=%d)\n",
           p.portalX, p.portalZ, p.eyes, p.dy, p.dyExact);
}

Sh118Piece pieces[SH118_PIECE_CAPACITY];
int n = sh118_compute(&p, pieces, SH118_PIECE_CAPACITY, MC_1_21_6, 123,
                      p.chunkX, p.chunkZ);
/* pieces[i].nominalMinY/MaxY 已是真实世界 Y */
```

| 函数 | 用途 |
|---|---|
| `sh118_compute(&p, pieces, n, mc, seed, cx, cz)` | 已知起始区块，算一座（含精确 Y） |
| `sh118_locateFirst(&p, mc, seed, gen)` | 原点最近的那座 |
| `sh118_locateAll(&arr, n, mc, seed, gen)` | 全部（≤128），复用传入的 Generator |
| `sh118_unionArea(rects, n, scratch)` | 任意矩形精确并集面积 |

传 `gen` 可避免每次重建 biome 生成器；这是 geode 里应该走的路径。

## 自检

```bash
cd tools/geode
cmake -S . -B build -DGEODE_BUILD_STRONGHOLD_SELFTEST=ON
cmake --build build --target sh118-selftest
./build/sh118-selftest
```

冻结的参考值（本模块必须满足的契约）：

| 版本 | start | portal | eyes | pieces |
|---|---|---|---|---|
| 1.21.6 | (-476, -2620) | (-521, -2651) | 4 | 227 |
| 1.18 | (-444, -2556) | (-401, -2528) | 1 | 363 |
| 26.3 | (-476, -2620) | (-521, -2651) | 2 | 227 |

外加 14 个版本 × 32 座要塞的不变式。实测 **448 座全部通过**。

## 踩过的坑

1. **`RingIter` 的 RNG 不能混用**：同心环几何和生物群系重定位都用 **Java LCG**
   （`uint64_t*` + `nextInt`/`nextDouble`），不是 xoroshiro。最初误用
   `xNextIntJ`，起手位置就错 32 格。
2. **`STRUCT()` / `JAVA_RANDOM` 已在 `rng.h` 定义**，`sh_types.h` 不能重复定义。
3. **`util.h` 不存在**，版本名表要自带。
4. **`vendor/` 保持原样**：只改 `#include` 行 + 新增 `getStrongholdPiecesEx`。
5. **精确 Y 的 RNG 复用**：`getStrongholdLoot` 内部不暴露布局 RNG，所以另跑一次
   `getStrongholdPiecesEx` 到独立缓冲区取状态（不能复用同一 `list`，否则
   覆盖掉 `room` 指针指向的数据）。
6. **眼数别自己重算**：直接取 `getStrongholdLoot` 填好的 `additionalData`。
   自己按 per-chunk 重播容易把区块范围或 RNG 类型（1.17 及更早用
   `JAVA_RANDOM`）搞错。
