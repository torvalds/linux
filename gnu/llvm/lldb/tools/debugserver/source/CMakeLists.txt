include(CheckCXXCompilerFlag)
include(CheckLibraryExists)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/..)
include_directories(${LLDB_SOURCE_DIR}/source)
include_directories(MacOSX)
include_directories(MacOSX/x86_64)
include_directories(MacOSX/arm64)

function(check_certificate identity result_valid)
  execute_process(
    COMMAND security find-certificate -Z -p -c ${identity} /Library/Keychains/System.keychain
    RESULT_VARIABLE exit_code OUTPUT_QUIET ERROR_QUIET)
  if(exit_code)
    set(${result_valid} FALSE PARENT_SCOPE)
  else()
    set(${result_valid} TRUE PARENT_SCOPE)
  endif()
endfunction()

function(get_debugserver_codesign_identity result)
  string(CONCAT not_found_help
    "This will cause failures in the test suite. "
    "Pass '-DLLDB_USE_SYSTEM_DEBUGSERVER=ON' to use the system one instead. "
    "See 'Code Signing on macOS' in the documentation."
  )

  # Explicit override: warn if unavailable
  if(LLDB_CODESIGN_IDENTITY)
    set(${result} ${LLDB_CODESIGN_IDENTITY} PARENT_SCOPE)
    check_certificate(${LLDB_CODESIGN_IDENTITY} available)
    if(NOT available)
      message(WARNING "LLDB_CODESIGN_IDENTITY not found: '${LLDB_CODESIGN_IDENTITY}' ${not_found_help}")
    endif()
    return()
  endif()

  # Development signing identity: use if available
  check_certificate(lldb_codesign available)
  if(available)
    set(${result} lldb_codesign PARENT_SCOPE)
    return()
  endif()

  message(WARNING "Development code sign identity not found: 'lldb_codesign' ${not_found_help}")

  # LLVM pendant: fallback if available
  if(LLVM_CODESIGNING_IDENTITY)
    check_certificate(${LLVM_CODESIGNING_IDENTITY} available)
    if(available)
      set(${result} ${LLVM_CODESIGNING_IDENTITY} PARENT_SCOPE)
      return()
    endif()
  endif()

  # Ad-hoc signing: last resort
  set(${result} "-" PARENT_SCOPE)
endfunction()

# debugserver does not depend on intrinsics_gen, or on llvm. Set the common
# llvm dependencies in the current scope to the empty set.
set(LLVM_COMMON_DEPENDS)

