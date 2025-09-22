# The CompilerRT build system requires CMake version 2.8.8 or higher in order
# to use its support for building convenience "libraries" as a collection of
# .o files. This is particularly useful in producing larger, more complex
# runtime libraries.

include(BuiltinTests)
include(CheckIncludeFile)
include(CheckCXXSourceCompiles)
include(GNUInstallDirs)
include(GetClangResourceDir)
include(ExtendPath)
include(CompilerRTDarwinUtils)

check_include_file(unwind.h HAVE_UNWIND_H)

# Used by sanitizer_common and tests.
check_include_file(rpc/xdr.h HAVE_RPC_XDR_H)
if (NOT HAVE_RPC_XDR_H)
  set(HAVE_RPC_XDR_H 0)
endif()

# Top level target used to build all compiler-rt libraries.
add_custom_target(compiler-rt ALL)
add_custom_target(install-compiler-rt)
add_custom_target(install-compiler-rt-stripped)
set_property(TARGET compiler-rt PROPERTY FOLDER "Compiler-RT/Metatargets")
set_property(
  TARGET
    install-compiler-rt
    install-compiler-rt-stripped
  PROPERTY
    FOLDER "Compiler-RT/Installation"
)

# Setting these variables from an LLVM build is sufficient that compiler-rt can
# construct the output paths, so it can behave as if it were in-tree here.
if (LLVM_LIBRARY_OUTPUT_INTDIR AND LLVM_RUNTIME_OUTPUT_INTDIR AND PACKAGE_VERSION)
  set(LLVM_TREE_AVAILABLE On)
endif()

if (LLVM_TREE_AVAILABLE)
  # Setup the paths where compiler-rt runtimes and headers should be stored.
  get_clang_resource_dir(COMPILER_RT_OUTPUT_DIR PREFIX ${LLVM_LIBRARY_OUTPUT_INTDIR}/..)
  set(COMPILER_RT_EXEC_OUTPUT_DIR ${LLVM_RUNTIME_OUTPUT_INTDIR})
  get_clang_resource_dir(COMPILER_RT_INSTALL_PATH)
  option(COMPILER_RT_INCLUDE_TESTS "Generate and build compiler-rt tests."
         ${LLVM_INCLUDE_TESTS})
  option(COMPILER_RT_ENABLE_WERROR "Fail and stop if warning is triggered"
         ${LLVM_ENABLE_WERROR})

  # Use just-built Clang to compile/link tests on all platforms.
  if (CMAKE_CROSSCOMPILING)
    if (CMAKE_HOST_WIN32)
      set(_host_executable_suffix ".exe")
    else()
      set(_host_executable_suffix "")
    endif()
  else()
    set(_host_executable_suffix ${CMAKE_EXECUTABLE_SUFFIX})
  endif()
  set(COMPILER_RT_TEST_COMPILER
    ${LLVM_RUNTIME_OUTPUT_INTDIR}/clang${_host_executable_suffix})
  set(COMPILER_RT_TEST_CXX_COMPILER
    ${LLVM_RUNTIME_OUTPUT_INTDIR}/clang++${_host_executable_suffix})
else()
    # Take output dir and install path from the user.
  set(COMPILER_RT_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR} CACHE PATH
    "Path where built compiler-rt libraries should be stored.")
  set(COMPILER_RT_EXEC_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/bin CACHE PATH
    "Path where built compiler-rt executables should be stored.")
  set(COMPILER_RT_INSTALL_PATH "" CACHE PATH
    "Prefix for directories where built compiler-rt artifacts should be installed.")
  option(COMPILER_RT_INCLUDE_TESTS "Generate and build compiler-rt tests." OFF)
  option(COMPILER_RT_ENABLE_WERROR "Fail and stop if warning is triggered" OFF)
  # Use a host compiler to compile/link tests.
  set(COMPILER_RT_TEST_COMPILER ${CMAKE_C_COMPILER} CACHE PATH "Compiler to use for testing")
  set(COMPILER_RT_TEST_CXX_COMPILER ${CMAKE_CXX_COMPILER} CACHE PATH "C++ Compiler to use for testing")
endif()

if("${COMPILER_RT_TEST_COMPILER}" MATCHES "clang[+]*$")
  set(COMPILER_RT_TEST_COMPILER_ID Clang)
elseif("${COMPILER_RT_TEST_COMPILER}" MATCHES "clang.*.exe$")
  set(COMPILER_RT_TEST_COMPILER_ID Clang)
else()
  set(COMPILER_RT_TEST_COMPILER_ID GNU)
endif()

