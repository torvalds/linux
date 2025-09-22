/*	$OpenBSD: uipc_socket.c,v 1.385 2025/07/25 08:58:44 mvs Exp $	*/
/*	$NetBSD: uipc_socket.c,v 1.21 1996/02/04 02:17:52 christos Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1988, 1990, 1993
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
 *	@(#)uipc_socket.c	8.3 (Berkeley) 4/15/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/domain.h>
#include <sys/event.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/unpcb.h>
#include <sys/socketvar.h>
#include <sys/signalvar.h>
#include <sys/pool.h>
#include <sys/atomic.h>
#include <sys/rwlock.h>
#include <sys/time.h>
#include <sys/refcnt.h>

#ifdef DDB
#include <machine/db_machdep.h>
#endif

void	sbsync(struct sockbuf *, struct mbuf *);

int	sosplice(struct socket *, int, off_t, struct timeval *);
void	sounsplice(struct socket *, struct socket *, int);
void	soidle(void *);
void	sotask(void *);
int	somove(struct socket *, int);
void	sorflush(struct socket *);

void	filt_sordetach(struct knote *kn);
int	filt_soread(struct knote *kn, long hint);
void	filt_sowdetach(struct knote *kn);
int	filt_sowrite(struct knote *kn, long hint);
int	filt_soexcept(struct knote *kn, long hint);

int	filt_sowmodify(struct kevent *kev, struct knote *kn);
int	filt_sowprocess(struct knote *kn, struct kevent *kev);

int	filt_sormodify(struct kevent *kev, struct knote *kn);
int	filt_sorprocess(struct knote *kn, struct kevent *kev);

int	filt_soemodify(struct kevent *kev, struct knote *kn);
int	filt_soeprocess(struct knote *kn, struct kevent *kev);

const struct filterops soread_filtops = {
	.f_flags	= FILTEROP_ISFD | FILTEROP_MPSAFE,
	.f_attach	= NULL,
	.f_detach	= filt_sordetach,
	.f_event	= filt_soread,
	.f_modify	= filt_sormodify,
	.f_process	= filt_sorprocess,
};

const struct filterops sowrite_filtops = {
	.f_flags	= FILTEROP_ISFD | FILTEROP_MPSAFE,
	.f_attach	= NULL,
	.f_detach	= filt_sowdetach,
	.f_event	= filt_sowrite,
	.f_modify	= filt_sowmodify,
	.f_process	= filt_sowprocess,
};

const struct filterops soexcept_filtops = {
	.f_flags	= FILTEROP_ISFD | FILTEROP_MPSAFE,
	.f_attach	= NULL,
	.f_detach	= filt_sordetach,
	.f_event	= filt_soexcept,
	.f_modify	= filt_soemodify,
	.f_process	= filt_soeprocess,
};

#ifndef SOMINCONN
#define SOMINCONN 80
#endif /* SOMINCONN */

int	somaxconn = SOMAXCONN;
int	sominconn = SOMINCONN;

struct pool socket_pool;
#ifdef SOCKET_SPLICE
struct pool sosplice_pool;
struct taskq *sosplice_taskq;
struct rwlock sosplice_lock = RWLOCK_INITIALIZER("sosplicelk");

#define so_splicelen	so_sp->ssp_len
#define so_splicemax	so_sp->ssp_max
#define so_spliceidletv	so_sp->ssp_idletv
#define so_spliceidleto	so_sp->ssp_idleto
#define so_splicetask	so_sp->ssp_task
#endif

void
soinit(void)
{
	pool_init(&socket_pool, sizeof(struct socket), 0, IPL_SOFTNET, 0,
	    "sockpl", NULL);
#ifdef SOCKET_SPLICE
	pool_init(&sosplice_pool, sizeof(struct sosplice), 0, IPL_SOFTNET, 0,
	    "sosppl", NULL);
#endif
}

struct socket *
soalloc(const struct protosw *prp, int wait)
{
	const struct domain *dp = prp->pr_domain;
	const char *dom_name = dp->dom_name;
	struct socket *so;

	so = pool_get(&socket_pool, (wait == M_WAIT ? PR_WAITOK : PR_NOWAIT) |
	    PR_ZERO);
	if (so == NULL)
		return (NULL);

#ifdef WITNESS
	/*
	 * XXX: Make WITNESS happy. AF_INET and AF_INET6 sockets could be
	 * spliced together.
	 */
	switch (dp->dom_family) {
	case AF_INET:
	case AF_INET6:
		dom_name = "inet46";
		break;
	}
#endif

	refcnt_init_trace(&so->so_refcnt, DT_REFCNT_IDX_SOCKET);
	rw_init_flags_trace(&so->so_lock, dom_name, RWL_DUPOK,
	    DT_RWLOCK_IDX_SOLOCK);
	rw_init(&so->so_rcv.sb_lock, "sbufrcv");
	rw_init(&so->so_snd.sb_lock, "sbufsnd");
	mtx_init_flags(&so->so_rcv.sb_mtx, IPL_MPFLOOR, "sbrcv", 0);
	mtx_init_flags(&so->so_snd.sb_mtx, IPL_MPFLOOR, "sbsnd", 0);
	klist_init_mutex(&so->so_rcv.sb_klist, &so->so_rcv.sb_mtx);
	klist_init_mutex(&so->so_snd.sb_klist, &so->so_snd.sb_mtx);
	sigio_init(&so->so_sigio);
	TAILQ_INIT(&so->so_q0);
	TAILQ_INIT(&so->so_q);

	return (so);
}

/*
 * Socket operation routines.
 * These routines are called by the routines in
 * sys_socket.c or from a system process, and
 * implement the semantics of socket operations by
 * switching out to the protocol specific routines.
 */
int
socreate(int dom, struct socket **aso, int type, int proto)
{
	struct proc *p = curproc;		/* XXX */
	const struct protosw *prp;
	struct socket *so;
	int error;

	if (proto)
		prp = pffindproto(dom, proto, type);
	else
		prp = pffindtype(dom, type);
	if (prp == NULL || prp->pr_usrreqs == NULL)
		return (EPROTONOSUPPORT);
	if (prp->pr_type != type)
		return (EPROTOTYPE);
	so = soalloc(prp, M_WAIT);
	so->so_type = type;
	if (suser(p) == 0)
		so->so_state = SS_PRIV;
	so->so_ruid = p->p_ucred->cr_ruid;
	so->so_euid = p->p_ucred->cr_uid;
	so->so_rgid = p->p_ucred->cr_rgid;
	so->so_egid = p->p_ucred->cr_gid;
	so->so_cpid = p->p_p->ps_pid;
	so->so_proto = prp;
	so->so_snd.sb_timeo_nsecs = INFSLP;
	so->so_rcv.sb_timeo_nsecs = INFSLP;

	solock_shared(so);
	error = pru_attach(so, proto, M_WAIT);
	if (error) {
		so->so_state |= SS_NOFDREF;
		/* sofree() calls sounlock(). */
		sofree(so, 0);
		return (error);
	}
	sounlock_shared(so);
	*aso = so;
	return (0);
}

int
sobind(struct socket *so, struct mbuf *nam, struct proc *p)
{
	soassertlocked(so);
	return pru_bind(so, nam, p);
}

int
solisten(struct socket *so, int backlog)
{
	int somaxconn_local = atomic_load_int(&somaxconn);
	int sominconn_local = atomic_load_int(&sominconn);
	int error;

	switch (so->so_type) {
	case SOCK_STREAM:
	case SOCK_SEQPACKET:
		break;
	default:
		return (EOPNOTSUPP);
	}

	soassertlocked(so);

	if (so->so_state & (SS_ISCONNECTED|SS_ISCONNECTING|SS_ISDISCONNECTING))
		return (EINVAL);
#ifdef SOCKET_SPLICE
	if (isspliced(so) || issplicedback(so))
		return (EOPNOTSUPP);
#endif /* SOCKET_SPLICE */
	error = pru_listen(so);
	if (error)
		return (error);
	if (TAILQ_FIRST(&so->so_q) == NULL)
		so->so_options |= SO_ACCEPTCONN;
	if (backlog < 0 || backlog > somaxconn_local)
		backlog = somaxconn_local;
	if (backlog < sominconn_local)
		backlog = sominconn_local;
	so->so_qlimit = backlog;
	return (0);
}

void
sorele(struct socket *so)
{
	if (refcnt_rele(&so->so_refcnt) == 0)
		return;

	sigio_free(&so->so_sigio);
	klist_free(&so->so_rcv.sb_klist);
	klist_free(&so->so_snd.sb_klist);

	mtx_enter(&so->so_snd.sb_mtx);
	sbrelease(&so->so_snd);
	mtx_leave(&so->so_snd.sb_mtx);

	if (so->so_proto->pr_flags & PR_RIGHTS &&
	    so->so_proto->pr_domain->dom_dispose)
		(*so->so_proto->pr_domain->dom_dispose)(so->so_rcv.sb_mb);
	m_purge(so->so_rcv.sb_mb);

#ifdef SOCKET_SPLICE
	if (so->so_sp)
		pool_put(&sosplice_pool, so->so_sp);
#endif
	pool_put(&socket_pool, so);
}

#define SOSP_FREEING_READ	1
#define SOSP_FREEING_WRITE	2
void
sofree(struct socket *so, int keep_lock)
{
	int persocket = solock_persocket(so);

	soassertlocked(so);

	if (so->so_pcb || (so->so_state & SS_NOFDREF) == 0) {
		if (!keep_lock)
			sounlock_shared(so);
		return;
	}
	if (so->so_head) {
		struct socket *head = so->so_head;

		/*
		 * We must not decommission a socket that's on the accept(2)
		 * queue.  If we do, then accept(2) may hang after select(2)
		 * indicated that the listening socket was ready.
		 */
		if (so->so_onq == &head->so_q) {
			if (!keep_lock)
				sounlock_shared(so);
			return;
		}

		if (persocket) {
			soref(head);
			sounlock(so);
			solock(head);
			solock(so);

			if (so->so_onq != &head->so_q0) {
				sounlock(so);
				sounlock(head);
				sorele(head);
				return;
			}
		}

		soqremque(so, 0);

		if (persocket) {
			sounlock(head);
			sorele(head);
		}
	}

	if (!keep_lock)
		sounlock_shared(so);
	sorele(so);
}

static inline uint64_t
solinger_nsec(struct socket *so)
{
	if (so->so_linger == 0)
		return INFSLP;

	return SEC_TO_NSEC(so->so_linger);
}

/*
 * Close a socket on last file table reference removal.
 * Initiate disconnect if connected.
 * Free socket when disconnect complete.
 */
