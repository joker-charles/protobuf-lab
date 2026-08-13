# reflect-proto

An experimental **protobuf wire-format codec driven by C++26 static
reflection** (P2996) and value annotations (P3394R4).  There is no runtime
`Descriptor`/`Reflection` machinery — the C++ type itself *is* the schema.
Wire primitives come from `libprotobuf` (`CodedInputStream` /
`CodedOutputStream`), and correctness is checked byte-for-byte against
protoc-generated code and the official protobuf conformance suite.

## Highlights

- **Explicit field numbers** via `[[=rpb::field_no<N>{}]]` annotations
  (P3394R4), read back with a scope splice — no `std::meta::extract`.
- **Oneof** via `rpb::OneOf<Ts...>` (`std::variant<std::monostate, Ts...>`):
  one wire field per alternative, presence semantics, last-wins parsing.
- **Recursive messages** via `std::unique_ptr<T>` members (by-value
  recursion is impossible in C++); singular message fields have real
  proto3 presence.
- **Deep-copyable messages** via the reflection-driven `rpb::deep_copy`
  helper: structs with `unique_ptr` members keep protobuf-style value
  semantics (fresh storage per copy) without hand-written copy
  constructors.
- **Compile-time validation**: missing/duplicate field numbers, oneof
  annotation-count or alternative-type violations, and `unique_ptr`
  pointee type errors all fail with `static_assert`.
- **Protobuf merge semantics**: repeated occurrences of singular message
  fields (including inside oneofs and `optional<T>` message members) merge
  instead of replacing.
- **Serialization depth guard**: nested messages recurse through
  `serialize()`, so a thread-local counter (limit 64, matching the
  parser) catches pathological nesting before it can overflow the stack.
- **Unknown-field preservation** (opt-in `rpb::UnknownFields` member) and
  proto3 default-value omission.

## Status

The official conformance suite is green for proto3 binary wire format:
**637 `protobuf_test` cases pass, 0 failures** (scalars, enums,
packed/unpacked repeated, maps incl. sint/fixed keys, oneofs, message
merge, present-but-empty messages, unknown fields, illegal tags,
truncated input).  JSON, text-format, and proto2 categories are skipped by
the testee.  The runner also reports 14 RECOMMENDED-level warnings about
packed/unpacked output-form alternatives; both encodings are legal, so
they are documented as accepted in `tests/conformance_failures.txt`
(which lists no actual failures).

## Project scope

This is an **experimental wire codec, not a protobuf reimplementation**.
It targets the proto3 binary wire format for struct-shaped messages and
deliberately reuses protobuf's own wire primitives
(`CodedInputStream`/`CodedOutputStream`) and official test suites.  The
point of the experiment is that C++26 static reflection can drive
serialization with **no code-generation step and no runtime descriptor** —
the struct *is* the schema.  Full protobuf parity (descriptors,
JSON/text formats, proto2, extensions, cross-language tooling) is
explicitly out of scope.  Success criterion: can struct-as-schema replace
the protoc pipeline for a useful proto3 subset?

## Requirements

- **g++-16** (`-std=c++26 -freflection`; the system default g++ rejects
  `-freflection`).  The project was verified with g++-16 16.1.0 from the
  stonking/26.10 archive.
- CMake >= 3.20.
- Network access on first configure: protobuf **v3.21.12** is fetched
  automatically via `FetchContent`.

## Usage

The codec is a **header-only library** (`src/codec.hpp`): the annotated
struct *is* the schema, so there is no `.proto` file and no code-generation
step.  It requires g++-16 with C++26 `-freflection`, which the `rpb`
CMake target propagates automatically.

### As a CMake dependency

```cmake
include(FetchContent)
FetchContent_Declare(reflect_proto
  GIT_REPOSITORY https://github.com/joker-charles/protobuf-lab.git
  GIT_TAG v0.1.0)
FetchContent_MakeAvailable(reflect_proto)
target_link_libraries(your_target PRIVATE rpb)
```

### Alternatively: install and find_package

```sh
cmake --install build --prefix /usr/local   # codec.hpp + rpb CMake package
```

```cmake
find_package(protobuf CONFIG REQUIRED)  # protobuf must be installed first
find_package(rpb CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE rpb::rpb)
```

### Minimal example

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
  rpb::parse(bytes, back);    // field-order independent; messages merge
  return 0;
}
```

A runnable version is in `examples/roundtrip.cpp`.

### Constraints to plan around

- g++-16 with C++26 `-freflection` is the only hard toolchain requirement.
- proto3 binary wire format for struct-shaped messages only; JSON, text
  format, and proto2 are not implemented (see "Project scope").
- Field numbers come from `[[=rpb::field_no<N>{}]]` annotations: adding or
  removing members does not change the wire format, but renumbering does.
- Singular message members: use `std::unique_ptr<T>` for real presence
  (including recursion); plain by-value members are omitted when
  all-default.
- Unknown fields are preserved only when the struct carries an
  `rpb::UnknownFields` member.

## Build and test

```sh
cmake -S . -B build -DCMAKE_CXX_COMPILER=g++-16 -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Offline builds can reuse a local protobuf checkout:

```sh
cmake -S . -B build -DCMAKE_CXX_COMPILER=g++-16 \
  -DFETCHCONTENT_SOURCE_DIR_PROTOBUF=$PWD/protobuf-3.21.12
```

### Tests

