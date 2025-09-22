set(LLVM_LINK_COMPONENTS
  Object
  Option
  Support
  TargetParser
  )

set(LLVM_TARGET_DEFINITIONS Opts.td)
tablegen(LLVM Opts.inc -gen-opt-parser-defs)
add_public_tablegen_target(SizeOptsTableGen)

add_llvm_tool(llvm-size
  llvm-size.cpp
  DEPENDS
  SizeOptsTableGen
  GENERATE_DRIVER
  )

if(LLVM_INSTALL_BINUTILS_SYMLINKS)
  add_llvm_tool_symlink(size llvm-size)
endif()