int
soclose(struct socket *so, int flags)
{
	struct socket *so2;
	int error = 0;

	solock_shared(so);
	/* Revoke async IO early. There is a final revocation in sofree(). */
	sigio_free(&so->so_sigio);
	if (so->so_state & SS_ISCONNECTED) {
		if (so->so_pcb == NULL)
			goto discard;
		if ((so->so_state & SS_ISDISCONNECTING) == 0) {
			error = sodisconnect(so);
			if (error)
				goto drop;
		}
		if (so->so_options & SO_LINGER) {
			if ((so->so_state & SS_ISDISCONNECTING) &&
			    (flags & MSG_DONTWAIT))
				goto drop;
			while (so->so_state & SS_ISCONNECTED) {
				error = sosleep_nsec(so, &so->so_timeo,
				    PSOCK | PCATCH, "netcls",
				    solinger_nsec(so));
				if (error)
					break;
			}
		}
	}
drop:
	if (so->so_pcb) {
		int error2;
		error2 = pru_detach(so);
		if (error == 0)
			error = error2;
	}
	if (so->so_options & SO_ACCEPTCONN) {
		int persocket = solock_persocket(so);

		while ((so2 = TAILQ_FIRST(&so->so_q0)) != NULL) {
			soref(so2);
			solock(so2);
			(void) soqremque(so2, 0);
			sounlock(so);
			soabort(so2);
			sounlock(so2);
			sorele(so2);
			solock(so);
		}
		while ((so2 = TAILQ_FIRST(&so->so_q)) != NULL) {
			soref(so2);
			solock_nonet(so2);
			(void) soqremque(so2, 1);
			if (persocket)
				sounlock(so);
			soabort(so2);
			sounlock_nonet(so2);
			sorele(so2);
			if (persocket)
				solock(so);
		}
	}
discard:
#ifdef SOCKET_SPLICE
	if (so->so_sp) {
		struct socket *soback;

		sounlock_shared(so);
		/*
		 * Concurrent sounsplice() locks `sb_mtx' mutexes on
		 * both `so_snd' and `so_rcv' before unsplice sockets.
		 */
		mtx_enter(&so->so_snd.sb_mtx);
		soback = soref(so->so_sp->ssp_soback);
		mtx_leave(&so->so_snd.sb_mtx);

		if (soback == NULL)
			goto notsplicedback;

		/*
		 * `so' can be only unspliced, and never spliced again.
		 * Thus if issplicedback(so) check is positive, socket is
		 * still spliced and `ssp_soback' points to the same
		 * socket that `soback'.
		 */
		sblock(&soback->so_rcv, SBL_WAIT | SBL_NOINTR);
		if (issplicedback(so)) {
			int freeing = SOSP_FREEING_WRITE;

			if (so->so_sp->ssp_soback == so)
				freeing |= SOSP_FREEING_READ;
			sounsplice(so->so_sp->ssp_soback, so, freeing);
		}
		sbunlock(&soback->so_rcv);
		sorele(soback);

notsplicedback:
		sblock(&so->so_rcv, SBL_WAIT | SBL_NOINTR);
		if (isspliced(so)) {
			int freeing = SOSP_FREEING_READ;

			if (so == so->so_sp->ssp_socket)
				freeing |= SOSP_FREEING_WRITE;
			sounsplice(so, so->so_sp->ssp_socket, freeing);
		}
		sbunlock(&so->so_rcv);

		timeout_del_barrier(&so->so_spliceidleto);
		task_del(sosplice_taskq, &so->so_splicetask);
		taskq_barrier(sosplice_taskq);

		solock_shared(so);
	}
#endif /* SOCKET_SPLICE */

	if (so->so_state & SS_NOFDREF)
		panic("soclose NOFDREF: so %p, so_type %d", so, so->so_type);
	so->so_state |= SS_NOFDREF;

	/* sofree() calls sounlock(). */
	sofree(so, 0);
	return (error);
}

void
soabort(struct socket *so)
{
	soassertlocked(so);
	pru_abort(so);
}

int
soaccept(struct socket *so, struct mbuf *nam)
{
	int error = 0;

	soassertlocked(so);

	if ((so->so_state & SS_NOFDREF) == 0)
		panic("soaccept !NOFDREF: so %p, so_type %d", so, so->so_type);
	so->so_state &= ~SS_NOFDREF;
	if ((so->so_state & SS_ISDISCONNECTED) == 0 ||
	    (so->so_proto->pr_flags & PR_ABRTACPTDIS) == 0)
		error = pru_accept(so, nam);
	else
		error = ECONNABORTED;
	return (error);
}

int
soconnect(struct socket *so, struct mbuf *nam)
{
	int error;

	soassertlocked(so);

	if (so->so_options & SO_ACCEPTCONN)
		return (EOPNOTSUPP);
	/*
	 * If protocol is connection-based, can only connect once.
	 * Otherwise, if connected, try to disconnect first.
	 * This allows user to disconnect by connecting to, e.g.,
	 * a null address.
	 */
	if (so->so_state & (SS_ISCONNECTED|SS_ISCONNECTING) &&
	    ((so->so_proto->pr_flags & PR_CONNREQUIRED) ||
	    (error = sodisconnect(so))))
		error = EISCONN;
	else
		error = pru_connect(so, nam);
	return (error);
}

int
soconnect2(struct socket *so1, struct socket *so2)
{
	int error;

	solock_pair(so1, so2);
	error = pru_connect2(so1, so2);
	sounlock_pair(so1, so2);

	return (error);
}

int
sodisconnect(struct socket *so)
{
	int error;

	soassertlocked(so);

	if ((so->so_state & SS_ISCONNECTED) == 0)
		return (ENOTCONN);
	if (so->so_state & SS_ISDISCONNECTING)
		return (EALREADY);
	error = pru_disconnect(so);
	return (error);
}

int m_getuio(struct mbuf **, int, long, struct uio *);

#define	SBLOCKWAIT(f)	(((f) & MSG_DONTWAIT) ? 0 : SBL_WAIT)
/*
 * Send on a socket.
 * If send must go all at once and message is larger than
 * send buffering, then hard error.
 * Lock against other senders.
 * If must go all at once and not enough room now, then
 * inform user that this would block and do nothing.
 * Otherwise, if nonblocking, send as much as possible.
 * The data to be sent is described by "uio" if nonzero,
 * otherwise by the mbuf chain "top" (which must be null
 * if uio is not).  Data provided in mbuf chain must be small
 * enough to send all at once.
 *
 * Returns nonzero on error, timeout or signal; callers
 * must check for short counts if EINTR/ERESTART are returned.
 * Data and control buffers are freed on return.
 */
int
sosend(struct socket *so, struct mbuf *addr, struct uio *uio, struct mbuf *top,
    struct mbuf *control, int flags)
{
	long space, clen = 0;
	size_t resid;
	int error;
	int atomic = sosendallatonce(so) || top;

	if (uio)
		resid = uio->uio_resid;
	else
		resid = top->m_pkthdr.len;
	/* MSG_EOR on a SOCK_STREAM socket is invalid. */
	if (so->so_type == SOCK_STREAM && (flags & MSG_EOR)) {
		m_freem(top);
		m_freem(control);
		return (EINVAL);
	}
	if (uio && uio->uio_procp)
		uio->uio_procp->p_ru.ru_msgsnd++;
	if (control) {
		/*
		 * In theory clen should be unsigned (since control->m_len is).
		 * However, space must be signed, as it might be less than 0
		 * if we over-committed, and we must use a signed comparison
		 * of space and clen.
		 */
		clen = control->m_len;
		/* reserve extra space for AF_UNIX's internalize */
		if (so->so_proto->pr_domain->dom_family == AF_UNIX &&
		    clen >= CMSG_ALIGN(sizeof(struct cmsghdr)) &&
		    mtod(control, struct cmsghdr *)->cmsg_type == SCM_RIGHTS)
			clen = CMSG_SPACE(
			    (clen - CMSG_ALIGN(sizeof(struct cmsghdr))) *
			    (sizeof(struct fdpass) / sizeof(int)));
	}

#define	snderr(errno)	{ error = errno; goto release; }

restart:
	if ((error = sblock(&so->so_snd, SBLOCKWAIT(flags))) != 0)
		goto out;
	mtx_enter(&so->so_snd.sb_mtx);
	so->so_snd.sb_state |= SS_ISSENDING;
	do {
		if (so->so_snd.sb_state & SS_CANTSENDMORE)
			snderr(EPIPE);
		if ((error = READ_ONCE(so->so_error))) {
			so->so_error = 0;
			snderr(error);
		}
		if ((so->so_state & SS_ISCONNECTED) == 0) {
			if (so->so_proto->pr_flags & PR_CONNREQUIRED) {
				if (!(resid == 0 && clen != 0))
					snderr(ENOTCONN);
			} else if (addr == NULL)
				snderr(EDESTADDRREQ);
		}
		space = sbspace_locked(&so->so_snd);
		if (flags & MSG_OOB)
			space += 1024;
		if (so->so_proto->pr_domain->dom_family == AF_UNIX) {
			if (atomic && resid > so->so_snd.sb_hiwat)
				snderr(EMSGSIZE);
		} else {
			if (clen > so->so_snd.sb_hiwat ||
			    (atomic && resid > so->so_snd.sb_hiwat - clen))
				snderr(EMSGSIZE);
		}
		if (space < clen ||
		    (space - clen < resid &&
		    (atomic || space < so->so_snd.sb_lowat))) {
			if (flags & MSG_DONTWAIT)
				snderr(EWOULDBLOCK);
			sbunlock(&so->so_snd);
			error = sbwait(&so->so_snd);
			so->so_snd.sb_state &= ~SS_ISSENDING;
			mtx_leave(&so->so_snd.sb_mtx);
			if (error)
				goto out;
			goto restart;
		}
		space -= clen;
		do {
			if (uio == NULL) {
				/*
				 * Data is prepackaged in "top".
				 */
				resid = 0;
				if (flags & MSG_EOR)
					top->m_flags |= M_EOR;
			} else {
				mtx_leave(&so->so_snd.sb_mtx);
				error = m_getuio(&top, atomic, space, uio);
				mtx_enter(&so->so_snd.sb_mtx);
				if (error)
					goto release;
				space -= top->m_pkthdr.len;
				resid = uio->uio_resid;
				if (flags & MSG_EOR)
					top->m_flags |= M_EOR;
			}
			if (resid == 0)
				so->so_snd.sb_state &= ~SS_ISSENDING;
			if (top && so->so_options & SO_ZEROIZE)
				top->m_flags |= M_ZEROIZE;
			mtx_leave(&so->so_snd.sb_mtx);
			solock_shared(so);
			if (flags & MSG_OOB)
				error = pru_sendoob(so, top, addr, control);
			else
				error = pru_send(so, top, addr, control);
			sounlock_shared(so);
			mtx_enter(&so->so_snd.sb_mtx);
			clen = 0;
			control = NULL;
			top = NULL;
			if (error)
				goto release;
		} while (resid && space > 0);
	} while (resid);

release:
	so->so_snd.sb_state &= ~SS_ISSENDING;
	mtx_leave(&so->so_snd.sb_mtx);
	sbunlock(&so->so_snd);
out:
	m_freem(top);
	m_freem(control);
	return (error);
}

