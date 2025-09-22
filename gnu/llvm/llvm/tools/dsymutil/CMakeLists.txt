set(LLVM_TARGET_DEFINITIONS Options.td)
tablegen(LLVM Options.inc -gen-opt-parser-defs)
add_public_tablegen_target(DsymutilTableGen)

set(LLVM_LINK_COMPONENTS
  AllTargetsCodeGens
  AllTargetsDescs
  AllTargetsInfos
  AsmPrinter
  CodeGen
  CodeGenTypes
  DWARFLinker
  DWARFLinkerClassic
  DWARFLinkerParallel
  DebugInfoDWARF
  MC
  Object
  Option
  Remarks
  Support
  Target
  TargetParser
  )

add_llvm_tool(dsymutil
  dsymutil.cpp
  BinaryHolder.cpp
  CFBundle.cpp
  DebugMap.cpp
  DwarfLinkerForBinary.cpp
  MachODebugMapParser.cpp
  MachOUtils.cpp
  Reproducer.cpp
  RelocationMap.cpp

  DEPENDS
  intrinsics_gen
  ${tablegen_deps}
  DsymutilTableGen
  GENERATE_DRIVER
  )

if(APPLE AND NOT LLVM_TOOL_LLVM_DRIVER_BUILD)
  target_link_libraries(dsymutil PRIVATE "-framework CoreFoundation")
endif(APPLE AND NOT LLVM_TOOL_LLVM_DRIVER_BUILD)

# target_link_libraries(dsymutil PRIVATE ${LLVM_ATOMIC_LIB})
