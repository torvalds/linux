/*	$OpenBSD: socketvar.h,v 1.159 2025/07/25 08:58:44 mvs Exp $	*/
/*	$NetBSD: socketvar.h,v 1.18 1996/02/09 18:25:38 christos Exp $	*/

/*-
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
 *	@(#)socketvar.h	8.1 (Berkeley) 6/2/93
 */

#ifndef _SYS_SOCKETVAR_H_
#define _SYS_SOCKETVAR_H_

#include <sys/event.h>
#include <sys/queue.h>
#include <sys/sigio.h>				/* for struct sigio_ref */
#include <sys/task.h>
#include <sys/timeout.h>
#include <sys/mutex.h>
#include <sys/rwlock.h>
#include <sys/refcnt.h>

#ifndef	_SOCKLEN_T_DEFINED_
#define	_SOCKLEN_T_DEFINED_
typedef	__socklen_t	socklen_t;	/* length type for network syscalls */
#endif

TAILQ_HEAD(soqhead, socket);

/*
 * Locks used to protect global data and struct members:
 *	I	immutable after creation
 *	a	atomic
 *	mr	sb_mxt of so_rcv buffer
 *	ms	sb_mtx of so_snd buffer
 *	m	sb_mtx
 *	br	sblock() of so_rcv buffer
 *	bs	sblock() od so_snd buffer
 *	s	solock()
 */

/*
 * Variables for socket splicing, allocated only when needed.
 */
struct sosplice {
	struct	socket *ssp_socket;	/* [mr ms] send data to drain socket */
	struct	socket *ssp_soback;	/* [ms ms] back ref to source socket */
	off_t	ssp_len;		/* [mr] number of bytes spliced */
	off_t	ssp_max;		/* [I] maximum number of bytes */
	struct	timeval ssp_idletv;	/* [I] idle timeout */
	struct	timeout ssp_idleto;
	struct	task ssp_task;		/* task for somove */
};

/*
 * Variables for socket buffering.
 */
struct sockbuf {
	struct rwlock sb_lock;
	struct mutex  sb_mtx;
/* The following fields are all zeroed on flush. */
#define	sb_startzero	sb_cc
	u_long	sb_cc;			/* [m] actual chars in buffer */
	u_long	sb_datacc;		/* [m] data only chars in buffer */
	u_long	sb_hiwat;		/* [m] max actual char count */
	u_long  sb_wat;			/* [m] default watermark */
	u_long	sb_mbcnt;		/* [m] chars of mbufs used */
	u_long	sb_mbmax;		/* [m] max chars of mbufs to use */
	long	sb_lowat;		/* [m] low water mark */
	struct mbuf *sb_mb;		/* [m] the mbuf chain */
	struct mbuf *sb_mbtail;		/* [m] the last mbuf in the chain */
	struct mbuf *sb_lastrecord;	/* [m] first mbuf of last record in
					    socket buffer */
	short	sb_flags;		/* [m] flags, see below */
/* End area that is zeroed on flush. */
#define	sb_endzero	sb_flags
	short	sb_state;		/* [m] socket state on sockbuf */
	uint64_t sb_timeo_nsecs;	/* [m] timeout for read/write */
	struct klist sb_klist;		/* [m] list of knotes */
};

#define SB_MAX		(2*1024*1024)	/* default for max chars in sockbuf */
#define SB_WAIT		0x0001		/* someone is waiting for data/space */
#define SB_ASYNC	0x0002		/* ASYNC I/O, need signals */
#define SB_SPLICE	0x0004		/* buffer is splice source or drain */
#define SB_NOINTR	0x0008		/* operations not interruptible */

/*
 * Kernel structure per socket.
 * Contains send and receive buffer queues,
 * handle on protocol and pointer to protocol
 * private data and error information.
 */
