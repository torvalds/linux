set(LLVM_LINK_COMPONENTS
  BinaryFormat
  Object
  Support
  TargetParser
  )

add_clang_tool(clang-offload-bundler
  ClangOffloadBundler.cpp

  DEPENDS
  intrinsics_gen
  )

set(CLANG_OFFLOAD_BUNDLER_LIB_DEPS
  clangBasic
  clangDriver
  )

clang_target_link_libraries(clang-offload-bundler
  PRIVATE
  ${CLANG_OFFLOAD_BUNDLER_LIB_DEPS}
  )
