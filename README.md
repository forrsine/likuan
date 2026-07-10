# likuan

`likuan` 是一个用 C17 实现的正则表达式引擎实验项目。项目从词法分析、语法分析、AST 构建开始，生成 Thompson NFA，并进一步支持子集构造 DFA、Hopcroft 最小化 DFA、捕获组回填、Graphviz DOT 导出和性能基准测试。

这个仓库适合作为课程设计、实验汇报或正则引擎实现学习材料：既可以直接运行命令行工具观察匹配结果，也可以通过 dump 工具查看 token、AST、NFA、DFA 和最小化 DFA 的中间表示。

## 功能概览

- 正则语法：普通字符、连接、选择 `|`、闭包 `*`、正闭包 `+`、可选 `?`、区间量词 `{m}` / `{m,}` / `{m,n}`、括号分组、`.`、字符类 `[]` / `[^]`、行首 `^`、行尾 `$`。
- 转义字符：`\d`、`\D`、`\w`、`\W`、`\s`、`\S`、`\t`、`\n`、`\r`。
- 前端解析：lexer、递归下降 parser、AST 节点、字节位置错误信息。
- NFA 执行：Thompson 构造、状态转移表、最左优先且同起点最长匹配。
- DFA 执行：子集构造、位图哈希去重、字符等价类压缩、锚点上下文处理。
- 最小化 DFA：Hopcroft 分区细化，自动合并等价状态。
- 捕获组：`matches[0]` 表示整体匹配，`matches[1..]` 表示括号分组；未参与匹配的分组返回 `[-1,-1]`。
- DFA 捕获：先用 DFA 定位整体匹配区间，再用带标签的 NFA 回填分组边界。
- 可视化：支持导出 NFA / MinDFA 的 Graphviz DOT 文件。
- 测试与基准：包含 lexer、parser、NFA、DFA、DOT、API、压力、符合性测试，以及 Linux/WSL 下的 POSIX 对照测试。

当前限制：

- 区间量词上限：`10000`
- DFA 状态上限：`4096`
- 括号嵌套上限：`256`

## 目录结构

```text
.
├── include/              # 对外 API 头文件
├── src/                  # 引擎核心实现
├── tests/                # 单元测试、压力测试、符合性测试
├── tools/                # CLI、dump、benchmark 工具
├── scripts/              # Windows 构建脚本和演示脚本
├── docs/                 # 进度记录、性能数据、DOT 示例
├── build-manual/         # Windows 手动构建输出目录
├── out/                  # 演示输出目录
└── CMakeLists.txt        # CMake 构建配置
```

核心模块对应关系：

- `lexer.c` / `parser.c` / `ast.c`：正则表达式前端解析。
- `nfa.c` / `matcher.c`：Thompson NFA 构造与 NFA 匹配。
- `dfa.c` / `dfa_minimize.c`：DFA 构造、压缩与最小化。
- `capture.c`：捕获组记录与回填。
- `dot.c`：自动机 DOT 导出。
- `regex_engine.c`：对外 API 封装。

## Windows 构建

本项目提供了不依赖 CMake 的 Windows 构建脚本，默认输出到 `build-manual`。

构建并运行全部测试：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-msvc.ps1 -Test
```

只构建：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-msvc.ps1
```

构建 Release 版本：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-msvc.ps1 -Configuration Release
```

Release 版本常用于性能测试：

```powershell
.\build-manual\rx_bench.exe --iterations 100000
```

构建脚本会生成 8 个测试程序和 7 个工具程序：

- 测试：`test_lexer`、`test_parser`、`test_nfa`、`test_dfa`、`test_dot`、`test_api`、`test_stress`、`test_conformance`
- 工具：`rx_cli`、`rx_dump_tokens`、`rx_dump_ast`、`rx_dump_nfa`、`rx_dump_dfa`、`rx_dump_dot`、`rx_bench`

## Linux / WSL2 构建

推荐在 WSL2 Ubuntu 中使用 CMake：

```bash
sudo apt update
sudo apt install -y build-essential cmake gdb valgrind graphviz git

cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

启用 AddressSanitizer 和 UndefinedBehaviorSanitizer：

```bash
cmake -S . -B build-asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DRX_ENABLE_SANITIZERS=ON
cmake --build build-asan -j
ctest --test-dir build-asan --output-on-failure
```

运行 Valgrind：

```bash
valgrind --leak-check=full --error-exitcode=1 ./build/test_api
valgrind --leak-check=full --error-exitcode=1 ./build/test_stress
```

如果系统提供 `<regex.h>`，CMake 会自动构建 `test_posix`，用于与 POSIX regex 做行为对照。

## 快速使用

Windows PowerShell：

```powershell
.\build-manual\rx_cli.exe --dfa '([a-z]+)(\d+)' '--abc123!'
.\build-manual\rx_dump_tokens.exe '([a-z]+)\d{2,4}'
.\build-manual\rx_dump_ast.exe '([a-z]+)\d{2,4}'
.\build-manual\rx_dump_nfa.exe '(a|b)*'
.\build-manual\rx_dump_dfa.exe '^abc$'
.\build-manual\rx_dump_dot.exe --dfa --output mindfa.dot '^abc$'
```