struct socket {
	const struct protosw *so_proto;	/* [I] protocol handle */
	struct rwlock so_lock;		/* this socket lock */
	struct refcnt so_refcnt;	/* references to this socket */
	void	*so_pcb;		/* [s] protocol control block */
	u_int	so_state;		/* [s] internal state flags SS_*,
					    see below */
	short	so_type;		/* [I] generic type, see socket.h */
	short	so_options;		/* [s] from socket call, see
					    socket.h */
	short	so_linger;		/* [s] time to linger while closing */
/*
 * Variables for connection queueing.
 * Socket where accepts occur is so_head in all subsidiary sockets.
 * If so_head is 0, socket is not related to an accept.
 * For head socket so_q0 queues partially completed connections,
 * while so_q is a queue of connections ready to be accepted.
 * If a connection is aborted and it has so_head set, then
 * it has to be pulled out of either so_q0 or so_q.
 * We allow connections to queue up based on current queue lengths
 * and limit on number of queued connections for this socket.
 *
 * Connections queue relies on both socket locks of listening and
 * unaccepted sockets. Socket lock of listening socket should be
 * always taken first.
 */
	struct	socket	*so_head;	/* [s] back pointer to accept socket */
	struct	soqhead	*so_onq;	/* [s] queue (q or q0) that we're on */
	struct	soqhead	so_q0;		/* [s] queue of partial connections */
	struct	soqhead	so_q;		/* [s] queue of incoming connections */
	struct	sigio_ref so_sigio;	/* async I/O registration */
	TAILQ_ENTRY(socket) so_qe;	/* [s] our queue entry (q or q0) */
	short	so_q0len;		/* [s] partials on so_q0 */
	short	so_qlen;		/* [s] number of connections on so_q */
	short	so_qlimit;		/* [s] max number queued connections */
	short	so_timeo;		/* [s] connection timeout */
	u_long	so_oobmark;		/* [mr] chars to oob mark */
	u_int	so_error;		/* [a] error affecting connection */

	struct sosplice *so_sp;		/* [s br] */

	struct sockbuf so_rcv;
	struct sockbuf so_snd;

	void	(*so_upcall)(struct socket *, caddr_t, int); /* [s] */
	caddr_t	so_upcallarg;		/* [s] Arg for above */
	uid_t	so_euid;		/* [I] who opened the socket */
	uid_t	so_ruid;		/* [I] */
	gid_t	so_egid;		/* [I] */
	gid_t	so_rgid;		/* [I] */
	pid_t	so_cpid;		/* [I] pid of process that opened
					    socket */
};

/*
 * Socket state bits.
 *
 * NOTE: The following states should be used with corresponding socket's
 * buffer `sb_state' only:
 *
 *	SS_CANTSENDMORE		with `so_snd'
 *	SS_ISSENDING		with `so_snd'
 *	SS_CANTRCVMORE		with `so_rcv'
 *	SS_RCVATMARK		with `so_rcv'
 */

#define	SS_NOFDREF		0x001	/* no file table ref any more */
#define	SS_ISCONNECTED		0x002	/* socket connected to a peer */
#define	SS_ISCONNECTING		0x004	/* in process of connecting to peer */
#define	SS_ISDISCONNECTING	0x008	/* in process of disconnecting */
#define	SS_CANTSENDMORE		0x010	/* can't send more data to peer */
#define	SS_CANTRCVMORE		0x020	/* can't receive more data from peer */
#define	SS_RCVATMARK		0x040	/* at mark on input */
#define	SS_ISDISCONNECTED	0x800	/* socket disconnected from peer */

#define	SS_PRIV			0x080	/* privileged for broadcast, raw... */
#define	SS_CONNECTOUT		0x1000	/* connect, not accept, at this end */
#define	SS_ISSENDING		0x2000	/* hint for lower layer */
#define	SS_DNS			0x4000	/* created using SOCK_DNS socket(2) */
#define	SS_YP			0x8000	/* created using ypconnect(2) */

#ifdef _KERNEL

#include <sys/protosw.h>
#include <lib/libkern/libkern.h>

struct mbuf;
struct sockaddr;
struct proc;
struct msghdr;
struct stat;
struct knote;

void	soassertlocked(struct socket *);
void	soassertlocked_readonly(struct socket *);
void	sbmtxassertlocked(struct sockbuf *);

int	soo_read(struct file *, struct uio *, int);
int	soo_write(struct file *, struct uio *, int);
int	soo_ioctl(struct file *, u_long, caddr_t, struct proc *);
int	soo_kqfilter(struct file *, struct knote *);
int	soo_close(struct file *, struct proc *);
int	soo_stat(struct file *, struct stat *, struct proc *);
void	sbappend(struct sockbuf *, struct mbuf *);
void	sbappendstream(struct sockbuf *, struct mbuf *);
int	sbappendaddr(struct sockbuf *, const struct sockaddr *, struct mbuf *,
	    struct mbuf *);
