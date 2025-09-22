set(LLVM_LINK_COMPONENTS
  BinaryFormat
  Support
  TargetParser
  TextAPI
  TextAPIBinaryReader
  Option
  )

set(LLVM_TARGET_DEFINITIONS InstallAPIOpts.td)
tablegen(LLVM InstallAPIOpts.inc -gen-opt-parser-defs)
add_public_tablegen_target(InstallAPIDriverOptions)

add_clang_tool(clang-installapi
  ClangInstallAPI.cpp
  Options.cpp

  DEPENDS
  InstallAPIDriverOptions
  GENERATE_DRIVER
  )

clang_target_link_libraries(clang-installapi
  PRIVATE
  clangAST
  clangInstallAPI
  clangBasic
  clangDriver
  clangFrontend
  clangTooling
  clangSerialization
  )

