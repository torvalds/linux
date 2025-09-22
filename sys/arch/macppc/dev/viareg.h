/*	$OpenBSD: viareg.h,v 1.4 2002/06/07 07:14:48 miod Exp $	*/
/*	$NetBSD: viareg.h,v 1.2 1998/10/20 14:56:30 tsubai Exp $	*/

/*-
 * Copyright (C) 1993	Allen K. Briggs, Chris P. Caputo,
 *			Michael L. Finch, Bradley A. Grantham, and
 *			Lawrence A. Kesteloot
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the Alice Group.
 * 4. The names of the Alice Group or any of its members may not be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE ALICE GROUP ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE ALICE GROUP BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
/*

	Prototype VIA control definitions

	06/04/92,22:33:57 BG Let's see what I can do.

*/

extern volatile unsigned char *Via1Base;
#define VIA1_addr	Via1Base	/* at PA 0x50f00000 */

#define VIA1		0
#define VIA2		0

	/* VIA interface registers */
#define vBufA		0x1e00	/* register A */
#define vBufB		0	/* register B */
#define vDirA		0x0600	/* data direction register */
#define vDirB		0x0400	/* data direction register */
#define vT1C		0x0800
#define vT1CH		0x0a00
#define vT1L		0x0c00
#define vT1LH		0x0e00
#define vT2C		0x1000
#define vT2CH		0x1200
#define vSR		0x1400	/* shift register */
#define vACR		0x1600	/* aux control register */
#define vIFR		0x1a00	/* interrupt flag register */
#define vIER		0x1c00	/* interrupt enable register */

#define via_reg(v, r) (*(Via1Base + (r)))

#include <machine/pio.h>

static __inline void
via_reg_and(int ign, int reg, int val)
{
	volatile unsigned char *addr = Via1Base + reg;

	out8(addr, in8(addr) & val);
}

static __inline void
via_reg_or(int ign, int reg, int val)
{
	volatile unsigned char *addr = Via1Base + reg;

	out8(addr, in8(addr) | val);
}

static __inline void
via_reg_xor(int ign, int reg, int val)
{
	volatile unsigned char *addr = Via1Base + reg;

	out8(addr, in8(addr) ^ val);
}

static __inline int
read_via_reg(int ign, int reg)
{
	volatile unsigned char *addr = Via1Base + reg;

	return in8(addr);
}

static __inline void
write_via_reg(int ign, int reg, int val)
{
	volatile unsigned char *addr = Via1Base + reg;

	out8(addr, val);
}
