add_llvm_component_library(LLVMDWARFLinkerClassic
  DWARFLinkerCompileUnit.cpp
  DWARFLinkerDeclContext.cpp
  DWARFLinker.cpp
  DWARFStreamer.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/DWARFLinker

  DEPENDS
  intrinsics_gen

  LINK_LIBS
  ${LLVM_PTHREAD_LIB}

  LINK_COMPONENTS
  AsmPrinter
  BinaryFormat
  CodeGen
  CodeGenTypes
  DebugInfoDWARF
  DWARFLinker
  MC
  Object
  Support
  TargetParser
  )
