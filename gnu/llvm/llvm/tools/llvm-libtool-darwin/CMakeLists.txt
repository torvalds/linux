set(LLVM_LINK_COMPONENTS
  BinaryFormat
  Core
  Object
  Option
  Support
  TargetParser
  TextAPI
  ${LLVM_TARGETS_TO_BUILD}
  )

set(LLVM_TARGET_DEFINITIONS Opts.td)
tablegen(LLVM Opts.inc -gen-opt-parser-defs)
add_public_tablegen_target(LibtoolDarwinOptsTableGen)

add_llvm_tool(llvm-libtool-darwin
  llvm-libtool-darwin.cpp

  DEPENDS
  LibtoolDarwinOptsTableGen
  GENERATE_DRIVER
)

if(LLVM_INSTALL_CCTOOLS_SYMLINKS)
  add_llvm_tool_symlink(libtool llvm-libtool-darwin)
endif()
