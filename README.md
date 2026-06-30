# likuan

基于 AI 大模型辅助的 C 语言实践项目：正则表达式引擎（NFA/DFA 双模式）。

## 当前进度

已完成第一步工程骨架和最小 NFA 匹配闭环：

- C17 + CMake 项目结构
- 对外 API：`regex_compile`、`regex_match`、`regex_search`、`regex_findall`、`regex_free`
- MVP 语法：普通字符、连接、`|`、`*`、`+`、`?`、括号、`.`、字符类、`[^]`、`^`、`$`、`\d`、`\w`、`\s`
- 单元测试：`tests/test_api.c`
- 命令行 Demo：`tools/rx_cli.c`

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
```

## 下一阶段

- 拆分 `src/regex_engine.c` 为 lexer/parser/nfa/matcher 等模块
- 实现 `{m,n}` 展开
- 增加捕获组位置返回
- 实现 NFA/DFA 状态表和 DOT 导出
- 实现子集构造、DFA 执行和 Hopcroft 最小化

