# See docs/CMake.html for instructions about how to build LLVM with CMake.

cmake_minimum_required(VERSION 3.20.0)

set(LLVM_COMMON_CMAKE_UTILS ${CMAKE_CURRENT_SOURCE_DIR}/../cmake)
include(${LLVM_COMMON_CMAKE_UTILS}/Modules/CMakePolicy.cmake
  NO_POLICY_SCOPE)

# Builds with custom install names and installation rpath setups may not work
# in the build tree. Allow these cases to use CMake's default build tree
# behavior by setting `LLVM_NO_INSTALL_NAME_DIR_FOR_BUILD_TREE` to do this.
option(LLVM_NO_INSTALL_NAME_DIR_FOR_BUILD_TREE "If set, use CMake's default build tree install name directory logic (Darwin only)" OFF)
mark_as_advanced(LLVM_NO_INSTALL_NAME_DIR_FOR_BUILD_TREE)
if(NOT LLVM_NO_INSTALL_NAME_DIR_FOR_BUILD_TREE)
  set(CMAKE_BUILD_WITH_INSTALL_NAME_DIR ON)
endif()

include(${LLVM_COMMON_CMAKE_UTILS}/Modules/LLVMVersion.cmake)

set_directory_properties(PROPERTIES LLVM_VERSION_MAJOR "${LLVM_VERSION_MAJOR}")

if (NOT PACKAGE_VERSION)
  set(PACKAGE_VERSION
    "${LLVM_VERSION_MAJOR}.${LLVM_VERSION_MINOR}.${LLVM_VERSION_PATCH}${LLVM_VERSION_SUFFIX}")
endif()

if(NOT DEFINED LLVM_SHLIB_SYMBOL_VERSION)
  # "Symbol version prefix for libLLVM.so"
  set(LLVM_SHLIB_SYMBOL_VERSION "LLVM_${LLVM_VERSION_MAJOR}.${LLVM_VERSION_MINOR}")
endif()

if ((CMAKE_GENERATOR MATCHES "Visual Studio") AND (MSVC_TOOLSET_VERSION LESS 142) AND (CMAKE_GENERATOR_TOOLSET STREQUAL ""))
  message(WARNING "Visual Studio generators use the x86 host compiler by "
                  "default, even for 64-bit targets. This can result in linker "
                  "instability and out of memory errors. To use the 64-bit "
                  "host compiler, pass -Thost=x64 on the CMake command line.")
endif()

if (CMAKE_GENERATOR STREQUAL "Xcode" AND NOT CMAKE_OSX_ARCHITECTURES)
  # Some CMake features like object libraries get confused if you don't
  # explicitly specify an architecture setting with the Xcode generator.
  set(CMAKE_OSX_ARCHITECTURES "x86_64")
endif()

project(LLVM
  VERSION ${LLVM_VERSION_MAJOR}.${LLVM_VERSION_MINOR}.${LLVM_VERSION_PATCH}
  LANGUAGES C CXX ASM)

if (NOT DEFINED CMAKE_INSTALL_LIBDIR AND DEFINED LLVM_LIBDIR_SUFFIX)
  # Must go before `include(GNUInstallDirs)`.
  set(CMAKE_INSTALL_LIBDIR "lib${LLVM_LIBDIR_SUFFIX}")
endif()

# Must go after `DEFINED LLVM_LIBDIR_SUFFIX` check.
set(LLVM_LIBDIR_SUFFIX "" CACHE STRING "Define suffix of library directory name (32/64)" )

# Must go after `project(..)`.
include(GNUInstallDirs)

# This C++ standard is required to build LLVM.
set(LLVM_REQUIRED_CXX_STANDARD 17)

# If we find that the cache contains CMAKE_CXX_STANDARD it means that it's a old CMakeCache.txt
# and we can just inform the user and then reset it.
if($CACHE{CMAKE_CXX_STANDARD} AND $CACHE{CMAKE_CXX_STANDARD} LESS ${LLVM_REQUIRED_CXX_STANDARD})
  message(WARNING "Resetting cache value for CMAKE_CXX_STANDARD to ${LLVM_REQUIRED_CXX_STANDARD}")
  unset(CMAKE_CXX_STANDARD CACHE)
endif()

# if CMAKE_CXX_STANDARD is still set after the cache unset above it means that the user requested it
# and we allow it to be set to something newer than the required standard but otherwise we fail.
if(DEFINED CMAKE_CXX_STANDARD AND CMAKE_CXX_STANDARD LESS ${LLVM_REQUIRED_CXX_STANDARD})
  message(FATAL_ERROR "Requested CMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD} which is less than the required ${LLVM_REQUIRED_CXX_STANDARD}.")
endif()

set(CMAKE_CXX_STANDARD ${LLVM_REQUIRED_CXX_STANDARD} CACHE STRING "C++ standard to conform to")
set(CMAKE_CXX_STANDARD_REQUIRED YES)

if (CYGWIN)
  # Cygwin is a bit stricter and lack things like 'strdup', 'stricmp', etc in
  # c++xx mode.
  set(CMAKE_CXX_EXTENSIONS YES)
else()
  set(CMAKE_CXX_EXTENSIONS NO)
