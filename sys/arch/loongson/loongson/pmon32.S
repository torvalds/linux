/*	$OpenBSD: pmon32.S,v 1.5 2018/04/20 14:36:42 visa Exp $	*/

/*
 * Copyright (c) 2009 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Wrapper routines to invoke PMON2000 functions from 64-bit code.
 *
 * PMON is compiled either as 32 or 64 bit code.  32 bit code uses the o32 ABI,
 * while 64 bit code uses the gcc o64 ABI (similar to the o32 ABI, but using
 * 64 bit registers).
 *
 * As a result, only up to four arguments to functions will be passed through
 * registers.  It's up to the caller to never invoke pmon_printf() with more
 * than four arguments or with a 64 bit argument; other functions are not
 * affected.
 */

#include <machine/param.h>
#include <machine/asm.h>
#include <mips64/mips_cpu.h>

#ifndef _STANDALONE
#include "assym.h"
#endif

	.set	mips3

	.data
	.globl	pmon_callvec
pmon_callvec:
	.word	0

pmon_o32:
	.word	0

	.text

/*
 * Note that we need to provide a CF_SZ untouched area above sp, or we'll risk
 * our stack being corrupted upon return.
 */
#define	PMON_WRAP(name, index) \
	NNON_LEAF(name, FRAMESZ(CF_SZ + 9 * REGSZ),  ra); \
	PTR_SUBU sp, sp, FRAMESZ(CF_SZ + 9 * REGSZ); \
	REG_S	ra, CF_RA_OFFS(sp); \
	.mask	0xc0ff0000, (CF_RA_OFFS - FRAMESZ(CF_SZ + 9 * REGSZ)); \
	REG_S	s0, (0 * REGSZ + CF_SZ)(sp); \
	REG_S	s1, (1 * REGSZ + CF_SZ)(sp); \
	REG_S	s2, (2 * REGSZ + CF_SZ)(sp); \
	REG_S	s3, (3 * REGSZ + CF_SZ)(sp); \
	REG_S	s4, (4 * REGSZ + CF_SZ)(sp); \
	REG_S	s5, (5 * REGSZ + CF_SZ)(sp); \
	REG_S	s6, (6 * REGSZ + CF_SZ)(sp); \
	REG_S	s7, (7 * REGSZ + CF_SZ)(sp); \
	REG_S	s8, (8 * REGSZ + CF_SZ)(sp); \
	lw	t0, pmon_callvec; \
	lw	t0, (index) * 4 (t0); \
	jalr	t0; \
	nop; \
	REG_L	s8, (8 * REGSZ + CF_SZ)(sp); \
	REG_L	s7, (7 * REGSZ + CF_SZ)(sp); \
	REG_L	s6, (6 * REGSZ + CF_SZ)(sp); \
	REG_L	s5, (5 * REGSZ + CF_SZ)(sp); \
	REG_L	s4, (4 * REGSZ + CF_SZ)(sp); \
	REG_L	s3, (3 * REGSZ + CF_SZ)(sp); \
	REG_L	s2, (2 * REGSZ + CF_SZ)(sp); \
	REG_L	s1, (1 * REGSZ + CF_SZ)(sp); \
	REG_L	s0, (0 * REGSZ + CF_SZ)(sp); \
	REG_L	ra, CF_RA_OFFS(sp); \
	PTR_ADDU sp, sp, FRAMESZ(CF_SZ + 9 * REGSZ); \
	jr	ra; \
	nop; \
	END(name)

PMON_WRAP(pmon_printf, 5)
PMON_WRAP(pmon_gets, 7)
#ifdef _STANDALONE
PMON_WRAP(pmon_open, 0)
PMON_WRAP(pmon_close, 1)
PMON_WRAP(pmon_read, 2)
PMON_WRAP(pmon_cacheflush, 6)
#endif
#if 0	/* unused */
PMON_WRAP(pmon_write, 3)
#endif

/*
 * pmon_lseek() is defined separately to work around the 64-bit code's
 * incompatibility with the o32 ABI.
 */
NNON_LEAF(pmon_lseek, FRAMESZ(CF_SZ + 9 * REGSZ), ra)
	PTR_SUBU sp, sp, FRAMESZ(CF_SZ + 9 * REGSZ)
	REG_S	ra, CF_RA_OFFS(sp)
	.mask	0xc0ff0000, (CF_RA_OFFS - FRAMESZ(CF_SZ + 9 * REGSZ))
	REG_S	s0, (0 * REGSZ + CF_SZ)(sp)
	REG_S	s1, (1 * REGSZ + CF_SZ)(sp)
	REG_S	s2, (2 * REGSZ + CF_SZ)(sp)
	REG_S	s3, (3 * REGSZ + CF_SZ)(sp)
	REG_S	s4, (4 * REGSZ + CF_SZ)(sp)
	REG_S	s5, (5 * REGSZ + CF_SZ)(sp)
	REG_S	s6, (6 * REGSZ + CF_SZ)(sp)
	REG_S	s7, (7 * REGSZ + CF_SZ)(sp)
	REG_S	s8, (8 * REGSZ + CF_SZ)(sp)
	lw	t0, pmon_callvec
	lw	t0, 4 * 4 (t0)
	lw	t1, pmon_o32
	bne	t1, zero, 1f
	nop
	/* gcc o64 ABI call. */
	jalr	t0
	nop
	b	2f
	nop
