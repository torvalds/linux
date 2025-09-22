// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "assembly.h"

// Out-of-line LSE atomics helpers. Ported from libgcc library.
// N = {1, 2, 4, 8}
// M = {1, 2, 4, 8, 16}
// ORDER = {'relax', 'acq', 'rel', 'acq_rel', 'sync'}
// Routines implemented:
//
//  iM __aarch64_casM_ORDER(iM expected, iM desired, iM *ptr)
//  iN __aarch64_swpN_ORDER(iN val, iN *ptr)
//  iN __aarch64_ldaddN_ORDER(iN val, iN *ptr)
//  iN __aarch64_ldclrN_ORDER(iN val, iN *ptr)
//  iN __aarch64_ldeorN_ORDER(iN val, iN *ptr)
//  iN __aarch64_ldsetN_ORDER(iN val, iN *ptr)
//
// Routines may modify temporary registers tmp0, tmp1, tmp2,
// return value x0 and the flags only.

#ifdef __aarch64__

#ifdef HAS_ASM_LSE
.arch armv8-a+lse
#else
.arch armv8-a
#endif

#if !defined(__APPLE__)
HIDDEN(__aarch64_have_lse_atomics)
#else
HIDDEN(___aarch64_have_lse_atomics)
#endif

// Generate mnemonics for
// L_cas:                                 SIZE: 1,2,4,8,16 MODEL: 1,2,3,4,5
// L_swp L_ldadd L_ldclr L_ldeor L_ldset: SIZE: 1,2,4,8    MODEL: 1,2,3,4,5

#if SIZE == 1
#define S b
#define UXT uxtb
#define B 0x00000000
#elif SIZE == 2
#define S h
#define UXT uxth
#define B 0x40000000
#elif SIZE == 4 || SIZE == 8 || SIZE == 16
#define S
#define UXT mov
#if SIZE == 4
#define B 0x80000000
#elif SIZE == 8
#define B 0xc0000000
#endif
#else
#error
#endif // SIZE

#if MODEL == 1
#define SUFF _relax
#define A
#define L
#define M 0x000000
#define N 0x000000
#define BARRIER
#elif MODEL == 2
#define SUFF _acq
#define A a
#define L
#define M 0x400000
#define N 0x800000
#define BARRIER
#elif MODEL == 3
#define SUFF _rel
#define A
#define L l
#define M 0x008000
#define N 0x400000
#define BARRIER
#elif MODEL == 4
#define SUFF _acq_rel
#define A a
#define L l
#define M 0x408000
#define N 0xc00000
#define BARRIER
#elif MODEL == 5
#define SUFF _sync
#ifdef L_swp
// swp has _acq semantics.
#define A a
#define L
#define M 0x400000
#define N 0x800000
#else
// All other _sync functions have _seq semantics.
#define A a
#define L l
#define M 0x408000
#define N 0xc00000
#endif
#define BARRIER dmb ish
#else
#error
#endif // MODEL

// Define register size.
#define x(N) GLUE2(x, N)
#define w(N) GLUE2(w, N)
#if SIZE < 8
#define s(N) w(N)
#else
#define s(N) x(N)
#endif

#define NAME(BASE) GLUE4(__aarch64_, BASE, SIZE, SUFF)
#if MODEL == 5
// Drop A for _sync functions.
#define LDXR GLUE3(ld, xr, S)
#else
#define LDXR GLUE4(ld, A, xr, S)
#endif
#define STXR GLUE4(st, L, xr, S)

// Define temporary registers.
#define tmp0 16
#define tmp1 17
#define tmp2 15

// Macro for branch to label if no LSE available
.macro JUMP_IF_NOT_LSE label
#if !defined(__APPLE__)
        adrp    x(tmp0), __aarch64_have_lse_atomics
        ldrb    w(tmp0), [x(tmp0), :lo12:__aarch64_have_lse_atomics]
#else
        adrp    x(tmp0), ___aarch64_have_lse_atomics@page
        ldrb    w(tmp0), [x(tmp0), ___aarch64_have_lse_atomics@pageoff]
#endif
        cbz     w(tmp0), \label
.endm

