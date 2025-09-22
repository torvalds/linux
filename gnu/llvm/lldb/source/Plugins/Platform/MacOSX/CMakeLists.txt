lldb_tablegen(PlatformMacOSXProperties.inc -gen-lldb-property-defs
  SOURCE PlatformMacOSXProperties.td
  TARGET LLDBPluginPlatformMacOSXPropertiesGen)

lldb_tablegen(PlatformMacOSXPropertiesEnum.inc -gen-lldb-property-enum-defs
  SOURCE PlatformMacOSXProperties.td
  TARGET LLDBPluginPlatformMacOSXPropertiesEnumGen)

list(APPEND PLUGIN_PLATFORM_MACOSX_SOURCES
  PlatformDarwin.cpp
  PlatformDarwinDevice.cpp
  PlatformDarwinKernel.cpp
  PlatformMacOSX.cpp
  PlatformRemoteAppleBridge.cpp
  PlatformRemoteAppleTV.cpp
  PlatformRemoteAppleWatch.cpp
  PlatformRemoteAppleXR.cpp
  PlatformRemoteDarwinDevice.cpp
  PlatformRemoteMacOSX.cpp
  PlatformRemoteiOS.cpp
  )

list(APPEND PLUGIN_PLATFORM_MACOSX_DARWIN_ONLY_SOURCES
  PlatformAppleSimulator.cpp
  )

if(CMAKE_SYSTEM_NAME MATCHES "Darwin")
  add_subdirectory(objcxx)
  set(OBJC_LIBS "lldbPluginPlatformMacOSXObjCXX")
  list(APPEND PLUGIN_PLATFORM_MACOSX_SOURCES
    ${PLUGIN_PLATFORM_MACOSX_DARWIN_ONLY_SOURCES})
else()
  list(APPEND LLVM_OPTIONAL_SOURCES
    ${PLUGIN_PLATFORM_MACOSX_DARWIN_ONLY_SOURCES})
endif()

add_lldb_library(lldbPluginPlatformMacOSX PLUGIN
  ${PLUGIN_PLATFORM_MACOSX_SOURCES}

  LINK_LIBS
    lldbBreakpoint
    lldbCore
    lldbHost
    lldbInterpreter
    lldbSymbol
    lldbTarget
    lldbUtility
    lldbPluginDynamicLoaderDarwinKernel
    lldbPluginObjectContainerMachOFileset
    lldbPluginPlatformPOSIX
    ${OBJC_LIBS}
  CLANG_LIBS
    clangBasic
  LINK_COMPONENTS
    Support
    TargetParser
    )

add_dependencies(lldbPluginPlatformMacOSX
  LLDBPluginPlatformMacOSXPropertiesGen
  LLDBPluginPlatformMacOSXPropertiesEnumGen)
