add_lldb_library(lldbPluginProcessOpenBSDKernel PLUGIN
  ProcessOpenBSDKernel.cpp
  RegisterContextOpenBSDKernel_arm64.cpp
  RegisterContextOpenBSDKernel_i386.cpp
  RegisterContextOpenBSDKernel_x86_64.cpp
  ThreadOpenBSDKernel.cpp

  LINK_LIBS
    lldbCore
    lldbTarget
    kvm
  LINK_COMPONENTS
    Support
  )
