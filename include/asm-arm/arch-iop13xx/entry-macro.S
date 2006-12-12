/*
 * iop13xx low level irq macros
 * Copyright (c) 2005-2006, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 */
	.macro  disable_fiq
	.endm

	/*
	 * Note: a 1-cycle window exists where iintvec will return the value
	 * of iintbase, so we explicitly check for "bad zeros"
	 */
	.macro  get_irqnr_and_base, irqnr, irqstat, base, tmp
	mrc	p15, 0, \tmp, c15, c1, 0
	orr	\tmp, \tmp, #(1 << 6)
	mcr	p15, 0, \tmp, c15, c1, 0	@ Enable cp6 access

	mrc	p6, 0, \irqnr, c3, c2, 0  	@ Read IINTVEC
	cmp	\irqnr, #0
	mrceq	p6, 0, \irqnr, c3, c2, 0  	@ Re-read on potentially bad zero
	adds	\irqstat, \irqnr, #1	  	@ Check for 0xffffffff
	movne	\irqnr, \irqnr, lsr #2	  	@ Convert to irqnr

	biceq	\tmp, \tmp, #(1 << 6)
	mcreq	p15, 0, \tmp, c15, c1, 0	@ Disable cp6 access if no more interrupts
	.endm
