# This file sets up a CMakeCache for the second stage of a Fuchsia toolchain build.

option(FUCHSIA_ENABLE_LLDB "Enable LLDB")

set(LLVM_TARGETS_TO_BUILD X86;ARM;AArch64;RISCV CACHE STRING "")

set(PACKAGE_VENDOR Fuchsia CACHE STRING "")

set(_FUCHSIA_ENABLE_PROJECTS "bolt;clang;clang-tools-extra;libc;lld;llvm;polly")
set(LLVM_ENABLE_RUNTIMES "compiler-rt;libcxx;libcxxabi;libunwind" CACHE STRING "")

set(LLVM_ENABLE_BACKTRACES OFF CACHE BOOL "")
set(LLVM_ENABLE_DIA_SDK OFF CACHE BOOL "")
set(LLVM_ENABLE_FATLTO ON CACHE BOOL "")
set(LLVM_ENABLE_HTTPLIB ON CACHE BOOL "")
set(LLVM_ENABLE_LIBCXX ON CACHE BOOL "")
set(LLVM_ENABLE_LIBEDIT OFF CACHE BOOL "")
set(LLVM_ENABLE_LLD ON CACHE BOOL "")
set(LLVM_ENABLE_LTO ON CACHE BOOL "")
set(LLVM_ENABLE_PER_TARGET_RUNTIME_DIR ON CACHE BOOL "")
set(LLVM_ENABLE_PLUGINS OFF CACHE BOOL "")
set(LLVM_ENABLE_UNWIND_TABLES OFF CACHE BOOL "")
set(LLVM_ENABLE_Z3_SOLVER OFF CACHE BOOL "")
set(LLVM_ENABLE_ZLIB ON CACHE BOOL "")
set(LLVM_FORCE_BUILD_RUNTIME ON CACHE BOOL "")
set(LLVM_INCLUDE_DOCS OFF CACHE BOOL "")
set(LLVM_INCLUDE_EXAMPLES OFF CACHE BOOL "")
set(LLVM_LIBC_FULL_BUILD ON CACHE BOOL "")
set(LIBC_HDRGEN_ONLY ON CACHE BOOL "")
set(LLVM_STATIC_LINK_CXX_STDLIB ON CACHE BOOL "")
set(LLVM_USE_RELATIVE_PATHS_IN_FILES ON CACHE BOOL "")
set(LLDB_ENABLE_CURSES OFF CACHE BOOL "")
set(LLDB_ENABLE_LIBEDIT OFF CACHE BOOL "")

if(WIN32)
  set(FUCHSIA_DISABLE_DRIVER_BUILD ON)
endif()

if (NOT FUCHSIA_DISABLE_DRIVER_BUILD)
  set(LLVM_TOOL_LLVM_DRIVER_BUILD ON CACHE BOOL "")
  set(LLVM_DRIVER_TARGET llvm-driver)
endif()

set(CLANG_DEFAULT_CXX_STDLIB libc++ CACHE STRING "")
set(CLANG_DEFAULT_LINKER lld CACHE STRING "")
set(CLANG_DEFAULT_OBJCOPY llvm-objcopy CACHE STRING "")
set(CLANG_DEFAULT_RTLIB compiler-rt CACHE STRING "")
set(CLANG_DEFAULT_UNWINDLIB libunwind CACHE STRING "")
set(CLANG_ENABLE_ARCMT OFF CACHE BOOL "")
set(CLANG_ENABLE_STATIC_ANALYZER ON CACHE BOOL "")
set(CLANG_PLUGIN_SUPPORT OFF CACHE BOOL "")

set(ENABLE_LINKER_BUILD_ID ON CACHE BOOL "")
set(ENABLE_X86_RELAX_RELOCATIONS ON CACHE BOOL "")

# TODO(#67176): relative-vtables doesn't play well with different default
# visibilities. Making everything hidden visibility causes other complications
# let's choose default visibility for our entire toolchain.
set(CMAKE_C_VISIBILITY_PRESET default CACHE STRING "")
set(CMAKE_CXX_VISIBILITY_PRESET default CACHE STRING "")

