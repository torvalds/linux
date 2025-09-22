# This directory contains a large amount of C code which provides
# generic implementations of the core runtime library along with optimized
# architecture-specific code in various subdirectories.

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.20.0)

  set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
  project(CompilerRTBuiltins C ASM)
  set(COMPILER_RT_STANDALONE_BUILD TRUE)
  set(COMPILER_RT_BUILTINS_STANDALONE_BUILD TRUE)

  set(COMPILER_RT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")

  set(LLVM_COMMON_CMAKE_UTILS "${COMPILER_RT_SOURCE_DIR}/../cmake")

  # Add path for custom modules
  list(INSERT CMAKE_MODULE_PATH 0
    "${COMPILER_RT_SOURCE_DIR}/cmake"
    "${COMPILER_RT_SOURCE_DIR}/cmake/Modules"
    "${LLVM_COMMON_CMAKE_UTILS}"
    "${LLVM_COMMON_CMAKE_UTILS}/Modules"
    )

  include(base-config-ix)
  include(CompilerRTUtils)

  if (NOT LLVM_RUNTIMES_BUILD)
    load_llvm_config()
  endif()
  construct_compiler_rt_default_triple()

  include(SetPlatformToolchainTools)
  if(APPLE)
    include(CompilerRTDarwinUtils)
  endif()
  if(APPLE)
    include(UseLibtool)
  endif()
  include(AddCompilerRT)

  if(MINGW)
    # Simplified version of what's set in cmake/config-ix.cmake; not including
    # builtins, which are linked separately.
    set(MINGW_LIBRARIES mingw32 moldname mingwex msvcrt advapi32 shell32
                        user32 kernel32 mingw32 moldname mingwex msvcrt)
   endif()
endif()

if (COMPILER_RT_STANDALONE_BUILD)
  # When compiler-rt is being built standalone, possibly as a cross-compilation
  # target, the target may or may not want position independent code. This
  # option provides an avenue through which the flag may be controlled when an
  # LLVM configuration is not being utilized.
  option(COMPILER_RT_BUILTINS_ENABLE_PIC
    "Turns on or off -fPIC for the builtin library source"
    ON)
endif()

include(builtin-config-ix)
include(CMakeDependentOption)
include(CMakePushCheckState)

option(COMPILER_RT_BUILTINS_HIDE_SYMBOLS
  "Do not export any symbols from the static library." ON)

# TODO: Need to add a mechanism for logging errors when builtin source files are
# added to a sub-directory and not this CMakeLists file.
set(GENERIC_SOURCES
  absvdi2.c
  absvsi2.c
  absvti2.c
  adddf3.c
  addsf3.c
  addvdi3.c
  addvsi3.c
  addvti3.c
  apple_versioning.c
  ashldi3.c
  ashlti3.c
  ashrdi3.c
  ashrti3.c
  bswapdi2.c
  bswapsi2.c
  clzdi2.c
  clzsi2.c
  clzti2.c
  cmpdi2.c
  cmpti2.c
  comparedf2.c
  comparesf2.c
  ctzdi2.c
  ctzsi2.c
  ctzti2.c
  divdc3.c
  divdf3.c
  divdi3.c
  divmoddi4.c
  divmodsi4.c
  divmodti4.c
  divsc3.c
  divsf3.c
  divsi3.c
  divti3.c
  extendsfdf2.c
  extendhfsf2.c
  ffsdi2.c
  ffssi2.c
  ffsti2.c
  fixdfdi.c
  fixdfsi.c
  fixdfti.c
  fixsfdi.c
  fixsfsi.c
  fixsfti.c
  fixunsdfdi.c
  fixunsdfsi.c
  fixunsdfti.c
  fixunssfdi.c
  fixunssfsi.c
  fixunssfti.c
  floatdidf.c
  floatdisf.c
  floatsidf.c
  floatsisf.c
  floattidf.c
  floattisf.c
  floatundidf.c
  floatundisf.c
  floatunsidf.c
  floatunsisf.c
  floatuntidf.c
  floatuntisf.c
  fp_mode.c
  int_util.c
  lshrdi3.c
  lshrti3.c
  moddi3.c
  modsi3.c
  modti3.c
  muldc3.c
  muldf3.c
  muldi3.c
  mulodi4.c
  mulosi4.c
  muloti4.c
  mulsc3.c
  mulsf3.c
  multi3.c
  mulvdi3.c
  mulvsi3.c
  mulvti3.c
  negdf2.c
  negdi2.c
  negsf2.c
  negti2.c
  negvdi2.c
  negvsi2.c
  negvti2.c
  os_version_check.c
  paritydi2.c
  paritysi2.c
  parityti2.c
  popcountdi2.c
  popcountsi2.c
  popcountti2.c
  powidf2.c
  powisf2.c
  subdf3.c
  subsf3.c
  subvdi3.c
  subvsi3.c
  subvti3.c
  trampoline_setup.c
  truncdfhf2.c
  truncdfsf2.c
  truncsfhf2.c
  ucmpdi2.c
  ucmpti2.c
  udivdi3.c
  udivmoddi4.c
  udivmodsi4.c
  udivmodti4.c
  udivsi3.c
  udivti3.c
  umoddi3.c
  umodsi3.c
  umodti3.c
)

# We only build BF16 files when "__bf16" is available.
set(BF16_SOURCES
    extendbfsf2.c
    truncdfbf2.c
    truncsfbf2.c
)

set(GENERIC_TF_SOURCES
  addtf3.c
  comparetf2.c
  divtc3.c
  divtf3.c
  extenddftf2.c
  extendhftf2.c
  extendsftf2.c
  fixtfdi.c
  fixtfsi.c
  fixtfti.c
  fixunstfdi.c
  fixunstfsi.c
  fixunstfti.c
  floatditf.c
  floatsitf.c
  floattitf.c
  floatunditf.c
  floatunsitf.c
  floatuntitf.c
  multc3.c
  multf3.c
  powitf2.c
  subtf3.c
  trunctfdf2.c
  trunctfhf2.c
  trunctfsf2.c
)

option(COMPILER_RT_EXCLUDE_ATOMIC_BUILTIN
  "Skip the atomic builtin (these should normally be provided by a shared library)"
  On)

if(NOT FUCHSIA AND NOT COMPILER_RT_BAREMETAL_BUILD AND NOT COMPILER_RT_GPU_BUILD)
  set(GENERIC_SOURCES
    ${GENERIC_SOURCES}
    emutls.c
    enable_execute_stack.c
    eprintf.c
  )
endif()

option(COMPILER_RT_LIBATOMIC_USE_PTHREAD
  "Whether libatomic should use pthreads if available."
  Off)

if(COMPILER_RT_LIBATOMIC_USE_PTHREAD)
  add_compile_definitions(_LIBATOMIC_USE_PTHREAD)
endif()

if(COMPILER_RT_HAS_ATOMIC_KEYWORD AND NOT COMPILER_RT_EXCLUDE_ATOMIC_BUILTIN)
  set(GENERIC_SOURCES
    ${GENERIC_SOURCES}
    atomic.c
  )
endif()

if(APPLE)
  set(GENERIC_SOURCES
    ${GENERIC_SOURCES}
    atomic_flag_clear.c
    atomic_flag_clear_explicit.c
    atomic_flag_test_and_set.c
    atomic_flag_test_and_set_explicit.c
    atomic_signal_fence.c
    atomic_thread_fence.c
  )
endif()

if (HAVE_UNWIND_H)
  set(GENERIC_SOURCES
    ${GENERIC_SOURCES}
    gcc_personality_v0.c
  )
endif ()

if (NOT FUCHSIA)
  set(GENERIC_SOURCES
    ${GENERIC_SOURCES}
    clear_cache.c
  )
endif()

# These files are used on 32-bit and 64-bit x86.
set(x86_ARCH_SOURCES
  cpu_model/x86.c
  )

if (NOT MSVC)
  set(x86_ARCH_SOURCES
    ${x86_ARCH_SOURCES}
    i386/fp_mode.c
  )
endif ()

# Implement extended-precision builtins, assuming long double is 80 bits.
# long double is not 80 bits on Android or MSVC.
set(x86_80_BIT_SOURCES
  divxc3.c
  extendxftf2.c
  fixxfdi.c
  fixxfti.c
  fixunsxfdi.c
  fixunsxfsi.c
  fixunsxfti.c
  floatdixf.c
  floattixf.c
  floatundixf.c
  floatuntixf.c
  mulxc3.c
  powixf2.c
  trunctfxf2.c
)

if (NOT MSVC)
  set(x86_64_SOURCES
    ${GENERIC_SOURCES}
    ${GENERIC_TF_SOURCES}
    ${x86_ARCH_SOURCES}
    x86_64/floatdidf.c
    x86_64/floatdisf.c
  )
  if (NOT WIN32)
    set(x86_64_SOURCES
      ${x86_64_SOURCES}
      x86_64/floatundidf.S
      x86_64/floatundisf.S
    )
  endif()

  if (NOT ANDROID)
    set(x86_64_SOURCES
      ${x86_64_SOURCES}
      ${x86_80_BIT_SOURCES}
      x86_64/floatdixf.c
    )
    if (NOT WIN32)
      set(x86_64_SOURCES
        ${x86_64_SOURCES}
        x86_64/floatundixf.S
      )
    endif()
  endif()

  # Darwin x86_64 Haswell
  set(x86_64h_SOURCES ${x86_64_SOURCES})

  if (WIN32)
    set(x86_64_SOURCES
      ${x86_64_SOURCES}
      x86_64/chkstk.S
    )
  endif()

  set(i386_SOURCES
    ${GENERIC_SOURCES}
    ${x86_ARCH_SOURCES}
    i386/ashldi3.S
    i386/ashrdi3.S
    i386/divdi3.S
    i386/floatdidf.S
    i386/floatdisf.S
    i386/floatundidf.S
    i386/floatundisf.S
    i386/lshrdi3.S
    i386/moddi3.S
    i386/muldi3.S
    i386/udivdi3.S
    i386/umoddi3.S
  )

  if (NOT ANDROID)
    set(i386_SOURCES
      ${i386_SOURCES}
      ${x86_80_BIT_SOURCES}
      i386/floatdixf.S
      i386/floatundixf.S
    )
  endif()

  if (WIN32)
    set(i386_SOURCES
      ${i386_SOURCES}
      i386/chkstk.S
    )
  endif()
else () # MSVC
  # Use C versions of functions when building on MSVC
  # MSVC's assembler takes Intel syntax, not AT&T syntax.
  # Also use only MSVC compilable builtin implementations.
  set(x86_64_SOURCES
    ${GENERIC_SOURCES}
    ${x86_ARCH_SOURCES}
    x86_64/floatdidf.c
    x86_64/floatdisf.c
  )
  set(i386_SOURCES ${GENERIC_SOURCES} ${x86_ARCH_SOURCES})
endif () # if (NOT MSVC)


# builtin support for Targets that have Arm state or have Thumb2
set(arm_or_thumb2_base_SOURCES
  arm/fp_mode.c
  arm/bswapdi2.S
  arm/bswapsi2.S
  arm/clzdi2.S
  arm/clzsi2.S
  arm/comparesf2.S
  arm/divmodsi4.S
  arm/divsi3.S
  arm/modsi3.S
  arm/udivmodsi4.S
  arm/udivsi3.S
  arm/umodsi3.S
  ${GENERIC_SOURCES}
)

set(arm_sync_SOURCES
  arm/sync_fetch_and_add_4.S
  arm/sync_fetch_and_add_8.S
  arm/sync_fetch_and_and_4.S
  arm/sync_fetch_and_and_8.S
  arm/sync_fetch_and_max_4.S
  arm/sync_fetch_and_max_8.S
  arm/sync_fetch_and_min_4.S
  arm/sync_fetch_and_min_8.S
  arm/sync_fetch_and_nand_4.S
  arm/sync_fetch_and_nand_8.S
  arm/sync_fetch_and_or_4.S
  arm/sync_fetch_and_or_8.S
  arm/sync_fetch_and_sub_4.S
  arm/sync_fetch_and_sub_8.S
  arm/sync_fetch_and_umax_4.S
  arm/sync_fetch_and_umax_8.S
  arm/sync_fetch_and_umin_4.S
  arm/sync_fetch_and_umin_8.S
  arm/sync_fetch_and_xor_4.S
  arm/sync_fetch_and_xor_8.S
)

# builtin support for Thumb-only targets with very limited Thumb2 technology,
# such as v6-m and v8-m.baseline
set(thumb1_base_SOURCES
  arm/divsi3.S
  arm/udivsi3.S
  arm/comparesf2.S
  arm/addsf3.S
  ${GENERIC_SOURCES}
)

set(arm_EABI_SOURCES
  arm/aeabi_cdcmp.S
  arm/aeabi_cdcmpeq_check_nan.c
  arm/aeabi_cfcmp.S
  arm/aeabi_cfcmpeq_check_nan.c
  arm/aeabi_dcmp.S
  arm/aeabi_div0.c
  arm/aeabi_drsub.c
  arm/aeabi_fcmp.S
  arm/aeabi_frsub.c
  arm/aeabi_idivmod.S
  arm/aeabi_ldivmod.S
  arm/aeabi_memcmp.S
  arm/aeabi_memcpy.S
  arm/aeabi_memmove.S
  arm/aeabi_memset.S
  arm/aeabi_uidivmod.S
  arm/aeabi_uldivmod.S
)

set(arm_Thumb1_JT_SOURCES
  arm/switch16.S
  arm/switch32.S
  arm/switch8.S
  arm/switchu8.S
)
set(arm_Thumb1_SjLj_EH_SOURCES
  arm/restore_vfp_d8_d15_regs.S
  arm/save_vfp_d8_d15_regs.S
)
set(arm_Thumb1_VFPv2_DP_SOURCES
  arm/adddf3vfp.S
  arm/divdf3vfp.S
  arm/eqdf2vfp.S
  arm/extendsfdf2vfp.S
  arm/fixdfsivfp.S
  arm/fixunsdfsivfp.S
  arm/floatsidfvfp.S
  arm/floatunssidfvfp.S
  arm/gedf2vfp.S
  arm/gtdf2vfp.S
  arm/ledf2vfp.S
  arm/ltdf2vfp.S
  arm/muldf3vfp.S
  arm/nedf2vfp.S
  arm/negdf2vfp.S
  arm/subdf3vfp.S
  arm/truncdfsf2vfp.S
  arm/unorddf2vfp.S
)
set(arm_Thumb1_VFPv2_SP_SOURCES
  arm/addsf3vfp.S
  arm/divsf3vfp.S
  arm/eqsf2vfp.S
  arm/fixsfsivfp.S
  arm/fixunssfsivfp.S
  arm/floatsisfvfp.S
  arm/floatunssisfvfp.S
  arm/gesf2vfp.S
  arm/gtsf2vfp.S
  arm/lesf2vfp.S
  arm/ltsf2vfp.S
  arm/mulsf3vfp.S
  arm/negsf2vfp.S
  arm/nesf2vfp.S
  arm/subsf3vfp.S
  arm/unordsf2vfp.S
)
set(arm_Thumb1_icache_SOURCES
  arm/sync_synchronize.S
)

# thumb1 calling into Arm to cover support
set(arm_Thumb1_SOURCES
  ${arm_Thumb1_JT_SOURCES}
  ${arm_Thumb1_SjLj_EH_SOURCES}
  ${arm_Thumb1_VFPv2_DP_SOURCES}
  ${arm_Thumb1_VFPv2_SP_SOURCES}
  ${arm_Thumb1_icache_SOURCES}
)

# base functionality for Arm Targets prior to Arm v7-a and Armv6-m such as v6,
# v5t, v4t
set(arm_min_SOURCES
  ${arm_or_thumb2_base_SOURCES}
  ${arm_EABI_SOURCES}
)

if(MINGW)
  set(arm_SOURCES
    arm/aeabi_idivmod.S
    arm/aeabi_ldivmod.S
    arm/aeabi_uidivmod.S
    arm/aeabi_uldivmod.S
    arm/chkstk.S
    ${arm_or_thumb2_base_SOURCES}
    ${arm_sync_SOURCES}
  )

  set(thumb1_SOURCES
    ${thumb1_base_SOURCES}
  )
elseif(NOT WIN32)
  # TODO the EABI sources should only be added to EABI targets
  set(arm_SOURCES
    ${arm_or_thumb2_base_SOURCES}
    ${arm_sync_SOURCES}
    ${arm_EABI_SOURCES}
    ${arm_Thumb1_SOURCES}
  )

  set(thumb1_SOURCES
    ${thumb1_base_SOURCES}
    ${arm_EABI_SOURCES}
  )
endif()

set(aarch64_SOURCES
  ${GENERIC_TF_SOURCES}
  ${GENERIC_SOURCES}
  cpu_model/aarch64.c
  aarch64/fp_mode.c
)

if (COMPILER_RT_HAS_AARCH64_SME)
  if (NOT COMPILER_RT_DISABLE_AARCH64_FMV AND COMPILER_RT_HAS_FNO_BUILTIN_FLAG AND (COMPILER_RT_HAS_AUXV OR COMPILER_RT_BAREMETAL_BUILD))
    list(APPEND aarch64_SOURCES aarch64/sme-abi.S aarch64/sme-libc-mem-routines.S aarch64/sme-abi-init.c aarch64/sme-abi-vg.c aarch64/sme-libc-routines.c)
    message(STATUS "AArch64 SME ABI routines enabled")
    set_source_files_properties(aarch64/sme-libc-routines.c PROPERTIES COMPILE_FLAGS "-fno-builtin")
  else()
    if(COMPILER_RT_DISABLE_AARCH64_FMV)
      message(WARNING "AArch64 SME ABI routines require function multiversioning support.")
    endif()
    if(NOT COMPILER_RT_HAS_FNO_BUILTIN_FLAG)
      message(WARNING "AArch64 SME ABI routines require '-fno-builtin'")
    endif()
    if(NOT (COMPILER_RT_HAS_AUXV OR COMPILER_RT_BAREMETAL_BUILD))
      message(WARNING "AArch64 SME ABI routines requires sys/auxv.h or COMPILER_RT_BAREMETAL_BUILD flag")
    endif()
    message(STATUS "AArch64 SME ABI routines disabled")
  endif()
endif()

# Generate outline atomics helpers from lse.S base
set(OA_HELPERS_DIR "${CMAKE_CURRENT_BINARY_DIR}/outline_atomic_helpers.dir")
file(MAKE_DIRECTORY "${OA_HELPERS_DIR}")

if(CMAKE_HOST_UNIX)
  set(COMPILER_RT_LINK_OR_COPY create_symlink)
else()
  set(COMPILER_RT_LINK_OR_COPY copy)
endif()

foreach(pat cas swp ldadd ldclr ldeor ldset)
  foreach(size 1 2 4 8 16)
    foreach(model 1 2 3 4 5)
      if(pat STREQUAL "cas" OR NOT size STREQUAL "16")
        set(source_asm "${CMAKE_CURRENT_SOURCE_DIR}/aarch64/lse.S")
        set(helper_asm "${OA_HELPERS_DIR}/outline_atomic_${pat}${size}_${model}.S")
        add_custom_command(
          OUTPUT "${helper_asm}"
          COMMAND ${CMAKE_COMMAND} -E ${COMPILER_RT_LINK_OR_COPY} "${source_asm}" "${helper_asm}"
          DEPENDS "${source_asm}"
        )
        set_source_files_properties("${helper_asm}"
          PROPERTIES
          COMPILE_DEFINITIONS "L_${pat};SIZE=${size};MODEL=${model}"
          INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}"
        )
        list(APPEND aarch64_SOURCES "${helper_asm}")
      endif()
    endforeach(model)
  endforeach(size)
