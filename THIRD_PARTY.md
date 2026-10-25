# 第三方来源与许可

本仓库自有代码采用[GPL-3.0-only](LICENSE)。以下外部代码与参考材料保留原作者版权及原许可证；项目许可证不改写这些声明。

## 外部开源项目

| 来源 | 使用范围 | 版本与许可 |
|---|---|---|
| [Cubiomes](https://github.com/Cubitect/cubiomes)，Cubitect | `third_party/cubiomes/`中的噪声、RNG、生物群系及查询表子集，用于局部世界计算和结构环境判断 | 固定提交`e61f90580cbdd883214a8054670dacae655e59c0`；[MIT原文](third_party/cubiomes/LICENSE) |
| [JSON for Modern C++](https://github.com/nlohmann/json)，Niels Lohmann及贡献者 | `third_party/json.hpp`，用于配置、JSON Lines通信及结果序列化 | `v3.12.0`；[MIT原文](third_party/json-LICENSE.MIT)，头文件内其他原始声明保留 |
| [geode-finder](https://github.com/soulsoiledit/geode-finder)，soiledit | 晶洞模型的早期算法参考；现代活动范围的CPU滑动窗口粗筛参考其`src/search.rs` | 粗筛参考提交`70f5171e3ef1090f8bc02ab27c458f56080dab3c`；[MIT原文](third_party/geode-finder-LICENSE.MIT) |

Cubiomes的`noise.c`包含本项目针对负Y坐标及epsilon处理的局部修正。Cubiomes不提供本项目的晶洞生成、结构拼接、含水层或洞穴雕刻完整实现。

## 游戏行为与数据参照

[Minecraft Java Edition](https://www.minecraft.net/)由Mojang Studios/Microsoft开发，是晶洞、局部地形、结构生成及版本差异的行为参照。开发工具可读取本地游戏文件，生成原生数值算子、系数和几何常量；冻结回归保留坐标及判定结果。

仓库不分发Minecraft JAR、官方数据包、NBT模板或反编译缓存。游戏及官方资源的权利归原权利人，不能以本仓库GPL声明替代其授权。开发期Java参照工具不参与正式运行。

## 分发时保留的文件

分发本项目时保留[LICENSE](LICENSE)、本说明、相关MIT许可证及源码中的版权声明。分发二进制时还需按GPL第3版条款提供相应源代码。OpenCL运行时使用系统驱动，本仓库不分发显卡驱动或GPU SDK。
