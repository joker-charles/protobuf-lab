# Repository Guidelines

## Purpose

This repo (`/home/rocky/Projects/json/protobuf-lab`) hosts an **experimental
protobuf codec driven by C++26 static reflection** (sources in `src/`),
plus a local vendored protobuf 3.21.12 tree (`protobuf-3.21.12/`,
gitignored) that doubles as the offline FetchContent cache.  Fresh clones
fetch protobuf 3.21.12 from GitHub automatically (see CMakeLists.txt); in
networks where GitHub is unreachable use the `RPB_PROTOBUF_REPO` cache
variable with a mirror, e.g.
`-DRPB_PROTOBUF_REPO=https://ghfast.top/https://github.com/protocolbuffers/protobuf.git`.

## Header layout (2026-11)

The once-monolithic headers are split; **public include names are unchanged**:

- Umbrellas: `src/codec.hpp`, `src/json.hpp`, `src/text_format.hpp` (plus
  the conformance mirror `src/test_messages.hpp`).  Consumers keep including
  these names.
- `src/rpb/*.hpp`: codec internals - `annotations` (P3394R4 annotations and
  member reflection), `traits` (wire wrappers + type traits), `layout`
  (field table + `check_layout`), `wire` (zigzag), `group` (proto2 group
  presence helpers), `core` (depth guards/scratch/default omission),
  `serialize`, `parse`, `deep` (`deep_equal`/`deep_copy`).
- `src/rpb/json/*.hpp`: `dom` (DOM/JSON parser/base64/name mapping), `wkt`
  (WKT traits + Timestamp/Duration/FieldMask), `fwd`, `parse`, `print`,
  `any` (Any registry; `fwd` breaks the parse<->print<->Any cycle).
- `src/rpb/text/*.hpp`: `lexer`, `any_fwd`, `any`, `parse`, `print`.
- Each sub-header is `#pragma once` and self-contained (includes its
  dependencies); the umbrella includes them in dependency order.  The
  include root is `src/` (`#include "rpb/..."`): CMake's `reflect_compile`
  adds it for in-tree targets, the `rpb` INTERFACE target propagates it to
  consumers, and `cmake --install` installs `src/*.hpp` plus `src/rpb/`.
- Behavioral verification for the split: pure code motion (no semantic
  change; only dead `member_inner_ptr` overloads and one duplicate forward
  declaration pair were dropped), token-level equivalence against the
  pre-split files, `ctest` 8/8, conformance 1938/1 + 116/0, and an
  installed-header consumer compile/run.

The document below is the accumulated, **verified** modern-C++ knowledge from
the coreutils factor pilot and this session. Read it before writing any
C++20/23/26 code; it exists so a fresh context does not have to relearn these
facts from web searches.

## Toolchain (critical)

- Reflection / contracts / `#embed` are **C++26 only** and need
  **`g++-16`** (`/usr/bin/g++-16`, 16.1.0-2ubuntu1, installed from the
  stonking/26.10 archive).
  The system default `g++` is 15 and **rejects `-freflection`**.
