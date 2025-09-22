set(LLVM_LINK_COMPONENTS ${LLVM_TARGETS_TO_BUILD} FuzzerCLI)
set(CXX_FLAGS_NOFUZZ ${CMAKE_CXX_FLAGS})
set(DUMMY_MAIN DummyClangFuzzer.cpp)
if(LLVM_LIB_FUZZING_ENGINE)
  unset(DUMMY_MAIN)
elseif(LLVM_USE_SANITIZE_COVERAGE)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=fuzzer")
  set(CXX_FLAGS_NOFUZZ "${CXX_FLAGS_NOFUZZ} -fsanitize=fuzzer-no-link")
  unset(DUMMY_MAIN)
endif()

# Needed by LLVM's CMake checks because this file defines multiple targets.
set(LLVM_OPTIONAL_SOURCES
  ClangFuzzer.cpp
  ClangObjectiveCFuzzer.cpp
  DummyClangFuzzer.cpp
  ExampleClangProtoFuzzer.cpp
  ExampleClangLoopProtoFuzzer.cpp
  ExampleClangLLVMProtoFuzzer.cpp
  )

if(CLANG_ENABLE_PROTO_FUZZER)
  # Create protobuf .h and .cc files, and put them in a library for use by
  # clang-proto-fuzzer components.
  find_package(Protobuf REQUIRED)
  add_definitions(-DGOOGLE_PROTOBUF_NO_RTTI)
  include_directories(${PROTOBUF_INCLUDE_DIRS})
  include_directories(${CMAKE_CURRENT_BINARY_DIR})
  protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS cxx_proto.proto)
  protobuf_generate_cpp(LOOP_PROTO_SRCS LOOP_PROTO_HDRS cxx_loop_proto.proto)
  set(LLVM_OPTIONAL_SOURCES ${LLVM_OPTIONAL_SOURCES} ${PROTO_SRCS})
  add_clang_library(clangCXXProto
    ${PROTO_SRCS}
    ${PROTO_HDRS}

    LINK_LIBS
    ${PROTOBUF_LIBRARIES}
    )

  add_clang_library(clangCXXLoopProto
    ${LOOP_PROTO_SRCS}
    ${LOOP_PROTO_HDRS}

    LINK_LIBS
    ${PROTOBUF_LIBRARIES}
    )

  # Build and include libprotobuf-mutator
  include(ProtobufMutator)
  include_directories(${ProtobufMutator_INCLUDE_DIRS})

  # Build the protobuf->C++ translation library and driver.
  add_clang_subdirectory(proto-to-cxx)

  # Build the protobuf->LLVM IR translation library and driver.
  add_clang_subdirectory(proto-to-llvm)
  
  # Build the fuzzer initialization library.
  add_clang_subdirectory(fuzzer-initialize)

  # Build the protobuf fuzzer
  add_clang_executable(clang-proto-fuzzer
    ${DUMMY_MAIN}
    ExampleClangProtoFuzzer.cpp
    )

  # Build the loop protobuf fuzzer
  add_clang_executable(clang-loop-proto-fuzzer
    ${DUMMY_MAIN}
    ExampleClangLoopProtoFuzzer.cpp
    )

  # Build the llvm protobuf fuzzer
  add_clang_executable(clang-llvm-proto-fuzzer
    ${DUMMY_MAIN}
    ExampleClangLLVMProtoFuzzer.cpp
    )

  set(COMMON_PROTO_FUZZ_LIBRARIES
    ${ProtobufMutator_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    ${LLVM_LIB_FUZZING_ENGINE}
    clangFuzzerInitialize
    )

  target_link_libraries(clang-proto-fuzzer
    PRIVATE
    ${COMMON_PROTO_FUZZ_LIBRARIES}
    clangHandleCXX
    clangCXXProto
    clangProtoToCXX
    )
  target_link_libraries(clang-loop-proto-fuzzer
    PRIVATE
    ${COMMON_PROTO_FUZZ_LIBRARIES}
    clangHandleCXX
    clangCXXLoopProto
    clangLoopProtoToCXX
    )
  target_link_libraries(clang-llvm-proto-fuzzer
    PRIVATE
    ${COMMON_PROTO_FUZZ_LIBRARIES}
    clangHandleLLVM
    clangCXXLoopProto
    clangLoopProtoToLLVM
    )

endif()

add_clang_subdirectory(handle-cxx)
add_clang_subdirectory(handle-llvm)
add_clang_subdirectory(dictionary)

add_clang_executable(clang-fuzzer
  EXCLUDE_FROM_ALL
  ${DUMMY_MAIN}
  ClangFuzzer.cpp

  DEPENDS
  ClangDriverOptions
  )

target_link_libraries(clang-fuzzer
  PRIVATE
  ${LLVM_LIB_FUZZING_ENGINE}
  clangHandleCXX
  )

add_clang_executable(clang-objc-fuzzer
  EXCLUDE_FROM_ALL
  ${DUMMY_MAIN}
  ClangObjectiveCFuzzer.cpp

  DEPENDS
  ClangDriverOptions
  )

target_link_libraries(clang-objc-fuzzer
  PRIVATE
  ${LLVM_LIB_FUZZING_ENGINE}
  clangHandleCXX
  )
