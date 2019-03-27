/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (C) 2002-2004
 * 	Hidetoshi Shimokawa. All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *
 *	This product includes software developed by Hidetoshi Shimokawa.
 *
 * 4. Neither the name of the author nor the names of its contributors
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
 * $Id: dcons.h,v 1.15 2003/10/23 15:05:31 simokawa Exp $
 * $FreeBSD$
 */

#if defined(_KERNEL) || defined(_BOOT)
#define	V volatile
#else
#define	V
#endif

#define DCONS_NPORT	2
#define DCONS_CON	0
#define DCONS_GDB	1

struct dcons_buf {
#define DCONS_VERSION 2
	V u_int32_t version;
	V u_int32_t ooffset[DCONS_NPORT];
	V u_int32_t ioffset[DCONS_NPORT];
	V u_int32_t osize[DCONS_NPORT];
	V u_int32_t isize[DCONS_NPORT];
#define DCONS_MAGIC 0x64636f6e	/* "dcon" */
	V u_int32_t magic;
#define DCONS_GEN_SHIFT		(24)
#define DCONS_GEN_MASK		(0xff)
#define DCONS_POS_MASK	((1<< DCONS_GEN_SHIFT) - 1)
	V u_int32_t optr[DCONS_NPORT];
	V u_int32_t iptr[DCONS_NPORT];
	V char buf[0];
};

#define DCONS_CSR_VAL_VER	0x64636f /* "dco" */
#define DCONS_CSR_KEY_HI	0x3a
#define DCONS_CSR_KEY_LO	0x3b
#define DCONS_CSR_KEY_RESET_HI	0x3c
#define DCONS_CSR_KEY_RESET_LO	0x3d

#define	DCONS_HEADER_SIZE sizeof(struct dcons_buf)
#define DCONS_MAKE_PTR(x)	htonl(((x)->gen << DCONS_GEN_SHIFT) | (x)->pos)
#define	DCONS_NEXT_GEN(x)	(((x) + 1) & DCONS_GEN_MASK)

struct dcons_ch {
	u_int32_t size;
	u_int32_t gen;
	u_int32_t pos;
#if defined(_KERNEL) || defined(_BOOT)
	V u_int32_t *ptr;
	V char *buf;
#else
	off_t buf;
#endif
};

#define KEY_CTRLB	2	/* ^B */
#define KEY_CR		13	/* CR '\r' */
#define KEY_TILDE	126	/* ~ */
#define STATE0		0
#define STATE1		1
#define STATE2		2
#define STATE3		3

#if defined(_KERNEL) || defined(_BOOT)
struct dcons_softc {
        struct dcons_ch o, i;
        int brk_state;
#define DC_GDB  1
        int flags;
	void *tty;
};

int	dcons_checkc(struct dcons_softc *);
int	dcons_ischar(struct dcons_softc *);
void	dcons_putc(struct dcons_softc *, int);
int	dcons_load_buffer(struct dcons_buf *, int, struct dcons_softc *);
void	dcons_init(struct dcons_buf *, int, struct dcons_softc *);
#endif
