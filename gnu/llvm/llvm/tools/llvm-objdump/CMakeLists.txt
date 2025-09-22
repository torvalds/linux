set(LLVM_LINK_COMPONENTS
  AllTargetsDescs
  AllTargetsDisassemblers
  AllTargetsInfos
  BinaryFormat
  DebugInfoBTF
  DebugInfoDWARF
  Demangle
  MC
  MCDisassembler
  Object
  Option
  Support
  Symbolize
  TargetParser
  )

set(LLVM_TARGET_DEFINITIONS ObjdumpOpts.td)
tablegen(LLVM ObjdumpOpts.inc -gen-opt-parser-defs)
add_public_tablegen_target(ObjdumpOptsTableGen)

set(LLVM_TARGET_DEFINITIONS OtoolOpts.td)
tablegen(LLVM OtoolOpts.inc -gen-opt-parser-defs)
add_public_tablegen_target(OtoolOptsTableGen)

add_llvm_tool(llvm-objdump
  llvm-objdump.cpp
  SourcePrinter.cpp
  COFFDump.cpp
  ELFDump.cpp
  MachODump.cpp
  OffloadDump.cpp
  WasmDump.cpp
  XCOFFDump.cpp
  DEPENDS
  ObjdumpOptsTableGen
  OtoolOptsTableGen
  GENERATE_DRIVER
  )

if(NOT LLVM_TOOL_LLVM_DRIVER_BUILD)
  target_link_libraries(llvm-objdump PRIVATE LLVMDebuginfod)
endif()

add_llvm_tool_symlink(llvm-otool llvm-objdump)

if(LLVM_INSTALL_BINUTILS_SYMLINKS)
  add_llvm_tool_symlink(objdump llvm-objdump)
endif()

if(LLVM_INSTALL_CCTOOLS_SYMLINKS)
  add_llvm_tool_symlink(otool llvm-otool)
endif()
