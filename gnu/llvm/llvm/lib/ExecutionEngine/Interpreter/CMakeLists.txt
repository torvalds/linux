add_llvm_component_library(LLVMInterpreter
  Execution.cpp
  ExternalFunctions.cpp
  Interpreter.cpp

  DEPENDS
  intrinsics_gen

  LINK_COMPONENTS
  CodeGen
  Core
  ExecutionEngine
  Support
  )

if( LLVM_ENABLE_FFI )
  target_link_libraries( LLVMInterpreter PRIVATE FFI::ffi )
endif()
