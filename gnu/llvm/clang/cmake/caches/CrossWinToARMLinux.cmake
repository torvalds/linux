# CrossWinToARMLinux.cmake
#
# Set up a CMakeCache for a cross Windows to ARM Linux toolchain build.
#
# This cache file can be used to build a cross toolchain to ARM Linux
# on Windows platform.
#
# NOTE: the build requires a development ARM Linux root filesystem to use
# proper target platform depended library and header files.
#
# The build generates a proper clang configuration file with stored
# --sysroot argument for specified target triple. Also it is possible
# to specify configuration path via CMake arguments, such as
#   -DCLANG_CONFIG_FILE_USER_DIR=<full-path-to-clang-configs>
# and/or
#   -DCLANG_CONFIG_FILE_SYSTEM_DIR=<full-path-to-clang-configs>
#
# See more details here: https://clang.llvm.org/docs/UsersManual.html#configuration-files
#
# Configure:
#  cmake -G Ninja ^
#       -DTOOLCHAIN_TARGET_TRIPLE=aarch64-unknown-linux-gnu ^
#       -DTOOLCHAIN_TARGET_SYSROOTFS=<path-to-develop-arm-linux-root-fs> ^
#       -DTOOLCHAIN_SHARED_LIBS=OFF ^ 
#       -DCMAKE_INSTALL_PREFIX=../install ^
#       -DCMAKE_CXX_FLAGS="-D__OPTIMIZE__" ^
#       -DREMOTE_TEST_HOST="<hostname>" ^
#       -DREMOTE_TEST_USER="<ssh_user_name>" ^
#       -C<llvm_src_root>/llvm-project/clang/cmake/caches/CrossWinToARMLinux.cmake ^
#       <llvm_src_root>/llvm-project/llvm
# Build:
#  cmake --build . --target install
# Tests:
#  cmake --build . --target check-llvm
#  cmake --build . --target check-clang
#  cmake --build . --target check-lld
#  cmake --build . --target check-compiler-rt-<TOOLCHAIN_TARGET_TRIPLE>
#  cmake --build . --target check-cxxabi-<TOOLCHAIN_TARGET_TRIPLE>
#  cmake --build . --target check-unwind-<TOOLCHAIN_TARGET_TRIPLE>
#  cmake --build . --target check-cxx-<TOOLCHAIN_TARGET_TRIPLE>
# (another way to execute the tests)
# python bin/llvm-lit.py -v --threads=32 runtimes/runtimes-<TOOLCHAIN_TARGET_TRIPLE>bins/libunwind/test  2>&1 | tee libunwind-tests.log
# python bin/llvm-lit.py -v --threads=32 runtimes/runtimes-<TOOLCHAIN_TARGET_TRIPLE>-bins/libcxxabi/test 2>&1 | tee libcxxabi-tests.log
# python bin/llvm-lit.py -v --threads=32 runtimes/runtimes-<TOOLCHAIN_TARGET_TRIPLE>-bins/libcxx/test    2>&1 | tee libcxx-tests.log


# LLVM_PROJECT_DIR is the path to the llvm-project directory.
# The right way to compute it would probably be to use "${CMAKE_SOURCE_DIR}/../",
# but CMAKE_SOURCE_DIR is set to the wrong value on earlier CMake versions
# that we still need to support (for instance, 3.10.2).
get_filename_component(LLVM_PROJECT_DIR
                       "${CMAKE_CURRENT_LIST_DIR}/../../../"
                       ABSOLUTE)

if (NOT DEFINED LLVM_ENABLE_ASSERTIONS)
  set(LLVM_ENABLE_ASSERTIONS ON CACHE BOOL "")
endif()
if (NOT DEFINED LLVM_ENABLE_PROJECTS)
  set(LLVM_ENABLE_PROJECTS "clang;clang-tools-extra;lld" CACHE STRING "")
endif()
if (NOT DEFINED LLVM_ENABLE_RUNTIMES)
  set(LLVM_ENABLE_RUNTIMES "compiler-rt;libunwind;libcxxabi;libcxx" CACHE STRING "")
endif()

if (NOT DEFINED TOOLCHAIN_TARGET_TRIPLE)
  set(TOOLCHAIN_TARGET_TRIPLE "aarch64-unknown-linux-gnu")
