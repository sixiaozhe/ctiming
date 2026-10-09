# 贡献指南

感谢参与 ctiming！欢迎提交 Issue 与 Pull Request。

## 开发环境

- Linux（glibc），CMake ≥ 3.16
- C11 与 C++17 编译器（GCC 或 Clang）

```bash
git clone <repo-url> ctiming
cd ctiming
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## 提交前检查

请确保构建无警告、全部测试通过：

```bash
cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure
```

新增功能请补充相应的单元测试（`tests/test_*.c` / `tests/test_*.cpp`）或端到端测试（`tests/integration_*.sh`）。

## 代码风格

- 运行时库为 C11，分析器为 C++17；不引入第三方依赖。
- **代码中不写注释**（本项目的约定）。
- 保持与现有文件一致的命名与格式（2 空格缩进；`ct_` / `ctiming_` 前缀；`CT_NOINSTR` / `CTIMING_HIDDEN` 属性）。
- 运行时热路径（插桩钩子）应避免分配与锁。

## Pull Request

- 一个 PR 聚焦一件事，说明动机、改动与验证方式。
- 若改动 `.ctrace` 格式或 `analysis.json` 字段，请同步更新 `docs/` 与相关测试。
- CI 必须通过。

## 报告问题

请在 Issue 中附上：复现命令、程序平台/编译器版本、`ctiming` 版本、期望与实际行为；如涉及分析结果，请附最小化的 `.ctrace` 或生成步骤。
