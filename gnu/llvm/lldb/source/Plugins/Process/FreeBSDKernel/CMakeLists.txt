set(FBSDKERNEL_LIBS)
if(FBSDVMCore_FOUND)
  list(APPEND FBSDKERNEL_LIBS fbsdvmcore)
endif()
if(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
  list(APPEND FBSDKERNEL_LIBS kvm)
endif()

if (NOT FBSDKERNEL_LIBS)
  message(STATUS "Skipping FreeBSDKernel plugin due to missing libfbsdvmcore")
  return()
endif()

add_lldb_library(lldbPluginProcessFreeBSDKernel PLUGIN
  ProcessFreeBSDKernel.cpp
  RegisterContextFreeBSDKernel_arm64.cpp
  RegisterContextFreeBSDKernel_i386.cpp
  RegisterContextFreeBSDKernel_x86_64.cpp
  ThreadFreeBSDKernel.cpp

  LINK_LIBS
    lldbCore
    lldbTarget
    ${FBSDKERNEL_LIBS}
  LINK_COMPONENTS
    Support
  )
