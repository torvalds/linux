set(LLVM_LINK_COMPONENTS
  ${LLVM_TARGETS_TO_BUILD}
  Core
  Option
  Support
  TargetParser
  )

set(LLVM_TARGET_DEFINITIONS Opts.td)
tablegen(LLVM Opts.inc -gen-opt-parser-defs)
add_public_tablegen_target(ScanDepsOptsTableGen)

add_clang_tool(clang-scan-deps
  ClangScanDeps.cpp

  DEPENDS
  ScanDepsOptsTableGen
  GENERATE_DRIVER
  )

set(CLANG_SCAN_DEPS_LIB_DEPS
  clangAST
  clangBasic
  clangDependencyScanning
  clangDriver
  clangFrontend
  clangLex
  clangSerialization
  clangTooling
  )

clang_target_link_libraries(clang-scan-deps
  PRIVATE
  ${CLANG_SCAN_DEPS_LIB_DEPS}
  )

