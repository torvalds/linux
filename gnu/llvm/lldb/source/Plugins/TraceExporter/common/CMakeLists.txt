add_lldb_library(lldbPluginTraceExporterCommon
  TraceHTR.cpp

  LINK_LIBS
    lldbCore
    lldbTarget
  )
