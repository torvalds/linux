/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _DEV_UART_H_
#define _DEV_UART_H_

/*
 * Bus access structure. This structure holds the minimum information needed
 * to access the UART. The rclk field, although not important to actually
 * access the UART, is important for baudrate programming, delay loops and
 * other timing related computations.
 */
struct uart_bas {
	bus_space_tag_t bst;
	bus_space_handle_t bsh;
	u_int	chan;
	u_int	rclk;
	u_int	regshft;
	u_int	regiowidth;
	u_int	busy_detect;
};

#define	uart_regofs(bas, reg)		((reg) << (bas)->regshft)
#define	uart_regiowidth(bas)		((bas)->regiowidth)

static inline uint32_t
uart_getreg(struct uart_bas *bas, int reg)
{
	uint32_t ret;

	switch (uart_regiowidth(bas)) {
	case 4:
		ret = bus_space_read_4(bas->bst, bas->bsh, uart_regofs(bas, reg));
		break;
	case 2:
		ret = bus_space_read_2(bas->bst, bas->bsh, uart_regofs(bas, reg));
		break;
	default:
		ret = bus_space_read_1(bas->bst, bas->bsh, uart_regofs(bas, reg));
		break;
	}

	return (ret);
}

static inline void
uart_setreg(struct uart_bas *bas, int reg, int value)
{

	switch (uart_regiowidth(bas)) {
	case 4:
		bus_space_write_4(bas->bst, bas->bsh, uart_regofs(bas, reg), value);
		break;
	case 2:
		bus_space_write_2(bas->bst, bas->bsh, uart_regofs(bas, reg), value);
		break;
	default:
		bus_space_write_1(bas->bst, bas->bsh, uart_regofs(bas, reg), value);
		break;
	}
}

/*
 * XXX we don't know the length of the bus space address range in use by
 * the UART. Since barriers don't use the length field currently, we put
 * a zero there for now.
 */
#define uart_barrier(bas)		\
	bus_space_barrier((bas)->bst, (bas)->bsh, 0, 0,		\
	    BUS_SPACE_BARRIER_READ|BUS_SPACE_BARRIER_WRITE)

/*
 * UART device classes.
 */
struct uart_class;

extern struct uart_class uart_ns8250_class __attribute__((weak));
extern struct uart_class uart_quicc_class __attribute__((weak));
extern struct uart_class uart_s3c2410_class __attribute__((weak));
extern struct uart_class uart_sab82532_class __attribute__((weak));
extern struct uart_class uart_sbbc_class __attribute__((weak));
extern struct uart_class uart_z8530_class __attribute__((weak));

/*
 * Device flags.
 */
#define	UART_FLAGS_CONSOLE(f)		((f) & 0x10)
#define	UART_FLAGS_DBGPORT(f)		((f) & 0x80)
#define	UART_FLAGS_FCR_RX_LOW(f)	((f) & 0x100)
#define	UART_FLAGS_FCR_RX_MEDL(f)	((f) & 0x200)
#define	UART_FLAGS_FCR_RX_MEDH(f)	((f) & 0x400)
#define	UART_FLAGS_FCR_RX_HIGH(f)	((f) & 0x800)

/*
 * Data parity values (magical numbers related to ns8250).
 */
#define	UART_PARITY_NONE		0
#define	UART_PARITY_ODD			1
#define	UART_PARITY_EVEN		3
#define	UART_PARITY_MARK		5
#define	UART_PARITY_SPACE		7

#endif /* _DEV_UART_H_ */
