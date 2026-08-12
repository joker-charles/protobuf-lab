cmake_minimum_required(VERSION 3.20)
project(reflect_proto LANGUAGES CXX)

# Experimental: protobuf wire-format codec driven by C++26 static
# reflection (P2996).  Wire primitives come from protobuf's own
# coded_stream.h; reflection drives field dispatch.  Requires g++-16.
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(PB_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../protobuf-3.21.12)

add_executable(reflect_proto src/main.cpp)
target_compile_options(reflect_proto PRIVATE -freflection -Wall -Wextra
                       -Wpedantic -Wconversion)
target_include_directories(reflect_proto PRIVATE SYSTEM ${PB_ROOT}/src)
target_link_libraries(reflect_proto PRIVATE ${PB_ROOT}/build/libprotobuf.a
                      pthread z)

add_executable(reflect_proto_tt src/main_tt.cpp)
target_compile_options(reflect_proto_tt PRIVATE -freflection -Wall -Wextra
                       -Wpedantic -Wconversion)
target_include_directories(reflect_proto_tt PRIVATE SYSTEM ${PB_ROOT}/src)
target_link_libraries(reflect_proto_tt PRIVATE ${PB_ROOT}/build/libprotobuf.a
                      pthread z)

include(CTest)
if(BUILD_TESTING)
  add_test(NAME selftest COMMAND reflect_proto --test)
  add_test(NAME selftest_tt COMMAND reflect_proto_tt --test)
  add_test(NAME interop COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/interop.sh)
  add_test(NAME interop_tt
           COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/interop_tt.sh)
endif()
