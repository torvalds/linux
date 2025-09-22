add_llvm_component_library(LLVMXRay
  BlockIndexer.cpp
  BlockPrinter.cpp
  BlockVerifier.cpp
  FDRRecordProducer.cpp
  FDRRecords.cpp
  FDRTraceExpander.cpp
  FDRTraceWriter.cpp
  FileHeaderReader.cpp
  InstrumentationMap.cpp
  LogBuilderConsumer.cpp
  Profile.cpp
  RecordInitializer.cpp
  RecordPrinter.cpp
  Trace.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/ADT
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/XRay

  LINK_COMPONENTS
  Support
  Object
  TargetParser
  )
