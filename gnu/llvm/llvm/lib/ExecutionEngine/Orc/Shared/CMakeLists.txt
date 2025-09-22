add_llvm_component_library(LLVMOrcShared
  AllocationActions.cpp
  ObjectFormats.cpp
  OrcError.cpp
  OrcRTBridge.cpp
  SimpleRemoteEPCUtils.cpp
  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/ExecutionEngine/Orc

  DEPENDS
  intrinsics_gen

  LINK_LIBS
  ${LLVM_PTHREAD_LIB}

  LINK_COMPONENTS
  Support
  )
