set(LLVM_LINK_COMPONENTS
  Object
  ObjCopy
  Option
  Support
  TargetParser
  MC
  BinaryFormat
  )

set(LLVM_TARGET_DEFINITIONS ObjcopyOpts.td)
tablegen(LLVM ObjcopyOpts.inc -gen-opt-parser-defs)
add_public_tablegen_target(ObjcopyOptsTableGen)

set(LLVM_TARGET_DEFINITIONS InstallNameToolOpts.td)
tablegen(LLVM InstallNameToolOpts.inc -gen-opt-parser-defs)
add_public_tablegen_target(InstallNameToolOptsTableGen)

set(LLVM_TARGET_DEFINITIONS BitcodeStripOpts.td)
tablegen(LLVM BitcodeStripOpts.inc -gen-opt-parser-defs)
add_public_tablegen_target(BitcodeStripOptsTableGen)

set(LLVM_TARGET_DEFINITIONS StripOpts.td)
tablegen(LLVM StripOpts.inc -gen-opt-parser-defs)
add_public_tablegen_target(StripOptsTableGen)

add_llvm_tool(llvm-objcopy
  ObjcopyOptions.cpp
  llvm-objcopy.cpp
  DEPENDS
  ObjcopyOptsTableGen
  InstallNameToolOptsTableGen
  StripOptsTableGen
  GENERATE_DRIVER
  )

add_llvm_tool_symlink(llvm-install-name-tool llvm-objcopy)
add_llvm_tool_symlink(llvm-bitcode-strip llvm-objcopy)
add_llvm_tool_symlink(llvm-strip llvm-objcopy)

if(LLVM_INSTALL_BINUTILS_SYMLINKS)
  add_llvm_tool_symlink(objcopy llvm-objcopy)
  add_llvm_tool_symlink(strip llvm-objcopy)
endif()

if(LLVM_INSTALL_CCTOOLS_SYMLINKS)
  add_llvm_tool_symlink(install_name_tool llvm-install-name-tool)
  add_llvm_tool_symlink(bitcode_strip llvm-bitcode-strip)
endif()
