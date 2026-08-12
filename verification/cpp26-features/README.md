# C++26 verification matrix (g++-16, 16.0.1 20260322)

One small test per claim in `AGENTS.md`. Verified 2026-08-12; this directory
is the reproducible record (kept in-repo on request - was previously scratch
in `/tmp`).

## Run

```sh
./run.sh          # needs /usr/bin/g++-16
GXX=/path/to/g++ ./run.sh
```

The script compiles every test, runs the pass-expected ones, and compares
against the expected outcomes below. Logs and binaries land in `build/`.

## Expected outcomes

Must FAIL to compile:

| test | claim verified |
|---|---|
| `a1_for_each` | `for_each` is not a member of `std::meta` |
| `a2_name_of` | `name_of` is not a member of `std::meta` |
| `a3_extract_enum` | `extract<int>` on an enumerator: uncaught `std::meta::exception`, what() = "value cannot be extracted" |
| `a4_range_for_splice` | splice of a regular range-for loop variable fails |
| `a6_range_splice_tmpl_args` | range splice in template-arg list is treated as a single element |
| `a7_nttp_reflect` | `^^` on a non-type template parameter is rejected |
| `a8_tf_vector_direct` | `template for` directly over a vector-returning range: "refers to a result of 'operator new'" (P1306R5 §3.2) |
| `b3d_cast_tf_loopvar` | direct cast of the `template for` loop-variable splice fails (bind first) |
| `b7_tf_byvalue_helper` | passing the loop var by value to a splicing consteval helper fails at definition |
| `b8_api_surface` | historical probe: `members_of` without ctx / `is_enum` (wrong API names) |
| `b9_tf_static_arr_var` | plain local constexpr var as `template for` range fails ("add 'static'") |
| `c2b_embed_include_dir` | `#embed` does not search `-I` paths |
| `c2c_embed_trailing` | `#embed` must be alone on its line |
| `c3b_contracts_no_handler` | link fails with undefined `handle_contract_violation` |

Must compile (and run where noted):

| test | claim verified |
|---|---|
| `a5_subscript_splice` | `[: es[I] :]` subscript splice works (corrects AGENTS.md) |
| `a5b_member_subscript_splice` | `v.[: ms[I] :]` member subscript splice works (corrects AGENTS.md) |
| `b1_member_index` / `b1b` | member infos by index as constexpr variables |
| `b2_member_type` | `typename [: type_of(r) :]` + `v.[:r:]` |
| `b3a_value_splice_bind` | bind-then-convert pattern |
| `b3b` / `b3c` | direct cast of a bound constexpr-info splice works (corrects AGENTS.md) |
| `b4_member_count` | `.size()` in a constexpr variable template |
| `b5_tf_direct` | `template for` + direct splice in body (enumerators) |
| `b6_tf_nttp_helper` | NTTP helper `helper<r>()` |
| `b8b_api_surface` | `members_of(^^S, ctx)`, `is_enum_type`/`is_class_type`, `substitute`, `parameters_of` |
| `b9b` / `b9c` | `static` local / namespace-scope constexpr var as `template for` range |
| `c1_define_static_std` | `std::define_static_string/array/object` live in `std` |
| `c1b_object_rvalue` / `c1c_object_namespace` | `define_static_object` with rvalue / namespace-scope constexpr |
| `c2a_embed_same_dir` | `#embed` next to source; `__cpp_pp_embed == 202502L` |
| `c3a_contracts_ok` | `contract_assert` + handler links and runs; accessor is `.comment()` |
| `c3c` / `c3d_stderr` | violation: handler called, then abort (exit 134) |
| `c4_ckd` | `ckd_add` / `ckd_sub` via `<stdckdint.h>` |
| `c5_noexcept_nortti` | `-fno-exceptions -fno-rtti` safe with reflection |

Size probes (`size_contracts` vs `size_plain`): C++23 and C++26 `.text` are
identical on this trivial TU; contracts add ~0.2KB. The ~9KB/~19KB figures in
AGENTS.md were measured on factor's larger binary (context-dependent).

## Wrong conclusions found this pass (now corrected in AGENTS.md)

1. "Splices on `arr[i]` subscripts also fail" - false; constant-index
   subscripts of a `define_static_array` result splice fine.
2. "Casting the splice expression directly fails" - false for bound constexpr
   infos; only the `template for` loop variable itself needs bind-first.
3. "`template for` direct splice fails / by-value helper works" - inverted;
   direct splice works, by-value helper fails (already corrected).
4. Minor precision: `members_of` needs `access_context`; `is_*` queries are
   `is_*_type`; `contract_violation` accessor is `.comment()`, not `.message()`.

## Known GCC quirks surfaced by the harness

- Local non-static constexpr `span` from `define_static_array` is
  consteval-only at runtime until its value is used in a constant-evaluated
  context first (bake via `constexpr std::size_t n = es.size();` or a
  `static_assert`).
- Local constexpr lvalue passed to `define_static_object` gives a
  consteval-only pointer; use an rvalue or namespace-scope constexpr.