endforeach(pat)

if (MINGW)
  set(aarch64_SOURCES
    ${aarch64_SOURCES}
    aarch64/chkstk.S
  )
endif()

set(amdgcn_SOURCES ${GENERIC_SOURCES})

set(armv4t_SOURCES ${arm_min_SOURCES})
set(armv5te_SOURCES ${arm_min_SOURCES})
set(armv6_SOURCES ${arm_min_SOURCES})
set(armhf_SOURCES ${arm_SOURCES})
set(armv7_SOURCES ${arm_SOURCES})
set(armv7s_SOURCES ${arm_SOURCES})
set(armv7k_SOURCES ${arm_SOURCES})
set(arm64_SOURCES ${aarch64_SOURCES})
set(arm64e_SOURCES ${aarch64_SOURCES})
set(arm64_32_SOURCES ${aarch64_SOURCES})

# macho_embedded archs
set(armv6m_SOURCES ${thumb1_SOURCES})
set(armv7m_SOURCES ${arm_SOURCES})
set(armv7em_SOURCES ${arm_SOURCES})
set(armv8m.base_SOURCES ${thumb1_SOURCES})
set(armv8m.main_SOURCES ${arm_SOURCES})
set(armv8.1m.main_SOURCES ${arm_SOURCES})

# 8-bit AVR MCU
set(avr_SOURCES
  avr/mulqi3.S
  avr/mulhi3.S
  avr/exit.S
  avr/divmodhi4.S
  avr/udivmodhi4.S
  avr/divmodqi4.S
  avr/udivmodqi4.S
  ${GENERIC_SOURCES}
)

