# reflect-proto

一个由 **C++26 静态反射**（P2996）与值注解（P3394R4）驱动的 **protobuf
wire 格式编解码器**。没有运行时 `Descriptor`/`Reflection`——C++ 类型本身
就是 schema。wire 原语来自 `libprotobuf`（`CodedInputStream` /
`CodedOutputStream`），正确性通过 protoc 生成代码的逐字节比对和官方
conformance 套件验证。

## 亮点

- **显式字段号**：`[[=rpb::field_no<N>{}]]` 注解（P3394R4），用作用域
  splice 读回——不经过 `std::meta::extract`。
- **Oneof**：`rpb::OneOf<Ts...>`（`std::variant<std::monostate, Ts...>`），
  每个替代一个 wire 字段，presence 语义，解析 last-wins。
- **递归消息**：`std::unique_ptr<T>` 成员（C++ 值语义无法表达自递归）；
  单值消息字段具有真实的 proto3 presence。
- **可深拷贝消息**：反射驱动的 `rpb::deep_copy`——带 `unique_ptr` 成员的
  struct 保持 protobuf 风格的值语义（每次拷贝独立存储），无需手写拷贝构造。
- **编译期校验**：字段号缺失/重复、oneof 注解数或替代类型违规、
  `unique_ptr` 指向非消息类型，全部 `static_assert` 报错。
- **protobuf merge 语义**：单值消息字段（含 oneof 内）重复出现时合并
  而非替换。
- **未知字段保真**（`rpb::UnknownFields` 成员，opt-in）与 proto3
  默认值省略。

## 状态

官方 conformance 套件在 proto3 二进制 wire 格式上全绿：**637 个必测
`protobuf_test` 用例通过、0 失败**（标量、枚举、packed/unpacked 重复、
map 含 sint/fixed 键、oneof、消息合并、显式空消息、未知字段、非法标签、
截断输入）。JSON、text format、proto2 类目由 testee 跳过。
`tests/conformance_failures.txt` 当前为空。

## 环境要求

- **g++-16**（`-std=c++26 -freflection`；系统默认 g++ 不支持
  `-freflection`）。项目在 g++-16 16.1.0（stonking/26.10 仓库）验证。
- CMake >= 3.20。
- 首次 configure 需要网络：protobuf **v3.21.12** 通过 `FetchContent`
  自动拉取。

## 构建与测试

```sh
cmake -S . -B build -DCMAKE_CXX_COMPILER=g++-16 -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

离线构建可复用本地 protobuf 检出：

```sh
cmake -S . -B build -DCMAKE_CXX_COMPILER=g++-16 \
  -DFETCHCONTENT_SOURCE_DIR_PROTOBUF=$PWD/protobuf-3.21.12
```

### 测试

| ctest 名称 | 覆盖内容 |
| --- | --- |
| `selftest` | 手算 wire 字节、oneof presence、乱序字段按号发射、未知字段、group 跳过、截断 |
| `selftest_tt` | 官方 `TestAllTypesProto3` 镜像的 roundtrip |
| `interop` | 与 protoc 生成代码在 `tests/test.proto` 上逐字节比对 |
| `interop_tt` | 与 protobuf 官方 `test_messages_proto3.proto` 逐字节比对 |
| `conformance` | 官方 conformance runner 对 `conformance_ours` 跑全套 |

`verification/cpp26-features/run.sh` 重跑 AGENTS.md 与
`docs/reflect_error.md` 中编译器行为结论对应的单文件探针。

## 设计速览

成员携带 `[[=rpb::field_no<N>{}]]` 注解；consteval 阶段构建
`(字段号, 成员, 替代)` 表并按字段号排序（常量求值里的 `std::sort`），
同时校验布局。普通成员遵循 proto3 默认省略，oneof 替代设置即发射，
`unique_ptr` 消息成员有真实 presence，`UnknownFields` 在所有已知字段之后
重发。解析按字段号集合分派、与字段顺序无关。

## 仓库结构

```
src/            codec.hpp + 自测/CLI 二进制
tests/          test.proto、参考 fixture、interop/conformance 脚本
cmake/          protobuf conformance.cmake 补丁（PATCH_COMMAND 自动应用）
docs/           GCC 16 反射错误记录
verification/   单文件编译器行为探针（run.sh）
```

## 已知限制

- **proto2**、**JSON**、**text format** 未实现；conformance testee 跳过
  这些类目。
- **按值消息成员**（仍然支持；如自测里的 `Person.home`）：全默认的按值
  嵌套消息视为未设置而省略，因此这类成员无法表达“设置了但为空”。官方
  `TestAllTypesProto3` 镜像已通过把所有单值消息成员改成 `std::unique_ptr`
  绕开此限制；用户 struct 若保留按值成员，在需要真实 presence 时应使用
  `std::unique_ptr<T>` / `std::optional<T>`。
- **`std::optional<T>` 消息成员**重复出现时替换而非合并（普通成员与
  oneof 已实现合并）。
- **多条目 map** 按排序序序列化；protobuf `Map` 是哈希序（未规定）。
  两者都是合法 wire 编码，但字节级 interop fixture 保持单条目。
- **序列化没有深度限制**（解析有 64 层）；极端嵌套结构可能在 serialize
  时爆栈。
- **未知字段保真是 opt-in**（需要 `rpb::UnknownFields` 成员）；没有则
  跳过未知字段。

## 未来计划

- **proto2 支持**（required、group、扩展、默认值），并启用 conformance
  的 proto2 半区。
- **JSON 与 text format 支持**，包括 well-known type 的 JSON 映射
  （Struct/Value/Any/Duration/Timestamp/FieldMask、NaN/Inf、枚举字符串化）。
- **序列化深度限制**，对齐解析器的 64 层递归防护。
- **`optional<T>` 消息合并**，实现与 protobuf merge 语义的完全对齐。
- **补全 `TestAllTypesProto3` 镜像**：repeated wrappers（211-219）、
  Duration/Timestamp/FieldMask/Any（301-315）、fieldname*（401-418）。
- **强制 RECOMMENDED 级 conformance**（当前只强制 REQUIRED；
  packed/unpacked 输出形式差异仅是警告）。
- **CI**：GitHub Actions 用 g++-16（需 stonking/26.10 源或兼容镜像）
  构建并跑 `ctest`，包含 conformance 套件。
- **基准测试**：与官方实现对比，优化序列化/解析热路径。

## 许可证

[GPLv2](LICENSE)（`GPL-2.0-only`）。与 protobuf 的 BSD-3-Clause 兼容：
BSD-3 被 FSF 列为 GPLv2 兼容许可，且本仓库不含 protobuf 源码
（vendored 树仅作本地离线缓存，未入库）。
