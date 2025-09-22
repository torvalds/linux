add_lldb_library(lldbPluginProcessMinidump PLUGIN
  MinidumpTypes.cpp
  MinidumpParser.cpp
  RegisterContextMinidump_ARM.cpp
  RegisterContextMinidump_ARM64.cpp
  RegisterContextMinidump_x86_32.cpp
  RegisterContextMinidump_x86_64.cpp
  ProcessMinidump.cpp
  ThreadMinidump.cpp

  LINK_LIBS
    lldbCore
    lldbTarget
    lldbUtility
    lldbPluginProcessUtility
    lldbPluginProcessElfCore
    lldbPluginObjectFilePlaceholder
  LINK_COMPONENTS
    BinaryFormat
    Object
    Support
  )
