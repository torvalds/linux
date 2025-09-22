cmake_minimum_required(VERSION 3.20.0)
set(LLVM_SUBPROJECT_TITLE "Clang")

if(NOT DEFINED LLVM_COMMON_CMAKE_UTILS)
  set(LLVM_COMMON_CMAKE_UTILS ${CMAKE_CURRENT_SOURCE_DIR}/../cmake)
endif()
include(${LLVM_COMMON_CMAKE_UTILS}/Modules/CMakePolicy.cmake
  NO_POLICY_SCOPE)

# If we are not building as a part of LLVM, build Clang as an
# standalone project, using LLVM as an external library:
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(Clang)
  set(CLANG_BUILT_STANDALONE TRUE)
endif()

# Make sure that our source directory is on the current cmake module path so that
# we can include cmake files from this directory.
list(INSERT CMAKE_MODULE_PATH 0
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules"
  "${LLVM_COMMON_CMAKE_UTILS}/Modules"
  )

# Must go below project(..)
include(GNUInstallDirs)
include(GetDarwinLinkerVersion)

if(CLANG_BUILT_STANDALONE)
  set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ standard to conform to")
  set(CMAKE_CXX_STANDARD_REQUIRED YES)
  set(CMAKE_CXX_EXTENSIONS NO)

  if(NOT MSVC_IDE)
    set(LLVM_ENABLE_ASSERTIONS ${ENABLE_ASSERTIONS}
      CACHE BOOL "Enable assertions")
    # Assertions should follow llvm-config's.
    mark_as_advanced(LLVM_ENABLE_ASSERTIONS)
  endif()

  find_package(LLVM REQUIRED HINTS "${LLVM_CMAKE_DIR}")
  list(APPEND CMAKE_MODULE_PATH "${LLVM_DIR}")

  # Turn into CACHE PATHs for overwritting
  set(LLVM_INCLUDE_DIRS ${LLVM_INCLUDE_DIRS} CACHE PATH "Path to llvm/include and any other header dirs needed")
  set(LLVM_BINARY_DIR "${LLVM_BINARY_DIR}" CACHE PATH "Path to LLVM build tree")
  set(LLVM_MAIN_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../llvm" CACHE PATH "Path to LLVM source tree")
  set(LLVM_TOOLS_BINARY_DIR "${LLVM_TOOLS_BINARY_DIR}" CACHE PATH "Path to llvm/bin")
  set(LLVM_LIBRARY_DIR "${LLVM_LIBRARY_DIR}" CACHE PATH "Path to llvm/lib")

  find_program(LLVM_TABLEGEN_EXE "llvm-tblgen" ${LLVM_TOOLS_BINARY_DIR}
    NO_DEFAULT_PATH)

  # They are used as destination of target generators.
  set(LLVM_RUNTIME_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR}/bin)
  set(LLVM_LIBRARY_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR}/lib${LLVM_LIBDIR_SUFFIX})
  if(WIN32 OR CYGWIN)
    # DLL platform -- put DLLs into bin.
    set(LLVM_SHLIB_OUTPUT_INTDIR ${LLVM_RUNTIME_OUTPUT_INTDIR})
  else()
    set(LLVM_SHLIB_OUTPUT_INTDIR ${LLVM_LIBRARY_OUTPUT_INTDIR})
  endif()

  option(LLVM_INSTALL_TOOLCHAIN_ONLY
    "Only include toolchain files in the 'install' target." OFF)

  option(LLVM_FORCE_USE_OLD_TOOLCHAIN
    "Set to ON to force using an old, unsupported host toolchain." OFF)
  option(CLANG_ENABLE_BOOTSTRAP "Generate the clang bootstrap target" OFF)
  option(LLVM_ENABLE_LIBXML2 "Use libxml2 if available." ON)

  include(AddLLVM)
  include(TableGen)
  include(HandleLLVMOptions)
  include(VersionFromVCS)
  include(CheckAtomic)
  include(GetErrcMessages)
  include(LLVMDistributionSupport)

  set(PACKAGE_VERSION "${LLVM_PACKAGE_VERSION}")
  set(BUG_REPORT_URL "${LLVM_PACKAGE_BUGREPORT}" CACHE STRING
    "Default URL where bug reports are to be submitted.")

  if (NOT DEFINED LLVM_INCLUDE_TESTS)
    set(LLVM_INCLUDE_TESTS ON)
  endif()

  include_directories(${LLVM_INCLUDE_DIRS})
  link_directories("${LLVM_LIBRARY_DIR}")

  set( CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin )
  set( CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib${LLVM_LIBDIR_SUFFIX} )
  set( CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib${LLVM_LIBDIR_SUFFIX} )

  find_package(Python3 ${LLVM_MINIMUM_PYTHON_VERSION} REQUIRED
    COMPONENTS Interpreter)

  if(LLVM_INCLUDE_TESTS)
    # Check prebuilt llvm/utils.
    if(EXISTS ${LLVM_TOOLS_BINARY_DIR}/FileCheck${CMAKE_EXECUTABLE_SUFFIX}
        AND EXISTS ${LLVM_TOOLS_BINARY_DIR}/count${CMAKE_EXECUTABLE_SUFFIX}
        AND EXISTS ${LLVM_TOOLS_BINARY_DIR}/not${CMAKE_EXECUTABLE_SUFFIX})
      set(LLVM_UTILS_PROVIDED ON)
    endif()

    # Seek installed Lit.
    find_program(LLVM_LIT
                 NAMES llvm-lit lit.py lit
                 PATHS "${LLVM_MAIN_SRC_DIR}/utils/lit"
                 DOC "Path to lit.py")

    if(EXISTS ${LLVM_MAIN_SRC_DIR}/utils/lit/lit.py)
      # Note: path not really used, except for checking if lit was found
      if(EXISTS ${LLVM_MAIN_SRC_DIR}/utils/llvm-lit)
        add_subdirectory(${LLVM_MAIN_SRC_DIR}/utils/llvm-lit utils/llvm-lit)
      endif()
      if(NOT LLVM_UTILS_PROVIDED)
        add_subdirectory(${LLVM_MAIN_SRC_DIR}/utils/FileCheck utils/FileCheck)
        add_subdirectory(${LLVM_MAIN_SRC_DIR}/utils/count utils/count)
        add_subdirectory(${LLVM_MAIN_SRC_DIR}/utils/not utils/not)
        set(LLVM_UTILS_PROVIDED ON)
        set(CLANG_TEST_DEPS FileCheck count not)
      endif()
    endif()

    if (NOT TARGET llvm_gtest)
        message(FATAL_ERROR "llvm-gtest not found. Please install llvm-gtest or disable tests with -DLLVM_INCLUDE_TESTS=OFF")
    endif()

    if(LLVM_LIT)
      # Define the default arguments to use with 'lit', and an option for the user
      # to override.
      set(LIT_ARGS_DEFAULT "-sv")
      if (MSVC OR XCODE)
        set(LIT_ARGS_DEFAULT "${LIT_ARGS_DEFAULT} --no-progress-bar")
      endif()
      set(LLVM_LIT_ARGS "${LIT_ARGS_DEFAULT}" CACHE STRING "Default options for lit")

      get_errc_messages(LLVM_LIT_ERRC_MESSAGES)

      # On Win32 hosts, provide an option to specify the path to the GnuWin32 tools.
      if( WIN32 AND NOT CYGWIN )
        set(LLVM_LIT_TOOLS_DIR "" CACHE PATH "Path to GnuWin32 tools")
      endif()
    else()
      set(LLVM_INCLUDE_TESTS OFF)
    endif()

    umbrella_lit_testsuite_begin(check-all)
  endif() # LLVM_INCLUDE_TESTS
