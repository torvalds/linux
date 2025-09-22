set(LLVM_LINK_COMPONENTS
  AllTargetsAsmParsers
  AllTargetsDescs
  AllTargetsInfos
  BinaryFormat
  Core
  Demangle
  Object
  Option
  Support
  Symbolize
  TargetParser
  TextAPI
  )

set(LLVM_TARGET_DEFINITIONS Opts.td)
tablegen(LLVM Opts.inc -gen-opt-parser-defs)
add_public_tablegen_target(NmOptsTableGen)

add_llvm_tool(llvm-nm
  llvm-nm.cpp

  DEPENDS
  NmOptsTableGen
  intrinsics_gen
  GENERATE_DRIVER
  )

setup_host_tool(llvm-nm LLVM_NM llvm_nm_exe llvm_nm_target)

if(LLVM_INSTALL_BINUTILS_SYMLINKS)
  add_llvm_tool_symlink(nm llvm-nm)
endif()
