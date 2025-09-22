add_llvm_component_library(LLVMDebugInfoGSYM
  DwarfTransformer.cpp
  Header.cpp
  FileWriter.cpp
  FunctionInfo.cpp
  GsymCreator.cpp
  GsymReader.cpp
  InlineInfo.cpp
  LineTable.cpp
  LookupResult.cpp
  ObjectFileTransformer.cpp
  ExtractRanges.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/DebugInfo/GSYM
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/DebugInfo

  DEPENDS
  LLVMMC

  LINK_LIBS
  ${LLVM_PTHREAD_LIB}

  LINK_COMPONENTS
  MC
  Object
  Support
  TargetParser
  DebugInfoDWARF
  )