endif() # standalone

# This allows disabling clang's XML dependency even if LLVM finds libxml2.
# By default, clang depends on libxml2 if LLVM does.
option(CLANG_ENABLE_LIBXML2 "Whether libclang may depend on libxml2"
       ${LLVM_ENABLE_LIBXML2})

if(CLANG_ENABLE_LIBXML2)
  # Don't look for libxml if we're using MSan, since uninstrumented third party
  # code may call MSan interceptors like strlen, leading to false positives.
  if(NOT LLVM_USE_SANITIZER MATCHES "Memory.*")
    set (LIBXML2_FOUND 0)
    find_package(LibXml2 2.5.3 QUIET)
    if (LIBXML2_FOUND)
      set(CLANG_HAVE_LIBXML 1)
    endif()
  endif()
endif()

if(CLANG_ENABLE_CIR)
  if (CLANG_BUILT_STANDALONE)
    message(FATAL_ERROR
      "ClangIR is not yet supported in the standalone build.")
  endif()
  if (NOT "${LLVM_ENABLE_PROJECTS}" MATCHES "MLIR|mlir")
    message(FATAL_ERROR
      "Cannot build ClangIR without MLIR in LLVM_ENABLE_PROJECTS")
  endif()
endif()

include(CheckIncludeFile)
check_include_file(sys/resource.h CLANG_HAVE_RLIMITS)