if(NOT DEFINED COMPILER_RT_OS_DIR)
  if(ANDROID)
    # The CMAKE_SYSTEM_NAME for Android is Android, but the OS is Linux and the
    # driver will search for compiler-rt libraries in the "linux" directory.
    set(COMPILER_RT_OS_DIR linux)
  else()
    string(TOLOWER ${CMAKE_SYSTEM_NAME} COMPILER_RT_OS_DIR)
  endif()
endif()
if(LLVM_ENABLE_PER_TARGET_RUNTIME_DIR AND NOT APPLE)
  set(COMPILER_RT_OUTPUT_LIBRARY_DIR
    ${COMPILER_RT_OUTPUT_DIR}/lib)
  extend_path(default_install_path "${COMPILER_RT_INSTALL_PATH}" lib)
  set(COMPILER_RT_INSTALL_LIBRARY_DIR "${default_install_path}" CACHE PATH
    "Path where built compiler-rt libraries should be installed.")
else(LLVM_ENABLE_PER_TARGET_RUNTIME_DIR AND NOT APPLE)
  set(COMPILER_RT_OUTPUT_LIBRARY_DIR
    ${COMPILER_RT_OUTPUT_DIR}/lib/${COMPILER_RT_OS_DIR})
  extend_path(default_install_path "${COMPILER_RT_INSTALL_PATH}" "lib/${COMPILER_RT_OS_DIR}")
  set(COMPILER_RT_INSTALL_LIBRARY_DIR "${default_install_path}" CACHE PATH
    "Path where built compiler-rt libraries should be installed.")
endif()
extend_path(default_install_path "${COMPILER_RT_INSTALL_PATH}" "${CMAKE_INSTALL_BINDIR}")
set(COMPILER_RT_INSTALL_BINARY_DIR "${default_install_path}" CACHE PATH
  "Path where built compiler-rt executables should be installed.")
extend_path(default_install_path "${COMPILER_RT_INSTALL_PATH}" "${CMAKE_INSTALL_INCLUDEDIR}")
set(COMPILER_RT_INSTALL_INCLUDE_DIR "${default_install_path}" CACHE PATH
  "Path where compiler-rt headers should be installed.")
extend_path(default_install_path "${COMPILER_RT_INSTALL_PATH}" "${CMAKE_INSTALL_DATADIR}")
set(COMPILER_RT_INSTALL_DATA_DIR "${default_install_path}" CACHE PATH
  "Path where compiler-rt data files should be installed.")

