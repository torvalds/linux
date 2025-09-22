//===-- assembly.h - compiler-rt assembler support macros -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines macros for use in compiler-rt assembler source.
// This file is not part of the interface of this library.
//
//===----------------------------------------------------------------------===//

#ifndef COMPILERRT_ASSEMBLY_H
#define COMPILERRT_ASSEMBLY_H

#if defined(__linux__) && defined(__CET__)
#if __has_include(<cet.h>)
#include <cet.h>
#endif
#endif

#if defined(__APPLE__) && defined(__aarch64__)
#define SEPARATOR %%
#else
#define SEPARATOR ;
#endif

#if defined(__APPLE__)
#define HIDDEN(name) .private_extern name
#define LOCAL_LABEL(name) L_##name
// tell linker it can break up file at label boundaries
#define FILE_LEVEL_DIRECTIVE .subsections_via_symbols
#define SYMBOL_IS_FUNC(name)
#define CONST_SECTION .const

#define NO_EXEC_STACK_DIRECTIVE

#elif defined(__ELF__)

#define HIDDEN(name) .hidden name
#define LOCAL_LABEL(name) .L_##name
#define FILE_LEVEL_DIRECTIVE
#if defined(__arm__) || defined(__aarch64__)
#define SYMBOL_IS_FUNC(name) .type name,%function
#else
#define SYMBOL_IS_FUNC(name) .type name,@function
#endif
#define CONST_SECTION .section .rodata

#if defined(__GNU__) || defined(__FreeBSD__) || defined(__Fuchsia__) ||        \
    defined(__linux__)
#define NO_EXEC_STACK_DIRECTIVE .section .note.GNU-stack,"",%progbits
#else
#define NO_EXEC_STACK_DIRECTIVE
#endif

#else // !__APPLE__ && !__ELF__

#define HIDDEN(name)
#define LOCAL_LABEL(name) .L ## name
#define FILE_LEVEL_DIRECTIVE
#define SYMBOL_IS_FUNC(name)                                                   \
  .def name SEPARATOR                                                          \
    .scl 2 SEPARATOR                                                           \
    .type 32 SEPARATOR                                                         \
  .endef
#define CONST_SECTION .section .rdata,"rd"

#define NO_EXEC_STACK_DIRECTIVE

#endif

#if defined(__arm__) || defined(__aarch64__)
#define FUNC_ALIGN                                                             \
  .text SEPARATOR                                                              \
  .balign 16 SEPARATOR
#else
#define FUNC_ALIGN
#endif

// BTI and PAC gnu property note
#define NT_GNU_PROPERTY_TYPE_0 5
#define GNU_PROPERTY_AARCH64_FEATURE_1_AND 0xc0000000
#define GNU_PROPERTY_AARCH64_FEATURE_1_BTI 1
#define GNU_PROPERTY_AARCH64_FEATURE_1_PAC 2

#if defined(__ARM_FEATURE_BTI_DEFAULT)
#define BTI_FLAG GNU_PROPERTY_AARCH64_FEATURE_1_BTI
#else
#define BTI_FLAG 0
#endif

#if __ARM_FEATURE_PAC_DEFAULT & 3
#define PAC_FLAG GNU_PROPERTY_AARCH64_FEATURE_1_PAC
#else
#define PAC_FLAG 0
#endif

#define GNU_PROPERTY(type, value)                                              \
  .pushsection .note.gnu.property, "a" SEPARATOR                               \
  .p2align 3 SEPARATOR                                                         \
  .word 4 SEPARATOR                                                            \
  .word 16 SEPARATOR                                                           \
  .word NT_GNU_PROPERTY_TYPE_0 SEPARATOR                                       \
  .asciz "GNU" SEPARATOR                                                       \
  .word type SEPARATOR                                                         \
  .word 4 SEPARATOR                                                            \
  .word value SEPARATOR                                                        \
  .word 0 SEPARATOR                                                            \
  .popsection

#if BTI_FLAG != 0
#define BTI_C hint #34
#define BTI_J hint #36
#else
#define BTI_C
#define BTI_J
#endif

