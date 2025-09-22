if( CMAKE_HOST_UNIX AND HAVE_LIBRT )
  set(rt_lib rt)
endif()

add_llvm_component_library(LLVMOrcDebugging
  DebugInfoSupport.cpp
  DebuggerSupport.cpp
  DebuggerSupportPlugin.cpp
  LLJITUtilsCBindings.cpp
  PerfSupportPlugin.cpp
  VTuneSupportPlugin.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/ExecutionEngine/Orc/Debugging/

  LINK_LIBS
  ${LLVM_PTHREAD_LIB}
  ${rt_lib}

  LINK_COMPONENTS
  DebugInfoDWARF
  JITLink
  OrcJIT
  OrcShared
  Support
  TargetParser
  )
