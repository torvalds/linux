if (NOT LIBIPT_INCLUDE_PATH)
  message (FATAL_ERROR "libipt include path not provided")
endif()

if (NOT EXISTS "${LIBIPT_INCLUDE_PATH}")
  message (FATAL_ERROR "invalid libipt include path provided")
endif()
include_directories(${LIBIPT_INCLUDE_PATH})

find_library(LIBIPT_LIBRARY ipt PATHS ${LIBIPT_LIBRARY_PATH} REQUIRED)

lldb_tablegen(TraceIntelPTCommandOptions.inc -gen-lldb-option-defs
  SOURCE TraceIntelPTOptions.td
  TARGET TraceIntelPTOptionsGen)

lldb_tablegen(TraceIntelPTProperties.inc -gen-lldb-property-defs
  SOURCE TraceIntelPTProperties.td
  TARGET TraceIntelPTPropertiesGen)

lldb_tablegen(TraceIntelPTPropertiesEnum.inc -gen-lldb-property-enum-defs
  SOURCE TraceIntelPTProperties.td
  TARGET TraceIntelPTPropertiesEnumGen)

add_lldb_library(lldbPluginTraceIntelPT PLUGIN
  CommandObjectTraceStartIntelPT.cpp
  DecodedThread.cpp
  TaskTimer.cpp
  LibiptDecoder.cpp
  PerfContextSwitchDecoder.cpp
  ThreadDecoder.cpp
  TraceCursorIntelPT.cpp
  TraceIntelPT.cpp
  TraceIntelPTJSONStructs.cpp
  TraceIntelPTMultiCpuDecoder.cpp
  TraceIntelPTBundleLoader.cpp
  TraceIntelPTBundleSaver.cpp

  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbTarget
    lldbPluginTraceCommon
    ${LIBIPT_LIBRARY}
  LINK_COMPONENTS
    Support
  )


add_dependencies(lldbPluginTraceIntelPT
  TraceIntelPTOptionsGen
  TraceIntelPTPropertiesGen
  TraceIntelPTPropertiesEnumGen)
