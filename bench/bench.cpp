// Micro-benchmark: rpb reflection codec vs protoc-generated code on the
// same TestAllTypesProto3 workload.  Timing uses only std::chrono; the
// protobuf side comes from the TT_GEN-generated
// test_messages_proto3.pb.{h,cc} (same fixture content, filled once per
// side).  Fixed iteration count (argv[1] overrides the default), ns/op
// table on stdout.  Deliberately NOT a ctest: timing is too noisy under
// the test runner.
//
// Usage:
//   build/bench [iterations]

#include "test_messages.hpp"

#include "google/protobuf/test_messages_proto3.pb.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace m = protobuf_test_messages::proto3;

// --- fixtures: same logical content on both sides ----------------------

static void fill_ours(tmm::TestAllTypesProto3 &p)
{
  p.optional_int32 = -32;
  p.optional_int64 = -1234567890123LL;
  p.optional_uint32 = 32;
  p.optional_uint64 = 64;
  p.optional_sint32 = rpb::SInt<std::int32_t>{-5};
  p.optional_fixed64 = rpb::Fixed64{0x1122334455667788ull};
  p.optional_float = 1.5f;
  p.optional_double = -2.25;
  p.optional_bool = true;
  p.optional_string = "hello";
  p.optional_bytes = std::string("\x01\x02\x03", 3);
  p.optional_nested_message = std::make_unique<tmm::NestedMessage>();
  p.optional_nested_message->a = 123;
  p.recursive_message = std::make_unique<tmm::TestAllTypesProto3>();
  p.recursive_message->optional_int32 = 77;
  p.repeated_int32 = {1, -2, 3, -4, 5};
  p.repeated_string = {"a", "bb", "ccc", "dddd"};
  p.repeated_nested_message.push_back(tmm::NestedMessage{1, nullptr, {}});
  p.repeated_nested_message.push_back(tmm::NestedMessage{2, nullptr, {}});
  p.map_int32_int32 = {{1, 2}, {3, 4}, {5, 6}};
  p.map_string_string = {{"k", "v"}, {"a", "b"}, {"z", "z"}};
  p.packed_int32 = {1, 2, 3, 4, 5, 6, 7, 8};
  p.packed_nested_enum = {tmm::NestedEnum::FOO, tmm::NestedEnum::BAR};
  p.oneof_field = std::string("note");
  p.optional_bool_wrapper = std::make_unique<tmm::BoolValue>(true);
  p.optional_timestamp =
      std::make_unique<tmm::Timestamp>(1700000000LL, 123456789);
  p.repeated_duration = {{tmm::Duration{1, 2}, tmm::Duration{-3, -4}}};
  p.fieldname1 = 1;
  p.Field_name18__ = 18;
}

static void fill_ref(m::TestAllTypesProto3 &p)
{
  p.set_optional_int32(-32);
  p.set_optional_int64(-1234567890123LL);
  p.set_optional_uint32(32);
  p.set_optional_uint64(64);
  p.set_optional_sint32(-5);
  p.set_optional_fixed64(0x1122334455667788ull);
  p.set_optional_float(1.5f);
  p.set_optional_double(-2.25);
  p.set_optional_bool(true);
  p.set_optional_string("hello");
  p.set_optional_bytes("\x01\x02\x03", 3);
  p.mutable_optional_nested_message()->set_a(123);
  p.mutable_recursive_message()->set_optional_int32(77);
  p.add_repeated_int32(1);
  p.add_repeated_int32(-2);
  p.add_repeated_int32(3);
  p.add_repeated_int32(-4);
  p.add_repeated_int32(5);
  p.add_repeated_string("a");
  p.add_repeated_string("bb");
  p.add_repeated_string("ccc");
  p.add_repeated_string("dddd");
  p.add_repeated_nested_message()->set_a(1);
  p.add_repeated_nested_message()->set_a(2);
  (*p.mutable_map_int32_int32())[1] = 2;
  (*p.mutable_map_int32_int32())[3] = 4;
  (*p.mutable_map_int32_int32())[5] = 6;
  (*p.mutable_map_string_string())["k"] = "v";
  (*p.mutable_map_string_string())["a"] = "b";
  (*p.mutable_map_string_string())["z"] = "z";
  p.add_packed_int32(1);
  p.add_packed_int32(2);
  p.add_packed_int32(3);
  p.add_packed_int32(4);
  p.add_packed_int32(5);
  p.add_packed_int32(6);
  p.add_packed_int32(7);
  p.add_packed_int32(8);
  p.add_packed_nested_enum(m::TestAllTypesProto3::FOO);
  p.add_packed_nested_enum(m::TestAllTypesProto3::BAR);
  p.set_oneof_string("note");
  p.mutable_optional_bool_wrapper()->set_value(true);
  p.mutable_optional_timestamp()->set_seconds(1700000000LL);
  p.mutable_optional_timestamp()->set_nanos(123456789);
  auto *d0 = p.add_repeated_duration();
  d0->set_seconds(1);
  d0->set_nanos(2);
  auto *d1 = p.add_repeated_duration();
  d1->set_seconds(-3);
  d1->set_nanos(-4);
  p.set_fieldname1(1);
  p.set_field_name18__(18);
}

