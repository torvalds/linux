include(BuiltinTests)
include(CheckCSourceCompiles)

# Make all the tests only check the compiler
set(TEST_COMPILE_ONLY On)

builtin_check_c_compiler_flag(-fPIC                 COMPILER_RT_HAS_FPIC_FLAG)
builtin_check_c_compiler_flag(-std=c11              COMPILER_RT_HAS_STD_C11_FLAG)
builtin_check_c_compiler_flag(-Wno-pedantic         COMPILER_RT_HAS_WNO_PEDANTIC)
builtin_check_c_compiler_flag(-fno-lto              COMPILER_RT_HAS_FNO_LTO_FLAG)
builtin_check_c_compiler_flag(-fno-profile-generate COMPILER_RT_HAS_FNO_PROFILE_GENERATE_FLAG)
builtin_check_c_compiler_flag(-fno-profile-instr-generate COMPILER_RT_HAS_FNO_PROFILE_INSTR_GENERATE_FLAG)
builtin_check_c_compiler_flag(-fno-profile-instr-use COMPILER_RT_HAS_FNO_PROFILE_INSTR_USE_FLAG)

if(ANDROID)
  set(OS_NAME "Android")
else()
  set(OS_NAME "${CMAKE_SYSTEM_NAME}")
endif()

set(ARM64 aarch64)
set(ARM32 arm armhf)
set(HEXAGON hexagon)
set(X86 i386)
set(X86_64 x86_64)
set(LOONGARCH64 loongarch64)
set(MIPS32 mips mipsel)
set(MIPS64 mips64 mips64el)
set(PPC32 powerpc powerpcspe)
set(PPC64 powerpc64 powerpc64le)
set(RISCV32 riscv32)
set(RISCV64 riscv64)
set(VE ve)

set(ALL_CRT_SUPPORTED_ARCH ${X86} ${X86_64} ${ARM32} ${ARM64} ${PPC32}
    ${PPC64} ${RISCV32} ${RISCV64} ${VE} ${HEXAGON} ${LOONGARCH64}
    ${MIPS32} ${MIPS64} ${SPARC} ${SPARCV9})

include(CompilerRTUtils)

if(NOT APPLE)
  if(COMPILER_RT_CRT_STANDALONE_BUILD)
    test_targets()
  endif()
  # Architectures supported by compiler-rt crt library.
  filter_available_targets(CRT_SUPPORTED_ARCH ${ALL_CRT_SUPPORTED_ARCH})
  message(STATUS "Supported architectures for crt: ${CRT_SUPPORTED_ARCH}")
endif()

if (CRT_SUPPORTED_ARCH AND OS_NAME MATCHES "Linux|SerenityOS" AND NOT LLVM_USE_SANITIZER)
  set(COMPILER_RT_HAS_CRT TRUE)
else()
  set(COMPILER_RT_HAS_CRT FALSE)
endif()
