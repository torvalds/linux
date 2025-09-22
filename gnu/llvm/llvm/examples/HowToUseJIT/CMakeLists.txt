set(LLVM_LINK_COMPONENTS
  Core
  ExecutionEngine
  Interpreter
  MC
  MCJIT
  Support
  nativecodegen
  )

add_llvm_example(HowToUseJIT
  HowToUseJIT.cpp
  )
