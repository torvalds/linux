/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)stab.h	8.1 (Berkeley) 6/2/93
 *
 * $FreeBSD$
 */

#ifndef _STAB_H_
#define _STAB_H_

/*
 * The following are symbols used by various debuggers and by the Pascal
 * compiler.  Each of them must have one (or more) of the bits defined by
 * the N_STAB mask set.
 */

#define	N_GSYM		0x20	/* global symbol */
#define	N_FNAME		0x22	/* F77 function name */
#define	N_FUN		0x24	/* procedure name */
#define	N_STSYM		0x26	/* data segment variable */
#define	N_LCSYM		0x28	/* bss segment variable */
#define	N_MAIN		0x2a	/* main function name */
#define	N_PC		0x30	/* global Pascal symbol */
#define	N_RSYM		0x40	/* register variable */
#define	N_SLINE		0x44	/* text segment line number */
#define	N_DSLINE	0x46	/* data segment line number */
#define	N_BSLINE	0x48	/* bss segment line number */
#define	N_SSYM		0x60	/* structure/union element */
#define	N_SO		0x64	/* main source file name */
#define	N_LSYM		0x80	/* stack variable */
#define	N_BINCL		0x82	/* include file beginning */
#define	N_SOL		0x84	/* included source file name */
#define	N_PSYM		0xa0	/* parameter variable */
#define	N_EINCL		0xa2	/* include file end */
#define	N_ENTRY		0xa4	/* alternate entry point */
#define	N_LBRAC		0xc0	/* left bracket */
#define	N_EXCL		0xc2	/* deleted include file */
#define	N_RBRAC		0xe0	/* right bracket */
#define	N_BCOMM		0xe2	/* begin common */
#define	N_ECOMM		0xe4	/* end common */
#define	N_ECOML		0xe8	/* end common (local name) */
#define	N_LENG		0xfe	/* length of preceding entry */

#endif
