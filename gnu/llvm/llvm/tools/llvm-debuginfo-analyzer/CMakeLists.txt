set(LLVM_LINK_COMPONENTS
  AllTargetsDescs
  AllTargetsDisassemblers
  AllTargetsInfos
  BinaryFormat
  DebugInfoCodeView
  DebugInfoLogicalView
  DebugInfoPDB
  MC
  MCDisassembler
  Object
  Support
  )

add_llvm_tool(llvm-debuginfo-analyzer
  llvm-debuginfo-analyzer.cpp
  Options.cpp
  )
