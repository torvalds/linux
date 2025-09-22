
set(LLVM_LINK_COMPONENTS
  AllTargetsDescs
  AllTargetsDisassemblers
  AllTargetsInfos
  DebugInfoDWARF
  Core
  MC
  IPO
  MCDisassembler
  Object
  ProfileData
  Support
  Symbolize
  TargetParser
  )

add_llvm_tool(llvm-profgen
  llvm-profgen.cpp
  PerfReader.cpp
  CSPreInliner.cpp
  ProfiledBinary.cpp
  ProfileGenerator.cpp
  MissingFrameInferrer.cpp
  )