- Build with: `cmake -S . -B build -DCMAKE_CXX_COMPILER=g++-16
  -DCMAKE_BUILD_TYPE=Release` (protobuf is fetched on first configure;
  offline: `-DFETCHCONTENT_SOURCE_DIR_PROTOBUF=$PWD/protobuf-3.21.12`;
  GitHub unreachable: add `-DRPB_PROTOBUF_REPO=https://ghfast.top/https://github.com/protocolbuffers/protobuf.git`)
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
- **`if constexpr` does NOT discard the false branch** (GCC 16.1.0, C++20/23/26,
  with or without `-freflection`): a `static_assert` or a hard-instantiation
  failure in a discarded `if constexpr` branch is still a compile error
  (verified 2026-08 with minimal repro). Do NOT put a failing dependent
  instantiation in an `if constexpr` false branch. Dispatch by **tag-type
  overload** or **struct partial specialization** instead (see the proto2
  group support in `codec.hpp`: `serialize_entry_group<std::true_type/…>` and
  the `GroupPtr`/`GroupPtrMut` partial specializations).
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
  **`codec.hpp` ships a weak default handler** (global scope, `abort()`), so
  header-only consumers link without one; a strong application definition
  overrides it. Do NOT define the handler inside `namespace rpb` - the
  contract machinery references the global symbol. Default `contract_assert`
  semantics: handler is called, then the program terminates (abort). `.text`
  delta on a tiny TU is ~0.2KB for one assert + handler; the ~9KB figure
  below was measured on factor's larger binary - context-dependent.
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
  zero field numbers, field numbers outside `[1, 536870911]` (the 29-bit
  protobuf tag limit), `OneOf` annotation-count mismatch, `unique_ptr`
  pointee not a message type, and vector/map/optional/oneof/`UnknownFields`
  alternatives all `static_assert`.  The same `check_layout` validation is
  applied to proto2 group bodies (`serialize_to_cos` / `parse_group_body`),
  which bypass the ordinary `serialize`/`parse` entry points.
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
  message; `rpb::Bytes` -> explicit bytes (LEN), a distinct type from
  `std::string` so a `std::variant` can hold both string and bytes oneof
  alternatives.  Wire wrappers (`SInt`/`Fixed*`) have defaulted `<=>` and
  work as `std::map` keys, so sint/fixed-key maps encode correctly.
- proto3 semantics: default-valued scalar/string/enum members and empty
  packed vectors are omitted; nested messages serialize unless all their
  members are default/absent (value semantics cannot distinguish unset from
  present-but-empty); optionals-with-value and non-null `unique_ptr` always
  serialize; empty repeated string elements still emit.  `rpb::deep_equal`
  compares messages with unique_ptr members by dereferencing recursively
  (vectors/maps/optionals recurse element-wise, so containers of messages
  with unique_ptr members compare by value too; `std::vector<bool>` uses
  `==` because its bit proxies have no reference type; variants compare by
  index and recurse into the active alternative, since variant `==` would
  compare unique_ptr alternatives by pointer; everything else is compared
  member-wise via reflection).  `rpb::deep_copy` is the symmetric
  value-copy helper:
  containers/variants/optionals are rebuilt element-wise and unique_ptr
  members allocate fresh pointees, so structs with unique_ptr members get
  protobuf-style deep-copy semantics without hand-written copy
  constructors (std::vector<bool> copies as-is; bit-proxy elements).
- Unknown fields: an `rpb::UnknownFields` member (any position, no
  annotation) captures and re-emits them after all known fields; without it
  they are skipped. Groups are skipped, not captured.
- Official-schema differential tests: `interop_tt` byte-compares our codec
  against protoc-generated code on protobuf's own
  `test_messages_proto3.proto` `TestAllTypesProto3` (mirror in
  `src/test_messages.hpp`). Recursive fields
  (`NestedMessage.corecursive`, `recursive_message`) are covered through
  `std::unique_ptr`; the 10-alternative oneof includes both string and
  bytes (via `rpb::Bytes`); Struct/Value/ListValue break their mutual
  recursion through `unique_ptr` oneof alternatives; all 19 maps are
  covered (sint/fixed keys use the wrappers).  The mirror is **complete**:
  repeated wrappers 211-219, Duration/Timestamp/FieldMask/Any (301-315),
  Struct/Value/ListValue (304/306/307/316/317/324), and fieldname*
  (401-418) are all present, and the shared fixture sets every field
  (repeated wrappers include an empty default-valued message to exercise
  presence).  `ctype=STRING_PIECE/CORD` fields have private accessors in
  generated 3.21 code; `ref_main_tt` sets them via
  `TextFormat::MergeFromString`.  Protobuf `Map` serialization order is
  hash order (unspecified) while ours is sorted, so the differential
  fixture keeps maps single-entry.
