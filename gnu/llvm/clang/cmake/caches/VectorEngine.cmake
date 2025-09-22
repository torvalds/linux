# This file sets up a CMakeCache for the simple VE build.
#
# VE is a CPU with vector engine.  But it is a connected to a host CPU as
# an accelerator.  So, we compile programs for VE on a host using clang/llvm
# as a cross compiler.  Therefore, the purpose of this cache file is to
# compile clang/llvm supporting both targets.
#
# Configure:
#   cmake -G Ninja -DCMAKE_BUILD_TYPE=Release
#       -C <llvm_src_root>/clang/cmake/caches/VectorEngine.cmake
#       <llvm_src_root>/llvm-project/llvm
# Build:
#   ninja
#

# Disable ZLIB, and ZSTD for VE since there is no pre-compiled libraries.
set(LLVM_ENABLE_ZLIB OFF CACHE BOOL "")
set(LLVM_ENABLE_ZSTD OFF CACHE BOOL "")

# Enable per-target runtimes directory
set(LLVM_ENABLE_PER_TARGET_RUNTIME_DIR On CACHE BOOL "")

# The lld is not supported for VE yet.
set(LLVM_ENABLE_PROJECTS "clang;clang-tools-extra" CACHE STRING "")
set(LLVM_ENABLE_RUNTIMES "compiler-rt;libcxx;libcxxabi;libunwind;openmp" CACHE STRING "")

# Compile for X86 and VE
set(LLVM_TARGETS_TO_BUILD "X86;VE" CACHE STRING "")

# Not use default here to use RUNTIMES_x86_64-unknown-linux-gnu_* variables.
set(LLVM_BUILTIN_TARGETS "x86_64-unknown-linux-gnu;ve-unknown-linux-gnu" CACHE STRING "")
set(LLVM_RUNTIME_TARGETS "x86_64-unknown-linux-gnu;ve-unknown-linux-gnu" CACHE STRING "")

# For the case of X86, we don't want to test compiler-rt for x86,
# so disable them as much as possible.
set(RUNTIMES_x86_64-unknown-linux-gnu_COMPILER_RT_BUILD_BUILTINS ON CACHE BOOL "")
set(RUNTIMES_x86_64-unknown-linux-gnu_COMPILER_RT_BUILD_CRT OFF CACHE BOOL "")
set(RUNTIMES_x86_64-unknown-linux-gnu_COMPILER_RT_BUILD_SANITIZERS OFF CACHE BOOL "")
set(RUNTIMES_x86_64-unknown-linux-gnu_COMPILER_RT_BUILD_XRAY OFF CACHE BOOL "")
set(RUNTIMES_x86_64-unknown-linux-gnu_COMPILER_RT_BUILD_LIBFUZZER OFF CACHE BOOL "")
set(RUNTIMES_x86_64-unknown-linux-gnu_COMPILER_RT_BUILD_CTX_PROFILE OFF CACHE BOOL "")
set(RUNTIMES_x86_64-unknown-linux-gnu_COMPILER_RT_BUILD_PROFILE OFF CACHE BOOL "")
set(RUNTIMES_x86_64-unknown-linux-gnu_COMPILER_RT_BUILD_MEMPROF OFF CACHE BOOL "")
set(RUNTIMES_x86_64-unknown-linux-gnu_COMPILER_RT_BUILD_ORC OFF CACHE BOOL "")
set(RUNTIMES_x86_64-unknown-linux-gnu_COMPILER_RT_BUILD_GWP_ASAN OFF CACHE BOOL "")

# VE supports builtins, crt, and profile only.
set(RUNTIMES_ve-unknown-linux-gnu_COMPILER_RT_BUILD_BUILTINS ON CACHE BOOL "")
set(RUNTIMES_ve-unknown-linux-gnu_COMPILER_RT_BUILD_CRT ON CACHE BOOL "")
set(RUNTIMES_ve-unknown-linux-gnu_COMPILER_RT_BUILD_SANITIZERS OFF CACHE BOOL "")
set(RUNTIMES_ve-unknown-linux-gnu_COMPILER_RT_BUILD_XRAY OFF CACHE BOOL "")
set(RUNTIMES_ve-unknown-linux-gnu_COMPILER_RT_BUILD_LIBFUZZER OFF CACHE BOOL "")
set(RUNTIMES_ve-unknown-linux-gnu_COMPILER_RT_BUILD_PROFILE ON CACHE BOOL "")
set(RUNTIMES_ve-unknown-linux-gnu_COMPILER_RT_BUILD_CTX_PROFILE OFF CACHE BOOL "")
set(RUNTIMES_ve-unknown-linux-gnu_COMPILER_RT_BUILD_MEMPROF OFF CACHE BOOL "")
set(RUNTIMES_ve-unknown-linux-gnu_COMPILER_RT_BUILD_ORC OFF CACHE BOOL "")
set(RUNTIMES_ve-unknown-linux-gnu_COMPILER_RT_BUILD_GWP_ASAN OFF CACHE BOOL "")