else()
  #NOTE: we must normalize specified target triple to a fully specified triple,
  # including the vendor part. It is necessary to synchronize the runtime library
  # installation path and operable target triple by Clang to get a correct runtime
  # path through `-print-runtime-dir` Clang option.
  string(REPLACE "-" ";" TOOLCHAIN_TARGET_TRIPLE "${TOOLCHAIN_TARGET_TRIPLE}")
  list(LENGTH TOOLCHAIN_TARGET_TRIPLE TOOLCHAIN_TARGET_TRIPLE_LEN)
  if (TOOLCHAIN_TARGET_TRIPLE_LEN LESS 3)
    message(FATAL_ERROR "invalid target triple")
  endif()
  # We suppose missed vendor's part.
  if (TOOLCHAIN_TARGET_TRIPLE_LEN LESS 4)
    list(INSERT TOOLCHAIN_TARGET_TRIPLE 1 "unknown")
  endif()
  string(REPLACE ";" "-" TOOLCHAIN_TARGET_TRIPLE "${TOOLCHAIN_TARGET_TRIPLE}")
endif()

message(STATUS "Toolchain target triple: ${TOOLCHAIN_TARGET_TRIPLE}")

if (DEFINED TOOLCHAIN_TARGET_SYSROOTFS)
  message(STATUS "Toolchain target sysroot: ${TOOLCHAIN_TARGET_SYSROOTFS}")
  # Store the --sysroot argument for the compiler-rt test flags.
  set(sysroot_flags --sysroot='${TOOLCHAIN_TARGET_SYSROOTFS}')
  # Generate the clang configuration file for the specified target triple
  # and store --sysroot in this file.
  file(WRITE "${CMAKE_BINARY_DIR}/bin/${TOOLCHAIN_TARGET_TRIPLE}.cfg" ${sysroot_flags})
endif()

# Build the shared libraries for libc++/libc++abi/libunwind.
if (NOT DEFINED TOOLCHAIN_SHARED_LIBS)
  set(TOOLCHAIN_SHARED_LIBS OFF)
endif()
 
if (NOT DEFINED LLVM_TARGETS_TO_BUILD)
  if ("${TOOLCHAIN_TARGET_TRIPLE}" MATCHES "^(armv|arm32)+")
    set(LLVM_TARGETS_TO_BUILD "ARM" CACHE STRING "")
  endif()
  if ("${TOOLCHAIN_TARGET_TRIPLE}" MATCHES "^(aarch64|arm64)+")
    set(LLVM_TARGETS_TO_BUILD "AArch64" CACHE STRING "")
  endif()
endif()

message(STATUS "Toolchain target to build: ${LLVM_TARGETS_TO_BUILD}")

# Allow to override libc++ ABI version. Use 2 by default.
if (NOT DEFINED LIBCXX_ABI_VERSION)
  set(LIBCXX_ABI_VERSION 2)
endif()

message(STATUS "Toolchain's Libc++ ABI version: ${LIBCXX_ABI_VERSION}")

if (NOT DEFINED CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "")
endif()

set(CMAKE_CROSSCOMPILING                    ON CACHE BOOL "")
set(CMAKE_CL_SHOWINCLUDES_PREFIX            "Note: including file: " CACHE STRING "")
# Required if COMPILER_RT_DEFAULT_TARGET_ONLY is ON
set(CMAKE_C_COMPILER_TARGET                 "${TOOLCHAIN_TARGET_TRIPLE}" CACHE STRING "")
set(CMAKE_CXX_COMPILER_TARGET               "${TOOLCHAIN_TARGET_TRIPLE}" CACHE STRING "")

set(LLVM_DEFAULT_TARGET_TRIPLE              "${TOOLCHAIN_TARGET_TRIPLE}" CACHE STRING "")
set(LLVM_TARGET_ARCH                        "${TOOLCHAIN_TARGET_TRIPLE}" CACHE STRING "")
set(LLVM_LIT_ARGS                           "-vv ${LLVM_LIT_ARGS}" CACHE STRING "" FORCE)

set(CLANG_DEFAULT_CXX_STDLIB                "libc++" CACHE STRING "")
set(CLANG_DEFAULT_LINKER                    "lld" CACHE STRING "")
set(CLANG_DEFAULT_OBJCOPY                   "llvm-objcopy" CACHE STRING "")
set(CLANG_DEFAULT_RTLIB                     "compiler-rt" CACHE STRING "")
set(CLANG_DEFAULT_UNWINDLIB                 "libunwind" CACHE STRING "")

if (NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY AND WIN32)
  #Note: Always specify MT DLL for the LLDB build configurations on Windows host.
  if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_MSVC_RUNTIME_LIBRARY            "MultiThreadedDebugDLL" CACHE STRING "")
  else()
    set(CMAKE_MSVC_RUNTIME_LIBRARY            "MultiThreadedDLL" CACHE STRING "")
  endif()
  # Grab all ucrt/vcruntime related DLLs into the binary installation folder.
  set(CMAKE_INSTALL_UCRT_LIBRARIES          ON CACHE BOOL "")
