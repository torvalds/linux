/*	$OpenBSD: regnum.h,v 1.4 2014/07/09 12:58:08 miod Exp $ */

/*
 * Copyright (c) 2001-2002 Opsycon AB  (www.opsycon.se / www.opsycon.com)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#ifndef _MIPS64_REGNUM_H_
#define _MIPS64_REGNUM_H_

/*
 * Location of the saved registers relative to ZERO.
 * Usage is p->p_regs[XX].
 */
#define ZERO	0
#define AST	1
#define V0	2
#define V1	3
#define A0	4
#define A1	5
#define A2	6
#define A3	7
#define A4	8
#define A5	9
#define A6	10
#define A7	11
#define T0	12
#define T1	13
#define T2	14
#define T3	15
#define S0	16
#define S1	17
#define S2	18
#define S3	19
#define S4	20
#define S5	21
#define S6	22
#define S7	23
#define T8	24
#define T9	25
#define K0	26
#define K1	27
#define GP	28
#define SP	29
#define S8	30
#define RA	31
#define	SR	32
#define	PS	SR	/* alias for SR */
#define MULLO	33
#define MULHI	34
#define BADVADDR 35
#define CAUSE	36
#define	PC	37
#define	IC	38
#define	CPL	39

#define	NUMSAVEREGS 40		/* Number of registers saved in trap */

#define FPBASE	NUMSAVEREGS
#define F0	(FPBASE+0)
#define F1	(FPBASE+1)
#define F2	(FPBASE+2)
#define F3	(FPBASE+3)
#define F4	(FPBASE+4)
#define F5	(FPBASE+5)
#define F6	(FPBASE+6)
#define F7	(FPBASE+7)
#define F8	(FPBASE+8)
#define F9	(FPBASE+9)
#define F10	(FPBASE+10)
#define F11	(FPBASE+11)
#define F12	(FPBASE+12)
#define F13	(FPBASE+13)
#define F14	(FPBASE+14)
#define F15	(FPBASE+15)
#define F16	(FPBASE+16)
#define F17	(FPBASE+17)
#define F18	(FPBASE+18)
#define F19	(FPBASE+19)
#define F20	(FPBASE+20)
#define F21	(FPBASE+21)
#define F22	(FPBASE+22)
#define F23	(FPBASE+23)
#define F24	(FPBASE+24)
#define F25	(FPBASE+25)
#define F26	(FPBASE+26)
#define F27	(FPBASE+27)
#define F28	(FPBASE+28)
#define F29	(FPBASE+29)
#define F30	(FPBASE+30)
#define F31	(FPBASE+31)
#define	FSR	(FPBASE+32)

#define	NUMFPREGS 33

#define	NREGS	(NUMSAVEREGS + NUMFPREGS)

#endif /* !_MIPS64_REGNUM_H_ */
