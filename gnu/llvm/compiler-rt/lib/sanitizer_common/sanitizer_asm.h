//===-- sanitizer_asm.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Various support for assembler.
//
//===----------------------------------------------------------------------===//

// Some toolchains do not support .cfi asm directives, so we have to hide
// them inside macros.
#if defined(__clang__) ||                                                      \
    (defined(__GNUC__) && defined(__GCC_HAVE_DWARF2_CFI_ASM))
  // GCC defined __GCC_HAVE_DWARF2_CFI_ASM if it supports CFI.
  // Clang seems to support CFI by default (or not?).
  // We need two versions of macros: for inline asm and standalone asm files.
# define CFI_INL_ADJUST_CFA_OFFSET(n) ".cfi_adjust_cfa_offset " #n ";"

# define CFI_STARTPROC .cfi_startproc
# define CFI_ENDPROC .cfi_endproc
# define CFI_ADJUST_CFA_OFFSET(n) .cfi_adjust_cfa_offset n
# define CFI_DEF_CFA_OFFSET(n) .cfi_def_cfa_offset n
# define CFI_REL_OFFSET(reg, n) .cfi_rel_offset reg, n
# define CFI_OFFSET(reg, n) .cfi_offset reg, n
# define CFI_DEF_CFA_REGISTER(reg) .cfi_def_cfa_register reg
# define CFI_DEF_CFA(reg, n) .cfi_def_cfa reg, n
# define CFI_RESTORE(reg) .cfi_restore reg

#else  // No CFI
# define CFI_INL_ADJUST_CFA_OFFSET(n)
# define CFI_STARTPROC
# define CFI_ENDPROC
# define CFI_ADJUST_CFA_OFFSET(n)
# define CFI_DEF_CFA_OFFSET(n)
# define CFI_REL_OFFSET(reg, n)
# define CFI_OFFSET(reg, n)
# define CFI_DEF_CFA_REGISTER(reg)
# define CFI_DEF_CFA(reg, n)
# define CFI_RESTORE(reg)
#endif

#if defined(__aarch64__) && defined(__ARM_FEATURE_BTI_DEFAULT)
# define ASM_STARTPROC CFI_STARTPROC; hint #34
# define C_ASM_STARTPROC SANITIZER_STRINGIFY(CFI_STARTPROC) "\nhint #34"
#else
# define ASM_STARTPROC CFI_STARTPROC
# define C_ASM_STARTPROC SANITIZER_STRINGIFY(CFI_STARTPROC)
#endif
#define ASM_ENDPROC CFI_ENDPROC
#define C_ASM_ENDPROC SANITIZER_STRINGIFY(CFI_ENDPROC)

#if defined(__x86_64__) || defined(__i386__) || defined(__sparc__)
# define ASM_TAIL_CALL jmp
#elif defined(__arm__) || defined(__aarch64__) || defined(__mips__) || \
    defined(__powerpc__) || defined(__loongarch_lp64)
# define ASM_TAIL_CALL b
#elif defined(__s390__)
# define ASM_TAIL_CALL jg
#elif defined(__riscv)
# define ASM_TAIL_CALL tail
#endif

// Currently, almost all of the shared libraries rely on the value of
// $t9 to get the address of current function, instead of PCREL, even
// on MIPSr6. To be compatiable with them, we have to set $t9 properly.
// MIPS uses GOT to get the address of preemptible functions.
#if defined(__mips64)
#  define C_ASM_TAIL_CALL(t_func, i_func)                       \
    "lui $t8, %hi(%neg(%gp_rel(" t_func ")))\n"                 \
    "daddu $t8, $t8, $t9\n"                                     \
    "daddiu $t8, $t8, %lo(%neg(%gp_rel(" t_func ")))\n"         \
    "ld $t9, %got_disp(" i_func ")($t8)\n"                      \
    "jr $t9\n"
