/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 NetApp, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/bus.h>

#include <gdb/gdb.h>

#include <machine/cpufunc.h>

static gdb_probe_f bvm_dbg_probe;
static gdb_init_f bvm_dbg_init;
static gdb_term_f bvm_dbg_term;
static gdb_getc_f bvm_dbg_getc;
static gdb_putc_f bvm_dbg_putc;

GDB_DBGPORT(bvm, bvm_dbg_probe, bvm_dbg_init, bvm_dbg_term,
    bvm_dbg_getc, bvm_dbg_putc);

#define	BVM_DBG_PORT	0x224
static int bvm_dbg_port = BVM_DBG_PORT;

#define BVM_DBG_SIG	('B' << 8 | 'V')

static int
bvm_dbg_probe(void)
{
	int disabled, port;

	disabled = 0;
	resource_int_value("bvmdbg", 0, "disabled", &disabled);

	if (!disabled) {
		if (resource_int_value("bvmdbg", 0, "port", &port) == 0)
			bvm_dbg_port = port;

		if (inw(bvm_dbg_port) == BVM_DBG_SIG) {
			/*
			 * Return a higher priority than 0 to override other
			 * gdb dbgport providers that may be present (e.g. uart)
			 */
			return (1);
		}
	}

	return (-1);
}

static void
bvm_dbg_init(void)
{
}

static void
bvm_dbg_term(void)
{
}

static void
bvm_dbg_putc(int c)
{

	outl(bvm_dbg_port, c);
}

static int
bvm_dbg_getc(void)
{

	return (inl(bvm_dbg_port));
}
