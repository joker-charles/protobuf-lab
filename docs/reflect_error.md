# GCC 16 反射编译错误记录

本文件按“错误 → 复现 → 原因 → 绕法”记录在 C++26 静态反射
（P2996 + P3394R4 注解）开发中遇到的编译器问题。每条均为
g++-16 (16.0.1 20260322, trunk r16-8246, experimental) 实测。

2026-08-12 起工具链为 g++-16 16.1.0-2ubuntu1（stonking 包），
本文件全部结论复验通过（详见第 1 条末尾）。

## 1. `typename [: ... :]::` 在求值表达式上下文中解析失败

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
struct field_no {
  using nested = std::integral_constant<std::uint32_t, N>;
  static constexpr std::uint32_t value = N;
};
}

struct S { [[=rpb::field_no<7>{}]] int a; };

template <std::meta::info M>
consteval std::uint32_t broken() {
  // 错误：typename + splice + :: 出现在求值表达式（return）里
  return typename [: std::meta::type_of(std::meta::annotations_of(M)[0]) :]::nested::value;
}
```

### 原因

GCC 16 只接受 `typename` + splice + `::` 出现在**类型上下文**
（`using` 声明、`__is_same` 等未求值操作数、模板实参）；
一旦出现在**求值表达式**（如 `return`）里就报
`expected '(' before ';'`。16.0.1 与 16.1.0 均复现，与注解机制无关。
注意：cppreference 的 splice specifiers 页里，`typename [:R:]::type`
是取**嵌套类型**的标准形式；取**静态数据成员**的标准形式是
作用域 splice `[:R:]::value`（不带 typename）——后者在 GCC 16 上
内联即用，根本不需要先绑别名。

### 绕法 A：静态成员用作用域 splice（不带 typename，可内联）

```cpp
template <std::meta::info M>
consteval std::uint32_t ok() {
  return [: std::meta::type_of(std::meta::annotations_of(M)[0]) :]::value;  // -> 7
}
```

### 绕法 B：嵌套类型走类型上下文 using（可内联）

```cpp
template <std::meta::info M>
consteval std::uint32_t ok() {
  using U = typename [: std::meta::type_of(std::meta::annotations_of(M)[0]) :]::nested;
  return U::value;  // -> 7
}
```

type-only 上下文（using 初始化）里 typename 可省略：`using U = [: ... :]::nested;`

### 绕法 C：先绑定别名（任何成员都适用，codec 现行写法）

```cpp
template <std::meta::info M>
consteval std::uint32_t ok() {
  using T = typename [: std::meta::type_of(std::meta::annotations_of(M)[0]) :];
  return T::nested::value;
}
```

### 绕法 D：注解值带数据成员，用 extract 取回（完全避开类型 splice）

```cpp
namespace rpb { struct field_no { std::uint32_t value; }; }  // structural

struct S { [[=rpb::field_no{7}]] int a; };