set(CMAKE_BUILD_TYPE Release CACHE STRING "")
if (APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")
elseif(WIN32)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded" CACHE STRING "")
endif()

if(APPLE)
  list(APPEND BUILTIN_TARGETS "default")
  list(APPEND RUNTIME_TARGETS "default")

  set(COMPILER_RT_ENABLE_TVOS OFF CACHE BOOL "")
  set(COMPILER_RT_ENABLE_WATCHOS OFF CACHE BOOL "")
  set(COMPILER_RT_USE_BUILTINS_LIBRARY ON CACHE BOOL "")

  set(LIBUNWIND_ENABLE_SHARED OFF CACHE BOOL "")
  set(LIBUNWIND_USE_COMPILER_RT ON CACHE BOOL "")
  set(LIBCXXABI_ENABLE_SHARED OFF CACHE BOOL "")
  set(LIBCXXABI_ENABLE_STATIC_UNWINDER ON CACHE BOOL "")
  set(LIBCXXABI_INSTALL_LIBRARY OFF CACHE BOOL "")
  set(LIBCXXABI_USE_COMPILER_RT ON CACHE BOOL "")
  set(LIBCXXABI_USE_LLVM_UNWINDER ON CACHE BOOL "")
  set(LIBCXX_ABI_VERSION 2 CACHE STRING "")
  set(LIBCXX_ENABLE_SHARED OFF CACHE BOOL "")
  set(LIBCXX_ENABLE_STATIC_ABI_LIBRARY ON CACHE BOOL "")
  set(LIBCXX_HARDENING_MODE "none" CACHE STRING "")
  set(LIBCXX_USE_COMPILER_RT ON CACHE BOOL "")
  set(RUNTIMES_CMAKE_ARGS "-DCMAKE_OSX_DEPLOYMENT_TARGET=10.13;-DCMAKE_OSX_ARCHITECTURES=arm64|x86_64" CACHE STRING "")
endif()

if(WIN32 OR LLVM_WINSYSROOT)
  if((NOT WIN32) AND (NOT LLVM_VFSOVERLAY))
    message(FATAL_ERROR "LLVM_VFSOVERLAY should be defined.")
  endif()
  set(target "x86_64-pc-windows-msvc")

  if (LLVM_WINSYSROOT)
    set(WINDOWS_COMPILER_FLAGS
      -Xclang
      -ivfsoverlay
      -Xclang
      ${LLVM_VFSOVERLAY}
      # TODO: /winsysroot should be set by HandleLLVMOptions.cmake automatically
      # but it current has a bug that prevents it from working under cross
      # compilation. Set this flag manually for now.
      /winsysroot
      ${LLVM_WINSYSROOT})
    string(REPLACE ";" " " WINDOWS_COMPILER_FLAGS "${WINDOWS_COMPILER_FLAGS}")
    set(WINDOWS_LINK_FLAGS
      # TODO: lld-link has a bug that it cannot infer the machine type
      # correctly when /winsysroot is set while Windows libraries search paths
      # are not explicitly defined.
      # Explicitly set the machine type for now.
      /machine:X64
      /vfsoverlay:${LLVM_VFSOVERLAY}
      /winsysroot:${LLVM_WINSYSROOT})
    string(REPLACE ";" " " WINDOWS_LINK_FLAGS "${WINDOWS_LINK_FLAGS}")
  endif()

  list(APPEND BUILTIN_TARGETS "${target}")
  set(BUILTINS_${target}_CMAKE_SYSTEM_NAME Windows CACHE STRING "")
  set(BUILTINS_${target}_CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "")
  set(BUILTINS_${target}_CMAKE_C_FLAGS ${WINDOWS_COMPILER_FLAGS} CACHE STRING "")
  set(BUILTINS_${target}_CMAKE_CXX_FLAGS ${WINDOWS_COMPILER_FLAGS} CACHE STRING "")
  set(BUILTINS_${target}_CMAKE_EXE_LINKER_FLAGS ${WINDOWS_LINK_FLAGS} CACHE STRING "")
  set(BUILTINS_${target}_CMAKE_SHARED_LINKER_FLAGS ${WINDOWS_LINK_FLAGS} CACHE STRING "")
  set(BUILTINS_${target}_CMAKE_MODULE_LINKER_FLAGS ${WINDOWS_LINK_FLAGS} CACHE STRING "")

  list(APPEND RUNTIME_TARGETS "${target}")
  set(RUNTIMES_${target}_CMAKE_SYSTEM_NAME Windows CACHE STRING "")
  set(RUNTIMES_${target}_CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "")
  set(RUNTIMES_${target}_LIBCXX_ABI_VERSION 2 CACHE STRING "")
  set(RUNTIMES_${target}_LIBCXX_ENABLE_ABI_LINKER_SCRIPT OFF CACHE BOOL "")
  set(RUNTIMES_${target}_LIBCXX_ENABLE_SHARED OFF CACHE BOOL "")
  set(RUNTIMES_${target}_LLVM_ENABLE_RUNTIMES "compiler-rt;libcxx" CACHE STRING "")
  set(RUNTIMES_${target}_CMAKE_C_FLAGS ${WINDOWS_COMPILER_FLAGS} CACHE STRING "")
  set(RUNTIMES_${target}_CMAKE_CXX_FLAGS ${WINDOWS_COMPILER_FLAGS} CACHE STRING "")
  set(RUNTIMES_${target}_CMAKE_EXE_LINKER_FLAGS ${WINDOWS_LINK_FLAGS} CACHE STRING "")
  set(RUNTIMES_${target}_CMAKE_SHARED_LINKER_FLAGS ${WINDOWS_LINK_FLAGS} CACHE STRING "")
  set(RUNTIMES_${target}_CMAKE_MODULE_LINKER_FLAGS ${WINDOWS_LINK_FLAGS} CACHE STRING "")
endif()

foreach(target aarch64-unknown-linux-gnu;armv7-unknown-linux-gnueabihf;i386-unknown-linux-gnu;riscv64-unknown-linux-gnu;x86_64-unknown-linux-gnu)
  if(LINUX_${target}_SYSROOT)
    # Set the per-target builtins options.
    list(APPEND BUILTIN_TARGETS "${target}")
    set(BUILTINS_${target}_CMAKE_SYSTEM_NAME Linux CACHE STRING "")
    set(BUILTINS_${target}_CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "")
    set(BUILTINS_${target}_CMAKE_C_FLAGS "--target=${target}" CACHE STRING "")
    set(BUILTINS_${target}_CMAKE_CXX_FLAGS "--target=${target}" CACHE STRING "")
    set(BUILTINS_${target}_CMAKE_ASM_FLAGS "--target=${target}" CACHE STRING "")
    set(BUILTINS_${target}_CMAKE_SYSROOT ${LINUX_${target}_SYSROOT} CACHE STRING "")
    set(BUILTINS_${target}_CMAKE_SHARED_LINKER_FLAGS "-fuse-ld=lld" CACHE STRING "")
    set(BUILTINS_${target}_CMAKE_MODULE_LINKER_FLAGS "-fuse-ld=lld" CACHE STRING "")
    set(BUILTINS_${target}_CMAKE_EXE_LINKER_FLAG "-fuse-ld=lld" CACHE STRING "")
    set(BUILTINS_${target}_COMPILER_RT_BUILD_STANDALONE_LIBATOMIC ON CACHE BOOL "")

    # Set the per-target runtimes options.
    list(APPEND RUNTIME_TARGETS "${target}")
    set(RUNTIMES_${target}_CMAKE_SYSTEM_NAME Linux CACHE STRING "")
    set(RUNTIMES_${target}_CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "")
    set(RUNTIMES_${target}_CMAKE_C_FLAGS "--target=${target}" CACHE STRING "")
    set(RUNTIMES_${target}_CMAKE_CXX_FLAGS "--target=${target}" CACHE STRING "")
    set(RUNTIMES_${target}_CMAKE_ASM_FLAGS "--target=${target}" CACHE STRING "")
    set(RUNTIMES_${target}_CMAKE_SYSROOT ${LINUX_${target}_SYSROOT} CACHE STRING "")
    set(RUNTIMES_${target}_CMAKE_SHARED_LINKER_FLAGS "-fuse-ld=lld" CACHE STRING "")
    set(RUNTIMES_${target}_CMAKE_MODULE_LINKER_FLAGS "-fuse-ld=lld" CACHE STRING "")
    set(RUNTIMES_${target}_CMAKE_EXE_LINKER_FLAGS "-fuse-ld=lld" CACHE STRING "")
    set(RUNTIMES_${target}_COMPILER_RT_CXX_LIBRARY "libcxx" CACHE STRING "")
    set(RUNTIMES_${target}_COMPILER_RT_USE_BUILTINS_LIBRARY ON CACHE BOOL "")
    set(RUNTIMES_${target}_COMPILER_RT_USE_LLVM_UNWINDER ON CACHE BOOL "")
    set(RUNTIMES_${target}_COMPILER_RT_CAN_EXECUTE_TESTS ON CACHE BOOL "")
    set(RUNTIMES_${target}_COMPILER_RT_BUILD_STANDALONE_LIBATOMIC ON CACHE BOOL "")
    set(RUNTIMES_${target}_LIBUNWIND_ENABLE_SHARED OFF CACHE BOOL "")
    set(RUNTIMES_${target}_LIBUNWIND_USE_COMPILER_RT ON CACHE BOOL "")
    set(RUNTIMES_${target}_LIBCXXABI_USE_COMPILER_RT ON CACHE BOOL "")
    set(RUNTIMES_${target}_LIBCXXABI_ENABLE_SHARED OFF CACHE BOOL "")
    set(RUNTIMES_${target}_LIBCXXABI_USE_LLVM_UNWINDER ON CACHE BOOL "")
    set(RUNTIMES_${target}_LIBCXXABI_INSTALL_LIBRARY OFF CACHE BOOL "")
    set(RUNTIMES_${target}_LIBCXX_USE_COMPILER_RT ON CACHE BOOL "")
    set(RUNTIMES_${target}_LIBCXX_ENABLE_SHARED OFF CACHE BOOL "")
    set(RUNTIMES_${target}_LIBCXX_ENABLE_STATIC_ABI_LIBRARY ON CACHE BOOL "")
    set(RUNTIMES_${target}_LIBCXX_ABI_VERSION 2 CACHE STRING "")
    set(RUNTIMES_${target}_LLVM_ENABLE_ASSERTIONS OFF CACHE BOOL "")
    set(RUNTIMES_${target}_SANITIZER_CXX_ABI "libc++" CACHE STRING "")
    set(RUNTIMES_${target}_SANITIZER_CXX_ABI_INTREE ON CACHE BOOL "")
    set(RUNTIMES_${target}_SANITIZER_TEST_CXX "libc++" CACHE STRING "")
    set(RUNTIMES_${target}_SANITIZER_TEST_CXX_INTREE ON CACHE BOOL "")
    set(RUNTIMES_${target}_LLVM_TOOLS_DIR "${CMAKE_BINARY_DIR}/bin" CACHE BOOL "")
    set(RUNTIMES_${target}_LLVM_ENABLE_RUNTIMES "compiler-rt;libcxx;libcxxabi;libunwind" CACHE STRING "")

    # Use .build-id link.
    list(APPEND RUNTIME_BUILD_ID_LINK "${target}")
  endif()
endforeach()

if(FUCHSIA_SDK)
  set(FUCHSIA_aarch64-unknown-fuchsia_NAME arm64)
  set(FUCHSIA_i386-unknown-fuchsia_NAME x64)
  set(FUCHSIA_x86_64-unknown-fuchsia_NAME x64)
  set(FUCHSIA_riscv64-unknown-fuchsia_NAME riscv64)
  foreach(target i386-unknown-fuchsia;x86_64-unknown-fuchsia;aarch64-unknown-fuchsia;riscv64-unknown-fuchsia)
    set(FUCHSIA_${target}_COMPILER_FLAGS "--target=${target} -I${FUCHSIA_SDK}/pkg/sync/include -I${FUCHSIA_SDK}/pkg/fdio/include")
    set(FUCHSIA_${target}_LINKER_FLAGS "-L${FUCHSIA_SDK}/arch/${FUCHSIA_${target}_NAME}/lib")
    set(FUCHSIA_${target}_SYSROOT "${FUCHSIA_SDK}/arch/${FUCHSIA_${target}_NAME}/sysroot")
  endforeach()

  foreach(target i386-unknown-fuchsia;x86_64-unknown-fuchsia;aarch64-unknown-fuchsia;riscv64-unknown-fuchsia)
    # Set the per-target builtins options.
    list(APPEND BUILTIN_TARGETS "${target}")
    set(BUILTINS_${target}_CMAKE_SYSTEM_NAME Fuchsia CACHE STRING "")
    set(BUILTINS_${target}_CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "")
    set(BUILTINS_${target}_CMAKE_ASM_FLAGS ${FUCHSIA_${target}_COMPILER_FLAGS} CACHE STRING "")
    set(BUILTINS_${target}_CMAKE_C_FLAGS ${FUCHSIA_${target}_COMPILER_FLAGS} CACHE STRING "")
    set(BUILTINS_${target}_CMAKE_CXX_FLAGS ${FUCHSIA_${target}_COMPILER_FLAGS} CACHE STRING "")
    set(BUILTINS_${target}_CMAKE_SHARED_LINKER_FLAGS ${FUCHSIA_${target}_LINKER_FLAGS} CACHE STRING "")
    set(BUILTINS_${target}_CMAKE_MODULE_LINKER_FLAGS ${FUCHSIA_${target}_LINKER_FLAGS} CACHE STRING "")
    set(BUILTINS_${target}_CMAKE_EXE_LINKER_FLAGS ${FUCHSIA_${target}_LINKER_FLAGS} CACHE STRING "")
    set(BUILTINS_${target}_CMAKE_SYSROOT ${FUCHSIA_${target}_SYSROOT} CACHE PATH "")
  endforeach()

  foreach(target x86_64-unknown-fuchsia;aarch64-unknown-fuchsia;riscv64-unknown-fuchsia)
    # Set the per-target runtimes options.
    list(APPEND RUNTIME_TARGETS "${target}")
    set(RUNTIMES_${target}_CMAKE_SYSTEM_NAME Fuchsia CACHE STRING "")
    set(RUNTIMES_${target}_CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "")
    set(RUNTIMES_${target}_CMAKE_BUILD_WITH_INSTALL_RPATH ON CACHE BOOL "")
    set(RUNTIMES_${target}_CMAKE_ASM_FLAGS ${FUCHSIA_${target}_COMPILER_FLAGS} CACHE STRING "")
    set(RUNTIMES_${target}_CMAKE_C_FLAGS ${FUCHSIA_${target}_COMPILER_FLAGS} CACHE STRING "")
    set(RUNTIMES_${target}_CMAKE_CXX_FLAGS ${FUCHSIA_${target}_COMPILER_FLAGS} CACHE STRING "")
    set(RUNTIMES_${target}_CMAKE_SHARED_LINKER_FLAGS ${FUCHSIA_${target}_LINKER_FLAGS} CACHE STRING "")
    set(RUNTIMES_${target}_CMAKE_MODULE_LINKER_FLAGS ${FUCHSIA_${target}_LINKER_FLAGS} CACHE STRING "")
    set(RUNTIMES_${target}_CMAKE_EXE_LINKER_FLAGS ${FUCHSIA_${target}_LINKER_FLAGS} CACHE STRING "")
    set(RUNTIMES_${target}_CMAKE_SYSROOT ${FUCHSIA_${target}_SYSROOT} CACHE PATH "")
    set(RUNTIMES_${target}_COMPILER_RT_CXX_LIBRARY "libcxx" CACHE STRING "")
    set(RUNTIMES_${target}_COMPILER_RT_USE_BUILTINS_LIBRARY ON CACHE BOOL "")
    set(RUNTIMES_${target}_COMPILER_RT_USE_LLVM_UNWINDER ON CACHE BOOL "")
    set(RUNTIMES_${target}_LIBUNWIND_USE_COMPILER_RT ON CACHE BOOL "")
    set(RUNTIMES_${target}_LIBUNWIND_HIDE_SYMBOLS ON CACHE BOOL "")
    set(RUNTIMES_${target}_LIBCXXABI_USE_COMPILER_RT ON CACHE BOOL "")
    set(RUNTIMES_${target}_LIBCXXABI_USE_LLVM_UNWINDER ON CACHE BOOL "")
    set(RUNTIMES_${target}_LIBCXXABI_HERMETIC_STATIC_LIBRARY ON CACHE BOOL "")
    set(RUNTIMES_${target}_LIBCXXABI_INSTALL_STATIC_LIBRARY OFF CACHE BOOL "")
    set(RUNTIMES_${target}_LIBCXX_USE_COMPILER_RT ON CACHE BOOL "")
    set(RUNTIMES_${target}_LIBCXX_ENABLE_STATIC_ABI_LIBRARY ON CACHE BOOL "")
    set(RUNTIMES_${target}_LIBCXX_HERMETIC_STATIC_LIBRARY ON CACHE BOOL "")
    set(RUNTIMES_${target}_LIBCXX_STATICALLY_LINK_ABI_IN_SHARED_LIBRARY OFF CACHE BOOL "")
    set(RUNTIMES_${target}_LIBCXX_ABI_VERSION 2 CACHE STRING "")
    set(RUNTIMES_${target}_LLVM_ENABLE_ASSERTIONS OFF CACHE BOOL "")
    set(RUNTIMES_${target}_LLVM_ENABLE_RUNTIMES "compiler-rt;libcxx;libcxxabi;libunwind" CACHE STRING "")

    # Compat multilibs.
    set(RUNTIMES_${target}+compat_LLVM_BUILD_COMPILER_RT OFF CACHE BOOL "")
    set(RUNTIMES_${target}+compat_LIBCXXABI_ENABLE_EXCEPTIONS OFF CACHE BOOL "")
    set(RUNTIMES_${target}+compat_LIBCXX_ENABLE_EXCEPTIONS OFF CACHE BOOL "")
    set(RUNTIMES_${target}+compat_CMAKE_CXX_FLAGS "${FUCHSIA_${target}_COMPILER_FLAGS} -fc++-abi=itanium" CACHE STRING "")

    set(RUNTIMES_${target}+asan_LLVM_BUILD_COMPILER_RT OFF CACHE BOOL "")
    set(RUNTIMES_${target}+asan_LLVM_USE_SANITIZER "Address" CACHE STRING "")
    set(RUNTIMES_${target}+asan_LIBCXXABI_ENABLE_NEW_DELETE_DEFINITIONS OFF CACHE BOOL "")
    set(RUNTIMES_${target}+asan_LIBCXX_ENABLE_NEW_DELETE_DEFINITIONS OFF CACHE BOOL "")

    set(RUNTIMES_${target}+noexcept_LLVM_BUILD_COMPILER_RT OFF CACHE BOOL "")
    set(RUNTIMES_${target}+noexcept_LIBCXXABI_ENABLE_EXCEPTIONS OFF CACHE BOOL "")
    set(RUNTIMES_${target}+noexcept_LIBCXX_ENABLE_EXCEPTIONS OFF CACHE BOOL "")

    set(RUNTIMES_${target}+asan+noexcept_LLVM_BUILD_COMPILER_RT OFF CACHE BOOL "")
    set(RUNTIMES_${target}+asan+noexcept_LLVM_USE_SANITIZER "Address" CACHE STRING "")
    set(RUNTIMES_${target}+asan+noexcept_LIBCXXABI_ENABLE_NEW_DELETE_DEFINITIONS OFF CACHE BOOL "")
    set(RUNTIMES_${target}+asan+noexcept_LIBCXX_ENABLE_NEW_DELETE_DEFINITIONS OFF CACHE BOOL "")
    set(RUNTIMES_${target}+asan+noexcept_LIBCXXABI_ENABLE_EXCEPTIONS OFF CACHE BOOL "")
    set(RUNTIMES_${target}+asan+noexcept_LIBCXX_ENABLE_EXCEPTIONS OFF CACHE BOOL "")

    # Use .build-id link.
    list(APPEND RUNTIME_BUILD_ID_LINK "${target}")
  endforeach()

  # HWAsan
  set(RUNTIMES_aarch64-unknown-fuchsia+hwasan_LLVM_BUILD_COMPILER_RT OFF CACHE BOOL "")
  set(RUNTIMES_aarch64-unknown-fuchsia+hwasan_LLVM_USE_SANITIZER "HWAddress" CACHE STRING "")
  set(RUNTIMES_aarch64-unknown-fuchsia+hwasan_LIBCXXABI_ENABLE_NEW_DELETE_DEFINITIONS OFF CACHE BOOL "")
  set(RUNTIMES_aarch64-unknown-fuchsia+hwasan_LIBCXX_ENABLE_NEW_DELETE_DEFINITIONS OFF CACHE BOOL "")

  # HWASan+noexcept
  set(RUNTIMES_aarch64-unknown-fuchsia+hwasan+noexcept_LLVM_BUILD_COMPILER_RT OFF CACHE BOOL "")
  set(RUNTIMES_aarch64-unknown-fuchsia+hwasan+noexcept_LLVM_USE_SANITIZER "HWAddress" CACHE STRING "")
  set(RUNTIMES_aarch64-unknown-fuchsia+hwasan+noexcept_LIBCXXABI_ENABLE_NEW_DELETE_DEFINITIONS OFF CACHE BOOL "")
  set(RUNTIMES_aarch64-unknown-fuchsia+hwasan+noexcept_LIBCXX_ENABLE_NEW_DELETE_DEFINITIONS OFF CACHE BOOL "")
  set(RUNTIMES_aarch64-unknown-fuchsia+hwasan+noexcept_LIBCXXABI_ENABLE_EXCEPTIONS OFF CACHE BOOL "")
  set(RUNTIMES_aarch64-unknown-fuchsia+hwasan+noexcept_LIBCXX_ENABLE_EXCEPTIONS OFF CACHE BOOL "")

  set(LLVM_RUNTIME_MULTILIBS "asan;noexcept;compat;asan+noexcept;hwasan;hwasan+noexcept" CACHE STRING "")

  set(LLVM_RUNTIME_MULTILIB_asan_TARGETS "x86_64-unknown-fuchsia;aarch64-unknown-fuchsia;riscv64-unknown-fuchsia" CACHE STRING "")
  set(LLVM_RUNTIME_MULTILIB_noexcept_TARGETS "x86_64-unknown-fuchsia;aarch64-unknown-fuchsia;riscv64-unknown-fuchsia" CACHE STRING "")
  set(LLVM_RUNTIME_MULTILIB_compat_TARGETS "x86_64-unknown-fuchsia;aarch64-unknown-fuchsia;riscv64-unknown-fuchsia" CACHE STRING "")
  set(LLVM_RUNTIME_MULTILIB_asan+noexcept_TARGETS "x86_64-unknown-fuchsia;aarch64-unknown-fuchsia;riscv64-unknown-fuchsia" CACHE STRING "")
  set(LLVM_RUNTIME_MULTILIB_hwasan_TARGETS "aarch64-unknown-fuchsia;riscv64-unknown-fuchsia" CACHE STRING "")
  set(LLVM_RUNTIME_MULTILIB_hwasan+noexcept_TARGETS "aarch64-unknown-fuchsia;riscv64-unknown-fuchsia" CACHE STRING "")
endif()

foreach(target armv6m-unknown-eabi;armv7m-unknown-eabi;armv8m.main-unknown-eabi)
  list(APPEND BUILTIN_TARGETS "${target}")
  set(BUILTINS_${target}_CMAKE_SYSTEM_NAME Generic CACHE STRING "")
  set(BUILTINS_${target}_CMAKE_SYSTEM_PROCESSOR arm CACHE STRING "")
  set(BUILTINS_${target}_CMAKE_SYSROOT "" CACHE STRING "")
  set(BUILTINS_${target}_CMAKE_BUILD_TYPE MinSizeRel CACHE STRING "")
  foreach(lang C;CXX;ASM)
    set(BUILTINS_${target}_CMAKE_${lang}_local_flags "--target=${target} -mthumb")
    if(${target} STREQUAL "armv8m.main-unknown-eabi")
      set(BUILTINS_${target}_CMAKE_${lang}_local_flags "${BUILTINS_${target}_CMAKE_${lang}_local_flags} -mfloat-abi=hard -march=armv8m.main+fp+dsp -mcpu=cortex-m33" CACHE STRING "")
    endif()
    set(BUILTINS_${target}_CMAKE_${lang}_FLAGS "${BUILTINS_${target}_CMAKE_${lang}_local_flags}" CACHE STRING "")
  endforeach()
  foreach(type SHARED;MODULE;EXE)
    set(BUILTINS_${target}_CMAKE_${type}_LINKER_FLAGS "-fuse-ld=lld" CACHE STRING "")
  endforeach()
  set(BUILTINS_${target}_COMPILER_RT_BAREMETAL_BUILD ON CACHE BOOL "")

  list(APPEND RUNTIME_TARGETS "${target}")
  set(RUNTIMES_${target}_CMAKE_SYSTEM_NAME Generic CACHE STRING "")
  set(RUNTIMES_${target}_CMAKE_SYSTEM_PROCESSOR arm CACHE STRING "")
  set(RUNTIMES_${target}_CMAKE_SYSROOT "" CACHE STRING "")
  set(RUNTIMES_${target}_CMAKE_BUILD_TYPE MinSizeRel CACHE STRING "")
  set(RUNTIMES_${target}_CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY CACHE STRING "")
  foreach(lang C;CXX;ASM)
    # TODO: The preprocessor defines workaround various issues in libc and libc++ integration.
    # These should be addressed and removed over time.
    set(RUNTIMES_${target}_CMAKE_${lang}_FLAGS "--target=${target} -mthumb -Wno-atomic-alignment \"-Dvfprintf(stream, format, vlist)=vprintf(format, vlist)\" \"-Dfprintf(stream, format, ...)=printf(format)\" \"-Dtimeval=struct timeval{int tv_sec; int tv_usec;}\" \"-Dgettimeofday(tv, tz)\" -D_LIBCPP_PRINT=1" CACHE STRING "")
  endforeach()
  foreach(type SHARED;MODULE;EXE)
    set(RUNTIMES_${target}_CMAKE_${type}_LINKER_FLAGS "-fuse-ld=lld" CACHE STRING "")
  endforeach()
  set(RUNTIMES_${target}_LLVM_LIBC_FULL_BUILD ON CACHE BOOL "")
  set(RUNTIMES_${target}_LIBC_ENABLE_USE_BY_CLANG ON CACHE BOOL "")
  set(RUNTIMES_${target}_LIBC_USE_NEW_HEADER_GEN OFF CACHE BOOL "")
  set(RUNTIMES_${target}_LIBCXX_ABI_VERSION 2 CACHE STRING "")
  set(RUNTIMES_${target}_LIBCXX_CXX_ABI none CACHE STRING "")
  set(RUNTIMES_${target}_LIBCXX_ENABLE_SHARED OFF CACHE BOOL "")
  set(RUNTIMES_${target}_LIBCXX_ENABLE_STATIC ON CACHE BOOL "")
  set(RUNTIMES_${target}_LIBCXX_ENABLE_NEW_DELETE_DEFINITIONS ON CACHE BOOL "")
  set(RUNTIMES_${target}_LIBCXX_LIBC "llvm-libc" CACHE STRING "")
  set(RUNTIMES_${target}_LIBCXX_ENABLE_FILESYSTEM OFF CACHE BOOL "")
  set(RUNTIMES_${target}_LIBCXX_ENABLE_RANDOM_DEVICE OFF CACHE BOOL "")
  set(RUNTIMES_${target}_LIBCXX_ENABLE_LOCALIZATION OFF CACHE BOOL "")
  set(RUNTIMES_${target}_LIBCXX_ENABLE_UNICODE OFF CACHE BOOL "")
  set(RUNTIMES_${target}_LIBCXX_ENABLE_WIDE_CHARACTERS OFF CACHE BOOL "")
  set(RUNTIMES_${target}_LIBCXX_ENABLE_EXCEPTIONS OFF CACHE BOOL "")
  set(RUNTIMES_${target}_LIBCXX_ENABLE_RTTI OFF CACHE BOOL "")
  set(RUNTIMES_${target}_LIBCXX_ENABLE_THREADS OFF CACHE BOOL "")
  set(RUNTIMES_${target}_LIBCXX_ENABLE_MONOTONIC_CLOCK OFF CACHE BOOL "")
  set(RUNTIMES_${target}_LIBCXX_USE_COMPILER_RT ON CACHE BOOL "")
  set(RUNTIMES_${target}_LLVM_INCLUDE_TESTS OFF CACHE BOOL "")
  set(RUNTIMES_${target}_LLVM_ENABLE_ASSERTIONS OFF CACHE BOOL "")
  set(RUNTIMES_${target}_LLVM_ENABLE_RUNTIMES "libc;libcxx" CACHE STRING "")
endforeach()

foreach(target riscv32-unknown-elf)
  list(APPEND BUILTIN_TARGETS "${target}")
  set(BUILTINS_${target}_CMAKE_SYSTEM_NAME Generic CACHE STRING "")
  set(BUILTINS_${target}_CMAKE_SYSTEM_PROCESSOR RISCV CACHE STRING "")
  set(BUILTINS_${target}_CMAKE_SYSROOT "" CACHE STRING "")
  set(BUILTINS_${target}_CMAKE_BUILD_TYPE MinSizeRel CACHE STRING "")
  foreach(lang C;CXX;ASM)
    set(BUILTINS_${target}_CMAKE_${lang}_FLAGS "--target=${target} -march=rv32imafc -mabi=ilp32f" CACHE STRING "")
  endforeach()
  foreach(type SHARED;MODULE;EXE)
    set(BUILTINS_${target}_CMAKE_${type}_LINKER_FLAGS "-fuse-ld=lld" CACHE STRING "")
  endforeach()
  set(BUILTINS_${target}_COMPILER_RT_BAREMETAL_BUILD ON CACHE BOOL "")

  list(APPEND RUNTIME_TARGETS "${target}")
  set(RUNTIMES_${target}_CMAKE_SYSTEM_NAME Generic CACHE STRING "")
  set(RUNTIMES_${target}_CMAKE_SYSTEM_PROCESSOR RISCV CACHE STRING "")
  set(RUNTIMES_${target}_CMAKE_SYSROOT "" CACHE STRING "")
  set(RUNTIMES_${target}_CMAKE_BUILD_TYPE MinSizeRel CACHE STRING "")
  set(RUNTIMES_${target}_CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY CACHE STRING "")
  foreach(lang C;CXX;ASM)
    # TODO: The preprocessor defines workaround various issues in libc and libc++ integration.
    # These should be addressed and removed over time.
    set(RUNTIMES_${target}_CMAKE_${lang}_FLAGS "--target=${target} -march=rv32imafc -mabi=ilp32f \"-Dvfprintf(stream, format, vlist)=vprintf(format, vlist)\" \"-Dfprintf(stream, format, ...)=printf(format)\" \"-Dtimeval=struct timeval{int tv_sec; int tv_usec;}\" \"-Dgettimeofday(tv, tz)\" -D_LIBCPP_PRINT=1" CACHE STRING "")
  endforeach()
  foreach(type SHARED;MODULE;EXE)
    set(RUNTIMES_${target}_CMAKE_${type}_LINKER_FLAGS "-fuse-ld=lld" CACHE STRING "")
  endforeach()
  set(RUNTIMES_${target}_LLVM_LIBC_FULL_BUILD ON CACHE BOOL "")
  set(RUNTIMES_${target}_LIBC_ENABLE_USE_BY_CLANG ON CACHE BOOL "")
  set(RUNTIMES_${target}_LIBC_USE_NEW_HEADER_GEN OFF CACHE BOOL "")
  set(RUNTIMES_${target}_LIBCXX_ABI_VERSION 2 CACHE STRING "")
  set(RUNTIMES_${target}_LIBCXX_CXX_ABI none CACHE STRING "")
  set(RUNTIMES_${target}_LIBCXX_ENABLE_SHARED OFF CACHE BOOL "")
  set(RUNTIMES_${target}_LIBCXX_ENABLE_STATIC ON CACHE BOOL "")
  set(RUNTIMES_${target}_LIBCXX_ENABLE_NEW_DELETE_DEFINITIONS ON CACHE BOOL "")
  set(RUNTIMES_${target}_LIBCXX_LIBC "llvm-libc" CACHE STRING "")
  set(RUNTIMES_${target}_LIBCXX_ENABLE_FILESYSTEM OFF CACHE BOOL "")
  set(RUNTIMES_${target}_LIBCXX_ENABLE_RANDOM_DEVICE OFF CACHE BOOL "")
  set(RUNTIMES_${target}_LIBCXX_ENABLE_LOCALIZATION OFF CACHE BOOL "")
  set(RUNTIMES_${target}_LIBCXX_ENABLE_UNICODE OFF CACHE BOOL "")
  set(RUNTIMES_${target}_LIBCXX_ENABLE_WIDE_CHARACTERS OFF CACHE BOOL "")
  set(RUNTIMES_${target}_LIBCXX_ENABLE_EXCEPTIONS OFF CACHE BOOL "")
  set(RUNTIMES_${target}_LIBCXX_ENABLE_RTTI OFF CACHE BOOL "")
  set(RUNTIMES_${target}_LIBCXX_ENABLE_THREADS OFF CACHE BOOL "")
  set(RUNTIMES_${target}_LIBCXX_ENABLE_MONOTONIC_CLOCK OFF CACHE BOOL "")
  set(RUNTIMES_${target}_LIBCXX_USE_COMPILER_RT ON CACHE BOOL "")
  set(RUNTIMES_${target}_LLVM_INCLUDE_TESTS OFF CACHE BOOL "")
  set(RUNTIMES_${target}_LLVM_ENABLE_ASSERTIONS OFF CACHE BOOL "")
  set(RUNTIMES_${target}_LLVM_ENABLE_RUNTIMES "libc;libcxx" CACHE STRING "")
endforeach()

set(LLVM_BUILTIN_TARGETS "${BUILTIN_TARGETS}" CACHE STRING "")
set(LLVM_RUNTIME_TARGETS "${RUNTIME_TARGETS}" CACHE STRING "")

# Setup toolchain.
set(LLVM_INSTALL_TOOLCHAIN_ONLY ON CACHE BOOL "")
set(LLVM_TOOLCHAIN_TOOLS
  dsymutil
  llvm-ar
  llvm-cov
  llvm-cxxfilt
  llvm-debuginfod
  llvm-debuginfod-find
  llvm-dlltool
  ${LLVM_DRIVER_TARGET}
  llvm-dwarfdump
  llvm-dwp
  llvm-ifs
  llvm-gsymutil
  llvm-lib
  llvm-libtool-darwin
  llvm-lipo
  llvm-ml
  llvm-mt
  llvm-nm
  llvm-objcopy
  llvm-objdump
  llvm-otool
  llvm-pdbutil
  llvm-profdata
  llvm-rc
  llvm-ranlib
  llvm-readelf
  llvm-readobj
  llvm-size
  llvm-strings
  llvm-strip
  llvm-symbolizer
  llvm-undname
  llvm-xray
  opt-viewer
  sancov
  scan-build-py
  CACHE STRING "")

set(LLVM_Toolchain_DISTRIBUTION_COMPONENTS
  bolt
  clang
  lld
  clang-apply-replacements
  clang-doc
  clang-format
  clang-resource-headers
  clang-include-fixer
  clang-refactor
  clang-scan-deps
  clang-tidy
  clangd
  find-all-symbols
  builtins
  runtimes
  ${LLVM_TOOLCHAIN_TOOLS}
  CACHE STRING "")

set(_FUCHSIA_DISTRIBUTIONS Toolchain)

if(FUCHSIA_ENABLE_LLDB)
  list(APPEND _FUCHSIA_ENABLE_PROJECTS lldb)
  list(APPEND _FUCHSIA_DISTRIBUTIONS Debugger)
  set(_FUCHSIA_LLDB_COMPONENTS
    lldb
    liblldb
    lldb-server
    lldb-argdumper
    lldb-dap
  )
  if(LLDB_ENABLE_PYTHON)
    list(APPEND _FUCHSIA_LLDB_COMPONENTS lldb-python-scripts)
  endif()
  set(LLVM_Debugger_DISTRIBUTION_COMPONENTS ${_FUCHSIA_LLDB_COMPONENTS} CACHE STRING "")
endif()

set(LLVM_DISTRIBUTIONS ${_FUCHSIA_DISTRIBUTIONS} CACHE STRING "")
set(LLVM_ENABLE_PROJECTS ${_FUCHSIA_ENABLE_PROJECTS} CACHE STRING "")