#ifdef L_cas
DEFINE_COMPILERRT_OUTLINE_FUNCTION_UNMANGLED(NAME(cas))
        JUMP_IF_NOT_LSE 8f
#if SIZE < 16
#ifdef HAS_ASM_LSE
#define CAS GLUE4(cas, A, L, S) s(0), s(1), [x2]
#else
#define CAS .inst 0x08a07c41 + B + M
#endif
        CAS    // s(0), s(1), [x2]
        ret
8:
        UXT    s(tmp0), s(0)
0:
        LDXR   s(0), [x2]
        cmp    s(0), s(tmp0)
        bne    1f
        STXR   w(tmp1), s(1), [x2]
        cbnz   w(tmp1), 0b
1:
        BARRIER
        ret
#else
#if MODEL == 5
// Drop A for _sync functions.
#define LDXP GLUE2(ld, xp)
#else
#define LDXP GLUE3(ld, A, xp)
#endif
#define STXP GLUE3(st, L, xp)
#ifdef HAS_ASM_LSE
#define CASP GLUE3(casp, A, L)  x0, x1, x2, x3, [x4]
#else
#define CASP .inst 0x48207c82 + M
#endif

        CASP   // x0, x1, x2, x3, [x4]
        ret
8:
        mov    x(tmp0), x0
        mov    x(tmp1), x1
0:
        LDXP   x0, x1, [x4]
        cmp    x0, x(tmp0)
        ccmp   x1, x(tmp1), #0, eq
        bne    1f
        STXP   w(tmp2), x2, x3, [x4]
        cbnz   w(tmp2), 0b
1:
        BARRIER
        ret
#endif
END_COMPILERRT_OUTLINE_FUNCTION(NAME(cas))
#endif // L_cas

#ifdef L_swp
#ifdef HAS_ASM_LSE
#define SWP GLUE4(swp, A, L, S)  s(0), s(0), [x1]
#else
#define SWP .inst 0x38208020 + B + N
#endif
DEFINE_COMPILERRT_OUTLINE_FUNCTION_UNMANGLED(NAME(swp))
        JUMP_IF_NOT_LSE 8f
        SWP    // s(0), s(0), [x1]
        ret
8:
        mov    s(tmp0), s(0)
0:
        LDXR   s(0), [x1]
        STXR   w(tmp1), s(tmp0), [x1]
        cbnz   w(tmp1), 0b
        BARRIER
        ret
END_COMPILERRT_OUTLINE_FUNCTION(NAME(swp))
#endif // L_swp

#if defined(L_ldadd) || defined(L_ldclr) ||                                    \
    defined(L_ldeor) || defined(L_ldset)

#ifdef L_ldadd
#define LDNM ldadd
#define OP add
#define OPN 0x0000
#elif defined(L_ldclr)
#define LDNM ldclr
#define OP bic
#define OPN 0x1000
#elif defined(L_ldeor)
#define LDNM ldeor
#define OP eor
#define OPN 0x2000
#elif defined(L_ldset)
#define LDNM ldset
#define OP orr
#define OPN 0x3000
#else
#error
#endif

#ifdef HAS_ASM_LSE
#define LDOP GLUE4(LDNM, A, L, S) s(0), s(0), [x1]
#else
#define LDOP .inst 0x38200020 + OPN + B + N
#endif

DEFINE_COMPILERRT_OUTLINE_FUNCTION_UNMANGLED(NAME(LDNM))
        JUMP_IF_NOT_LSE 8f
        LDOP // s(0), s(0), [x1]
        ret
8:
        mov    s(tmp0), s(0)
0:
        LDXR   s(0), [x1]
        OP     s(tmp1), s(0), s(tmp0)
        STXR   w(tmp2), s(tmp1), [x1]
        cbnz   w(tmp2), 0b
        BARRIER
        ret
END_COMPILERRT_OUTLINE_FUNCTION(NAME(LDNM))
#endif // L_ldadd L_ldclr L_ldeor L_ldset

NO_EXEC_STACK_DIRECTIVE

// GNU property note for BTI and PAC
GNU_PROPERTY_BTI_PAC

#endif // __aarch64__
