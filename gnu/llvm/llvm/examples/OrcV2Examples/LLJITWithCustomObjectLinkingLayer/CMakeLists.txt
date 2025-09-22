set(LLVM_LINK_COMPONENTS
  Core
  IRReader
  JITLink
  OrcJIT
  Support
  nativecodegen
  )

add_llvm_example(LLJITWithCustomObjectLinkingLayer
  LLJITWithCustomObjectLinkingLayer.cpp
  )

export_executable_symbols(LLJITWithCustomObjectLinkingLayer)
