# likuan

基于 AI 大模型辅助的 C 语言实践项目：正则表达式引擎（NFA/DFA 双模式）。

## 当前进度

已完成工程骨架、解析前端、Thompson NFA、子集构造 DFA、状态转移表和 NFA/DFA 双模式匹配：

- C17 + CMake 项目结构
- 对外 API：`regex_compile`、`regex_compile_ex`、`regex_match`、`regex_search`、`regex_findall`、`regex_free`
- MVP 语法：普通字符、连接、`|`、`*`、`+`、`?`、`{m}`、`{m,}`、`{m,n}`、括号、`.`、字符类、`[^]`、`^`、`$`、`\d`、`\w`、`\s`
- 区间量词采用 Thompson NFA 展开，重复次数上限为 10000，防止异常模式消耗过多内存
- 编译期检查非法重复、倒置字符范围和不合法的范围端点
- `regex_compile_ex` 返回带字节位置的详细错误，括号嵌套上限为 256
- `RX_FLAG_DFA` 可选择 DFA 执行器，默认仍使用 NFA
- DFA 状态上限为 4096；当前 DFA 模式暂不支持 `^`、`$`，NFA 模式正常支持
- 独立模块：`charset`、`lexer`、`parser`、`ast`、`nfa`、`matcher`、`dfa`
- 单元测试：`test_lexer`、`test_parser`、`test_nfa`、`test_dfa`、`test_api`
- 命令行工具：`rx_cli`、`rx_dump_tokens`、`rx_dump_ast`、`rx_dump_nfa`、`rx_dump_dfa`

## 构建与测试

推荐在 WSL2 Ubuntu 中执行：

```bash
sudo apt update
sudo apt install -y build-essential cmake gdb valgrind graphviz git

cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

命令行试用：

```bash
./build/rx_cli "[a-z]+\\d+" "abc123"
./build/rx_dump_tokens "([a-z]+)\\d{2,4}"
./build/rx_dump_ast "([a-z]+)\\d{2,4}"
./build/rx_dump_nfa "a|b"
./build/rx_dump_dfa "a|b"
./build/rx_cli --dfa "([a-z]+)\\d{2,4}" "abc123"
```

Windows 原生环境可安装 Visual Studio 2022 Build Tools（勾选“使用 C++ 的桌面开发”）和 CMake，然后在 Developer PowerShell 中执行：

```powershell
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

## 下一阶段

- 增加捕获组位置返回
- 完善 DFA 模式的 `^`、`$` 锚点语义
- 实现 NFA/DFA DOT 导出
- 实现 Hopcroft 最小化
