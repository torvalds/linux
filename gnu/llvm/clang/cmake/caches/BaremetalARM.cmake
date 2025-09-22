set(LLVM_TARGETS_TO_BUILD ARM;X86 CACHE STRING "")

# Builtins
set(LLVM_BUILTIN_TARGETS "armv7m-none-eabi;armv6m-none-eabi;armv7em-none-eabi" CACHE STRING "Builtin Targets")

set(BUILTINS_armv6m-none-eabi_CMAKE_SYSROOT ${BAREMETAL_ARMV6M_SYSROOT} CACHE STRING "armv6m-none-eabi Sysroot")
set(BUILTINS_armv6m-none-eabi_CMAKE_SYSTEM_NAME Generic CACHE STRING "armv6m-none-eabi System Name")
set(BUILTINS_armv6m-none-eabi_COMPILER_RT_BAREMETAL_BUILD ON CACHE BOOL "armv6m-none-eabi Baremetal build")
set(BUILTINS_armv6m-none-eabi_COMPILER_RT_OS_DIR "baremetal" CACHE STRING "armv6m-none-eabi os dir")

set(BUILTINS_armv7m-none-eabi_CMAKE_SYSROOT ${BAREMETAL_ARMV7M_SYSROOT} CACHE STRING "armv7m-none-eabi Sysroot")
set(BUILTINS_armv7m-none-eabi_CMAKE_SYSTEM_NAME Generic CACHE STRING "armv7m-none-eabi System Name")
set(BUILTINS_armv7m-none-eabi_COMPILER_RT_BAREMETAL_BUILD ON CACHE BOOL "armv7m-none-eabi Baremetal build")
set(BUILTINS_armv7m-none-eabi_CMAKE_C_FLAGS "-mfpu=fp-armv8" CACHE STRING "armv7m-none-eabi C Flags")
set(BUILTINS_armv7m-none-eabi_CMAKE_ASM_FLAGS "-mfpu=fp-armv8" CACHE STRING "armv7m-none-eabi ASM Flags")
set(BUILTINS_armv7m-none-eabi_COMPILER_RT_OS_DIR "baremetal" CACHE STRING "armv7m-none-eabi os dir")

set(BUILTINS_armv7em-none-eabi_CMAKE_SYSROOT ${BAREMETAL_ARMV7EM_SYSROOT} CACHE STRING "armv7em-none-eabi Sysroot")
set(BUILTINS_armv7em-none-eabi_CMAKE_SYSTEM_NAME Generic CACHE STRING "armv7em-none-eabi System Name")
set(BUILTINS_armv7em-none-eabi_COMPILER_RT_BAREMETAL_BUILD ON CACHE BOOL "armv7em-none-eabi Baremetal build")
set(BUILTINS_armv7em-none-eabi_CMAKE_C_FLAGS "-mfpu=fp-armv8" CACHE STRING "armv7em-none-eabi C Flags")
set(BUILTINS_armv7em-none-eabi_CMAKE_ASM_FLAGS "-mfpu=fp-armv8" CACHE STRING "armv7em-none-eabi ASM Flags")
set(BUILTINS_armv7em-none-eabi_COMPILER_RT_OS_DIR "baremetal" CACHE STRING "armv7em-none-eabi os dir")

set(LLVM_INSTALL_TOOLCHAIN_ONLY ON CACHE BOOL "")
set(LLVM_TOOLCHAIN_TOOLS
  dsymutil
  llc
  llvm-ar
  llvm-cxxfilt
  llvm-dwarfdump
  llvm-nm
  llvm-objdump
  llvm-pdbutil
  llvm-ranlib
  llvm-readobj
  llvm-size
  llvm-symbolizer
  opt
  CACHE STRING "")

set(LLVM_DISTRIBUTION_COMPONENTS
  clang
  lld
  clang-resource-headers
  builtins-armv6m-none-eabi
  builtins-armv7m-none-eabi
  builtins-armv7em-none-eabi
  runtimes
  ${LLVM_TOOLCHAIN_TOOLS}
  CACHE STRING "")
