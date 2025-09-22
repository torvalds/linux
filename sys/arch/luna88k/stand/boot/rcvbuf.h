/*	$OpenBSD: rcvbuf.h,v 1.2 2023/01/10 17:10:57 miod Exp $	*/
/*	$NetBSD: rcvbuf.h,v 1.1 2013/01/05 17:44:24 tsutsui Exp $	*/

/*
 * Copyright (c) 1992 OMRON Corporation.
 *
 * This code is derived from software contributed to Berkeley by
 * OMRON Corporation.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *	@(#)rcvbuf.h	8.1 (Berkeley) 6/10/93
 */
/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * OMRON Corporation.
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
 *	@(#)rcvbuf.h	8.1 (Berkeley) 6/10/93
 */

/*
 * rcvbuf.h -- receive buffer control structure definition
 *	by A.Fujita, Dec-12-1992
 */

#define	RBUF_SIZE	128

struct rcvbuf {
	u_char	* volatile rb_push;
	u_char	* volatile rb_pop;
	u_char	 rb_buf[RBUF_SIZE+4];
};

#define RBUF_INIT(n)	rcvbuf[n].rb_pop = rcvbuf[n].rb_push = &rcvbuf[n].rb_buf[RBUF_SIZE]

#define PUSH_RBUF(n, c)							\
do {									\
	*(--rcvbuf[n].rb_push) = c ;					\
	if (rcvbuf[n].rb_push == rcvbuf[n].rb_buf)			\
		rcvbuf[n].rb_push = &rcvbuf[n].rb_buf[RBUF_SIZE];	\
} while (/* CONSTCOND */0)

#define POP_RBUF(n, c)							\
do {									\
	c= *(--rcvbuf[n].rb_pop);					\
	if (rcvbuf[n].rb_pop == rcvbuf[n].rb_buf)			\
		rcvbuf[n].rb_pop = &rcvbuf[n].rb_buf[RBUF_SIZE];	\
} while (0)

#define PEEK_RBUF(n, c)							\
do {									\
	c= *(rcvbuf[n].rb_pop - 1);					\
} while (0)

#define RBUF_EMPTY(n)	(rcvbuf[n].rb_push == rcvbuf[n].rb_pop ? 1: 0)

extern	struct rcvbuf	rcvbuf[];