endif()

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(FATAL_ERROR "
No build type selected. You need to pass -DCMAKE_BUILD_TYPE=<type> in order to configure LLVM.
Available options are:
  * -DCMAKE_BUILD_TYPE=Release - For an optimized build with no assertions or debug info.
  * -DCMAKE_BUILD_TYPE=Debug - For an unoptimized build with assertions and debug info.
  * -DCMAKE_BUILD_TYPE=RelWithDebInfo - For an optimized build with no assertions but with debug info.
  * -DCMAKE_BUILD_TYPE=MinSizeRel - For a build optimized for size instead of speed.
Learn more about these options in our documentation at https://llvm.org/docs/CMake.html#cmake-build-type
")
endif()

# Set default build type for cmake's try_compile module.
# CMake 3.17 or newer sets CMAKE_DEFAULT_BUILD_TYPE to one of the
# items from CMAKE_CONFIGURATION_TYPES. Logic below can be further
# simplified once LLVM's minimum CMake version is updated to 3.17.
if(CMAKE_DEFAULT_BUILD_TYPE)
  set(CMAKE_TRY_COMPILE_CONFIGURATION ${CMAKE_DEFAULT_BUILD_TYPE})
else()
  if(CMAKE_CONFIGURATION_TYPES)
    list(GET CMAKE_CONFIGURATION_TYPES 0 CMAKE_TRY_COMPILE_CONFIGURATION)
  elseif(CMAKE_BUILD_TYPE)
    set(CMAKE_TRY_COMPILE_CONFIGURATION ${CMAKE_BUILD_TYPE})
  endif()
endif()

# Side-by-side subprojects layout: automatically set the
# LLVM_EXTERNAL_${project}_SOURCE_DIR using LLVM_ALL_PROJECTS
# This allows an easy way of setting up a build directory for llvm and another
# one for llvm+clang+... using the same sources.
set(LLVM_ALL_PROJECTS "bolt;clang;clang-tools-extra;compiler-rt;cross-project-tests;libc;libclc;lld;lldb;mlir;openmp;polly;pstl")
# The flang project is not yet part of "all" projects (see C++ requirements)
set(LLVM_EXTRA_PROJECTS "flang")
# List of all known projects in the mono repo
set(LLVM_KNOWN_PROJECTS "${LLVM_ALL_PROJECTS};${LLVM_EXTRA_PROJECTS}")
set(LLVM_ENABLE_PROJECTS "" CACHE STRING
    "Semicolon-separated list of projects to build (${LLVM_KNOWN_PROJECTS}), or \"all\".")
# Make sure expansion happens first to not handle "all" in rest of the checks.
if( LLVM_ENABLE_PROJECTS STREQUAL "all" )
  set( LLVM_ENABLE_PROJECTS ${LLVM_ALL_PROJECTS})
endif()
foreach(proj ${LLVM_ENABLE_PROJECTS})
  if (NOT proj STREQUAL "llvm" AND NOT "${proj}" IN_LIST LLVM_KNOWN_PROJECTS)
     MESSAGE(FATAL_ERROR "${proj} isn't a known project: ${LLVM_KNOWN_PROJECTS}. Did you mean to enable it as a runtime in LLVM_ENABLE_RUNTIMES?")
  endif()
endforeach()

if ("flang" IN_LIST LLVM_ENABLE_PROJECTS)
  if (NOT "mlir" IN_LIST LLVM_ENABLE_PROJECTS)
    message(STATUS "Enabling MLIR as a dependency to flang")
    list(APPEND LLVM_ENABLE_PROJECTS "mlir")
  endif()

  if (NOT "clang" IN_LIST LLVM_ENABLE_PROJECTS)
    message(FATAL_ERROR "Clang is not enabled, but is required for the Flang driver")
  endif()
endif()

# Select the runtimes to build
#
# As we migrate runtimes to using the bootstrapping build, the set of default runtimes
# should grow as we remove those runtimes from LLVM_ENABLE_PROJECTS above.
set(LLVM_DEFAULT_RUNTIMES "libcxx;libcxxabi;libunwind")
set(LLVM_SUPPORTED_RUNTIMES "libc;libunwind;libcxxabi;pstl;libcxx;compiler-rt;openmp;llvm-libgcc;offload")
set(LLVM_ENABLE_RUNTIMES "" CACHE STRING
  "Semicolon-separated list of runtimes to build, or \"all\" (${LLVM_DEFAULT_RUNTIMES}). Supported runtimes are ${LLVM_SUPPORTED_RUNTIMES}.")
if(LLVM_ENABLE_RUNTIMES STREQUAL "all")
  set(LLVM_ENABLE_RUNTIMES ${LLVM_DEFAULT_RUNTIMES})
endif()
foreach(proj IN LISTS LLVM_ENABLE_RUNTIMES)
  if (NOT "${proj}" IN_LIST LLVM_SUPPORTED_RUNTIMES)
    message(FATAL_ERROR "Runtime \"${proj}\" is not a supported runtime. Supported runtimes are: ${LLVM_SUPPORTED_RUNTIMES}")
  endif()
endforeach()

# Set a shorthand option to enable the GPU build of the 'libc' project.
option(LIBC_GPU_BUILD "Enable the 'libc' project targeting the GPU" OFF)
if(LIBC_GPU_BUILD)
  if(LLVM_RUNTIME_TARGETS)
    list(APPEND LLVM_RUNTIME_TARGETS "nvptx64-nvidia-cuda" "amdgcn-amd-amdhsa")
  else()
    set(LLVM_RUNTIME_TARGETS "default;nvptx64-nvidia-cuda;amdgcn-amd-amdhsa")
  endif()
  list(APPEND RUNTIMES_nvptx64-nvidia-cuda_LLVM_ENABLE_RUNTIMES "libc")
  list(APPEND RUNTIMES_amdgcn-amd-amdhsa_LLVM_ENABLE_RUNTIMES "libc")
endif()

set(NEED_LIBC_HDRGEN FALSE)
if("libc" IN_LIST LLVM_ENABLE_RUNTIMES)
  set(NEED_LIBC_HDRGEN TRUE)
endif()
foreach(_name ${LLVM_RUNTIME_TARGETS})
  if("libc" IN_LIST RUNTIMES_${_name}_LLVM_ENABLE_RUNTIMES)
    set(NEED_LIBC_HDRGEN TRUE)
    if("${_name}" STREQUAL "amdgcn-amd-amdhsa" OR "${_name}" STREQUAL "nvptx64-nvidia-cuda")
      set(LLVM_LIBC_GPU_BUILD ON)
    endif()
  endif()
endforeach()
if("${LIBC_TARGET_TRIPLE}" STREQUAL "amdgcn-amd-amdhsa" OR
   "${LIBC_TARGET_TRIPLE}" STREQUAL "nvptx64-nvidia-cuda")
  set(LLVM_LIBC_GPU_BUILD ON)
endif()
if(NEED_LIBC_HDRGEN)
  # To build the libc runtime, we need to be able to build few libc build
  # tools from the "libc" project. So, we add it to the list of enabled
  # projects.
  if (NOT "libc" IN_LIST LLVM_ENABLE_PROJECTS)
    message(STATUS "Enabling libc project to build libc build tools")
    list(APPEND LLVM_ENABLE_PROJECTS "libc")
  endif()
endif()
unset(NEED_LIBC_HDRGEN)

# LLVM_ENABLE_PROJECTS_USED is `ON` if the user has ever used the
# `LLVM_ENABLE_PROJECTS` CMake cache variable.  This exists for
# several reasons:
#
# * As an indicator that the `LLVM_ENABLE_PROJECTS` list is now the single
# source of truth for which projects to build. This means we will ignore user
# supplied `LLVM_TOOL_<project>_BUILD` CMake cache variables and overwrite
# them.
#
# * The case where the user previously had `LLVM_ENABLE_PROJECTS` set to a
# non-empty list but now the user wishes to disable building all other projects
# by setting `LLVM_ENABLE_PROJECTS` to an empty string. In that case we still
# need to set the `LLVM_TOOL_${upper_proj}_BUILD` variables so that we disable
# building all the projects that were previously enabled.
set(LLVM_ENABLE_PROJECTS_USED OFF CACHE BOOL "")
mark_as_advanced(LLVM_ENABLE_PROJECTS_USED)

if (LLVM_ENABLE_PROJECTS_USED OR NOT LLVM_ENABLE_PROJECTS STREQUAL "")
  set(LLVM_ENABLE_PROJECTS_USED ON CACHE BOOL "" FORCE)
  foreach(proj ${LLVM_KNOWN_PROJECTS} ${LLVM_EXTERNAL_PROJECTS})
    string(TOUPPER "${proj}" upper_proj)
    string(REGEX REPLACE "-" "_" upper_proj ${upper_proj})
    if ("${proj}" IN_LIST LLVM_ENABLE_PROJECTS)
      message(STATUS "${proj} project is enabled")
      set(SHOULD_ENABLE_PROJECT TRUE)
      set(PROJ_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../${proj}")
      if(NOT EXISTS "${PROJ_DIR}" OR NOT IS_DIRECTORY "${PROJ_DIR}")
        message(FATAL_ERROR "LLVM_ENABLE_PROJECTS requests ${proj} but directory not found: ${PROJ_DIR}")
      endif()
      if( LLVM_EXTERNAL_${upper_proj}_SOURCE_DIR STREQUAL "" )
        set(LLVM_EXTERNAL_${upper_proj}_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../${proj}" CACHE PATH "" FORCE)
      else()
        set(LLVM_EXTERNAL_${upper_proj}_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../${proj}" CACHE PATH "")
      endif()
    elseif ("${proj}" IN_LIST LLVM_EXTERNAL_PROJECTS)
      message(STATUS "${proj} project is enabled")
      set(SHOULD_ENABLE_PROJECT TRUE)
    else()
      message(STATUS "${proj} project is disabled")
      set(SHOULD_ENABLE_PROJECT FALSE)
    endif()
    # Force `LLVM_TOOL_${upper_proj}_BUILD` variables to have values that
    # corresponds with `LLVM_ENABLE_PROJECTS`. This prevents the user setting
    # `LLVM_TOOL_${upper_proj}_BUILD` variables externally. At some point
    # we should deprecate allowing users to set these variables by turning them
    # into normal CMake variables rather than cache variables.
    set(LLVM_TOOL_${upper_proj}_BUILD
      ${SHOULD_ENABLE_PROJECT}
      CACHE
      BOOL "Whether to build ${upper_proj} as part of LLVM" FORCE
    )
  endforeach()
endif()
unset(SHOULD_ENABLE_PROJECT)

# Build llvm with ccache if the package is present
set(LLVM_CCACHE_BUILD OFF CACHE BOOL "Set to ON for a ccache enabled build")
if(LLVM_CCACHE_BUILD)
  find_program(CCACHE_PROGRAM ccache)
  if(CCACHE_PROGRAM)
    set(LLVM_CCACHE_MAXSIZE "" CACHE STRING "Size of ccache")
    set(LLVM_CCACHE_DIR "" CACHE STRING "Directory to keep ccached data")
    set(LLVM_CCACHE_PARAMS "CCACHE_CPP2=yes CCACHE_HASHDIR=yes"
        CACHE STRING "Parameters to pass through to ccache")

    if(NOT CMAKE_SYSTEM_NAME MATCHES "Windows")
      set(CCACHE_PROGRAM "${LLVM_CCACHE_PARAMS} ${CCACHE_PROGRAM}")
      if (LLVM_CCACHE_MAXSIZE)
        set(CCACHE_PROGRAM "CCACHE_MAXSIZE=${LLVM_CCACHE_MAXSIZE} ${CCACHE_PROGRAM}")
      endif()
      if (LLVM_CCACHE_DIR)
        set(CCACHE_PROGRAM "CCACHE_DIR=${LLVM_CCACHE_DIR} ${CCACHE_PROGRAM}")
      endif()
      set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ${CCACHE_PROGRAM})
    else()
      if(LLVM_CCACHE_MAXSIZE OR LLVM_CCACHE_DIR OR
         NOT LLVM_CCACHE_PARAMS MATCHES "CCACHE_CPP2=yes CCACHE_HASHDIR=yes")
        message(FATAL_ERROR "Ccache configuration through CMake is not supported on Windows. Please use environment variables.")
      endif()
      # RULE_LAUNCH_COMPILE should work with Ninja but currently has issues
      # with cmd.exe and some MSVC tools other than cl.exe
      set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
      set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    endif()
  else()
    message(FATAL_ERROR "Unable to find the program ccache. Set LLVM_CCACHE_BUILD to OFF")
  endif()
endif()

set(LLVM_EXTERNAL_PROJECT_BUILD_TOOL_ARGS "" CACHE STRING
  "Optional arguments for the native tool used in CMake --build invocations for external projects.")
mark_as_advanced(LLVM_EXTERNAL_PROJECT_BUILD_TOOL_ARGS)

option(LLVM_DEPENDENCY_DEBUGGING "Dependency debugging mode to verify correctly expressed library dependencies (Darwin only)" OFF)

# Some features of the LLVM build may be disallowed when dependency debugging is
# enabled. In particular you cannot use ccache because we want to force compile
# operations to always happen.
if(LLVM_DEPENDENCY_DEBUGGING)
  if(NOT CMAKE_HOST_APPLE)
    message(FATAL_ERROR "Dependency debugging is only currently supported on Darwin hosts.")
  endif()
  if(LLVM_CCACHE_BUILD)
    message(FATAL_ERROR "Cannot enable dependency debugging while using ccache.")
  endif()
endif()

option(LLVM_ENABLE_DAGISEL_COV "Debug: Prints tablegen patterns that were used for selecting" OFF)
option(LLVM_ENABLE_GISEL_COV "Enable collection of GlobalISel rule coverage" OFF)
if(LLVM_ENABLE_GISEL_COV)
  set(LLVM_GISEL_COV_PREFIX "${CMAKE_BINARY_DIR}/gisel-coverage-" CACHE STRING "Provide a filename prefix to collect the GlobalISel rule coverage")
endif()

# Add path for custom modules
list(INSERT CMAKE_MODULE_PATH 0
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules"
  "${LLVM_COMMON_CMAKE_UTILS}/Modules"
  )

# Generate a CompilationDatabase (compile_commands.json file) for our build,
# for use by clang_complete, YouCompleteMe, etc.
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

option(LLVM_INSTALL_BINUTILS_SYMLINKS
  "Install symlinks from the binutils tool names to the corresponding LLVM tools." OFF)

option(LLVM_INSTALL_CCTOOLS_SYMLINKS
  "Install symlinks from the cctools tool names to the corresponding LLVM tools." OFF)

# By default we use symlinks on Unix platforms and copy binaries on Windows
# If you have the correct setup on Windows you can use this option to enable
# symlinks and save a lot of diskspace.
option(LLVM_USE_SYMLINKS "Use symlinks instead of copying binaries" ${CMAKE_HOST_UNIX})

option(LLVM_INSTALL_UTILS "Include utility binaries in the 'install' target." OFF)

option(LLVM_INSTALL_TOOLCHAIN_ONLY "Only include toolchain files in the 'install' target." OFF)

# Unfortunatly Clang is too eager to search directories for module maps, which can cause the
# installed version of the maps to be found when building LLVM from source. Therefore we turn off
# the installation by default. See llvm.org/PR31905.
option(LLVM_INSTALL_MODULEMAPS "Install the modulemap files in the 'install' target." OFF)

option(LLVM_USE_FOLDERS "Enable solution folders in Visual Studio. Disable for Express versions." ON)
if ( LLVM_USE_FOLDERS )
  set_property(GLOBAL PROPERTY USE_FOLDERS ON)
endif()

include(VersionFromVCS)

option(LLVM_APPEND_VC_REV
  "Embed the version control system revision in LLVM" ON)

set(LLVM_FORCE_VC_REVISION
  "" CACHE STRING "Force custom VC revision for LLVM_APPEND_VC_REV")

set(LLVM_FORCE_VC_REPOSITORY
  "" CACHE STRING "Force custom VC repository for LLVM_APPEND_VC_REV")

option(LLVM_TOOL_LLVM_DRIVER_BUILD "Enables building the llvm multicall tool" OFF)

set(PACKAGE_NAME LLVM)
set(PACKAGE_STRING "${PACKAGE_NAME} ${PACKAGE_VERSION}")
set(PACKAGE_BUGREPORT "https://github.com/llvm/llvm-project/issues/")

set(BUG_REPORT_URL "${PACKAGE_BUGREPORT}" CACHE STRING
  "Default URL where bug reports are to be submitted.")
set(LLDB_BUG_REPORT_URL "${BUG_REPORT_URL}" CACHE STRING
  "Default URL where lldb bug reports are to be submitted.")

# Configure CPack.
if(NOT DEFINED CPACK_PACKAGE_INSTALL_DIRECTORY)
  set(CPACK_PACKAGE_INSTALL_DIRECTORY "LLVM")
endif()
if(NOT DEFINED CPACK_PACKAGE_VENDOR)
  set(CPACK_PACKAGE_VENDOR "LLVM")
endif()
set(CPACK_PACKAGE_VERSION_MAJOR ${LLVM_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${LLVM_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${LLVM_VERSION_PATCH})
set(CPACK_PACKAGE_VERSION ${PACKAGE_VERSION})
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.TXT")
if(WIN32 AND NOT UNIX)
  set(CPACK_NSIS_COMPRESSOR "/SOLID lzma \r\n SetCompressorDictSize 32")
  if(NOT DEFINED CPACK_PACKAGE_INSTALL_REGISTRY_KEY)
    set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY "LLVM")
  endif()
  set(CPACK_PACKAGE_ICON "${CMAKE_CURRENT_SOURCE_DIR}\\\\cmake\\\\nsis_logo.bmp")
  set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}\\\\cmake\\\\nsis_icon.ico")
  set(CPACK_NSIS_MUI_UNIICON "${CMAKE_CURRENT_SOURCE_DIR}\\\\cmake\\\\nsis_icon.ico")
  set(CPACK_NSIS_MODIFY_PATH "ON")
  set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL "ON")
  if( CMAKE_CL_64 )
    if(NOT DEFINED CPACK_NSIS_INSTALL_ROOT)
      set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    endif()
  endif()
endif()
include(CPack)

# Sanity check our source directory to make sure that we are not trying to
# generate an in-source build (unless on MSVC_IDE, where it is ok), and to make
# sure that we don't have any stray generated files lying around in the tree
# (which would end up getting picked up by header search, instead of the correct
# versions).
if( CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_CURRENT_BINARY_DIR AND NOT MSVC_IDE )
  message(FATAL_ERROR "In-source builds are not allowed.
Please create a directory and run cmake from there, passing the path
to this source directory as the last argument.
This process created the file `CMakeCache.txt' and the directory `CMakeFiles'.
Please delete them.")
endif()

string(TOUPPER "${CMAKE_BUILD_TYPE}" uppercase_CMAKE_BUILD_TYPE)

option(LLVM_ADDITIONAL_BUILD_TYPES "Additional build types that are allowed to be passed into CMAKE_BUILD_TYPE" "")

set(ALLOWED_BUILD_TYPES DEBUG RELEASE RELWITHDEBINFO MINSIZEREL ${LLVM_ADDITIONAL_BUILD_TYPES})
string (REPLACE ";" "|" ALLOWED_BUILD_TYPES_STRING "${ALLOWED_BUILD_TYPES}")
string (TOUPPER "${ALLOWED_BUILD_TYPES_STRING}" uppercase_ALLOWED_BUILD_TYPES)

if (CMAKE_BUILD_TYPE AND
    NOT uppercase_CMAKE_BUILD_TYPE MATCHES "^(${uppercase_ALLOWED_BUILD_TYPES})$")
  message(FATAL_ERROR "Unknown value for CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
endif()

# LLVM_INSTALL_PACKAGE_DIR needs to be declared prior to adding the tools
# subdirectory in order to have the value available for llvm-config.
include(GNUInstallPackageDir)
set(LLVM_INSTALL_PACKAGE_DIR "${CMAKE_INSTALL_PACKAGEDIR}/llvm" CACHE STRING
  "Path for CMake subdirectory for LLVM (defaults to '${CMAKE_INSTALL_PACKAGEDIR}/llvm')")

set(LLVM_TOOLS_INSTALL_DIR "${CMAKE_INSTALL_BINDIR}" CACHE STRING
    "Path for binary subdirectory (defaults to '${CMAKE_INSTALL_BINDIR}')")
mark_as_advanced(LLVM_TOOLS_INSTALL_DIR)

set(LLVM_UTILS_INSTALL_DIR "${LLVM_TOOLS_INSTALL_DIR}" CACHE STRING
    "Path to install LLVM utilities (enabled by LLVM_INSTALL_UTILS=ON) (defaults to LLVM_TOOLS_INSTALL_DIR)")
mark_as_advanced(LLVM_UTILS_INSTALL_DIR)

set(LLVM_EXAMPLES_INSTALL_DIR "examples" CACHE STRING
    "Path for examples subdirectory (enabled by LLVM_BUILD_EXAMPLES=ON) (defaults to 'examples')")
mark_as_advanced(LLVM_EXAMPLES_INSTALL_DIR)

# They are used as destination of target generators.
set(LLVM_RUNTIME_OUTPUT_INTDIR ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/bin)
set(LLVM_LIBRARY_OUTPUT_INTDIR ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/lib${LLVM_LIBDIR_SUFFIX})
if(WIN32 OR CYGWIN)
  # DLL platform -- put DLLs into bin.
  set(LLVM_SHLIB_OUTPUT_INTDIR ${LLVM_RUNTIME_OUTPUT_INTDIR})
else()
  set(LLVM_SHLIB_OUTPUT_INTDIR ${LLVM_LIBRARY_OUTPUT_INTDIR})
endif()

# Each of them corresponds to llvm-config's.
set(LLVM_TOOLS_BINARY_DIR ${LLVM_RUNTIME_OUTPUT_INTDIR}) # --bindir
set(LLVM_LIBRARY_DIR      ${LLVM_LIBRARY_OUTPUT_INTDIR}) # --libdir
set(LLVM_MAIN_SRC_DIR     ${CMAKE_CURRENT_SOURCE_DIR}  ) # --src-root
set(LLVM_MAIN_INCLUDE_DIR ${LLVM_MAIN_SRC_DIR}/include ) # --includedir
set(LLVM_BINARY_DIR       ${CMAKE_CURRENT_BINARY_DIR}  ) # --prefix


# Note: LLVM_CMAKE_DIR does not include generated files
set(LLVM_CMAKE_DIR ${LLVM_MAIN_SRC_DIR}/cmake/modules)
set(LLVM_EXAMPLES_BINARY_DIR ${LLVM_BINARY_DIR}/examples)
set(LLVM_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/include)

# List of all targets to be built by default:
set(LLVM_ALL_TARGETS
  AArch64
  AMDGPU
  ARM
  AVR
  BPF
  Hexagon
  Lanai
  LoongArch
  Mips
  MSP430
  NVPTX
  PowerPC
  RISCV
  Sparc
  SystemZ
  VE
  WebAssembly
  X86
  XCore
  )

set(LLVM_ALL_EXPERIMENTAL_TARGETS
  ARC
  CSKY
  DirectX
  M68k
  SPIRV
  Xtensa
)

# List of targets with JIT support:
set(LLVM_TARGETS_WITH_JIT X86 PowerPC AArch64 ARM Mips SystemZ)

set(LLVM_TARGETS_TO_BUILD "all"
    CACHE STRING "Semicolon-separated list of targets to build, or \"all\".")

set(LLVM_EXPERIMENTAL_TARGETS_TO_BUILD ""
    CACHE STRING "Semicolon-separated list of experimental targets to build, or \"all\".")

option(BUILD_SHARED_LIBS
  "Build all libraries as shared libraries instead of static" OFF)

option(LLVM_ENABLE_BACKTRACES "Enable embedding backtraces on crash." ON)
if(LLVM_ENABLE_BACKTRACES)
  set(ENABLE_BACKTRACES 1)
endif()

option(LLVM_ENABLE_UNWIND_TABLES "Emit unwind tables for the libraries" ON)

option(LLVM_ENABLE_CRASH_OVERRIDES "Enable crash overrides." ON)
if(LLVM_ENABLE_CRASH_OVERRIDES)
  set(ENABLE_CRASH_OVERRIDES 1)
endif()

option(LLVM_ENABLE_CRASH_DUMPS "Turn on memory dumps on crashes. Currently only implemented on Windows." OFF)

set(WINDOWS_PREFER_FORWARD_SLASH_DEFAULT OFF)
if (MINGW)
  # Cygwin doesn't identify itself as Windows, and thus gets path::Style::posix
  # as native path style, regardless of what this is set to.
  set(WINDOWS_PREFER_FORWARD_SLASH_DEFAULT ON)
endif()
option(LLVM_WINDOWS_PREFER_FORWARD_SLASH "Prefer path names with forward slashes on Windows." ${WINDOWS_PREFER_FORWARD_SLASH_DEFAULT})

option(LLVM_ENABLE_FFI "Use libffi to call external functions from the interpreter" OFF)
set(FFI_LIBRARY_DIR "" CACHE PATH "Additional directory, where CMake should search for libffi.so")
set(FFI_INCLUDE_DIR "" CACHE PATH "Additional directory, where CMake should search for ffi.h or ffi/ffi.h")

set(LLVM_TARGET_ARCH "host"
  CACHE STRING "Set target to use for LLVM JIT or use \"host\" for automatic detection.")

set(LLVM_ENABLE_LIBXML2 "ON" CACHE STRING "Use libxml2 if available. Can be ON, OFF, or FORCE_ON")

option(LLVM_ENABLE_LIBEDIT "Use libedit if available." ON)

option(LLVM_ENABLE_LIBPFM "Use libpfm for performance counters if available." ON)

# On z/OS, threads cannot be used because TLS is not supported.
if (CMAKE_SYSTEM_NAME MATCHES "OS390")
  option(LLVM_ENABLE_THREADS "Use threads if available." OFF)
else()
  option(LLVM_ENABLE_THREADS "Use threads if available." ON)
endif()

set(LLVM_ENABLE_ZLIB "ON" CACHE STRING "Use zlib for compression/decompression if available. Can be ON, OFF, or FORCE_ON")

set(LLVM_ENABLE_ZSTD "ON" CACHE STRING "Use zstd for compression/decompression if available. Can be ON, OFF, or FORCE_ON")

set(LLVM_USE_STATIC_ZSTD FALSE CACHE BOOL "Use static version of zstd. Can be TRUE, FALSE")

set(LLVM_ENABLE_CURL "OFF" CACHE STRING "Use libcurl for the HTTP client if available. Can be ON, OFF, or FORCE_ON")

set(LLVM_HAS_LOGF128 "OFF" CACHE STRING "Use logf128 to constant fold fp128 logarithm calls. Can be ON, OFF, or FORCE_ON")

set(LLVM_ENABLE_HTTPLIB "OFF" CACHE STRING "Use cpp-httplib HTTP server library if available. Can be ON, OFF, or FORCE_ON")

set(LLVM_Z3_INSTALL_DIR "" CACHE STRING "Install directory of the Z3 solver.")

option(LLVM_ENABLE_Z3_SOLVER
  "Enable Support for the Z3 constraint solver in LLVM."
  ${LLVM_ENABLE_Z3_SOLVER_DEFAULT}
)

if (LLVM_ENABLE_Z3_SOLVER)
  find_package(Z3 4.8.9)

  if (LLVM_Z3_INSTALL_DIR)
    if (NOT Z3_FOUND)
      message(FATAL_ERROR "Z3 >= 4.8.9 has not been found in LLVM_Z3_INSTALL_DIR: ${LLVM_Z3_INSTALL_DIR}.")
    endif()
  endif()

  if (NOT Z3_FOUND)
    message(FATAL_ERROR "LLVM_ENABLE_Z3_SOLVER cannot be enabled when Z3 is not available.")
  endif()

  set(LLVM_WITH_Z3 1)
endif()

set(LLVM_ENABLE_Z3_SOLVER_DEFAULT "${Z3_FOUND}")


if( LLVM_TARGETS_TO_BUILD STREQUAL "all" )
  set( LLVM_TARGETS_TO_BUILD ${LLVM_ALL_TARGETS} )
endif()

if(LLVM_EXPERIMENTAL_TARGETS_TO_BUILD STREQUAL "all")
  set(LLVM_EXPERIMENTAL_TARGETS_TO_BUILD ${LLVM_ALL_EXPERIMENTAL_TARGETS})
endif()

set(LLVM_TARGETS_TO_BUILD
   ${LLVM_TARGETS_TO_BUILD}
   ${LLVM_EXPERIMENTAL_TARGETS_TO_BUILD})
list(REMOVE_DUPLICATES LLVM_TARGETS_TO_BUILD)

if (NOT CMAKE_SYSTEM_NAME MATCHES "OS390")
  option(LLVM_ENABLE_PIC "Build Position-Independent Code" ON)
endif()
option(LLVM_ENABLE_MODULES "Compile with C++ modules enabled." OFF)
if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  option(LLVM_ENABLE_MODULE_DEBUGGING "Compile with -gmodules." ON)
else()
  option(LLVM_ENABLE_MODULE_DEBUGGING "Compile with -gmodules." OFF)
endif()
option(LLVM_ENABLE_LOCAL_SUBMODULE_VISIBILITY "Compile with -fmodules-local-submodule-visibility." ON)
option(LLVM_ENABLE_LIBCXX "Use libc++ if available." OFF)
option(LLVM_ENABLE_LLVM_LIBC "Set to on to link all LLVM executables against LLVM libc, assuming it is accessible by the host compiler." OFF)
option(LLVM_STATIC_LINK_CXX_STDLIB "Statically link the standard library." OFF)
option(LLVM_ENABLE_LLD "Use lld as C and C++ linker." OFF)
option(LLVM_ENABLE_PEDANTIC "Compile with pedantic enabled." ON)
option(LLVM_ENABLE_WERROR "Fail and stop if a warning is triggered." OFF)

option(LLVM_ENABLE_DUMP "Enable dump functions even when assertions are disabled" OFF)
option(LLVM_UNREACHABLE_OPTIMIZE "Optimize llvm_unreachable() as undefined behavior (default), guaranteed trap when OFF" ON)

if( NOT uppercase_CMAKE_BUILD_TYPE STREQUAL "DEBUG" )
  option(LLVM_ENABLE_ASSERTIONS "Enable assertions" OFF)
else()
  option(LLVM_ENABLE_ASSERTIONS "Enable assertions" ON)
endif()

option(LLVM_ENABLE_EXPENSIVE_CHECKS "Enable expensive checks" OFF)

# While adding scalable vector support to LLVM, we temporarily want to
# allow an implicit conversion of TypeSize to uint64_t, and to allow
# code to get the fixed number of elements from a possibly scalable vector.
# This CMake flag enables a more strict mode where it asserts that the type
# is not a scalable vector type.
#
# Enabling this flag makes it easier to find cases where the compiler makes
# assumptions on the size being 'fixed size', when building tests for
# SVE/SVE2 or other scalable vector architectures.
option(LLVM_ENABLE_STRICT_FIXED_SIZE_VECTORS
       "Enable assertions that type is not scalable in implicit conversion from TypeSize to uint64_t and calls to getNumElements" OFF)

set(LLVM_ABI_BREAKING_CHECKS "WITH_ASSERTS" CACHE STRING
  "Enable abi-breaking checks.  Can be WITH_ASSERTS, FORCE_ON or FORCE_OFF.")

option(LLVM_FORCE_USE_OLD_TOOLCHAIN
       "Set to ON to force using an old, unsupported host toolchain." OFF)

set(LLVM_LOCAL_RPATH "" CACHE FILEPATH
  "If set, an absolute path added as rpath on binaries that do not already contain an executable-relative rpath.")

option(LLVM_TEMPORARILY_ALLOW_OLD_TOOLCHAIN
       "Set to ON to only warn when using a toolchain which is about to be deprecated, instead of emitting an error." OFF)

option(LLVM_USE_INTEL_JITEVENTS
  "Use Intel JIT API to inform Intel(R) VTune(TM) Amplifier XE 2011 about JIT code"
  OFF)

if( LLVM_USE_INTEL_JITEVENTS )
  # Verify we are on a supported platform
  if( NOT CMAKE_SYSTEM_NAME MATCHES "Windows" AND NOT CMAKE_SYSTEM_NAME MATCHES "Linux" )
    message(FATAL_ERROR
      "Intel JIT API support is available on Linux and Windows only.")
  endif()
endif( LLVM_USE_INTEL_JITEVENTS )

option(LLVM_USE_OPROFILE
  "Use opagent JIT interface to inform OProfile about JIT code" OFF)

option(LLVM_EXTERNALIZE_DEBUGINFO
  "Generate dSYM files and strip executables and libraries (Darwin Only)" OFF)

option(LLVM_ENABLE_EXPORTED_SYMBOLS_IN_EXECUTABLES
  "Preserve exported symbols in executables" ON)

set(LLVM_CODESIGNING_IDENTITY "" CACHE STRING
  "Sign executables and dylibs with the given identity or skip if empty (Darwin Only)")

# If enabled, verify we are on a platform that supports oprofile.
if( LLVM_USE_OPROFILE )
  if( NOT CMAKE_SYSTEM_NAME MATCHES "Linux" )
    message(FATAL_ERROR "OProfile support is available on Linux only.")
  endif( NOT CMAKE_SYSTEM_NAME MATCHES "Linux" )
endif( LLVM_USE_OPROFILE )

option(LLVM_USE_PERF
  "Use perf JIT interface to inform perf about JIT code" OFF)

# If enabled, verify we are on a platform that supports perf.
if( LLVM_USE_PERF )
  if( NOT CMAKE_SYSTEM_NAME MATCHES "Linux" )
    message(FATAL_ERROR "perf support is available on Linux only.")
  endif( NOT CMAKE_SYSTEM_NAME MATCHES "Linux" )
endif( LLVM_USE_PERF )

set(LLVM_USE_SANITIZER "" CACHE STRING
  "Define the sanitizer used to build binaries and tests.")
option(LLVM_OPTIMIZE_SANITIZED_BUILDS "Pass -O1 on debug sanitizer builds" ON)
set(LLVM_UBSAN_FLAGS
    "-fsanitize=undefined -fno-sanitize=vptr,function -fno-sanitize-recover=all"
    CACHE STRING
    "Compile flags set to enable UBSan. Only used if LLVM_USE_SANITIZER contains 'Undefined'.")
set(LLVM_LIB_FUZZING_ENGINE "" CACHE PATH
  "Path to fuzzing library for linking with fuzz targets")

option(LLVM_USE_SPLIT_DWARF
  "Use -gsplit-dwarf when compiling llvm and --gdb-index when linking." OFF)

# Define an option controlling whether we should build for 32-bit on 64-bit
# platforms, where supported.
if( CMAKE_SIZEOF_VOID_P EQUAL 8 AND NOT (WIN32 OR ${CMAKE_SYSTEM_NAME} MATCHES "AIX"))
  # TODO: support other platforms and toolchains.
  option(LLVM_BUILD_32_BITS "Build 32 bits executables and libraries." OFF)
endif()

# Define the default arguments to use with 'lit', and an option for the user to
# override.
set(LIT_ARGS_DEFAULT "-sv")
if (MSVC_IDE OR XCODE)
  set(LIT_ARGS_DEFAULT "${LIT_ARGS_DEFAULT} --no-progress-bar")
endif()
if(LLVM_INDIVIDUAL_TEST_COVERAGE)
   set(LIT_ARGS_DEFAULT "${LIT_ARGS_DEFAULT} --per-test-coverage")
endif()
set(LLVM_LIT_ARGS "${LIT_ARGS_DEFAULT}" CACHE STRING "Default options for lit")

# On Win32 hosts, provide an option to specify the path to the GnuWin32 tools.
if( WIN32 AND NOT CYGWIN )
  set(LLVM_LIT_TOOLS_DIR "" CACHE PATH "Path to GnuWin32 tools")
endif()
set(LLVM_NATIVE_TOOL_DIR "" CACHE PATH "Path to a directory containing prebuilt matching native tools (such as llvm-tblgen)")

set(LLVM_ENABLE_RPMALLOC "" CACHE BOOL "Replace the CRT allocator with rpmalloc.")
if(LLVM_ENABLE_RPMALLOC)
  if(NOT (CMAKE_SYSTEM_NAME MATCHES "Windows|Linux"))
    message(FATAL_ERROR "LLVM_ENABLE_RPMALLOC is only supported on Windows and Linux.")
  endif()
  if(LLVM_USE_SANITIZER)
    message(FATAL_ERROR "LLVM_ENABLE_RPMALLOC cannot be used along with LLVM_USE_SANITIZER!")
  endif()
  if(WIN32)
    if(CMAKE_CONFIGURATION_TYPES)
      foreach(BUILD_MODE ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER "${BUILD_MODE}" uppercase_BUILD_MODE)
        if(uppercase_BUILD_MODE STREQUAL "DEBUG")
          message(WARNING "The Debug target isn't supported along with LLVM_ENABLE_RPMALLOC!")
        endif()
      endforeach()
    else()
      if(CMAKE_BUILD_TYPE AND uppercase_CMAKE_BUILD_TYPE STREQUAL "DEBUG")
        message(FATAL_ERROR "The Debug target isn't supported along with LLVM_ENABLE_RPMALLOC!")
      endif()
    endif()
  endif()

  # Override the C runtime allocator with the in-tree rpmalloc
  set(LLVM_INTEGRATED_CRT_ALLOC "${CMAKE_CURRENT_SOURCE_DIR}/lib/Support")
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

set(LLVM_INTEGRATED_CRT_ALLOC "${LLVM_INTEGRATED_CRT_ALLOC}" CACHE PATH "Replace the Windows CRT allocator with any of {rpmalloc|mimalloc|snmalloc}. Only works with CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded.")
if(LLVM_INTEGRATED_CRT_ALLOC)
  if(NOT WIN32)
    message(FATAL_ERROR "LLVM_INTEGRATED_CRT_ALLOC is only supported on Windows.")
  endif()
  if(LLVM_USE_SANITIZER)
    message(FATAL_ERROR "LLVM_INTEGRATED_CRT_ALLOC cannot be used along with LLVM_USE_SANITIZER!")
  endif()
  if(CMAKE_BUILD_TYPE AND uppercase_CMAKE_BUILD_TYPE STREQUAL "DEBUG")
    message(FATAL_ERROR "The Debug target isn't supported along with LLVM_INTEGRATED_CRT_ALLOC!")
  endif()
endif()

# Define options to control the inclusion and default build behavior for
# components which may not strictly be necessary (tools, examples, and tests).
#
# This is primarily to support building smaller or faster project files.
option(LLVM_INCLUDE_TOOLS "Generate build targets for the LLVM tools." ON)
option(LLVM_BUILD_TOOLS
  "Build the LLVM tools. If OFF, just generate build targets." ON)

option(LLVM_INCLUDE_UTILS "Generate build targets for the LLVM utils." ON)
option(LLVM_BUILD_UTILS
  "Build LLVM utility binaries. If OFF, just generate build targets." ON)

option(LLVM_INCLUDE_RUNTIMES "Generate build targets for the LLVM runtimes." ON)
option(LLVM_BUILD_RUNTIMES
  "Build the LLVM runtimes. If OFF, just generate build targets." ON)

option(LLVM_BUILD_RUNTIME
  "Build the LLVM runtime libraries." ON)
option(LLVM_BUILD_EXAMPLES
  "Build the LLVM example programs. If OFF, just generate build targets." OFF)
option(LLVM_INCLUDE_EXAMPLES "Generate build targets for the LLVM examples" ON)

if(LLVM_BUILD_EXAMPLES)
  add_compile_definitions(BUILD_EXAMPLES)
endif(LLVM_BUILD_EXAMPLES)

option(LLVM_BUILD_TESTS
  "Build LLVM unit tests. If OFF, just generate build targets." OFF)
option(LLVM_INCLUDE_TESTS "Generate build targets for the LLVM unit tests." ON)

option(LLVM_INSTALL_GTEST
  "Install the llvm gtest library.  This should be on if you want to do
   stand-alone builds of the other projects and run their unit tests." OFF)

option(LLVM_BUILD_BENCHMARKS "Add LLVM benchmark targets to the list of default
targets. If OFF, benchmarks still could be built using Benchmarks target." OFF)
option(LLVM_INCLUDE_BENCHMARKS "Generate benchmark targets. If OFF, benchmarks can't be built." ON)

option (LLVM_BUILD_DOCS "Build the llvm documentation." OFF)
option (LLVM_INCLUDE_DOCS "Generate build targets for llvm documentation." ON)
option (LLVM_ENABLE_DOXYGEN "Use doxygen to generate llvm API documentation." OFF)
option (LLVM_ENABLE_SPHINX "Use Sphinx to generate llvm documentation." OFF)
option (LLVM_ENABLE_OCAMLDOC "Build OCaml bindings documentation." ON)
option (LLVM_ENABLE_BINDINGS "Build bindings." ON)

set(LLVM_INSTALL_DOXYGEN_HTML_DIR "${CMAKE_INSTALL_DOCDIR}/llvm/doxygen-html"
    CACHE STRING "Doxygen-generated HTML documentation install directory")
set(LLVM_INSTALL_OCAMLDOC_HTML_DIR "${CMAKE_INSTALL_DOCDIR}/llvm/ocaml-html"
    CACHE STRING "OCamldoc-generated HTML documentation install directory")

option (LLVM_BUILD_EXTERNAL_COMPILER_RT
  "Build compiler-rt as an external project." OFF)

option (LLVM_VERSION_PRINTER_SHOW_HOST_TARGET_INFO
  "Show target and host info when tools are invoked with --version." ON)

option(LLVM_VERSION_PRINTER_SHOW_BUILD_CONFIG
  "Show the optional build config flags when tools are invoked with --version." ON)

# You can configure which libraries from LLVM you want to include in the
# shared library by setting LLVM_DYLIB_COMPONENTS to a semi-colon delimited
# list of LLVM components. All component names handled by llvm-config are valid.
if(NOT DEFINED LLVM_DYLIB_COMPONENTS)
  set(LLVM_DYLIB_COMPONENTS "all" CACHE STRING
    "Semicolon-separated list of components to include in libLLVM, or \"all\".")
endif()

if(MSVC)
  option(LLVM_BUILD_LLVM_C_DYLIB "Build LLVM-C.dll (Windows only)" ON)
  # Set this variable to OFF here so it can't be set with a command-line
  # argument.
  set (LLVM_LINK_LLVM_DYLIB OFF)
  if (BUILD_SHARED_LIBS)
    message(FATAL_ERROR "BUILD_SHARED_LIBS options is not supported on Windows.")
  endif()
else()
  option(LLVM_LINK_LLVM_DYLIB "Link tools against the libllvm dynamic library" OFF)
  option(LLVM_BUILD_LLVM_C_DYLIB "Build libllvm-c re-export library (Darwin only)" OFF)
  set(LLVM_BUILD_LLVM_DYLIB_default OFF)
  if(LLVM_LINK_LLVM_DYLIB OR LLVM_BUILD_LLVM_C_DYLIB)
    set(LLVM_BUILD_LLVM_DYLIB_default ON)
  endif()
  option(LLVM_BUILD_LLVM_DYLIB "Build libllvm dynamic library" ${LLVM_BUILD_LLVM_DYLIB_default})
endif()

if (LLVM_LINK_LLVM_DYLIB AND BUILD_SHARED_LIBS)
  message(FATAL_ERROR "Cannot enable BUILD_SHARED_LIBS with LLVM_LINK_LLVM_DYLIB.  We recommend disabling BUILD_SHARED_LIBS.")
endif()

option(LLVM_OPTIMIZED_TABLEGEN "Force TableGen to be built with optimization" OFF)
if(CMAKE_CROSSCOMPILING OR (LLVM_OPTIMIZED_TABLEGEN AND (LLVM_ENABLE_ASSERTIONS OR CMAKE_CONFIGURATION_TYPES)))
  set(LLVM_USE_HOST_TOOLS ON)
endif()

option(LLVM_OMIT_DAGISEL_COMMENTS "Do not add comments to DAG ISel" ON)
if (CMAKE_BUILD_TYPE AND uppercase_CMAKE_BUILD_TYPE MATCHES "^(RELWITHDEBINFO|DEBUG)$")
  set(LLVM_OMIT_DAGISEL_COMMENTS OFF)
endif()

if (MSVC_IDE)
  option(LLVM_ADD_NATIVE_VISUALIZERS_TO_SOLUTION "Configure project to use Visual Studio native visualizers" TRUE)
endif()

if(NOT LLVM_INDIVIDUAL_TEST_COVERAGE)
  if(LLVM_BUILD_INSTRUMENTED OR LLVM_BUILD_INSTRUMENTED_COVERAGE OR LLVM_ENABLE_IR_PGO)
    if(NOT LLVM_PROFILE_MERGE_POOL_SIZE)
      # A pool size of 1-2 is probably sufficient on an SSD. 3-4 should be fine
      # for spinning disks. Anything higher may only help on slower mediums.
      set(LLVM_PROFILE_MERGE_POOL_SIZE "4")
    endif()
    if(NOT LLVM_PROFILE_FILE_PATTERN)
      if(NOT LLVM_PROFILE_DATA_DIR)
        file(TO_NATIVE_PATH "${LLVM_BINARY_DIR}/profiles" LLVM_PROFILE_DATA_DIR)
      endif()
      file(TO_NATIVE_PATH "${LLVM_PROFILE_DATA_DIR}/%${LLVM_PROFILE_MERGE_POOL_SIZE}m.profraw" LLVM_PROFILE_FILE_PATTERN)
    endif()
    if(NOT LLVM_CSPROFILE_FILE_PATTERN)
      if(NOT LLVM_CSPROFILE_DATA_DIR)
        file(TO_NATIVE_PATH "${LLVM_BINARY_DIR}/csprofiles" LLVM_CSPROFILE_DATA_DIR)
      endif()
      file(TO_NATIVE_PATH "${LLVM_CSPROFILE_DATA_DIR}/%${LLVM_PROFILE_MERGE_POOL_SIZE}m.profraw" LLVM_CSPROFILE_FILE_PATTERN)
    endif()
  endif()
endif()

if (LLVM_BUILD_STATIC)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
  # Remove shared library suffixes from use in find_library
  foreach (shared_lib_suffix ${CMAKE_SHARED_LIBRARY_SUFFIX} ${CMAKE_IMPORT_LIBRARY_SUFFIX})
    list(FIND CMAKE_FIND_LIBRARY_SUFFIXES ${shared_lib_suffix} shared_lib_suffix_idx)
    if(NOT ${shared_lib_suffix_idx} EQUAL -1)
      list(REMOVE_AT CMAKE_FIND_LIBRARY_SUFFIXES ${shared_lib_suffix_idx})
    endif()
  endforeach()
endif()

# Use libtool instead of ar if you are both on an Apple host, and targeting Apple.
if(CMAKE_HOST_APPLE AND APPLE)
  include(UseLibtool)
endif()

# Override the default target with an environment variable named by LLVM_TARGET_TRIPLE_ENV.
set(LLVM_TARGET_TRIPLE_ENV CACHE STRING "The name of environment variable to override default target. Disabled by blank.")
mark_as_advanced(LLVM_TARGET_TRIPLE_ENV)

if(CMAKE_SYSTEM_NAME MATCHES "BSD|Linux|OS390")
  set(LLVM_ENABLE_PER_TARGET_RUNTIME_DIR_default ON)
else()
  set(LLVM_ENABLE_PER_TARGET_RUNTIME_DIR_default OFF)
endif()
set(LLVM_ENABLE_PER_TARGET_RUNTIME_DIR ${LLVM_ENABLE_PER_TARGET_RUNTIME_DIR_default} CACHE BOOL
  "Enable per-target runtimes directory")

set(LLVM_PROFDATA_FILE "" CACHE FILEPATH
  "Profiling data file to use when compiling in order to improve runtime performance.")

if(LLVM_INCLUDE_TESTS)
  # All LLVM Python files should be compatible down to this minimum version.
  set(LLVM_MINIMUM_PYTHON_VERSION 3.8)
else()
  # FIXME: it is unknown if this is the actual minimum bound
  set(LLVM_MINIMUM_PYTHON_VERSION 3.0)
endif()

# Find python before including config-ix, since it needs to be able to search
# for python modules.
find_package(Python3 ${LLVM_MINIMUM_PYTHON_VERSION} REQUIRED
    COMPONENTS Interpreter)

# All options referred to from HandleLLVMOptions have to be specified
# BEFORE this include, otherwise options will not be correctly set on
# first cmake run
include(config-ix)

# By default, we target the host, but this can be overridden at CMake
# invocation time. Except on 64-bit AIX, where the system toolchain
# expect 32-bit objects by default.
if("${LLVM_HOST_TRIPLE}" MATCHES "^powerpc64-ibm-aix")
  string(REGEX REPLACE "^powerpc64" "powerpc" LLVM_DEFAULT_TARGET_TRIPLE_DEFAULT "${LLVM_HOST_TRIPLE}")
else()
  # Only set default triple when native target is enabled.
  if (LLVM_NATIVE_TARGET)
    set(LLVM_DEFAULT_TARGET_TRIPLE_DEFAULT "${LLVM_HOST_TRIPLE}")
  endif()
endif()

set(LLVM_DEFAULT_TARGET_TRIPLE "${LLVM_DEFAULT_TARGET_TRIPLE_DEFAULT}" CACHE STRING
    "Default target for which LLVM will generate code." )
message(STATUS "LLVM default target triple: ${LLVM_DEFAULT_TARGET_TRIPLE}")

set(LLVM_TARGET_TRIPLE "${LLVM_DEFAULT_TARGET_TRIPLE}")

if(WIN32 OR CYGWIN)
  if(BUILD_SHARED_LIBS OR LLVM_BUILD_LLVM_DYLIB)
    set(LLVM_ENABLE_PLUGINS_default ON)
  else()
    set(LLVM_ENABLE_PLUGINS_default OFF)
  endif()
else()
  set(LLVM_ENABLE_PLUGINS_default ${LLVM_ENABLE_PIC})
endif()
option(LLVM_ENABLE_PLUGINS "Enable plugin support" ${LLVM_ENABLE_PLUGINS_default})

set(LLVM_ENABLE_NEW_PASS_MANAGER TRUE CACHE BOOL
  "Enable the new pass manager by default.")
if(NOT LLVM_ENABLE_NEW_PASS_MANAGER)
  message(FATAL_ERROR "Enabling the legacy pass manager on the cmake level is"
                      " no longer supported.")
endif()

include(HandleLLVMOptions)

######

# Configure all of the various header file fragments LLVM uses which depend on
# configuration variables.
set(LLVM_ENUM_TARGETS "")
set(LLVM_ENUM_ASM_PRINTERS "")
set(LLVM_ENUM_ASM_PARSERS "")
set(LLVM_ENUM_DISASSEMBLERS "")
set(LLVM_ENUM_TARGETMCAS "")
set(LLVM_ENUM_EXEGESIS "")
foreach(t ${LLVM_TARGETS_TO_BUILD})
  set( td ${LLVM_MAIN_SRC_DIR}/lib/Target/${t} )

  # Make sure that any experimental targets were passed via
  # LLVM_EXPERIMENTAL_TARGETS_TO_BUILD, not LLVM_TARGETS_TO_BUILD.
  # We allow experimental targets that are not in LLVM_ALL_EXPERIMENTAL_TARGETS,
  # as long as they are passed via LLVM_EXPERIMENTAL_TARGETS_TO_BUILD.
  if ( NOT "${t}" IN_LIST LLVM_ALL_TARGETS AND NOT "${t}" IN_LIST LLVM_EXPERIMENTAL_TARGETS_TO_BUILD )
    if( "${t}" IN_LIST LLVM_ALL_EXPERIMENTAL_TARGETS )
      message(FATAL_ERROR "The target `${t}' is experimental and must be passed "
        "via LLVM_EXPERIMENTAL_TARGETS_TO_BUILD.")
    else()
      message(FATAL_ERROR "The target `${t}' is not a core tier target. It may be "
        "experimental, if so it must be passed via "
        "LLVM_EXPERIMENTAL_TARGETS_TO_BUILD.\n"
        "Core tier targets: ${LLVM_ALL_TARGETS}\n"
        "Known experimental targets: ${LLVM_ALL_EXPERIMENTAL_TARGETS}")
    endif()
  else()
    set(LLVM_ENUM_TARGETS "${LLVM_ENUM_TARGETS}LLVM_TARGET(${t})\n")
    string(TOUPPER ${t} T_UPPER)
    set(LLVM_HAS_${T_UPPER}_TARGET 1)
  endif()

  file(GLOB asmp_file "${td}/*AsmPrinter.cpp")
  if( asmp_file )
    set(LLVM_ENUM_ASM_PRINTERS
      "${LLVM_ENUM_ASM_PRINTERS}LLVM_ASM_PRINTER(${t})\n")
  endif()
  if( EXISTS ${td}/AsmParser/CMakeLists.txt )
    set(LLVM_ENUM_ASM_PARSERS
      "${LLVM_ENUM_ASM_PARSERS}LLVM_ASM_PARSER(${t})\n")
  endif()
  if( EXISTS ${td}/Disassembler/CMakeLists.txt )
    set(LLVM_ENUM_DISASSEMBLERS
      "${LLVM_ENUM_DISASSEMBLERS}LLVM_DISASSEMBLER(${t})\n")
  endif()
  if( EXISTS ${td}/MCA/CMakeLists.txt )
    set(LLVM_ENUM_TARGETMCAS
      "${LLVM_ENUM_TARGETMCAS}LLVM_TARGETMCA(${t})\n")
  endif()
  if( EXISTS ${LLVM_MAIN_SRC_DIR}/tools/llvm-exegesis/lib/${t}/CMakeLists.txt )
    set(LLVM_ENUM_EXEGESIS
      "${LLVM_ENUM_EXEGESIS}LLVM_EXEGESIS(${t})\n")
  endif()
endforeach(t)

# Provide an LLVM_ namespaced alias for use in #cmakedefine.
set(LLVM_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})

# Produce the target definition files, which provide a way for clients to easily
# include various classes of targets.
configure_file(
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Config/AsmPrinters.def.in
  ${LLVM_INCLUDE_DIR}/llvm/Config/AsmPrinters.def
  )
configure_file(
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Config/AsmParsers.def.in
  ${LLVM_INCLUDE_DIR}/llvm/Config/AsmParsers.def
  )
configure_file(
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Config/Disassemblers.def.in
  ${LLVM_INCLUDE_DIR}/llvm/Config/Disassemblers.def
  )
configure_file(
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Config/Targets.def.in
  ${LLVM_INCLUDE_DIR}/llvm/Config/Targets.def
  )
configure_file(
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Config/TargetMCAs.def.in
  ${LLVM_INCLUDE_DIR}/llvm/Config/TargetMCAs.def
  )
configure_file(
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Config/TargetExegesis.def.in
  ${LLVM_INCLUDE_DIR}/llvm/Config/TargetExegesis.def
  )

# They are not referenced. See set_output_directory().
set( CMAKE_RUNTIME_OUTPUT_DIRECTORY ${LLVM_TOOLS_BINARY_DIR} )
set( CMAKE_LIBRARY_OUTPUT_DIRECTORY ${LLVM_LIBRARY_DIR} )
set( CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${LLVM_LIBRARY_DIR} )

# For up-to-date instructions for installing the TFLite dependency, refer to
# the bot setup script: https://github.com/google/ml-compiler-opt/blob/main/buildbot/buildbot_init.sh
set(LLVM_HAVE_TFLITE "" CACHE BOOL "Use tflite")
if (LLVM_HAVE_TFLITE)
  find_package(tensorflow-lite REQUIRED)
endif()

# For up-to-date instructions for installing the Tensorflow dependency, refer to
# the bot setup script: https://github.com/google/ml-compiler-opt/blob/main/buildbot/buildbot_init.sh
# Specifically, assuming python3 is installed:
# python3 -m pip install --upgrade pip && python3 -m pip install --user tf_nightly==2.3.0.dev20200528
# Then set TENSORFLOW_AOT_PATH to the package install - usually it's ~/.local/lib/python3.7/site-packages/tensorflow
#
set(TENSORFLOW_AOT_PATH "" CACHE PATH "Path to TensorFlow pip install dir")

if (NOT TENSORFLOW_AOT_PATH STREQUAL "")
  set(LLVM_HAVE_TF_AOT "ON" CACHE BOOL "Tensorflow AOT available")
  set(TENSORFLOW_AOT_COMPILER
    "${TENSORFLOW_AOT_PATH}/../../../../bin/saved_model_cli"
    CACHE PATH "Path to the Tensorflow AOT compiler")
  include_directories(${TENSORFLOW_AOT_PATH}/include)
  add_subdirectory(${TENSORFLOW_AOT_PATH}/xla_aot_runtime_src
    ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}/tf_runtime)
  install(TARGETS tf_xla_runtime EXPORT LLVMExports
    ARCHIVE DESTINATION lib${LLVM_LIBDIR_SUFFIX} COMPONENT tf_xla_runtime)
  set_property(GLOBAL APPEND PROPERTY LLVM_EXPORTS tf_xla_runtime)
  # Once we add more modules, we should handle this more automatically.
  if (DEFINED LLVM_OVERRIDE_MODEL_HEADER_INLINERSIZEMODEL)
    set(LLVM_INLINER_MODEL_PATH "none")
  elseif(NOT DEFINED LLVM_INLINER_MODEL_PATH
      OR "${LLVM_INLINER_MODEL_PATH}" STREQUAL ""
      OR "${LLVM_INLINER_MODEL_PATH}" STREQUAL "autogenerate")
    set(LLVM_INLINER_MODEL_PATH "autogenerate")
    set(LLVM_INLINER_MODEL_AUTOGENERATED 1)
  endif()
  if (DEFINED LLVM_OVERRIDE_MODEL_HEADER_REGALLOCEVICTMODEL)
    set(LLVM_RAEVICT_MODEL_PATH "none")
  elseif(NOT DEFINED LLVM_RAEVICT_MODEL_PATH
      OR "${LLVM_RAEVICT_MODEL_PATH}" STREQUAL ""
      OR "${LLVM_RAEVICT_MODEL_PATH}" STREQUAL "autogenerate")
    set(LLVM_RAEVICT_MODEL_PATH "autogenerate")
    set(LLVM_RAEVICT_MODEL_AUTOGENERATED 1)
  endif()

endif()

# Configure the three LLVM configuration header files.
configure_file(
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Config/config.h.cmake
  ${LLVM_INCLUDE_DIR}/llvm/Config/config.h)
configure_file(
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Config/llvm-config.h.cmake
  ${LLVM_INCLUDE_DIR}/llvm/Config/llvm-config.h)
configure_file(
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Config/abi-breaking.h.cmake
  ${LLVM_INCLUDE_DIR}/llvm/Config/abi-breaking.h)

if(APPLE AND DARWIN_LTO_LIBRARY)
  set(CMAKE_EXE_LINKER_FLAGS
    "${CMAKE_EXE_LINKER_FLAGS} -Wl,-lto_library -Wl,${DARWIN_LTO_LIBRARY}")
  set(CMAKE_SHARED_LINKER_FLAGS
    "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-lto_library -Wl,${DARWIN_LTO_LIBRARY}")
  set(CMAKE_MODULE_LINKER_FLAGS
    "${CMAKE_MODULE_LINKER_FLAGS} -Wl,-lto_library -Wl,${DARWIN_LTO_LIBRARY}")
endif()

# Build with _XOPEN_SOURCE on AIX, as stray macros in _ALL_SOURCE mode tend to
# break things. In this case we need to enable the large-file API as well.
if (UNIX AND ${CMAKE_SYSTEM_NAME} MATCHES "AIX")
          add_compile_definitions(_XOPEN_SOURCE=700)
          add_compile_definitions(_LARGE_FILE_API)

  # Modules should be built with -shared -Wl,-G, so we can use runtime linking
  # with plugins.
  string(APPEND CMAKE_MODULE_LINKER_FLAGS " -shared -Wl,-G")

  # Also set the correct flags for building shared libraries.
  string(APPEND CMAKE_SHARED_LINKER_FLAGS " -shared")
endif()

# Build with _XOPEN_SOURCE on z/OS.
if (CMAKE_SYSTEM_NAME MATCHES "OS390")
  add_compile_definitions(_XOPEN_SOURCE=600)
  add_compile_definitions(_OPEN_SYS) # Needed for process information.
  add_compile_definitions(_OPEN_SYS_FILE_EXT) # Needed for EBCDIC I/O.
  add_compile_definitions(_EXT) # Needed for file data.
  add_compile_definitions(_UNIX03_THREADS) # Multithreading support.
  # Need to build LLVM as ASCII application.
  # This can't be a global setting because other projects may
  # need to be built in EBCDIC mode.
  append("-fzos-le-char-mode=ascii" CMAKE_CXX_FLAGS CMAKE_C_FLAGS)
  append("-m64" CMAKE_CXX_FLAGS CMAKE_C_FLAGS)
endif()

# Build with _FILE_OFFSET_BITS=64 on Solaris to match g++ >= 9.
if (UNIX AND ${CMAKE_SYSTEM_NAME} MATCHES "SunOS")
          add_compile_definitions(_FILE_OFFSET_BITS=64)
endif()

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories( ${LLVM_INCLUDE_DIR} ${LLVM_MAIN_INCLUDE_DIR})

# when crosscompiling import the executable targets from a file
if(LLVM_USE_HOST_TOOLS)
  include(CrossCompile)
  llvm_create_cross_target(LLVM NATIVE "" Release)
endif(LLVM_USE_HOST_TOOLS)
if(LLVM_TARGET_IS_CROSSCOMPILE_HOST)
# Dummy use to avoid CMake Warning: Manually-specified variables were not used
# (this is a variable that CrossCompile sets on recursive invocations)
endif()

if( ${CMAKE_SYSTEM_NAME} MATCHES SunOS )
   # special hack for Solaris to handle crazy system sys/regset.h
   include_directories("${LLVM_MAIN_INCLUDE_DIR}/llvm/Support/Solaris")
endif( ${CMAKE_SYSTEM_NAME} MATCHES SunOS )

# Make sure we don't get -rdynamic in every binary. For those that need it,
# use export_executable_symbols(target).
set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS "")

include(AddLLVM)
include(TableGen)

include(LLVMDistributionSupport)

if( MINGW AND NOT "${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang" )
  # People report that -O3 is unreliable on MinGW. The traditional
  # build also uses -O2 for that reason:
  llvm_replace_compiler_option(CMAKE_CXX_FLAGS_RELEASE "-O3" "-O2")
endif()

if(LLVM_INCLUDE_TESTS)
  umbrella_lit_testsuite_begin(check-all)
endif()

# Put this before tblgen. Else we have a circular dependence.
add_subdirectory(lib/Demangle)
add_subdirectory(lib/Support)
add_subdirectory(lib/TableGen)

add_subdirectory(utils/TableGen)

add_subdirectory(include)

add_subdirectory(lib)

if( LLVM_INCLUDE_UTILS )
  add_subdirectory(utils/FileCheck)
  add_subdirectory(utils/PerfectShuffle)
  add_subdirectory(utils/count)
  add_subdirectory(utils/not)
  add_subdirectory(utils/UnicodeData)
  add_subdirectory(utils/yaml-bench)
  add_subdirectory(utils/split-file)
  add_subdirectory(utils/mlgo-utils)
  if( LLVM_INCLUDE_TESTS )
    set(LLVM_SUBPROJECT_TITLE "Third-Party/Google Test")
    add_subdirectory(${LLVM_THIRD_PARTY_DIR}/unittest ${CMAKE_CURRENT_BINARY_DIR}/third-party/unittest)
    set(LLVM_SUBPROJECT_TITLE) 
  endif()
else()
  if ( LLVM_INCLUDE_TESTS )
    message(FATAL_ERROR "Including tests when not building utils will not work.
    Either set LLVM_INCLUDE_UTILS to On, or set LLVM_INCLUDE_TESTS to Off.")
  endif()
endif()

# Use LLVM_ADD_NATIVE_VISUALIZERS_TO_SOLUTION instead of LLVM_INCLUDE_UTILS because it is not really a util
if (LLVM_ADD_NATIVE_VISUALIZERS_TO_SOLUTION)
  add_subdirectory(utils/LLVMVisualizers)
endif()

foreach( binding ${LLVM_BINDINGS_LIST} )
  if( EXISTS "${LLVM_MAIN_SRC_DIR}/bindings/${binding}/CMakeLists.txt" )
    add_subdirectory(bindings/${binding})
  endif()
endforeach()

add_subdirectory(projects)

if( LLVM_INCLUDE_TOOLS )
  add_subdirectory(tools)
endif()

if( LLVM_INCLUDE_RUNTIMES )
  add_subdirectory(runtimes)
endif()

if( LLVM_INCLUDE_EXAMPLES )
  add_subdirectory(examples)
endif()

if( LLVM_INCLUDE_TESTS )
  set(LLVM_GTEST_RUN_UNDER
    "" CACHE STRING
    "Define the wrapper program that LLVM unit tests should be run under.")
  if(EXISTS ${LLVM_MAIN_SRC_DIR}/projects/test-suite AND TARGET clang)
    include(LLVMExternalProjectUtils)
    llvm_ExternalProject_Add(test-suite ${LLVM_MAIN_SRC_DIR}/projects/test-suite
      USE_TOOLCHAIN
      EXCLUDE_FROM_ALL
      NO_INSTALL
      ALWAYS_CLEAN)
  endif()
  add_subdirectory(utils/lit)
  add_subdirectory(test)
  add_subdirectory(unittests)

  if (WIN32)
    # This utility is used to prevent crashing tests from calling Dr. Watson on
    # Windows.
    add_subdirectory(utils/KillTheDoctor)
  endif()

  umbrella_lit_testsuite_end(check-all)
  get_property(LLVM_ALL_LIT_DEPENDS GLOBAL PROPERTY LLVM_ALL_LIT_DEPENDS)
  get_property(LLVM_ALL_ADDITIONAL_TEST_DEPENDS
      GLOBAL PROPERTY LLVM_ALL_ADDITIONAL_TEST_DEPENDS)
  add_custom_target(test-depends)
  if(LLVM_ALL_LIT_DEPENDS OR LLVM_ALL_ADDITIONAL_TEST_DEPENDS)
    add_dependencies(test-depends ${LLVM_ALL_LIT_DEPENDS} ${LLVM_ALL_ADDITIONAL_TEST_DEPENDS})
  endif()
  set_target_properties(test-depends PROPERTIES FOLDER "LLVM/Tests")
  add_dependencies(check-all test-depends)
endif()

if (LLVM_INCLUDE_DOCS)
  add_subdirectory(docs)
endif()

add_subdirectory(cmake/modules)

# Do this last so that all lit targets have already been created.
if (LLVM_INCLUDE_UTILS)
  add_subdirectory(utils/llvm-lit)
endif()

if (NOT LLVM_INSTALL_TOOLCHAIN_ONLY)
  install(DIRECTORY include/llvm include/llvm-c
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    COMPONENT llvm-headers
    FILES_MATCHING
    PATTERN "*.def"
    PATTERN "*.h"
    PATTERN "*.td"
    PATTERN "*.inc"
    PATTERN "LICENSE.TXT"
    )

  install(DIRECTORY ${LLVM_INCLUDE_DIR}/llvm ${LLVM_INCLUDE_DIR}/llvm-c
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    COMPONENT llvm-headers
    FILES_MATCHING
    PATTERN "*.def"
    PATTERN "*.h"
    PATTERN "*.gen"
    PATTERN "*.inc"
    # Exclude include/llvm/CMakeFiles/intrinsics_gen.dir, matched by "*.def"
    PATTERN "CMakeFiles" EXCLUDE
    PATTERN "config.h" EXCLUDE
    )

  if (LLVM_INSTALL_MODULEMAPS)
    install(DIRECTORY include
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
            COMPONENT llvm-headers
            FILES_MATCHING
            PATTERN "module.modulemap"
            )
    install(FILES include/module.install.modulemap
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
            COMPONENT llvm-headers
            RENAME "module.extern.modulemap"
            )
  endif(LLVM_INSTALL_MODULEMAPS)

  # Installing the headers needs to depend on generating any public
  # tablegen'd headers.
  add_custom_target(llvm-headers DEPENDS intrinsics_gen omp_gen)
  set_target_properties(llvm-headers PROPERTIES FOLDER "LLVM/Resources")

  if (NOT LLVM_ENABLE_IDE)
    add_llvm_install_targets(install-llvm-headers
                             DEPENDS llvm-headers
                             COMPONENT llvm-headers)
  endif()

  # Custom target to install all libraries.
  add_custom_target(llvm-libraries)
  set_target_properties(llvm-libraries PROPERTIES FOLDER "LLVM/Resources")

  if (NOT LLVM_ENABLE_IDE)
    add_llvm_install_targets(install-llvm-libraries
                             DEPENDS llvm-libraries
                             COMPONENT llvm-libraries)
  endif()

  get_property(LLVM_LIBS GLOBAL PROPERTY LLVM_LIBS)
  if(LLVM_LIBS)
    list(REMOVE_DUPLICATES LLVM_LIBS)
    foreach(lib ${LLVM_LIBS})
      add_dependencies(llvm-libraries ${lib})
      if (NOT LLVM_ENABLE_IDE)
        add_dependencies(install-llvm-libraries install-${lib})
        add_dependencies(install-llvm-libraries-stripped install-${lib}-stripped)
      endif()
    endforeach()
  endif()
endif()

# This must be at the end of the LLVM root CMakeLists file because it must run
# after all targets are created.
llvm_distribution_add_targets()
process_llvm_pass_plugins(GEN_CONFIG)
include(CoverageReport)

# This allows us to deploy the Universal CRT DLLs by passing -DCMAKE_INSTALL_UCRT_LIBRARIES=ON to CMake
if (MSVC AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows" AND CMAKE_INSTALL_UCRT_LIBRARIES)
  include(InstallRequiredSystemLibraries)
endif()

if (LLVM_INCLUDE_BENCHMARKS)
  # Override benchmark defaults so that when the library itself is updated these
  # modifications are not lost.
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Disable benchmark testing" FORCE)
  set(BENCHMARK_ENABLE_EXCEPTIONS OFF CACHE BOOL "Disable benchmark exceptions" FORCE)
  set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "Don't install benchmark" FORCE)
  set(BENCHMARK_DOWNLOAD_DEPENDENCIES OFF CACHE BOOL "Don't download dependencies" FORCE)
  set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "Disable Google Test in benchmark" FORCE)
  set(BENCHMARK_ENABLE_WERROR ${LLVM_ENABLE_WERROR} CACHE BOOL
    "Handle -Werror for Google Benchmark based on LLVM_ENABLE_WERROR" FORCE)
  # Since LLVM requires C++11 it is safe to assume that std::regex is available.
  set(HAVE_STD_REGEX ON CACHE BOOL "OK" FORCE)
  add_subdirectory(${LLVM_THIRD_PARTY_DIR}/benchmark
    ${CMAKE_CURRENT_BINARY_DIR}/third-party/benchmark)
  set_target_properties(benchmark PROPERTIES FOLDER "Third-Party/Google Benchmark")
  set_target_properties(benchmark_main PROPERTIES FOLDER "Third-Party/Google Benchmark")
  add_subdirectory(benchmarks)
endif()

if (LLVM_INCLUDE_UTILS AND LLVM_INCLUDE_TOOLS)
  add_subdirectory(utils/llvm-locstats)
endif()