if(APPLE)
  # On Darwin if /usr/include/c++ doesn't exist, the user probably has Xcode but
  # not the command line tools (or is using macOS 10.14 or newer). If this is
  # the case, we need to find the OS X sysroot to pass to clang.
  if(NOT EXISTS /usr/include/c++)
    execute_process(COMMAND xcrun -sdk macosx --show-sdk-path
       OUTPUT_VARIABLE OSX_SYSROOT
       ERROR_QUIET
       OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (NOT OSX_SYSROOT OR NOT EXISTS ${OSX_SYSROOT})
      message(WARNING "Detected OSX_SYSROOT ${OSX_SYSROOT} does not exist")
    else()
      message(STATUS "Found OSX_SYSROOT: ${OSX_SYSROOT}")
      set(OSX_SYSROOT_FLAG "-isysroot${OSX_SYSROOT}")
    endif()
  else()
    set(OSX_SYSROOT_FLAG "")
  endif()

  try_compile_only(COMPILER_RT_HAS_DARWIN_TARGET_VARIANT_FLAG
                   FLAGS
                   "-target" "x86_64-apple-macos10.15"
                   "-darwin-target-variant" "x86_64-apple-ios13.1-macabi"
                   "-Werror")
  option(COMPILER_RT_ENABLE_MACCATALYST "Enable building for Mac Catalyst" ${COMPILER_RT_HAS_DARWIN_TARGET_VARIANT_FLAG})

  # Don't enable COMPILER_RT_ENABLE_IOS if we can't find the sdk dir.
  # This can happen when you only have the commandline tools installed
  # which doesn't come with the iOS SDK.
  find_darwin_sdk_dir(HAS_IOS_SDK "iphoneos")
  set(COMPILER_RT_ENABLE_IOS_DEFAULT On)
  if("${HAS_IOS_SDK}" STREQUAL "")
    message(WARNING "iOS SDK not found! Building compiler-rt without iOS support.")
    set(COMPILER_RT_ENABLE_IOS_DEFAULT Off)
  endif()
  option(COMPILER_RT_ENABLE_IOS "Enable building for iOS" ${COMPILER_RT_ENABLE_IOS_DEFAULT})

  option(COMPILER_RT_ENABLE_WATCHOS "Enable building for watchOS - Experimental" Off)
  option(COMPILER_RT_ENABLE_TVOS "Enable building for tvOS - Experimental" Off)
  option(COMPILER_RT_ENABLE_XROS "Enable building for xrOS - Experimental" Off)

else()
  option(COMPILER_RT_DEFAULT_TARGET_ONLY "Build builtins only for the default target" Off)
endif()

if(WIN32 AND NOT MINGW AND NOT CYGWIN)
  set(CMAKE_SHARED_LIBRARY_PREFIX_C "")
  set(CMAKE_SHARED_LIBRARY_PREFIX_CXX "")
  set(CMAKE_STATIC_LIBRARY_PREFIX_C "")
  set(CMAKE_STATIC_LIBRARY_PREFIX_CXX "")
  set(CMAKE_STATIC_LIBRARY_SUFFIX_C ".lib")
  set(CMAKE_STATIC_LIBRARY_SUFFIX_CXX ".lib")
endif()

macro(test_targets)
  # Find and run MSVC (not clang-cl) and get its version. This will tell clang-cl
  # what version of MSVC to pretend to be so that the STL works.
  set(MSVC_VERSION_FLAG "")
  if (MSVC)
    execute_process(COMMAND "$ENV{VSINSTALLDIR}/VC/bin/cl.exe"
      OUTPUT_QUIET
      ERROR_VARIABLE MSVC_COMPAT_VERSION
      )
    string(REGEX REPLACE "^.*Compiler Version ([0-9.]+) for .*$" "\\1"
      MSVC_COMPAT_VERSION "${MSVC_COMPAT_VERSION}")
    if (MSVC_COMPAT_VERSION MATCHES "^[0-9].+$")
      set(MSVC_VERSION_FLAG "-fms-compatibility-version=${MSVC_COMPAT_VERSION}")
      # Add this flag into the host build if this is clang-cl.
      if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        append("${MSVC_VERSION_FLAG}" CMAKE_C_FLAGS CMAKE_CXX_FLAGS)
      elseif (COMPILER_RT_TEST_COMPILER_ID MATCHES "Clang")
        # Add this flag to test compiles to suppress clang's auto-detection
        # logic.
        append("${MSVC_VERSION_FLAG}" COMPILER_RT_TEST_COMPILER_CFLAGS)
      endif()
    endif()
  endif()

  # Generate the COMPILER_RT_SUPPORTED_ARCH list.
  if(ANDROID)
    # Examine compiler output to determine target architecture.
    detect_target_arch()
    set(COMPILER_RT_OS_SUFFIX "-android")
  elseif(NOT APPLE) # Supported archs for Apple platforms are generated later
    if(COMPILER_RT_DEFAULT_TARGET_ONLY)
      add_default_target_arch(${COMPILER_RT_DEFAULT_TARGET_ARCH})
    elseif("${COMPILER_RT_DEFAULT_TARGET_ARCH}" MATCHES "i[2-6]86|x86|amd64")
      if(NOT MSVC)
        test_target_arch(x86_64 "" "-m64")
        test_target_arch(i386 __i386__ "-m32")
      else()
        if (CMAKE_SIZEOF_VOID_P EQUAL 4)
          test_target_arch(i386 "" "")
        else()
          test_target_arch(x86_64 "" "")
        endif()
      endif()
    elseif("${COMPILER_RT_DEFAULT_TARGET_ARCH}" MATCHES "amdgcn")
      test_target_arch(amdgcn "" "--target=amdgcn-amd-amdhsa" "-nogpulib"
                       "-flto" "-fconvergent-functions"
                       "-Xclang -mcode-object-version=none")
    elseif("${COMPILER_RT_DEFAULT_TARGET_ARCH}" MATCHES "hexagon")
      test_target_arch(hexagon "" "")
    elseif("${COMPILER_RT_DEFAULT_TARGET_ARCH}" MATCHES "loongarch64")
      test_target_arch(loongarch64 "" "")
    elseif("${COMPILER_RT_DEFAULT_TARGET_ARCH}" MATCHES "powerpc64le|ppc64le")
      test_target_arch(powerpc64le "" "-m64")
    elseif("${COMPILER_RT_DEFAULT_TARGET_ARCH}" MATCHES "powerpc")
      test_target_arch(powerpc "" "-m32")
      test_target_arch(powerpc64 "" "-m64")
    elseif("${COMPILER_RT_DEFAULT_TARGET_ARCH}" MATCHES "s390x")
      test_target_arch(s390x "" "")
    elseif("${COMPILER_RT_DEFAULT_TARGET_ARCH}" MATCHES "sparc")
      test_target_arch(sparc "" "-m32")
      test_target_arch(sparcv9 "" "-m64")
    elseif("${COMPILER_RT_DEFAULT_TARGET_ARCH}" MATCHES "mips")
      CHECK_SYMBOL_EXISTS (_MIPS_ARCH_MIPS32R6 "" COMPILER_RT_MIPS32R6)
      CHECK_SYMBOL_EXISTS (_MIPS_ARCH_MIPS64R6 "" COMPILER_RT_MIPS64R6)
      CHECK_SYMBOL_EXISTS (__mips64 "" COMPILER_RT_MIPS_64)
      CHECK_SYMBOL_EXISTS (__MIPSEL__ "" COMPILER_RT_MIPS_EL)
      if ("${COMPILER_RT_MIPS_64}")
        set(COMPILER_RT_DEFAULT_TARGET_ARCH "mips64")
      else()
        set(COMPILER_RT_DEFAULT_TARGET_ARCH "mips")
      endif()
      if ("${COMPILER_RT_MIPS_EL}")
        set(COMPILER_RT_DEFAULT_TARGET_ARCH "${COMPILER_RT_DEFAULT_TARGET_ARCH}el")
      endif()

      # FIXME: Ideally, we would build the N32 library too.
      if("${COMPILER_RT_MIPS_EL}" AND ("${COMPILER_RT_MIPS32R6}" OR "${COMPILER_RT_MIPS64R6}"))
        test_target_arch(mipsel "" "-mips32r6" "-mabi=32" "-D_LARGEFILE_SOURCE=1" "-D_FILE_OFFSET_BITS=64")
        test_target_arch(mips64el "" "-mips64r6" "-mabi=64")
      elseif("${COMPILER_RT_MIPS_EL}")
        test_target_arch(mipsel "" "-mips32r2" "-mabi=32" "-D_LARGEFILE_SOURCE=1" "-D_FILE_OFFSET_BITS=64")
        test_target_arch(mips64el "" "-mips64r2" "-mabi=64")
      elseif("${COMPILER_RT_MIPS32R6}" OR "${COMPILER_RT_MIPS64R6}")
        test_target_arch(mips "" "-mips32r6" "-mabi=32" "-D_LARGEFILE_SOURCE=1" "-D_FILE_OFFSET_BITS=64")
        test_target_arch(mips64 "" "-mips64r6" "-mabi=64")
      else()
        test_target_arch(mips "" "-mips32r2" "-mabi=32" "-D_LARGEFILE_SOURCE=1" "-D_FILE_OFFSET_BITS=64")
        test_target_arch(mips64 "" "-mips64r2" "-mabi=64")
      endif()
    elseif("${COMPILER_RT_DEFAULT_TARGET_ARCH}" MATCHES "nvptx")
      test_target_arch(nvptx64 "" "--nvptx64-nvidia-cuda" "-nogpulib" "-flto"
                       "-fconvergent-functions" "-c")
    elseif("${COMPILER_RT_DEFAULT_TARGET_ARCH}" MATCHES "arm")
      if(WIN32)
        test_target_arch(arm "" "" "")
      else()
        test_target_arch(armv4t "" "-march=armv4t" "-mfloat-abi=soft")
        test_target_arch(armv6m "" "-march=armv6m" "-mfloat-abi=soft")
        test_target_arch(arm "" "-march=armv7-a" "-mfloat-abi=soft")
        test_target_arch(armhf "" "-march=armv7-a" "-mfloat-abi=hard")
      endif()
    elseif("${COMPILER_RT_DEFAULT_TARGET_ARCH}" MATCHES "avr")
      test_target_arch(avr "__AVR__" "--target=avr")
    elseif("${COMPILER_RT_DEFAULT_TARGET_ARCH}" MATCHES "aarch32")
      test_target_arch(aarch32 "" "-march=armv8-a")
    elseif("${COMPILER_RT_DEFAULT_TARGET_ARCH}" MATCHES "aarch64")
      test_target_arch(aarch64 "" "-march=armv8-a")
    elseif("${COMPILER_RT_DEFAULT_TARGET_ARCH}" MATCHES "riscv32")
      test_target_arch(riscv32 "" "")
    elseif("${COMPILER_RT_DEFAULT_TARGET_ARCH}" MATCHES "riscv64")
      test_target_arch(riscv64 "" "")
    elseif("${COMPILER_RT_DEFAULT_TARGET_ARCH}" MATCHES "wasm32")
      test_target_arch(wasm32 "" "--target=wasm32-unknown-unknown")
    elseif("${COMPILER_RT_DEFAULT_TARGET_ARCH}" MATCHES "wasm64")
      test_target_arch(wasm64 "" "--target=wasm64-unknown-unknown")
    elseif("${COMPILER_RT_DEFAULT_TARGET_ARCH}" MATCHES "ve")
      test_target_arch(ve "__ve__" "--target=ve-unknown-none")
    endif()
    set(COMPILER_RT_OS_SUFFIX "")
  endif()
endmacro()
