set(LLVM_LINK_COMPONENTS
  Core
  ExecutionEngine
  MC
  MCJIT
  RuntimeDyld
  Support
  Target
  nativecodegen
  )

# Enable EH and RTTI for this demo
if(NOT LLVM_ENABLE_EH)
  message(FATAL_ERROR "ExceptionDemo must require EH.")
endif()

add_llvm_example(ExceptionDemo
  ExceptionDemo.cpp
  )

export_executable_symbols(ExceptionDemo)
