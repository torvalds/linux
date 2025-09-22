# See www/CMake.html for instructions on how to build libcxxabi with CMake.

#===============================================================================
# Setup Project
#===============================================================================

cmake_minimum_required(VERSION 3.20.0)
set(LLVM_SUBPROJECT_TITLE "libc++abi")

set(LLVM_COMMON_CMAKE_UTILS "${CMAKE_CURRENT_SOURCE_DIR}/../cmake")

# Add path for custom modules
list(INSERT CMAKE_MODULE_PATH 0
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules"
  "${CMAKE_CURRENT_SOURCE_DIR}/../runtimes/cmake/Modules"
  "${LLVM_COMMON_CMAKE_UTILS}"
  "${LLVM_COMMON_CMAKE_UTILS}/Modules"
  )

set(CMAKE_FOLDER "libc++")

set(LIBCXXABI_SOURCE_DIR  ${CMAKE_CURRENT_SOURCE_DIR})
set(LIBCXXABI_BINARY_DIR  ${CMAKE_CURRENT_BINARY_DIR})
set(LIBCXXABI_LIBCXX_PATH "${CMAKE_CURRENT_LIST_DIR}/../libcxx" CACHE PATH
        "Specify path to libc++ source.")

include(GNUInstallDirs)

# Require out of source build.
include(MacroEnsureOutOfSourceBuild)
MACRO_ENSURE_OUT_OF_SOURCE_BUILD(
 "${PROJECT_NAME} requires an out of source build. Please create a separate
 build directory and run 'cmake /path/to/${PROJECT_NAME} [options]' there."
 )

#===============================================================================
# Setup CMake Options
#===============================================================================
include(CMakeDependentOption)
include(HandleCompilerRT)

