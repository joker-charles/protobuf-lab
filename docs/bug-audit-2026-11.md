# protobuf-lab 缺陷排查与修复记录（2026-11）

基线：`f85af79`（proto2 group 支持 + JSON/text 完成）。排查方式：官方 conformance
基线、与 protobuf 3.21.12 生成代码/`json_util`/`TextFormat` 的差分、ASan/UBSan
和随机/变异 fuzz。所有临时 harness 在 `build-explore/`（gitignored），未进入仓库。

## 1. 基线状态

- 全量构建（protobuf 3.21.12 经 `ghfast.top` 镜像拉取）通过。
- `ctest` 6/6；官方 conformance：proto3 binary/JSON + proto2 binary/JSON walk
  `1939 successes, 0 unexpected failures`，text walk `116 successes, 0 unexpected
  failures`。
- 即：**官方用例全绿**，本次修复的是未被 conformance 固定用例覆盖的潜在缺陷。

## 2. 已修复缺陷

### 2.1 binary codec（`src/codec.hpp`）

| 级别 | 缺陷 | 修复 |
|---|---|---|
| 安全 | LEN 长度未校验，6 字节输入可触发 4 GiB 分配（`bad_alloc`/ASan allocation-size-too-big） | 统一 `read_length()`：64 位 varint + `INT_MAX` + 当前 limit 剩余字节校验；`parse()`/map entry 显式 `PushLimit` |
| 安全 | 长度 varint 超 32 位被截断、packed 长度 `0xFFFFFFFF` 导致 `PushLimit(-1)` 空循环接受 | 同上；packed/map/string/unknown 全部走 `read_length()` |
| 安全 | 已知字段解析中途失败后仍走 unknown fallback：接受畸形输入、把已知字段号写成 unknown、`serialize(parse(x))` 不可再解析、可绕过深度守卫 | `parse_table_result` 三态分派（not_found/ok/error）；只有 `not_found` 才 capture/skip；wire type 不匹配按 unknown 处理（protobuf 语义），匹配但载荷畸形直接失败。unknown-field capture 自身也改三态（captured/unsupported/error）：截断的 LEN 载荷直接失败，不再消费 length 后回退 `skip_field` 从错误位置重读 |
| 正确性 | 字面 `0x00` tag、截断/超宽 tag varint、尾随垃圾被当作 EOF | `read_tag_checked()`：tag ≤5 字节、field ≤2^29-1；EOF 需未消费任何字节 |
| 正确性 | packed repeated 接受任意 wire type（3/4/5/7） | 只接受 packed(wt2) 或元素自身 wire type |
| 安全 | group 递归绕过两个深度计数；unknown group 跳过无限递归导致 SIGSEGV | 深度守卫移入 `serialize_to_cos`；`parse_group_body` 计数；`skip_field` 增加 `skip_depth_v` |
| 正确性 | group body 类型绕过 `check_layout` | `serialize_to_cos`/`parse_group_body` 入口调用 `check_layout<T>()` |
| 正确性 | `field_no > 2^29-1` 可编译并产生溢出 tag | `layout_numbers_ok` 增加上限 `static_assert` |
| 正确性 | `-0.0` 被当作默认值省略 | 浮点默认值比较改为 raw-bits 语义（`v == 0.0 && !signbit(v)`） |
| 正确性 | map entry key/value wire type 错误时整体拒绝 | 与 protobuf 一致：作为 entry 内 unknown 跳过，默认 key/value 生效 |
| 可用性 | 零成员 message 无法编译 | `field_table_size_impl` 一元 fold 加初值 |
| 正确性 | bare-struct group 默认值仍输出 START/END | 无 presence 的 group 在 body 为空时省略 |
| 测试辅助 | `deep_equal` 对容器用 `==`，与 `deep_copy` 不对称 | vector/map/optional 逐元素递归（`vector<bool>` 例外） |

### 2.2 JSON（`src/json.hpp`）

| 级别 | 缺陷 | 修复 |
|---|---|---|
| 正确性 | 1970 前非整天 Timestamp 打印日期 +1 天 | `seconds/86400` 改为 floor 除法（负余数借位） |
| 正确性 | `Duration{0, nanos<0}` 丢负号（`-0.5s` → `0.5s`） | 统一按符号 + 绝对值格式化 |
| 正确性 | FieldMask 拒绝 `fooBar.bazQux`、把 `foo1` 错误转成 `foo_1`、拒绝 `_` | 按 protobuf `FieldMaskUtil` 语义重写 parse/print |
| 正确性 | Timestamp 接受 `2021-02-31`/`2019-02-29`/`00:00:60`/`+24:00`/`+99:99`；>9 位小数被拒 | 完整公历/offset/闰秒校验；fraction 截断前 9 位 |
| 可用性 | 顶层 WKT（Timestamp/Duration/Any/…）的 `json_parse`/`json_serialize` 走普通 message 路径 | 新增 `is_wkt_type_v`，顶层 WKT 走 `json_parse_value`/`json_print_value`；`google.protobuf.Value` 的顶层 null 可往返，其余 WKT 拒绝 null 根 |

### 2.3 text format（`src/text_format.hpp`, `src/test_messages.hpp`）

