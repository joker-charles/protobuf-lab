# Repository Guidelines

## Purpose

This repo (`/home/joker/apps/protobuf-lab`) hosts an **experimental protobuf
wire-format codec driven by C++26 static reflection** (`reflect-proto/`),
plus a vendored Ubuntu protobuf 3.21.12 source tree used as the reference
implementation (`protobuf-3.21.12/`, gitignored).

The document below is the accumulated, **verified** modern-C++ knowledge from
the coreutils factor pilot and this session. Read it before writing any
C++20/23/26 code; it exists so a fresh context does not have to relearn these
facts from web searches.

## Toolchain (critical)

- Reflection / contracts / `#embed` are **C++26 only** and need
  **`g++-16`** (`/usr/bin/g++-16`, 16.1.0-2ubuntu1, installed from the
  stonking/26.10 archive).
  The system default `g++` is 15 and **rejects `-freflection`**.
- Build with: `cmake -S reflect-proto -B reflect-proto/build
  -DCMAKE_CXX_COMPILER=g++-16 -DCMAKE_BUILD_TYPE=Release`
- Flags: `-std=c++26 -freflection`. Contracts are on by default in C++26;
  `-fno-contracts` disables them. `-fno-exceptions -fno-rtti` are safe (no
  exceptions/RTTI used) and shrink the dynamic binary ~17%.
