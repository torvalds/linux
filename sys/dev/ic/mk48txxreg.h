/*	$OpenBSD: mk48txxreg.h,v 1.5 2009/05/15 23:02:25 miod Exp $	*/
/*	$NetBSD: mk48txxreg.h,v 1.4 2000/11/11 11:59:42 pk Exp $ */
/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Paul Kranenburg.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Mostek MK48Txx clocks.
 *
 * The MK48T02 has 2KB of non-volatile memory. The time-of-day clock
 * registers start at offset 0x7f8.
 *
 * The MK48T08 and MK48T18 have 8KB of non-volatile memory
 *
 * The MK48T59 also has 8KB of non-volatile memory but in addition it
 * has a battery low detection bit and a power supply wakeup alarm for
 * power management.  It's at offset 0x1ff0 in the NVRAM.
 */

/*
 * Mostek MK48TXX register definitions
 */

/*
 * The first bank of eight registers at offset (nvramsz - 16) is
 * available only on recenter (which??) MK48Txx models.
 */
#define MK48TXX_X0	0	/* find out later */
				/* ... */
#define MK48TXX_X7	7	/* find out later */
#define MK48TXX_ICSR	8	/* control register */
#define MK48TXX_ISEC	9	/* seconds (0..59; BCD) */
#define MK48TXX_IMIN	10	/* minutes (0..59; BCD) */
#define MK48TXX_IHOUR	11	/* hour (0..23; BCD) */
#define MK48TXX_IWDAY	12	/* weekday (1..7) */
#define MK48TXX_IDAY	13	/* day in month (1..31; BCD) */
#define MK48TXX_IMON	14	/* month (1..12; BCD) */
#define MK48TXX_IYEAR	15	/* year (0..99; BCD) */

/* Bits in the control register */
#define MK48TXX_CSR_WRITE	0x80	/* want to write */
#define MK48TXX_CSR_READ	0x40	/* want to read (freeze clock) */

#define MK48T02_CLKSZ		2048
#define MK48T02_CLKOFF		0x7f0

#define MK48T08_CLKSZ		8192
#define MK48T08_CLKOFF		0x1ff0

#define MK48T18_CLKSZ		8192
#define MK48T18_CLKOFF		0x1ff0

#define MK48T59_CLKSZ		8192
#define MK48T59_CLKOFF		0x1ff0

#define	MK48T35_CLKSZ		0x8000
#define	MK48T35_CLKOFF		0x7ff0

/* Chip attach function */
todr_chip_handle_t mk48txx_attach(bus_space_tag_t, bus_space_handle_t,
				  const char *, int);

/* Retrieve size of the on-chip NVRAM area */
int	mk48txx_get_nvram_size(todr_chip_handle_t, bus_size_t *);