# VE uses builtins from Compiler-RT.
set(RUNTIMES_ve-unknown-linux-gnu_COMPILER_RT_USE_BUILTINS_LIBRARY TRUE CACHE BOOL "")

# VE uses libunwind and Compiler-RT from libcxxabi.
set(RUNTIMES_ve-unknown-linux-gnu_LIBCXXABI_USE_LLVM_UNWINDER TRUE CACHE BOOL "")
set(RUNTIMES_ve-unknown-linux-gnu_LIBCXXABI_USE_COMPILER_RT TRUE CACHE BOOL "")

# VE uses Compiler-RT from libcxx.
set(RUNTIMES_ve-unknown-linux-gnu_LIBCXX_USE_COMPILER_RT TRUE CACHE BOOL "")

# Pretended standalone build for OpenMP since OpenMP doesn't support
# LLVM_ENABLE_PER_TARGET_RUNTIME_DIR yet.
#   - Use OPENMP_STANDALONE_BUILD
#   - Define OPENMP_LIBDIR_SUFFIX to pretend per-target openmp directory
#   - Define OPENMP_LLVM_TOOLS_DIR for test
set(RUNTIMES_x86_64-unknown-linux-gnu_OPENMP_STANDALONE_BUILD ON CACHE BOOL "")
set(RUNTIMES_ve-unknown-linux-gnu_OPENMP_STANDALONE_BUILD ON CACHE BOOL "")

# Specify LIBDIR_SUFFIX for OpenMP to install them at following directories.
#   install/lib/clang/${VERSION}/lib/x86_64-unknown-linux-gnu
#   install/lib/clang/${VERSION}/lib/ve-unknown-linux-gnu
set(RUNTIMES_x86_64-unknown-linux-gnu_OPENMP_LIBDIR_SUFFIX "/x86_64-unknown-linux-gnu" CACHE STRING "")
set(RUNTIMES_ve-unknown-linux-gnu_OPENMP_LIBDIR_SUFFIX "/ve-unknown-linux-gnu" CACHE STRING "")

# Specify OPENMP_LLVM_TOOLS_DIR for test
set(RUNTIMES_x86_64-unknown-linux-gnu_OPENMP_LLVM_TOOLS_DIR "${CMAKE_BINARY_DIR}/bin" CACHE STRING "")
set(RUNTIMES_ve-unknown-linux-gnu_OPENMP_LLVM_TOOLS_DIR "${CMAKE_BINARY_DIR}/bin" CACHE STRING "")

# VE doesn't support libomptarget.  Disable it for x86_64 also.
set(RUNTIMES_x86_64-unknown-linux-gnu_OPENMP_ENABLE_LIBOMPTARGET FALSE CACHE BOOL "")
set(RUNTIMES_ve-unknown-linux-gnu_OPENMP_ENABLE_LIBOMPTARGET FALSE CACHE BOOL "")

# VE requires -lrt flag for shm_open.
set(RUNTIMES_ve-unknown-linux-gnu_LIBOMP_HAVE_SHM_OPEN_WITH_LRT TRUE CACHE BOOL "")

# Compiler flags for testing
set(RUNTIMES_ve-unknown-linux-gnu_COMPILER_RT_TEST_COMPILER_CFLAGS "--target=ve-unknown-linux-gnu" CACHE BOOL "")
set(RUNTIMES_ve-unknown-linux-gnu_LIBCXXABI_TEST_COMPILER_CFLAGS "--target=ve-unknown-linux-gnu" CACHE BOOL "")
set(RUNTIMES_ve-unknown-linux-gnu_LIBCXX_TEST_COMPILER_CFLAGS "--target=ve-unknown-linux-gnu" CACHE BOOL "")
set(RUNTIMES_ve-unknown-linux-gnu_LIBUNWIND_TEST_COMPILER_CFLAGS "--target=ve-unknown-linux-gnu" CACHE BOOL "")
set(RUNTIMES_ve-unknown-linux-gnu_OPENMP_TEST_OPENMP_FLAGS "--target=ve-unknown-linux-gnu -fopenmp -pthread -lrt -ldl -Wl,-rpath,${CMAKE_BINARY_DIR}/lib/ve-unknown-linux-gnu" CACHE BOOL "")

# setup toolchain
set(LLVM_INSTALL_TOOLCHAIN_ONLY ON CACHE BOOL "")
set(LLVM_TOOLCHAIN_TOOLS
  dsymutil
  llc
  llvm-ar
  llvm-cxxfilt
  llvm-cov
  llvm-dwarfdump
  llvm-link
  llvm-nm
  llvm-objdump
  llvm-profdata
  llvm-ranlib
  llvm-readelf
  llvm-readobj
  llvm-size
  llvm-symbolizer
  opt
  CACHE STRING "")

set(LLVM_DISTRIBUTION_COMPONENTS
  clang
  clang-format
  clang-resource-headers
  builtins
  runtimes
  ${LLVM_TOOLCHAIN_TOOLS}
  CACHE STRING "")
