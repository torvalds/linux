set(LLVM_LINK_COMPONENTS
  AllTargetsCodeGens
  AllTargetsDescs
  AllTargetsInfos
  DebugInfoDWARF
  DWP
  MC
  Object
  Option
  Support
  TargetParser
  )

set(LLVM_TARGET_DEFINITIONS Opts.td)
tablegen(LLVM Opts.inc -gen-opt-parser-defs)
add_public_tablegen_target(DwpOptsTableGen)

add_llvm_tool(llvm-dwp
  llvm-dwp.cpp

  DEPENDS
  intrinsics_gen
  DwpOptsTableGen
  GENERATE_DRIVER
  )

if(LLVM_INSTALL_BINUTILS_SYMLINKS)
  add_llvm_tool_symlink(dwp llvm-dwp)
endif()
