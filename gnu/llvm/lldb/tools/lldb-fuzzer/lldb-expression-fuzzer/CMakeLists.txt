if(CLANG_ENABLE_PROTO_FUZZER)
  set(LLVM_LINK_COMPONENTS
    Support
    )

  add_llvm_fuzzer(lldb-expression-fuzzer
    EXCLUDE_FROM_ALL
    lldb-expression-fuzzer.cpp
    )

  if(TARGET lldb-expression-fuzzer)
    target_include_directories(lldb-expression-fuzzer PRIVATE ..)
    find_package(Protobuf REQUIRED)
    add_definitions(-DGOOGLE_PROTOBUF_NO_RTTI)
    include_directories(${PROTOBUF_INCLUDE_DIRS})
    include_directories(${CMAKE_CURRENT_BINARY_DIR}/../../../../clang/tools/clang-fuzzer PRIVATE ..)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../../../clang/tools/clang-fuzzer)

    set(CLANG_CMAKE_MODULE_PATH
      ${CMAKE_CURRENT_SOURCE_DIR}/../../../../clang/cmake/modules)

    set(CMAKE_MODULE_PATH
      ${CMAKE_MODULE_PATH}
      ${CLANG_CMAKE_MODULE_PATH})


    set (PBM_PREFIX lldb_protobuf_mutator)
    include(ProtobufMutator)
    include_directories(${ProtobufMutator_INCLUDE_DIRS})

    target_link_libraries(lldb-expression-fuzzer
      PRIVATE
      ${ProtobufMutator_LIBRARIES}
      ${LLVM_LIB_FUZZING_ENGINE}
      clangHandleCXX
      clangCXXProto
      clangProtoToCXX
      liblldb
      )

    add_custom_command(TARGET lldb-expression-fuzzer PRE_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/fuzzer-artifacts/expression-artifacts
      # Create and compile a simple C program using the command line. This is
      # needed because LLDB's expression evaluator needs a legitmate target
      # instead of a dummy target
      COMMAND echo 'int main (int argc, char** argv) { return 0\; }' | clang -o main.out -xc -
      )

    # Create a directory for storing the fuzzer's artifacts and run the fuzzer with arguments that will
    # not attempt to reduce the size of the inputs being generated
    # Also set the executable that's created above as an environment variable for the
    # source code to use
    add_custom_target(fuzz-lldb-expression
      COMMENT "Running the LLDB expression evaluator fuzzer..."
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/fuzzer-artifacts/expression-artifacts
      COMMAND ${CMAKE_COMMAND} -E env LLDB_FUZZER_TARGET=${CMAKE_CURRENT_BINARY_DIR}/main.out $<TARGET_FILE:lldb-expression-fuzzer> -artifact_prefix=expression- -reduce_inputs=0
      USES_TERMINAL
      )
  endif()
endif()
