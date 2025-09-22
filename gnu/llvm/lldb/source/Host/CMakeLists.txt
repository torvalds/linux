if (APPLE AND LLVM_ENABLE_LOCAL_SUBMODULE_VISIBILITY)
  # The arpa/inet.h header used in the files here is providing a miscompiled
  # htonl function on macOS < 14 when local submodule visibility is active.
  if (CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS 14.0)
    # Disabling modules in this directory.
    remove_module_flags()
  endif()
endif()

macro(add_host_subdirectory group)
  list(APPEND HOST_SOURCES ${ARGN})
  source_group(${group} FILES ${ARGN})
endmacro()

add_host_subdirectory(common
  common/Alarm.cpp
  common/FileAction.cpp
  common/FileCache.cpp
  common/File.cpp
  common/FileSystem.cpp
  common/GetOptInc.cpp
  common/Host.cpp
  common/HostInfoBase.cpp
  common/HostNativeThreadBase.cpp
  common/HostProcess.cpp
  common/HostThread.cpp
  common/LockFileBase.cpp
  common/LZMA.cpp
  common/MainLoopBase.cpp
  common/MonitoringProcessLauncher.cpp
  common/NativeProcessProtocol.cpp
  common/NativeRegisterContext.cpp
  common/NativeThreadProtocol.cpp
  common/NativeWatchpointList.cpp
  common/OptionParser.cpp
  common/PipeBase.cpp
  common/ProcessLaunchInfo.cpp
  common/ProcessRunLock.cpp
  common/PseudoTerminal.cpp
  common/StreamFile.cpp
  common/SocketAddress.cpp
  common/Socket.cpp
  common/TCPSocket.cpp
  common/Terminal.cpp
  common/ThreadLauncher.cpp
  common/UDPSocket.cpp
  common/XML.cpp
  common/ZipFileResolver.cpp
  )

if (LLDB_ENABLE_LIBEDIT)
  add_host_subdirectory(common
    common/Editline.cpp
    )
endif()

add_host_subdirectory(posix
  posix/ConnectionFileDescriptorPosix.cpp
  )

if (CMAKE_SYSTEM_NAME MATCHES "Windows")
  add_host_subdirectory(windows
    windows/ConnectionGenericFileWindows.cpp
    windows/FileSystem.cpp
    windows/Host.cpp
    windows/HostInfoWindows.cpp
    windows/HostProcessWindows.cpp
    windows/HostThreadWindows.cpp
    windows/LockFileWindows.cpp
    windows/MainLoopWindows.cpp
    windows/PipeWindows.cpp
    windows/ProcessLauncherWindows.cpp
    windows/ProcessRunLock.cpp
    )
else()
  add_host_subdirectory(posix
    posix/DomainSocket.cpp
    posix/FileSystemPosix.cpp
    posix/HostInfoPosix.cpp
    posix/HostProcessPosix.cpp
    posix/HostThreadPosix.cpp
    posix/LockFilePosix.cpp
    posix/MainLoopPosix.cpp
    posix/PipePosix.cpp
    posix/ProcessLauncherPosixFork.cpp
    )

  if (CMAKE_SYSTEM_NAME MATCHES "Darwin")
    add_subdirectory(macosx/objcxx)
    set(LLDBObjCLibs lldbHostMacOSXObjCXX)
    add_host_subdirectory(macosx
      macosx/cfcpp/CFCBundle.cpp
      macosx/cfcpp/CFCData.cpp
      macosx/cfcpp/CFCMutableArray.cpp
      macosx/cfcpp/CFCMutableDictionary.cpp
      macosx/cfcpp/CFCMutableSet.cpp
      macosx/cfcpp/CFCString.cpp
      )
    if(APPLE_EMBEDDED)
      set_property(SOURCE macosx/Host.mm APPEND PROPERTY
               COMPILE_DEFINITIONS "NO_XPC_SERVICES=1")
    endif()


  elseif (CMAKE_SYSTEM_NAME MATCHES "Linux|Android")
    add_host_subdirectory(linux
      linux/AbstractSocket.cpp
      linux/Host.cpp
      linux/HostInfoLinux.cpp
      linux/LibcGlue.cpp
      linux/Support.cpp
      )
    if (CMAKE_SYSTEM_NAME MATCHES "Android")
      add_host_subdirectory(android
        android/HostInfoAndroid.cpp
        android/LibcGlue.cpp
        )
    endif()
  elseif (CMAKE_SYSTEM_NAME MATCHES "FreeBSD")
    add_host_subdirectory(freebsd
      freebsd/Host.cpp
      freebsd/HostInfoFreeBSD.cpp
      )

  elseif (CMAKE_SYSTEM_NAME MATCHES "NetBSD")
    add_host_subdirectory(netbsd
      netbsd/HostNetBSD.cpp
      netbsd/HostInfoNetBSD.cpp
      )

  elseif (CMAKE_SYSTEM_NAME MATCHES "OpenBSD")
    add_host_subdirectory(openbsd
      openbsd/Host.cpp
      openbsd/HostInfoOpenBSD.cpp
      )
  endif()
endif()

set(EXTRA_LIBS)
if (CMAKE_SYSTEM_NAME MATCHES "NetBSD")
  list(APPEND EXTRA_LIBS kvm)
endif()
if (LLDB_ENABLE_LIBXML2)
  list(APPEND EXTRA_LIBS LibXml2::LibXml2)
endif()
if (HAVE_LIBDL)
  list(APPEND EXTRA_LIBS ${CMAKE_DL_LIBS})
endif()
if (LLDB_ENABLE_LIBEDIT)
  list(APPEND EXTRA_LIBS LibEdit::LibEdit)
endif()
if (LLDB_ENABLE_LZMA)
  list(APPEND EXTRA_LIBS ${LIBLZMA_LIBRARIES})
endif()
if (WIN32)
  list(APPEND LLDB_SYSTEM_LIBS psapi)
endif()

if (LLDB_ENABLE_LIBEDIT)
  list(APPEND LLDB_LIBEDIT_LIBS LibEdit::LibEdit)
  if (LLVM_BUILD_STATIC)
    list(APPEND LLDB_SYSTEM_LIBS gpm)
  endif()
endif()

add_lldb_library(lldbHost NO_PLUGIN_DEPENDENCIES
  ${HOST_SOURCES}

  LINK_LIBS
    lldbUtility
    ${EXTRA_LIBS}
    ${LLDBObjCLibs}
    ${LLDB_LIBEDIT_LIBS}

  LINK_COMPONENTS
    Object
    Support
  )