endif()

# Set up RPATH for the target runtime/builtin libraries.
# See some details here: https://reviews.llvm.org/D91099
if (NOT DEFINED RUNTIMES_INSTALL_RPATH)
  set(RUNTIMES_INSTALL_RPATH                "\$ORIGIN/../lib;${CMAKE_INSTALL_PREFIX}/lib")
endif()

set(LLVM_BUILTIN_TARGETS                    "${TOOLCHAIN_TARGET_TRIPLE}" CACHE STRING "")

set(BUILTINS_${TOOLCHAIN_TARGET_TRIPLE}_CMAKE_SYSTEM_NAME                         "Linux" CACHE STRING "")
set(BUILTINS_${TOOLCHAIN_TARGET_TRIPLE}_CMAKE_INSTALL_RPATH                       "${RUNTIMES_INSTALL_RPATH}"  CACHE STRING "")
set(BUILTINS_${TOOLCHAIN_TARGET_TRIPLE}_CMAKE_BUILD_WITH_INSTALL_RPATH            ON  CACHE BOOL "")
set(BUILTINS_${TOOLCHAIN_TARGET_TRIPLE}_LLVM_CMAKE_DIR                            "${LLVM_PROJECT_DIR}/llvm/cmake/modules" CACHE PATH "")

if (DEFINED TOOLCHAIN_TARGET_COMPILER_FLAGS)
  foreach(lang C;CXX;ASM)
    set(BUILTINS_${TOOLCHAIN_TARGET_TRIPLE}_CMAKE_${lang}_FLAGS         "${TOOLCHAIN_TARGET_COMPILER_FLAGS}" CACHE STRING "")
  endforeach()
endif()
foreach(type SHARED;MODULE;EXE)
  set(BUILTINS_${TOOLCHAIN_TARGET_TRIPLE}_CMAKE_${type}_LINKER_FLAGS    "-fuse-ld=lld" CACHE STRING "")
endforeach()

set(LLVM_RUNTIME_TARGETS                    "${TOOLCHAIN_TARGET_TRIPLE}" CACHE STRING "")
set(LLVM_ENABLE_PER_TARGET_RUNTIME_DIR      ON CACHE BOOL "")

set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_LLVM_ENABLE_RUNTIMES                      "${LLVM_ENABLE_RUNTIMES}" CACHE STRING "")

set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_CMAKE_SYSTEM_NAME                         "Linux" CACHE STRING "")
set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_CMAKE_INSTALL_RPATH                       "${RUNTIMES_INSTALL_RPATH}"  CACHE STRING "")
set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_CMAKE_BUILD_WITH_INSTALL_RPATH            ON  CACHE BOOL "")

if (DEFINED TOOLCHAIN_TARGET_COMPILER_FLAGS)
  foreach(lang C;CXX;ASM)
    set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_CMAKE_${lang}_FLAGS         "${TOOLCHAIN_TARGET_COMPILER_FLAGS}" CACHE STRING "")
  endforeach()
endif()
foreach(type SHARED;MODULE;EXE)
  set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_CMAKE_${type}_LINKER_FLAGS    "-fuse-ld=lld" CACHE STRING "")
endforeach()

set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_COMPILER_RT_BUILD_BUILTINS                ON CACHE BOOL "")
set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_COMPILER_RT_BUILD_SANITIZERS              OFF CACHE BOOL "")
set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_COMPILER_RT_BUILD_XRAY                    OFF CACHE BOOL "")
set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_COMPILER_RT_BUILD_LIBFUZZER               OFF CACHE BOOL "")
set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_COMPILER_RT_BUILD_PROFILE                 OFF CACHE BOOL "")
set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_COMPILER_RT_BUILD_CRT                     ON CACHE BOOL "")
set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_COMPILER_RT_BUILD_ORC                     OFF CACHE BOOL "")
set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_COMPILER_RT_DEFAULT_TARGET_ONLY           ON CACHE BOOL "")
set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_COMPILER_RT_INCLUDE_TESTS                 ON CACHE BOOL "")
set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_COMPILER_RT_CAN_EXECUTE_TESTS             ON CACHE BOOL "")

set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_COMPILER_RT_USE_BUILTINS_LIBRARY          ON CACHE BOOL "")
set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_COMPILER_RT_CXX_LIBRARY                   libcxx CACHE STRING "")
# The compiler-rt tests disable the clang configuration files during the execution by setting CLANG_NO_DEFAULT_CONFIG=1
# and drops out the --sysroot from there. Provide it explicity via the test flags here if target sysroot has been specified.
set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_COMPILER_RT_TEST_COMPILER_CFLAGS          "--stdlib=libc++ ${sysroot_flags}" CACHE STRING "")
  