# hexagon arch
set(hexagon_SOURCES
  hexagon/common_entry_exit_abi1.S
  hexagon/common_entry_exit_abi2.S
  hexagon/common_entry_exit_legacy.S
  hexagon/dfaddsub.S
  hexagon/dfdiv.S
  hexagon/dffma.S
  hexagon/dfminmax.S
  hexagon/dfmul.S
  hexagon/dfsqrt.S
  hexagon/divdi3.S
  hexagon/divsi3.S
  hexagon/fastmath2_dlib_asm.S
  hexagon/fastmath2_ldlib_asm.S
  hexagon/fastmath_dlib_asm.S
  hexagon/memcpy_forward_vp4cp4n2.S
  hexagon/memcpy_likely_aligned.S
  hexagon/moddi3.S
  hexagon/modsi3.S
  hexagon/sfdiv_opt.S
  hexagon/sfsqrt_opt.S
  hexagon/udivdi3.S
  hexagon/udivmoddi4.S
  hexagon/udivmodsi4.S
  hexagon/udivsi3.S
  hexagon/umoddi3.S
  hexagon/umodsi3.S
  ${GENERIC_SOURCES}
  ${GENERIC_TF_SOURCES}
)

set(loongarch_SOURCES
  loongarch/fp_mode.c
  ${GENERIC_SOURCES}
  ${GENERIC_TF_SOURCES}
)
set(loongarch64_SOURCES
  ${loongarch_SOURCES}
)

