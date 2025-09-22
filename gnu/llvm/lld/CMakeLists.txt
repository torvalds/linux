cmake_minimum_required(VERSION 3.20.0)
set(LLVM_SUBPROJECT_TITLE "LLD")

if(NOT DEFINED LLVM_COMMON_CMAKE_UTILS)
  set(LLVM_COMMON_CMAKE_UTILS ${CMAKE_CURRENT_SOURCE_DIR}/../cmake)
endif()
include(${LLVM_COMMON_CMAKE_UTILS}/Modules/CMakePolicy.cmake
  NO_POLICY_SCOPE)

# If we are not building as a part of LLVM, build LLD as an
# standalone project, using LLVM as an external library:
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(lld)
  set(LLD_BUILT_STANDALONE TRUE)
endif()

# Must go below project(..)
include(GNUInstallDirs)

if(LLD_BUILT_STANDALONE)
  set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ standard to conform to")
  set(CMAKE_CXX_STANDARD_REQUIRED YES)
  set(CMAKE_CXX_EXTENSIONS NO)

  set(CMAKE_INCLUDE_CURRENT_DIR ON)

  find_package(LLVM REQUIRED HINTS "${LLVM_CMAKE_DIR}")
  list(APPEND CMAKE_MODULE_PATH "${LLVM_DIR}")

  # Turn into CACHE PATHs for overwriting
  set(LLVM_INCLUDE_DIRS ${LLVM_INCLUDE_DIRS} CACHE PATH "Path to llvm/include and any other header dirs needed")
  set(LLVM_BINARY_DIR "${LLVM_BINARY_DIR}" CACHE PATH "Path to LLVM build tree")
  set(LLVM_MAIN_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../llvm" CACHE PATH "Path to LLVM source tree")

  find_program(LLVM_TABLEGEN_EXE "llvm-tblgen" ${LLVM_TOOLS_BINARY_DIR}
    NO_DEFAULT_PATH)

  # They are used as destination of target generators.
  set(LLVM_RUNTIME_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR}/bin)
  set(LLVM_LIBRARY_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR}/lib${LLVM_LIBDIR_SUFFIX})

  include(AddLLVM)
  include(TableGen)
  include(HandleLLVMOptions)
  include(GetErrcMessages)
  include(CheckAtomic)

  set(PACKAGE_VERSION "${LLVM_PACKAGE_VERSION}")

  include_directories(${LLVM_INCLUDE_DIRS})
  link_directories(${LLVM_LIBRARY_DIRS})

  if(LLVM_INCLUDE_TESTS)
    find_package(Python3 ${LLVM_MINIMUM_PYTHON_VERSION} REQUIRED
      COMPONENTS Interpreter)

    # Check prebuilt llvm/utils.
    if(EXISTS ${LLVM_TOOLS_BINARY_DIR}/FileCheck${CMAKE_EXECUTABLE_SUFFIX}
        AND EXISTS ${LLVM_TOOLS_BINARY_DIR}/not${CMAKE_EXECUTABLE_SUFFIX})
      set(LLVM_UTILS_PROVIDED ON)
    endif()

    if(EXISTS ${LLVM_MAIN_SRC_DIR}/utils/lit/lit.py)
      # Note: path not really used, except for checking if lit was found
      set(LLVM_EXTERNAL_LIT ${LLVM_MAIN_SRC_DIR}/utils/lit/lit.py)
      if(NOT LLVM_UTILS_PROVIDED)
        add_subdirectory(${LLVM_MAIN_SRC_DIR}/utils/FileCheck utils/FileCheck)
        add_subdirectory(${LLVM_MAIN_SRC_DIR}/utils/not utils/not)
        set(LLVM_UTILS_PROVIDED ON)
        set(LLD_TEST_DEPS FileCheck not)
      endif()

      if (NOT TARGET llvm_gtest)
        message(FATAL_ERROR "llvm-gtest not found. Please install llvm-gtest or disable tests with -DLLVM_INCLUDE_TESTS=OFF")
      endif()
    else()
      # Seek installed Lit.
      find_program(LLVM_EXTERNAL_LIT
                   NAMES llvm-lit lit.py lit
                   PATHS "${LLVM_MAIN_SRC_DIR}/utils/lit"
                   DOC "Path to lit.py")
    endif()

    if(LLVM_EXTERNAL_LIT)
      # Define the default arguments to use with 'lit', and an option for the user
      # to override.
      set(LIT_ARGS_DEFAULT "-sv")
      if (MSVC OR XCODE)
        set(LIT_ARGS_DEFAULT "${LIT_ARGS_DEFAULT} --no-progress-bar")
      endif()
      set(LLVM_LIT_ARGS "${LIT_ARGS_DEFAULT}" CACHE STRING "Default options for lit")

      get_errc_messages(LLVM_LIT_ERRC_MESSAGES)

      # On Win32 hosts, provide an option to specify the path to the GnuWin32 tools.
      if(WIN32 AND NOT CYGWIN)
        set(LLVM_LIT_TOOLS_DIR "" CACHE PATH "Path to GnuWin32 tools")
      endif()
    else()
      set(LLVM_INCLUDE_TESTS OFF)
    endif()
  endif()
