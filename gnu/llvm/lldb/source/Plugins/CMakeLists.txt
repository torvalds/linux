add_subdirectory(ABI)
add_subdirectory(Architecture)
add_subdirectory(Disassembler)
add_subdirectory(DynamicLoader)
add_subdirectory(ExpressionParser)
add_subdirectory(Instruction)
add_subdirectory(InstrumentationRuntime)
add_subdirectory(JITLoader)
add_subdirectory(Language)
add_subdirectory(LanguageRuntime)
add_subdirectory(MemoryHistory)
add_subdirectory(ObjectContainer)
add_subdirectory(ObjectFile)
add_subdirectory(OperatingSystem)
add_subdirectory(Platform)
add_subdirectory(Process)
add_subdirectory(REPL)
add_subdirectory(RegisterTypeBuilder)
add_subdirectory(ScriptInterpreter)
add_subdirectory(StructuredData)
add_subdirectory(SymbolFile)
add_subdirectory(SystemRuntime)
add_subdirectory(SymbolLocator)
add_subdirectory(SymbolVendor)
add_subdirectory(Trace)
add_subdirectory(TraceExporter)
add_subdirectory(TypeSystem)
add_subdirectory(UnwindAssembly)

set(LLDB_STRIPPED_PLUGINS)
get_property(LLDB_ALL_PLUGINS GLOBAL PROPERTY LLDB_PLUGINS)

set(LLDB_ENUM_PLUGINS "")

# FIXME: ProcessWindowsCommon needs to be initialized after all other process
# plugins but before ProcessGDBRemote.
set(LLDB_PROCESS_WINDOWS_PLUGIN "")
set(LLDB_PROCESS_GDB_PLUGIN "")

foreach(p ${LLDB_ALL_PLUGINS})
  # Strip lldbPlugin from the plugin name.
  string(SUBSTRING ${p} 10 -1 pStripped)
  if(${pStripped} MATCHES "^ScriptInterpreter*")
    set(LLDB_ENUM_PLUGINS "${LLDB_ENUM_PLUGINS}LLDB_SCRIPT_PLUGIN(${pStripped})\n")
  elseif(${pStripped} STREQUAL "ProcessWindowsCommon")
    set(LLDB_PROCESS_WINDOWS_PLUGIN "LLDB_PLUGIN(${pStripped})\n")
  elseif(${pStripped} STREQUAL "ProcessGDBRemote")
    set(LLDB_PROCESS_GDB_PLUGIN "LLDB_PLUGIN(${pStripped})\n")
  else()
    set(LLDB_ENUM_PLUGINS "${LLDB_ENUM_PLUGINS}LLDB_PLUGIN(${pStripped})\n")
  endif()
endforeach(p)

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/Plugins.def.in
  ${CMAKE_CURRENT_BINARY_DIR}/Plugins.def
  )

set_property(GLOBAL PROPERTY LLDB_PLUGINS_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR})
