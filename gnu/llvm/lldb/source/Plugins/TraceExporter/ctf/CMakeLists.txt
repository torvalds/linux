lldb_tablegen(TraceExporterCTFCommandOptions.inc -gen-lldb-option-defs
  SOURCE TraceExporterCTFOptions.td
  TARGET TraceExporterCTFOptionsGen)

add_lldb_library(lldbPluginTraceExporterCTF PLUGIN
CommandObjectThreadTraceExportCTF.cpp
  TraceExporterCTF.cpp

  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbTarget
    lldbPluginTraceExporterCommon
  LINK_COMPONENTS
    Support
  )

add_dependencies(lldbPluginTraceExporterCTF TraceExporterCTFOptionsGen)
