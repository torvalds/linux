set(LLVM_LINK_COMPONENTS
  Core
  Support
  TargetParser
  )

add_clang_executable(clang-import-test
  clang-import-test.cpp
  DEPENDS
  intrinsics_gen
  )

set(CLANG_IMPORT_TEST_LIB_DEPS
  clangAST
  clangBasic
  clangCodeGen
  clangDriver
  clangFrontend
  clangLex
  clangParse
  clangSerialization
  )

clang_target_link_libraries(clang-import-test
  PRIVATE
  ${CLANG_IMPORT_TEST_LIB_DEPS}
  )
