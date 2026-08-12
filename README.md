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
- **Compile-time validation**: missing/duplicate field numbers, oneof
  annotation-count or alternative-type violations, and `unique_ptr`
  pointee type errors all fail with `static_assert`.
- **Protobuf merge semantics**: repeated occurrences of singular message
  fields (including inside oneofs) merge instead of replacing.
- **Unknown-field preservation** (opt-in `rpb::UnknownFields` member) and
  proto3 default-value omission.

## Status

The official conformance suite is green for proto3 binary wire format:
**637 required `protobuf_test` cases pass, 0 failures** (scalars, enums,
packed/unpacked repeated, maps incl. sint/fixed keys, oneofs, message
merge, present-but-empty messages, unknown fields, illegal tags,
truncated input).  JSON, text-format, and proto2 categories are skipped by
the testee.  See `tests/conformance_failures.txt` (currently empty).

## Requirements

- **g++-16** (`-std=c++26 -freflection`; the system default g++ rejects
  `-freflection`).  The project was verified with g++-16 16.1.0 from the
  stonking/26.10 archive.
- CMake >= 3.20.
- Network access on first configure: protobuf **v3.21.12** is fetched
  automatically via `FetchContent`.

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
| `selftest` | hand-computed wire bytes, oneof presence, out-of-order field emission, unknown fields, group skip, truncation |
| `selftest_tt` | roundtrips on the official `TestAllTypesProto3` mirror |
| `interop` | byte-compare our codec vs protoc-generated code on `tests/test.proto` |
| `interop_tt` | byte-compare on protobuf's own `test_messages_proto3.proto` |
| `conformance` | official protobuf conformance runner against `conformance_ours` |

`verification/cpp26-features/run.sh` re-runs the one-file-per-claim
compiler-behavior probes backing the notes in `AGENTS.md` and
`docs/reflect_error.md`.

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
- **`std::optional<T>` message members** replace on repeated occurrences
  instead of merging (merge is implemented for plain members and oneofs).
- **Multi-entry maps** serialize in sorted key order; protobuf's `Map`
  order is hash-based and unspecified.  Both are valid wire encodings, but
  byte-level interop fixtures keep maps single-entry.
- **Serialization has no depth limit** (parsing does, 64 levels); a
  pathological nested structure could overflow the stack on serialize.
- **Unknown-field preservation is opt-in** (add an `rpb::UnknownFields`
  member); without it, unknown fields are skipped.

## Future plans

- **proto2 support** (required fields, groups, extensions, defaults) and
  enabling the proto2 half of the conformance suite.
- **JSON and text-format support**, including the well-known-type JSON
  mapping (Struct/Value/Any/Duration/Timestamp/FieldMask, NaN/Inf,
  enum-as-string).
- **Serialization depth limit** to match the parser's 64-level recursion
  guard.
- **`optional<T>` message merge** for full parity with protobuf's merge
  semantics.
- **Complete the `TestAllTypesProto3` mirror**: repeated wrappers
  (211-219), Duration/Timestamp/FieldMask/Any (301-315), fieldname*
  (401-418).
- **Enforce the RECOMMENDED conformance level** (currently only REQUIRED
  is enforced; packed/unpacked output-form alternatives are warnings).
- **CI**: GitHub Actions workflow building with g++-16 (needs the
  stonking/26.10 archive or a compatible image) and running `ctest`,
  including the conformance suite.
- **Benchmarks** against the official implementation, and micro-optimizing
  the hot serialization/parsing paths.

## License

[GPLv2](LICENSE) (`GPL-2.0-only`).  GPLv2 is compatible with protobuf's
BSD-3-Clause license (FSF lists BSD-3-Clause as GPL-compatible), and this
repository contains no protobuf source — the vendored tree is only a local
offline FetchContent cache and is not part of the repository.  A Chinese
version of this README is available at [README.zh-CN.md](README.zh-CN.md).
