set(LLVM_LINK_COMPONENTS
  BinaryFormat
  InterfaceStub
  ObjectYAML
  Option
  Support
  TargetParser
  TextAPI
  )

set(LLVM_TARGET_DEFINITIONS Opts.td)
tablegen(LLVM Opts.inc -gen-opt-parser-defs)
add_public_tablegen_target(IFSOptsTableGen)

add_llvm_tool(llvm-ifs
  ErrorCollector.cpp
  llvm-ifs.cpp

DEPENDS
  IFSOptsTableGen

  GENERATE_DRIVER
  )
