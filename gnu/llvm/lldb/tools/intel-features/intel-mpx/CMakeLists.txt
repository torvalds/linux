add_lldb_library(lldbIntelMPX
  cli-wrapper-mpxtable.cpp

  LINK_LIBS
    liblldb

  LINK_COMPONENTS
    Support
    TargetParser
  )
