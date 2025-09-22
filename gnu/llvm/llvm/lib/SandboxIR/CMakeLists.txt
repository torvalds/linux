add_llvm_component_library(LLVMSandboxIR
  SandboxIR.cpp
  Tracker.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms/SandboxIR

  LINK_COMPONENTS
  Core
  Support
  )

