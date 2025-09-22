add_lldb_library(lldbPluginDisassemblerLLVMC PLUGIN
  DisassemblerLLVMC.cpp

  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbTarget
  LINK_COMPONENTS
    ${LLVM_TARGETS_TO_BUILD}
    MC
    MCDisassembler
    RuntimeDyld
    Support
    TargetParser
  )
