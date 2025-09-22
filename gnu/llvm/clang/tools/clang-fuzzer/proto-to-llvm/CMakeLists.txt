set(LLVM_LINK_COMPONENTS ${LLVM_TARGETS_TO_BUILD})
set(CMAKE_CXX_FLAGS ${CXX_FLAGS_NOFUZZ})

# Needed by LLVM's CMake checks because this file defines multiple targets.
set(LLVM_OPTIONAL_SOURCES loop_proto_to_llvm.cpp loop_proto_to_llvm_main.cpp)

add_clang_library(clangLoopProtoToLLVM loop_proto_to_llvm.cpp
                  DEPENDS clangCXXLoopProto
                  LINK_LIBS clangCXXLoopProto ${PROTOBUF_LIBRARIES}
                  )

add_clang_executable(clang-loop-proto-to-llvm loop_proto_to_llvm_main.cpp)

target_link_libraries(clang-loop-proto-to-llvm PRIVATE clangLoopProtoToLLVM)
