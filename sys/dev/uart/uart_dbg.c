/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Marcel Moolenaar
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <machine/bus.h>

#include <gdb/gdb.h>

#include <dev/uart/uart.h>
#include <dev/uart/uart_cpu.h>

static gdb_probe_f uart_dbg_probe;
static gdb_init_f uart_dbg_init;
static gdb_term_f uart_dbg_term;
static gdb_getc_f uart_dbg_getc;
static gdb_putc_f uart_dbg_putc;

GDB_DBGPORT(uart, uart_dbg_probe, uart_dbg_init, uart_dbg_term,
    uart_dbg_getc, uart_dbg_putc);

static struct uart_devinfo uart_dbgport;

static int
uart_dbg_probe(void)
{

	if (uart_cpu_getdev(UART_DEV_DBGPORT, &uart_dbgport))
		return (-1);

	if (uart_probe(&uart_dbgport))
		return (-1);

	return (0);
}

static void
uart_dbg_init(void)
{

	uart_dbgport.type = UART_DEV_DBGPORT;
	uart_add_sysdev(&uart_dbgport);
	uart_init(&uart_dbgport);
}

static void
uart_dbg_term(void)
{

	uart_term(&uart_dbgport);
}

static void
uart_dbg_putc(int c)
{

	uart_putc(&uart_dbgport, c);
}

static int
uart_dbg_getc(void)
{

	return (uart_poll(&uart_dbgport));
}
