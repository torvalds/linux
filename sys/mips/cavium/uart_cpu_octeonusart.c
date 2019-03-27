/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 M. Warner Losh <imp@FreeBSD.org>
 * Copyright (c) 2006 Wojciech A. Koszek <wkoszek@FreeBSD.org>
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
 * $Id$
 */
#include "opt_uart.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/cons.h>

#include <machine/bus.h>

#include <dev/uart/uart.h>
#include <dev/uart/uart_cpu.h>

#include <mips/cavium/octeon_pcmap_regs.h>

#include <contrib/octeon-sdk/cvmx.h>

bus_space_tag_t uart_bus_space_io;
bus_space_tag_t uart_bus_space_mem;

/*
 * Specailized uart bus space.  We present a 1 apart byte oriented
 * bus to the outside world, but internally translate to/from the 8-apart
 * 64-bit word bus that's on the octeon.  We only support simple read/write
 * in this space.  Everything else is undefined.
 */
static uint8_t
ou_bs_r_1(void *t, bus_space_handle_t handle, bus_size_t offset)
{

	return (cvmx_read64_uint64(handle + offset));
}

static uint16_t
ou_bs_r_2(void *t, bus_space_handle_t handle, bus_size_t offset)
{

	return (cvmx_read64_uint64(handle + offset));
}

static uint32_t
ou_bs_r_4(void *t, bus_space_handle_t handle, bus_size_t offset)
{

	return (cvmx_read64_uint64(handle + offset));
}

static uint64_t
ou_bs_r_8(void *t, bus_space_handle_t handle, bus_size_t offset)
{

	return (cvmx_read64_uint64(handle + offset));
}

static void
ou_bs_w_1(void *t, bus_space_handle_t bsh, bus_size_t offset, uint8_t value)
{

	cvmx_write64_uint64(bsh + offset, value);
}

static void
ou_bs_w_2(void *t, bus_space_handle_t bsh, bus_size_t offset, uint16_t value)
{

	cvmx_write64_uint64(bsh + offset, value);
}

static void
ou_bs_w_4(void *t, bus_space_handle_t bsh, bus_size_t offset, uint32_t value)
{

	cvmx_write64_uint64(bsh + offset, value);
}

static void
ou_bs_w_8(void *t, bus_space_handle_t bsh, bus_size_t offset, uint64_t value)
{

	cvmx_write64_uint64(bsh + offset, value);
}

struct bus_space octeon_uart_tag = {
	.bs_map = generic_bs_map,
	.bs_unmap = generic_bs_unmap,
	.bs_subregion = generic_bs_subregion,
	.bs_barrier = generic_bs_barrier,
	.bs_r_1 = ou_bs_r_1,
	.bs_r_2 = ou_bs_r_2,
	.bs_r_4 = ou_bs_r_4,
	.bs_r_8 = ou_bs_r_8,
	.bs_w_1 = ou_bs_w_1,
	.bs_w_2 = ou_bs_w_2,
	.bs_w_4 = ou_bs_w_4,
	.bs_w_8 = ou_bs_w_8,
};

extern struct uart_class uart_oct16550_class;

int
uart_cpu_eqres(struct uart_bas *b1, struct uart_bas *b2)
{

	return ((b1->bsh == b2->bsh && b1->bst == b2->bst) ? 1 : 0);
}

int
uart_cpu_getdev(int devtype, struct uart_devinfo *di)
{
	struct uart_class *class = &uart_oct16550_class;

	/*
	 * These fields need to be setup corretly for uart_getenv to
	 * work in all cases.
	 */
	uart_bus_space_io = NULL;		/* No io map for this device */
	uart_bus_space_mem = &octeon_uart_tag;
	di->bas.bst = uart_bus_space_mem;

	/*
	 * If env specification for UART exists it takes precedence:
	 * hw.uart.console="mm:0xf1012000" or similar
	 */
	if (uart_getenv(devtype, di, class) == 0)
		return (0);

	/*
	 * Fallback to UART0 for console.
	 */
	di->ops = uart_getops(class);
	di->bas.chan = 0;
	/* XXX */
	if (bus_space_map(di->bas.bst, CVMX_MIO_UARTX_RBR(0),
	    uart_getrange(class), 0, &di->bas.bsh) != 0)
		return (ENXIO);
	di->bas.regshft = 3;
	di->bas.rclk = 0;
	di->baudrate = 115200;
	di->databits = 8;
	di->stopbits = 1;
	di->parity = UART_PARITY_NONE;

	return (0);
}