set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_LIBUNWIND_USE_COMPILER_RT                 ON CACHE BOOL "")
set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_LIBUNWIND_ENABLE_SHARED                   ${TOOLCHAIN_SHARED_LIBS} CACHE BOOL "")

set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_LIBCXXABI_USE_LLVM_UNWINDER               ON CACHE BOOL "")
set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_LIBCXXABI_ENABLE_STATIC_UNWINDER          ON CACHE BOOL "")
set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_LIBCXXABI_USE_COMPILER_RT                 ON CACHE BOOL "")
set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_LIBCXXABI_ENABLE_NEW_DELETE_DEFINITIONS   OFF CACHE BOOL "")
set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_LIBCXXABI_ENABLE_SHARED                   ${TOOLCHAIN_SHARED_LIBS} CACHE BOOL "")

set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_LIBCXX_USE_COMPILER_RT                    ON CACHE BOOL "")
set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_LIBCXX_ENABLE_SHARED                      ${TOOLCHAIN_SHARED_LIBS} CACHE BOOL "")
set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_LIBCXX_ABI_VERSION                        ${LIBCXX_ABI_VERSION} CACHE STRING "")
set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_LIBCXX_CXX_ABI                            "libcxxabi" CACHE STRING "")    #!!!
set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_LIBCXX_ENABLE_NEW_DELETE_DEFINITIONS      ON CACHE BOOL "")

# Avoid searching for the python3 interpreter during the runtimes configuration for the cross builds.
# It starts searching the python3 package using the target's sysroot path, that usually is not compatible with the build host.
find_package(Python3 COMPONENTS Interpreter)
set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_Python3_EXECUTABLE                        ${Python3_EXECUTABLE} CACHE PATH "")

set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_LIBUNWIND_TEST_PARAMS_default "${RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_TEST_PARAMS}")
set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_LIBCXXABI_TEST_PARAMS_default "${RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_TEST_PARAMS}")
set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_LIBCXX_TEST_PARAMS_default "${RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_TEST_PARAMS}")

# Remote test configuration.
if(DEFINED REMOTE_TEST_HOST)
  # Allow override with the custom values.
  if(NOT DEFINED DEFAULT_TEST_EXECUTOR)
    set(DEFAULT_TEST_EXECUTOR                 "\\\"${Python3_EXECUTABLE}\\\" \\\"${LLVM_PROJECT_DIR}/libcxx/utils/ssh.py\\\" --host=${REMOTE_TEST_USER}@${REMOTE_TEST_HOST}")
  endif()

  set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_COMPILER_RT_EMULATOR
        "\\\"${Python3_EXECUTABLE}\\\" \\\"${LLVM_PROJECT_DIR}/llvm/utils/remote-exec.py\\\" --host=${REMOTE_TEST_USER}@${REMOTE_TEST_HOST}"
        CACHE STRING "")

  list(APPEND RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_LIBUNWIND_TEST_PARAMS_default "executor=${DEFAULT_TEST_EXECUTOR}")
  list(APPEND RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_LIBCXXABI_TEST_PARAMS_default "executor=${DEFAULT_TEST_EXECUTOR}")
  list(APPEND RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_LIBCXX_TEST_PARAMS_default    "executor=${DEFAULT_TEST_EXECUTOR}")
endif()

set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_LIBUNWIND_TEST_PARAMS "${RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_LIBUNWIND_TEST_PARAMS_default}" CACHE INTERNAL "")
set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_LIBCXXABI_TEST_PARAMS "${RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_LIBCXXABI_TEST_PARAMS_default}" CACHE INTERNAL "")
set(RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_LIBCXX_TEST_PARAMS "${RUNTIMES_${TOOLCHAIN_TARGET_TRIPLE}_LIBCXX_TEST_PARAMS_default}" CACHE INTERNAL "")

set(LLVM_INSTALL_TOOLCHAIN_ONLY ON CACHE BOOL "")
set(LLVM_TOOLCHAIN_TOOLS
  llvm-ar
  llvm-cov
  llvm-cxxfilt
  llvm-dwarfdump
  llvm-lib
  llvm-nm
  llvm-objdump
  llvm-pdbutil
  llvm-profdata
  llvm-ranlib
  llvm-readobj
  llvm-size
  llvm-symbolizer
  CACHE STRING "")

set(LLVM_DISTRIBUTION_COMPONENTS
  clang
  lld
  LTO
  clang-format
  builtins
  runtimes
  ${LLVM_TOOLCHAIN_TOOLS}
  CACHE STRING "")
