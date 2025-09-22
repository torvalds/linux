include(BuiltinTests)
include(CheckIncludeFiles)
include(CheckCSourceCompiles)

# Make all the tests only check the compiler
set(TEST_COMPILE_ONLY On)

# Check host compiler support for certain flags
builtin_check_c_compiler_flag(-fPIC                 COMPILER_RT_HAS_FPIC_FLAG)
builtin_check_c_compiler_flag(-fPIE                 COMPILER_RT_HAS_FPIE_FLAG)
builtin_check_c_compiler_flag(-fno-builtin          COMPILER_RT_HAS_FNO_BUILTIN_FLAG)
builtin_check_c_compiler_flag(-std=c11              COMPILER_RT_HAS_STD_C11_FLAG)
builtin_check_c_compiler_flag(-fvisibility=hidden   COMPILER_RT_HAS_VISIBILITY_HIDDEN_FLAG)
builtin_check_c_compiler_flag(-fomit-frame-pointer  COMPILER_RT_HAS_OMIT_FRAME_POINTER_FLAG)
builtin_check_c_compiler_flag(-ffreestanding        COMPILER_RT_HAS_FFREESTANDING_FLAG)
builtin_check_c_compiler_flag(-fxray-instrument     COMPILER_RT_HAS_XRAY_COMPILER_FLAG)
builtin_check_c_compiler_flag(-fno-lto              COMPILER_RT_HAS_FNO_LTO_FLAG)
builtin_check_c_compiler_flag(-fno-profile-generate COMPILER_RT_HAS_FNO_PROFILE_GENERATE_FLAG)
builtin_check_c_compiler_flag(-fno-profile-instr-generate COMPILER_RT_HAS_FNO_PROFILE_INSTR_GENERATE_FLAG)
builtin_check_c_compiler_flag(-fno-profile-instr-use COMPILER_RT_HAS_FNO_PROFILE_INSTR_USE_FLAG)
builtin_check_c_compiler_flag(-Wno-pedantic         COMPILER_RT_HAS_WNO_PEDANTIC)
builtin_check_c_compiler_flag(-nogpulib             COMPILER_RT_HAS_NOGPULIB_FLAG)
builtin_check_c_compiler_flag(-flto                 COMPILER_RT_HAS_FLTO_FLAG)
builtin_check_c_compiler_flag(-fconvergent-functions COMPILER_RT_HAS_FCONVERGENT_FUNCTIONS_FLAG)
builtin_check_c_compiler_flag("-Xclang -mcode-object-version=none" COMPILER_RT_HAS_CODE_OBJECT_VERSION_FLAG)
builtin_check_c_compiler_flag(-Wbuiltin-declaration-mismatch COMPILER_RT_HAS_WBUILTIN_DECLARATION_MISMATCH_FLAG)
builtin_check_c_compiler_flag(/Zl COMPILER_RT_HAS_ZL_FLAG)

