# 第 11 天 NFA/DFA 性能报告

## 测试目的

比较同一套正则语法在 NFA 与最小化 DFA 模式下的编译成本、单次 search 耗时和自动机状态数量，并为后续 POSIX `regex.h` 对照保留同一套基准入口。

## 测试环境

- 平台：Windows 原生。
- 编译器：Scope MSVC。
- 优化：`/O2`。
- 警告：`/W4`。
- 每个场景匹配次数：100000。
- 计时方式：C 标准库 `clock()`，结果为每次操作平均微秒。

本机没有 WSL 发行版和 POSIX `regex.h`，因此本表只包含 NFA 与 DFA。POSIX 正式数据需要在 WSL/Linux Release 构建中补采。

## 结果

| 场景 | 模式 | 编译 us | 匹配 us | NFA 状态 | 子集 DFA | MinDFA | 字符类 |
|---|---|---:|---:|---:|---:|---:|---:|
| word-digits | NFA | 1.000 | 5.360 | 16 | 0 | 0 | 0 |
| word-digits | DFA | 65.000 | 0.190 | 16 | 6 | 6 | 3 |
| alternation | NFA | 4.000 | 1.450 | 48 | 0 | 0 | 0 |
| alternation | DFA | 270.500 | 0.100 | 48 | 20 | 18 | 13 |
| nested-plus | NFA | 1.500 | 1.950 | 12 | 0 | 0 | 0 |
| nested-plus | DFA | 22.000 | 0.100 | 12 | 4 | 3 | 2 |
| identifier | NFA | 1.000 | 0.580 | 6 | 0 | 0 | 0 |
| identifier | DFA | 12.500 | 0.040 | 6 | 3 | 2 | 3 |

原始数据见 `performance-day11.csv`。

## 分析

| 场景 | DFA 相对 NFA 匹配速度 |
|---|---:|
| word-digits | 28.21x |
| alternation | 14.50x |
| nested-plus | 19.50x |
| identifier | 14.50x |

- DFA 的匹配阶段没有动态状态集合扩展，因此重复执行时明显更快。
- DFA 编译需要子集构造、字符等价类计算和 Hopcroft 最小化，编译成本高于 NFA。
- `alternation` 从 20 个子集状态最小化到 18 个状态；`nested-plus` 从 4 个状态降到 3 个；`identifier` 从 3 个状态降到 2 个。
- 对一次性短文本，NFA 较低的编译成本可能更合适；对同一模式的大量重复匹配，DFA 更有优势。
- 当前数字来自 Windows `clock()`，极短耗时可能受计时器分辨率影响，报告最终版应以 WSL/Linux 高迭代 Release 数据复核。

## 正确性配套验证

`test_conformance` 包含 45 个模式/文本/操作组合，每个组合同时执行 NFA 与 DFA，共 90 次，结果全部通过。`test_stress` 继续覆盖 4096 字节 nested-plus、失败尾字符、64 个捕获组和 1000 次编译释放。

## 复现命令

```powershell
.\build-manual\rx_bench.exe --iterations 100000 --csv ..\docs\performance-day11.csv
.\build-manual\test_conformance.exe
.\build-manual\test_stress.exe
```

WSL/Linux POSIX 对比：

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
ctest --test-dir build-release --output-on-failure
./build-release/rx_bench --iterations 100000 --compare-posix \
  --csv docs/performance-posix.csv
```
