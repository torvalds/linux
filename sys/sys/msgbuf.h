/*	$OpenBSD: msgbuf.h,v 1.13 2020/10/25 10:55:42 visa Exp $	*/
/*	$NetBSD: msgbuf.h,v 1.8 1995/03/26 20:24:27 jtc Exp $	*/

/*
 * Copyright (c) 1981, 1984, 1993
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
 *	@(#)msgbuf.h	8.1 (Berkeley) 6/2/93
 */

/*
 * Locking:
 *	I	immutable after creation
 *	L	log_mtx
 *	Lw	log_mtx for writing
 */
struct	msgbuf {
#define	MSG_MAGIC	0x063061
	long	msg_magic;		/* [I] buffer magic value */
	long	msg_bufx;		/* [L] write pointer */
	long	msg_bufr;		/* [L] read pointer */
	long	msg_bufs;		/* [I] real msg_bufc size (bytes) */
	long	msg_bufd;		/* [L] number of dropped bytes */
	char	msg_bufc[1];		/* [Lw] buffer */
};
#ifdef _KERNEL
#define CONSBUFSIZE	(16 * 1024)	/* console message buffer size */
extern struct msgbuf *msgbufp;
extern struct msgbuf *consbufp;

void	initmsgbuf(caddr_t buf, size_t bufsize);
void	initconsbuf(void);
void	msgbuf_putchar(struct msgbuf *, const char c);
#endif
