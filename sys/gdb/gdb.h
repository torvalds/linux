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
 *
 * $FreeBSD$
 */

#ifndef _GDB_GDB_H_
#define	_GDB_GDB_H_

typedef int gdb_checkc_f(void);
typedef int gdb_getc_f(void);
typedef void gdb_init_f(void);
typedef int gdb_probe_f(void);
typedef void gdb_putc_f(int);
typedef void gdb_term_f(void);

struct gdb_dbgport {
	const char	*gdb_name;
	gdb_getc_f	*gdb_getc;
	gdb_init_f	*gdb_init;
	gdb_probe_f	*gdb_probe;
	gdb_putc_f	*gdb_putc;
	gdb_term_f	*gdb_term;
	int		gdb_active;
};

#define	GDB_DBGPORT(name, probe, init, term, getc, putc)		\
	static struct gdb_dbgport name##_gdb_dbgport = {		\
		.gdb_name = #name,					\
		.gdb_probe = probe,					\
		.gdb_init = init,					\
		.gdb_term = term,					\
		.gdb_getc = getc,					\
		.gdb_putc = putc,					\
	};								\
	DATA_SET(gdb_dbgport_set, name##_gdb_dbgport)

#endif /* !_GDB_GDB_H_ */
