cmake_minimum_required(VERSION 3.20)
project(rpb_consumer CXX)

# Consumes reflect-proto exactly like the README's FetchContent flow.
# CI builds this with -DREFLECT_PROTO_DIR=$GITHUB_WORKSPACE; locally:
#   cmake -S tests/consumer -B /tmp/consumer-build \
#     -DCMAKE_CXX_COMPILER=g++-16 -DREFLECT_PROTO_DIR=$PWD \
#     -DFETCHCONTENT_SOURCE_DIR_PROTOBUF=$PWD/protobuf-3.21.12
# The README shows the GIT_REPOSITORY/GIT_TAG variant; SOURCE_DIR keeps the
# CI smoke test offline for the codec itself (protobuf is still fetched).
set(REFLECT_PROTO_DIR "" CACHE PATH "Path to the reflect-proto checkout")
if(NOT REFLECT_PROTO_DIR)
  message(FATAL_ERROR "REFLECT_PROTO_DIR must point at a reflect-proto checkout")
endif()

include(FetchContent)
FetchContent_Declare(reflect_proto SOURCE_DIR ${REFLECT_PROTO_DIR})
FetchContent_MakeAvailable(reflect_proto)

add_executable(app app.cpp)
target_link_libraries(app PRIVATE rpb)
