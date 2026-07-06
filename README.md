# likuan

基于 AI 大模型辅助的 C17 正则表达式引擎，支持 NFA、DFA 和最小化 DFA 执行路径。

## 已实现功能

- 语法：普通字符、连接、`|`、`*`、`+`、`?`、`{m}`、`{m,}`、`{m,n}`、括号、`.`、字符类、`[^]`、`^`、`$`。
- 转义：`\d`、`\D`、`\w`、`\W`、`\s`、`\S`、`\t`、`\n`、`\r`。
- 解析：lexer、递归下降 parser、AST、字节位置错误信息。
- NFA：Thompson 构造、状态转移表、最左且同起点最长匹配。
- DFA：子集构造、位图哈希去重、字符等价类压缩、边界锚点上下文。
- MinDFA：Hopcroft 分区细化，自动合并等价状态。
- 捕获组：`matches[0]` 为整体，`matches[1..]` 为括号分组，未参与组返回 `[-1,-1]`。
- DFA 捕获：DFA 定位整体区间，再用带标签 NFA 回填分组。
- API：`regex_compile`、`regex_compile_ex`、`regex_match`、`regex_search`、`regex_findall`、`regex_capture_count`、`regex_get_stats`、`regex_free`。
- 工具：CLI、token/AST/NFA/DFA dump、NFA/DFA/POSIX benchmark。
- 测试：lexer、parser、NFA、DFA、API、压力测试、90 次跨模式符合性执行，以及 Linux 上自动启用的 POSIX 对照测试。

区间量词上限为 10000，DFA 状态上限为 4096，括号嵌套上限为 256。

## 构建与测试

推荐使用 WSL2 Ubuntu：

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

## 使用示例

```bash
./build/rx_cli --dfa "([a-z]+)(\\d+)" "--abc123!"
./build/rx_dump_tokens "([a-z]+)\\d{2,4}"
./build/rx_dump_ast "([a-z]+)\\d{2,4}"
./build/rx_dump_nfa "(a|b)*"
./build/rx_dump_dfa "^abc$"
```

## 性能对比

Release 模式运行内置基准：

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

自定义模式和文本：

```bash
./build-release/rx_bench --iterations 100000 --compare-posix \
  "[a-z]+[0-9]{2,4}" "prefix abc123 suffix"
```

在具有 `<regex.h>` 的 Linux/WSL 环境中，CMake 会自动构建 `test_posix`，`rx_bench --compare-posix` 会输出 DFA 相对 POSIX 的匹配速度百分比。Windows 原生构建仍可比较 NFA 与 DFA。

## 下一阶段

- 在 WSL/Linux Release 环境采集正式性能数据并生成对比表。
- 实现 NFA/DFA DOT 导出。
- 整理最终实验报告、AI 对话记录和答辩演示材料。