1:
	/* o32 ABI call.  The parameters have to be rearranged. */
	sw	a2, 4 * 4 (sp)
	sll	a2, a1, 0		# get the low 32 bits
	dsrl	a3, a1, 32		# get the high 32 bits
	jalr	t0
	nop
	/* Rearrange the return value. */
	dsll	v0, v0, 32		# clear any sign extension
	dsrl	v0, v0, 32
	dsll	v1, v1, 32
	or	v0, v0, v1
2:
	REG_L	s8, (8 * REGSZ + CF_SZ)(sp)
	REG_L	s7, (7 * REGSZ + CF_SZ)(sp)
	REG_L	s6, (6 * REGSZ + CF_SZ)(sp)
	REG_L	s5, (5 * REGSZ + CF_SZ)(sp)
	REG_L	s4, (4 * REGSZ + CF_SZ)(sp)
	REG_L	s3, (3 * REGSZ + CF_SZ)(sp)
	REG_L	s2, (2 * REGSZ + CF_SZ)(sp)
	REG_L	s1, (1 * REGSZ + CF_SZ)(sp)
	REG_L	s0, (0 * REGSZ + CF_SZ)(sp)
	REG_L	ra, CF_RA_OFFS(sp)
	PTR_ADDU sp, sp, FRAMESZ(CF_SZ + 9 * REGSZ)
	jr	ra
	nop
END(pmon_lseek)

/*
 * Probe if PMON is compiled to use the o32 ABI.
 */
NNON_LEAF(pmon_probe_abi, FRAMESZ(CF_SZ + 9 * REGSZ), ra)
	PTR_SUBU sp, sp, FRAMESZ(CF_SZ + 9 * REGSZ)
	REG_S	ra, CF_RA_OFFS(sp)
	.mask	0xc0ff0000, (CF_RA_OFFS - FRAMESZ(CF_SZ + 9 * REGSZ))
	REG_S	s0, (0 * REGSZ + CF_SZ)(sp)
	REG_S	s1, (1 * REGSZ + CF_SZ)(sp)
	REG_S	s2, (2 * REGSZ + CF_SZ)(sp)
	REG_S	s3, (3 * REGSZ + CF_SZ)(sp)
	REG_S	s4, (4 * REGSZ + CF_SZ)(sp)
	REG_S	s5, (5 * REGSZ + CF_SZ)(sp)
	REG_S	s6, (6 * REGSZ + CF_SZ)(sp)
	REG_S	s7, (7 * REGSZ + CF_SZ)(sp)
	REG_S	s8, (8 * REGSZ + CF_SZ)(sp)
	/* Skip probing on pre-LS3A machines.  They are not affected. */
	mfc0	t0, COP_0_PRID
	and	t1, t0, 0xff00
	li	t2, 0x6300
	bne	t1, t2, 1f
	and	t1, t0, 0x00ff
	li	t2, 0x0005
	blt	t1, t2, 1f
	nop
	/*
	 * Call lseek() with an invalid fd.  The call should fail with return
	 * value -1.  The value is in register pair v1:v0 if the o32 ABI is
	 * in use, while the gcc o64 ABI uses register v0 only.
	 *
	 * Zero v0 and v1, and if both contain value -1 when the call returns,
	 * assume that PMON uses the o32 ABI.  This relies on the fact that
	 * the firmware detects the invalid fd and returns before value -1
	 * could accidentally be assigned to both registers.
	 */
	li	a0, 0x10000
	move	a1, zero
	move	a2, zero
	move	a3, zero
	sw	zero, 4 * 4 (sp)
	move	v0, zero
	move	v1, zero
	lw	t0, pmon_callvec
	lw	t0, 4 * 4 (t0)
	jalr	t0
	nop
	/* Check the return value. */
	li	t0, -1
	bne	v0, t0, 1f
	nop
	bne	v1, t0, 1f
	li	t1, 1
	sw	t1, pmon_o32
1:
	REG_L	s8, (8 * REGSZ + CF_SZ)(sp)
	REG_L	s7, (7 * REGSZ + CF_SZ)(sp)
	REG_L	s6, (6 * REGSZ + CF_SZ)(sp)
	REG_L	s5, (5 * REGSZ + CF_SZ)(sp)
	REG_L	s4, (4 * REGSZ + CF_SZ)(sp)
	REG_L	s3, (3 * REGSZ + CF_SZ)(sp)
	REG_L	s2, (2 * REGSZ + CF_SZ)(sp)
	REG_L	s1, (1 * REGSZ + CF_SZ)(sp)
	REG_L	s0, (0 * REGSZ + CF_SZ)(sp)
	REG_L	ra, CF_RA_OFFS(sp)
	PTR_ADDU sp, sp, FRAMESZ(CF_SZ + 9 * REGSZ)
	jr	ra
	nop
END(pmon_probe_abi)