set(mips_SOURCES ${GENERIC_SOURCES})
set(mipsel_SOURCES ${mips_SOURCES})
set(mips64_SOURCES ${GENERIC_TF_SOURCES}
                   ${mips_SOURCES})
set(mips64el_SOURCES ${GENERIC_TF_SOURCES}
                     ${mips_SOURCES})

set(nvptx64_SOURCES ${GENERIC_SOURCES})

set(powerpc_SOURCES ${GENERIC_SOURCES})

set(powerpcspe_SOURCES ${GENERIC_SOURCES})

set(powerpc64_SOURCES
  ppc/divtc3.c
  ppc/fixtfdi.c
  ppc/fixunstfdi.c
  ppc/floatditf.c
  ppc/floatunditf.c
  ppc/gcc_qadd.c
  ppc/gcc_qdiv.c
  ppc/gcc_qmul.c
  ppc/gcc_qsub.c
  ppc/multc3.c
  ${GENERIC_SOURCES}
)
# These routines require __int128, which isn't supported on AIX.
if (NOT OS_NAME MATCHES "AIX")
  set(powerpc64_SOURCES
    ppc/floattitf.c
    ppc/fixtfti.c
    ppc/fixunstfti.c
    ${powerpc64_SOURCES}
  )
endif()
set(powerpc64le_SOURCES ${powerpc64_SOURCES})