int	sbappendcontrol(struct sockbuf *, struct mbuf *, struct mbuf *);
void	sbappendrecord(struct sockbuf *, struct mbuf *);
void	sbcompress(struct sockbuf *, struct mbuf *, struct mbuf *);
struct mbuf *
	sbcreatecontrol(const void *, size_t, int, int);
void	sbdrop(struct sockbuf *, int);
void	sbdroprecord(struct sockbuf *);
void	sbflush(struct sockbuf *);
void	sbrelease(struct sockbuf *);
int	sbcheckreserve(u_long, u_long);
int	sbchecklowmem(void);
int	sbreserve(struct sockbuf *, u_long);
int	sbwait(struct sockbuf *);
void	soinit(void);
void	soabort(struct socket *);
int	soaccept(struct socket *, struct mbuf *);
int	sobind(struct socket *, struct mbuf *, struct proc *);
void	socantrcvmore(struct socket *);
void	socantsendmore(struct socket *);
int	soclose(struct socket *, int);
int	soconnect(struct socket *, struct mbuf *);
int	soconnect2(struct socket *, struct socket *);
int	socreate(int, struct socket **, int, int);
int	sodisconnect(struct socket *);
struct socket *soalloc(const struct protosw *, int);
void	sofree(struct socket *, int);
void	sorele(struct socket *);
int	sogetopt(struct socket *, int, int, struct mbuf *);
void	sohasoutofband(struct socket *);
void	soisconnected(struct socket *);
void	soisconnecting(struct socket *);
void	soisdisconnected(struct socket *);
void	soisdisconnecting(struct socket *);
int	solisten(struct socket *, int);
struct socket *sonewconn(struct socket *, int, int);
void	soqinsque(struct socket *, struct socket *, int);
int	soqremque(struct socket *, int);
int	soreceive(struct socket *, struct mbuf **, struct uio *,
	    struct mbuf **, struct mbuf **, int *, socklen_t);
int	soreserve(struct socket *, u_long, u_long);
int	sosend(struct socket *, struct mbuf *, struct uio *,
	    struct mbuf *, struct mbuf *, int);
int	sosetopt(struct socket *, int, int, struct mbuf *);
int	soshutdown(struct socket *, int);
void	sowakeup(struct socket *, struct sockbuf *);
void	sorwakeup(struct socket *);
void	sowwakeup(struct socket *);
int	sockargs(struct mbuf **, const void *, size_t, int);

int	sosleep_nsec(struct socket *, void *, int, const char *, uint64_t);
void	solock(struct socket *);
void	solock_shared(struct socket *);
void	solock_nonet(struct socket *);
int	solock_persocket(struct socket *);
void	solock_pair(struct socket *, struct socket *);
void	sounlock(struct socket *);
void	sounlock_shared(struct socket *);
void	sounlock_nonet(struct socket *);
void	sounlock_pair(struct socket *, struct socket *);

int	sendit(struct proc *, int, struct msghdr *, int, register_t *);
int	recvit(struct proc *, int, struct msghdr *, caddr_t, register_t *);
int	doaccept(struct proc *, int, struct sockaddr *, socklen_t *, int,
	    register_t *);

#ifdef SOCKBUF_DEBUG
void	sblastrecordchk(struct sockbuf *, const char *);
#define	SBLASTRECORDCHK(sb, where)	sblastrecordchk((sb), (where))

void	sblastmbufchk(struct sockbuf *, const char *);
#define	SBLASTMBUFCHK(sb, where)	sblastmbufchk((sb), (where))
void	sbcheck(struct socket *, struct sockbuf *);
#define	SBCHECK(so, sb)			sbcheck((so), (sb))
#else
#define	SBLASTRECORDCHK(sb, where)	/* nothing */
#define	SBLASTMBUFCHK(sb, where)	/* nothing */
#define	SBCHECK(so, sb)			/* nothing */
#endif /* SOCKBUF_DEBUG */

/*
 * Flags to sblock()
 */
#define SBL_WAIT	0x01	/* Wait if lock not immediately available. */
#define SBL_NOINTR	0x02	/* Enforce non-interruptible sleep. */