#if (BTI_FLAG | PAC_FLAG) != 0
#define GNU_PROPERTY_BTI_PAC                                                   \
  GNU_PROPERTY(GNU_PROPERTY_AARCH64_FEATURE_1_AND, BTI_FLAG | PAC_FLAG)
#else
#define GNU_PROPERTY_BTI_PAC
#endif

#if defined(__clang__) || defined(__GCC_HAVE_DWARF2_CFI_ASM)
#define CFI_START .cfi_startproc
#define CFI_END .cfi_endproc
#else
#define CFI_START
#define CFI_END
#endif

#if defined(__arm__)

// Determine actual [ARM][THUMB[1][2]] ISA using compiler predefined macros:
// - for '-mthumb -march=armv6' compiler defines '__thumb__'
// - for '-mthumb -march=armv7' compiler defines '__thumb__' and '__thumb2__'
#if defined(__thumb2__) || defined(__thumb__)
#define DEFINE_CODE_STATE .thumb SEPARATOR
#define DECLARE_FUNC_ENCODING    .thumb_func SEPARATOR
#if defined(__thumb2__)
#define USE_THUMB_2
#define IT(cond)  it cond
#define ITT(cond) itt cond
#define ITE(cond) ite cond
#else
#define USE_THUMB_1
#define IT(cond)
#define ITT(cond)
#define ITE(cond)
#endif // defined(__thumb__2)
#else // !defined(__thumb2__) && !defined(__thumb__)
#define DEFINE_CODE_STATE .arm SEPARATOR
#define DECLARE_FUNC_ENCODING
#define IT(cond)
#define ITT(cond)
#define ITE(cond)
#endif

#if defined(USE_THUMB_1) && defined(USE_THUMB_2)
#error "USE_THUMB_1 and USE_THUMB_2 can't be defined together."
#endif

#if defined(__ARM_ARCH_4T__) || __ARM_ARCH >= 5
#define ARM_HAS_BX
#endif
#if !defined(__ARM_FEATURE_CLZ) && !defined(USE_THUMB_1) &&  \
    (__ARM_ARCH >= 6 || (__ARM_ARCH == 5 && !defined(__ARM_ARCH_5__)))
#define __ARM_FEATURE_CLZ
#endif

#ifdef ARM_HAS_BX
#define JMP(r) bx r
#define JMPc(r, c) bx##c r
#else
#define JMP(r) mov pc, r
#define JMPc(r, c) mov##c pc, r
#endif

// pop {pc} can't switch Thumb mode on ARMv4T
#if __ARM_ARCH >= 5
#define POP_PC() pop {pc}
#else
#define POP_PC()                                                               \
  pop {ip};                                                                    \
  JMP(ip)
#endif

#if defined(USE_THUMB_2)
#define WIDE(op) op.w
#else
#define WIDE(op) op
#endif
#else // !defined(__arm)
#define DECLARE_FUNC_ENCODING
#define DEFINE_CODE_STATE
#endif

#define GLUE2_(a, b) a##b
#define GLUE(a, b) GLUE2_(a, b)
#define GLUE2(a, b) GLUE2_(a, b)
#define GLUE3_(a, b, c) a##b##c
#define GLUE3(a, b, c) GLUE3_(a, b, c)
#define GLUE4_(a, b, c, d) a##b##c##d
#define GLUE4(a, b, c, d) GLUE4_(a, b, c, d)

#define SYMBOL_NAME(name) GLUE(__USER_LABEL_PREFIX__, name)

#ifdef VISIBILITY_HIDDEN
#define DECLARE_SYMBOL_VISIBILITY(name)                                        \
  HIDDEN(SYMBOL_NAME(name)) SEPARATOR
#define DECLARE_SYMBOL_VISIBILITY_UNMANGLED(name) \
  HIDDEN(name) SEPARATOR
#else
#define DECLARE_SYMBOL_VISIBILITY(name)
#define DECLARE_SYMBOL_VISIBILITY_UNMANGLED(name)
#endif