endif() # standalone

set(LLD_TOOLS_INSTALL_DIR "${CMAKE_INSTALL_BINDIR}" CACHE PATH
    "Path for binary subdirectory (defaults to '${CMAKE_INSTALL_BINDIR}')")
mark_as_advanced(LLD_TOOLS_INSTALL_DIR)

set(LLD_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(LLD_INCLUDE_DIR ${LLD_SOURCE_DIR}/include )
set(LLD_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})

set(LLD_VENDOR ${PACKAGE_VENDOR} CACHE STRING
  "Vendor-specific text for showing with version information.")

if(LLD_VENDOR)
  add_definitions(-DLLD_VENDOR="${LLD_VENDOR}")
endif()

# Compute the LLD version from the LLVM version.
string(REGEX MATCH "[0-9]+\\.[0-9]+(\\.[0-9]+)?" LLD_VERSION
  ${PACKAGE_VERSION})
message(STATUS "LLD version: ${LLD_VERSION}")

string(REGEX REPLACE "([0-9]+)\\.[0-9]+(\\.[0-9]+)?" "\\1" LLD_VERSION_MAJOR
  ${LLD_VERSION})
string(REGEX REPLACE "[0-9]+\\.([0-9]+)(\\.[0-9]+)?" "\\1" LLD_VERSION_MINOR
  ${LLD_VERSION})

# Configure the Version.inc file.
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/include/lld/Common/Version.inc.in
  ${CMAKE_CURRENT_BINARY_DIR}/include/lld/Common/Version.inc)


if (CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not allowed. CMake would overwrite "
"the makefiles distributed with LLVM. Please create a directory and run cmake "
"from there, passing the path to this source directory as the last argument. "
"This process created the file `CMakeCache.txt' and the directory "
"`CMakeFiles'. Please delete them.")
endif()

# Add path for custom modules.
list(INSERT CMAKE_MODULE_PATH 0
  "${LLD_SOURCE_DIR}/cmake/modules"
  "${LLVM_COMMON_CMAKE_UTILS}/Modules"
  )

include(AddLLD)

option(LLD_ENABLE_WASM
       "Enable support for WASM."
       OFF)
if (LLD_ENABLE_WASM)
  add_definitions("-DLLD_ENABLE_WASM=1")
endif()

option(LLD_ENABLE_MACHO
       "Enable support for MachO."
       OFF)
if (LLD_ENABLE_MACHO)
  add_definitions("-DLLD_ENABLE_MACHO=1")
endif()

option(LLD_ENABLE_MINGW
       "Enable support for MINGW."
       OFF)
if (LLD_ENABLE_MINGW)
  add_definitions("-DLLD_ENABLE_MINGW=1")
endif()

option(LLD_ENABLE_COFF
       "Enable support for COFF."
       OFF)
if (LLD_ENABLE_COFF)
  add_definitions("-DLLD_ENABLE_COFF=1")
endif()

option(LLD_USE_VTUNE
       "Enable VTune user task tracking."
       OFF)
if (LLD_USE_VTUNE)
  find_package(VTune)
  if (VTUNE_FOUND)
    include_directories(${VTune_INCLUDE_DIRS})
    list(APPEND LLVM_COMMON_LIBS ${VTune_LIBRARIES})
    add_definitions(-DLLD_HAS_VTUNE)
  endif()
endif()

option(LLD_BUILD_TOOLS
  "Build the lld tools. If OFF, just generate build targets." ON)

option(LLD_DEFAULT_LD_LLD_IS_MINGW
    "Use MinGW as the default backend for ld.lld. If OFF, ELF will be used." OFF)
if (LLD_DEFAULT_LD_LLD_IS_MINGW)
  add_definitions("-DLLD_DEFAULT_LD_LLD_IS_MINGW=1")
endif()

if (MSVC)
  add_definitions(-wd4530) # Suppress 'warning C4530: C++ exception handler used, but unwind semantics are not enabled.'
  add_definitions(-wd4062) # Suppress 'warning C4062: enumerator X in switch of enum Y is not handled' from system header.
endif()

include_directories(BEFORE
  ${CMAKE_CURRENT_BINARY_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  )

if (NOT LLVM_INSTALL_TOOLCHAIN_ONLY)
  install(DIRECTORY include/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING
    PATTERN "*.h"
    )
endif()

add_subdirectory(Common)
add_subdirectory(tools/lld)

if (LLVM_INCLUDE_TESTS)
  add_custom_target(LLDUnitTests)
  set_target_properties(LLDUnitTests PROPERTIES FOLDER "LLD/Tests")
  if (TARGET llvm_gtest)
    add_subdirectory(unittests)
  endif()
  add_subdirectory(test)
endif()

add_subdirectory(docs)
add_subdirectory(COFF)
add_subdirectory(ELF)
if (LLD_ENABLE_MACHO)
  add_subdirectory(MachO)
endif()
add_subdirectory(MinGW)
add_subdirectory(wasm)

add_subdirectory(cmake/modules)
