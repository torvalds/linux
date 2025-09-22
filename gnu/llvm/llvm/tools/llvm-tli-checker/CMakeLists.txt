set(LLVM_LINK_COMPONENTS
  Analysis
  BinaryFormat
  BitReader
  BitstreamReader
  Core
  Demangle
  MC
  MCParser
  Object
  Option
  Remarks
  Support
  TargetParser
  TextAPI
  )

set(LLVM_TARGET_DEFINITIONS Opts.td)
tablegen(LLVM Opts.inc -gen-opt-parser-defs)
add_public_tablegen_target(TLICheckerOptsTableGen)

add_llvm_tool(llvm-tli-checker
  llvm-tli-checker.cpp

  DEPENDS
  TLICheckerOptsTableGen
  )