int
m_getuio(struct mbuf **mp, int atomic, long space, struct uio *uio)
{
	struct mbuf *m, *top = NULL;
	struct mbuf **nextp = &top;
	u_long len, mlen, alen;
	int align = atomic ? roundup(max_hdr, sizeof(long)) : 0;
	int error;

	do {
		/* How much data we want to put in this mbuf? */
		len = ulmin(uio->uio_resid, space);
		/* How much space are we allocating for that data? */
		alen = align + len;
		if (top == NULL && alen <= MHLEN) {
			m = m_gethdr(M_WAIT, MT_DATA);
			mlen = MHLEN;
		} else {
			m = m_clget(NULL, M_WAIT, ulmin(alen, MAXMCLBYTES));
			mlen = m->m_ext.ext_size;
			if (top != NULL)
				m->m_flags &= ~M_PKTHDR;
		}

		/* chain mbuf together */
		*nextp = m;
		nextp = &m->m_next;

		/* put the data at the end of the buffer */
		if (len < mlen)
			m_align(m, len);
		else
			len = mlen;

		error = uiomove(mtod(m, caddr_t), len, uio);
		if (error) {
			m_freem(top);
			return (error);
		}

		/* adjust counters */
		space -= len;
		m->m_len = len;
		top->m_pkthdr.len += len;
		align = 0;

		/* Is there more space and more data? */
	} while (space > 0 && uio->uio_resid > 0);

	KASSERT(top != NULL);
	*mp = top;
	return 0;
}

/*
 * Following replacement or removal of the first mbuf on the first
 * mbuf chain of a socket buffer, push necessary state changes back
 * into the socket buffer so that other consumers see the values
 * consistently.  'nextrecord' is the callers locally stored value of
 * the original value of sb->sb_mb->m_nextpkt which must be restored
 * when the lead mbuf changes.  NOTE: 'nextrecord' may be NULL.
 */
void
sbsync(struct sockbuf *sb, struct mbuf *nextrecord)
{

	/*
	 * First, update for the new value of nextrecord.  If necessary,
	 * make it the first record.
	 */
	if (sb->sb_mb != NULL)
		sb->sb_mb->m_nextpkt = nextrecord;
	else
		sb->sb_mb = nextrecord;

	/*
	 * Now update any dependent socket buffer fields to reflect
	 * the new state.  This is an inline of SB_EMPTY_FIXUP, with
	 * the addition of a second clause that takes care of the
	 * case where sb_mb has been updated, but remains the last
	 * record.
	 */
	if (sb->sb_mb == NULL) {
		sb->sb_mbtail = NULL;
		sb->sb_lastrecord = NULL;
	} else if (sb->sb_mb->m_nextpkt == NULL)
		sb->sb_lastrecord = sb->sb_mb;
}

/*
 * Implement receive operations on a socket.
 * We depend on the way that records are added to the sockbuf
 * by sbappend*.  In particular, each record (mbufs linked through m_next)
 * must begin with an address if the protocol so specifies,
 * followed by an optional mbuf or mbufs containing ancillary data,
 * and then zero or more mbufs of data.
 * In order to avoid blocking network for the entire time here, we release
 * the solock() while doing the actual copy to user space.
 * Although the sockbuf is locked, new data may still be appended,
 * and thus we must maintain consistency of the sockbuf during that time.
 *
 * The caller may receive the data as a single mbuf chain by supplying
 * an mbuf **mp0 for use in returning the chain.  The uio is then used
 * only for the count in uio_resid.
 */