#elif defined(__mips__)
#  define C_ASM_TAIL_CALL(t_func, i_func)                       \
    ".set    noreorder\n"                                       \
    ".cpload $t9\n"                                             \
    ".set    reorder\n"                                         \
    "lw $t9, %got(" i_func ")($gp)\n"                           \
    "jr $t9\n"
#elif defined(ASM_TAIL_CALL)
#  define C_ASM_TAIL_CALL(t_func, i_func)                       \
    SANITIZER_STRINGIFY(ASM_TAIL_CALL) " " i_func
#endif

#if defined(__ELF__) && defined(__x86_64__) || defined(__i386__) || \
    defined(__riscv)
# define ASM_PREEMPTIBLE_SYM(sym) sym@plt
#else
# define ASM_PREEMPTIBLE_SYM(sym) sym
#endif

#if !defined(__APPLE__)
# define ASM_HIDDEN(symbol) .hidden symbol
# if defined(__arm__) || defined(__aarch64__)
#  define ASM_TYPE_FUNCTION(symbol) .type symbol, %function
# else
#  define ASM_TYPE_FUNCTION(symbol) .type symbol, @function
# endif
# define ASM_SIZE(symbol) .size symbol, .-symbol
# define ASM_SYMBOL(symbol) symbol
# define ASM_SYMBOL_INTERCEPTOR(symbol) symbol
# if defined(__i386__) || defined(__powerpc__) || defined(__s390__) || \
     defined(__sparc__)
// For details, see interception.h
#  define ASM_WRAPPER_NAME(symbol) __interceptor_##symbol
#  define ASM_TRAMPOLINE_ALIAS(symbol, name)                                   \
         .weak symbol;                                                         \
         .set symbol, ASM_WRAPPER_NAME(name)
#  define ASM_INTERCEPTOR_TRAMPOLINE(name)
#  define ASM_INTERCEPTOR_TRAMPOLINE_SUPPORT 0
# else  // Architecture supports interceptor trampoline
// Keep trampoline implementation in sync with interception/interception.h
#  define ASM_WRAPPER_NAME(symbol) ___interceptor_##symbol
#  define ASM_TRAMPOLINE_ALIAS(symbol, name)                                   \
         .weak symbol;                                                         \
         .set symbol, __interceptor_trampoline_##name
#  define ASM_INTERCEPTOR_TRAMPOLINE(name)                                     \
         .weak __interceptor_##name;                                           \
         .set __interceptor_##name, ASM_WRAPPER_NAME(name);                    \
         .globl __interceptor_trampoline_##name;                               \
         ASM_TYPE_FUNCTION(__interceptor_trampoline_##name);                   \
         __interceptor_trampoline_##name:                                      \
                 ASM_STARTPROC;                                                \
                 ASM_TAIL_CALL ASM_PREEMPTIBLE_SYM(__interceptor_##name);      \
                 ASM_ENDPROC;                                                  \
         ASM_SIZE(__interceptor_trampoline_##name)
#  define ASM_INTERCEPTOR_TRAMPOLINE_SUPPORT 1
# endif  // Architecture supports interceptor trampoline
#else
# define ASM_HIDDEN(symbol)
# define ASM_TYPE_FUNCTION(symbol)
# define ASM_SIZE(symbol)
# define ASM_SYMBOL(symbol) _##symbol
# define ASM_SYMBOL_INTERCEPTOR(symbol) _wrap_##symbol
# define ASM_WRAPPER_NAME(symbol) __interceptor_##symbol
#endif

#if defined(__ELF__) && (defined(__GNU__) || defined(__FreeBSD__) || \
                         defined(__Fuchsia__) || defined(__linux__))
// clang-format off
#define NO_EXEC_STACK_DIRECTIVE .section .note.GNU-stack,"",%progbits
// clang-format on
#else
#define NO_EXEC_STACK_DIRECTIVE
#endif

#if (defined(__x86_64__) || defined(__i386__)) && defined(__has_include) && __has_include(<cet.h>)
#include <cet.h>
#endif
#ifndef _CET_ENDBR
#define _CET_ENDBR
#endif
