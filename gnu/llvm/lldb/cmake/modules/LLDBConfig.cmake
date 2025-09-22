include(CheckCXXSymbolExists)
include(CheckTypeSize)

set(LLDB_PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(LLDB_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/source")
set(LLDB_INCLUDE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/include")

set(LLDB_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(LLDB_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(LLDB_OBJ_DIR ${CMAKE_CURRENT_BINARY_DIR})

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR
    "In-source builds are not allowed. CMake would overwrite the makefiles "
    "distributed with LLDB. Please create a directory and run cmake from "
    "there, passing the path to this source directory as the last argument. "
    "This process created the file `CMakeCache.txt' and the directory "
    "`CMakeFiles'. Please delete them.")
endif()

macro(add_optional_dependency variable description package found)
  cmake_parse_arguments(ARG
    "QUIET"
    "VERSION"
    ""
    ${ARGN})

  set(${variable} "Auto" CACHE STRING "${description} On, Off or Auto (default)")
  string(TOUPPER "${${variable}}" ${variable})

  if("${${variable}}" STREQUAL "AUTO")
    set(find_package TRUE)
    set(maybe_required)
  elseif(${${variable}})
    set(find_package TRUE)
    set(maybe_required REQUIRED)
  else()
    set(find_package FALSE)
    set(${variable} FALSE)
  endif()

  if(${find_package})
    set(maybe_quiet)
    if(ARG_QUIET)
      set(maybe_quiet QUIET)
    endif()
    find_package(${package} ${ARG_VERSION} ${maybe_required} ${maybe_quiet})
    set(${variable} "${${found}}")
  endif()

  message(STATUS "${description}: ${${variable}}")
endmacro()

add_optional_dependency(LLDB_ENABLE_SWIG "Enable SWIG to generate LLDB bindings" SWIG SWIG_FOUND VERSION 4)
add_optional_dependency(LLDB_ENABLE_LIBEDIT "Enable editline support in LLDB" LibEdit LibEdit_FOUND)
add_optional_dependency(LLDB_ENABLE_CURSES "Enable curses support in LLDB" CursesAndPanel CURSESANDPANEL_FOUND)
add_optional_dependency(LLDB_ENABLE_LZMA "Enable LZMA compression support in LLDB" LibLZMA LIBLZMA_FOUND)
add_optional_dependency(LLDB_ENABLE_LUA "Enable Lua scripting support in LLDB" LuaAndSwig LUAANDSWIG_FOUND)
add_optional_dependency(LLDB_ENABLE_PYTHON "Enable Python scripting support in LLDB" PythonAndSwig PYTHONANDSWIG_FOUND)
add_optional_dependency(LLDB_ENABLE_LIBXML2 "Enable Libxml 2 support in LLDB" LibXml2 LIBXML2_FOUND VERSION 2.8)
add_optional_dependency(LLDB_ENABLE_FBSDVMCORE "Enable libfbsdvmcore support in LLDB" FBSDVMCore FBSDVMCore_FOUND QUIET)

option(LLDB_USE_ENTITLEMENTS "When codesigning, use entitlements if available" ON)
option(LLDB_BUILD_FRAMEWORK "Build LLDB.framework (Darwin only)" OFF)
option(LLDB_NO_INSTALL_DEFAULT_RPATH "Disable default RPATH settings in binaries" OFF)
option(LLDB_USE_SYSTEM_DEBUGSERVER "Use the system's debugserver for testing (Darwin only)." OFF)
option(LLDB_SKIP_STRIP "Whether to skip stripping of binaries when installing lldb." OFF)
option(LLDB_SKIP_DSYM "Whether to skip generating a dSYM when installing lldb." OFF)
option(LLDB_ENFORCE_STRICT_TEST_REQUIREMENTS
  "Fail to configure if certain requirements are not met for testing." OFF)

set(LLDB_GLOBAL_INIT_DIRECTORY "" CACHE STRING
  "Path to the global lldbinit directory. Relative paths are resolved relative to the
  directory containing the LLDB library.")

if (LLDB_USE_SYSTEM_DEBUGSERVER)
  # The custom target for the system debugserver has no install target, so we
  # need to remove it from the LLVM_DISTRIBUTION_COMPONENTS list.
  if (LLVM_DISTRIBUTION_COMPONENTS)
    list(REMOVE_ITEM LLVM_DISTRIBUTION_COMPONENTS debugserver)
    set(LLVM_DISTRIBUTION_COMPONENTS ${LLVM_DISTRIBUTION_COMPONENTS} CACHE STRING "" FORCE)
  endif()
endif()

if(LLDB_BUILD_FRAMEWORK)
  if(NOT APPLE)
    message(FATAL_ERROR "LLDB.framework can only be generated when targeting Apple platforms")
  endif()

  set(LLDB_FRAMEWORK_VERSION A CACHE STRING "LLDB.framework version (default is A)")
  set(LLDB_FRAMEWORK_BUILD_DIR bin CACHE STRING "Output directory for LLDB.framework")
  set(LLDB_FRAMEWORK_INSTALL_DIR Library/Frameworks CACHE STRING "Install directory for LLDB.framework")

  get_filename_component(LLDB_FRAMEWORK_ABSOLUTE_BUILD_DIR ${LLDB_FRAMEWORK_BUILD_DIR} ABSOLUTE
    BASE_DIR ${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR})

  # Essentially, emit the framework's dSYM outside of the framework directory.
  set(LLDB_DEBUGINFO_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR}/bin CACHE STRING
      "Directory to emit dSYM files stripped from executables and libraries (Darwin Only)")

  # Custom target to remove the targets (binaries, directories) that were
  # copied into LLDB.framework in the build tree.
  #
  # These targets need to be removed before the install phase because otherwise
  # because otherwise they may overwrite already installed binaries with the
  # wrong RPATH (i.e. build RPATH instead of install RPATH).
  #
  # This target needs to be created here (rather than in API/CMakeLists.txt)
  # because add_lldb_tool creates the custom rules to copy the binaries before
  # the framework target exists and that's the only place where this is
  # tracked.
  add_custom_target(lldb-framework-cleanup
    COMMENT "Cleaning up build-tree frameworks in preparation for install")
endif()

if(APPLE AND CMAKE_GENERATOR STREQUAL Xcode)
  if(NOT LLDB_EXPLICIT_XCODE_CACHE_USED)
    message(WARNING
      "When building with Xcode, we recommend using the corresponding cache script. "
      "If this was a mistake, clean your build directory and re-run CMake with:\n"
      "  -C ${CMAKE_SOURCE_DIR}/cmake/caches/Apple-lldb-Xcode.cmake\n"
      "See: https://lldb.llvm.org/resources/build.html#cmakegeneratedxcodeproject\n")
  endif()
endif()

set(LLDB_EXPORT_ALL_SYMBOLS 0 CACHE BOOL
  "Causes lldb to export some private symbols when building liblldb. See lldb/source/API/liblldb-private.exports for the full list of symbols that get exported.")

set(LLDB_EXPORT_ALL_SYMBOLS_EXPORTS_FILE "" CACHE PATH
  "When `LLDB_EXPORT_ALL_SYMBOLS` is enabled, this specifies the exports file to use when building liblldb.")

if (CMAKE_SYSTEM_NAME MATCHES "Windows")
  set(LLDB_EXPORT_ALL_SYMBOLS_PLUGINS "" CACHE STRING
    "When `LLDB_EXPORT_ALL_SYMBOLS` is enabled, this specifies the plugins whose symbols should be exported.")
endif()

if ((NOT MSVC) OR MSVC12)
  add_definitions( -DHAVE_ROUND )
endif()

# Check if we libedit capable of handling wide characters (built with
# '--enable-widec').
if (LLDB_ENABLE_LIBEDIT)
  set(CMAKE_REQUIRED_LIBRARIES ${LibEdit_LIBRARIES})
  set(CMAKE_REQUIRED_INCLUDES ${LibEdit_INCLUDE_DIRS})
  check_symbol_exists(el_winsertstr histedit.h LLDB_EDITLINE_USE_WCHAR)
  set(CMAKE_EXTRA_INCLUDE_FILES histedit.h)
  check_type_size(el_rfunc_t LLDB_EL_RFUNC_T_SIZE)
  if (LLDB_EL_RFUNC_T_SIZE STREQUAL "")
    set(LLDB_HAVE_EL_RFUNC_T 0)
  else()
    set(LLDB_HAVE_EL_RFUNC_T 1)
  endif()
  set(CMAKE_REQUIRED_LIBRARIES)
  set(CMAKE_REQUIRED_INCLUDES)
  set(CMAKE_EXTRA_INCLUDE_FILES)
endif()

if (LLDB_ENABLE_PYTHON)
  if(CMAKE_SYSTEM_NAME MATCHES "Windows")
    set(default_embed_python_home ON)
  else()
    set(default_embed_python_home OFF)
  endif()
  option(LLDB_EMBED_PYTHON_HOME
    "Embed PYTHONHOME in the binary. If set to OFF, PYTHONHOME environment variable will be used to to locate Python."
    ${default_embed_python_home})

  include_directories(${Python3_INCLUDE_DIRS})
  if (LLDB_EMBED_PYTHON_HOME)
    get_filename_component(PYTHON_HOME "${Python3_EXECUTABLE}" DIRECTORY)
    set(LLDB_PYTHON_HOME "${PYTHON_HOME}" CACHE STRING
      "Path to use as PYTHONHOME in lldb. If a relative path is specified, it will be resolved at runtime relative to liblldb directory.")
  endif()
endif()

if (LLVM_EXTERNAL_CLANG_SOURCE_DIR)
  include_directories(${LLVM_EXTERNAL_CLANG_SOURCE_DIR}/include)
else ()
  include_directories(${CMAKE_SOURCE_DIR}/tools/clang/include)
endif ()
include_directories("${CMAKE_CURRENT_BINARY_DIR}/../clang/include")

# GCC silently accepts any -Wno-<foo> option, but warns about those options
# being unrecognized only if the compilation triggers other warnings to be
# printed. Therefore, check for whether the compiler supports options in the
# form -W<foo>, and if supported, add the corresponding -Wno-<foo> option.

if (LLVM_COMPILER_IS_GCC_COMPATIBLE)
  # Disable GCC warnings
  append("-Wno-deprecated-declarations" CMAKE_CXX_FLAGS)
  append("-Wno-unknown-pragmas" CMAKE_CXX_FLAGS)
  append("-Wno-strict-aliasing" CMAKE_CXX_FLAGS)

  check_cxx_compiler_flag("-Wstringop-truncation" CXX_SUPPORTS_STRINGOP_TRUNCATION)
  append_if(CXX_SUPPORTS_STRINGOP_TRUNCATION "-Wno-stringop-truncation" CMAKE_CXX_FLAGS)
endif()

# Disable Clang warnings
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  append("-Wno-deprecated-register" CMAKE_CXX_FLAGS)
  append("-Wno-vla-extension" CMAKE_CXX_FLAGS)
endif()

# Disable MSVC warnings
if( MSVC )
  add_definitions(
    -wd4018 # Suppress 'warning C4018: '>=' : signed/unsigned mismatch'
    -wd4068 # Suppress 'warning C4068: unknown pragma'
    -wd4150 # Suppress 'warning C4150: deletion of pointer to incomplete type'
    -wd4201 # Suppress 'warning C4201: nonstandard extension used: nameless struct/union'
    -wd4251 # Suppress 'warning C4251: T must have dll-interface to be used by clients of class U.'
    -wd4521 # Suppress 'warning C4521: 'type' : multiple copy constructors specified'
    -wd4530 # Suppress 'warning C4530: C++ exception handler used, but unwind semantics are not enabled.'
    -wd4589 # Suppress 'warning C4589: Constructor of abstract class 'lldb_private::NativeRegisterContextDBReg_x86' ignores initializer for virtual base class 'lldb_private::NativeRegisterContextRegisterInfo''
  )
endif()

# Use the Unicode (UTF-16) APIs by default on Win32
if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    add_definitions( -D_UNICODE -DUNICODE )
endif()

# If LLDB_VERSION_* is specified, use it, if not use LLVM_VERSION_*.
if(NOT DEFINED LLDB_VERSION_MAJOR)
  set(LLDB_VERSION_MAJOR ${LLVM_VERSION_MAJOR})
endif()
if(NOT DEFINED LLDB_VERSION_MINOR)
  set(LLDB_VERSION_MINOR ${LLVM_VERSION_MINOR})
endif()
if(NOT DEFINED LLDB_VERSION_PATCH)
  set(LLDB_VERSION_PATCH ${LLVM_VERSION_PATCH})
endif()
if(NOT DEFINED LLDB_VERSION_SUFFIX)
  set(LLDB_VERSION_SUFFIX ${LLVM_VERSION_SUFFIX})
endif()
set(LLDB_VERSION "${LLDB_VERSION_MAJOR}.${LLDB_VERSION_MINOR}.${LLDB_VERSION_PATCH}${LLDB_VERSION_SUFFIX}")
message(STATUS "LLDB version: ${LLDB_VERSION}")

if (LLDB_ENABLE_LZMA)
  include_directories(${LIBLZMA_INCLUDE_DIRS})
endif()

if (LLDB_ENABLE_LIBXML2)
  include_directories(${LIBXML2_INCLUDE_DIR})
endif()

include_directories(BEFORE
  ${CMAKE_CURRENT_BINARY_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  )

if (NOT LLVM_INSTALL_TOOLCHAIN_ONLY)
  install(DIRECTORY include/
    COMPONENT lldb-headers
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING
    PATTERN "*.h"
    PATTERN ".cmake" EXCLUDE
    )

  install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include/
    COMPONENT lldb-headers
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING
    PATTERN "*.h"
    PATTERN ".cmake" EXCLUDE
    )

  add_custom_target(lldb-headers)
  set_target_properties(lldb-headers PROPERTIES FOLDER "LLDB/Resources")

  if (NOT CMAKE_CONFIGURATION_TYPES)
    add_llvm_install_targets(install-lldb-headers
                             COMPONENT lldb-headers)
  endif()
endif()

# Find Apple-specific libraries or frameworks that may be needed.
if (APPLE)
  if(NOT APPLE_EMBEDDED)
    find_library(CARBON_LIBRARY Carbon)
    find_library(CORE_SERVICES_LIBRARY CoreServices)
  endif()
  find_library(FOUNDATION_LIBRARY Foundation)
  find_library(CORE_FOUNDATION_LIBRARY CoreFoundation)
  find_library(SECURITY_LIBRARY Security)
  include_directories(${LIBXML2_INCLUDE_DIR})
endif()

if( WIN32 AND NOT CYGWIN )
  set(PURE_WINDOWS 1)
endif()

if(NOT PURE_WINDOWS)
  set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
  find_package(Threads REQUIRED)
endif()

# Figure out if lldb could use lldb-server.  If so, then we'll
# ensure we build lldb-server when an lldb target is being built.
if (CMAKE_SYSTEM_NAME MATCHES "Android|Darwin|FreeBSD|Linux|NetBSD|Windows")
  set(LLDB_CAN_USE_LLDB_SERVER ON)
else()
  set(LLDB_CAN_USE_LLDB_SERVER OFF)
endif()

# Figure out if lldb could use debugserver.  If so, then we'll
# ensure we build debugserver when we build lldb.
if (CMAKE_SYSTEM_NAME MATCHES "Darwin")
    set(LLDB_CAN_USE_DEBUGSERVER ON)
else()
    set(LLDB_CAN_USE_DEBUGSERVER OFF)
endif()

if ((CMAKE_SYSTEM_NAME MATCHES "Android") AND LLVM_BUILD_STATIC AND
    ((ANDROID_ABI MATCHES "armeabi") OR (ANDROID_ABI MATCHES "mips")))
  add_definitions(-DANDROID_USE_ACCEPT_WORKAROUND)
endif()

include(LLDBGenerateConfig)
