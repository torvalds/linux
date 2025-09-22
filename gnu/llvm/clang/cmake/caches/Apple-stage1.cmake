# This file sets up a CMakeCache for Apple-style bootstrap builds. It can be
# used on any Darwin system to approximate Apple Clang builds.

if($ENV{DT_TOOLCHAIN_DIR})
  set(CMAKE_INSTALL_PREFIX $ENV{DT_TOOLCHAIN_DIR}/usr/)
else()
  set(CMAKE_INSTALL_PREFIX /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.toolchain/usr/)
endif()

set(LLVM_TARGETS_TO_BUILD X86 CACHE STRING "")
set(CLANG_VENDOR Apple CACHE STRING "")
set(LLVM_INCLUDE_TESTS OFF CACHE BOOL "")
set(LLVM_INCLUDE_EXAMPLES OFF CACHE BOOL "")
set(LLVM_INCLUDE_UTILS OFF CACHE BOOL "")
set(LLVM_INCLUDE_DOCS OFF CACHE BOOL "")
set(CLANG_INCLUDE_TESTS OFF CACHE BOOL "")
set(COMPILER_RT_INCLUDE_TESTS OFF CACHE BOOL "")
set(COMPILER_RT_BUILD_SANITIZERS OFF CACHE BOOL "")
set(CMAKE_MACOSX_RPATH ON CACHE BOOL "")
set(LLVM_ENABLE_ZLIB OFF CACHE BOOL "")
set(LLVM_ENABLE_BACKTRACES OFF CACHE BOOL "")
set(CLANG_PLUGIN_SUPPORT OFF CACHE BOOL "")
set(CLANG_SPAWN_CC1 ON CACHE BOOL "")
set(CLANG_BOOTSTRAP_PASSTHROUGH
  CMAKE_OSX_ARCHITECTURES
  CACHE STRING "")

# Disabling embedded darwin compiler-rt on stage1 builds is required because we
# don't build stage1 to support arm code generation.
set(COMPILER_RT_ENABLE_IOS OFF CACHE BOOL "")
set(COMPILER_RT_ENABLE_WATCHOS OFF CACHE BOOL "")
set(COMPILER_RT_ENABLE_TVOS OFF CACHE BOOL "")

set(BOOTSTRAP_LLVM_ENABLE_LTO ON CACHE BOOL "")
set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "")

set(LIBCXX_ENABLE_NEW_DELETE_DEFINITIONS OFF CACHE BOOL "")
set(LIBCXXABI_ENABLE_NEW_DELETE_DEFINITIONS ON CACHE BOOL "")

set(CLANG_BOOTSTRAP_TARGETS
  generate-order-file
  check-all
  check-llvm
  check-clang
  llvm-config
  test-suite
  test-depends
  llvm-test-depends
  clang-test-depends
  distribution
  install-distribution
  install-xcode-toolchain
  install-distribution-toolchain
  clang CACHE STRING "")

#bootstrap
set(CLANG_ENABLE_BOOTSTRAP ON CACHE BOOL "")
set(CLANG_BOOTSTRAP_CMAKE_ARGS
  -C ${CMAKE_CURRENT_LIST_DIR}/Apple-stage2.cmake
  CACHE STRING "")