int
soreceive(struct socket *so, struct mbuf **paddr, struct uio *uio,
    struct mbuf **mp0, struct mbuf **controlp, int *flagsp,
    socklen_t controllen)
{
	struct mbuf *m, **mp;
	struct mbuf *cm;
	u_long len, offset, moff;
	int flags, error, error2, type, uio_error = 0;
	const struct protosw *pr = so->so_proto;
	struct mbuf *nextrecord;
	size_t resid, orig_resid = uio->uio_resid;

	mp = mp0;
	if (paddr)
		*paddr = NULL;
	if (controlp)
		*controlp = NULL;
	if (flagsp)
		flags = *flagsp &~ MSG_EOR;
	else
		flags = 0;
	if (flags & MSG_OOB) {
		m = m_get(M_WAIT, MT_DATA);
		solock_shared(so);
		error = pru_rcvoob(so, m, flags & MSG_PEEK);
		sounlock_shared(so);
		if (error)
			goto bad;
		do {
			error = uiomove(mtod(m, caddr_t),
			    ulmin(uio->uio_resid, m->m_len), uio);
			m = m_free(m);
		} while (uio->uio_resid && error == 0 && m);
bad:
		m_freem(m);
		return (error);
	}
	if (mp)
		*mp = NULL;

restart:
	if ((error = sblock(&so->so_rcv, SBLOCKWAIT(flags))) != 0)
		return (error);
	mtx_enter(&so->so_rcv.sb_mtx);

	m = so->so_rcv.sb_mb;
#ifdef SOCKET_SPLICE
	if (isspliced(so))
		m = NULL;
#endif /* SOCKET_SPLICE */
	/*
	 * If we have less data than requested, block awaiting more
	 * (subject to any timeout) if:
	 *   1. the current count is less than the low water mark,
	 *   2. MSG_WAITALL is set, and it is possible to do the entire
	 *	receive operation at once if we block (resid <= hiwat), or
	 *   3. MSG_DONTWAIT is not set.
	 * If MSG_WAITALL is set but resid is larger than the receive buffer,
	 * we have to do the receive in sections, and thus risk returning
	 * a short count if a timeout or signal occurs after we start.
	 */
	if (m == NULL || (((flags & MSG_DONTWAIT) == 0 &&
	    so->so_rcv.sb_cc < uio->uio_resid) &&
	    (so->so_rcv.sb_cc < so->so_rcv.sb_lowat ||
	    ((flags & MSG_WAITALL) && uio->uio_resid <= so->so_rcv.sb_hiwat)) &&
	    m->m_nextpkt == NULL && (pr->pr_flags & PR_ATOMIC) == 0)) {
#ifdef DIAGNOSTIC
		if (m == NULL && so->so_rcv.sb_cc)
#ifdef SOCKET_SPLICE
		    if (!isspliced(so))
#endif /* SOCKET_SPLICE */
			panic("receive 1: so %p, so_type %d, sb_cc %lu",
			    so, so->so_type, so->so_rcv.sb_cc);
#endif
		if ((error2 = READ_ONCE(so->so_error))) {
			if (m)
				goto dontblock;
			error = error2;
			if ((flags & MSG_PEEK) == 0)
				so->so_error = 0;
			goto release;
		}
		if (so->so_rcv.sb_state & SS_CANTRCVMORE) {
			if (m)
				goto dontblock;
			else if (so->so_rcv.sb_cc == 0)
				goto release;
		}
		for (; m; m = m->m_next)
			if (m->m_type == MT_OOBDATA  || (m->m_flags & M_EOR)) {
				m = so->so_rcv.sb_mb;
				goto dontblock;
			}
		if ((so->so_state & (SS_ISCONNECTED|SS_ISCONNECTING)) == 0 &&
		    (so->so_proto->pr_flags & PR_CONNREQUIRED)) {
			error = ENOTCONN;
			goto release;
		}
		if (uio->uio_resid == 0 && controlp == NULL)
			goto release;
		if (flags & MSG_DONTWAIT) {
			error = EWOULDBLOCK;
			goto release;
		}
		SBLASTRECORDCHK(&so->so_rcv, "soreceive sbwait 1");
		SBLASTMBUFCHK(&so->so_rcv, "soreceive sbwait 1");

		sbunlock(&so->so_rcv);
		error = sbwait(&so->so_rcv);
		mtx_leave(&so->so_rcv.sb_mtx);
		if (error)
			return (error);
		goto restart;
	}
dontblock:
	/*
	 * On entry here, m points to the first record of the socket buffer.
	 * From this point onward, we maintain 'nextrecord' as a cache of the
	 * pointer to the next record in the socket buffer.  We must keep the
	 * various socket buffer pointers and local stack versions of the
	 * pointers in sync, pushing out modifications before operations that
	 * may sleep, and re-reading them afterwards.
	 *
	 * Otherwise, we will race with the network stack appending new data
	 * or records onto the socket buffer by using inconsistent/stale
	 * versions of the field, possibly resulting in socket buffer
	 * corruption.
	 */
	if (uio->uio_procp)
		uio->uio_procp->p_ru.ru_msgrcv++;
	KASSERT(m == so->so_rcv.sb_mb);
	SBLASTRECORDCHK(&so->so_rcv, "soreceive 1");
	SBLASTMBUFCHK(&so->so_rcv, "soreceive 1");
	nextrecord = m->m_nextpkt;
	if (pr->pr_flags & PR_ADDR) {
#ifdef DIAGNOSTIC
		if (m->m_type != MT_SONAME)
			panic("receive 1a: so %p, so_type %d, m %p, m_type %d",
			    so, so->so_type, m, m->m_type);
#endif
		orig_resid = 0;
		if (flags & MSG_PEEK) {
			if (paddr)
				*paddr = m_copym(m, 0, m->m_len, M_NOWAIT);
			m = m->m_next;
		} else {
			sbfree(&so->so_rcv, m);
			if (paddr) {
				*paddr = m;
				so->so_rcv.sb_mb = m->m_next;
				m->m_next = NULL;
				m = so->so_rcv.sb_mb;
			} else {
				so->so_rcv.sb_mb = m_free(m);
				m = so->so_rcv.sb_mb;
			}
			sbsync(&so->so_rcv, nextrecord);
		}
	}
	while (m && m->m_type == MT_CONTROL && error == 0) {
		int skip = 0;
		if (flags & MSG_PEEK) {
			if (mtod(m, struct cmsghdr *)->cmsg_type ==
			    SCM_RIGHTS) {
				/* don't leak internalized SCM_RIGHTS msgs */
				skip = 1;
			} else if (controlp)
				*controlp = m_copym(m, 0, m->m_len, M_NOWAIT);
			m = m->m_next;
		} else {
			sbfree(&so->so_rcv, m);
			so->so_rcv.sb_mb = m->m_next;
			m->m_nextpkt = m->m_next = NULL;
			cm = m;
			m = so->so_rcv.sb_mb;
			sbsync(&so->so_rcv, nextrecord);
			if (controlp) {
				if (pr->pr_domain->dom_externalize) {
					mtx_leave(&so->so_rcv.sb_mtx);
					error =
					    (*pr->pr_domain->dom_externalize)
					    (cm, controllen, flags);
					mtx_enter(&so->so_rcv.sb_mtx);
				}
				*controlp = cm;
			} else {
				/*
				 * Dispose of any SCM_RIGHTS message that went
				 * through the read path rather than recv.
				 */
				if (pr->pr_domain->dom_dispose) {
					mtx_leave(&so->so_rcv.sb_mtx);
					pr->pr_domain->dom_dispose(cm);
					mtx_enter(&so->so_rcv.sb_mtx);
				}
				m_free(cm);
			}
		}
		if (m != NULL)
			nextrecord = so->so_rcv.sb_mb->m_nextpkt;
		else
			nextrecord = so->so_rcv.sb_mb;
		if (controlp && !skip)
			controlp = &(*controlp)->m_next;
		orig_resid = 0;
	}

	/* If m is non-NULL, we have some data to read. */
	if (m) {
		type = m->m_type;
		if (type == MT_OOBDATA)
			flags |= MSG_OOB;
		if (m->m_flags & M_BCAST)
			flags |= MSG_BCAST;
		if (m->m_flags & M_MCAST)
			flags |= MSG_MCAST;
	}
	SBLASTRECORDCHK(&so->so_rcv, "soreceive 2");
	SBLASTMBUFCHK(&so->so_rcv, "soreceive 2");

	moff = 0;
	offset = 0;
	while (m && uio->uio_resid > 0 && error == 0) {
		if (m->m_type == MT_OOBDATA) {
			if (type != MT_OOBDATA)
				break;
		} else if (type == MT_OOBDATA) {
			break;
		} else if (m->m_type == MT_CONTROL) {
			/*
			 * If there is more than one control message in the
			 * stream, we do a short read.  Next can be received
			 * or disposed by another system call.
			 */
			break;
#ifdef DIAGNOSTIC
		} else if (m->m_type != MT_DATA && m->m_type != MT_HEADER) {
			panic("receive 3: so %p, so_type %d, m %p, m_type %d",
			    so, so->so_type, m, m->m_type);
#endif
		}
		so->so_rcv.sb_state &= ~SS_RCVATMARK;
		len = uio->uio_resid;
		if (so->so_oobmark && len > so->so_oobmark - offset)
			len = so->so_oobmark - offset;
		if (len > m->m_len - moff)
			len = m->m_len - moff;
		/*
		 * If mp is set, just pass back the mbufs.
		 * Otherwise copy them out via the uio, then free.
		 * Sockbuf must be consistent here (points to current mbuf,
		 * it points to next record) when we drop priority;
		 * we must note any additions to the sockbuf when we
		 * block interrupts again.
		 */
		if (mp == NULL && uio_error == 0) {
			SBLASTRECORDCHK(&so->so_rcv, "soreceive uiomove");
			SBLASTMBUFCHK(&so->so_rcv, "soreceive uiomove");
			resid = uio->uio_resid;
			mtx_leave(&so->so_rcv.sb_mtx);
			uio_error = uiomove(mtod(m, caddr_t) + moff, len, uio);
			mtx_enter(&so->so_rcv.sb_mtx);
			if (uio_error)
				uio->uio_resid = resid - len;
		} else
			uio->uio_resid -= len;
		if (len == m->m_len - moff) {
			if (m->m_flags & M_EOR)
				flags |= MSG_EOR;
			if (flags & MSG_PEEK) {
				m = m->m_next;
				moff = 0;
				orig_resid = 0;
			} else {
				nextrecord = m->m_nextpkt;
				sbfree(&so->so_rcv, m);
				if (mp) {
					*mp = m;
					mp = &m->m_next;
					so->so_rcv.sb_mb = m = m->m_next;
					*mp = NULL;
				} else {
					so->so_rcv.sb_mb = m_free(m);
					m = so->so_rcv.sb_mb;
				}
				/*
				 * If m != NULL, we also know that
				 * so->so_rcv.sb_mb != NULL.
				 */
				KASSERT(so->so_rcv.sb_mb == m);
				if (m) {
					m->m_nextpkt = nextrecord;
					if (nextrecord == NULL)
						so->so_rcv.sb_lastrecord = m;
				} else {
					so->so_rcv.sb_mb = nextrecord;
					SB_EMPTY_FIXUP(&so->so_rcv);
				}
				SBLASTRECORDCHK(&so->so_rcv, "soreceive 3");
				SBLASTMBUFCHK(&so->so_rcv, "soreceive 3");
			}
		} else {
			if (flags & MSG_PEEK) {
				moff += len;
				orig_resid = 0;
			} else {
				if (mp) {
					mtx_leave(&so->so_rcv.sb_mtx);
					*mp = m_copym(m, 0, len, M_WAIT);
					mtx_enter(&so->so_rcv.sb_mtx);
				}
				m->m_data += len;
				m->m_len -= len;
				so->so_rcv.sb_cc -= len;
				so->so_rcv.sb_datacc -= len;
			}
		}
		if (so->so_oobmark) {
			if ((flags & MSG_PEEK) == 0) {
				so->so_oobmark -= len;
				if (so->so_oobmark == 0) {
					so->so_rcv.sb_state |= SS_RCVATMARK;
					break;
				}
			} else {
				offset += len;
				if (offset == so->so_oobmark)
					break;
			}
		}
		if (flags & MSG_EOR)
			break;
		/*
		 * If the MSG_WAITALL flag is set (for non-atomic socket),
		 * we must not quit until "uio->uio_resid == 0" or an error
		 * termination.  If a signal/timeout occurs, return
		 * with a short count but without error.
		 * Keep sockbuf locked against other readers.
		 */
		while (flags & MSG_WAITALL && m == NULL && uio->uio_resid > 0 &&
		    !sosendallatonce(so) && !nextrecord) {
			if (so->so_rcv.sb_state & SS_CANTRCVMORE ||
			    so->so_error)
				break;
			SBLASTRECORDCHK(&so->so_rcv, "soreceive sbwait 2");
			SBLASTMBUFCHK(&so->so_rcv, "soreceive sbwait 2");
			if (sbwait(&so->so_rcv)) {
				mtx_leave(&so->so_rcv.sb_mtx);
				sbunlock(&so->so_rcv);
				return (0);
			}
			if ((m = so->so_rcv.sb_mb) != NULL)
				nextrecord = m->m_nextpkt;
		}
	}

	if (m && pr->pr_flags & PR_ATOMIC) {
		flags |= MSG_TRUNC;
		if ((flags & MSG_PEEK) == 0)
			sbdroprecord(&so->so_rcv);
	}
	if ((flags & MSG_PEEK) == 0) {
		if (m == NULL) {
			/*
			 * First part is an inline SB_EMPTY_FIXUP().  Second
			 * part makes sure sb_lastrecord is up-to-date if
			 * there is still data in the socket buffer.
			 */
			so->so_rcv.sb_mb = nextrecord;
			if (so->so_rcv.sb_mb == NULL) {
				so->so_rcv.sb_mbtail = NULL;
				so->so_rcv.sb_lastrecord = NULL;
			} else if (nextrecord->m_nextpkt == NULL)
				so->so_rcv.sb_lastrecord = nextrecord;
		}
		SBLASTRECORDCHK(&so->so_rcv, "soreceive 4");
		SBLASTMBUFCHK(&so->so_rcv, "soreceive 4");
		if (pr->pr_flags & PR_WANTRCVD) {
			mtx_leave(&so->so_rcv.sb_mtx);
			solock_shared(so);
			pru_rcvd(so);
			sounlock_shared(so);
			mtx_enter(&so->so_rcv.sb_mtx);
		}
	}
	if (orig_resid == uio->uio_resid && orig_resid &&
	    (flags & MSG_EOR) == 0 &&
	    (so->so_rcv.sb_state & SS_CANTRCVMORE) == 0) {
		mtx_leave(&so->so_rcv.sb_mtx);
		sbunlock(&so->so_rcv);
		goto restart;
	}

	if (uio_error)
		error = uio_error;

	if (flagsp)
		*flagsp |= flags;
release:
	mtx_leave(&so->so_rcv.sb_mtx);
	sbunlock(&so->so_rcv);
	return (error);
}

int
soshutdown(struct socket *so, int how)
{
	int error = 0;

	switch (how) {
	case SHUT_RD:
		sorflush(so);
		break;
	case SHUT_RDWR:
		sorflush(so);
		/* FALLTHROUGH */
	case SHUT_WR:
		solock_shared(so);
		error = pru_shutdown(so);
		sounlock_shared(so);
		break;
	default:
		error = EINVAL;
		break;
	}

	return (error);
}

void
sorflush(struct socket *so)
{
	struct sockbuf *sb = &so->so_rcv;
	struct mbuf *m;
	const struct protosw *pr = so->so_proto;
	int error;

	error = sblock(sb, SBL_WAIT | SBL_NOINTR);
	/* with SBL_WAIT and SLB_NOINTR sblock() must not fail */
	KASSERT(error == 0);

	solock_shared(so);
	socantrcvmore(so);
	sounlock_shared(so);
	mtx_enter(&sb->sb_mtx);
	m = sb->sb_mb;
	memset(&sb->sb_startzero, 0,
	     (caddr_t)&sb->sb_endzero - (caddr_t)&sb->sb_startzero);
	sb->sb_timeo_nsecs = INFSLP;
	mtx_leave(&sb->sb_mtx);
	sbunlock(sb);

	if (pr->pr_flags & PR_RIGHTS && pr->pr_domain->dom_dispose)
		(*pr->pr_domain->dom_dispose)(m);
	m_purge(m);
}

#ifdef SOCKET_SPLICE

