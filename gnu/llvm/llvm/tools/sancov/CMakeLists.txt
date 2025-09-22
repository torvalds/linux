set(LLVM_LINK_COMPONENTS
  AllTargetsDescs
  AllTargetsDisassemblers
  AllTargetsInfos
  MC
  MCDisassembler
  Object
  Option
  Support
  Symbolize
  TargetParser
  )

set(LLVM_TARGET_DEFINITIONS Opts.td)
tablegen(LLVM Opts.inc -gen-opt-parser-defs)
add_public_tablegen_target(SancovOptsTableGen)

add_llvm_tool(sancov
  sancov.cpp

  DEPENDS
  SancovOptsTableGen
  GENERATE_DRIVER
  )