| ctest name | what it checks |
| --- | --- |
| `selftest` | hand-computed wire bytes, oneof presence, optional-message merge, depth-64 roundtrip, out-of-order field emission, unknown fields, group skip, truncation |
| `selftest_tt` | roundtrips on the complete official `TestAllTypesProto3` mirror |
| `interop` | byte-compare our codec vs protoc-generated code on `tests/test.proto` |
| `interop_tt` | byte-compare on protobuf's own `test_messages_proto3.proto` (full schema) |
| `conformance` | official protobuf conformance runner against `conformance_ours` |

GitHub Actions (`.github/workflows/ci.yml`) builds inside the official
`gcc:16` container and runs the full suite plus a consumer smoke test on
every push.

`verification/cpp26-features/run.sh` re-runs the one-file-per-claim
compiler-behavior probes backing the notes in `AGENTS.md` and
`docs/reflect_error.md`.

### Benchmarks

`build/bench [iterations]` compares the reflection codec against
protoc-generated code on the same `TestAllTypesProto3` fixture (fixed
iteration count, std::chrono only, no third-party benchmark library):

```sh
cmake --build build -j --target bench
./build/bench 200000
```

It prints an ns/op table with serialize/parse ratios plus a checksum (to
defeat dead-code elimination).  This is the evidence that decides whether
the struct-as-schema experiment is worth continuing: if the reflection
codec stays within a small constant factor of protoc-generated code, the
no-codegen ergonomics may be worth the cost; if the gap grows with message
size/complexity, the hot paths need work first.

On the current fixture the reflection codec is **faster than
protoc-generated code** (about 0.86x serialize / 0.87x parse, i.e. ~14%
quicker on both).  Two dispatch-level optimizations got it there: parsing
resolves tags through a compile-time binary decision tree over the sorted
field table (O(log N) comparisons instead of scanning every member), and
serialization reuses per-thread scratch buffers for embedded-message /
map-entry / packed payloads instead of allocating a temporary string per
nested chunk.

## Design in one paragraph

Members carry `[[=rpb::field_no<N>{}]]` annotations; a consteval pass
builds a `(fieldno, member, alternative)` table sorted by field number
(`std::sort` in constant evaluation) and validates the layout.  Ordinary
members follow proto3 default omission, oneof alternatives emit whenever
set, `unique_ptr` message members have real presence, and `UnknownFields`
re-emit after all known fields.  Parsing dispatches by field-number sets
and is order-independent.

## Repository layout

```
src/            codec.hpp + selftest/CLI binaries
bench/          rpb-vs-protobuf micro-benchmark (not a ctest)
tests/          test.proto, reference fixtures, interop/conformance scripts
cmake/          protobuf conformance.cmake patch (applied via PATCH_COMMAND)
docs/           GCC 16 reflection error log
verification/   one-file compiler-behavior probes (run.sh)
```

## Known limitations

- **proto2**, **JSON**, and **text format** are not implemented; the
  conformance testee skips those categories.
- **Value-semantics struct members** (still supported; e.g. `Person.home`
  in the selftests): an all-default by-value nested message is treated as
  unset and omitted, so "present but empty" cannot be expressed for such
  members.  The official `TestAllTypesProto3` mirror already avoids this by
  using `std::unique_ptr` for every singular message member; user structs
  that keep by-value members should reach for `std::unique_ptr<T>` /
  `std::optional<T>` whenever real presence matters.
- **Multi-entry maps** serialize in sorted key order; protobuf's `Map`
  order is hash-based and unspecified.  Both are valid wire encodings, but
  byte-level interop fixtures keep maps single-entry.
- **Serialization over-depth aborts**: like the parser's 64-level
  recursion limit, `serialize()` guards nested-message depth; but while
  `parse()` returns `false` on over-depth input, `serialize()` has no
  error channel in its public API, so the guard trips a C++26 contract and
  aborts via the violation handler (a weak default is provided; a strong
  `handle_contract_violation` in the application overrides it).
- **Unknown-field preservation is opt-in** (add an `rpb::UnknownFields`
  member); without it, unknown fields are skipped.

## Future plans

### Engineering hardening (incremental)

- **Enforce the RECOMMENDED conformance level** (currently only REQUIRED
  is enforced; the 14 packed/unpacked output-form alternatives are
  documented as accepted warnings in `tests/conformance_failures.txt`).
- **Keep validating with larger/more diverse fixtures** (deep nesting,
  many small messages, big maps) so the benchmark evidence stays honest;
  the current fixture already shows the reflection codec ahead of
  protoc-generated code.

### Research directions (optional, not parity goals)

- **proto2 support** (required fields, groups, extensions, defaults) and
  the proto2 half of the conformance suite — only meaningful if the
  struct-as-schema experiment succeeds and a proto2 subset is wanted.
- **JSON and text-format support**, including the well-known-type JSON
  mapping (Struct/Value/Any/Duration/Timestamp/FieldMask, NaN/Inf,
  enum-as-string).

## License

[GPLv2](LICENSE) (`GPL-2.0-only`).  GPLv2 is compatible with protobuf's
BSD-3-Clause license (FSF lists BSD-3-Clause as GPL-compatible), and this
repository contains no protobuf source — the vendored tree is only a local
offline FetchContent cache and is not part of the repository.  A Chinese
version of this README is available at [README.zh-CN.md](README.zh-CN.md).