int
sosplice(struct socket *so, int fd, off_t max, struct timeval *tv)
{
	struct file	*fp;
	struct socket	*sosp;
	struct taskq	*tq;
	int		 error = 0;

	if ((so->so_proto->pr_flags & PR_SPLICE) == 0)
		return (EPROTONOSUPPORT);
	if (max && max < 0)
		return (EINVAL);
	if (tv && (tv->tv_sec < 0 || !timerisvalid(tv)))
		return (EINVAL);

	/* If no fd is given, unsplice by removing existing link. */
	if (fd < 0) {
		if ((error = sblock(&so->so_rcv, SBL_WAIT)) != 0)
			return (error);
		if (so->so_sp && so->so_sp->ssp_socket)
			sounsplice(so, so->so_sp->ssp_socket, 0);
		else
			error = EPROTO;
		sbunlock(&so->so_rcv);
		return (error);
	}

	if (sosplice_taskq == NULL) {
		rw_enter_write(&sosplice_lock);
		if (sosplice_taskq == NULL) {
			tq = taskq_create("sosplice", 1, IPL_SOFTNET,
			    TASKQ_MPSAFE);
			if (tq == NULL) {
				rw_exit_write(&sosplice_lock);
				return (ENOMEM);
			}
			/* Ensure the taskq is fully visible to other CPUs. */
			membar_producer();
			sosplice_taskq = tq;
		}
		rw_exit_write(&sosplice_lock);
	} else {
		/* Ensure the taskq is fully visible on this CPU. */
		membar_consumer();
	}

	/* Find sosp, the drain socket where data will be spliced into. */
	if ((error = getsock(curproc, fd, &fp)) != 0)
		return (error);
	sosp = fp->f_data;

	if (sosp->so_proto->pr_usrreqs->pru_send !=
	    so->so_proto->pr_usrreqs->pru_send) {
		error = EPROTONOSUPPORT;
		goto frele;
	}

	if ((error = sblock(&so->so_rcv, SBL_WAIT)) != 0)
		goto frele;
	if ((error = sblock(&sosp->so_snd, SBL_WAIT)) != 0) {
		sbunlock(&so->so_rcv);
		goto frele;
	}
	solock_pair(so, sosp);

	if ((so->so_options & SO_ACCEPTCONN) ||
	    (sosp->so_options & SO_ACCEPTCONN)) {
		error = EOPNOTSUPP;
		goto release;
	}
	if ((so->so_state & (SS_ISCONNECTED|SS_ISCONNECTING)) == 0 &&
	    (so->so_proto->pr_flags & PR_CONNREQUIRED)) {
		error = ENOTCONN;
		goto release;
	}
	if ((sosp->so_state & (SS_ISCONNECTED|SS_ISCONNECTING)) == 0) {
		error = ENOTCONN;
		goto release;
	}
	if (so->so_sp == NULL) {
		so->so_sp = pool_get(&sosplice_pool, PR_WAITOK | PR_ZERO);
		timeout_set_flags(&so->so_spliceidleto, soidle, so,
		    KCLOCK_NONE, TIMEOUT_PROC | TIMEOUT_MPSAFE);
		task_set(&so->so_splicetask, sotask, so);
	}
	if (sosp->so_sp == NULL) {
		sosp->so_sp = pool_get(&sosplice_pool, PR_WAITOK | PR_ZERO);
		timeout_set_flags(&sosp->so_spliceidleto, soidle, sosp,
		    KCLOCK_NONE, TIMEOUT_PROC | TIMEOUT_MPSAFE);
		task_set(&sosp->so_splicetask, sotask, sosp);
	}
	if (so->so_sp->ssp_socket || sosp->so_sp->ssp_soback) {
		error = EBUSY;
		goto release;
	}

	so->so_splicelen = 0;
	so->so_splicemax = max;
	if (tv)
		so->so_spliceidletv = *tv;
	else
		timerclear(&so->so_spliceidletv);

	/*
	 * To prevent sorwakeup() calling somove() before this somove()
	 * has finished, the socket buffers are not marked as spliced yet.
	 */

	/* Splice so and sosp together. */
	mtx_enter(&so->so_rcv.sb_mtx);
	mtx_enter(&sosp->so_snd.sb_mtx);
	so->so_sp->ssp_socket = soref(sosp);
	sosp->so_sp->ssp_soback = soref(so);
	mtx_leave(&sosp->so_snd.sb_mtx);
	mtx_leave(&so->so_rcv.sb_mtx);

	sounlock_pair(so, sosp);
	sbunlock(&sosp->so_snd);

	if (somove(so, M_WAIT)) {
		mtx_enter(&so->so_rcv.sb_mtx);
		mtx_enter(&sosp->so_snd.sb_mtx);
		so->so_rcv.sb_flags |= SB_SPLICE;
		sosp->so_snd.sb_flags |= SB_SPLICE;
		mtx_leave(&sosp->so_snd.sb_mtx);
		mtx_leave(&so->so_rcv.sb_mtx);
	}

	sbunlock(&so->so_rcv);
	FRELE(fp, curproc);
	return (0);

 release:
	sounlock_pair(so, sosp);
	sbunlock(&sosp->so_snd);
	sbunlock(&so->so_rcv);
 frele:
	FRELE(fp, curproc);
	return (error);
}

void
sounsplice(struct socket *so, struct socket *sosp, int freeing)
{
	sbassertlocked(&so->so_rcv);

	mtx_enter(&so->so_rcv.sb_mtx);
	mtx_enter(&sosp->so_snd.sb_mtx);
	so->so_rcv.sb_flags &= ~SB_SPLICE;
	sosp->so_snd.sb_flags &= ~SB_SPLICE;
	KASSERT(so->so_sp->ssp_socket == sosp);
	KASSERT(sosp->so_sp->ssp_soback == so);
	so->so_sp->ssp_socket = sosp->so_sp->ssp_soback = NULL;
	mtx_leave(&sosp->so_snd.sb_mtx);
	mtx_leave(&so->so_rcv.sb_mtx);

	task_del(sosplice_taskq, &so->so_splicetask);
	timeout_del(&so->so_spliceidleto);

	/* Do not wakeup a socket that is about to be freed. */
	if ((freeing & SOSP_FREEING_READ) == 0) {
		int readable;

		solock_shared(so);
		mtx_enter(&so->so_rcv.sb_mtx);
		readable = so->so_qlen || soreadable(so);
		mtx_leave(&so->so_rcv.sb_mtx);
		if (readable)
			sorwakeup(so);
		sounlock_shared(so);
	}
	if ((freeing & SOSP_FREEING_WRITE) == 0) {
		solock_shared(sosp);
		if (sowriteable(sosp))
			sowwakeup(sosp);
		sounlock_shared(sosp);
	}

	sorele(sosp);
	sorele(so);
}

void
soidle(void *arg)
{
	struct socket *so = arg;

	sblock(&so->so_rcv, SBL_WAIT | SBL_NOINTR);
	if (so->so_rcv.sb_flags & SB_SPLICE) {
		WRITE_ONCE(so->so_error, ETIMEDOUT);
		sounsplice(so, so->so_sp->ssp_socket, 0);
	}
	sbunlock(&so->so_rcv);
}

void
sotask(void *arg)
{
	struct socket *so = arg;
	int doyield = 0;

	sblock(&so->so_rcv, SBL_WAIT | SBL_NOINTR);
	if (so->so_rcv.sb_flags & SB_SPLICE) {
		if (so->so_proto->pr_flags & PR_WANTRCVD)
			doyield = 1;
		somove(so, M_DONTWAIT);
	}
	sbunlock(&so->so_rcv);

	if (doyield) {
		/* Avoid user land starvation. */
		yield();
	}
}

/*
 * Move data from receive buffer of spliced source socket to send
 * buffer of drain socket.  Try to move as much as possible in one
 * big chunk.  It is a TCP only implementation.
 * Return value 0 means splicing has been finished, 1 continue.
 */