set(DEBUGSERVER_RESOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../resources")
set(DEBUGSERVER_INFO_PLIST "${DEBUGSERVER_RESOURCE_DIR}/lldb-debugserver-Info.plist")

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++ -Wl,-sectcreate,__TEXT,__info_plist,${DEBUGSERVER_INFO_PLIST}")

check_cxx_compiler_flag("-Wno-gnu-zero-variadic-macro-arguments"
                        CXX_SUPPORTS_NO_GNU_ZERO_VARIADIC_MACRO_ARGUMENTS)
if (CXX_SUPPORTS_NO_GNU_ZERO_VARIADIC_MACRO_ARGUMENTS)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-gnu-zero-variadic-macro-arguments")
endif ()

check_cxx_compiler_flag("-Wno-zero-length-array"
                        CXX_SUPPORTS_NO_ZERO_LENGTH_ARRAY)
if (CXX_SUPPORTS_NO_ZERO_LENGTH_ARRAY)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-zero-length-array")
endif ()

check_cxx_compiler_flag("-Wno-extended-offsetof"
                        CXX_SUPPORTS_NO_EXTENDED_OFFSETOF)
if (CXX_SUPPORTS_NO_EXTENDED_OFFSETOF)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-extended-offsetof")
endif ()

include(CheckCSourceCompiles)
check_c_source_compiles(
    "
    #include <TargetConditionals.h>
    #if TARGET_CPU_ARM64
    #if TARGET_OS_OSX
    #warning Building for macOS
    #else
    #error Not building for macOS
    #endif
    #else
    #error Not building for ARM64
    #endif
    int main(void) { return 0; }
    "
    BUILDING_FOR_ARM64_OSX
)

# You can only debug arm64e processes using an arm64e debugserver.
option(LLDB_ENABLE_ARM64E_DEBUGSERVER "Build debugserver for arm64 and arm64e" OFF)
if (BUILDING_FOR_ARM64_OSX AND LLDB_ENABLE_ARM64E_DEBUGSERVER)
  set(CMAKE_OSX_ARCHITECTURES "arm64;arm64e")
endif ()

find_library(SECURITY_LIBRARY Security)

set(LLDB_CODESIGN_IDENTITY "" CACHE STRING
    "Identity override for debugserver; see 'Code Signing on macOS' in the documentation (Darwin only)")

get_debugserver_codesign_identity(debugserver_codesign_identity)

# Override locally, so the identity is used for targets created in this scope.
set(LLVM_CODESIGNING_IDENTITY ${debugserver_codesign_identity})

if(APPLE)
  set(LIBCOMPRESSION compression)
  if(APPLE_EMBEDDED)
    find_library(BACKBOARD_LIBRARY BackBoardServices
      PATHS ${CMAKE_OSX_SYSROOT}/System/Library/PrivateFrameworks)
    find_library(FRONTBOARD_LIBRARY FrontBoardServices
      PATHS ${CMAKE_OSX_SYSROOT}/System/Library/PrivateFrameworks)
    find_library(SPRINGBOARD_LIBRARY SpringBoardServices
      PATHS ${CMAKE_OSX_SYSROOT}/System/Library/PrivateFrameworks)
    find_library(MOBILESERVICES_LIBRARY MobileCoreServices
      PATHS ${CMAKE_OSX_SYSROOT}/System/Library/PrivateFrameworks)
    find_library(LOCKDOWN_LIBRARY lockdown)
    if (APPLE_EMBEDDED STREQUAL "watchos")
      find_library(CAROUSELSERVICES_LIBRARY CarouselServices
        PATHS ${CMAKE_OSX_SYSROOT}/System/Library/PrivateFrameworks)
    endif()

    if(NOT BACKBOARD_LIBRARY)
      set(SKIP_TEST_DEBUGSERVER ON CACHE BOOL "" FORCE)
    endif()
  endif()
endif()

if(LLDB_USE_ENTITLEMENTS)
  if(APPLE_EMBEDDED)
    set(entitlements ${DEBUGSERVER_RESOURCE_DIR}/debugserver-entitlements.plist)
  else()
    if (LLDB_USE_PRIVATE_ENTITLEMENTS)
      set(entitlements ${DEBUGSERVER_RESOURCE_DIR}/debugserver-macosx-private-entitlements.plist)
    else()
      set(entitlements ${DEBUGSERVER_RESOURCE_DIR}/debugserver-macosx-entitlements.plist)
    endif()
  endif()
endif()

add_definitions(-DLLDB_USE_OS_LOG)

if(${CMAKE_OSX_SYSROOT} MATCHES ".Internal.sdk$")
  message(STATUS "LLDB debugserver energy support is enabled")
  add_definitions(-DLLDB_ENERGY)
  set(ENERGY_LIBRARY -lpmenergy -lpmsample)
else()
  message(STATUS "LLDB debugserver energy support is disabled")
endif()

set(generated_mach_interfaces
  ${CMAKE_CURRENT_BINARY_DIR}/mach_exc.h
  ${CMAKE_CURRENT_BINARY_DIR}/mach_excServer.c
  ${CMAKE_CURRENT_BINARY_DIR}/mach_excUser.c
  )

set(MIG_ARCH_FLAGS "")
if (DEFINED MIG_ARCHS)
  foreach(ARCH ${MIG_ARCHS})
    set(MIG_ARCH_FLAGS "${MIG_ARCH_FLAGS} -arch ${ARCH}")
  endforeach()
endif()
separate_arguments(MIG_ARCH_FLAGS_SEPARTED NATIVE_COMMAND "${MIG_ARCH_FLAGS}")

add_custom_command(OUTPUT ${generated_mach_interfaces}
  VERBATIM COMMAND mig ${MIG_ARCH_FLAGS_SEPARTED} -isysroot ${CMAKE_OSX_SYSROOT} ${CMAKE_CURRENT_SOURCE_DIR}/MacOSX/dbgnub-mig.defs
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/MacOSX/dbgnub-mig.defs
  )

set(DEBUGSERVER_VERS_GENERATED_FILE ${CMAKE_CURRENT_BINARY_DIR}/debugserver_vers.c)
configure_file(debugserver_vers.c.in
               ${DEBUGSERVER_VERS_GENERATED_FILE} @ONLY)

set(lldbDebugserverCommonSources
  DNBArch.cpp
  DNBBreakpoint.cpp
  DNB.cpp
  DNBDataRef.cpp
  DNBError.cpp
  DNBLog.cpp
  DNBRegisterInfo.cpp
  DNBThreadResumeActions.cpp
  JSON.cpp
  StdStringExtractor.cpp
  StringConvert.cpp
  # JSON reader depends on the following LLDB-common files
  ${LLDB_SOURCE_DIR}/source/Host/common/SocketAddress.cpp
  # end JSON reader dependencies
  libdebugserver.cpp
  PseudoTerminal.cpp
  PThreadEvent.cpp
  PThreadMutex.cpp
  RNBContext.cpp
  RNBRemote.cpp
  RNBServices.cpp
  RNBSocket.cpp
  SysSignal.cpp
  TTYState.cpp

  MacOSX/CFBundle.cpp
  MacOSX/CFString.cpp
  MacOSX/Genealogy.cpp
  MacOSX/MachException.cpp
  MacOSX/MachProcess.mm
  MacOSX/MachTask.mm
  MacOSX/MachThread.cpp
  MacOSX/MachThreadList.cpp
  MacOSX/MachVMMemory.cpp
  MacOSX/MachVMRegion.cpp
  MacOSX/OsLogger.cpp
  MacOSX/arm64/DNBArchImplARM64.cpp
  MacOSX/x86_64/DNBArchImplX86_64.cpp
  ${generated_mach_interfaces}
  ${DEBUGSERVER_VERS_GENERATED_FILE})

# Tell LLVM not to complain about these source files.
set(LLVM_OPTIONAL_SOURCES
  ${lldbDebugserverCommonSources}
  debugserver.cpp)

add_lldb_library(lldbDebugserverCommon ${lldbDebugserverCommonSources})
set_target_properties(lldbDebugserverCommon PROPERTIES FOLDER "lldb libraries/debugserver")

target_link_libraries(lldbDebugserverCommon
                      INTERFACE ${COCOA_LIBRARY}
                      ${CORE_FOUNDATION_LIBRARY}
                      ${FOUNDATION_LIBRARY}
                      ${BACKBOARD_LIBRARY}
                      ${FRONTBOARD_LIBRARY}
                      ${SPRINGBOARD_LIBRARY}
                      ${MOBILESERVICES_LIBRARY}
                      ${LOCKDOWN_LIBRARY}
                      ${CAROUSELSERVICES_LIBRARY}
                      ${FOUNDATION_LIBRARY}
                      ${SECURITY_LIBRARY}
                      ${LIBCOMPRESSION}
                      ${ENERGY_LIBRARY})
add_lldb_tool(debugserver ADD_TO_FRAMEWORK
  debugserver.cpp
  LINK_LIBS lldbDebugserverCommon
  ENTITLEMENTS ${entitlements}
)

# Workaround for Xcode-specific code-signing behavior:
# The XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY option causes debugserver to be copied
# into the framework first and code-signed afterwards. Sign the copy manually.
if (debugserver_codesign_identity AND LLDB_BUILD_FRAMEWORK AND
    CMAKE_GENERATOR STREQUAL "Xcode")
  if(NOT CMAKE_CODESIGN_ALLOCATE)
    execute_process(
      COMMAND xcrun -f codesign_allocate
      OUTPUT_STRIP_TRAILING_WHITESPACE
      OUTPUT_VARIABLE CMAKE_CODESIGN_ALLOCATE
    )
  endif()
  if(entitlements)
    set(pass_entitlements --entitlements ${entitlements})
  endif()

  set(copy_location ${LLDB_FRAMEWORK_ABSOLUTE_BUILD_DIR}/LLDB.framework/Versions/${LLDB_FRAMEWORK_VERSION}/Resources/debugserver)

  add_custom_command(TARGET debugserver POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E
            env CODESIGN_ALLOCATE=${CMAKE_CODESIGN_ALLOCATE}
            xcrun codesign -f -s ${debugserver_codesign_identity}
            ${pass_entitlements} ${copy_location}
    COMMENT "Code-sign debugserver copy in the build-tree framework: ${copy_location}"
  )
endif()

set_target_properties(debugserver PROPERTIES FOLDER "lldb libraries/debugserver")

if(APPLE_EMBEDDED)
  set_property(TARGET lldbDebugserverCommon APPEND PROPERTY COMPILE_DEFINITIONS
    WITH_LOCKDOWN
    WITH_FBS
    WITH_BKS
    )
  if(CAROUSELSERVICES_LIBRARY)
    set_property(TARGET lldbDebugserverCommon APPEND PROPERTY COMPILE_DEFINITIONS
      WITH_CAROUSEL
      )
  endif()
  set_property(TARGET debugserver APPEND PROPERTY COMPILE_DEFINITIONS
    WITH_LOCKDOWN
    WITH_FBS
    WITH_BKS
    )
  set_property(TARGET lldbDebugserverCommon APPEND PROPERTY COMPILE_FLAGS
    -F${CMAKE_OSX_SYSROOT}/System/Library/PrivateFrameworks
    )

  add_lldb_library(lldbDebugserverCommon_NonUI ${lldbDebugserverCommonSources})
  target_link_libraries(lldbDebugserverCommon_NonUI
                      INTERFACE ${COCOA_LIBRARY}
                      ${CORE_FOUNDATION_LIBRARY}
                      ${FOUNDATION_LIBRARY}
                      ${SECURITY_LIBRARY}
                      ${LIBCOMPRESSION})

  add_lldb_tool(debugserver-nonui
    debugserver.cpp

    LINK_LIBS
      lldbDebugserverCommon_NonUI

    ENTITLEMENTS
      ${entitlements}
    )
endif()
