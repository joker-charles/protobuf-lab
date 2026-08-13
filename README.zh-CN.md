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
- **protobuf merge 语义**：单值消息字段（含 oneof 内与 `optional<T>`
  消息成员）重复出现时合并而非替换。
- **序列化深度防护**：嵌套消息都经 `serialize()` 递归，用一个
  thread_local 计数器（上限 64，与解析侧一致）在病态嵌套击穿栈之前
  拦下。
- **未知字段保真**（`rpb::UnknownFields` 成员，opt-in）与 proto3
  默认值省略。

## 状态

官方 conformance 套件在 proto3 二进制 wire 格式上全绿：**637 个必测
`protobuf_test` 用例通过、0 失败**（标量、枚举、packed/unpacked 重复、
map 含 sint/fixed 键、oneof、消息合并、显式空消息、未知字段、非法标签、
截断输入）。JSON、text format、proto2 类目由 testee 跳过。runner 还会
报告 14 个 RECOMMENDED 级 packed/unpacked 输出形式 WARNING：两种编码都
合法，已在 `tests/conformance_failures.txt` 中记录为“评估后接受”（该文件
不含任何真实失败用例）。

## 项目定位

这是一个**实验性 wire codec，不是 protobuf 的重实现**。它只覆盖
struct 型消息的 proto3 二进制 wire 格式，并刻意复用 protobuf 自己的
wire 原语（`CodedInputStream`/`CodedOutputStream`）和官方测试套件。
实验的目的是证明 C++26 静态反射能驱动序列化——**无代码生成、无运行时
descriptor**，结构体本身就是 schema。protobuf 全量对齐（descriptor、
JSON/text 格式、proto2、扩展、跨语言工具链）明确不在范围内。实验的
成功标准：在有用的 proto3 子集上，“结构体即 schema”能否替代 protoc
管线？

## 环境要求

- **g++-16**（`-std=c++26 -freflection`；系统默认 g++ 不支持
  `-freflection`）。项目在 g++-16 16.1.0（stonking/26.10 仓库）验证。
- CMake >= 3.20。
- 首次 configure 需要网络：protobuf **v3.21.12** 通过 `FetchContent`
  自动拉取。

## 用法

codec 是**纯头文件库**（`src/codec.hpp`）：带注解的 struct 本身就是
schema，没有 `.proto` 文件、没有代码生成步骤。需要 g++-16 的 C++26
`-freflection`，`rpb` CMake 目标会自动传播这些要求。

### 作为 CMake 依赖

```cmake
include(FetchContent)
FetchContent_Declare(reflect_proto
  GIT_REPOSITORY https://github.com/joker-charles/protobuf-lab.git
  GIT_TAG v0.1.0)
FetchContent_MakeAvailable(reflect_proto)
target_link_libraries(your_target PRIVATE rpb)
```

### 或者：install + find_package

```sh
cmake --install build --prefix /usr/local   # 安装 codec.hpp 与 rpb CMake 包
```

```cmake
find_package(protobuf CONFIG REQUIRED)  # 需要先装好 protobuf
find_package(rpb CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE rpb::rpb)
```

### 最小示例

```cpp
#include "codec.hpp"

struct Greeting {
  [[=rpb::field_no<1>{}]] std::string name;
  [[=rpb::field_no<2>{}]] std::int32_t times;
};

int main() {
  Greeting g;
  g.name = "world";
  g.times = 3;
  std::string bytes;
  rpb::serialize(bytes, g);   // 0A 05 77 6F 72 6C 64 10 03

  Greeting back;
  rpb::parse(bytes, back);    // 解析与字段顺序无关；消息字段合并
  return 0;
}
```

可运行版本见 `examples/roundtrip.cpp`。

### 使用前要了解的限制

- 唯一硬性工具链要求：g++-16 的 C++26 `-freflection`。
- 仅支持 struct 型消息的 proto3 二进制 wire 格式；JSON、text format、
  proto2 未实现（见“项目定位”）。
- 字段号来自 `[[=rpb::field_no<N>{}]]` 注解：增删成员不改变 wire 格式，
  但改字段号会。
- 单值消息成员：需要真实 presence（含递归）时用 `std::unique_ptr<T>`；
  按值成员全默认时会被省略。