set(riscv_SOURCES
  riscv/fp_mode.c
  riscv/save.S
  riscv/restore.S
  ${GENERIC_SOURCES}
  ${GENERIC_TF_SOURCES}
)
set(riscv32_SOURCES
  riscv/mulsi3.S
  ${riscv_SOURCES}
)
set(riscv64_SOURCES
  riscv/muldi3.S
  ${riscv_SOURCES}
)

set(sparc_SOURCES ${GENERIC_SOURCES} ${GENERIC_TF_SOURCES})
set(sparcv9_SOURCES ${GENERIC_SOURCES} ${GENERIC_TF_SOURCES})

set(wasm32_SOURCES
  ${GENERIC_TF_SOURCES}
  ${GENERIC_SOURCES}
)
set(wasm64_SOURCES
  ${GENERIC_TF_SOURCES}
  ${GENERIC_SOURCES}
)

set(ve_SOURCES
  ve/grow_stack.S
  ve/grow_stack_align.S
  ${GENERIC_TF_SOURCES}
  ${GENERIC_SOURCES})

add_custom_target(builtins)
set_target_properties(builtins PROPERTIES FOLDER "Compiler-RT/Metatargets")

option(COMPILER_RT_ENABLE_SOFTWARE_INT128
  "Enable the int128 builtin routines for all targets."
  OFF)

