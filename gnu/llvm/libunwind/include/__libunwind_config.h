//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef ____LIBUNWIND_CONFIG_H__
#define ____LIBUNWIND_CONFIG_H__

#define _LIBUNWIND_VERSION 15000

#if defined(__arm__) && !defined(__USING_SJLJ_EXCEPTIONS__) && \
    !defined(__ARM_DWARF_EH__) && !defined(__SEH__)
#define _LIBUNWIND_ARM_EHABI
#endif

#define _LIBUNWIND_HIGHEST_DWARF_REGISTER_X86       8
#define _LIBUNWIND_HIGHEST_DWARF_REGISTER_X86_64    32
#define _LIBUNWIND_HIGHEST_DWARF_REGISTER_PPC       112
#define _LIBUNWIND_HIGHEST_DWARF_REGISTER_PPC64     116
#define _LIBUNWIND_HIGHEST_DWARF_REGISTER_ARM64     95
#define _LIBUNWIND_HIGHEST_DWARF_REGISTER_ARM       287
#define _LIBUNWIND_HIGHEST_DWARF_REGISTER_OR1K      32
#define _LIBUNWIND_HIGHEST_DWARF_REGISTER_MIPS      65
#define _LIBUNWIND_HIGHEST_DWARF_REGISTER_SPARC     31
#define _LIBUNWIND_HIGHEST_DWARF_REGISTER_SPARC64   31
#define _LIBUNWIND_HIGHEST_DWARF_REGISTER_HEXAGON   34
#define _LIBUNWIND_HIGHEST_DWARF_REGISTER_RISCV     64
#define _LIBUNWIND_HIGHEST_DWARF_REGISTER_VE        143
#define _LIBUNWIND_HIGHEST_DWARF_REGISTER_S390X     83
#define _LIBUNWIND_HIGHEST_DWARF_REGISTER_LOONGARCH 64

