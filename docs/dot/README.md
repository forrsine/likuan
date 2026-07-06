# DOT 示例

本目录包含答辩要求的三组自动机示例：

- `a|b`
- `a*`
- `([a-z]+)\d{2,4}`

每组分别提供 Thompson NFA 和 Hopcroft 最小化后的 DFA。

安装 Graphviz 后可渲染：

```powershell
dot -Tpng a-or-b.nfa.dot -o a-or-b.nfa.png
dot -Tpng a-or-b.mindfa.dot -o a-or-b.mindfa.png
```

重新生成：

```powershell
.\build-manual\rx_dump_dot.exe --nfa --output docs\dot\a-or-b.nfa.dot 'a|b'
.\build-manual\rx_dump_dot.exe --dfa --output docs\dot\a-or-b.mindfa.dot 'a|b'
```
