cmake_minimum_required(VERSION 3.20)
project(reflect_proto VERSION 0.1.0 LANGUAGES CXX)

# Experimental protobuf wire-format codec driven by C++26 static reflection
# (P2996 + P3394R4 annotations).  Requires g++-16 (the system default g++
# rejects -freflection) and a C++26 toolchain.
#
# protobuf 3.21.12 is fetched automatically (FetchContent) and used for
# wire primitives (CodedInputStream/CodedOutputStream), protoc, and the
# conformance test runner.  Offline builds can point
# FETCHCONTENT_SOURCE_DIR_PROTOBUF at a local checkout (the repo keeps a
# vendored tree at protobuf-3.21.12/, gitignored).

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- protobuf v3.21.12 -------------------------------------------------
include(FetchContent)

set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_CONFORMANCE ON CACHE BOOL "" FORCE)

FetchContent_Declare(protobuf
  GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
  GIT_TAG v3.21.12
  # Vendored 3.21 cmake/conformance.cmake omits conformance_test_main.cc
  # and text_format_conformance_suite.cc, so conformance_test_runner has no
  # main() and fails to link.  (Skipped automatically when
  # FETCHCONTENT_SOURCE_DIR_PROTOBUF points at an already-patched tree.)
  PATCH_COMMAND git apply ${CMAKE_CURRENT_SOURCE_DIR}/cmake/protobuf-conformance.patch
)
FetchContent_MakeAvailable(protobuf)

set(PB_INC ${protobuf_SOURCE_DIR}/src)

function(reflect_compile tgt)
  target_compile_options(${tgt} PRIVATE -freflection -Wall -Wextra
                         -Wpedantic -Wconversion)
  target_include_directories(${tgt} PRIVATE SYSTEM ${PB_INC})
  target_link_libraries(${tgt} PRIVATE protobuf::libprotobuf)
endfunction()

# --- the codec as a header-only library --------------------------------
# Consumers: FetchContent this repo and `target_link_libraries(... rpb)`.
# The target propagates C++26, -freflection, and the protobuf dependency.
add_library(rpb INTERFACE)
target_include_directories(rpb INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(rpb INTERFACE protobuf::libprotobuf)
target_compile_features(rpb INTERFACE cxx_std_26)
target_compile_options(rpb INTERFACE -freflection)

add_executable(example examples/roundtrip.cpp)
target_link_libraries(example PRIVATE rpb)

# --- install / find_package packaging ----------------------------------
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

install(FILES src/codec.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS rpb EXPORT rpbTargets)

set(RPB_CMAKE_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/rpb)
configure_package_config_file(
  cmake/rpbConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/rpbConfig.cmake
  INSTALL_DESTINATION ${RPB_CMAKE_INSTALL_DIR})
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/rpbConfigVersion.cmake
  COMPATIBILITY SameMajorVersion)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/rpbConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/rpbConfigVersion.cmake
  DESTINATION ${RPB_CMAKE_INSTALL_DIR})
install(EXPORT rpbTargets
  NAMESPACE rpb::
  DESTINATION ${RPB_CMAKE_INSTALL_DIR})

# --- codec binaries ----------------------------------------------------
add_executable(reflect_proto src/main.cpp)
reflect_compile(reflect_proto)

add_executable(reflect_proto_tt src/main_tt.cpp)
reflect_compile(reflect_proto_tt)

# --- conformance testee ------------------------------------------------
set(CONF_GEN ${CMAKE_CURRENT_BINARY_DIR}/gen_conf)
file(MAKE_DIRECTORY ${CONF_GEN})
add_custom_command(
  OUTPUT ${CONF_GEN}/conformance.pb.cc ${CONF_GEN}/conformance.pb.h
  COMMAND protoc ${protobuf_SOURCE_DIR}/conformance/conformance.proto
          --proto_path=${protobuf_SOURCE_DIR}/conformance
          --cpp_out=${CONF_GEN}
  DEPENDS protoc ${protobuf_SOURCE_DIR}/conformance/conformance.proto
  COMMENT "Generating conformance.pb.cc")
add_executable(conformance_ours src/conformance_ours.cpp
               ${CONF_GEN}/conformance.pb.cc)
reflect_compile(conformance_ours)
target_include_directories(conformance_ours PRIVATE SYSTEM ${CONF_GEN}
                           ${protobuf_SOURCE_DIR}/conformance)

# --- protobuf-side reference fixtures (tests) --------------------------
set(TEST_GEN ${CMAKE_CURRENT_BINARY_DIR}/gen_test)
file(MAKE_DIRECTORY ${TEST_GEN})
add_custom_command(
  OUTPUT ${TEST_GEN}/test.pb.cc ${TEST_GEN}/test.pb.h
  COMMAND protoc ${CMAKE_CURRENT_SOURCE_DIR}/tests/test.proto
          --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/tests
          --cpp_out=${TEST_GEN}
  DEPENDS protoc ${CMAKE_CURRENT_SOURCE_DIR}/tests/test.proto
  COMMENT "Generating test.pb.cc")
add_executable(ref tests/ref_main.cc ${TEST_GEN}/test.pb.cc)
target_compile_features(ref PRIVATE cxx_std_17)
target_include_directories(ref PRIVATE SYSTEM ${PB_INC} ${TEST_GEN})
target_link_libraries(ref PRIVATE protobuf::libprotobuf)

set(TT_GEN ${CMAKE_CURRENT_BINARY_DIR}/gen_tt)
file(MAKE_DIRECTORY ${TT_GEN})
add_custom_command(
  OUTPUT ${TT_GEN}/google/protobuf/test_messages_proto3.pb.cc
         ${TT_GEN}/google/protobuf/test_messages_proto3.pb.h
  COMMAND protoc ${protobuf_SOURCE_DIR}/src/google/protobuf/test_messages_proto3.proto
          --proto_path=${protobuf_SOURCE_DIR}/src
          --cpp_out=${TT_GEN}
  DEPENDS protoc ${protobuf_SOURCE_DIR}/src/google/protobuf/test_messages_proto3.proto
  COMMENT "Generating test_messages_proto3.pb.cc")
add_executable(ref_tt tests/ref_main_tt.cc
               ${TT_GEN}/google/protobuf/test_messages_proto3.pb.cc)
target_compile_features(ref_tt PRIVATE cxx_std_17)
target_include_directories(ref_tt PRIVATE SYSTEM ${PB_INC} ${TT_GEN})
target_link_libraries(ref_tt PRIVATE protobuf::libprotobuf)

# --- benchmark (not part of ctest: timing is too noisy) -----------------
# Compares the reflection codec against protoc-generated code on the same
# TestAllTypesProto3 fixture; see bench/bench.cpp and README "Benchmarks".
add_executable(bench bench/bench.cpp
               ${TT_GEN}/google/protobuf/test_messages_proto3.pb.cc)
reflect_compile(bench)
target_include_directories(bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
                           SYSTEM ${TT_GEN})

# --- tests -------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
  add_test(NAME selftest COMMAND reflect_proto --test)
  add_test(NAME selftest_tt COMMAND reflect_proto_tt --test)
  add_test(NAME interop
           COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/interop.sh
                   $<TARGET_FILE:reflect_proto> $<TARGET_FILE:ref>)
  add_test(NAME interop_tt
           COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/interop_tt.sh
                   $<TARGET_FILE:reflect_proto_tt> $<TARGET_FILE:ref_tt>)
  add_test(NAME conformance
           COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/conformance.sh
                   $<TARGET_FILE:conformance_ours>
                   $<TARGET_FILE:conformance_test_runner>)
endif()