int
somove(struct socket *so, int wait)
{
	struct socket	*sosp = so->so_sp->ssp_socket;
	struct mbuf	*m, **mp, *nextrecord;
	u_long		 len, off, oobmark;
	long		 space;
	int		 error = 0, maxreached = 0, unsplice = 0;
	unsigned int	 rcvstate;

	sbassertlocked(&so->so_rcv);

	if (so->so_proto->pr_flags & PR_WANTRCVD)
		sblock(&so->so_snd, SBL_WAIT | SBL_NOINTR);

	mtx_enter(&so->so_rcv.sb_mtx);
	mtx_enter(&sosp->so_snd.sb_mtx);

 nextpkt:
	if ((error = READ_ONCE(so->so_error)))
		goto release;
	if (sosp->so_snd.sb_state & SS_CANTSENDMORE) {
		error = EPIPE;
		goto release;
	}

	error = READ_ONCE(sosp->so_error);
	if (error) {
		if (error != ETIMEDOUT && error != EFBIG && error != ELOOP)
			goto release;
		error = 0;
	}
	if ((sosp->so_state & SS_ISCONNECTED) == 0)
		goto release;

	/* Calculate how many bytes can be copied now. */
	len = so->so_rcv.sb_datacc;
	if (so->so_splicemax) {
		KASSERT(so->so_splicelen < so->so_splicemax);
		if (so->so_splicemax <= so->so_splicelen + len) {
			len = so->so_splicemax - so->so_splicelen;
			maxreached = 1;
		}
	}
	space = sbspace_locked(&sosp->so_snd);
	if (so->so_oobmark && so->so_oobmark < len &&
	    so->so_oobmark < space + 1024)
		space += 1024;
	if (space <= 0) {
		maxreached = 0;
		goto release;
	}
	if (space < len) {
		maxreached = 0;
		if (space < sosp->so_snd.sb_lowat)
			goto release;
		len = space;
	}
	sosp->so_snd.sb_state |= SS_ISSENDING;

	SBLASTRECORDCHK(&so->so_rcv, "somove 1");
	SBLASTMBUFCHK(&so->so_rcv, "somove 1");
	m = so->so_rcv.sb_mb;
	if (m == NULL)
		goto release;
	nextrecord = m->m_nextpkt;

	/* Drop address and control information not used with splicing. */
	if (so->so_proto->pr_flags & PR_ADDR) {
#ifdef DIAGNOSTIC
		if (m->m_type != MT_SONAME)
			panic("somove soname: so %p, so_type %d, m %p, "
			    "m_type %d", so, so->so_type, m, m->m_type);
#endif
		m = m->m_next;
	}
	while (m && m->m_type == MT_CONTROL)
		m = m->m_next;
	if (m == NULL) {
		sbdroprecord(&so->so_rcv);
		if (so->so_proto->pr_flags & PR_WANTRCVD) {
			mtx_leave(&sosp->so_snd.sb_mtx);
			mtx_leave(&so->so_rcv.sb_mtx);
			solock_shared(so);
			pru_rcvd(so);
			sounlock_shared(so);
			mtx_enter(&so->so_rcv.sb_mtx);
			mtx_enter(&sosp->so_snd.sb_mtx);
		}
		goto nextpkt;
	}

	/*
	 * By splicing sockets connected to localhost, userland might create a
	 * loop.  Dissolve splicing with error if loop is detected by counter.
	 *
	 * If we deal with looped broadcast/multicast packet we bail out with
	 * no error to suppress splice termination.
	 */
	if ((m->m_flags & M_PKTHDR) &&
	    ((m->m_pkthdr.ph_loopcnt++ >= M_MAXLOOP) ||
	    ((m->m_flags & M_LOOP) && (m->m_flags & (M_BCAST|M_MCAST))))) {
		error = ELOOP;
		goto release;
	}

	if (so->so_proto->pr_flags & PR_ATOMIC) {
		if ((m->m_flags & M_PKTHDR) == 0)
			panic("somove !PKTHDR: so %p, so_type %d, m %p, "
			    "m_type %d", so, so->so_type, m, m->m_type);
		if (sosp->so_snd.sb_hiwat < m->m_pkthdr.len) {
			error = EMSGSIZE;
			goto release;
		}
		if (len < m->m_pkthdr.len)
			goto release;
		if (m->m_pkthdr.len < len) {
			maxreached = 0;
			len = m->m_pkthdr.len;
		}
		/*
		 * Throw away the name mbuf after it has been assured
		 * that the whole first record can be processed.
		 */
		m = so->so_rcv.sb_mb;
		sbfree(&so->so_rcv, m);
		so->so_rcv.sb_mb = m_free(m);
		sbsync(&so->so_rcv, nextrecord);
	}
	/*
	 * Throw away the control mbufs after it has been assured
	 * that the whole first record can be processed.
	 */
	m = so->so_rcv.sb_mb;
	while (m && m->m_type == MT_CONTROL) {
		sbfree(&so->so_rcv, m);
		so->so_rcv.sb_mb = m_free(m);
		m = so->so_rcv.sb_mb;
		sbsync(&so->so_rcv, nextrecord);
	}

	SBLASTRECORDCHK(&so->so_rcv, "somove 2");
	SBLASTMBUFCHK(&so->so_rcv, "somove 2");

	/* Take at most len mbufs out of receive buffer. */
	for (off = 0, mp = &m; off <= len && *mp;
	    off += (*mp)->m_len, mp = &(*mp)->m_next) {
		u_long size = len - off;

#ifdef DIAGNOSTIC
		if ((*mp)->m_type != MT_DATA && (*mp)->m_type != MT_HEADER)
			panic("somove type: so %p, so_type %d, m %p, "
			    "m_type %d", so, so->so_type, *mp, (*mp)->m_type);
#endif
		if ((*mp)->m_len > size) {
			/*
			 * Move only a partial mbuf at maximum splice length or
			 * if the drain buffer is too small for this large mbuf.
			 */
			if (!maxreached && sosp->so_snd.sb_datacc > 0) {
				len -= size;
				break;
			}
			if (wait == M_WAIT) {
				mtx_leave(&sosp->so_snd.sb_mtx);
				mtx_leave(&so->so_rcv.sb_mtx);
			}
			*mp = m_copym(so->so_rcv.sb_mb, 0, size, wait);
			if (wait == M_WAIT) {
				mtx_enter(&so->so_rcv.sb_mtx);
				mtx_enter(&sosp->so_snd.sb_mtx);
			}
			if (*mp == NULL) {
				len -= size;
				break;
			}
			so->so_rcv.sb_mb->m_data += size;
			so->so_rcv.sb_mb->m_len -= size;
			so->so_rcv.sb_cc -= size;
			so->so_rcv.sb_datacc -= size;
		} else {
			*mp = so->so_rcv.sb_mb;
			sbfree(&so->so_rcv, *mp);
			so->so_rcv.sb_mb = (*mp)->m_next;
			sbsync(&so->so_rcv, nextrecord);
		}
	}
	*mp = NULL;

	SBLASTRECORDCHK(&so->so_rcv, "somove 3");
	SBLASTMBUFCHK(&so->so_rcv, "somove 3");
	SBCHECK(so, &so->so_rcv);
	if (m == NULL)
		goto release;
	m->m_nextpkt = NULL;
	if (m->m_flags & M_PKTHDR) {
		m_resethdr(m);
		m->m_pkthdr.len = len;
	}

	/* Receive buffer did shrink by len bytes, adjust oob. */
	rcvstate = so->so_rcv.sb_state;
	so->so_rcv.sb_state &= ~SS_RCVATMARK;
	oobmark = so->so_oobmark;
	so->so_oobmark = oobmark > len ? oobmark - len : 0;
	if (oobmark) {
		if (oobmark == len)
			so->so_rcv.sb_state |= SS_RCVATMARK;
		if (oobmark >= len)
			oobmark = 0;
	}

	/* Send window update to source peer as receive buffer has changed. */
	if (so->so_proto->pr_flags & PR_WANTRCVD) {
		mtx_leave(&sosp->so_snd.sb_mtx);
		mtx_leave(&so->so_rcv.sb_mtx);
		solock_shared(so);
		pru_rcvd(so);
		sounlock_shared(so);
		mtx_enter(&so->so_rcv.sb_mtx);
		mtx_enter(&sosp->so_snd.sb_mtx);
	}

	/*
	 * Handle oob data.  If any malloc fails, ignore error.
	 * TCP urgent data is not very reliable anyway.
	 */
	while (((rcvstate & SS_RCVATMARK) || oobmark) &&
	    (so->so_options & SO_OOBINLINE)) {
		struct mbuf *o = NULL;

		mtx_leave(&sosp->so_snd.sb_mtx);
		mtx_leave(&so->so_rcv.sb_mtx);

		if (rcvstate & SS_RCVATMARK) {
			o = m_get(wait, MT_DATA);
			rcvstate &= ~SS_RCVATMARK;
		} else if (oobmark) {
			o = m_split(m, oobmark, wait);
			if (o) {
				solock_shared(sosp);
				error = pru_send(sosp, m, NULL, NULL);
				sounlock_shared(sosp);

				if (error) {
					mtx_enter(&so->so_rcv.sb_mtx);
					mtx_enter(&sosp->so_snd.sb_mtx);
					if (sosp->so_snd.sb_state &
					    SS_CANTSENDMORE)
						error = EPIPE;
					m_freem(o);
					goto release;
				}
				len -= oobmark;
				so->so_splicelen += oobmark;
				m = o;
				o = m_get(wait, MT_DATA);
			}
			oobmark = 0;
		}
		if (o) {
			o->m_len = 1;
			*mtod(o, caddr_t) = *mtod(m, caddr_t);

			solock_shared(sosp);
			error = pru_sendoob(sosp, o, NULL, NULL);
			sounlock_shared(sosp);

			if (error) {
				mtx_enter(&so->so_rcv.sb_mtx);
				mtx_enter(&sosp->so_snd.sb_mtx);
				if (sosp->so_snd.sb_state & SS_CANTSENDMORE)
					error = EPIPE;
				m_freem(m);
				goto release;
			}
			len -= 1;
			so->so_splicelen += 1;
			if (oobmark) {
				oobmark -= 1;
				if (oobmark == 0)
					rcvstate |= SS_RCVATMARK;
			}
			m_adj(m, 1);
		}

		mtx_enter(&so->so_rcv.sb_mtx);
		mtx_enter(&sosp->so_snd.sb_mtx);
	}

	/* Append all remaining data to drain socket. */
	if (so->so_rcv.sb_cc == 0 || maxreached)
		sosp->so_snd.sb_state &= ~SS_ISSENDING;

	mtx_leave(&sosp->so_snd.sb_mtx);
	mtx_leave(&so->so_rcv.sb_mtx);
	solock_shared(sosp);
	error = pru_send(sosp, m, NULL, NULL);
	sounlock_shared(sosp);
	mtx_enter(&so->so_rcv.sb_mtx);
	mtx_enter(&sosp->so_snd.sb_mtx);

	if (error) {
		if (sosp->so_snd.sb_state & SS_CANTSENDMORE ||
		    sosp->so_pcb == NULL)
			error = EPIPE;
		goto release;
	}
	so->so_splicelen += len;

	/* Move several packets if possible. */
	if (!maxreached && nextrecord)
		goto nextpkt;

 release:
	sosp->so_snd.sb_state &= ~SS_ISSENDING;

	if (!error && maxreached && so->so_splicemax == so->so_splicelen)
		error = EFBIG;
	if (error)
		WRITE_ONCE(so->so_error, error);

	if (((so->so_rcv.sb_state & SS_CANTRCVMORE) &&
	    so->so_rcv.sb_cc == 0) ||
	    (sosp->so_snd.sb_state & SS_CANTSENDMORE) ||
	    maxreached || error)
		unsplice = 1;

	mtx_leave(&sosp->so_snd.sb_mtx);
	mtx_leave(&so->so_rcv.sb_mtx);

	if (so->so_proto->pr_flags & PR_WANTRCVD)
		sbunlock(&so->so_snd);

	if (unsplice) {
		sounsplice(so, sosp, 0);
		return (0);
	}
	if (timerisset(&so->so_spliceidletv)) {
		timeout_add_nsec(&so->so_spliceidleto,
		    TIMEVAL_TO_NSEC(&so->so_spliceidletv));
	}
	return (1);
}
#endif /* SOCKET_SPLICE */

void
sorwakeup(struct socket *so)
{
#ifdef SOCKET_SPLICE
	if (so->so_proto->pr_flags & PR_SPLICE) {
		mtx_enter(&so->so_rcv.sb_mtx);
		if (so->so_rcv.sb_flags & SB_SPLICE)
			task_add(sosplice_taskq, &so->so_splicetask);
		if (isspliced(so)) {
			mtx_leave(&so->so_rcv.sb_mtx);
			return;
		}
		mtx_leave(&so->so_rcv.sb_mtx);
	}
#endif
	sowakeup(so, &so->so_rcv);
	if (so->so_upcall)
		(*(so->so_upcall))(so, so->so_upcallarg, M_DONTWAIT);
}

void
sowwakeup(struct socket *so)
{
#ifdef SOCKET_SPLICE
	if (so->so_proto->pr_flags & PR_SPLICE) {
		mtx_enter(&so->so_snd.sb_mtx);
		if (so->so_snd.sb_flags & SB_SPLICE)
			task_add(sosplice_taskq,
			    &so->so_sp->ssp_soback->so_splicetask);
		if (issplicedback(so)) {
			mtx_leave(&so->so_snd.sb_mtx);
			return;
		}
		mtx_leave(&so->so_snd.sb_mtx);
	}
#endif
	sowakeup(so, &so->so_snd);
}

