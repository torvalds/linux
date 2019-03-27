/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)nlist.h	8.2 (Berkeley) 1/21/94
 *
 * $FreeBSD$
 */

#ifndef _SYS_NLIST_AOUT_H_
#define	_SYS_NLIST_AOUT_H_

/*
 * Symbol table entries in a.out files.
 */

/*
 * Layout of each symbol.  The "#ifdef _AOUT_INCLUDE_" is so that
 * programs including nlist.h can initialize nlist structures
 * statically.
 */
struct nlist {
#ifdef _AOUT_INCLUDE_
	union {
		const char *n_name; /* symbol name (in memory) */
		long n_strx;	/* file string table offset (on disk) */
	} n_un;
#else
	const char *n_name;	/* symbol name (in memory) */
#endif
	unsigned char n_type;	/* type defines */
	char n_other;		/* ".type" and binding information */
	short n_desc;		/* used by stab entries */
	unsigned long n_value;	/* address/value of the symbol */
};

#define	n_hash	n_desc		/* used internally by ld(1); XXX */

/*
 * Defines for n_type.
 */
#define	N_UNDF	0x00		/* undefined */
#define	N_ABS	0x02		/* absolute address */
#define	N_TEXT	0x04		/* text segment */
#define	N_DATA	0x06		/* data segment */
#define	N_BSS	0x08		/* bss segment */
#define	N_INDR	0x0a		/* alias definition */
#define	N_SIZE	0x0c		/* pseudo type, defines a symbol's size */
#define	N_COMM	0x12		/* common reference */
/* GNU extensions */
#define N_SETA	0x14		/* Absolute set element symbol */
#define N_SETT  0x16		/* Text set element symbol */
#define N_SETD  0x18		/* Data set element symbol */
#define N_SETB  0x1a		/* Bss set element symbol */
#define N_SETV  0x1c		/* Pointer to set vector in data area. */
/* end GNU extensions */
#define	N_FN	0x1e		/* file name (N_EXT on) */
#define	N_WARN	0x1e		/* warning message (N_EXT off) */

#define	N_EXT	0x01		/* external (global) bit, OR'ed in */
#define	N_TYPE	0x1e		/* mask for all the type bits */
#define	N_STAB	0xe0		/* mask for debugger symbols -- stab(5) */

/*
 * Defines for n_other.  It contains the ".type" (AUX) field in the least
 * significant 4 bits, and the binding (for weak symbols) in the most
 * significant 4 bits.
 */
#define N_AUX(p)	((p)->n_other & 0xf)
#define N_BIND(p)	(((unsigned int)(p)->n_other >> 4) & 0xf)
#define N_OTHER(r, v)	(((unsigned int)(r) << 4) | ((v) & 0xf))

#define AUX_OBJECT	1	/* data object */
#define AUX_FUNC	2	/* function */

/*#define BIND_LOCAL	0	not used */
/*#define BIND_GLOBAL	1	not used */
#define BIND_WEAK	2	/* weak binding */

#define	N_FORMAT	"%08x"	/* namelist value format; XXX */

#endif /* !_SYS_NLIST_AOUT_H_ */
