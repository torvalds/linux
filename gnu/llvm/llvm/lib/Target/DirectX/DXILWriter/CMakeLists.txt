include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../)

add_llvm_component_library(LLVMDXILBitWriter
  DXILBitcodeWriter.cpp
  DXILValueEnumerator.cpp
  DXILWriterPass.cpp

  DEPENDS
  intrinsics_gen

  LINK_COMPONENTS
  Analysis
  BitWriter
  Core
  DirectXPointerTypeAnalysis
  MC
  Object
  Support
  TargetParser
  TransformUtils
  )
