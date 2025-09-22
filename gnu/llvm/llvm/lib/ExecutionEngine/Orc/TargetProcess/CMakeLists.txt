if( CMAKE_HOST_UNIX AND HAVE_LIBRT )
  set(rt_lib rt)
endif()

set(intel_jit_profiling )
if( LLVM_USE_INTEL_JITEVENTS )
  set(intel_jit_profiling IntelJITProfiling)
  include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../IntelJITProfiling)
  include_directories(${PROJECT_BINARY_DIR}/ittapi/include/ )
endif()

add_llvm_component_library(LLVMOrcTargetProcess
  ExecutorSharedMemoryMapperService.cpp
  JITLoaderGDB.cpp
  JITLoaderPerf.cpp
  JITLoaderVTune.cpp
  OrcRTBootstrap.cpp
  RegisterEHFrames.cpp
  SimpleExecutorDylibManager.cpp
  SimpleExecutorMemoryManager.cpp
  SimpleRemoteEPCServer.cpp
  TargetExecutionUtils.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/ExecutionEngine/Orc

  LINK_LIBS
  ${LLVM_PTHREAD_LIB}
  ${rt_lib}

  LINK_COMPONENTS
  ${intel_jit_profiling}
  OrcShared
  Support
  TargetParser
  )
