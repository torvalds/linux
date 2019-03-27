/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1990, 1993
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
 *	@(#)socketvar.h	8.3 (Berkeley) 2/19/95
 *
 * $FreeBSD$
 */
#ifndef _SYS_SOCKBUF_H_
#define _SYS_SOCKBUF_H_

/*
 * Constants for sb_flags field of struct sockbuf/xsockbuf.
 */
#define	SB_WAIT		0x04		/* someone is waiting for data/space */
#define	SB_SEL		0x08		/* someone is selecting */
#define	SB_ASYNC	0x10		/* ASYNC I/O, need signals */
#define	SB_UPCALL	0x20		/* someone wants an upcall */
#define	SB_NOINTR	0x40		/* operations not interruptible */
#define	SB_AIO		0x80		/* AIO operations queued */
#define	SB_KNOTE	0x100		/* kernel note attached */
#define	SB_NOCOALESCE	0x200		/* don't coalesce new data into existing mbufs */
#define	SB_IN_TOE	0x400		/* socket buffer is in the middle of an operation */
#define	SB_AUTOSIZE	0x800		/* automatically size socket buffer */
#define	SB_STOP		0x1000		/* backpressure indicator */
#define	SB_AIO_RUNNING	0x2000		/* AIO operation running */

#define	SBS_CANTSENDMORE	0x0010	/* can't send more data to peer */
#define	SBS_CANTRCVMORE		0x0020	/* can't receive more data from peer */
#define	SBS_RCVATMARK		0x0040	/* at mark on input */

#if defined(_KERNEL) || defined(_WANT_SOCKET)
#include <sys/_lock.h>
#include <sys/_mutex.h>
#include <sys/_sx.h>
#include <sys/_task.h>

#define	SB_MAX		(2*1024*1024)	/* default for max chars in sockbuf */

struct mbuf;
struct sockaddr;
struct socket;
struct thread;
struct selinfo;

/*
 * Variables for socket buffering.
 *
 * Locking key to struct sockbuf:
 * (a) locked by SOCKBUF_LOCK().
 */
struct	sockbuf {
	struct	mtx sb_mtx;		/* sockbuf lock */
	struct	sx sb_sx;		/* prevent I/O interlacing */
	struct	selinfo *sb_sel;	/* process selecting read/write */
	short	sb_state;	/* (a) socket state on sockbuf */
#define	sb_startzero	sb_mb
	struct	mbuf *sb_mb;	/* (a) the mbuf chain */
	struct	mbuf *sb_mbtail; /* (a) the last mbuf in the chain */
	struct	mbuf *sb_lastrecord;	/* (a) first mbuf of last
					 * record in socket buffer */
	struct	mbuf *sb_sndptr; /* (a) pointer into mbuf chain */
	struct	mbuf *sb_fnrdy;	/* (a) pointer to first not ready buffer */
	u_int	sb_sndptroff;	/* (a) byte offset of ptr into chain */
	u_int	sb_acc;		/* (a) available chars in buffer */
	u_int	sb_ccc;		/* (a) claimed chars in buffer */
	u_int	sb_hiwat;	/* (a) max actual char count */
	u_int	sb_mbcnt;	/* (a) chars of mbufs used */
	u_int   sb_mcnt;        /* (a) number of mbufs in buffer */
	u_int   sb_ccnt;        /* (a) number of clusters in buffer */
	u_int	sb_mbmax;	/* (a) max chars of mbufs to use */
	u_int	sb_ctl;		/* (a) non-data chars in buffer */
	int	sb_lowat;	/* (a) low water mark */
	sbintime_t	sb_timeo;	/* (a) timeout for read/write */
	short	sb_flags;	/* (a) flags, see below */
	int	(*sb_upcall)(struct socket *, void *, int); /* (a) */
	void	*sb_upcallarg;	/* (a) */
	TAILQ_HEAD(, kaiocb) sb_aiojobq; /* (a) pending AIO ops */
	struct	task sb_aiotask; /* AIO task */
};

#endif	/* defined(_KERNEL) || defined(_WANT_SOCKET) */
#ifdef _KERNEL

/*
 * Per-socket buffer mutex used to protect most fields in the socket
 * buffer.
 */
#define	SOCKBUF_MTX(_sb)		(&(_sb)->sb_mtx)
#define	SOCKBUF_LOCK_INIT(_sb, _name) \
	mtx_init(SOCKBUF_MTX(_sb), _name, NULL, MTX_DEF)
#define	SOCKBUF_LOCK_DESTROY(_sb)	mtx_destroy(SOCKBUF_MTX(_sb))
#define	SOCKBUF_LOCK(_sb)		mtx_lock(SOCKBUF_MTX(_sb))
#define	SOCKBUF_OWNED(_sb)		mtx_owned(SOCKBUF_MTX(_sb))
#define	SOCKBUF_UNLOCK(_sb)		mtx_unlock(SOCKBUF_MTX(_sb))
#define	SOCKBUF_LOCK_ASSERT(_sb)	mtx_assert(SOCKBUF_MTX(_sb), MA_OWNED)
#define	SOCKBUF_UNLOCK_ASSERT(_sb)	mtx_assert(SOCKBUF_MTX(_sb), MA_NOTOWNED)

/*
 * Socket buffer private mbuf(9) flags.
 */
#define	M_NOTREADY	M_PROTO1	/* m_data not populated yet */
#define	M_BLOCKED	M_PROTO2	/* M_NOTREADY in front of m */
#define	M_NOTAVAIL	(M_NOTREADY | M_BLOCKED)

void	sbappend(struct sockbuf *sb, struct mbuf *m, int flags);
void	sbappend_locked(struct sockbuf *sb, struct mbuf *m, int flags);
void	sbappendstream(struct sockbuf *sb, struct mbuf *m, int flags);
void	sbappendstream_locked(struct sockbuf *sb, struct mbuf *m, int flags);
int	sbappendaddr(struct sockbuf *sb, const struct sockaddr *asa,
	    struct mbuf *m0, struct mbuf *control);
int	sbappendaddr_locked(struct sockbuf *sb, const struct sockaddr *asa,
	    struct mbuf *m0, struct mbuf *control);
int	sbappendaddr_nospacecheck_locked(struct sockbuf *sb,
	    const struct sockaddr *asa, struct mbuf *m0, struct mbuf *control);
void	sbappendcontrol(struct sockbuf *sb, struct mbuf *m0,
	    struct mbuf *control);
void	sbappendcontrol_locked(struct sockbuf *sb, struct mbuf *m0,
	    struct mbuf *control);
void	sbappendrecord(struct sockbuf *sb, struct mbuf *m0);
void	sbappendrecord_locked(struct sockbuf *sb, struct mbuf *m0);
void	sbcompress(struct sockbuf *sb, struct mbuf *m, struct mbuf *n);
struct mbuf *
	sbcreatecontrol(caddr_t p, int size, int type, int level);
void	sbdestroy(struct sockbuf *sb, struct socket *so);
void	sbdrop(struct sockbuf *sb, int len);
void	sbdrop_locked(struct sockbuf *sb, int len);
struct mbuf *
	sbcut_locked(struct sockbuf *sb, int len);
void	sbdroprecord(struct sockbuf *sb);
void	sbdroprecord_locked(struct sockbuf *sb);
void	sbflush(struct sockbuf *sb);
void	sbflush_locked(struct sockbuf *sb);
void	sbrelease(struct sockbuf *sb, struct socket *so);
void	sbrelease_internal(struct sockbuf *sb, struct socket *so);
void	sbrelease_locked(struct sockbuf *sb, struct socket *so);
int	sbsetopt(struct socket *so, int cmd, u_long cc);
int	sbreserve_locked(struct sockbuf *sb, u_long cc, struct socket *so,
	    struct thread *td);
void	sbsndptr_adv(struct sockbuf *sb, struct mbuf *mb, u_int len);
struct mbuf *
	sbsndptr_noadv(struct sockbuf *sb, u_int off, u_int *moff);
struct mbuf *
	sbsndmbuf(struct sockbuf *sb, u_int off, u_int *moff);
int	sbwait(struct sockbuf *sb);
int	sblock(struct sockbuf *sb, int flags);
void	sbunlock(struct sockbuf *sb);
void	sballoc(struct sockbuf *, struct mbuf *);
void	sbfree(struct sockbuf *, struct mbuf *);
int	sbready(struct sockbuf *, struct mbuf *, int);

/*
 * Return how much data is available to be taken out of socket
 * buffer right now.
 */
static inline u_int
sbavail(struct sockbuf *sb)
{

#if 0
	SOCKBUF_LOCK_ASSERT(sb);
#endif
	return (sb->sb_acc);
}

/*
 * Return how much data sits there in the socket buffer
 * It might be that some data is not yet ready to be read.
 */
static inline u_int
sbused(struct sockbuf *sb)
{

#if 0
	SOCKBUF_LOCK_ASSERT(sb);
#endif
	return (sb->sb_ccc);
}

/*
 * How much space is there in a socket buffer (so->so_snd or so->so_rcv)?
 * This is problematical if the fields are unsigned, as the space might
 * still be negative (ccc > hiwat or mbcnt > mbmax).
 */
static inline long
sbspace(struct sockbuf *sb)
{
	int bleft, mleft;		/* size should match sockbuf fields */

#if 0
	SOCKBUF_LOCK_ASSERT(sb);
#endif

	if (sb->sb_flags & SB_STOP)
		return(0);

	bleft = sb->sb_hiwat - sb->sb_ccc;
	mleft = sb->sb_mbmax - sb->sb_mbcnt;

	return ((bleft < mleft) ? bleft : mleft);
}

#define SB_EMPTY_FIXUP(sb) do {						\
	if ((sb)->sb_mb == NULL) {					\
		(sb)->sb_mbtail = NULL;					\
		(sb)->sb_lastrecord = NULL;				\
	}								\
} while (/*CONSTCOND*/0)

#ifdef SOCKBUF_DEBUG
void	sblastrecordchk(struct sockbuf *, const char *, int);
void	sblastmbufchk(struct sockbuf *, const char *, int);
void	sbcheck(struct sockbuf *, const char *, int);
#define	SBLASTRECORDCHK(sb)	sblastrecordchk((sb), __FILE__, __LINE__)
#define	SBLASTMBUFCHK(sb)	sblastmbufchk((sb), __FILE__, __LINE__)
#define	SBCHECK(sb)		sbcheck((sb), __FILE__, __LINE__)
#else
#define	SBLASTRECORDCHK(sb)	do {} while (0)
#define	SBLASTMBUFCHK(sb)	do {} while (0)
#define	SBCHECK(sb)		do {} while (0)
#endif /* SOCKBUF_DEBUG */

#endif /* _KERNEL */

#endif /* _SYS_SOCKBUF_H_ */
