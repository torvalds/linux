set(LLVM_LINK_COMPONENTS
  BinaryFormat
  Object
  Option
  Support
  )

set(LLVM_TARGET_DEFINITIONS Opts.td)

tablegen(LLVM Opts.inc -gen-opt-parser-defs)
add_public_tablegen_target(CvtResTableGen)

add_llvm_tool(llvm-cvtres
  llvm-cvtres.cpp
  )
