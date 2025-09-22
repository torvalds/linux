set(LLVM_LINK_COMPONENTS
  Core
  OrcJIT
  Support
  nativecodegen
  )

add_llvm_example(HowToUseLLJIT
  HowToUseLLJIT.cpp
  )

export_executable_symbols(HowToUseLLJIT)
