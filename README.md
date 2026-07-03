# likuan

基于 AI 大模型辅助的 C 语言实践项目：正则表达式引擎（NFA/DFA 双模式）。

## 当前进度

已完成工程骨架、解析前端、AST 输出、最小 NFA 匹配闭环和区间量词：

- C17 + CMake 项目结构
- 对外 API：`regex_compile`、`regex_match`、`regex_search`、`regex_findall`、`regex_free`
- MVP 语法：普通字符、连接、`|`、`*`、`+`、`?`、`{m}`、`{m,}`、`{m,n}`、括号、`.`、字符类、`[^]`、`^`、`$`、`\d`、`\w`、`\s`
- 区间量词采用 Thompson NFA 展开，重复次数上限为 10000，防止异常模式消耗过多内存
- 编译期检查非法重复、倒置字符范围和不合法的范围端点
- 独立模块：`charset`、`lexer`、`parser`、`ast`，捕获组在 AST 中保留编号
- 单元测试：`test_lexer`、`test_parser`、`test_api`
- 命令行工具：`rx_cli`、`rx_dump_ast`
- 拆分 `src/regex_engine.c` 中剩余的 NFA 与 matcher 逻辑

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
./build/rx_dump_ast "([a-z]+)\\d{2,4}"
```

Windows 原生环境可安装 Visual Studio 2022 Build Tools（勾选“使用 C++ 的桌面开发”）和 CMake，然后在 Developer PowerShell 中执行：

```powershell
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

## 下一阶段


- 实现 NFA 状态转移表输出
- 增加捕获组位置返回
- 实现 NFA/DFA 状态表和 DOT 导出
- 实现子集构造、DFA 执行和 Hopcroft 最小化
