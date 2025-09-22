/*	$OpenBSD: loongson2.h,v 1.4 2013/01/16 20:28:06 miod Exp $	*/

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

#ifndef	_MIPS64_LOONGSON2_H_
#define	_MIPS64_LOONGSON2_H_

/*
 * Loongson 2E/2F specific defines
 */

/*
 * Address Window registers physical addresses
 *
 * The Loongson 2F processor has an AXI crossbar with four possible bus
 * masters, each one having four programmable address windows.
 *
 * Each window is defined with three 64-bit registers:
 * - a base address register, defining the address in the master address
 *   space (base register).
 * - an address mask register, defining which address bits are valid in this
 *   window.  A given address matches a window if (addr & mask) == base.
 * - the location of the window base in the target, as well at the target
 *   number itself (mmap register). The lower 20 bits of the address are
 *   forced as zeroes regardless of their value in this register.
 *   The translated address is thus (addr & ~mask) | (mmap & ~0xfffff).
 */

#define	LOONGSON_AWR_BASE_ADDRESS	0x3ff00000

#define	LOONGSON_AWR_BASE(master, window) \
	(LOONGSON_AWR_BASE_ADDRESS + (window) * 0x08 + (master) * 0x60 + 0x00)
#define	LOONGSON_AWR_SIZE(master, window) \
	(LOONGSON_AWR_BASE_ADDRESS + (window) * 0x08 + (master) * 0x60 + 0x20)
#define	LOONGSON_AWR_MMAP(master, window) \
	(LOONGSON_AWR_BASE_ADDRESS + (window) * 0x08 + (master) * 0x60 + 0x40)

/*
 * Bits in the diagnostic register
 */

#define	COP_0_DIAG_ITLB_CLEAR		0x04
#define	COP_0_DIAG_BTB_CLEAR		0x02
#define	COP_0_DIAG_RAS_DISABLE		0x01

#if defined(_KERNEL) && !defined(_LOCORE)
int	loongson2f_cpuspeed(int *);
void	loongson2f_setperf(int);
#endif

#endif	/* _MIPS64_LOONGSON2_H_ */
