if(Mips IN_LIST LLVM_TARGETS_TO_BUILD)
  set(mips_target Mips)
endif()

add_lldb_library(lldbPluginInstructionMIPS PLUGIN
  EmulateInstructionMIPS.cpp

  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbTarget
    lldbPluginProcessUtility
  LINK_COMPONENTS
    MC
    Support
    ${mips_target}
  )