# This check requires _GNU_SOURCE on linux
check_include_file(dlfcn.h CLANG_HAVE_DLFCN_H)
if( CLANG_HAVE_DLFCN_H )
  include(CheckLibraryExists)
  include(CheckSymbolExists)
  check_library_exists(dl dlopen "" HAVE_LIBDL)
  if( HAVE_LIBDL )
    list(APPEND CMAKE_REQUIRED_LIBRARIES dl)
  endif()
  list(APPEND CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
  check_symbol_exists(dladdr dlfcn.h CLANG_HAVE_DLADDR)
  list(REMOVE_ITEM CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
  if( HAVE_LIBDL )
    list(REMOVE_ITEM CMAKE_REQUIRED_LIBRARIES dl)
  endif()
endif()

set(CLANG_RESOURCE_DIR "" CACHE STRING
  "Relative directory from the Clang binary to its resource files.")

set(C_INCLUDE_DIRS "" CACHE STRING
  "Colon separated list of directories clang will search for headers.")

set(USE_DEPRECATED_GCC_INSTALL_PREFIX OFF CACHE BOOL "Temporary workaround before GCC_INSTALL_PREFIX is completely removed")
set(GCC_INSTALL_PREFIX "" CACHE PATH "Directory where gcc is installed." )
set(DEFAULT_SYSROOT "" CACHE STRING
  "Default <path> to all compiler invocations for --sysroot=<path>." )
if(GCC_INSTALL_PREFIX AND NOT USE_DEPRECATED_GCC_INSTALL_PREFIX)
  message(FATAL_ERROR "GCC_INSTALL_PREFIX is deprecated and will be removed. Use "
    "configuration files (https://clang.llvm.org/docs/UsersManual.html#configuration-files)"
    "to specify the default --gcc-install-dir= or --gcc-triple=. --gcc-toolchain= is discouraged. "
    "See https://github.com/llvm/llvm-project/pull/77537 for detail.")
endif()

set(ENABLE_LINKER_BUILD_ID OFF CACHE BOOL "pass --build-id to ld")

set(ENABLE_X86_RELAX_RELOCATIONS ON CACHE BOOL
    "enable x86 relax relocations by default")

set(PPC_LINUX_DEFAULT_IEEELONGDOUBLE OFF CACHE BOOL
    "Enable IEEE binary128 as default long double format on PowerPC Linux.")

set(CLANG_SPAWN_CC1 OFF CACHE BOOL
    "Whether clang should use a new process for the CC1 invocation")

option(CLANG_DEFAULT_PIE_ON_LINUX "Default to -fPIE and -pie on linux-gnu" ON)

set(CLANG_DEFAULT_LINKER "" CACHE STRING
  "Default linker to use (linker name or absolute path, empty for platform default)")

set(CLANG_DEFAULT_CXX_STDLIB "" CACHE STRING
  "Default C++ stdlib to use (\"libstdc++\" or \"libc++\", empty for platform default")
if (NOT(CLANG_DEFAULT_CXX_STDLIB STREQUAL "" OR
        CLANG_DEFAULT_CXX_STDLIB STREQUAL "libstdc++" OR
        CLANG_DEFAULT_CXX_STDLIB STREQUAL "libc++"))
  message(WARNING "Resetting default C++ stdlib to use platform default")
  set(CLANG_DEFAULT_CXX_STDLIB "" CACHE STRING
    "Default C++ stdlib to use (\"libstdc++\" or \"libc++\", empty for platform default" FORCE)
endif()

set(CLANG_DEFAULT_RTLIB "" CACHE STRING
  "Default runtime library to use (\"libgcc\" or \"compiler-rt\", empty for platform default)")
if (NOT(CLANG_DEFAULT_RTLIB STREQUAL "" OR
        CLANG_DEFAULT_RTLIB STREQUAL "libgcc" OR
        CLANG_DEFAULT_RTLIB STREQUAL "compiler-rt"))
  message(WARNING "Resetting default rtlib to use platform default")
  set(CLANG_DEFAULT_RTLIB "" CACHE STRING
    "Default runtime library to use (\"libgcc\" or \"compiler-rt\", empty for platform default)" FORCE)
endif()

set(CLANG_DEFAULT_UNWINDLIB "" CACHE STRING
  "Default unwind library to use (\"none\" \"libgcc\" or \"libunwind\", empty to match runtime library.)")
if (CLANG_DEFAULT_UNWINDLIB STREQUAL "")
  if (CLANG_DEFAULT_RTLIB STREQUAL "libgcc")
    set (CLANG_DEFAULT_UNWINDLIB "libgcc" CACHE STRING "" FORCE)
  endif()
endif()

if (NOT(CLANG_DEFAULT_UNWINDLIB STREQUAL "" OR
        CLANG_DEFAULT_UNWINDLIB STREQUAL "none" OR
        CLANG_DEFAULT_UNWINDLIB STREQUAL "libgcc" OR
        CLANG_DEFAULT_UNWINDLIB STREQUAL "libunwind"))
  message(WARNING "Resetting default unwindlib to use platform default")
  set(CLANG_DEFAULT_UNWINDLIB "" CACHE STRING
    "Default unwind library to use (\"none\" \"libgcc\" or \"libunwind\", empty to match runtime library.)" FORCE)
endif()

set(CLANG_DEFAULT_OBJCOPY "objcopy" CACHE STRING
  "Default objcopy executable to use.")

set(CLANG_DEFAULT_OPENMP_RUNTIME "libomp" CACHE STRING
  "Default OpenMP runtime used by -fopenmp.")

set(CLANG_SYSTEMZ_DEFAULT_ARCH "z10" CACHE STRING "SystemZ Default Arch")

set(CLANG_VENDOR ${PACKAGE_VENDOR} CACHE STRING
  "Vendor-specific text for showing with version information.")

set(CLANG_REPOSITORY_STRING "" CACHE STRING
  "Vendor-specific text for showing the repository the source is taken from.")

if(CLANG_REPOSITORY_STRING)
  add_definitions(-DCLANG_REPOSITORY_STRING="${CLANG_REPOSITORY_STRING}")
endif()

set(CLANG_VENDOR_UTI "org.llvm.clang" CACHE STRING
  "Vendor-specific uti.")

set(CLANG_PYTHON_BINDINGS_VERSIONS "" CACHE STRING
    "Python versions to install libclang python bindings for")

set(CLANG_LINK_CLANG_DYLIB ${LLVM_LINK_LLVM_DYLIB} CACHE BOOL
    "Link tools against libclang-cpp.so")

if (NOT LLVM_LINK_LLVM_DYLIB AND CLANG_LINK_CLANG_DYLIB)
  message(FATAL_ERROR "Cannot set CLANG_LINK_CLANG_DYLIB=ON when "
                      "LLVM_LINK_LLVM_DYLIB=OFF")
endif()

# The libdir suffix must exactly match whatever LLVM's configuration used.
set(CLANG_LIBDIR_SUFFIX "${LLVM_LIBDIR_SUFFIX}")

set(CLANG_TOOLS_INSTALL_DIR "${CMAKE_INSTALL_BINDIR}" CACHE PATH
    "Path for binary subdirectory (defaults to '${CMAKE_INSTALL_BINDIR}')")
mark_as_advanced(CLANG_TOOLS_INSTALL_DIR)

set(CLANG_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(CLANG_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})

if( CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR AND NOT MSVC_IDE )
  message(FATAL_ERROR "In-source builds are not allowed. "
"Please create a directory and run cmake "
"from there, passing the path to this source directory as the last argument. "
"This process created the file `CMakeCache.txt' and the directory "
"`CMakeFiles'. Please delete them.")
endif()

# If CLANG_VERSION_* is specified, use it, if not use LLVM_VERSION_*.
if(NOT DEFINED CLANG_VERSION_MAJOR)
  set(CLANG_VERSION_MAJOR ${LLVM_VERSION_MAJOR})
endif()
if(NOT DEFINED CLANG_VERSION_MINOR)
  set(CLANG_VERSION_MINOR ${LLVM_VERSION_MINOR})
endif()
if(NOT DEFINED CLANG_VERSION_PATCHLEVEL)
  set(CLANG_VERSION_PATCHLEVEL ${LLVM_VERSION_PATCH})
endif()
if(NOT DEFINED CLANG_VERSION_SUFFIX)
  set(CLANG_VERSION_SUFFIX ${LLVM_VERSION_SUFFIX})
endif()
set(CLANG_VERSION "${CLANG_VERSION_MAJOR}.${CLANG_VERSION_MINOR}.${CLANG_VERSION_PATCHLEVEL}${CLANG_VERSION_SUFFIX}")
message(STATUS "Clang version: ${CLANG_VERSION}")

# Configure the Version.inc file.
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/include/clang/Basic/Version.inc.in
  ${CMAKE_CURRENT_BINARY_DIR}/include/clang/Basic/Version.inc)

# Add appropriate flags for GCC
if (LLVM_COMPILER_IS_GCC_COMPATIBLE)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-common -Woverloaded-virtual")
  if (NOT "${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-strict-aliasing")
  endif ()

  # Enable -pedantic for Clang even if it's not enabled for LLVM.
  if (NOT LLVM_ENABLE_PEDANTIC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pedantic -Wno-long-long")
  endif ()

  if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-nested-anon-types" )
  endif ()
endif ()

# Determine HOST_LINK_VERSION on Darwin.
set(HOST_LINK_VERSION)
if (APPLE AND NOT CMAKE_LINKER MATCHES ".*lld.*")
  get_darwin_linker_version(HOST_LINK_VERSION)
  message(STATUS "Host linker version: ${HOST_LINK_VERSION}")
endif()

include(CMakeParseArguments)
include(AddClang)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(BEFORE
  ${CMAKE_CURRENT_BINARY_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  )

if (NOT LLVM_INSTALL_TOOLCHAIN_ONLY)
  install(DIRECTORY include/clang include/clang-c
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    COMPONENT clang-headers
    FILES_MATCHING
    PATTERN "*.def"
    PATTERN "*.h"
    PATTERN "config.h" EXCLUDE
    )

  install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include/clang
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    COMPONENT clang-headers
    FILES_MATCHING
    PATTERN "CMakeFiles" EXCLUDE
    PATTERN "*.inc"
    PATTERN "*.h"
    )

  # Installing the headers needs to depend on generating any public
  # tablegen'd headers.
  add_custom_target(clang-headers DEPENDS clang-tablegen-targets)
  set_target_properties(clang-headers PROPERTIES FOLDER "Clang/Resources")
  if(NOT LLVM_ENABLE_IDE)
    add_llvm_install_targets(install-clang-headers
                             DEPENDS clang-headers
                             COMPONENT clang-headers)
  endif()

  add_custom_target(bash-autocomplete DEPENDS utils/bash-autocomplete.sh)
  set_target_properties(bash-autocomplete PROPERTIES FOLDER "Clang/Misc")
  install(FILES utils/bash-autocomplete.sh
          DESTINATION "${CMAKE_INSTALL_DATADIR}/clang"
          COMPONENT bash-autocomplete)
  if(NOT LLVM_ENABLE_IDE)
    add_llvm_install_targets(install-bash-autocomplete
                             DEPENDS bash-autocomplete
                             COMPONENT bash-autocomplete)
  endif()
endif()

option(CLANG_BUILD_TOOLS
  "Build the Clang tools. If OFF, just generate build targets." ON)

if(LLVM_ENABLE_PLUGINS OR LLVM_EXPORT_SYMBOLS_FOR_PLUGINS)
  set(HAVE_CLANG_PLUGIN_SUPPORT ON)
else()
  set(HAVE_CLANG_PLUGIN_SUPPORT OFF)
endif()
CMAKE_DEPENDENT_OPTION(CLANG_PLUGIN_SUPPORT
  "Build clang with plugin support" ON
  "HAVE_CLANG_PLUGIN_SUPPORT" OFF)

# If libstdc++ is statically linked, clang-repl needs to statically link libstdc++
# itself, which is not possible in many platforms because of current limitations in
# JIT stack. (more platforms need to be supported by JITLink)
if(NOT LLVM_STATIC_LINK_CXX_STDLIB)
  set(HAVE_CLANG_REPL_SUPPORT ON)
endif()

option(CLANG_ENABLE_ARCMT "Build ARCMT." ON)
option(CLANG_ENABLE_STATIC_ANALYZER
  "Include static analyzer in clang binary." ON)

option(CLANG_ENABLE_PROTO_FUZZER "Build Clang protobuf fuzzer." OFF)

if(NOT CLANG_ENABLE_STATIC_ANALYZER AND CLANG_ENABLE_ARCMT)
  message(FATAL_ERROR "Cannot disable static analyzer while enabling ARCMT or Z3")
endif()

if(CLANG_ENABLE_ARCMT)
  set(CLANG_ENABLE_OBJC_REWRITER ON)
endif()

# This option is a stop-gap, we should commit to removing this as
# soon as possible. See discussion:
# https://discourse.llvm.org/t/rationale-for-removing-versioned-libclang-middle-ground-to-keep-it-behind-option/
option(CLANG_FORCE_MATCHING_LIBCLANG_SOVERSION
  "Force the SOVERSION of libclang to be equal to CLANG_MAJOR" ON)

# Clang version information
set(CLANG_EXECUTABLE_VERSION
    "${CLANG_VERSION_MAJOR}" CACHE STRING
    "Major version number that will be appended to the clang executable name")
set(LIBCLANG_LIBRARY_VERSION
    "${CLANG_VERSION_MAJOR}" CACHE STRING
    "Major version number that will be appended to the libclang library")
mark_as_advanced(CLANG_EXECUTABLE_VERSION LIBCLANG_LIBRARY_VERSION)

option(CLANG_INCLUDE_TESTS
       "Generate build targets for the Clang unit tests."
       ${LLVM_INCLUDE_TESTS})

option(CLANG_ENABLE_HLSL "Include HLSL build products" Off)
# While HLSL support is experimental this should stay hidden.
mark_as_advanced(CLANG_ENABLE_HLSL)

add_subdirectory(utils/TableGen)

# Export CLANG_TABLEGEN_EXE for use by flang docs.
set(CLANG_TABLEGEN_EXE "${CLANG_TABLEGEN_EXE}" CACHE INTERNAL "")

add_subdirectory(include)

# All targets below may depend on all tablegen'd files.
get_property(CLANG_TABLEGEN_TARGETS GLOBAL PROPERTY CLANG_TABLEGEN_TARGETS)
add_custom_target(clang-tablegen-targets
  DEPENDS
  omp_gen
  ClangDriverOptions
  ${CLANG_TABLEGEN_TARGETS})
set_target_properties(clang-tablegen-targets PROPERTIES FOLDER "Clang/Tablegenning/Targets")
list(APPEND LLVM_COMMON_DEPENDS clang-tablegen-targets)

# Force target to be built as soon as possible. Clang modules builds depend
# header-wise on it as they ship all headers from the umbrella folders. Building
# an entire module might include header, which depends on intrinsics_gen.
if(LLVM_ENABLE_MODULES)
  list(APPEND LLVM_COMMON_DEPENDS intrinsics_gen)
endif()

add_subdirectory(lib)
add_subdirectory(tools)
add_subdirectory(runtime)

option(CLANG_BUILD_EXAMPLES "Build CLANG example programs by default." OFF)
add_subdirectory(examples)

if(APPLE)
  # this line is needed as a cleanup to ensure that any CMakeCaches with the old
  # default value get updated to the new default.
  if(CLANG_ORDER_FILE STREQUAL "")
    unset(CLANG_ORDER_FILE CACHE)
    unset(CLANG_ORDER_FILE)
  endif()


  set(CLANG_ORDER_FILE ${CMAKE_CURRENT_BINARY_DIR}/clang.order CACHE FILEPATH
    "Order file to use when compiling clang in order to improve startup time (Darwin Only - requires ld64).")

  if(NOT EXISTS ${CLANG_ORDER_FILE})
    string(FIND "${CLANG_ORDER_FILE}" "${CMAKE_CURRENT_BINARY_DIR}" PATH_START)
    if(PATH_START EQUAL 0)
      file(WRITE ${CLANG_ORDER_FILE} "\n")
    else()
      message(FATAL_ERROR "Specified order file '${CLANG_ORDER_FILE}' does not exist.")
    endif()
  endif()
endif()


if( CLANG_INCLUDE_TESTS )
  find_package(Perl)

  add_subdirectory(unittests)
  list(APPEND CLANG_TEST_DEPS ClangUnitTests)
  list(APPEND CLANG_TEST_PARAMS
    clang_unit_site_config=${CMAKE_CURRENT_BINARY_DIR}/test/Unit/lit.site.cfg
  )
  add_subdirectory(test)
  add_subdirectory(bindings/python/tests)

  if(CLANG_BUILT_STANDALONE)
    umbrella_lit_testsuite_end(check-all)
  endif()
  add_subdirectory(utils/perf-training)
endif()

option(CLANG_INCLUDE_DOCS "Generate build targets for the Clang docs."
  ${LLVM_INCLUDE_DOCS})
if( CLANG_INCLUDE_DOCS )
  add_subdirectory(docs)
endif()

# Custom target to install all clang libraries.
add_custom_target(clang-libraries)
set_target_properties(clang-libraries PROPERTIES FOLDER "Clang/Install")

if(NOT LLVM_ENABLE_IDE)
  add_llvm_install_targets(install-clang-libraries
                           DEPENDS clang-libraries
                           COMPONENT clang-libraries)
endif()

get_property(CLANG_LIBS GLOBAL PROPERTY CLANG_LIBS)
if(CLANG_LIBS)
  list(REMOVE_DUPLICATES CLANG_LIBS)
  foreach(lib ${CLANG_LIBS})
    add_dependencies(clang-libraries ${lib})
    if(NOT LLVM_ENABLE_IDE)
      add_dependencies(install-clang-libraries install-${lib})
      add_dependencies(install-clang-libraries-stripped install-${lib}-stripped)
    endif()
  endforeach()
endif()

add_subdirectory(cmake/modules)

if(CLANG_STAGE)
  message(STATUS "Setting current clang stage to: ${CLANG_STAGE}")
endif()

if (CLANG_ENABLE_BOOTSTRAP)
  include(ExternalProject)

  add_custom_target(clang-bootstrap-deps DEPENDS clang)

  if(NOT CLANG_STAGE)
    set(CLANG_STAGE stage1)
  endif()

  string(REGEX MATCH "stage([0-9]*)" MATCHED_STAGE "${CLANG_STAGE}")
  if(MATCHED_STAGE)
    if(NOT LLVM_BUILD_INSTRUMENTED)
      math(EXPR STAGE_NUM "${CMAKE_MATCH_1} + 1")
      set(NEXT_CLANG_STAGE stage${STAGE_NUM})
    else()
      set(NEXT_CLANG_STAGE stage${CMAKE_MATCH_1})
    endif()
  else()
    set(NEXT_CLANG_STAGE bootstrap)
  endif()

  if(BOOTSTRAP_LLVM_BUILD_INSTRUMENTED)
    set(NEXT_CLANG_STAGE ${NEXT_CLANG_STAGE}-instrumented)
  endif()
  message(STATUS "Setting next clang stage to: ${NEXT_CLANG_STAGE}")


  set(STAMP_DIR ${CMAKE_CURRENT_BINARY_DIR}/${NEXT_CLANG_STAGE}-stamps/)
  set(BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/${NEXT_CLANG_STAGE}-bins/)

  if(BOOTSTRAP_LLVM_ENABLE_LLD)
    # adding lld to clang-bootstrap-deps without having it enabled in
    # LLVM_ENABLE_PROJECTS just generates a cryptic error message.
    if (NOT "lld" IN_LIST LLVM_ENABLE_PROJECTS)
      message(FATAL_ERROR "LLD is enabled in the bootstrap build, but lld is not in LLVM_ENABLE_PROJECTS")
    endif()
    add_dependencies(clang-bootstrap-deps lld)
  endif()

  if (WIN32)
    # Build llvm-rc and llvm-mt which are needed by the Windows build.
    add_dependencies(clang-bootstrap-deps llvm-rc)
    if(LLVM_ENABLE_LIBXML2)
      add_dependencies(clang-bootstrap-deps llvm-mt)
    endif()
  endif()

  # If the next stage is LTO we need to depend on LTO and possibly lld or LLVMgold
  if(BOOTSTRAP_LLVM_ENABLE_LTO OR LLVM_ENABLE_LTO AND NOT LLVM_BUILD_INSTRUMENTED)
    if(APPLE)
      add_dependencies(clang-bootstrap-deps LTO)
      # on Darwin we need to set DARWIN_LTO_LIBRARY so that -flto will work
      # using the just-built compiler, and we need to override DYLD_LIBRARY_PATH
      # so that the host object file tools will use the just-built libLTO.
      # However if System Integrity Protection is enabled the DYLD variables
      # will be scrubbed from the environment of any base system commands. This
      # includes /bin/sh, which ninja uses when executing build commands. To
      # work around the envar being filtered away we pass it in as a CMake
      # variable, and have LLVM's CMake append the envar to the archiver calls.
      set(LTO_LIBRARY -DDARWIN_LTO_LIBRARY=${LLVM_SHLIB_OUTPUT_INTDIR}/libLTO.dylib
        -DDYLD_LIBRARY_PATH=${LLVM_LIBRARY_OUTPUT_INTDIR})
    elseif(MSVC)
      add_dependencies(clang-bootstrap-deps llvm-lib)
      set(${CLANG_STAGE}_AR -DCMAKE_AR=${LLVM_RUNTIME_OUTPUT_INTDIR}/llvm-lib)
    elseif(NOT WIN32)
      add_dependencies(clang-bootstrap-deps llvm-ar llvm-ranlib)
      if(NOT BOOTSTRAP_LLVM_ENABLE_LLD AND LLVM_BINUTILS_INCDIR)
        add_dependencies(clang-bootstrap-deps LLVMgold)
      endif()
      set(${CLANG_STAGE}_AR -DCMAKE_AR=${LLVM_RUNTIME_OUTPUT_INTDIR}/llvm-ar)
      set(${CLANG_STAGE}_RANLIB -DCMAKE_RANLIB=${LLVM_RUNTIME_OUTPUT_INTDIR}/llvm-ranlib)
    endif()
  endif()

  if(CLANG_BOOTSTRAP_EXTRA_DEPS)
    add_dependencies(clang-bootstrap-deps ${CLANG_BOOTSTRAP_EXTRA_DEPS})
  endif()

  add_custom_target(${NEXT_CLANG_STAGE}-clear
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${NEXT_CLANG_STAGE}-cleared
    )
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${NEXT_CLANG_STAGE}-cleared
    DEPENDS clang-bootstrap-deps
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${STAMP_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${STAMP_DIR}
    COMMENT "Clobberring ${NEXT_CLANG_STAGE} build and stamp directories"
    )

  if(CMAKE_VERBOSE_MAKEFILE)
    set(verbose -DCMAKE_VERBOSE_MAKEFILE=On)
  endif()

  set(_BOOTSTRAP_DEFAULT_PASSTHROUGH
    PACKAGE_VERSION
    PACKAGE_VENDOR
    LLVM_VERSION_MAJOR
    LLVM_VERSION_MINOR
    LLVM_VERSION_PATCH
    CLANG_VERSION_MAJOR
    CLANG_VERSION_MINOR
    CLANG_VERSION_PATCHLEVEL
    CLANG_VERSION_SUFFIX
    CLANG_VENDOR
    LLVM_VERSION_SUFFIX
    LLVM_BINUTILS_INCDIR
    CLANG_REPOSITORY_STRING
    CMAKE_MAKE_PROGRAM
    CMAKE_OSX_ARCHITECTURES
    CMAKE_BUILD_TYPE
    LLVM_ENABLE_PROJECTS
    LLVM_ENABLE_RUNTIMES)

  # We don't need to depend on compiler-rt/libcxx if we're building instrumented
  # because the next stage will use the same compiler used to build this stage.
  if(NOT LLVM_BUILD_INSTRUMENTED)
    if(TARGET compiler-rt)
      add_dependencies(clang-bootstrap-deps compiler-rt)
    endif()
    if(TARGET cxx-headers)
      add_dependencies(clang-bootstrap-deps cxx-headers)
    endif()
  endif()

  set(C_COMPILER "clang")
  set(CXX_COMPILER "clang++")
  if(WIN32)
    set(C_COMPILER "clang-cl.exe")
    set(CXX_COMPILER "clang-cl.exe")
  endif()

  set(COMPILER_OPTIONS
    -DCMAKE_CXX_COMPILER=${LLVM_RUNTIME_OUTPUT_INTDIR}/${CXX_COMPILER}
    -DCMAKE_C_COMPILER=${LLVM_RUNTIME_OUTPUT_INTDIR}/${C_COMPILER}
    -DCMAKE_ASM_COMPILER=${LLVM_RUNTIME_OUTPUT_INTDIR}/${C_COMPILER}
    -DCMAKE_ASM_COMPILER_ID=Clang)

  # cmake requires CMAKE_LINKER to be specified if the compiler is MSVC-like,
  # otherwise it defaults the linker to be link.exe.
  if(BOOTSTRAP_LLVM_ENABLE_LLD)
    if((WIN32 AND NOT BOOTSTRAP_CMAKE_SYSTEM_NAME) OR BOOTSTRAP_CMAKE_SYSTEM_NAME STREQUAL "Windows")
      set(${CLANG_STAGE}_LINKER -DCMAKE_LINKER=${LLVM_RUNTIME_OUTPUT_INTDIR}/lld-link${CMAKE_EXECUTABLE_SUFFIX})
    endif()
  endif()

  if(BOOTSTRAP_CMAKE_SYSTEM_NAME)
    set(${CLANG_STAGE}_TABLEGEN
      -DLLVM_TABLEGEN=${LLVM_RUNTIME_OUTPUT_INTDIR}/llvm-tblgen
      -DCLANG_TABLEGEN=${LLVM_RUNTIME_OUTPUT_INTDIR}/clang-tblgen)
    if(BOOTSTRAP_CMAKE_SYSTEM_NAME STREQUAL "Linux")
      if(BOOTSTRAP_LLVM_ENABLE_LLD)
        set(${CLANG_STAGE}_LINKER -DCMAKE_LINKER=${LLVM_RUNTIME_OUTPUT_INTDIR}/ld.lld)
      endif()
      if(NOT BOOTSTRAP_LLVM_ENABLE_LTO)
        add_dependencies(clang-bootstrap-deps llvm-ar llvm-ranlib)
        set(${CLANG_STAGE}_AR -DCMAKE_AR=${LLVM_RUNTIME_OUTPUT_INTDIR}/llvm-ar)
        set(${CLANG_STAGE}_RANLIB -DCMAKE_RANLIB=${LLVM_RUNTIME_OUTPUT_INTDIR}/llvm-ranlib)
      endif()
      add_dependencies(clang-bootstrap-deps llvm-objcopy llvm-strip)
      set(${CLANG_STAGE}_OBJCOPY -DCMAKE_OBJCOPY=${LLVM_RUNTIME_OUTPUT_INTDIR}/llvm-objcopy)
      set(${CLANG_STAGE}_STRIP -DCMAKE_STRIP=${LLVM_RUNTIME_OUTPUT_INTDIR}/llvm-strip)
      set(${CLANG_STAGE}_READELF -DCMAKE_READELF=${LLVM_RUNTIME_OUTPUT_INTDIR}/llvm-readelf)
    endif()
  endif()

  if(BOOTSTRAP_LLVM_BUILD_INSTRUMENTED)
    add_dependencies(clang-bootstrap-deps llvm-profdata)
    set(PGO_OPT -DLLVM_PROFDATA=${LLVM_RUNTIME_OUTPUT_INTDIR}/llvm-profdata)
  endif()

  if(LLVM_BUILD_INSTRUMENTED)
    add_dependencies(clang-bootstrap-deps generate-profdata)
    set(PGO_OPT -DLLVM_PROFDATA_FILE=${CMAKE_CURRENT_BINARY_DIR}/utils/perf-training/clang.profdata)
    # Use the current tools for LTO instead of the instrumented ones
    list(APPEND _BOOTSTRAP_DEFAULT_PASSTHROUGH
      CMAKE_CXX_COMPILER
      CMAKE_C_COMPILER
      CMAKE_ASM_COMPILER
      CMAKE_AR
      CMAKE_RANLIB
      DARWIN_LTO_LIBRARY
      DYLD_LIBRARY_PATH)

    set(COMPILER_OPTIONS)
    set(LTO_LIBRARY)
    set(LTO_AR)
    set(LTO_RANLIB)
  endif()

  # Populate the passthrough variables
  foreach(variableName ${CLANG_BOOTSTRAP_PASSTHROUGH} ${_BOOTSTRAP_DEFAULT_PASSTHROUGH})
    if(DEFINED ${variableName})
      if("${${variableName}}" STREQUAL "")
        set(value "")
      else()
        string(REPLACE ";" "|" value "${${variableName}}")
      endif()
      list(APPEND PASSTHROUGH_VARIABLES
        -D${variableName}=${value})
    endif()
  endforeach()

  # Find all variables that start with BOOTSTRAP_ and populate a variable with
  # them.
  get_cmake_property(variableNames VARIABLES)
  foreach(variableName ${variableNames})
    if(variableName MATCHES "^BOOTSTRAP_")
      string(SUBSTRING ${variableName} 10 -1 varName)
      string(REPLACE ";" "|" value "${${variableName}}")
      list(APPEND PASSTHROUGH_VARIABLES
        -D${varName}=${value})
    endif()
    if(${variableName} AND variableName MATCHES "LLVM_EXTERNAL_.*_SOURCE_DIR")
      list(APPEND PASSTHROUGH_VARIABLES
        -D${variableName}=${${variableName}})
    endif()
  endforeach()

  # Build arguments for native tool used in CMake.
  set(build_configuration "$<CONFIG>")
  set(build_tool_args "${LLVM_EXTERNAL_PROJECT_BUILD_TOOL_ARGS}")
  if(NOT build_tool_args STREQUAL "")
    string(PREPEND build_tool_args "-- ")
    separate_arguments(build_tool_args UNIX_COMMAND "${build_tool_args}")
  endif()

  ExternalProject_Add(${NEXT_CLANG_STAGE}
    DEPENDS clang-bootstrap-deps
    PREFIX ${NEXT_CLANG_STAGE}
    SOURCE_DIR ${CMAKE_SOURCE_DIR}
    STAMP_DIR ${STAMP_DIR}
    BINARY_DIR ${BINARY_DIR}
    EXCLUDE_FROM_ALL 1
    CMAKE_ARGS
                # We shouldn't need to set this here, but INSTALL_DIR doesn't
                # seem to work, so instead I'm passing this through
                -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
                ${PASSTHROUGH_VARIABLES}
                ${CLANG_BOOTSTRAP_CMAKE_ARGS}
                 -DCLANG_STAGE=${NEXT_CLANG_STAGE}
                ${COMPILER_OPTIONS}
                ${${CLANG_STAGE}_TABLEGEN}
                ${LTO_LIBRARY} ${verbose} ${PGO_OPT}
                ${${CLANG_STAGE}_LINKER}
                ${${CLANG_STAGE}_AR}
                ${${CLANG_STAGE}_RANLIB}
                ${${CLANG_STAGE}_OBJCOPY}
                ${${CLANG_STAGE}_STRIP}
    BUILD_COMMAND ${CMAKE_COMMAND} --build ${BINARY_DIR}
                                   --config ${build_configuration}
                                   ${build_tool_args}
    INSTALL_COMMAND ""
    STEP_TARGETS configure build
    USES_TERMINAL_CONFIGURE 1
    USES_TERMINAL_BUILD 1
    USES_TERMINAL_INSTALL 1
    LIST_SEPARATOR |
    )

  # exclude really-install from main target
  set_target_properties(${NEXT_CLANG_STAGE} PROPERTIES _EP_really-install_EXCLUDE_FROM_MAIN On)
  ExternalProject_Add_Step(${NEXT_CLANG_STAGE} really-install
    COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target install
    COMMENT "Performing install step for '${NEXT_CLANG_STAGE}'"
    DEPENDEES build
    USES_TERMINAL 1
  )
  ExternalProject_Add_StepTargets(${NEXT_CLANG_STAGE} really-install)
  add_custom_target(${NEXT_CLANG_STAGE}-install DEPENDS ${NEXT_CLANG_STAGE}-really-install)

  if(NOT CLANG_BOOTSTRAP_TARGETS)
    set(CLANG_BOOTSTRAP_TARGETS check-llvm check-clang check-all)
  endif()
  foreach(target ${CLANG_BOOTSTRAP_TARGETS})

    ExternalProject_Add_Step(${NEXT_CLANG_STAGE} ${target}
      COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target ${target}
      COMMENT "Performing ${target} for '${NEXT_CLANG_STAGE}'"
      DEPENDEES configure
      # We need to set ALWAYS to ON here, otherwise these targets won't be
      # built on a second invocation of ninja.  The targets have their own
      # logic to determine if they should build or not so setting ALWAYS ON
      # here does not mean the targets will always rebuild it just means that
      # they will check their dependenices and see if they need to be built.
      ALWAYS ON
      EXCLUDE_FROM_MAIN ON
      USES_TERMINAL 1
    )

    if(target MATCHES "^stage[0-9]*")
      add_custom_target(${target} DEPENDS ${NEXT_CLANG_STAGE}-${target})
    endif()

    ExternalProject_Add_StepTargets(${NEXT_CLANG_STAGE} ${target})
  endforeach()
endif()

set(CLANG_BOLT OFF CACHE STRING "Apply BOLT optimization to Clang. \
  May be specified as Instrument or Perf or LBR to use a particular profiling \
  mechanism.")
string(TOUPPER "${CLANG_BOLT}" CLANG_BOLT)

if (CLANG_BOLT AND NOT LLVM_BUILD_INSTRUMENTED)
  set(CLANG_PATH ${LLVM_RUNTIME_OUTPUT_INTDIR}/clang)
  set(CLANG_INSTRUMENTED ${LLVM_RUNTIME_OUTPUT_INTDIR}/${CLANG_BOLT_INSTRUMENTED})
  set(BOLT_FDATA ${CMAKE_CURRENT_BINARY_DIR}/utils/perf-training/prof.fdata)

  # Pass extra flag in no-LBR mode
  if (CLANG_BOLT STREQUAL "PERF")
    set(BOLT_NO_LBR "-nl")
  endif()

  if (CLANG_BOLT STREQUAL "INSTRUMENT")
    # Instrument clang with BOLT
    add_custom_target(clang-instrumented
      DEPENDS ${CLANG_INSTRUMENTED}
    )
    add_custom_command(OUTPUT ${CLANG_INSTRUMENTED}
      DEPENDS clang llvm-bolt
      COMMAND llvm-bolt ${CLANG_PATH} -o ${CLANG_INSTRUMENTED}
        -instrument --instrumentation-file-append-pid
        --instrumentation-file=${BOLT_FDATA}
      COMMENT "Instrumenting clang binary with BOLT"
      VERBATIM
    )
    add_custom_target(clang-bolt-training-deps DEPENDS clang-instrumented)
  else() # perf or LBR
    add_custom_target(clang-bolt-training-deps DEPENDS clang)
  endif()

  # Optimize original (pre-bolt) Clang using the collected profile
  set(CLANG_OPTIMIZED ${CMAKE_CURRENT_BINARY_DIR}/clang.bolt)
  add_custom_target(clang-bolt
    DEPENDS ${CLANG_OPTIMIZED}
  )
  add_custom_command(OUTPUT ${CLANG_OPTIMIZED}
    DEPENDS clang-bolt-profile
    COMMAND llvm-bolt ${CLANG_PATH}
      -o ${CLANG_OPTIMIZED}
      -data ${BOLT_FDATA}
      -reorder-blocks=ext-tsp -reorder-functions=hfsort+ -split-functions
      -split-all-cold -split-eh -dyno-stats -icf=1 -use-gnu-stack
      ${BOLT_NO_LBR}
    COMMAND ${CMAKE_COMMAND} -E rename ${CLANG_OPTIMIZED} $<TARGET_FILE:clang>
    COMMENT "Optimizing Clang with BOLT"
    VERBATIM
  )
endif()

if (LLVM_ADD_NATIVE_VISUALIZERS_TO_SOLUTION)
  add_subdirectory(utils/ClangVisualizers)
endif()
add_subdirectory(utils/hmaptool)

if(CLANG_BUILT_STANDALONE)
  llvm_distribution_add_targets()
  process_llvm_pass_plugins()
endif()

set(CLANG_INSTALL_LIBDIR_BASENAME "lib${CLANG_LIBDIR_SUFFIX}")

configure_file(
  ${CLANG_SOURCE_DIR}/include/clang/Config/config.h.cmake
  ${CLANG_BINARY_DIR}/include/clang/Config/config.h)
