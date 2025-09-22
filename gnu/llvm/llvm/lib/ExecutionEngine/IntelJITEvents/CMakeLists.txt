include_directories( ${CMAKE_CURRENT_SOURCE_DIR}/.. )
include_directories( ${PROJECT_BINARY_DIR}/ittapi/include/ )
add_llvm_component_library(LLVMIntelJITEvents
  IntelJITEventListener.cpp


  LINK_COMPONENTS
  CodeGen
  Core
  DebugInfoDWARF
  Support
  Object
  ExecutionEngine
  IntelJITProfiling
)

add_dependencies(LLVMIntelJITEvents LLVMCodeGen)