# Define options.
option(LIBCXXABI_ENABLE_EXCEPTIONS
  "Provide support for exceptions in the runtime.
  When disabled, libc++abi does not support stack unwinding and other exceptions-related features." ON)
option(LIBCXXABI_ENABLE_ASSERTIONS "Enable assertions independent of build mode." ON)
option(LIBCXXABI_ENABLE_PEDANTIC "Compile with pedantic enabled." OFF)
option(LIBCXXABI_ENABLE_WERROR "Fail and stop if a warning is triggered." OFF)
option(LIBCXXABI_USE_LLVM_UNWINDER "Build and use the LLVM unwinder." ON)
if (LIBCXXABI_USE_LLVM_UNWINDER AND NOT "libunwind" IN_LIST LLVM_ENABLE_RUNTIMES)
  message(FATAL_ERROR "LIBCXXABI_USE_LLVM_UNWINDER is set to ON, but libunwind is not specified in LLVM_ENABLE_RUNTIMES.")
endif()
option(LIBCXXABI_ENABLE_STATIC_UNWINDER "Statically link the LLVM unwinder." OFF)
option(LIBCXXABI_USE_COMPILER_RT "Use compiler-rt instead of libgcc" OFF)
option(LIBCXXABI_ENABLE_THREADS "Build with threads enabled" ON)
option(LIBCXXABI_HAS_PTHREAD_API "Ignore auto-detection and force use of pthread API" OFF)
option(LIBCXXABI_HAS_WIN32_THREAD_API "Ignore auto-detection and force use of win32 thread API" OFF)
option(LIBCXXABI_HAS_EXTERNAL_THREAD_API
  "Build libc++abi with an externalized threading API.
  This option may only be set to ON when LIBCXXABI_ENABLE_THREADS=ON." OFF)
option(LIBCXXABI_ENABLE_FORGIVING_DYNAMIC_CAST
"Make dynamic_cast more forgiving when type_info's mistakenly have hidden \
visibility, and thus multiple type_infos can exist for a single type. \
When the dynamic_cast would normally fail, this option will cause the \
library to try comparing the type_info names to see if they are equal \
instead." OFF)

option(LIBCXXABI_ENABLE_NEW_DELETE_DEFINITIONS
  "Build libc++abi with definitions for operator new/delete. These are normally
   defined in libc++abi, but it is also possible to define them in libc++, in
   which case the definition in libc++abi should be turned off." ON)
option(LIBCXXABI_BUILD_32_BITS "Build 32 bit multilib libc++abi. This option is not supported anymore when building the runtimes. Please specify a full triple instead." ${LLVM_BUILD_32_BITS})
if (LIBCXXABI_BUILD_32_BITS)
  message(FATAL_ERROR "LIBCXXABI_BUILD_32_BITS is not supported anymore when building the runtimes, please specify a full triple instead.")
endif()

option(LIBCXXABI_INCLUDE_TESTS "Generate build targets for the libc++abi unit tests." ${LLVM_INCLUDE_TESTS})
set(LIBCXXABI_LIBDIR_SUFFIX "${LLVM_LIBDIR_SUFFIX}" CACHE STRING
    "Define suffix of library directory name (32/64)")
option(LIBCXXABI_INSTALL_HEADERS "Install the libc++abi headers." ON)
option(LIBCXXABI_INSTALL_LIBRARY "Install the libc++abi library." ON)

set(LIBCXXABI_SHARED_OUTPUT_NAME "c++abi" CACHE STRING "Output name for the shared libc++abi runtime library.")
set(LIBCXXABI_STATIC_OUTPUT_NAME "c++abi" CACHE STRING "Output name for the static libc++abi runtime library.")

set(LIBCXXABI_INSTALL_INCLUDE_DIR "${CMAKE_INSTALL_INCLUDEDIR}/c++/v1" CACHE STRING "Path to install the libc++abi headers at.")

if(LLVM_LIBRARY_OUTPUT_INTDIR)
  set(LIBCXXABI_GENERATED_INCLUDE_DIR "${LLVM_BINARY_DIR}/include/c++/v1")
else()
  set(LIBCXXABI_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/include/c++/v1")
endif()

set(LIBCXXABI_LIBCXX_LIBRARY_PATH "" CACHE PATH "The path to libc++ library.")
set(LIBCXXABI_LIBRARY_VERSION "1.0" CACHE STRING
"Version of libc++abi. This will be reflected in the name of the shared \
library produced. For example, -DLIBCXXABI_LIBRARY_VERSION=x.y will \
result in the library being named libc++abi.x.y.dylib, along with the \
usual symlinks pointing to that.")

# Default to building a shared library so that the default options still test
# the libc++abi that is being built. The problem with testing a static libc++abi
# is that libc++ will prefer a dynamic libc++abi from the system over a static
# libc++abi from the output directory.
option(LIBCXXABI_ENABLE_SHARED "Build libc++abi as a shared library." ON)
option(LIBCXXABI_ENABLE_STATIC "Build libc++abi as a static library." ON)

cmake_dependent_option(LIBCXXABI_INSTALL_STATIC_LIBRARY
  "Install the static libc++abi library." ON
  "LIBCXXABI_ENABLE_STATIC;LIBCXXABI_INSTALL_LIBRARY" OFF)
cmake_dependent_option(LIBCXXABI_INSTALL_SHARED_LIBRARY
  "Install the shared libc++abi library." ON
  "LIBCXXABI_ENABLE_SHARED;LIBCXXABI_INSTALL_LIBRARY" OFF)

cmake_dependent_option(LIBCXXABI_STATICALLY_LINK_UNWINDER_IN_STATIC_LIBRARY
  "Statically link the LLVM unwinder to static library" ON
  "LIBCXXABI_ENABLE_STATIC_UNWINDER" OFF)
cmake_dependent_option(LIBCXXABI_STATICALLY_LINK_UNWINDER_IN_SHARED_LIBRARY
  "Statically link the LLVM unwinder to shared library" ON
  "LIBCXXABI_ENABLE_STATIC_UNWINDER" OFF)

option(LIBCXXABI_BAREMETAL "Build libc++abi for baremetal targets." OFF)
# The default terminate handler attempts to demangle uncaught exceptions, which
# causes extra I/O and demangling code to be pulled in.
option(LIBCXXABI_SILENT_TERMINATE "Set this to make the terminate handler default to a silent alternative" OFF)
option(LIBCXXABI_NON_DEMANGLING_TERMINATE "Set this to make the terminate handler
avoid demangling" OFF)

if (NOT LIBCXXABI_ENABLE_SHARED AND NOT LIBCXXABI_ENABLE_STATIC)
  message(FATAL_ERROR "libc++abi must be built as either a shared or static library.")
endif()

# TODO: Remove this, which shouldn't be necessary since we know we're being built
#       side-by-side with libc++.
set(LIBCXXABI_LIBCXX_INCLUDES "" CACHE PATH
    "Specify path to libc++ includes.")

set(LIBCXXABI_HERMETIC_STATIC_LIBRARY_DEFAULT OFF)
if (WIN32)
  set(LIBCXXABI_HERMETIC_STATIC_LIBRARY_DEFAULT ON)
endif()
option(LIBCXXABI_HERMETIC_STATIC_LIBRARY
  "Do not export any symbols from the static library." ${LIBCXXABI_HERMETIC_STATIC_LIBRARY_DEFAULT})

if(MINGW)
  set(LIBCXXABI_DEFAULT_TEST_CONFIG "llvm-libc++abi-mingw.cfg.in")
elseif(WIN32) # clang-cl
  if (LIBCXXABI_ENABLE_SHARED)
    set(LIBCXXABI_DEFAULT_TEST_CONFIG "llvm-libc++abi-shared-clangcl.cfg.in")
  else()
    set(LIBCXXABI_DEFAULT_TEST_CONFIG "llvm-libc++abi-static-clangcl.cfg.in")
  endif()
else()
  if (LIBCXXABI_ENABLE_SHARED)
    set(LIBCXXABI_DEFAULT_TEST_CONFIG "llvm-libc++abi-shared.cfg.in")
  else()
    set(LIBCXXABI_DEFAULT_TEST_CONFIG "llvm-libc++abi-static.cfg.in")
  endif()
endif()
set(LIBCXXABI_TEST_CONFIG "${LIBCXXABI_DEFAULT_TEST_CONFIG}" CACHE STRING
  "The path to the Lit testing configuration to use when running the tests.
   If a relative path is provided, it is assumed to be relative to '<monorepo>/libcxxabi/test/configs'.")
if (NOT IS_ABSOLUTE "${LIBCXXABI_TEST_CONFIG}")
  set(LIBCXXABI_TEST_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/test/configs/${LIBCXXABI_TEST_CONFIG}")
endif()
message(STATUS "Using libc++abi testing configuration: ${LIBCXXABI_TEST_CONFIG}")
set(LIBCXXABI_TEST_PARAMS "" CACHE STRING
    "A list of parameters to run the Lit test suite with.")

#===============================================================================
# Configure System
#===============================================================================

# Add path for custom modules
set(CMAKE_MODULE_PATH
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules"
  ${CMAKE_MODULE_PATH}
  )

set(LIBCXXABI_INSTALL_RUNTIME_DIR "${CMAKE_INSTALL_BINDIR}" CACHE STRING
    "Path where built libc++abi runtime libraries should be installed.")

if(LLVM_ENABLE_PER_TARGET_RUNTIME_DIR AND NOT APPLE)
  set(LIBCXXABI_TARGET_SUBDIR ${LLVM_DEFAULT_TARGET_TRIPLE})
  if(LIBCXXABI_LIBDIR_SUBDIR)
    string(APPEND LIBCXXABI_TARGET_SUBDIR /${LIBCXXABI_LIBDIR_SUBDIR})
  endif()
  set(LIBCXXABI_HEADER_DIR ${LLVM_BINARY_DIR})
  set(LIBCXXABI_LIBRARY_DIR ${LLVM_LIBRARY_OUTPUT_INTDIR}/${LIBCXXABI_TARGET_SUBDIR})
  set(LIBCXXABI_INSTALL_LIBRARY_DIR lib${LLVM_LIBDIR_SUFFIX}/${LIBCXXABI_TARGET_SUBDIR} CACHE STRING
      "Path where built libc++abi libraries should be installed.")
  unset(LIBCXXABI_TARGET_SUBDIR)
else()
  if(LLVM_LIBRARY_OUTPUT_INTDIR)
    set(LIBCXXABI_HEADER_DIR ${LLVM_BINARY_DIR})
    set(LIBCXXABI_LIBRARY_DIR ${LLVM_LIBRARY_OUTPUT_INTDIR})
  else()
    set(LIBCXXABI_HEADER_DIR ${CMAKE_BINARY_DIR})
    set(LIBCXXABI_LIBRARY_DIR ${CMAKE_BINARY_DIR}/lib${LIBCXXABI_LIBDIR_SUFFIX})
  endif()
  set(LIBCXXABI_INSTALL_LIBRARY_DIR lib${LIBCXXABI_LIBDIR_SUFFIX} CACHE STRING
      "Path where built libc++abi libraries should be installed.")
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${LIBCXXABI_LIBRARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${LIBCXXABI_LIBRARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${LIBCXXABI_LIBRARY_DIR})

# By default, libcxx and libcxxabi share a library directory.
if (NOT LIBCXXABI_LIBCXX_LIBRARY_PATH)
  set(LIBCXXABI_LIBCXX_LIBRARY_PATH "${LIBCXXABI_LIBRARY_DIR}" CACHE PATH
      "The path to libc++ library." FORCE)
endif()

# Declare libc++abi configuration variables.
# They are intended for use as follows:
# LIBCXXABI_C_FLAGS: General flags for both the c++ compiler and linker.
# LIBCXXABI_CXX_FLAGS: General flags for both the c++ compiler and linker.
# LIBCXXABI_COMPILE_FLAGS: Compile only flags.
# LIBCXXABI_LINK_FLAGS: Linker only flags.
# LIBCXXABI_LIBRARIES: libraries libc++abi is linked to.

set(LIBCXXABI_C_FLAGS "")
set(LIBCXXABI_CXX_FLAGS "")
set(LIBCXXABI_COMPILE_FLAGS "")
set(LIBCXXABI_LINK_FLAGS "")
set(LIBCXXABI_LIBRARIES "")
set(LIBCXXABI_ADDITIONAL_COMPILE_FLAGS "" CACHE STRING
    "Additional Compile only flags which can be provided in cache")
set(LIBCXXABI_ADDITIONAL_LIBRARIES "" CACHE STRING
    "Additional libraries libc++abi is linked to which can be provided in cache")

# Include macros for adding and removing libc++abi flags.
include(HandleLibcxxabiFlags)

#===============================================================================
# Setup Compiler Flags
#===============================================================================

# Configure target flags
if (${CMAKE_SYSTEM_NAME} MATCHES "AIX")
  add_flags_if_supported("-mdefault-visibility-export-mapping=explicit")
  set(CMAKE_AIX_EXPORT_ALL_SYMBOLS OFF)
endif()
add_library_flags("${LIBCXXABI_ADDITIONAL_LIBRARIES}")

# Configure compiler. Must happen after setting the target flags.
include(config-ix)

if (CXX_SUPPORTS_NOSTDINCXX_FLAG)
  list(APPEND LIBCXXABI_COMPILE_FLAGS -nostdinc++)
  # cmake 3.14 and above remove system include paths that are explicitly
  # passed on the command line.  We build with -nostdinc++ and explicitly add
  # just the libcxx system include paths with -I on the command line.
  # Setting CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES effectively prevents cmake
  # from removing these.
  # See: https://gitlab.kitware.com/cmake/cmake/issues/19227
  set(CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES "")
  # Remove -stdlib flags to prevent them from causing an unused flag warning.
  string(REPLACE "--stdlib=libc++" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  string(REPLACE "--stdlib=libstdc++" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  string(REPLACE "-stdlib=libc++" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  string(REPLACE "-stdlib=libstdc++" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

# Let the library headers know they are currently being used to build the
# library.
add_definitions(-D_LIBCXXABI_BUILDING_LIBRARY)

# libcxxabi needs to, for various reasons, include the libcpp headers as if
# it is being built as part of libcxx.
add_definitions(-D_LIBCPP_BUILDING_LIBRARY)

# Get feature flags.
add_compile_flags_if_supported(-fstrict-aliasing)

# Exceptions
if (LIBCXXABI_ENABLE_EXCEPTIONS)
  # Catches C++ exceptions only and tells the compiler to assume that extern C
  # functions never throw a C++ exception.
  add_compile_flags_if_supported(-EHsc)
  # Do we really need to be run through the C compiler ?
  add_c_compile_flags_if_supported(-funwind-tables)
else()
  add_compile_flags_if_supported(-fno-exceptions)
  add_compile_flags_if_supported(-EHs-)
  add_compile_flags_if_supported(-EHa-)
endif()

# Assert
string(TOUPPER "${CMAKE_BUILD_TYPE}" uppercase_CMAKE_BUILD_TYPE)
if (LIBCXXABI_ENABLE_ASSERTIONS)
  # MSVC doesn't like _DEBUG on release builds. See PR 4379.
  if (NOT MSVC)
    list(APPEND LIBCXXABI_COMPILE_FLAGS -D_DEBUG)
  endif()
  # On Release builds cmake automatically defines NDEBUG, so we
  # explicitly undefine it:
  if (uppercase_CMAKE_BUILD_TYPE STREQUAL "RELEASE")
    list(APPEND LIBCXXABI_COMPILE_FLAGS -UNDEBUG)
  endif()
else()
  if (NOT uppercase_CMAKE_BUILD_TYPE STREQUAL "RELEASE")
    list(APPEND LIBCXXABI_COMPILE_FLAGS -DNDEBUG)
  endif()
endif()

# Threading
if (NOT LIBCXXABI_ENABLE_THREADS)
  if (LIBCXXABI_HAS_PTHREAD_API)
    message(FATAL_ERROR "LIBCXXABI_HAS_PTHREAD_API can only"
                        " be set to ON when LIBCXXABI_ENABLE_THREADS"
                        " is also set to ON.")
  endif()
  if (LIBCXXABI_HAS_WIN32_THREAD_API)
    message(FATAL_ERROR "LIBCXXABI_HAS_WIN32_THREAD_API can only"
                        " be set to ON when LIBCXXABI_ENABLE_THREADS"
                        " is also set to ON.")
  endif()
  if (LIBCXXABI_HAS_EXTERNAL_THREAD_API)
    message(FATAL_ERROR "LIBCXXABI_HAS_EXTERNAL_THREAD_API can only"
                        " be set to ON when LIBCXXABI_ENABLE_THREADS"
                        " is also set to ON.")
  endif()
  add_definitions(-D_LIBCXXABI_HAS_NO_THREADS)
endif()

if (LIBCXXABI_HAS_EXTERNAL_THREAD_API)
  if (LIBCXXABI_HAS_PTHREAD_API)
    message(FATAL_ERROR "The options LIBCXXABI_HAS_EXTERNAL_THREAD_API"
                        " and LIBCXXABI_HAS_PTHREAD_API cannot be both"
                        " set to ON at the same time.")
  endif()
  if (LIBCXXABI_HAS_WIN32_THREAD_API)
    message(FATAL_ERROR "The options LIBCXXABI_HAS_EXTERNAL_THREAD_API"
                        " and LIBCXXABI_HAS_WIN32_THREAD_API cannot be both"
                        " set to ON at the same time.")
  endif()
endif()

if (LIBCXXABI_HAS_PTHREAD_API)
  if (LIBCXXABI_HAS_WIN32_THREAD_API)
    message(FATAL_ERROR "The options LIBCXXABI_HAS_PTHREAD_API"
            "and LIBCXXABI_HAS_WIN32_THREAD_API cannot be both"
            "set to ON at the same time.")
  endif()
endif()

if (LLVM_ENABLE_MODULES)
  # Ignore that the rest of the modules flags are now unused.
  add_compile_flags_if_supported(-Wno-unused-command-line-argument)
  add_compile_flags(-fno-modules)
endif()

set(LIBCXXABI_HAS_UNDEFINED_SYMBOLS OFF)
if ((NOT LIBCXXABI_ENABLE_NEW_DELETE_DEFINITIONS) OR MINGW)
  set(LIBCXXABI_HAS_UNDEFINED_SYMBOLS ON)
endif()

if (LIBCXXABI_HAS_UNDEFINED_SYMBOLS)
  # Need to allow unresolved symbols if this is to work with shared library builds
  if (APPLE)
    list(APPEND LIBCXXABI_LINK_FLAGS "-undefined dynamic_lookup")
  else()
    # Relax this restriction from HandleLLVMOptions
    string(REPLACE "-Wl,-z,defs" "" CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")
  endif()
endif()

if (LIBCXXABI_HAS_PTHREAD_API)
  add_definitions(-D_LIBCPP_HAS_THREAD_API_PTHREAD)
endif()

if (LIBCXXABI_HAS_WIN32_THREAD_API)
  add_definitions(-D_LIBCPP_HAS_THREAD_API_WIN32)
endif()

if (LIBCXXABI_HAS_EXTERNAL_THREAD_API)
  add_definitions(-D_LIBCPP_HAS_THREAD_API_EXTERNAL)
endif()

if (MSVC)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

if (LIBCXXABI_SILENT_TERMINATE)
  add_definitions(-DLIBCXXABI_SILENT_TERMINATE)
endif()

if (LIBCXXABI_NON_DEMANGLING_TERMINATE)
  add_definitions(-DLIBCXXABI_NON_DEMANGLING_TERMINATE)
endif()

if (LIBCXXABI_BAREMETAL)
    add_definitions(-DLIBCXXABI_BAREMETAL)
endif()

if (C_SUPPORTS_COMMENT_LIB_PRAGMA)
  if (LIBCXXABI_HAS_PTHREAD_LIB)
    add_definitions(-D_LIBCXXABI_LINK_PTHREAD_LIB)
  endif()
endif()

string(REPLACE ";" " " LIBCXXABI_CXX_FLAGS "${LIBCXXABI_CXX_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${LIBCXXABI_CXX_FLAGS}")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${LIBCXXABI_C_FLAGS}")

# On AIX, avoid picking up VMX extensions(i.e. vec_malloc) which would change
# the default alignment of the allocators here.
if (UNIX AND ${CMAKE_SYSTEM_NAME} MATCHES "AIX")
  add_definitions("-D_XOPEN_SOURCE=700")
endif()

#===============================================================================
# Setup Source Code
#===============================================================================

set(LIBCXXABI_LIBUNWIND_INCLUDES "${LIBCXXABI_LIBUNWIND_INCLUDES}" CACHE PATH
    "Specify path to libunwind includes." FORCE)
set(LIBCXXABI_LIBUNWIND_PATH "${LIBCXXABI_LIBUNWIND_PATH}" CACHE PATH
    "Specify path to libunwind source." FORCE)

if (LIBCXXABI_USE_LLVM_UNWINDER OR LLVM_NATIVE_ARCH MATCHES ARM)
  find_path(LIBCXXABI_LIBUNWIND_INCLUDES_INTERNAL libunwind.h
    PATHS ${LIBCXXABI_LIBUNWIND_INCLUDES}
          ${LIBCXXABI_LIBUNWIND_PATH}/include
          ${CMAKE_BINARY_DIR}/${LIBCXXABI_LIBUNWIND_INCLUDES}
          ${LLVM_MAIN_SRC_DIR}/projects/libunwind/include
          ${LLVM_MAIN_SRC_DIR}/runtimes/libunwind/include
          ${LLVM_MAIN_SRC_DIR}/../libunwind/include
    NO_DEFAULT_PATH
    NO_CMAKE_FIND_ROOT_PATH
  )

  if (LIBCXXABI_LIBUNWIND_INCLUDES_INTERNAL STREQUAL "LIBCXXABI_LIBUNWIND_INCLUDES_INTERNAL-NOTFOUND")
    set(LIBCXXABI_LIBUNWIND_INCLUDES_INTERNAL "")
  endif()
endif()

if (NOT "${LIBCXXABI_LIBUNWIND_INCLUDES_INTERNAL}" STREQUAL "")
  include_directories("${LIBCXXABI_LIBUNWIND_INCLUDES_INTERNAL}")
endif()

add_custom_target(cxxabi-test-depends
  COMMENT "Build dependencies required to run the libc++abi test suite.")

# Add source code. This also contains all of the logic for deciding linker flags
# soname, etc...
add_subdirectory(include)
add_subdirectory(src)

if (LIBCXXABI_INCLUDE_TESTS)
  add_subdirectory(test)
  add_subdirectory(fuzz)
endif()
