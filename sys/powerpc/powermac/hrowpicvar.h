/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2003 by Peter Grehan. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef  _POWERPC_POWERMAC_HROWPICVAR_H_
#define  _POWERPC_POWERMAC_HROWPICVAR_H_

#define HROWPIC_IRQMAX	64
#define HROWPIC_IRQ_REGNUM	32	/* irqs per register */
#define HROWPIC_IRQ_SHIFT	5	/* high or low irq word */
#define HROWPIC_IRQ_MASK ((HROWPIC_IRQMAX-1) >> 1)  /* irq bit pos in word */

/*
 * Register offsets within bank. There are two identical banks,
 * separated by 16 bytes. Interrupts 0->31 are processed in the
 * second bank, and 32->63 in the first bank.
 */
#define  HPIC_STATUS	0x00		/* active interrupt sources */
#define  HPIC_ENABLE	0x04		/* interrupt asserts ppc EXTINT */
#define  HPIC_CLEAR	0x08		/* clear int source */
#define  HPIC_TRIGGER	0x0c		/* edge/level int trigger */

#define HPIC_PRIMARY	1	/* primary register bank */
#define HPIC_SECONDARY  0       /* secondary register bank */

/*
 * Convert an interrupt into a prim/sec bank number
 */
#define HPIC_INT_TO_BANK(x) \
	(((x) >> HROWPIC_IRQ_SHIFT) ^ 1)

/*
 * Convert an interrupt into the bit number within a bank register
 */
#define HPIC_INT_TO_REGBIT(x) \
	((x) & HROWPIC_IRQ_MASK)

#define  HPIC_1ST_OFFSET  0x10		/* offset to primary reg bank */

struct hrowpic_softc {
	device_t	sc_dev;			/* macio device */
	struct resource *sc_rres;		/* macio bus resource */
	bus_space_tag_t sc_bt;			/* macio bus tag/handle */
	bus_space_handle_t sc_bh;
	int		sc_rrid;
	uint32_t	sc_softreg[2];		/* ENABLE reg copy */
	u_int		sc_vector[HROWPIC_IRQMAX];
};

#endif  /* _POWERPC_POWERMAC_HROWPICVAR_H_ */
