add_lldb_library(lldbPluginObjCPlusPlusLanguage PLUGIN
  ObjCPlusPlusLanguage.cpp

  LINK_LIBS
    lldbCore
    lldbTarget
    lldbPluginClangCommon
)