- Official conformance suite: `conformance` ctest runs protobuf's
  `conformance_test_runner` against `conformance_ours` (a stdin/stdout
  testee speaking the 4-byte-length-prefixed Conformance protocol).  The
  runner comes from FetchContent (`protobuf_BUILD_CONFORMANCE=ON`); the
  vendored 3.21 `cmake/conformance.cmake` omits `conformance_test_main.cc`
  and `text_format_conformance_suite.cc` (no `main()`, link failure), so
  `cmake/protobuf-conformance.patch` is applied via `PATCH_COMMAND`.
  Status: proto3 binary/JSON + proto2 binary/JSON walk is green
  (`1938 successes, 0 unexpected failures, 1 expected failure`), text walk
  `116 successes, 0 unexpected failures` (2026-11 audit).  The expected
  failure is `Recommended.Proto3.JsonInput.FieldMaskInvalidCharacter`:
  our FieldMask parser accepts `_` exactly like protobuf's own C++
  implementation (which lists the same test in `failure_list_cpp.txt`), so
  `tests/conformance_failures.txt` documents it.  Merge support came from
  parsing repeated occurrences of singular/oneof message fields into the
  existing value instead of replacing; RECOMMENDED-level packed/unpacked
  output-form alternatives show up as warnings only (not enforced).
- Codec guards: `kMaxSerializeDepth` (64) bounds **cumulative** nesting on
  both sides via thread_local counters (`serialize_depth_v`,
  `parse_depth_v`) incremented at every recursive `serialize_to_cos()` /
  `parse()` / `parse_group_body()` entry - this covers map values / vector
  elements / unique_ptr / optional / plain struct members **and** proto2
  group bodies.  Unknown-group skipping has its own bounded counter
  (`skip_depth_v`) so hostile nested START_GROUP input returns false
  instead of overflowing the stack.  Per-stream `SetRecursionLimit(...)`
  remains as belt-and-suspenders, but it resets per nested `parse()` call
  (each level builds a fresh CodedInputStream), which is why the cumulative
  counter exists.  Over-deep serialize trips a `contract_assert` and
  aborts; over-deep parse returns false (documented asymmetry).
  `optional<T>` message members merge on repeated occurrences
  (scalar/string stay last-wins), matching plain-member/oneof behavior.
- Strict wire parsing (2026-11 audit): LEN prefixes are read as 64-bit and
  rejected if they exceed `INT_MAX` or the bytes remaining in the current
  limit (a 6-byte input cannot trigger a 4 GiB allocation); tag varints are
  limited to 5 bytes and field numbers to 29 bits; a literal `0x00` tag,
  a truncated tag, trailing garbage, or a truncated payload is a parse
  error rather than clean EOF.  A known field number carrying an
  unexpected wire type is treated as an **unknown** field (skip/capture),
  matching protobuf; a known field with the correct wire type but a
  malformed payload is a hard error (no unknown-field resynchronisation).
  Packed repeated fields accept the packed form (wt 2) or one element with
  its own wire type, and reject illegal wire types (3/4/5/7 mismatches).
- `-0.0` is a present float/double value (raw-bits comparison), not the
  proto3 default: it serializes and survives round-trips.
- JSON/text format: JSON is implemented in `src/json.hpp` (proto3 mapping,
  WKTs, Any registry, top-level WKT dispatch); text format in
  `src/text_format.hpp` (proto2 groups, `google.protobuf.Value` field
  names, octal/hex ints, duplicate-field rejection, `f` float suffix).
  Known intentional deviations are listed in `docs/bug-audit-2026-11.md`.
- Benchmarks: `bench/bench.cpp` (target `bench`, not a ctest) compares the
  reflection codec against protoc-generated code on the same
  TestAllTypesProto3 fixture; timing uses only std::chrono.  On the current
  fixture rpb is ~0.86x serialize / ~0.87x parse vs protobuf (faster).
  Dispatch-level optimizations that matter: parsing resolves each tag via
  a template-generated binary decision tree over the sorted field table
  (`parse_table_range`/`parse_table_row`, O(log N)) instead of the old
  declaration-order member scan (`parse_fields`), and serialization reuses
  per-thread scratch buffers (`serialize_scratch_v` stack,
  `serialize_scratch_top_v`) for embedded-message/map-entry/packed
  payloads instead of allocating a `std::string` per nested chunk.
- libprotobuf is linked only for wire primitives (`CodedInputStream`/
  `CodedOutputStream`); there is no descriptor/reflection runtime by design.