// ann 为 annotations_of / annotations_of_with_type 返回的注解 info
std::meta::extract<rpb::field_no>(ann).value;  // -> 7
```

### 16.1.0 复验（2026-08-12）

升级 g++-16 16.1.0 后复测并对照 cppreference
（cpp/language/splice_specifiers）：

- `typename [: expr :]::` 在求值表达式里仍是同一解析错误。
- 作用域 splice `[: expr :]::value`（静态成员，表达式上下文）内联可用。
- `typename [: expr :]::nested` 与 `typename template [:^^TCls:]<3>::type`
  在类型上下文内联可用。
- 绕法 C、D 以及 codec 同款的 namespace 级变量模板
  （`inline constexpr meta::info first_ann_v = annotations_of(M)[0]`）
  全部正常。
- 注意：`annotations_of` 只认**成员/枚举器**上的注解，查类型本身
  返回空 vector，`[0]` 会触发 libstdc++ hardening 断言
  （`__glibcxx_assert_fail` 非 constexpr）——不是编译器 bug，
  是查询对象搞错了。取成员注解必须先从
  `nonstatic_data_members_of(^^T, ctx)[I]` 拿到成员 info。
- 类型级注解 `[[=...]] struct S` 会被忽略（attribute 必须跟在
  `struct` 关键字后），本项目用不到，一律挂在成员上。

## 2. 相关要点（同属本会话验证）

- 取成员注解的 consteval 辅助函数必须用 NTTP 传 `meta::info`
  （`template <std::meta::info M> consteval ...`）；普通函数参数不是
  常量表达式，直接 `annotations_of(m)` 会报
  `'m' is not a constant expression`。
- P3394R4 注解语法 `[[=expr]]`（值须为 structural 类型）在本编译器
  可用；`annotations_of`、`annotations_of_with_type`、
  `std::meta::extract` 均验证可用，多注解按声明顺序返回。
- `annotations_of` / `annotations_of_with_type` 返回**瞬态 vector**：
  绑定到局部 constexpr 变量（`constexpr auto anns = annotations_of(M);`）
  报 "refers to a result of 'operator new'"（P1306R5 §3.2 同款限制）。
  取值时直接下标调用本身即可（`annotations_of(M)[K]` 在常量求值里合法），
  或者用 `template for` 内联 `std::define_static_array(annotations_of(M))`。

## 3. 注解取值：注解 info 不能直接 splice，用 scope splice 取静态成员

（2026-08-12，g++-16 16.1.0-2ubuntu1 实测）

### 注解 info 不能做表达式 splice

```cpp
// 错误：cannot use an annotation 'rpb::field_no{7}' in a splice expression
return [: std::meta::annotations_of_with_type(M, ^^rpb::field_no)[K] :].value;
```

P2996 的注解 info 不代表一个"实体"，GCC 16 拒绝把注解放进 splice。
取值只有两条路：`std::meta::extract<AnnType>(ann)`，或者把数值编码进
注解的**类型**、再用作用域 splice 取静态数据成员（绕法 A 落地）。

### 推荐形态：NTTP 注解类型 + scope splice（codec 现行写法）

```cpp
namespace rpb {
template <std::uint32_t N>
struct field_no {
  static constexpr std::uint32_t value = N;
};
}
struct S { [[=rpb::field_no<7>{}]] int a; };

template <typename T> struct is_field_no : std::false_type {};
template <std::uint32_t N> struct is_field_no<rpb::field_no<N>>
    : std::true_type {};

template <std::meta::info M, std::size_t K>
consteval std::uint32_t field_number() {
  std::size_t seen = 0;
  template for (constexpr auto ann :
                std::define_static_array(std::meta::annotations_of(M))) {
    using A = std::remove_cvref_t<typename [: std::meta::type_of(ann) :]>;
    if constexpr (is_field_no_v<A>) {
      if (seen == K)
        return [: std::meta::type_of(ann) :]::value;  // 无 extract
      ++seen;
    }
  }
  return 0;
}
```

### 实测要点

- `type_of(ann)` 返回的注解类型**带 cv/ref 限定**：实测
  `A`（未剥限定）`!= rpb::field_no<7>`，但
  `std::remove_cvref_t<A>` `== rpb::field_no<7>`。做类型匹配前必须先
  `remove_cvref_t`。
- `template for` 循环变量用 `constexpr auto`；循环体内可以改局部变量
  （`++n` 计数）也可以提前 `return`（本会话均验证可用）。
- 传统属性与值注解**不能混在同一个 `[[...]]` 列表**：
  `[[nodiscard, =rpb::field_no<7>{}]]` 报
  "mixing annotations and attributes in the same list"（16.1.0）。
  分两个列表 `[[deprecated]] [[=rpb::field_no<7>{}]]` 正常，且
  `annotations_of` 只返回值注解（`[[deprecated]]` 不占名额）。
- 若坚持 structural 值类型（`struct field_no { std::uint32_t value; };`
  + `[[=rpb::field_no{7}]]`），则必须走
  `std::meta::extract<rpb::field_no>(ann).value`——该路径本会话同样
  验证可用，只是代码里多一次 extract。
