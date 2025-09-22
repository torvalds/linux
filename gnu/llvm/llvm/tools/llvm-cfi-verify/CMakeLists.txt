set(LLVM_LINK_COMPONENTS
  AllTargetsAsmParsers
  AllTargetsDescs
  AllTargetsDisassemblers
  AllTargetsInfos
  MC
  MCParser
  Object
  Support
  Symbolize
  )

add_llvm_tool(llvm-cfi-verify
  llvm-cfi-verify.cpp
  )

add_subdirectory(lib)
target_link_libraries(llvm-cfi-verify PRIVATE LLVMCFIVerify)