- 未知字段只在 struct 带 `rpb::UnknownFields` 成员时保留。

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
| `selftest` | 手算 wire 字节、oneof presence、optional 消息合并、深度 64 roundtrip、乱序字段按号发射、未知字段、group 跳过、截断 |
| `selftest_tt` | 完整官方 `TestAllTypesProto3` 镜像的 roundtrip |
| `interop` | 与 protoc 生成代码在 `tests/test.proto` 上逐字节比对 |
| `interop_tt` | 与 protobuf 官方 `test_messages_proto3.proto` 全 schema 逐字节比对 |
| `conformance` | 官方 conformance runner 对 `conformance_ours` 跑全套 |

GitHub Actions（`.github/workflows/ci.yml`，跑在官方 `gcc:16` 容器内）在
每次 push 时构建并执行全套 `ctest` 与消费者冒烟测试。

`verification/cpp26-features/run.sh` 重跑 AGENTS.md 与
`docs/reflect_error.md` 中编译器行为结论对应的单文件探针。

### 基准测试

`build/bench [iterations]` 在同一个 `TestAllTypesProto3` fixture 上对比
反射 codec 与 protoc 生成代码（固定迭代次数，只用 std::chrono，不引入
第三方基准库）：

```sh
cmake --build build -j --target bench
./build/bench 200000
```

输出 ns/op 表格、serialize/parse 比值与一个校验和（用于防止死代码消除）。
这是判断“结构体即 schema”实验是否值得继续的证据：若反射 codec 相对
protoc 生成代码只慢一个小的常数因子，无代码生成的收益可能值得；若差距
随消息规模/复杂度扩大，则要先优化热路径。

## 设计速览

成员携带 `[[=rpb::field_no<N>{}]]` 注解；consteval 阶段构建
`(字段号, 成员, 替代)` 表并按字段号排序（常量求值里的 `std::sort`），
同时校验布局。普通成员遵循 proto3 默认省略，oneof 替代设置即发射，
`unique_ptr` 消息成员有真实 presence，`UnknownFields` 在所有已知字段之后
重发。解析按字段号集合分派、与字段顺序无关。

## 仓库结构

```
src/            codec.hpp + 自测/CLI 二进制
bench/          rpb 对比 protobuf 的微基准（非 ctest）
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
- **多条目 map** 按排序序序列化；protobuf `Map` 是哈希序（未规定）。
  两者都是合法 wire 编码，但字节级 interop fixture 保持单条目。
- **序列化超深会 abort**：`serialize()` 与解析侧一样有 64 层嵌套深度
  防护；但 `parse()` 超深时优雅返回 `false`，而 `serialize()` 的公开
  接口没有错误通道，超深会触发 C++26 契约断言并经契约处理函数中止进程
  （库自带 weak 默认处理函数，应用可定义强符号版本覆盖）。
- **未知字段保真是 opt-in**（需要 `rpb::UnknownFields` 成员）；没有则
  跳过未知字段。

## 未来计划

按上述定位划分——工程收口 + 可选研究方向，不是通往 protobuf 全量对齐
的路线图。

### 工程收口（增量）

- **强制 RECOMMENDED 级 conformance**（当前只强制 REQUIRED；14 个
  packed/unpacked 输出形式差异已在 `tests/conformance_failures.txt`
  记录为接受的 WARNING）。
- **按 `bench/bench.cpp` 的数据优化序列化/解析热路径**（当前 fixture
  上约 1.1x serialize / 1.2x parse，相对 protoc 生成代码）。

### 研究方向（可选，非 parity 目标）

- **proto2 支持**（required、group、扩展、默认值），并启用 conformance
  的 proto2 半区——仅当“结构体即 schema”实验成功且确实需要 proto2 子集
  时才有意义。
- **JSON 与 text format 支持**，包括 well-known type 的 JSON 映射
  （Struct/Value/Any/Duration/Timestamp/FieldMask、NaN/Inf、枚举字符串化）。

## 许可证

[GPLv2](LICENSE)（`GPL-2.0-only`）。与 protobuf 的 BSD-3-Clause 兼容：
BSD-3 被 FSF 列为 GPLv2 兼容许可，且本仓库不含 protobuf 源码
（vendored 树仅作本地离线缓存，未入库）。
