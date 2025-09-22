set(LLVM_LINK_COMPONENTS ${LLVM_TARGETS_TO_BUILD})
set(CMAKE_CXX_FLAGS ${CXX_FLAGS_NOFUZZ})

# Needed by LLVM's CMake checks because this file defines multiple targets.
set(LLVM_OPTIONAL_SOURCES proto_to_cxx.cpp proto_to_cxx_main.cpp
                          loop_proto_to_cxx.cpp loop_proto_to_cxx_main.cpp)

add_clang_library(clangProtoToCXX proto_to_cxx.cpp
                  DEPENDS clangCXXProto
                  LINK_LIBS clangCXXProto ${PROTOBUF_LIBRARIES}
                  )

add_clang_library(clangLoopProtoToCXX loop_proto_to_cxx.cpp
                  DEPENDS clangCXXLoopProto
                  LINK_LIBS clangCXXLoopProto ${PROTOBUF_LIBRARIES}
                  )
target_include_directories(clangProtoToCXX PRIVATE .)
target_include_directories(clangLoopProtoToCXX PRIVATE .)

add_clang_executable(clang-proto-to-cxx proto_to_cxx_main.cpp)
add_clang_executable(clang-loop-proto-to-cxx loop_proto_to_cxx_main.cpp)

target_link_libraries(clang-proto-to-cxx PRIVATE clangProtoToCXX)
target_link_libraries(clang-loop-proto-to-cxx PRIVATE clangLoopProtoToCXX)