#if defined(_LIBUNWIND_IS_NATIVE_ONLY)
# if defined(__linux__)
#  define _LIBUNWIND_TARGET_LINUX 1
# endif
# if defined(__HAIKU__)
#  define _LIBUNWIND_TARGET_HAIKU 1
# endif
# if defined(__i386__)
#  define _LIBUNWIND_TARGET_I386
#  define _LIBUNWIND_CONTEXT_SIZE 8
#  define _LIBUNWIND_CURSOR_SIZE 15
#  define _LIBUNWIND_HIGHEST_DWARF_REGISTER _LIBUNWIND_HIGHEST_DWARF_REGISTER_X86
# elif defined(__x86_64__)
#  define _LIBUNWIND_TARGET_X86_64 1
#  if defined(_WIN64)
#    define _LIBUNWIND_CONTEXT_SIZE 54
#    ifdef __SEH__
#      define _LIBUNWIND_CURSOR_SIZE 204
#    else
#      define _LIBUNWIND_CURSOR_SIZE 66
#    endif
#  else
#    define _LIBUNWIND_CONTEXT_SIZE 21
#    define _LIBUNWIND_CURSOR_SIZE 33
#  endif
#  define _LIBUNWIND_HIGHEST_DWARF_REGISTER _LIBUNWIND_HIGHEST_DWARF_REGISTER_X86_64
# elif defined(__powerpc64__)
#  define _LIBUNWIND_TARGET_PPC64 1
#  define _LIBUNWIND_CONTEXT_SIZE 167
#  define _LIBUNWIND_CURSOR_SIZE 179
#  define _LIBUNWIND_HIGHEST_DWARF_REGISTER _LIBUNWIND_HIGHEST_DWARF_REGISTER_PPC64
# elif defined(__powerpc__)
#  define _LIBUNWIND_TARGET_PPC 1
#  define _LIBUNWIND_CONTEXT_SIZE 117
#  define _LIBUNWIND_CURSOR_SIZE 124
#  define _LIBUNWIND_HIGHEST_DWARF_REGISTER _LIBUNWIND_HIGHEST_DWARF_REGISTER_PPC
# elif defined(__aarch64__)
#  define _LIBUNWIND_TARGET_AARCH64 1
#  define _LIBUNWIND_CONTEXT_SIZE 66
#  if defined(__SEH__)
#    define _LIBUNWIND_CURSOR_SIZE 164
#  else
#    define _LIBUNWIND_CURSOR_SIZE 78
#  endif
#  define _LIBUNWIND_HIGHEST_DWARF_REGISTER _LIBUNWIND_HIGHEST_DWARF_REGISTER_ARM64
# elif defined(__arm__)
#  define _LIBUNWIND_TARGET_ARM 1
#  if defined(__SEH__)
#    define _LIBUNWIND_CONTEXT_SIZE 42
#    define _LIBUNWIND_CURSOR_SIZE 80
#  elif defined(__ARM_WMMX)
#    define _LIBUNWIND_CONTEXT_SIZE 61
#    define _LIBUNWIND_CURSOR_SIZE 68
#  else
#    define _LIBUNWIND_CONTEXT_SIZE 42
#    define _LIBUNWIND_CURSOR_SIZE 49
#  endif
#  define _LIBUNWIND_HIGHEST_DWARF_REGISTER _LIBUNWIND_HIGHEST_DWARF_REGISTER_ARM
# elif defined(__or1k__)
#  define _LIBUNWIND_TARGET_OR1K 1
#  define _LIBUNWIND_CONTEXT_SIZE 16
#  define _LIBUNWIND_CURSOR_SIZE 24
#  define _LIBUNWIND_HIGHEST_DWARF_REGISTER _LIBUNWIND_HIGHEST_DWARF_REGISTER_OR1K
# elif defined(__hexagon__)
#  define _LIBUNWIND_TARGET_HEXAGON 1
// Values here change when : Registers.hpp - hexagon_thread_state_t change
#  define _LIBUNWIND_CONTEXT_SIZE 18
#  define _LIBUNWIND_CURSOR_SIZE 24
#  define _LIBUNWIND_HIGHEST_DWARF_REGISTER _LIBUNWIND_HIGHEST_DWARF_REGISTER_HEXAGON
# elif defined(__mips__)
#  if defined(_ABIO32) && _MIPS_SIM == _ABIO32
#    define _LIBUNWIND_TARGET_MIPS_O32 1
#    if defined(__mips_hard_float)
#      define _LIBUNWIND_CONTEXT_SIZE 50
#      define _LIBUNWIND_CURSOR_SIZE 57
#    else
#      define _LIBUNWIND_CONTEXT_SIZE 18
#      define _LIBUNWIND_CURSOR_SIZE 24
#    endif
#  elif defined(_ABIN32) && _MIPS_SIM == _ABIN32
#    define _LIBUNWIND_TARGET_MIPS_NEWABI 1
#    if defined(__mips_hard_float)
#      define _LIBUNWIND_CONTEXT_SIZE 67
#      define _LIBUNWIND_CURSOR_SIZE 74
#    else
#      define _LIBUNWIND_CONTEXT_SIZE 35
#      define _LIBUNWIND_CURSOR_SIZE 42
#    endif
#  elif defined(_ABI64) && _MIPS_SIM == _ABI64
#    define _LIBUNWIND_TARGET_MIPS_NEWABI 1
#    if defined(__mips_hard_float)
#      define _LIBUNWIND_CONTEXT_SIZE 67
#      define _LIBUNWIND_CURSOR_SIZE 79
#    else
#      define _LIBUNWIND_CONTEXT_SIZE 35
#      define _LIBUNWIND_CURSOR_SIZE 47
#    endif
#  else
#    error "Unsupported MIPS ABI and/or environment"
#  endif
#  define _LIBUNWIND_HIGHEST_DWARF_REGISTER _LIBUNWIND_HIGHEST_DWARF_REGISTER_MIPS
#elif defined(__sparc__) && defined(__arch64__)
#define _LIBUNWIND_TARGET_SPARC64 1
#define _LIBUNWIND_HIGHEST_DWARF_REGISTER                                      \
  _LIBUNWIND_HIGHEST_DWARF_REGISTER_SPARC64
