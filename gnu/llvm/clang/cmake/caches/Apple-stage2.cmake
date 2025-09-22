# This file sets up a CMakeCache for Apple-style stage2 bootstrap. It is
# specified by the stage1 build.

set(LLVM_TARGETS_TO_BUILD X86 ARM AArch64 CACHE STRING "")
set(PACKAGE_VENDOR Apple CACHE STRING "")
set(CLANG_VENDOR_UTI com.apple.clang CACHE STRING "")
set(LLVM_INCLUDE_EXAMPLES OFF CACHE BOOL "")
set(LLVM_INCLUDE_DOCS OFF CACHE BOOL "")
set(LLVM_TOOL_CLANG_TOOLS_EXTRA_BUILD OFF CACHE BOOL "")
set(CLANG_TOOL_SCAN_BUILD_BUILD OFF CACHE BOOL "")
set(CLANG_TOOL_SCAN_VIEW_BUILD OFF CACHE BOOL "")
set(CLANG_LINKS_TO_CREATE clang++ cc c++ CACHE STRING "")
set(CMAKE_MACOSX_RPATH ON CACHE BOOL "")
set(LLVM_ENABLE_ZLIB ON CACHE BOOL "")
set(LLVM_ENABLE_BACKTRACES OFF CACHE BOOL "")
set(LLVM_ENABLE_MODULES ON CACHE BOOL "")
set(LLVM_EXTERNALIZE_DEBUGINFO ON CACHE BOOL "")
set(LLVM_ENABLE_EXPORTED_SYMBOLS_IN_EXECUTABLES OFF CACHE BOOL "")
set(LLVM_PLUGIN_SUPPORT OFF CACHE BOOL "")
set(CLANG_PLUGIN_SUPPORT OFF CACHE BOOL "")
set(CLANG_SPAWN_CC1 ON CACHE BOOL "")
set(BUG_REPORT_URL "http://developer.apple.com/bugreporter/" CACHE STRING "")

set(LLVM_BUILD_EXTERNAL_COMPILER_RT ON CACHE BOOL "Build Compiler-RT with just-built clang")
set(COMPILER_RT_ENABLE_IOS ON CACHE BOOL "Build iOS Compiler-RT libraries")

set(LLVM_CREATE_XCODE_TOOLCHAIN ON CACHE BOOL "Generate targets to create and install an Xcode compatible toolchain")

# Make unit tests (if present) part of the ALL target
set(LLVM_BUILD_TESTS ON CACHE BOOL "")

set(LLVM_ENABLE_LTO ON CACHE BOOL "")
set(CMAKE_C_FLAGS "-fno-stack-protector -fno-common -Wno-profile-instr-unprofiled" CACHE STRING "")
set(CMAKE_CXX_FLAGS "-fno-stack-protector -fno-common -Wno-profile-instr-unprofiled" CACHE STRING "")
if(LLVM_ENABLE_LTO AND NOT LLVM_ENABLE_LTO STREQUAL "THIN")
  set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -gline-tables-only -DNDEBUG" CACHE STRING "")
  set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -gline-tables-only -DNDEBUG" CACHE STRING "")
endif()
set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "")

set(LLVM_LTO_VERSION_OFFSET 3000 CACHE STRING "")

# Generating Xcode toolchains is useful for developers wanting to build and use
# clang without installing over existing tools.
set(LLVM_CREATE_XCODE_TOOLCHAIN ON CACHE BOOL "")

# setup toolchain
set(LLVM_INSTALL_TOOLCHAIN_ONLY ON CACHE BOOL "")
set(LLVM_TOOLCHAIN_TOOLS
  dsymutil
  llvm-cov
  llvm-dwarfdump
  llvm-profdata
  llvm-objdump
  llvm-nm
  llvm-size
  llvm-cxxfilt
  llvm-config
  CACHE STRING "")

set(LLVM_BUILD_UTILS ON CACHE BOOL "")
set(LLVM_INSTALL_UTILS ON CACHE BOOL "")
set(LLVM_TOOLCHAIN_UTILITIES
  FileCheck
  yaml2obj
  not
  count
  CACHE STRING "")

set(LLVM_DISTRIBUTION_COMPONENTS
  clang
  LTO
  clang-format
  clang-resource-headers
  Remarks
  ${LLVM_TOOLCHAIN_TOOLS}
  ${LLVM_TOOLCHAIN_UTILITIES}
  CACHE STRING "")

# Build the libc++ headers
set(LLVM_ENABLE_RUNTIMES "libcxx;libcxxabi" CACHE STRING "")
set(LLVM_RUNTIME_DISTRIBUTION_COMPONENTS cxx-headers CACHE STRING "")
set(LIBCXX_INSTALL_LIBRARY OFF CACHE BOOL "")
set(LIBCXX_INSTALL_HEADERS ON CACHE BOOL "")
set(LIBCXX_INCLUDE_TESTS OFF CACHE BOOL "")
set(LIBCXX_USE_COMPILER_RT ON CACHE BOOL "")

# test args

set(LLVM_LIT_ARGS "--xunit-xml-output=testresults.xunit.xml -v" CACHE STRING "")