if (APPLE)
  add_subdirectory(Darwin-excludes)
  add_subdirectory(macho_embedded)
  darwin_add_builtin_libraries(${BUILTIN_SUPPORTED_OS})
else ()
  set(BUILTIN_CFLAGS "")
  add_security_warnings(BUILTIN_CFLAGS 0)

  if (COMPILER_RT_HAS_FCF_PROTECTION_FLAG)
    append_list_if(COMPILER_RT_ENABLE_CET -fcf-protection=full BUILTIN_CFLAGS)
  endif()

  append_list_if(COMPILER_RT_HAS_STD_C11_FLAG -std=c11 BUILTIN_CFLAGS)
  append_list_if(COMPILER_RT_HAS_WBUILTIN_DECLARATION_MISMATCH_FLAG -Werror=builtin-declaration-mismatch BUILTIN_CFLAGS)

  # Don't embed directives for picking any specific CRT
  if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "")
    append_list_if(COMPILER_RT_HAS_ZL_FLAG /Zl BUILTIN_CFLAGS)
  endif()

  # These flags would normally be added to CMAKE_C_FLAGS by the llvm
  # cmake step. Add them manually if this is a standalone build.
  if(COMPILER_RT_STANDALONE_BUILD)
    if(COMPILER_RT_BUILTINS_ENABLE_PIC)
      append_list_if(COMPILER_RT_HAS_FPIC_FLAG -fPIC BUILTIN_CFLAGS)
    endif()
    append_list_if(COMPILER_RT_HAS_FNO_BUILTIN_FLAG -fno-builtin BUILTIN_CFLAGS)
    if(COMPILER_RT_BUILTINS_HIDE_SYMBOLS)
      append_list_if(COMPILER_RT_HAS_VISIBILITY_HIDDEN_FLAG -fvisibility=hidden BUILTIN_CFLAGS)
    endif()
    if(NOT COMPILER_RT_DEBUG)
      append_list_if(COMPILER_RT_HAS_OMIT_FRAME_POINTER_FLAG -fomit-frame-pointer BUILTIN_CFLAGS)
    endif()
  endif()

  # Directly targeting the GPU requires a few extra flags.
  if("${COMPILER_RT_DEFAULT_TARGET_ARCH}" MATCHES "amdgcn|nvptx")
    append_list_if(COMPILER_RT_HAS_FFREESTANDING_FLAG -ffreestanding BUILTIN_CFLAGS)
    append_list_if(COMPILER_RT_HAS_NOGPULIB_FLAG -nogpulib BUILTIN_CFLAGS)
    append_list_if(COMPILER_RT_HAS_FLTO_FLAG -flto BUILTIN_CFLAGS)
    append_list_if(COMPILER_RT_HAS_FCONVERGENT_FUNCTIONS_FLAG
                   -fconvergent-functions BUILTIN_CFLAGS)

    # AMDGPU targets want to use a generic ABI.
    if("${COMPILER_RT_DEFAULT_TARGET_ARCH}" MATCHES "amdgcn")
      append_list_if(COMPILER_RT_HAS_CODE_OBJECT_VERSION_FLAG
                     "SHELL:-Xclang -mcode-object-version=none" BUILTIN_CFLAGS)
    endif()
  endif()

  set(BUILTIN_DEFS "")

  if(COMPILER_RT_BUILTINS_HIDE_SYMBOLS)
    append_list_if(COMPILER_RT_HAS_VISIBILITY_HIDDEN_FLAG VISIBILITY_HIDDEN BUILTIN_DEFS)
  endif()

  if(COMPILER_RT_DISABLE_AARCH64_FMV)
    list(APPEND BUILTIN_DEFS DISABLE_AARCH64_FMV)
  endif()

  append_list_if(COMPILER_RT_HAS_ASM_LSE HAS_ASM_LSE BUILTIN_DEFS)

  foreach (arch ${BUILTIN_SUPPORTED_ARCH})
    if (CAN_TARGET_${arch})
      cmake_push_check_state()
      # TODO: we should probably make most of the checks in builtin-config depend on the target flags.
      message(STATUS "Performing additional configure checks with target flags: ${TARGET_${arch}_CFLAGS}")
      set(BUILTIN_CFLAGS_${arch} ${BUILTIN_CFLAGS})
      list(APPEND CMAKE_REQUIRED_FLAGS ${TARGET_${arch}_CFLAGS} ${BUILTIN_CFLAGS_${arch}})
      # For ARM archs, exclude any VFP builtins if VFP is not supported
      if (${arch} MATCHES "^(arm|armhf|armv7|armv7s|armv7k|armv7m|armv7em|armv8m.main|armv8.1m.main)$")
        string(REPLACE ";" " " _TARGET_${arch}_CFLAGS "${TARGET_${arch}_CFLAGS}")
        check_compile_definition(__ARM_FP "${CMAKE_C_FLAGS} ${_TARGET_${arch}_CFLAGS}" COMPILER_RT_HAS_${arch}_VFP)
        if(NOT COMPILER_RT_HAS_${arch}_VFP)
          list(REMOVE_ITEM ${arch}_SOURCES ${arm_Thumb1_VFPv2_DP_SOURCES} ${arm_Thumb1_VFPv2_SP_SOURCES} ${arm_Thumb1_SjLj_EH_SOURCES})
        else()
          # Exclude any double-precision builtins if VFP is single-precision-only
          try_compile_only(COMPILER_RT_HAS_${arch}_VFP_DP
                           SOURCE "#if !(__ARM_FP & 0x8)
                                   #error No double-precision support!
                                   #endif
                                   int main(void) { return 0; }")
          if(NOT COMPILER_RT_HAS_${arch}_VFP_DP)
            list(REMOVE_ITEM ${arch}_SOURCES ${arm_Thumb1_VFPv2_DP_SOURCES})
          endif()
        endif()
      endif()
      check_c_source_compiles("_Float16 foo(_Float16 x) { return x; }
                               int main(void) { return 0; }"
                              COMPILER_RT_HAS_${arch}_FLOAT16)
      append_list_if(COMPILER_RT_HAS_${arch}_FLOAT16 -DCOMPILER_RT_HAS_FLOAT16 BUILTIN_CFLAGS_${arch})
      check_c_source_compiles("__bf16 foo(__bf16 x) { return x; }
                               int main(void) { return 0; }"
                              COMPILER_RT_HAS_${arch}_BFLOAT16)
      # Build BF16 files only when "__bf16" is available.
      if(COMPILER_RT_HAS_${arch}_BFLOAT16)
        list(APPEND ${arch}_SOURCES ${BF16_SOURCES})
      endif()

      # Remove a generic C builtin when an arch-specific builtin is specified.
      filter_builtin_sources(${arch}_SOURCES ${arch})

      # Needed for clear_cache on debug mode, due to r7's usage in inline asm.
      # Release mode already sets it via -O2/3, Debug mode doesn't.
      if (${arch} STREQUAL "armhf")
        list(APPEND BUILTIN_CFLAGS_${arch} -fomit-frame-pointer -DCOMPILER_RT_ARMHF_TARGET)
      endif()

      # For RISCV32, we must force enable int128 for compiling long
      # double routines.
      if(COMPILER_RT_ENABLE_SOFTWARE_INT128 OR "${arch}" STREQUAL "riscv32")
        list(APPEND BUILTIN_CFLAGS_${arch} -fforce-enable-int128)
      endif()

      add_compiler_rt_runtime(clang_rt.builtins
                              STATIC
                              ARCHS ${arch}
                              DEPS ${deps_${arch}}
                              SOURCES ${${arch}_SOURCES}
                              DEFS ${BUILTIN_DEFS}
                              CFLAGS ${BUILTIN_CFLAGS_${arch}}
                              PARENT_TARGET builtins)
      cmake_pop_check_state()
    endif ()
  endforeach ()
endif ()

add_dependencies(compiler-rt builtins)

option(COMPILER_RT_BUILD_STANDALONE_LIBATOMIC
  "Build standalone shared atomic library."
  OFF)

if(COMPILER_RT_BUILD_STANDALONE_LIBATOMIC)
  add_custom_target(builtins-standalone-atomic)
  set(BUILTIN_TYPE SHARED)
  if(OS_NAME MATCHES "AIX")
    include(CompilerRTAIXUtils)
    if(NOT COMPILER_RT_LIBATOMIC_LINK_FLAGS)
      get_aix_libatomic_default_link_flags(COMPILER_RT_LIBATOMIC_LINK_FLAGS
        "${CMAKE_CURRENT_SOURCE_DIR}/ppc/atomic.exp")
    endif()
    # For different versions of cmake, SHARED behaves differently. For some
    # versions, we might need MODULE rather than SHARED.
    get_aix_libatomic_type(BUILTIN_TYPE)
  else()
    list(APPEND COMPILER_RT_LIBATOMIC_LINK_FLAGS -nodefaultlibs)
  endif()
  foreach (arch ${BUILTIN_SUPPORTED_ARCH})
    if(CAN_TARGET_${arch})
      list(APPEND COMPILER_RT_LIBATOMIC_LINK_LIBS_${arch} clang_rt.builtins-${arch})
      append_list_if(MINGW "${MINGW_LIBRARIES}" COMPILER_RT_LIBATOMIC_LINK_LIBS_${arch})
      add_compiler_rt_runtime(clang_rt.atomic
                              ${BUILTIN_TYPE}
                              ARCHS ${arch}
                              SOURCES atomic.c
                              LINK_FLAGS ${COMPILER_RT_LIBATOMIC_LINK_FLAGS}
                              LINK_LIBS ${COMPILER_RT_LIBATOMIC_LINK_LIBS_${arch}}
                              PARENT_TARGET builtins-standalone-atomic)
    endif()
  endforeach()
  # FIXME: On AIX, we have to archive built shared libraries into a static
  # archive, i.e., libatomic.a. Once cmake adds support of such usage for AIX,
  # this ad-hoc part can be removed.
  if(OS_NAME MATCHES "AIX")
    archive_aix_libatomic(clang_rt.atomic libatomic
                          ARCHS ${BUILTIN_SUPPORTED_ARCH}
                          PARENT_TARGET builtins-standalone-atomic)
  endif()
  add_dependencies(compiler-rt builtins-standalone-atomic)
endif()

cmake_dependent_option(COMPILER_RT_BUILD_CRT "Build crtbegin.o/crtend.o" ON "COMPILER_RT_HAS_CRT" OFF)

if (COMPILER_RT_BUILD_CRT)
  add_compiler_rt_component(crt)

  option(COMPILER_RT_CRT_USE_EH_FRAME_REGISTRY "Use eh_frame in crtbegin.o/crtend.o" ON)

  include(CheckSectionExists)
  check_section_exists(".init_array" COMPILER_RT_HAS_INITFINI_ARRAY
    SOURCE "volatile int x;\n__attribute__((constructor)) void f(void) {x = 0;}\nint main(void) { return 0; }\n")

  append_list_if(COMPILER_RT_HAS_STD_C11_FLAG -std=c11 CRT_CFLAGS)
  append_list_if(COMPILER_RT_HAS_INITFINI_ARRAY -DCRT_HAS_INITFINI_ARRAY CRT_CFLAGS)
  append_list_if(COMPILER_RT_CRT_USE_EH_FRAME_REGISTRY -DEH_USE_FRAME_REGISTRY CRT_CFLAGS)
  append_list_if(COMPILER_RT_HAS_FPIC_FLAG -fPIC CRT_CFLAGS)
  append_list_if(COMPILER_RT_HAS_WNO_PEDANTIC -Wno-pedantic CRT_CFLAGS)
  if (COMPILER_RT_HAS_FCF_PROTECTION_FLAG)
    append_list_if(COMPILER_RT_ENABLE_CET -fcf-protection=full CRT_CFLAGS)
  endif()

  foreach(arch ${BUILTIN_SUPPORTED_ARCH})
    add_compiler_rt_runtime(clang_rt.crtbegin
      OBJECT
      ARCHS ${arch}
      SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/crtbegin.c
      CFLAGS ${CRT_CFLAGS}
      PARENT_TARGET crt)
    add_compiler_rt_runtime(clang_rt.crtend
      OBJECT
      ARCHS ${arch}
      SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/crtend.c
      CFLAGS ${CRT_CFLAGS}
      PARENT_TARGET crt)
  endforeach()

  add_dependencies(compiler-rt crt)
endif()
