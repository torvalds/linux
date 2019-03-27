/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Marcel Moolenaar
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

#ifndef _MACHINE_GDB_MACHDEP_H_
#define	_MACHINE_GDB_MACHDEP_H_

#ifdef BOOKE
#define	PPC_GDB_NREGS0	1
#define	PPC_GDB_NREGS4	(70 + 1)
#define	PPC_GDB_NREGS8	(1 + 32)
#define	PPC_GDB_NREGS16	0
#else
#define	PPC_GDB_NREGS0	0
#define	PPC_GDB_NREGS4	(32 + 7 + 2)
#define	PPC_GDB_NREGS8	32
#define	PPC_GDB_NREGS16	32
#endif

#define GDB_NREGS	(PPC_GDB_NREGS0 + PPC_GDB_NREGS4 + \
			 PPC_GDB_NREGS8 + PPC_GDB_NREGS16)
#define	GDB_REG_PC	64

#define	GDB_BUFSZ	(PPC_GDB_NREGS4 * 8 +	\
			 PPC_GDB_NREGS8 * 16 +	\
			 PPC_GDB_NREGS16 * 32)

static __inline size_t
gdb_cpu_regsz(int regnum)
{

#ifdef BOOKE
	if (regnum == 70)
		return (0);
	if (regnum == 71 || regnum >= 73)
		return (8);
#else
	if (regnum >= 32 && regnum <= 63)
		return (8);
	if (regnum >= 71 && regnum <= 102)
		return (16);
#endif
	return (4);
}

static __inline int
gdb_cpu_query(void)
{

	return (0);
}

static __inline void *
gdb_begin_write(void)
{

	return (NULL);
}

static __inline void
gdb_end_write(void *arg __unused)
{

}

void *gdb_cpu_getreg(int, size_t *);
void gdb_cpu_setreg(int, void *);
int gdb_cpu_signal(int, int);

#endif /* !_MACHINE_GDB_MACHDEP_H_ */