| 级别 | 缺陷 | 修复 |
|---|---|---|
| 安全 | float fallback 的 `strtod(std::string(tok).c_str(), &end)` 悬垂指针（ASan stack-use-after-scope） | 临时串保活 + 完整消费校验 |
| 正确性 | map 的 message 值 `value { ... }` 无法解析（与自身 printer 不一致） | 冒号可选 |
| 正确性 | 枚举 map 值 `value: BAZ` 因空格被拒 | `parse_ident` 先 skip_ws |
| 正确性 | `google.protobuf.Value` text 字段名全空（缺 `rpb::name`） | 补 6 个 name 注解（null/number/string/bool/struct/list_value） |
| 正确性 | 未知负枚举 printer 输出但 parser 拒绝；枚举范围用 unsigned | enum 走 `underlying_type_t` 有符号校验 |
| 正确性 | 八进制当十进制（`010`→10）、十六进制拒绝、整数溢出回绕 | 0x/八进制/十进制 + `unsigned __int128` 溢出检查 |
| 正确性 | `\x4142` 被截断为 `B` | `\x` 最多 2 位 hex |
| 正确性 | 重复 singular/oneof/message 字段 last-wins | 按 field table 记录 seen，第二次出现拒绝（repeated/map 除外） |
| 正确性 | `1.5f`/`1f` 拒绝、list 尾逗号接受、map 缺 key 拒绝、前导 `+` 接受 | 已修复 |

## 3. 验证结果

- `ctest` **8/8**：`selftest`、`selftest_tt`、`json_selftest`、`text_selftest`、
  `interop_wkt`、`interop`、`interop_tt`、`conformance`。
- 官方 conformance：`1938 successes, 1 expected failure, 0 unexpected`
  （proto3/JSON + proto2 binary/JSON），text `116/0`。expected failure 见 §4。
- ASan+UBSan：`selftest`/`selftest_tt`/`json_selftest`/`text_selftest`/
  `interop_wkt` 全部干净。
- 差分 fuzz（最终工作树）：
  - binary Person 随机 20k 例：0 差异；
  - hardening 变异/非法输入 20k 例：0 crash/0 超大分配；剩余 187 例全部是
    “proto3 string 非法 UTF-8，protobuf C++ 拒绝、本 codec 接受”的既有差异；
  - proto2 group happy-path 60,030 checks / 0 failures；group/unknown-group
    深度攻击返回 false，不再栈溢出；
  - JSON Person 5k、WKT-as-fields 1k（含 dotted FieldMask）：0 差异；
  - text random 3000 双向 0 reject/0 mismatch；self_tt 29/29；hostile 241 例
    0 value-diff。

## 4. 有意的偏离 / 已知限制（P3）

- **proto3 string 非法 UTF-8**：protobuf C++ 解析时拒绝，本 codec 不校验（binary
  parse）。合法数据 interop 不受影响；未在本次修复范围内（需要为每个 string
  member 增加 UTF-8 扫描，影响性能，且 conformance 未覆盖）。
- **text string 非法 UTF-8**：本 codec 拒绝（protobuf TextFormat 接受），因为
  conformance REQUIRED 用例 `StringFieldBadUTF8Octal/Hex` 要求拒绝；bytes 不校验。
  由此文本打印的非法 UTF-8 string 不能 self-round-trip（与 protobuf C++ 的
  printer/parser 不对称一致）。
- **FieldMask `_`**：`Recommended.Proto3.JsonInput.FieldMaskInvalidCharacter`
  （输入 `"foo,bar_bar"`）期望解析失败，我们的行为与 protobuf C++ 一致地接受，
  故列入 `tests/conformance_failures.txt`（protobuf 自己的
  `failure_list_cpp.txt` 也列出该项）。
- **text 宽松语法**：`//` 注释、前导 `;`/`,`、map 方括号列表、`\uD800`/
  `\U00110000`、map entry 内重复 key/value（我们 last-wins）等仍与 protobuf
  文本 tokenizer 有差异；属低优先级，未改。
- **proto2 required/extensions**：codec 无该语义，维持既有文档化限制。
- **text 递归**：无 `kMaxSerializeDepth` 上限（与 protobuf text 一致，且比
  protobuf 更不易栈溢出）。

## 5. 复现命令

```sh
# 拉取 protobuf（GitHub 不可达时）
git clone --depth 1 --branch v3.21.12 \
  https://ghfast.top/https://github.com/protocolbuffers/protobuf.git protobuf-3.21.12
( cd protobuf-3.21.12 && git apply ../cmake/protobuf-conformance.patch )

cmake -S . -B build -DCMAKE_CXX_COMPILER=g++-16 -DCMAKE_BUILD_TYPE=Release \
  -DFETCHCONTENT_SOURCE_DIR_PROTOBUF=$PWD/protobuf-3.21.12
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

## 6. Header split (2026-11, pure refactor)

The monolithic headers were split without changing the public API (`#include
"codec.hpp"`, `"json.hpp"`, `"text_format.hpp"` keep working):

| Umbrella | Split into |
|---|---|
| `src/codec.hpp` | `src/rpb/{annotations,traits,layout,wire,group,core,serialize,parse,deep}.hpp` |
| `src/json.hpp` | `src/rpb/json/{dom,wkt,fwd,parse,print,any}.hpp` |
| `src/text_format.hpp` | `src/rpb/text/{lexer,any_fwd,any,parse,print}.hpp` |

Every sub-header is `#pragma once` and self-contained; the umbrella includes
them in dependency order.  Cyclic dependencies (Any registry <-> parse/print)
are broken with dedicated `fwd`/`any_fwd` headers.  The split is pure code motion modulo two deliberate cleanups: the unused
`member_inner_ptr` helper overloads and one duplicate pair of forward
declarations were dropped.  This was verified at token level (comment/
whitespace-insensitive token multisets and order-preserving subsequences
against the original files); behavior is unchanged:

- `ctest` 8/8, conformance `1938/1 + 116/0`, `interop*` byte-identical.
- An installed-header consumer (`cmake --install`, then compile with only
  `-I <prefix>/include`) exercises wire + JSON + text round-trips.
- CMake: `reflect_compile` adds the `src/` include root for in-tree targets,
  and `install` now ships the umbrella headers plus the `rpb/` tree.
