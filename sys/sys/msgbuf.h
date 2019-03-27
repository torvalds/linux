/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 * $FreeBSD$
 */

#ifndef _SYS_MSGBUF_H_
#define	_SYS_MSGBUF_H_

#include <sys/lock.h>
#include <sys/mutex.h>

struct msgbuf {
	char	   *msg_ptr;		/* pointer to buffer */
#define	MSG_MAGIC	0x063062
	u_int	   msg_magic;
	u_int	   msg_size;		/* size of buffer area */
	u_int	   msg_wseq;		/* write sequence number */
	u_int	   msg_rseq;		/* read sequence number */
	u_int	   msg_cksum;		/* checksum of contents */
	u_int	   msg_seqmod;		/* range for sequence numbers */
	int	   msg_lastpri;		/* saved priority value */
	u_int      msg_flags;
#define MSGBUF_NEEDNL	0x01	/* set when newline needed */
	struct mtx msg_lock;		/* mutex to protect the buffer */
};

/* Normalise a sequence number or a difference between sequence numbers. */
#define	MSGBUF_SEQNORM(mbp, seq)	(((seq) + (mbp)->msg_seqmod) % \
    (mbp)->msg_seqmod)
#define	MSGBUF_SEQ_TO_POS(mbp, seq)	((seq) % (mbp)->msg_size)
/* Subtract sequence numbers.  Note that only positive values result. */
#define	MSGBUF_SEQSUB(mbp, seq1, seq2)	(MSGBUF_SEQNORM((mbp), (seq1) - (seq2)))

#ifdef _KERNEL
extern int	msgbufsize;
extern int	msgbuftrigger;
extern struct	msgbuf *msgbufp;
extern struct	mtx msgbuf_lock;

void	msgbufinit(void *ptr, int size);
void	msgbuf_addchar(struct msgbuf *mbp, int c);
void	msgbuf_addstr(struct msgbuf *mbp, int pri, const char *str, int filter_cr);
void	msgbuf_clear(struct msgbuf *mbp);
void	msgbuf_copy(struct msgbuf *src, struct msgbuf *dst);
int	msgbuf_getbytes(struct msgbuf *mbp, char *buf, int buflen);
int	msgbuf_getchar(struct msgbuf *mbp);
int	msgbuf_getcount(struct msgbuf *mbp);
void	msgbuf_init(struct msgbuf *mbp, void *ptr, int size);
int	msgbuf_peekbytes(struct msgbuf *mbp, char *buf, int buflen,
	    u_int *seqp);
void	msgbuf_reinit(struct msgbuf *mbp, void *ptr, int size);

#ifndef MSGBUF_SIZE
#define	MSGBUF_SIZE	(32768 * 3)
#endif
#endif /* KERNEL */

#endif /* !_SYS_MSGBUF_H_ */