int	sblock(struct sockbuf *, int);
void	sbunlock(struct sockbuf *);

extern u_long		sb_max;
extern struct pool	socket_pool;

static inline struct socket *
soref(struct socket *so)
{
	if (so == NULL)
		return NULL;
	refcnt_take(&so->so_refcnt);
	return so;
}

/*
 * Macros for sockets and socket buffering.
 */

#define isspliced(so)		((so)->so_sp && (so)->so_sp->ssp_socket)
#define issplicedback(so)	((so)->so_sp && (so)->so_sp->ssp_soback)

/*
 * Do we need to notify the other side when I/O is possible?
 */
static inline int
sb_notify(struct sockbuf *sb)
{
	int rv;

	mtx_enter(&sb->sb_mtx);
	rv = ((sb->sb_flags & (SB_WAIT|SB_ASYNC|SB_SPLICE)) != 0 ||
	    !klist_empty(&sb->sb_klist));
	mtx_leave(&sb->sb_mtx);

	return rv;
}

/*
 * How much space is there in a socket buffer (so->so_snd or so->so_rcv)?
 * This is problematical if the fields are unsigned, as the space might
 * still be negative (cc > hiwat or mbcnt > mbmax).  Should detect
 * overflow and return 0.
 */

static inline long
sbspace_locked(struct sockbuf *sb)
{
	sbmtxassertlocked(sb);

	return lmin(sb->sb_hiwat - sb->sb_cc, sb->sb_mbmax - sb->sb_mbcnt);
}

static inline long
sbspace(struct sockbuf *sb)
{
	long ret;

	mtx_enter(&sb->sb_mtx);
	ret = sbspace_locked(sb);
	mtx_leave(&sb->sb_mtx);

	return ret;
}

/* do we have to send all at once on a socket? */
#define	sosendallatonce(so) \
    ((so)->so_proto->pr_flags & PR_ATOMIC)

/* are we sending on this socket? */
#define	soissending(so) \
    ((so)->so_snd.sb_state & SS_ISSENDING)

/* can we read something from so? */
static inline int
soreadable(struct socket *so)
{
	soassertlocked_readonly(so);
	if (isspliced(so))
		return 0;
	return (so->so_rcv.sb_state & SS_CANTRCVMORE) ||
	    so->so_error || so->so_rcv.sb_cc >= so->so_rcv.sb_lowat;
}

/* can we write something to so? */
static inline int
sowriteable(struct socket *so)
{
	soassertlocked_readonly(so);
	return ((sbspace(&so->so_snd) >= so->so_snd.sb_lowat &&
	    ((so->so_state & SS_ISCONNECTED) ||
	    (so->so_proto->pr_flags & PR_CONNREQUIRED)==0)) ||
	    (so->so_snd.sb_state & SS_CANTSENDMORE) || so->so_error);
}

/* adjust counters in sb reflecting allocation of m */
static inline void
sballoc(struct sockbuf *sb, struct mbuf *m)
{
	sb->sb_cc += m->m_len;
	if (m->m_type != MT_CONTROL && m->m_type != MT_SONAME)
		sb->sb_datacc += m->m_len;
	sb->sb_mbcnt += MSIZE;
	if (m->m_flags & M_EXT)
		sb->sb_mbcnt += m->m_ext.ext_size;
}

/* adjust counters in sb reflecting freeing of m */
static inline void
sbfree(struct sockbuf *sb, struct mbuf *m)
{
	sb->sb_cc -= m->m_len;
	if (m->m_type != MT_CONTROL && m->m_type != MT_SONAME)
		sb->sb_datacc -= m->m_len;
	sb->sb_mbcnt -= MSIZE;
	if (m->m_flags & M_EXT)
		sb->sb_mbcnt -= m->m_ext.ext_size;
}

static inline void
sbassertlocked(struct sockbuf *sb)
{
	rw_assert_wrlock(&sb->sb_lock);
}

#define	SB_EMPTY_FIXUP(sb) do {						\
	if ((sb)->sb_mb == NULL) {					\
		(sb)->sb_mbtail = NULL;					\
		(sb)->sb_lastrecord = NULL;				\
	}								\
} while (/*CONSTCOND*/0)

#endif /* _KERNEL */
#endif /* _SYS_SOCKETVAR_H_ */
