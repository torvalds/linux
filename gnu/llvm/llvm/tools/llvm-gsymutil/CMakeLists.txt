set(LLVM_LINK_COMPONENTS
  ${LLVM_TARGETS_TO_BUILD}
  DebugInfoDWARF
  DebugInfoGSYM
  MC
  Object
  Option
  Support
  TargetParser
  )

set(LLVM_TARGET_DEFINITIONS Opts.td)
tablegen(LLVM Opts.inc -gen-opt-parser-defs)
add_public_tablegen_target(GSYMUtilOptsTableGen)

add_llvm_tool(llvm-gsymutil
  llvm-gsymutil.cpp

  DEPENDS
  GSYMUtilOptsTableGen

  GENERATE_DRIVER
  )
