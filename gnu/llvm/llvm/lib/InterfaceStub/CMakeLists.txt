add_llvm_component_library(LLVMInterfaceStub
  ELFObjHandler.cpp
  IFSHandler.cpp
  IFSStub.cpp

  LINK_COMPONENTS
  BinaryFormat
  MC
  Object
  Support
  TargetParser
)
