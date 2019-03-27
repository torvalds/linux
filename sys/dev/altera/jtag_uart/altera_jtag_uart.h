/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011-2012 Robert N. M. Watson
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

#ifndef _DEV_ALTERA_JTAG_UART_H_
#define	_DEV_ALTERA_JTAG_UART_H_

struct altera_jtag_uart_softc {
	device_t		 ajus_dev;
	int			 ajus_unit;

	/*
	 * Hardware resources.
	 */
	struct resource		*ajus_irq_res;
	int			 ajus_irq_rid;
	void			*ajus_irq_cookie;
	struct resource		*ajus_mem_res;
	int			 ajus_mem_rid;

	/*
	 * TTY resources.
	 */
	struct tty		*ajus_ttyp;
	int			 ajus_alt_break_state;

	/*
	 * Driver resources.
	 */
	u_int			 ajus_flags;
	struct mtx		*ajus_lockp;
	struct mtx		 ajus_lock;
	struct callout		 ajus_io_callout;
	struct callout		 ajus_ac_callout;

	/*
	 * One-character buffer required because it's not possible to peek at
	 * the input FIFO without reading it.
	 */
	int			 ajus_buffer_valid;
	int			*ajus_buffer_validp;
	uint8_t			 ajus_buffer_data;
	uint8_t			*ajus_buffer_datap;
	int			 ajus_jtag_present;
	int			*ajus_jtag_presentp;
	u_int			 ajus_jtag_missed;
	u_int			*ajus_jtag_missedp;
};

#define	AJU_TTYNAME	"ttyj"

/*
 * Flag values for ajus_flags.
 */
#define	ALTERA_JTAG_UART_FLAG_CONSOLE	0x00000001	/* Is console. */

/*
 * Because tty-level use of the I/O ports completes with low-level console
 * use, spinlocks must be employed here.
 */
#define	AJU_CONSOLE_LOCK_INIT() do {					\
	mtx_init(&aju_cons_lock, "aju_cons_lock", NULL, MTX_SPIN);	\
} while (0)

#define	AJU_CONSOLE_LOCK() do {						\
	if (!kdb_active)						\
		mtx_lock_spin(&aju_cons_lock);				\
} while (0)

#define	AJU_CONSOLE_LOCK_ASSERT() {					\
	if (!kdb_active)						\
		mtx_assert(&aju_cons_lock, MA_OWNED);			\
} while (0)

#define	AJU_CONSOLE_UNLOCK() do {					\
	if (!kdb_active)						\
		mtx_unlock_spin(&aju_cons_lock);			\
} while (0)

#define	AJU_LOCK_INIT(sc) do {						\
	mtx_init(&(sc)->ajus_lock, "aju_lock", NULL, MTX_SPIN);		\
} while (0)

#define	AJU_LOCK_DESTROY(sc) do {					\
	mtx_destroy(&(sc)->ajus_lock);					\
} while (0)

#define	AJU_LOCK(sc) do {						\
	mtx_lock_spin((sc)->ajus_lockp);				\
} while (0)

#define	AJU_LOCK_ASSERT(sc) do {					\
	mtx_assert((sc)->ajus_lockp, MA_OWNED);				\
} while (0)

#define	AJU_UNLOCK(sc) do {						\
	mtx_unlock_spin((sc)->ajus_lockp);				\
} while (0)

/*
 * When a TTY-level Altera JTAG UART instance is also the low-level console,
 * the TTY layer borrows the console-layer lock and buffer rather than using
 * its own.
 */
extern struct mtx	aju_cons_lock;
extern char  		aju_cons_buffer_data;
extern int		aju_cons_buffer_valid;
extern int		aju_cons_jtag_present;
extern u_int		aju_cons_jtag_missed;

/*
 * Base physical address of the JTAG UART in BERI.
 */
#define	BERI_UART_BASE		0x7f000000	/* JTAG UART */

/*-
 * Routines for interacting with the BERI console JTAG UART.  Programming
 * details from the June 2011 "Embedded Peripherals User Guide" by Altera
 * Corporation, tables 6-2 (JTAG UART Core Register Map), 6-3 (Data Register
 * Bits), and 6-4 (Control Register Bits).
 *
 * Offsets of data and control registers relative to the base.  Altera
 * conventions are maintained in BERI.
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
 * Driver attachment functions for Nexus.
 */
int	altera_jtag_uart_attach(struct altera_jtag_uart_softc *sc);
void	altera_jtag_uart_detach(struct altera_jtag_uart_softc *sc);

extern devclass_t	altera_jtag_uart_devclass;

#endif /* _DEV_ALTERA_JTAG_UART_H_ */