builtin_check_c_compiler_source(COMPILER_RT_HAS_ATOMIC_KEYWORD
"
int foo(int x, int y) {
 _Atomic int result = x * y;
 return result;
}
")

builtin_check_c_compiler_source(COMPILER_RT_HAS_ASM_LSE
"
asm(\".arch armv8-a+lse\");
asm(\"cas w0, w1, [x2]\");
")

builtin_check_c_compiler_source(COMPILER_RT_HAS_AARCH64_SME
"
void foo(void)  __arm_streaming_compatible {
  asm(\".arch armv9-a+sme\");
  asm(\"smstart\");
}
")

check_include_files("sys/auxv.h"    COMPILER_RT_HAS_AUXV)

if(ANDROID)
  set(OS_NAME "Android")
else()
  set(OS_NAME "${CMAKE_SYSTEM_NAME}")
endif()

set(AMDGPU amdgcn)
set(ARM64 aarch64)
set(ARM32 arm armhf armv4t armv5te armv6 armv6m armv7m armv7em armv7 armv7s armv7k armv8m.base armv8m.main armv8.1m.main)
set(AVR avr)
set(HEXAGON hexagon)
set(X86 i386)
set(X86_64 x86_64)
set(LOONGARCH64 loongarch64)
set(MIPS32 mips mipsel)
set(MIPS64 mips64 mips64el)
set(NVPTX nvptx64)
set(PPC32 powerpc powerpcspe)
set(PPC64 powerpc64 powerpc64le)
set(RISCV32 riscv32)
set(RISCV64 riscv64)
set(SPARC sparc)
set(SPARCV9 sparcv9)
set(WASM32 wasm32)
set(WASM64 wasm64)
set(VE ve)

if(APPLE)
  set(ARM64 arm64 arm64e)
  set(ARM32 armv7 armv7k armv7s)
  set(X86_64 x86_64 x86_64h)
endif()

set(ALL_BUILTIN_SUPPORTED_ARCH
  ${X86} ${X86_64} ${AMDGPU} ${ARM32} ${ARM64} ${AVR}
  ${HEXAGON} ${MIPS32} ${MIPS64} ${NVPTX} ${PPC32} ${PPC64}
  ${RISCV32} ${RISCV64} ${SPARC} ${SPARCV9}
  ${WASM32} ${WASM64} ${VE} ${LOONGARCH64})

include(CompilerRTUtils)
include(CompilerRTDarwinUtils)

if(APPLE)

  find_darwin_sdk_dir(DARWIN_osx_SYSROOT macosx)
  find_darwin_sdk_dir(DARWIN_iossim_SYSROOT iphonesimulator)
  find_darwin_sdk_dir(DARWIN_ios_SYSROOT iphoneos)
  find_darwin_sdk_dir(DARWIN_watchossim_SYSROOT watchsimulator)
  find_darwin_sdk_dir(DARWIN_watchos_SYSROOT watchos)
  find_darwin_sdk_dir(DARWIN_tvossim_SYSROOT appletvsimulator)
  find_darwin_sdk_dir(DARWIN_tvos_SYSROOT appletvos)
  find_darwin_sdk_dir(DARWIN_xrossim_SYSROOT xrsimulator)
  find_darwin_sdk_dir(DARWIN_xros_SYSROOT xros)

  # Get supported architecture from SDKSettings.
  function(sdk_has_arch_support sdk_path os arch has_support)
    execute_process(COMMAND
        /usr/libexec/PlistBuddy -c "Print :SupportedTargets:${os}:Archs" ${sdk_path}/SDKSettings.plist
      OUTPUT_VARIABLE SDK_SUPPORTED_ARCHS
      RESULT_VARIABLE PLIST_ERROR
      ERROR_QUIET)
    if (PLIST_ERROR EQUAL 0 AND
        SDK_SUPPORTED_ARCHS MATCHES " ${arch}\n")
      message(STATUS "Found ${arch} support in ${sdk_path}/SDKSettings.plist")
      set("${has_support}" On PARENT_SCOPE)
    else()
      message(STATUS "No ${arch} support in ${sdk_path}/SDKSettings.plist")
      set("${has_support}" Off PARENT_SCOPE)
    endif()
  endfunction()

  set(DARWIN_EMBEDDED_PLATFORMS)
  set(DARWIN_osx_BUILTIN_MIN_VER 10.7)
  set(DARWIN_osx_BUILTIN_MIN_VER_FLAG
      -mmacosx-version-min=${DARWIN_osx_BUILTIN_MIN_VER})
  set(DARWIN_osx_BUILTIN_ALL_POSSIBLE_ARCHS ${X86} ${X86_64})
  # Add support for arm64 macOS if available in SDK.
  foreach(arch ${ARM64})
    sdk_has_arch_support(${DARWIN_osx_SYSROOT} macosx ${arch} MACOS_ARM_SUPPORT)
    if (MACOS_ARM_SUPPORT)
     list(APPEND DARWIN_osx_BUILTIN_ALL_POSSIBLE_ARCHS ${arch})
    endif()
  endforeach(arch)

  if(COMPILER_RT_ENABLE_IOS)
    list(APPEND DARWIN_EMBEDDED_PLATFORMS ios)
    set(DARWIN_ios_MIN_VER_FLAG -miphoneos-version-min)
    set(DARWIN_ios_BUILTIN_MIN_VER 6.0)
    set(DARWIN_ios_BUILTIN_MIN_VER_FLAG
      ${DARWIN_ios_MIN_VER_FLAG}=${DARWIN_ios_BUILTIN_MIN_VER})
    set(DARWIN_ios_BUILTIN_ALL_POSSIBLE_ARCHS ${ARM64} ${ARM32})
    set(DARWIN_iossim_BUILTIN_ALL_POSSIBLE_ARCHS ${X86} ${X86_64})
    find_darwin_sdk_version(iossim_sdk_version "iphonesimulator")
    if ("${iossim_sdk_version}" VERSION_GREATER 14.0 OR "${iossim_sdk_version}" VERSION_EQUAL 14.0)
      list(APPEND DARWIN_iossim_BUILTIN_ALL_POSSIBLE_ARCHS arm64)
    endif()
  endif()
  if(COMPILER_RT_ENABLE_WATCHOS)
    list(APPEND DARWIN_EMBEDDED_PLATFORMS watchos)
    set(DARWIN_watchos_MIN_VER_FLAG -mwatchos-version-min)
    set(DARWIN_watchos_BUILTIN_MIN_VER 2.0)
    set(DARWIN_watchos_BUILTIN_MIN_VER_FLAG
      ${DARWIN_watchos_MIN_VER_FLAG}=${DARWIN_watchos_BUILTIN_MIN_VER})
    set(DARWIN_watchos_BUILTIN_ALL_POSSIBLE_ARCHS armv7 armv7k arm64_32)
    set(DARWIN_watchossim_BUILTIN_ALL_POSSIBLE_ARCHS ${X86})
    find_darwin_sdk_version(watchossim_sdk_version "watchsimulator")
    if ("${watchossim_sdk_version}" VERSION_GREATER 7.0 OR "${watchossim_sdk_version}" VERSION_EQUAL 7.0)
      list(APPEND DARWIN_watchossim_BUILTIN_ALL_POSSIBLE_ARCHS arm64)
    endif()
  endif()
  if(COMPILER_RT_ENABLE_TVOS)
    list(APPEND DARWIN_EMBEDDED_PLATFORMS tvos)
    set(DARWIN_tvos_MIN_VER_FLAG -mtvos-version-min)
    set(DARWIN_tvos_BUILTIN_MIN_VER 9.0)
    set(DARWIN_tvos_BUILTIN_MIN_VER_FLAG
      ${DARWIN_tvos_MIN_VER_FLAG}=${DARWIN_tvos_BUILTIN_MIN_VER})
    set(DARWIN_tvos_BUILTIN_ALL_POSSIBLE_ARCHS armv7 arm64)
    set(DARWIN_tvossim_BUILTIN_ALL_POSSIBLE_ARCHS ${X86} ${X86_64})
    find_darwin_sdk_version(tvossim_sdk_version "appletvsimulator")
    if ("${tvossim_sdk_version}" VERSION_GREATER 14.0 OR "${tvossim_sdk_version}" VERSION_EQUAL 14.0)
      list(APPEND DARWIN_tvossim_BUILTIN_ALL_POSSIBLE_ARCHS arm64)
    endif()
  endif()
  if(COMPILER_RT_ENABLE_XROS)
    list(APPEND DARWIN_EMBEDDED_PLATFORMS xros)
    set(DARWIN_xros_BUILTIN_ALL_POSSIBLE_ARCHS ${ARM64} ${ARM32})
    set(DARWIN_xrossim_BUILTIN_ALL_POSSIBLE_ARCHS arm64)
  endif()

  set(BUILTIN_SUPPORTED_OS osx)

  # We're setting the flag manually for each target OS
  set(CMAKE_OSX_DEPLOYMENT_TARGET "")

  # NOTE: We deliberately avoid using `DARWIN_<os>_ARCHS` here because that is
  # used by `config-ix.cmake` in the context of building the rest of
  # compiler-rt where the global `${TEST_COMPILE_ONLY}` (used by
  # `darwin_test_archs()`) has a different value.
  darwin_test_archs(osx
    DARWIN_osx_BUILTIN_ARCHS
    ${DARWIN_osx_BUILTIN_ALL_POSSIBLE_ARCHS}
  )
  message(STATUS "OSX supported builtin arches: ${DARWIN_osx_BUILTIN_ARCHS}")
  foreach(arch ${DARWIN_osx_BUILTIN_ARCHS})
    list(APPEND COMPILER_RT_SUPPORTED_ARCH ${arch})
    set(CAN_TARGET_${arch} 1)
  endforeach()

  foreach(platform ${DARWIN_EMBEDDED_PLATFORMS})
    if(DARWIN_${platform}sim_SYSROOT)
      set(DARWIN_${platform}sim_BUILTIN_MIN_VER
        ${DARWIN_${platform}_BUILTIN_MIN_VER})
      set(DARWIN_${platform}sim_BUILTIN_MIN_VER_FLAG
        ${DARWIN_${platform}_BUILTIN_MIN_VER_FLAG})

      set(DARWIN_${platform}sim_SKIP_CC_KEXT On)

      darwin_test_archs(${platform}sim
        DARWIN_${platform}sim_BUILTIN_ARCHS
        ${DARWIN_${platform}sim_BUILTIN_ALL_POSSIBLE_ARCHS}
      )
      message(STATUS "${platform} Simulator supported builtin arches: ${DARWIN_${platform}sim_BUILTIN_ARCHS}")
      if(DARWIN_${platform}sim_BUILTIN_ARCHS)
        list(APPEND BUILTIN_SUPPORTED_OS ${platform}sim)
      endif()
      foreach(arch ${DARWIN_${platform}sim_BUILTIN_ARCHS})
        list(APPEND COMPILER_RT_SUPPORTED_ARCH ${arch})
        set(CAN_TARGET_${arch} 1)
      endforeach()
    endif()

    if(DARWIN_${platform}_SYSROOT)
      darwin_test_archs(${platform}
        DARWIN_${platform}_BUILTIN_ARCHS
        ${DARWIN_${platform}_BUILTIN_ALL_POSSIBLE_ARCHS}
      )
      message(STATUS "${platform} supported builtin arches: ${DARWIN_${platform}_BUILTIN_ARCHS}")
      if(DARWIN_${platform}_BUILTIN_ARCHS)
        list(APPEND BUILTIN_SUPPORTED_OS ${platform})
      endif()
      foreach(arch ${DARWIN_${platform}_BUILTIN_ARCHS})
        list(APPEND COMPILER_RT_SUPPORTED_ARCH ${arch})
        set(CAN_TARGET_${arch} 1)
      endforeach()
    endif()
  endforeach()

  list_intersect(BUILTIN_SUPPORTED_ARCH ALL_BUILTIN_SUPPORTED_ARCH COMPILER_RT_SUPPORTED_ARCH)

else()
  # If we're not building the builtins standalone, just rely on the  tests in
  # config-ix.cmake to tell us what to build. Otherwise we need to do some leg
  # work here...
  if(COMPILER_RT_BUILTINS_STANDALONE_BUILD)
    test_targets()
  endif()
  # Architectures supported by compiler-rt libraries.
  filter_available_targets(BUILTIN_SUPPORTED_ARCH
    ${ALL_BUILTIN_SUPPORTED_ARCH})
endif()

if(OS_NAME MATCHES "Linux|SerenityOS" AND NOT LLVM_USE_SANITIZER AND NOT
   COMPILER_RT_GPU_BUILD)
  set(COMPILER_RT_HAS_CRT TRUE)
else()
  set(COMPILER_RT_HAS_CRT FALSE)
endif()

message(STATUS "Builtin supported architectures: ${BUILTIN_SUPPORTED_ARCH}")