// --- timing -------------------------------------------------------------

template <typename Fn>
double ns_per_op(std::size_t iters, Fn &&fn)
{
  for (int i = 0; i < 1000; ++i)  // warm-up (caches, page faults, ...)
    fn();
  auto t0 = std::chrono::steady_clock::now();
  for (std::size_t i = 0; i < iters; ++i)
    fn();
  auto t1 = std::chrono::steady_clock::now();
  return std::chrono::duration<double, std::nano>(t1 - t0).count()
         / static_cast<double>(iters);
}

int main(int argc, char **argv)
{
  std::size_t iters = 200000;
  if (argc > 1)
    iters = static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10));
  if (iters == 0)
    iters = 1;

  tmm::TestAllTypesProto3 ours;
  fill_ours(ours);
  m::TestAllTypesProto3 ref;
  fill_ref(ref);

  std::string ours_bytes;
  std::string ref_bytes;
  rpb::serialize(ours_bytes, ours);
  ref.SerializeToString(&ref_bytes);
  std::printf("fixture: ours=%zu bytes, protobuf=%zu bytes\n",
              ours_bytes.size(), ref_bytes.size());

  std::size_t checksum = 0;  // defeats dead-code elimination; printed below

  double ours_ser = ns_per_op(iters, [&] {
    ours_bytes.clear();
    rpb::serialize(ours_bytes, ours);
    checksum += ours_bytes.size();
  });
  double ref_ser = ns_per_op(iters, [&] {
    ref_bytes.clear();
    ref.SerializeToString(&ref_bytes);
    checksum += ref_bytes.size();
  });

  double ours_par = ns_per_op(iters, [&] {
    tmm::TestAllTypesProto3 q;
    if (!rpb::parse(ours_bytes, q))
      std::abort();
    checksum += static_cast<std::size_t>(q.optional_int32);
  });
  double ref_par = ns_per_op(iters, [&] {
    m::TestAllTypesProto3 q;
    if (!q.ParseFromString(ref_bytes))
      std::abort();
    checksum += static_cast<std::size_t>(q.optional_int32());
  });

  std::printf("operation                  ns/op         ops/s\n");
  std::printf("rpb serialize (ours)  %12.2f  %12.0f\n", ours_ser,
              1e9 / ours_ser);
  std::printf("protobuf serialize    %12.2f  %12.0f\n", ref_ser, 1e9 / ref_ser);
  std::printf("rpb parse (ours)      %12.2f  %12.0f\n", ours_par,
              1e9 / ours_par);
  std::printf("protobuf parse        %12.2f  %12.0f\n", ref_par, 1e9 / ref_par);
  std::printf("serialize ratio (ours/protobuf) %.2fx\n", ours_ser / ref_ser);
  std::printf("parse ratio (ours/protobuf)     %.2fx\n", ours_par / ref_par);
  std::printf("checksum=%zu (ignore)\n", checksum);
  return 0;
}
