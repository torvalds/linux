if(Mips IN_LIST LLVM_TARGETS_TO_BUILD)
  set(mips_target Mips)
endif()

add_lldb_library(lldbPluginInstructionMIPS64 PLUGIN
  EmulateInstructionMIPS64.cpp

  LINK_LIBS
    lldbCore
    lldbHost
    lldbInterpreter
    lldbSymbol
    lldbPluginProcessUtility
  LINK_COMPONENTS
    MC
    Support
    ${mips_target}
  )
