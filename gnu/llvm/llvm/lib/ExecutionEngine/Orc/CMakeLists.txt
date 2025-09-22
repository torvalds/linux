if (NOT HAVE_CXX_ATOMICS64_WITHOUT_LIB)
  set (atomic_lib atomic)
endif()

if( CMAKE_HOST_UNIX AND HAVE_LIBRT )
  set(rt_lib rt)
endif()

add_llvm_component_library(LLVMOrcJIT
  COFFVCRuntimeSupport.cpp
  COFFPlatform.cpp
  CompileOnDemandLayer.cpp
  CompileUtils.cpp
  Core.cpp
  DebugObjectManagerPlugin.cpp
  DebugUtils.cpp
  EPCDynamicLibrarySearchGenerator.cpp
  EPCDebugObjectRegistrar.cpp
  EPCEHFrameRegistrar.cpp
  EPCGenericDylibManager.cpp
  EPCGenericJITLinkMemoryManager.cpp
  EPCGenericRTDyldMemoryManager.cpp
  EPCIndirectionUtils.cpp
  ExecutionUtils.cpp
  ObjectFileInterface.cpp
  IndirectionUtils.cpp
  IRCompileLayer.cpp
  IRTransformLayer.cpp
  JITTargetMachineBuilder.cpp
  LazyReexports.cpp
  Layer.cpp
  LookupAndRecordAddrs.cpp
  LLJIT.cpp
  MachOPlatform.cpp
  MapperJITLinkMemoryManager.cpp
  MemoryMapper.cpp
  ELFNixPlatform.cpp
  Mangling.cpp
  ObjectLinkingLayer.cpp
  ObjectTransformLayer.cpp
  OrcABISupport.cpp
  OrcV2CBindings.cpp
  RTDyldObjectLinkingLayer.cpp
  SectCreate.cpp
  SimpleRemoteEPC.cpp
  Speculation.cpp
  SpeculateAnalyses.cpp
  ExecutorProcessControl.cpp
  TaskDispatch.cpp
  ThreadSafeModule.cpp
  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/ExecutionEngine/Orc

  DEPENDS
  intrinsics_gen

  LINK_LIBS
  ${LLVM_PTHREAD_LIB}
  ${rt_lib}
  ${atomic_lib}

  LINK_COMPONENTS
  Core
  ExecutionEngine
  JITLink
  Object
  OrcShared
  OrcTargetProcess
  WindowsDriver
  MC
  Passes
  RuntimeDyld
  Support
  Target
  TargetParser
  TransformUtils
  )

add_subdirectory(Debugging)
add_subdirectory(Shared)
add_subdirectory(TargetProcess)

target_link_libraries(LLVMOrcJIT
  PRIVATE
  LLVMAnalysis
  LLVMBitReader
  LLVMBitWriter
  LLVMPasses
  )
