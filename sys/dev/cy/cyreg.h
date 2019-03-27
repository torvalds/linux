/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1995 Bruce Evans.
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
 * 3. Neither the name of the author nor the names of contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Definitions for Cyclades Cyclom-Y serial boards.
 */

/*
 * Cyclades register offsets.  These are physical offsets for ISA boards
 * and physical offsets divided by 2 for PCI boards.
 */
#define	CY8_SVCACKR		0x100	/* (r) */
#define	CY8_SVCACKT		0x200	/* (r) */
#define	CY8_SVCACKM		0x300	/* (r) */
#define	CY16_RESET		0x1400	/* (r) */
#define	CY_CLEAR_INTR		0x1800	/* intr ack address (w) */

#define	CY_MAX_CD1400s		8	/* for Cyclom-32Y */

#define	CY_CLOCK(version)	((version) >= 0x48 ? 60000000 : 25000000)
#define	CY_RTS_DTR_SWAPPED(version)	((version) >= 0x48)

/*
 * The `cd' macros are for access to cd1400 registers.  The `cy' macros
 * are for access to Cyclades registers.  Both sets of macros scale the
 * register number to get an offset, but the scales are different for
 * mostly historical reasons.
 */
#ifdef CyDebug
#define	cd_inb(iobase, reg, cy_align) \
	(++cd_inbs, *((iobase) + (2 * (reg) << (cy_align))))
#define	cy_inb(iobase, reg, cy_align) \
	(++cy_inbs, *((iobase) + ((reg) << (cy_align))))
#define	cd_outb(iobase, reg, cy_align, val) \
	(++cd_outbs, (void)(*((iobase) + (2 * (reg) << (cy_align))) = (val)))
#define	cy_outb(iobase, reg, cy_align, val) \
	(++cy_outbs, (void)(*((iobase) + ((reg) << (cy_align))) = (val)))
#else
#define	cd_inb(iobase, reg, cy_align) \
	(*((iobase) + (2 * (reg) << (cy_align))))
#define	cy_inb(iobase, reg, cy_align) \
	(*((iobase) + ((reg) << (cy_align))))
#define	cd_outb(iobase, reg, cy_align, val) \
	((void)(*((iobase) + (2 * (reg) << (cy_align))) = (val)))
#define	cy_outb(iobase, reg, cy_align, val) \
	((void)(*((iobase) + ((reg) << (cy_align))) = (val)))
#endif