int
sosetopt(struct socket *so, int level, int optname, struct mbuf *m)
{
	int error = 0;

	if (level != SOL_SOCKET) {
		if (so->so_proto->pr_ctloutput) {
			solock(so);
			error = (*so->so_proto->pr_ctloutput)(PRCO_SETOPT, so,
			    level, optname, m);
			sounlock(so);
			return (error);
		}
		error = ENOPROTOOPT;
	} else {
		switch (optname) {

		case SO_LINGER:
			if (m == NULL || m->m_len != sizeof (struct linger) ||
			    mtod(m, struct linger *)->l_linger < 0 ||
			    mtod(m, struct linger *)->l_linger > SHRT_MAX)
				return (EINVAL);

			solock(so);
			so->so_linger = mtod(m, struct linger *)->l_linger;
			if (*mtod(m, int *))
				so->so_options |= optname;
			else
				so->so_options &= ~optname;
			sounlock(so);

			break;
		case SO_BINDANY:
			if ((error = suser(curproc)) != 0)	/* XXX */
				return (error);
			/* FALLTHROUGH */

		case SO_DEBUG:
		case SO_KEEPALIVE:
		case SO_USELOOPBACK:
		case SO_BROADCAST:
		case SO_REUSEADDR:
		case SO_REUSEPORT:
		case SO_OOBINLINE:
		case SO_TIMESTAMP:
		case SO_ZEROIZE:
			if (m == NULL || m->m_len < sizeof (int))
				return (EINVAL);

			solock(so);
			if (*mtod(m, int *))
				so->so_options |= optname;
			else
				so->so_options &= ~optname;
			sounlock(so);

			break;
		case SO_DONTROUTE:
			if (m == NULL || m->m_len < sizeof (int))
				return (EINVAL);
			if (*mtod(m, int *))
				error = EOPNOTSUPP;
			break;

		case SO_SNDBUF:
		case SO_RCVBUF:
		case SO_SNDLOWAT:
		case SO_RCVLOWAT:
		    {
			struct sockbuf *sb = (optname == SO_SNDBUF ||
			    optname == SO_SNDLOWAT ?
			    &so->so_snd : &so->so_rcv);
			u_long cnt;

			if (m == NULL || m->m_len < sizeof (int))
				return (EINVAL);
			cnt = *mtod(m, int *);
			if ((long)cnt <= 0)
				cnt = 1;

			mtx_enter(&sb->sb_mtx);
			switch (optname) {
			case SO_SNDBUF:
			case SO_RCVBUF:
				if (sb->sb_state &
				    (SS_CANTSENDMORE | SS_CANTRCVMORE)) {
					error = EINVAL;
					break;
				}
				if (sbcheckreserve(cnt, sb->sb_wat) ||
				    sbreserve(sb, cnt)) {
					error = ENOBUFS;
					break;
				}
				sb->sb_wat = cnt;
				break;
			case SO_SNDLOWAT:
			case SO_RCVLOWAT:
				sb->sb_lowat = (cnt > sb->sb_hiwat) ?
				    sb->sb_hiwat : cnt;
				break;
			}
			mtx_leave(&sb->sb_mtx);

			break;
		    }

		case SO_SNDTIMEO:
		case SO_RCVTIMEO:
		    {
			struct sockbuf *sb = (optname == SO_SNDTIMEO ?
			    &so->so_snd : &so->so_rcv);
			struct timeval tv;
			uint64_t nsecs;

			if (m == NULL || m->m_len < sizeof (tv))
				return (EINVAL);
			memcpy(&tv, mtod(m, struct timeval *), sizeof tv);
			if (!timerisvalid(&tv))
				return (EINVAL);
			nsecs = TIMEVAL_TO_NSEC(&tv);
			if (nsecs == UINT64_MAX)
				return (EDOM);
			if (nsecs == 0)
				nsecs = INFSLP;

			mtx_enter(&sb->sb_mtx);
			sb->sb_timeo_nsecs = nsecs;
			mtx_leave(&sb->sb_mtx);
			break;
		    }

		case SO_RTABLE:
			if (so->so_proto->pr_domain &&
			    so->so_proto->pr_domain->dom_protosw &&
			    so->so_proto->pr_ctloutput) {
				const struct domain *dom =
				    so->so_proto->pr_domain;

				level = dom->dom_protosw->pr_protocol;
				solock(so);
				error = (*so->so_proto->pr_ctloutput)
				    (PRCO_SETOPT, so, level, optname, m);
				sounlock(so);
			} else
				error = ENOPROTOOPT;
			break;
#ifdef SOCKET_SPLICE
		case SO_SPLICE:
			if (m == NULL) {
				error = sosplice(so, -1, 0, NULL);
			} else if (m->m_len < sizeof(int)) {
				error = EINVAL;
			} else if (m->m_len < sizeof(struct splice)) {
				error = sosplice(so, *mtod(m, int *), 0, NULL);
			} else {
				error = sosplice(so,
				    mtod(m, struct splice *)->sp_fd,
				    mtod(m, struct splice *)->sp_max,
				   &mtod(m, struct splice *)->sp_idle);
			}
			break;
#endif /* SOCKET_SPLICE */

		default:
			error = ENOPROTOOPT;
			break;
		}
	}

	return (error);
}

int
sogetopt(struct socket *so, int level, int optname, struct mbuf *m)
{
	int error = 0;

	if (level != SOL_SOCKET) {
		if (so->so_proto->pr_ctloutput) {
			m->m_len = 0;

			solock(so);
			error = (*so->so_proto->pr_ctloutput)(PRCO_GETOPT, so,
			    level, optname, m);
			sounlock(so);
			return (error);
		} else
			return (ENOPROTOOPT);
	} else {
		m->m_len = sizeof (int);

		switch (optname) {

		case SO_LINGER:
			m->m_len = sizeof (struct linger);
			solock_shared(so);
			mtod(m, struct linger *)->l_onoff =
				so->so_options & SO_LINGER;
			mtod(m, struct linger *)->l_linger = so->so_linger;
			sounlock_shared(so);
			break;

		case SO_BINDANY:
		case SO_USELOOPBACK:
		case SO_DEBUG:
		case SO_KEEPALIVE:
		case SO_REUSEADDR:
		case SO_REUSEPORT:
		case SO_BROADCAST:
		case SO_OOBINLINE:
		case SO_ACCEPTCONN:
		case SO_TIMESTAMP:
		case SO_ZEROIZE:
			*mtod(m, int *) = so->so_options & optname;
			break;

		case SO_DONTROUTE:
			*mtod(m, int *) = 0;
			break;

		case SO_TYPE:
			*mtod(m, int *) = so->so_type;
			break;

		case SO_ERROR:
			solock(so);
			*mtod(m, int *) = so->so_error;
			so->so_error = 0;
			sounlock(so);

			break;

		case SO_DOMAIN:
			*mtod(m, int *) = so->so_proto->pr_domain->dom_family;
			break;

		case SO_PROTOCOL:
			*mtod(m, int *) = so->so_proto->pr_protocol;
			break;

		case SO_SNDBUF:
			*mtod(m, int *) = so->so_snd.sb_hiwat;
			break;

		case SO_RCVBUF:
			*mtod(m, int *) = so->so_rcv.sb_hiwat;
			break;

		case SO_SNDLOWAT:
			*mtod(m, int *) = so->so_snd.sb_lowat;
			break;

		case SO_RCVLOWAT:
			*mtod(m, int *) = so->so_rcv.sb_lowat;
			break;

		case SO_SNDTIMEO:
		case SO_RCVTIMEO:
		    {
			struct sockbuf *sb = (optname == SO_SNDTIMEO ?
			    &so->so_snd : &so->so_rcv);
			struct timeval tv;
			uint64_t nsecs;

			mtx_enter(&sb->sb_mtx);
			nsecs = sb->sb_timeo_nsecs;
			mtx_leave(&sb->sb_mtx);

			m->m_len = sizeof(struct timeval);
			memset(&tv, 0, sizeof(tv));
			if (nsecs != INFSLP)
				NSEC_TO_TIMEVAL(nsecs, &tv);
			memcpy(mtod(m, struct timeval *), &tv, sizeof tv);
			break;
		    }

		case SO_RTABLE:
			if (so->so_proto->pr_domain &&
			    so->so_proto->pr_domain->dom_protosw &&
			    so->so_proto->pr_ctloutput) {
				const struct domain *dom =
				    so->so_proto->pr_domain;

				level = dom->dom_protosw->pr_protocol;
				solock(so);
				error = (*so->so_proto->pr_ctloutput)
				    (PRCO_GETOPT, so, level, optname, m);
				sounlock(so);
				if (error)
					return (error);
				break;
			}
			return (ENOPROTOOPT);

#ifdef SOCKET_SPLICE
		case SO_SPLICE:
		    {
			off_t len;

			m->m_len = sizeof(off_t);
			solock_shared(so);
			len = so->so_sp ? so->so_splicelen : 0;
			sounlock_shared(so);
			memcpy(mtod(m, off_t *), &len, sizeof(off_t));
			break;
		    }
#endif /* SOCKET_SPLICE */

		case SO_PEERCRED:
			if (so->so_proto->pr_protocol == AF_UNIX) {
				struct unpcb *unp = sotounpcb(so);

				solock(so);
				if (unp->unp_flags & UNP_FEIDS) {
					m->m_len = sizeof(unp->unp_connid);
					memcpy(mtod(m, caddr_t),
					    &(unp->unp_connid), m->m_len);
					sounlock(so);
					break;
				}
				sounlock(so);

				return (ENOTCONN);
			}
			return (EOPNOTSUPP);

		default:
			return (ENOPROTOOPT);
		}
		return (0);
	}
}

void
sohasoutofband(struct socket *so)
{
	pgsigio(&so->so_sigio, SIGURG, 0);
	knote(&so->so_rcv.sb_klist, 0);
}

int
soo_kqfilter(struct file *fp, struct knote *kn)
{
	struct socket *so = kn->kn_fp->f_data;
	struct sockbuf *sb;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &soread_filtops;
		sb = &so->so_rcv;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &sowrite_filtops;
		sb = &so->so_snd;
		break;
	case EVFILT_EXCEPT:
		kn->kn_fop = &soexcept_filtops;
		sb = &so->so_rcv;
		break;
	default:
		return (EINVAL);
	}

	klist_insert(&sb->sb_klist, kn);

	return (0);
}

void
filt_sordetach(struct knote *kn)
{
	struct socket *so = kn->kn_fp->f_data;

	klist_remove(&so->so_rcv.sb_klist, kn);
}

