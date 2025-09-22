lldb_tablegen(JITLoaderGDBProperties.inc -gen-lldb-property-defs
  SOURCE JITLoaderGDBProperties.td
  TARGET LLDBPluginJITLoaderGDBPropertiesGen)

lldb_tablegen(JITLoaderGDBPropertiesEnum.inc -gen-lldb-property-enum-defs
  SOURCE JITLoaderGDBProperties.td
  TARGET LLDBPluginJITLoaderGDBPropertiesEnumGen)

add_lldb_library(lldbPluginJITLoaderGDB PLUGIN
  JITLoaderGDB.cpp

  LINK_LIBS
    lldbBreakpoint
    lldbCore
    lldbInterpreter
    lldbSymbol
    lldbTarget
    lldbUtility
    lldbPluginObjectFileMachO
  LINK_COMPONENTS
    Support
  )

add_dependencies(lldbPluginJITLoaderGDB
  LLDBPluginJITLoaderGDBPropertiesGen
  LLDBPluginJITLoaderGDBPropertiesEnumGen)