Linux / WSL：

```bash
./build/rx_cli --dfa "([a-z]+)(\\d+)" "--abc123!"
./build/rx_dump_tokens "([a-z]+)\\d{2,4}"
./build/rx_dump_ast "([a-z]+)\\d{2,4}"
./build/rx_dump_nfa "(a|b)*"
./build/rx_dump_dfa "^abc$"
./build/rx_dump_dot --nfa --output nfa.dot "([a-z]+)\\d{2,4}"
./build/rx_dump_dot --dfa --output mindfa.dot "([a-z]+)\\d{2,4}"
dot -Tpng mindfa.dot -o mindfa.png
```

`rx_cli` 默认使用 NFA 搜索；传入 `--dfa` 后使用 DFA 搜索。匹配成功时会输出整体匹配区间和捕获组区间。

## 一键演示

演示脚本会构建 Release 版本，并依次展示 DFA 捕获组、token、AST、NFA、MinDFA、DOT 导出和 benchmark。输出文件会放到 `out/demo`。

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\demo.ps1
```

同时运行完整回归测试：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\demo.ps1 -RunTests
```

调整 benchmark 迭代次数：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\demo.ps1 -Iterations 100000
```

如果本机安装了 Graphviz，脚本会额外把 DOT 文件渲染成 PNG；否则只生成 DOT 文件。

## C API

公共接口位于 `include/regex_engine.h`。

常用函数：

- `regex_compile` / `regex_compile_ex`：编译正则表达式。
- `regex_match`：要求从文本起点开始匹配。
- `regex_search`：在文本中搜索第一个匹配。
- `regex_findall`：遍历所有匹配。
- `regex_capture_count`：获取捕获组数量。
- `regex_get_stats`：读取 NFA / DFA 状态数量等统计信息。
- `regex_error` / `regex_status_string`：读取错误信息。
- `regex_free`：释放编译后的正则对象。

最小示例：

```c
#include "regex_engine.h"

#include <stdio.h>

int main(void)
{
    rx_regex_t *re = NULL;
    rx_match_t matches[3];
    char error[256];

    int rc = regex_compile_ex(&re, "([a-z]+)(\\d+)", RX_FLAG_DFA,
                              error, sizeof(error));
    if (rc != RX_OK) {
        printf("compile failed: %s\n", error);
        return 1;
    }

    rc = regex_search(re, "--abc123!", matches, 3);
    if (rc == RX_OK) {
        printf("match: [%d,%d)\n", matches[0].rm_so, matches[0].rm_eo);
        printf("group1: [%d,%d)\n", matches[1].rm_so, matches[1].rm_eo);
        printf("group2: [%d,%d)\n", matches[2].rm_so, matches[2].rm_eo);
    }

    regex_free(re);
    return rc == RX_OK ? 0 : 1;
}
```

可选编译标志：

- `RX_FLAG_NONE`：使用 NFA 路径。
- `RX_FLAG_DFA`：使用 DFA 路径。

## 性能测试

Windows：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-msvc.ps1 -Configuration Release
.\build-manual\rx_bench.exe --iterations 100000
```

Linux / WSL：

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
./build-release/rx_bench --iterations 100000 --compare-posix
```

保存为 CSV：

```bash
./build-release/rx_bench --iterations 100000 --compare-posix \
  --csv docs/performance.csv
```

自定义 pattern 和 text：

```bash
./build-release/rx_bench --iterations 100000 --compare-posix \
  "[a-z]+[0-9]{2,4}" "prefix abc123 suffix"
```

`rx_bench` 会输出编译耗时、匹配耗时、NFA 状态数、DFA 子集状态数、最小化 DFA 状态数、字符等价类数量，以及 DFA 相对 NFA / POSIX 的匹配速度比例。

## 测试说明

当前测试覆盖：

- lexer token 识别
- parser AST 结构
- Thompson NFA 构造
- DFA 构造与最小化
- DOT 导出
- 对外 API 与捕获组
- 压力测试
- NFA / DFA 跨模式符合性测试
- Linux / WSL 下的 POSIX 行为对照

在 Windows 上运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-msvc.ps1 -Test
```

在 Linux / WSL 上运行：

```bash
ctest --test-dir build --output-on-failure
```

## 汇报材料建议

用于演示或答辩时，可以按下面顺序展示：

1. 使用 `rx_cli --dfa` 展示一次包含捕获组的搜索。
2. 使用 `rx_dump_tokens` 和 `rx_dump_ast` 展示前端解析。
3. 使用 `rx_dump_nfa` 展示 Thompson NFA。
4. 使用 `rx_dump_dfa` 展示子集构造和最小化结果。
5. 使用 `rx_dump_dot` 导出 DOT，并用 Graphviz 渲染自动机图。
6. 使用 `rx_bench` 展示 NFA、DFA 和 POSIX 的性能对比。

也可以直接运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\demo.ps1 -RunTests
```
