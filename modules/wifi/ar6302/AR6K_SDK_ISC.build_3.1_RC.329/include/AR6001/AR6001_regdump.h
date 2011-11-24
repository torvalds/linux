//------------------------------------------------------------------------------
// <copyright file="AR6001_regdump.h" company="Atheros">
//    Copyright (c) 2006 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// Author(s): ="Atheros"
//==============================================================================

#ifndef __AR6000_REGDUMP_H__
#define __AR6000_REGDUMP_H__

#if !defined(__ASSEMBLER__)
/*
 * Target CPU state at the time of failure is reflected
 * in a register dump, which the Host can fetch through
 * the diagnostic window.
 */

struct MIPS_exception_frame_s {
    A_UINT32 pc;        /* Program Counter */
    A_UINT32 at;        /* MIPS General Purpose registers */
    A_UINT32 v0;
    A_UINT32 v1;
    A_UINT32 a0;
    A_UINT32 a1;
    A_UINT32 a2;
    A_UINT32 a3;
    A_UINT32 t0;
    A_UINT32 t1;
    A_UINT32 t2;
    A_UINT32 t3;
    A_UINT32 t4;
    A_UINT32 t5;
    A_UINT32 t6;
    A_UINT32 t7;
    A_UINT32 s0;
    A_UINT32 s1;
    A_UINT32 s2;
    A_UINT32 s3;
    A_UINT32 s4;
    A_UINT32 s5;
    A_UINT32 s6;
    A_UINT32 s7;
    A_UINT32 t8;
    A_UINT32 t9;
    A_UINT32 k0;
    A_UINT32 k1;
    A_UINT32 gp;
    A_UINT32 sp;
    A_UINT32 s8;
    A_UINT32 ra;
    A_UINT32 cause; /* Selected coprocessor regs */
    A_UINT32 status;
};
typedef struct MIPS_exception_frame_s CPU_exception_frame_t;

#endif

/*
 * Offsets into MIPS_exception_frame structure, for use in assembler code
 * MUST MATCH C STRUCTURE ABOVE
 */
#define RD_pc           0
#define RD_at           1
#define RD_v0           2
#define RD_v1           3
#define RD_a0           4
#define RD_a1           5
#define RD_a2           6
#define RD_a3           7
#define RD_t0           8
#define RD_t1           9
#define RD_t2           10
#define RD_t3           11
#define RD_t4           12
#define RD_t5           13
#define RD_t6           14
#define RD_t7           15
#define RD_s0           16
#define RD_s1           17
#define RD_s2           18
#define RD_s3           19
#define RD_s4           20
#define RD_s5           21
#define RD_s6           22
#define RD_s7           23
#define RD_t8           24
#define RD_t9           25
#define RD_k0           26
#define RD_k1           27
#define RD_gp           28
#define RD_sp           29
#define RD_s8           30
#define RD_ra           31
#define RD_cause        32
#define RD_status       33

#define RD_SIZE         (34*4) /* Space for this number of words */

#endif /* __AR6000_REGDUMP_H__ */
