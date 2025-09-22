set(LLVM_LINK_COMPONENTS
  Option
  Support
  WindowsManifest
  )

set(LLVM_TARGET_DEFINITIONS Opts.td)

tablegen(LLVM Opts.inc -gen-opt-parser-defs)
add_public_tablegen_target(MtTableGen)

add_llvm_tool(llvm-mt
  llvm-mt.cpp
  DEPENDS
  MtTableGen
  GENERATE_DRIVER
  )
