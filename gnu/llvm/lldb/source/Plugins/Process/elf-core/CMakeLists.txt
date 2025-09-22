add_lldb_library(lldbPluginProcessElfCore PLUGIN
  ProcessElfCore.cpp
  ThreadElfCore.cpp
  RegisterContextLinuxCore_x86_64.cpp
  RegisterContextPOSIXCore_arm.cpp
  RegisterContextPOSIXCore_arm64.cpp
  RegisterContextPOSIXCore_mips64.cpp
  RegisterContextPOSIXCore_powerpc.cpp
  RegisterContextPOSIXCore_ppc64le.cpp
  RegisterContextPOSIXCore_s390x.cpp
  RegisterContextPOSIXCore_x86_64.cpp
  RegisterContextPOSIXCore_riscv64.cpp
  RegisterUtilities.cpp

  LINK_LIBS
    lldbCore
    lldbTarget
    lldbPluginDynamicLoaderPosixDYLD
    lldbPluginObjectFileELF
    lldbPluginProcessUtility
  LINK_COMPONENTS
    BinaryFormat
    Support
  )