#define DEFINE_COMPILERRT_FUNCTION(name)                                       \
  DEFINE_CODE_STATE                                                            \
  FILE_LEVEL_DIRECTIVE SEPARATOR                                               \
  .globl SYMBOL_NAME(name) SEPARATOR                                           \
  SYMBOL_IS_FUNC(SYMBOL_NAME(name)) SEPARATOR                                  \
  DECLARE_SYMBOL_VISIBILITY(name)                                              \
  DECLARE_FUNC_ENCODING                                                        \
  SYMBOL_NAME(name):

#define DEFINE_COMPILERRT_THUMB_FUNCTION(name)                                 \
  DEFINE_CODE_STATE                                                            \
  FILE_LEVEL_DIRECTIVE SEPARATOR                                               \
  .globl SYMBOL_NAME(name) SEPARATOR                                           \
  SYMBOL_IS_FUNC(SYMBOL_NAME(name)) SEPARATOR                                  \
  DECLARE_SYMBOL_VISIBILITY(name) SEPARATOR                                    \
  .thumb_func SEPARATOR                                                        \
  SYMBOL_NAME(name):

#define DEFINE_COMPILERRT_PRIVATE_FUNCTION(name)                               \
  DEFINE_CODE_STATE                                                            \
  FILE_LEVEL_DIRECTIVE SEPARATOR                                               \
  .globl SYMBOL_NAME(name) SEPARATOR                                           \
  SYMBOL_IS_FUNC(SYMBOL_NAME(name)) SEPARATOR                                  \
  HIDDEN(SYMBOL_NAME(name)) SEPARATOR                                          \
  DECLARE_FUNC_ENCODING                                                        \
  SYMBOL_NAME(name):

#define DEFINE_COMPILERRT_PRIVATE_FUNCTION_UNMANGLED(name)                     \
  DEFINE_CODE_STATE                                                            \
  .globl name SEPARATOR                                                        \
  SYMBOL_IS_FUNC(name) SEPARATOR                                               \
  HIDDEN(name) SEPARATOR                                                       \
  DECLARE_FUNC_ENCODING                                                        \
  name:

#define DEFINE_COMPILERRT_OUTLINE_FUNCTION_UNMANGLED(name)                     \
  DEFINE_CODE_STATE                                                            \
  FUNC_ALIGN                                                                   \
  .globl name SEPARATOR                                                        \
  SYMBOL_IS_FUNC(name) SEPARATOR                                               \
  DECLARE_SYMBOL_VISIBILITY_UNMANGLED(name) SEPARATOR                          \
  DECLARE_FUNC_ENCODING                                                        \
  name:                                                                        \
  SEPARATOR CFI_START                                                          \
  SEPARATOR BTI_C

#define DEFINE_COMPILERRT_FUNCTION_ALIAS(name, target)                         \
  .globl SYMBOL_NAME(name) SEPARATOR                                           \
  SYMBOL_IS_FUNC(SYMBOL_NAME(name)) SEPARATOR                                  \
  DECLARE_SYMBOL_VISIBILITY(name) SEPARATOR                                    \
  .set SYMBOL_NAME(name), SYMBOL_NAME(target) SEPARATOR

#if defined(__ARM_EABI__)
#define DEFINE_AEABI_FUNCTION_ALIAS(aeabi_name, name)                          \
  DEFINE_COMPILERRT_FUNCTION_ALIAS(aeabi_name, name)
#else
#define DEFINE_AEABI_FUNCTION_ALIAS(aeabi_name, name)
#endif

#ifdef __ELF__
#define END_COMPILERRT_FUNCTION(name)                                          \
  .size SYMBOL_NAME(name), . - SYMBOL_NAME(name)
#define END_COMPILERRT_OUTLINE_FUNCTION(name)                                  \
  CFI_END SEPARATOR                                                            \
  .size SYMBOL_NAME(name), . - SYMBOL_NAME(name)
#else
#define END_COMPILERRT_FUNCTION(name)
#define END_COMPILERRT_OUTLINE_FUNCTION(name)                                  \
  CFI_END
#endif

#endif // COMPILERRT_ASSEMBLY_H
