add_lldb_library(lldbPluginObjCRuntime
  ObjCLanguageRuntime.cpp

  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbTarget
    lldbUtility
)

add_subdirectory(AppleObjCRuntime)
add_subdirectory(GNUstepObjCRuntime)
