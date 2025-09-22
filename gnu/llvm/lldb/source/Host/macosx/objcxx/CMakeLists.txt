
remove_module_flags()
include_directories(..)

add_lldb_library(lldbHostMacOSXObjCXX NO_PLUGIN_DEPENDENCIES
  Host.mm
  HostInfoMacOSX.mm
  HostThreadMacOSX.mm

  LINK_LIBS
    lldbUtility
    ${EXTRA_LIBS}

  LINK_COMPONENTS
    Support
    TargetParser
  )

target_compile_options(lldbHostMacOSXObjCXX PRIVATE -fno-objc-exceptions)
