# 前十天功能审计

审计依据：

- `AI辅助C语言实践课程题目设计.docx` 中的基础要求、验收标准和两周安排。
- `正则表达式引擎系统设计文档.docx` 第 3、6、8、9、10 章。
- 当前 `main` 分支源码、测试、工具和每日开发记录。

## 验收矩阵

| 天数 | 计划交付物 | 当前结论 | 主要证据 |
|---|---|---|---|
| 第 1 天 | 读题、资料整理、语法/API 范围、系统设计 | 完成 | 两份原始 Word 文档、系统设计文档 |
| 第 2 天 | CMake 骨架、目录、测试命令、开发环境说明 | 完成 | `CMakeLists.txt`、`include/src/tests/tools/docs`、README |
| 第 3 天 | lexer、token、字符类和转义 | 完成 | `lexer.c`、`charset.c`、`test_lexer` |
| 第 4 天 | parser、AST、优先级、错误位置 | 完成 | `parser.c`、`ast.c`、`test_parser`、dump 工具 |
| 第 5 天 | Thompson NFA、状态表、NFA 匹配 | 完成 | `nfa.c`、`matcher.c`、`test_nfa`、`rx_dump_nfa` |
| 第 6 天 | epsilon-closure、move、子集构造 | 完成 | `dfa.c`、动态位图状态集合 |
| 第 7 天 | 字符压缩、锚点 DFA、DFA 执行器 | 完成 | 等价类、四种边界上下文、`test_dfa` |
| 第 8 天 | Hopcroft、统一 API 和错误码 | 完成 | `dfa_minimize.c`、`regex_engine.c` |
| 第 9 天 | 捕获回填、findall、边界集成 | 完成并修复 | `capture.c`、`regex_capture_count`、API 测试 |
| 第 10 天 | 对照测试、压力测试、benchmark、内存检查入口 | 已实现；外部工具验证待执行 | `test_stress`、`test_posix`、`rx_bench`、sanitizer CMake 选项 |

## 本次发现并修复

1. 当前提交中残留多处 Git 冲突标记，涉及 README、NFA、DFA、matcher 和 API，导致仓库源码不是稳定可重建状态。
2. 第 9 天叠加了两套捕获实现，一套固定最多 32 组，另一套为动态回填。
3. NFA 测试仍调用已经淘汰的旧函数签名，MSVC 报参数数量警告。
4. DFA 状态集合唯一性使用线性扫描，没有落实设计文档中的位图哈希去重。
5. 第 10 天缺少压力测试、POSIX 行为对照、性能基准和 sanitizer 构建入口。

修复后只保留一套动态捕获实现，`matcher.c` 负责整体 NFA 匹配，`capture.c` 负责指定区间的捕获回填。DFA 构造使用 FNV 风格位图哈希桶去重。

## 已执行验证

Windows 原生 Scope MSVC，`/W4`：

```text
lexer tests passed
parser tests passed
NFA tests passed
DFA tests passed
all tests passed
stress tests passed
```

全部 CLI/dump/benchmark 工具均重新编译，无编译警告。

Release 优化基准，20000 次：

| 场景 | NFA 匹配 us | DFA 匹配 us | 约加速 |
|---|---:|---:|---:|
| word-digits | 7.300 | 0.300 | 24.3x |
| alternation | 2.200 | 0.150 | 14.7x |
| nested-plus | 2.500 | 0.100 | 25.0x |
| identifier | 0.600 | 0.050 | 12.0x |

这些数据只用于本机功能验收，不替代 WSL/Linux 下的 POSIX 正式报告数据。

## 尚需外部环境执行

本机未安装 CMake、GCC、WSL 发行版、Valgrind，也没有 POSIX `<regex.h>`。以下项目已经配置，但本次无法现场执行：

- `test_posix` 的 Linux/POSIX 行为对照。
- `rx_bench --compare-posix` 的 80% 性能目标验证。
- ASan/UBSan 和 Valgrind 内存报告。

在 WSL/Linux 中执行 README 的构建、sanitizer 和 Valgrind 命令即可完成正式验收。