#define _LIBUNWIND_CONTEXT_SIZE 33
#define _LIBUNWIND_CURSOR_SIZE 45
# elif defined(__sparc__)
  #define _LIBUNWIND_TARGET_SPARC 1
  #define _LIBUNWIND_HIGHEST_DWARF_REGISTER _LIBUNWIND_HIGHEST_DWARF_REGISTER_SPARC
  #define _LIBUNWIND_CONTEXT_SIZE 16
  #define _LIBUNWIND_CURSOR_SIZE 23
# elif defined(__riscv)
#  define _LIBUNWIND_TARGET_RISCV 1
#  if defined(__riscv_flen)
#   define RISCV_FLEN __riscv_flen
#  else
#   define RISCV_FLEN 0
#  endif
#  define _LIBUNWIND_CONTEXT_SIZE (32 * (__riscv_xlen + RISCV_FLEN) / 64)
#  if __riscv_xlen == 32
#   define _LIBUNWIND_CURSOR_SIZE (_LIBUNWIND_CONTEXT_SIZE + 7)
#  elif __riscv_xlen == 64
#   define _LIBUNWIND_CURSOR_SIZE (_LIBUNWIND_CONTEXT_SIZE + 12)
#  else
#   error "Unsupported RISC-V ABI"
#  endif
# define _LIBUNWIND_HIGHEST_DWARF_REGISTER _LIBUNWIND_HIGHEST_DWARF_REGISTER_RISCV
# elif defined(__ve__)
#  define _LIBUNWIND_TARGET_VE 1
#  define _LIBUNWIND_CONTEXT_SIZE 67
#  define _LIBUNWIND_CURSOR_SIZE 79
#  define _LIBUNWIND_HIGHEST_DWARF_REGISTER _LIBUNWIND_HIGHEST_DWARF_REGISTER_VE
# elif defined(__s390x__)
#  define _LIBUNWIND_TARGET_S390X 1
#  define _LIBUNWIND_CONTEXT_SIZE 34
#  define _LIBUNWIND_CURSOR_SIZE 46
#  define _LIBUNWIND_HIGHEST_DWARF_REGISTER _LIBUNWIND_HIGHEST_DWARF_REGISTER_S390X
#elif defined(__loongarch__)
#define _LIBUNWIND_TARGET_LOONGARCH 1
#if __loongarch_grlen == 64
#define _LIBUNWIND_CONTEXT_SIZE 65
#define _LIBUNWIND_CURSOR_SIZE 77
#else
#error "Unsupported LoongArch ABI"
#endif
#define _LIBUNWIND_HIGHEST_DWARF_REGISTER                                      \
  _LIBUNWIND_HIGHEST_DWARF_REGISTER_LOONGARCH
#elif defined(__wasm__)
// Unused
#define _LIBUNWIND_CONTEXT_SIZE 0
#define _LIBUNWIND_CURSOR_SIZE 0
# else
#  error "Unsupported architecture."
# endif
#else // !_LIBUNWIND_IS_NATIVE_ONLY
# define _LIBUNWIND_TARGET_I386
# define _LIBUNWIND_TARGET_X86_64 1
# define _LIBUNWIND_TARGET_PPC 1
# define _LIBUNWIND_TARGET_PPC64 1
# define _LIBUNWIND_TARGET_AARCH64 1
# define _LIBUNWIND_TARGET_ARM 1
# define _LIBUNWIND_TARGET_OR1K 1
# define _LIBUNWIND_TARGET_MIPS_O32 1
# define _LIBUNWIND_TARGET_MIPS_NEWABI 1
# define _LIBUNWIND_TARGET_SPARC 1
# define _LIBUNWIND_TARGET_SPARC64 1
# define _LIBUNWIND_TARGET_HEXAGON 1
# define _LIBUNWIND_TARGET_RISCV 1
# define _LIBUNWIND_TARGET_VE 1
# define _LIBUNWIND_TARGET_S390X 1
# define _LIBUNWIND_TARGET_LOONGARCH 1
# define _LIBUNWIND_CONTEXT_SIZE 167
# define _LIBUNWIND_CURSOR_SIZE 204
# define _LIBUNWIND_HIGHEST_DWARF_REGISTER 287
#endif // _LIBUNWIND_IS_NATIVE_ONLY

#endif // ____LIBUNWIND_CONFIG_H__
