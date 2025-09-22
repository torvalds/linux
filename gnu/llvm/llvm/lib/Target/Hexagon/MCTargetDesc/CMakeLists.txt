add_llvm_component_library(LLVMHexagonDesc
  HexagonAsmBackend.cpp
  HexagonELFObjectWriter.cpp
  HexagonInstPrinter.cpp
  HexagonMCAsmInfo.cpp
  HexagonMCChecker.cpp
  HexagonMCCodeEmitter.cpp
  HexagonMCCompound.cpp
  HexagonMCDuplexInfo.cpp
  HexagonMCELFStreamer.cpp
  HexagonMCExpr.cpp
  HexagonMCInstrInfo.cpp
  HexagonMCShuffler.cpp
  HexagonMCTargetDesc.cpp
  HexagonShuffler.cpp

  LINK_COMPONENTS
  HexagonInfo
  MC
  Support

  ADD_TO_COMPONENT
  Hexagon
  )

add_dependencies(LLVMHexagonDesc HexagonCommonTableGen)