int
filt_soread(struct knote *kn, long hint)
{
	struct socket *so = kn->kn_fp->f_data;
	u_int state = READ_ONCE(so->so_state);
	u_int error = READ_ONCE(so->so_error);
	int rv = 0;

	MUTEX_ASSERT_LOCKED(&so->so_rcv.sb_mtx);

	if (so->so_options & SO_ACCEPTCONN) {
		short qlen = READ_ONCE(so->so_qlen);

		soassertlocked_readonly(so);

		kn->kn_data = qlen;
		rv = (kn->kn_data != 0);

		if (kn->kn_flags & (__EV_POLL | __EV_SELECT)) {
			if (state & SS_ISDISCONNECTED) {
				kn->kn_flags |= __EV_HUP;
				rv = 1;
			} else {
				rv = qlen || soreadable(so);
			}
		}

		return rv;
	}

	kn->kn_data = so->so_rcv.sb_cc;
#ifdef SOCKET_SPLICE
	if (isspliced(so)) {
		rv = 0;
	} else
#endif /* SOCKET_SPLICE */
	if (so->so_rcv.sb_state & SS_CANTRCVMORE) {
		kn->kn_flags |= EV_EOF;
		if (kn->kn_flags & __EV_POLL) {
			if (state & SS_ISDISCONNECTED)
				kn->kn_flags |= __EV_HUP;
		}
		kn->kn_fflags = error;
		rv = 1;
	} else if (error) {
		rv = 1;
	} else if (kn->kn_sfflags & NOTE_LOWAT) {
		rv = (kn->kn_data >= kn->kn_sdata);
	} else {
		rv = (kn->kn_data >= so->so_rcv.sb_lowat);
	}

	return rv;
}

void
filt_sowdetach(struct knote *kn)
{
	struct socket *so = kn->kn_fp->f_data;

	klist_remove(&so->so_snd.sb_klist, kn);
}

int
filt_sowrite(struct knote *kn, long hint)
{
	struct socket *so = kn->kn_fp->f_data;
	u_int state = READ_ONCE(so->so_state);
	u_int error = READ_ONCE(so->so_error);
	int rv;

	MUTEX_ASSERT_LOCKED(&so->so_snd.sb_mtx);

	kn->kn_data = sbspace_locked(&so->so_snd);
	if (so->so_snd.sb_state & SS_CANTSENDMORE) {
		kn->kn_flags |= EV_EOF;
		if (kn->kn_flags & __EV_POLL) {
			if (state & SS_ISDISCONNECTED)
				kn->kn_flags |= __EV_HUP;
		}
		kn->kn_fflags = error;
		rv = 1;
	} else if (error) {
		rv = 1;
	} else if (((state & SS_ISCONNECTED) == 0) &&
	    (so->so_proto->pr_flags & PR_CONNREQUIRED)) {
		rv = 0;
	} else if (kn->kn_sfflags & NOTE_LOWAT) {
		rv = (kn->kn_data >= kn->kn_sdata);
	} else {
		rv = (kn->kn_data >= so->so_snd.sb_lowat);
	}

	return (rv);
}

int
filt_soexcept(struct knote *kn, long hint)
{
	struct socket *so = kn->kn_fp->f_data;
	int rv = 0;

	MUTEX_ASSERT_LOCKED(&so->so_rcv.sb_mtx);

#ifdef SOCKET_SPLICE
	if (isspliced(so)) {
		rv = 0;
	} else
#endif /* SOCKET_SPLICE */
	if (kn->kn_sfflags & NOTE_OOB) {
		if (so->so_oobmark || (so->so_rcv.sb_state & SS_RCVATMARK)) {
			kn->kn_fflags |= NOTE_OOB;
			kn->kn_data -= so->so_oobmark;
			rv = 1;
		}
	}

	if (kn->kn_flags & __EV_POLL) {
		u_int state = READ_ONCE(so->so_state);

		if (state & SS_ISDISCONNECTED) {
			kn->kn_flags |= __EV_HUP;
			rv = 1;
		}
	}

	return rv;
}

int
filt_sowmodify(struct kevent *kev, struct knote *kn)
{
	struct socket *so = kn->kn_fp->f_data;
	int rv;

	mtx_enter(&so->so_snd.sb_mtx);
	rv = knote_modify(kev, kn);
	mtx_leave(&so->so_snd.sb_mtx);

	return (rv);
}

int
filt_sowprocess(struct knote *kn, struct kevent *kev)
{
	struct socket *so = kn->kn_fp->f_data;
	int rv;

	mtx_enter(&so->so_snd.sb_mtx);
	rv = knote_process(kn, kev);
	mtx_leave(&so->so_snd.sb_mtx);

	return (rv);
}

int
filt_sormodify(struct kevent *kev, struct knote *kn)
{
	struct socket *so = kn->kn_fp->f_data;
	int rv;

	if (so->so_proto->pr_flags & PR_WANTRCVD)
		solock_shared(so);
	mtx_enter(&so->so_rcv.sb_mtx);
	rv = knote_modify(kev, kn);
	mtx_leave(&so->so_rcv.sb_mtx);
	if (so->so_proto->pr_flags & PR_WANTRCVD)
		sounlock_shared(so);

	return (rv);
}

int
filt_sorprocess(struct knote *kn, struct kevent *kev)
{
	struct socket *so = kn->kn_fp->f_data;
	int rv;

	if (so->so_proto->pr_flags & PR_WANTRCVD)
		solock_shared(so);
	mtx_enter(&so->so_rcv.sb_mtx);
	rv = knote_process(kn, kev);
	mtx_leave(&so->so_rcv.sb_mtx);
	if (so->so_proto->pr_flags & PR_WANTRCVD)
		sounlock_shared(so);

	return (rv);
}

int
filt_soemodify(struct kevent *kev, struct knote *kn)
{
	struct socket *so = kn->kn_fp->f_data;
	int rv;

	mtx_enter(&so->so_rcv.sb_mtx);
	rv = knote_modify(kev, kn);
	mtx_leave(&so->so_rcv.sb_mtx);

	return (rv);
}

int
filt_soeprocess(struct knote *kn, struct kevent *kev)
{
	struct socket *so = kn->kn_fp->f_data;
	int rv;

	mtx_enter(&so->so_rcv.sb_mtx);
	rv = knote_process(kn, kev);
	mtx_leave(&so->so_rcv.sb_mtx);

	return (rv);
}

#ifdef DDB
void
sobuf_print(struct sockbuf *,
    int (*)(const char *, ...) __attribute__((__format__(__kprintf__,1,2))));

void
sobuf_print(struct sockbuf *sb,
    int (*pr)(const char *, ...) __attribute__((__format__(__kprintf__,1,2))))
{
	(*pr)("\tsb_cc: %lu\n", sb->sb_cc);
	(*pr)("\tsb_datacc: %lu\n", sb->sb_datacc);
	(*pr)("\tsb_hiwat: %lu\n", sb->sb_hiwat);
	(*pr)("\tsb_wat: %lu\n", sb->sb_wat);
	(*pr)("\tsb_mbcnt: %lu\n", sb->sb_mbcnt);
	(*pr)("\tsb_mbmax: %lu\n", sb->sb_mbmax);
	(*pr)("\tsb_lowat: %ld\n", sb->sb_lowat);
	(*pr)("\tsb_mb: %p\n", sb->sb_mb);
	(*pr)("\tsb_mbtail: %p\n", sb->sb_mbtail);
	(*pr)("\tsb_lastrecord: %p\n", sb->sb_lastrecord);
	(*pr)("\tsb_flags: %04x\n", sb->sb_flags);
	(*pr)("\tsb_state: %04x\n", sb->sb_state);
	(*pr)("\tsb_timeo_nsecs: %llu\n", sb->sb_timeo_nsecs);
}

void
so_print(void *v,
    int (*pr)(const char *, ...) __attribute__((__format__(__kprintf__,1,2))))
{
	struct socket *so = v;

	(*pr)("socket %p\n", so);
	(*pr)("so_type: %i\n", so->so_type);
	(*pr)("so_options: 0x%04x\n", so->so_options); /* %b */
	(*pr)("so_linger: %i\n", so->so_linger);
	(*pr)("so_state: 0x%04x\n", so->so_state);
	(*pr)("so_pcb: %p\n", so->so_pcb);
	(*pr)("so_proto: %p\n", so->so_proto);
	(*pr)("so_sigio: %p\n", so->so_sigio.sir_sigio);

	(*pr)("so_head: %p\n", so->so_head);
	(*pr)("so_onq: %p\n", so->so_onq);
	(*pr)("so_q0: @%p first: %p\n", &so->so_q0, TAILQ_FIRST(&so->so_q0));
	(*pr)("so_q: @%p first: %p\n", &so->so_q, TAILQ_FIRST(&so->so_q));
	(*pr)("so_eq: next: %p\n", TAILQ_NEXT(so, so_qe));
	(*pr)("so_q0len: %i\n", so->so_q0len);
	(*pr)("so_qlen: %i\n", so->so_qlen);
	(*pr)("so_qlimit: %i\n", so->so_qlimit);
	(*pr)("so_timeo: %i\n", so->so_timeo);
	(*pr)("so_obmark: %lu\n", so->so_oobmark);

	(*pr)("so_sp: %p\n", so->so_sp);
	if (so->so_sp != NULL) {
		(*pr)("\tssp_socket: %p\n", so->so_sp->ssp_socket);
		(*pr)("\tssp_soback: %p\n", so->so_sp->ssp_soback);
		(*pr)("\tssp_len: %lld\n", so->so_splicelen);
		(*pr)("\tssp_max: %lld\n", so->so_splicemax);
		(*pr)("\tssp_idletv: %lld %ld\n", so->so_spliceidletv.tv_sec,
		    so->so_spliceidletv.tv_usec);
		(*pr)("\tssp_idleto: %spending (@%d)\n",
		    timeout_pending(&so->so_spliceidleto) ? "" : "not ",
		    so->so_spliceidleto.to_time);
	}

	(*pr)("so_rcv:\n");
	sobuf_print(&so->so_rcv, pr);
	(*pr)("so_snd:\n");
	sobuf_print(&so->so_snd, pr);

	(*pr)("so_upcall: %p so_upcallarg: %p\n",
	    so->so_upcall, so->so_upcallarg);

	(*pr)("so_euid: %d so_ruid: %d\n", so->so_euid, so->so_ruid);
	(*pr)("so_egid: %d so_rgid: %d\n", so->so_egid, so->so_rgid);
	(*pr)("so_cpid: %d\n", so->so_cpid);
}
#endif
