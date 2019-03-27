/*-
 * Copyright (c) 2011, 2013 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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

#include "stand.h"
#include "mips.h"

/*-
 * Routines for interacting with the CHERI console UART.  Programming details
 * from the June 2011 "Embedded Peripherals User Guide" by Altera
 * Corporation, tables 6-2 (JTAG UART Core Register Map), 6-3 (Data Register
 * Bits), and 6-4 (Control Register Bits).
 *
 * Hard-coded physical address for the first JTAG UART -- true on all BERI and
 * CHERI boards.
 */
#define	CHERI_UART_BASE		0x7f000000	/* JTAG UART */

/*
 *
 * Offsets of data and control registers relative to the base.  Altera
 * conventions are maintained in CHERI.
 */
#define	ALTERA_JTAG_UART_DATA_OFF	0x00000000
#define	ALTERA_JTAG_UART_CONTROL_OFF	0x00000004

/*
 * Offset 0: 'data' register -- bits 31-16 (RAVAIL), 15 (RVALID),
 * 14-8 (Reserved), 7-0 (DATA).
 *
 * DATA - One byte read or written.
 * RAVAIL - Bytes available to read (excluding the current byte).
 * RVALID - Whether the byte in DATA is valid.
 */
#define	ALTERA_JTAG_UART_DATA_DATA		0x000000ff
#define	ALTERA_JTAG_UART_DATA_RESERVED		0x00007f00
#define	ALTERA_JTAG_UART_DATA_RVALID		0x00008000
#define	ALTERA_JTAG_UART_DATA_RAVAIL		0xffff0000
#define	ALTERA_JTAG_UART_DATA_RAVAIL_SHIFT	16

/*-
 * Offset 1: 'control' register -- bits 31-16 (WSPACE), 15-11 (Reserved),
 * 10 (AC), 9 (WI), 8 (RI), 7..2 (Reserved), 1 (WE), 0 (RE).
 *
 * RE - Enable read interrupts.
 * WE - Enable write interrupts.
 * RI - Read interrupt pending.
 * WI - Write interrupt pending.
 * AC - Activity bit; set to '1' to clear to '0'.
 * WSPACE - Space available in the write FIFO.
 */
#define	ALTERA_JTAG_UART_CONTROL_RE		0x00000001
#define	ALTERA_JTAG_UART_CONTROL_WE		0x00000002
#define	ALTERA_JTAG_UART_CONTROL_RESERVED0	0x000000fc
#define	ALTERA_JTAG_UART_CONTROL_RI		0x00000100
#define	ALTERA_JTAG_UART_CONTROL_WI		0x00000200
#define	ALTERA_JTAG_UART_CONTROL_AC		0x00000400
#define	ALTERA_JTAG_UART_CONTROL_RESERVED1	0x0000f800
#define	ALTERA_JTAG_UART_CONTROL_WSPACE		0xffff0000
#define	ALTERA_JTAG_UART_CONTROL_WSPACE_SHIFT	16

/*
 * One-byte buffer as we can't check whether the UART is readable without
 * actually reading from it.
 */
static char	buffer_data;
static int	buffer_valid;

/*
 * Low-level read and write register routines; the Altera UART is little
 * endian, so we byte swap 32-bit reads and writes.
 */
static inline uint32_t
uart_data_read(void)
{

	return (mips_ioread_uint32le(mips_phys_to_uncached(CHERI_UART_BASE +
	    ALTERA_JTAG_UART_DATA_OFF)));
}

static inline void
uart_data_write(uint32_t v)
{

	mips_iowrite_uint32le(mips_phys_to_uncached(CHERI_UART_BASE +
	    ALTERA_JTAG_UART_DATA_OFF), v);
}

static inline uint32_t
uart_control_read(void)
{

	return (mips_ioread_uint32le(mips_phys_to_uncached(CHERI_UART_BASE +
	    ALTERA_JTAG_UART_CONTROL_OFF)));
}

static inline void
uart_control_write(uint32_t v)
{

	mips_iowrite_uint32le(mips_phys_to_uncached(CHERI_UART_BASE +
	    ALTERA_JTAG_UART_DATA_OFF), v);
}

static int
uart_readable(void)
{
	uint32_t v;

	if (buffer_valid)
		return (1);
	v = uart_data_read();
	if ((v & ALTERA_JTAG_UART_DATA_RVALID) != 0) {
		buffer_valid = 1;
		buffer_data = (v & ALTERA_JTAG_UART_DATA_DATA);
	}
	return (0);
}

int
keyhit(int seconds)
{
	register_t stoptime;

	stoptime = cp0_count_get() + seconds * 100000000;	/* 100 MHz. */
	do {
		if (uart_readable())
			return (1);
	} while (cp0_count_get() < stoptime);
	return (0);
}

int
beri_getc(void)
{

	while (!(uart_readable()));
	buffer_valid = 0;
	return (buffer_data);
}

void
beri_putc(int ch)
{

	uart_data_write(ch);
}
