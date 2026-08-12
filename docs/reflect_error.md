# GCC 16 反射编译错误记录

本文件按“错误 → 复现 → 原因 → 绕法”记录在 C++26 静态反射
（P2996 + P3394R4 注解）开发中遇到的编译器问题。每条均为
g++-16 (16.0.1 20260322, trunk r16-8246, experimental) 实测。

## 1. 类型 splice 内联 `::` 成员访问解析失败

- 标志：`-std=c++26 -freflection`
- 验证日期：2026-08-12
- 错误信息：

```
error: expected '(' before ';' token [-Wtemplate-body]
```

### 最小复现

```cpp
#include <meta>
#include <cstdint>

namespace rpb {
template <std::uint32_t N>
struct field_no { static constexpr std::uint32_t value = N; };
}

struct S { [[=rpb::field_no<7>{}]] int a; };

template <std::meta::info M>
consteval std::uint32_t broken() {
  // 错误：typename [: ... :] 后直接跟 :: 成员访问
  return typename [: std::meta::type_of(std::meta::annotations_of(M)[0]) :]::value;
}
```

### 原因

GCC 16 解析器无法处理「类型 splice 内联在表达式中、后面紧跟 `::`
成员访问」这一形式。模板类型（`field_no<7>`）和普通类型均复现，
与注解机制无关——已验证的 `using M = typename [: meta::type_of(r) :];`
写法不受影响。

### 绕法 A：先绑定别名

```cpp
template <std::meta::info M>
consteval std::uint32_t ok() {
  using T = typename [: std::meta::type_of(std::meta::annotations_of(M)[0]) :];
  return T::value;
}
```

### 绕法 B：注解值带数据成员，用 extract 取回（完全避开类型 splice）

```cpp
namespace rpb { struct field_no { std::uint32_t value; }; }  // structural

struct S { [[=rpb::field_no{7}]] int a; };

// ann 为 annotations_of / annotations_of_with_type 返回的注解 info
std::meta::extract<rpb::field_no>(ann).value;  // -> 7
```

## 2. 相关要点（同属本会话验证）

- 取成员注解的 consteval 辅助函数必须用 NTTP 传 `meta::info`
  （`template <std::meta::info M> consteval ...`）；普通函数参数不是
  常量表达式，直接 `annotations_of(m)` 会报
  `'m' is not a constant expression`。
- P3394R4 注解语法 `[[=expr]]`（值须为 structural 类型）在本编译器
  可用；`annotations_of`、`annotations_of_with_type`、
  `std::meta::extract` 均验证可用，多注解按声明顺序返回。