- `g++-15 -std=c++23` is the stable fallback (no reflection/contracts/#embed).

## Verified C++26 reflection (P2996) on GCC 16 - 2026-08

### Available in `<meta>` (namespace `std::meta`)

- `access_context::unprivileged()` - **`nonstatic_data_members_of(info,
  access_context)` REQUIRES the second argument** (GCC has no default);
  `members_of(info, access_context)` needs it too.
- `enumerators_of(info)`, `nonstatic_data_members_of`, `members_of`,
  `parameters_of` all return `std::vector<meta::info>`.
- `type_of(info)`, `identifier_of(info)` (bare name, `std::string_view`),
  `u8identifier_of` (`u8string_view`), `display_string_of` (qualified name),
  `extract<T>(info)` (strings/objects only; throws on enumerators),
  `substitute(info, range_of_infos)` (e.g. `substitute(^^std::vector,
  {^^int})` -> `std::vector<int>`), `is_*_type` queries. **Note the `_type`
  suffix**: `is_enum_type` / `is_class_type`; plain `is_enum` / `is_class`
  do NOT exist in `std::meta`.
- `std::define_static_string/array/object` live in `std`, **not** `std::meta`,
  and return static-storage pointers/spans (`string` -> `const char*`,
  `array` -> `span`, `object` -> `const T*`). GCC quirks (verified 2026-08):
  (1) passing a **local** constexpr lvalue to `define_static_object` yields a
  consteval-only pointer; use an rvalue or a namespace-scope constexpr.
  (2) a **local** constexpr `span` from `define_static_array` is consteval-only
  at runtime unless its value is first demanded in a constant-evaluated
  context (e.g. `constexpr std::size_t n = es.size();` or a `static_assert` on
  it); carry the values out via constexpr locals. `define_static_string`
  pointers are runtime-usable directly.

### Missing / broken in GCC 16 (do not use)

- `for_each`, `name_of` (use `identifier_of`).
- `extract<int>` on an **enumerator** throws "value cannot be extracted".
- `typename [: expr :]::` in an **evaluated expression** (e.g. `return
  typename [: type_of(r) :]::nested::value;`) is a **parse error**
  (`expected '(' before ';'`, 16.0.1 and 16.1.0). Use the scope-splice form
  or move to a type context (see "Patterns that DO work").
- Splices on **regular range-for** loop variables fail ("consteval-only
  variable '__for_range' not declared 'constexpr'"). `template for` loop
  variables are fine (see below).
- Range splices in template argument lists; `^^` on non-type template params.
- `template for` directly over vector-returning reflection ranges is
  **ill-formed by design** (P1306R5 §3.2: needs non-transient constexpr
  allocation; GCC 16: "refers to a result of 'operator new'").
  The paper's fix is `define_static_array(...)`.

### Patterns that DO work (use these)

- Get member infos by index as constexpr variables:
  `constexpr meta::info r = meta::nonstatic_data_members_of(^^T,
  meta::access_context::unprivileged())[I];`
- Member type + access:
  `using M = typename [: meta::type_of(r) :];` then `v.[:r:]`.
- **Scope splice for static data members** (no `typename`, expression
  context, inline OK): `[: meta::type_of(ann) :]::value` (verified 2026-08,
  16.1.0). `typename [:R:]::value` is the wrong spelling for a non-type
  member - `typename` is only for nested *types*.
- **Type splices in type contexts** work inline even with function calls:
  `using U = typename [: expr :]::nested;` (or `using U = [: expr :]::nested;`
  in type-only contexts), `typename template [:^^TCls:]<3>::type`,
  `__is_same(typename [: expr :]::nested, ...)`.
- `annotations_of` / `annotations_of_with_type` only read annotations on
  **members and enumerators** - querying a *type* returns an empty vector
  (subscripting it then trips the libstdc++ hardening assert). Always go
  through `nonstatic_data_members_of(^^T, ctx)[I]` first.
- Value splice: bind to a local first, then convert:
  `auto e = [: r :];` / `int x = static_cast<int>(e);`. Direct casts of a
  **bound constexpr info** also work: `static_cast<int>([:r:])`,
  `(int)v.[:r:]` (verified 2026-08; an earlier note claimed these fail).
  Direct cast of the **`template for` loop variable itself** fails
  ("consteval-only expressions are only allowed in a constant-evaluated
  context") - bind `auto e = [:r:]` first there.
- Subscript splices work with a constant index: `[: es[I] :]` (enumerator
  value), `v.[: ms[I] :]` (member access) - an earlier note claimed these
  fail.
- Member count: `meta::nonstatic_data_members_of(^^T, ctx).size()` in a
  constexpr variable template.
- `template for (constexpr auto r : define_static_array(...))` **works with
  direct splices in the expansion body** (verified on g++-16, 2026-08):
  `auto e = [: r :]` (enumerators), `v.[:r:] = ...` (member access),
  `typename [: meta::type_of(r) :]` (member type). factor's option table
  (`src/factor.cpp:79`) uses the direct enumerator splice.
- The `template for` range must be the **inline** `define_static_array(...)`
  expression, or a `static` / namespace-scope constexpr variable. A plain
  local `constexpr auto es = define_static_array(...)` fails ("address of
  non-static constexpr variable ... may differ on each invocation; add
  'static'").
- An NTTP helper also works: `template <meta::info r> consteval ...` called
  as `helper<r>()` inside the `template for` body.
- **Do NOT** pass the loop variable **by value** into a `consteval` helper
  whose body splices the parameter: GCC 16 checks the body at definition and
  a function parameter is not a constant expression there ("'r' is not a
  constant expression"). This corrects an earlier note that claimed the
  opposite.
- The robust codec pattern used here: `std::index_sequence` +
  `member_v<T,I>` - no `template for` needed.

## Other verified modern-C++ facts

- `#embed` (P1967): directive must be alone on its line; GCC does **not**
  search `-I` paths for it - the file must sit next to the source or use an
  absolute path. `__cpp_pp_embed` = 202502L.
- Contracts: `contract_assert(cond)` keyword; linking requires a user-defined
  `void handle_contract_violation(std::contracts::contract_violation const&)`
  (`<contracts>`). The accessor is `.comment()` (there is **no** `.message()`).
  Default `contract_assert` semantics: handler is called, then the program
  terminates (abort). `.text` delta on a tiny TU is ~0.2KB for one
  assert + handler; the ~9KB figure below was measured on factor's larger
  binary - context-dependent.
- Checked arithmetic: `ckd_add/ckd_sub/ckd_mul` via `<stdckdint.h>` (C++26).
- `std::print` pulls in `<ostream>`; startup cost is dominated by the dynamic
  loader + libstdc++ relocations (~6300 RELA), not by print/format. **Static
  linking (`-static`) gives the fastest startup** (factor: 1.05ms -> 0.52ms).
- Performance: `unsigned __int128` single-multiply in `umul_ppmm` (~1.6x on
  Pollard rho); `std::countr_zero` instead of bit-loops in GCD (~2x).
- C++26 mode adds ~19KB .text vs C++23 (our contracts ~9KB + libstdc++
  C++26 hardening ~10KB); reflection table data itself is ~free. On a trivial
  TU (no templates/contracts) the two modes are identical (263 vs 263 bytes
  .text) - the delta only shows up with real template/contract usage.

### Verification harness

The reflection/contracts/#embed claims above are backed by one-file-per-claim
tests in `verification/cpp26-features/`; run `./run.sh` (needs g++-16) to
re-verify. Wrong conclusions found in 2026-08 are corrected inline above.

### protobuf 3.21 API gotchas (verified while writing the codec)

- `CodedOutputStream::WriteString(const std::string&)` writes the **raw
  bytes only - no length prefix**. Emit the length yourself
  (`WriteVarint32(size)` + `WriteRaw`), or use `WriteStringWithSizeToArray`.
- `CodedInputStream::ReadRaw(void* buffer, int size)` copies **into a caller
  buffer** (newer protobuf uses a `const void**` out-parameter). Passing a
  `const void**` corrupts the stack - this caused a segfault/stack-smash.
- `CodedInputStream::ReadTag()` takes **no arguments** and returns `uint32_t`
  (0 = EOF) in 3.21; newer versions take a `uint32_t*` out-param.

## Offline docs (read these before web search)

- `/home/joker/apps/cppreference-docs` - offline cppreference (C++20/23
  complete; C++26 as of the 2025-02 snapshot).
- `/home/joker/apps/gcc-docs-16.2.0` - GCC 16.2 manual + `cxx-status.html`
  (official C++26 = experimental status).

## Reference implementation: factor pilot

`/home/joker/apps/coreutils-9.4/cpp/factor` is the proven migration template:
CMake presets (`cpp23`, `cpp23-gmp`, `cpp26`, `cpp26-gmp`, `sanitize`,
`static`), behavior-baseline freezing, differential testing vs GNU, a C++26
reflection-generated getopt table, `#embed` prime tables, contracts, and
`std::expected`/`format`/`span` modernization. Consult it for idioms.

## Codec design conventions (reflect-proto)

- Field numbers are explicit (P3394R4 annotations): every member except
  `UnknownFields` carries `[[=rpb::field_no<N>{}]]`; ordinary members exactly
  one, `OneOf` members one per alternative (annotation order ↔ std::variant
  alternative order).  The number rides on the annotation type and is read
  with a scope splice (`[: type_of(ann) :]::value`) -- no
  `std::meta::extract`.  Known fields serialize in ascending field-number
  order (compile-time table, consteval `std::sort`); declaration order is
  irrelevant.  Validated at compile time: missing annotation, duplicate or
  zero field numbers, `OneOf` annotation-count mismatch, `unique_ptr`
  pointee not a message type, and vector/map/optional/oneof/`UnknownFields`
  alternatives all `static_assert`.
- Type mapping: string/bytes -> LEN; integral/enum -> varint (sign-extended);
  `SInt<T>` -> zigzag varint; `Fixed32/SFixed32` -> 4-byte LE;
  `Fixed64/SFixed64` -> 8-byte LE; float/double -> 4/8-byte LE; packable
  vectors -> packed LEN; `vector<string/message>` -> repeated LEN;
  `vector<Unpacked<T>>` -> repeated unpacked (tag per element);
  `std::map<K,V>` -> repeated map-entry messages (key=1/value=2, serialized
  in `std::map` sorted order; protobuf map order is unspecified, so
  byte-level interop uses single-entry maps); `optional<T>` -> presence;
  `OneOf<Ts...>` (`std::variant<std::monostate, Ts...>`) -> oneof, one wire
  field per alternative, presence semantics (set -> emit, even defaults),
  last-wins on parse; `std::unique_ptr<T>` -> singular message with real
  presence (null omitted; breaks by-value recursion cycles, e.g.
  `TestAllTypesProto3.recursive_message`); nested struct -> embedded
  message.
- proto3 semantics: default-valued scalar/string/enum members and empty
  packed vectors are omitted; nested messages serialize unless all their
  members are default/absent (value semantics cannot distinguish unset from
  present-but-empty); optionals-with-value and non-null `unique_ptr` always
  serialize; empty repeated string elements still emit.  `rpb::deep_equal`
  compares messages with unique_ptr members by dereferencing recursively
  (containers/strings/variants use `==`, everything else is compared
  member-wise via reflection).
- Unknown fields: an `rpb::UnknownFields` member (any position, no
  annotation) captures and re-emits them after all known fields; without it
  they are skipped. Groups are skipped, not captured.
- Official-schema differential tests: `interop_tt` byte-compares our codec
  against protoc-generated code on protobuf's own
  `test_messages_proto3.proto` `TestAllTypesProto3` (mirror in
  `reflect-proto/src/test_messages.hpp`). Recursive fields
  (`NestedMessage.corecursive`, `recursive_message`) are covered through
  `std::unique_ptr`. Known unrepresentable bits, omitted from the mirror
  and left unset in the shared fixture: `oneof_string` (bytes == string
  collides in `std::variant`), Struct/Value/ListValue (mutually
  recursive), and sint/fixed **map keys** (60-65: zigzag/fixed key
  encoding needs wrapper key types that `std::map` cannot order).
  `ctype=STRING_PIECE/CORD` fields have private accessors in generated
  3.21 code; `ref_main_tt` sets them via `TextFormat::MergeFromString`.
- libprotobuf is linked only for wire primitives (`CodedInputStream`/
  `CodedOutputStream`); there is no descriptor/reflection runtime by design.
