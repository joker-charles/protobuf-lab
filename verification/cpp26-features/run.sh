#!/usr/bin/env bash
# Re-run the AGENTS.md C++26 verification matrix on g++-16.
# Usage: ./run.sh   (set GXX to override the compiler)
set -u
cd "$(dirname "$0")"

GXX="${GXX:-/usr/bin/g++-16}"
FLAGS="-std=c++26 -freflection"
BUILD="build"
mkdir -p "$BUILD"

fail=0
report() { # name pass detail
  if [ "$2" = 1 ]; then
    printf 'PASS  %-28s %s\n' "$1" "$3"
  else
    printf 'FAIL  %-28s %s\n' "$1" "$3"
    fail=1
  fi
}

# --- compile tests that must FAIL --------------------------------------
for t in a1_for_each a2_name_of a3_extract_enum a4_range_for_splice \
         a6_range_splice_tmpl_args a7_nttp_reflect a8_tf_vector_direct \
         b3d_cast_tf_loopvar b7_tf_byvalue_helper b8_api_surface \
         b9_tf_static_arr_var c2c_embed_trailing; do
  if "$GXX" $FLAGS "$t.cpp" -o "$BUILD/$t" 2>"$BUILD/$t.log"; then
    report "$t" 0 "COMPILE-OK (should fail)"
  else
    report "$t" 1 "$(head -1 "$BUILD/$t.log" | cut -c1-90)"
  fi
done

# c2b: source must NOT sit next to data.bin; -I must NOT help.
cp c2b_embed_include_dir.cpp "$BUILD/" 2>/dev/null
if (cd "$BUILD" && "$GXX" $FLAGS -I"$PWD/.." c2b_embed_include_dir.cpp -o c2b \
    2>c2b.log); then
  report c2b_embed_include_dir 0 "COMPILE-OK (should fail)"
else
  report c2b_embed_include_dir 1 "$(head -1 "$BUILD/c2b.log" | cut -c1-90)"
fi

# c3b: must fail at LINK (no user handler).
if "$GXX" $FLAGS c3b_contracts_no_handler.cpp -o "$BUILD/c3b" 2>"$BUILD/c3b.log"; then
  report c3b_contracts_no_handler 0 "COMPILE-OK (should fail at link)"
elif rg -q "undefined reference to .handle_contract_violation" "$BUILD/c3b.log"; then
  report c3b_contracts_no_handler 1 "undefined reference (as expected)"
else
  report c3b_contracts_no_handler 0 "$(head -1 "$BUILD/c3b.log" | cut -c1-90)"
fi

# --- compile + run tests that must pass --------------------------------
for t in a5_subscript_splice a5b_member_subscript_splice b1_member_index \
         b1b_member_index_static_arr b2_member_type b3a_value_splice_bind \
         b3b_cast_splice_direct b3c_cast_member_splice b4_member_count \
         b5_tf_direct b6_tf_nttp_helper b8b_api_surface b9b_tf_static_local \
         b9c_tf_namespace_var c1_define_static_std c1b_object_rvalue \
         c1c_object_namespace c2a_embed_same_dir c3a_contracts_ok c4_ckd; do
  if "$GXX" $FLAGS "$t.cpp" -o "$BUILD/$t" 2>"$BUILD/$t.log"; then
    if "$BUILD/$t" >"$BUILD/$t.run" 2>&1; then
      report "$t" 1 "$(head -1 "$BUILD/$t.run" | cut -c1-60)"
    else
      report "$t" 0 "RUN-FAIL exit=$? $(head -1 "$BUILD/$t.run" | cut -c1-60)"
    fi
  else
    report "$t" 0 "$(head -1 "$BUILD/$t.log" | cut -c1-90)"
  fi
done

# --- specials ------------------------------------------------------------
# c3c/c3d: compile OK, violation must call the handler then abort.
for t in c3c_contracts_violation c3d_stderr; do
  if "$GXX" $FLAGS "$t.cpp" -o "$BUILD/$t" 2>"$BUILD/$t.log"; then
    if "$BUILD/$t" >"$BUILD/$t.run" 2>&1; then
      report "$t" 0 "RUN-OK (should abort)"
    else
      rc=$?
      if rg -q "handler-called" "$BUILD/$t.run"; then
        report "$t" 1 "handler called, abort exit=$rc"
      else
        report "$t" 0 "abort exit=$rc but no handler evidence"
      fi
    fi
  else
    report "$t" 0 "$(head -1 "$BUILD/$t.log" | cut -c1-90)"
  fi
done

# c5: -fno-exceptions -fno-rtti must be safe with reflection.
if "$GXX" $FLAGS -fno-exceptions -fno-rtti c5_noexcept_nortti.cpp \
    -o "$BUILD/c5" 2>"$BUILD/c5.log" && "$BUILD/c5"; then
  report c5_noexcept_nortti 1 "-fno-exceptions -fno-rtti"
else
  report c5_noexcept_nortti 0 "$(head -1 "$BUILD/c5.log" | cut -c1-90)"
fi

# size probes: C++23 vs C++26 must be identical; contracts add a little.
"$GXX" $FLAGS size_contracts.cpp -o "$BUILD/sc26" 2>/dev/null
"$GXX" $FLAGS size_plain.cpp -o "$BUILD/sp26" 2>/dev/null
"$GXX" -std=c++23 size_plain.cpp -o "$BUILD/sp23" 2>/dev/null
sc26=$(size -A "$BUILD/sc26" | awk '/^\.text/{print $2}')
sp26=$(size -A "$BUILD/sp26" | awk '/^\.text/{print $2}')
sp23=$(size -A "$BUILD/sp23" | awk '/^\.text/{print $2}')
printf 'INFO  .text bytes: contracts+c26=%s c26=%s c23=%s\n' "$sc26" "$sp26" "$sp23"
[ "$sp26" = "$sp23" ] || { echo "FAIL  C++26 vs C++23 .text differ"; fail=1; }
[ "$sc26" -gt "$sp26" ] || { echo "FAIL  contracts .text not larger"; fail=1; }

echo
if [ "$fail" -eq 0 ]; then echo "ALL CHECKS PASSED"; else echo "SOME CHECKS FAILED"; fi
exit "$fail"
