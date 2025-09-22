/*	$OpenBSD: uipc_usrreq.c,v 1.221 2025/08/04 04:59:31 guenther Exp $	*/
/*	$NetBSD: uipc_usrreq.c,v 1.18 1996/02/09 19:00:50 christos Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1989, 1991, 1993
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
 *	@(#)uipc_usrreq.c	8.3 (Berkeley) 1/4/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/filedesc.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/unpcb.h>
#include <sys/un.h>
#include <sys/namei.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/mbuf.h>
#include <sys/task.h>
#include <sys/pledge.h>
#include <sys/pool.h>
#include <sys/rwlock.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#include <sys/refcnt.h>

#include "kcov.h"
#if NKCOV > 0
#include <sys/kcov.h>
#endif

/*
 * Locks used to protect global data and struct members:
 *      I       immutable after creation
 *      D       unp_df_lock
 *      G       unp_gc_lock
 *      M       unp_ino_mtx
 *      R       unp_rights_mtx
 *      a       atomic
 *      s       socket lock
 */

struct rwlock unp_df_lock = RWLOCK_INITIALIZER("unpdflk");
struct rwlock unp_gc_lock = RWLOCK_INITIALIZER("unpgclk");

struct mutex unp_rights_mtx = MUTEX_INITIALIZER(IPL_SOFTNET);
struct mutex unp_ino_mtx = MUTEX_INITIALIZER(IPL_SOFTNET);

/*
 * Stack of sets of files that were passed over a socket but were
 * not received and need to be closed.
 */
struct	unp_deferral {
	SLIST_ENTRY(unp_deferral)	ud_link;	/* [D] */
	int				ud_n;		/* [I] */
	/* followed by ud_n struct fdpass */
	struct fdpass			ud_fp[];	/* [I] */
};

void	uipc_setaddr(const struct unpcb *, struct mbuf *);
void	unp_discard(struct fdpass *, int);
void	unp_remove_gcrefs(struct fdpass *, int);
void	unp_restore_gcrefs(struct fdpass *, int);
void	unp_scan(struct mbuf *, void (*)(struct fdpass *, int));
int	unp_nam2sun(struct mbuf *, struct sockaddr_un **, size_t *);
static inline void unp_ref(struct unpcb *);
static inline void unp_rele(struct unpcb *);
struct socket *unp_solock_peer(struct socket *);

struct pool unpcb_pool;
struct task unp_gc_task = TASK_INITIALIZER(unp_gc, NULL);

/*
 * Unix communications domain.
 *
 * TODO:
 *	RDM
 *	rethink name space problems
 *	need a proper out-of-band
 */
const struct	sockaddr sun_noname = { sizeof(sun_noname), AF_UNIX };

/* [G] list of all UNIX domain sockets, for unp_gc() */
LIST_HEAD(unp_head, unpcb)	unp_head =
	LIST_HEAD_INITIALIZER(unp_head);
/* [D] list of sets of files that were sent over sockets that are now closed */
SLIST_HEAD(,unp_deferral)	unp_deferred =
	SLIST_HEAD_INITIALIZER(unp_deferred);

ino_t	unp_ino;	/* [U] prototype for fake inode numbers */
int	unp_rights;	/* [R] file descriptors in flight */
int	unp_defer;	/* [G] number of deferred fp to close by the GC task */
int	unp_gcing;	/* [G] GC task currently running */

const struct pr_usrreqs uipc_usrreqs = {
	.pru_attach	= uipc_attach,
	.pru_detach	= uipc_detach,
	.pru_bind	= uipc_bind,
	.pru_listen	= uipc_listen,
	.pru_connect	= uipc_connect,
	.pru_accept	= uipc_accept,
	.pru_disconnect	= uipc_disconnect,
	.pru_shutdown	= uipc_shutdown,
	.pru_rcvd	= uipc_rcvd,
	.pru_send	= uipc_send,
	.pru_abort	= uipc_abort,
	.pru_sense	= uipc_sense,
	.pru_sockaddr	= uipc_sockaddr,
	.pru_peeraddr	= uipc_peeraddr,
	.pru_connect2	= uipc_connect2,
};

const struct pr_usrreqs uipc_dgram_usrreqs = {
	.pru_attach	= uipc_attach,
	.pru_detach	= uipc_detach,
	.pru_bind	= uipc_bind,
	.pru_listen	= uipc_listen,
	.pru_connect	= uipc_connect,
	.pru_disconnect	= uipc_disconnect,
	.pru_shutdown	= uipc_dgram_shutdown,
	.pru_send	= uipc_dgram_send,
	.pru_sense	= uipc_sense,
	.pru_sockaddr	= uipc_sockaddr,
	.pru_peeraddr	= uipc_peeraddr,
	.pru_connect2	= uipc_connect2,
};

void
unp_init(void)
{
	pool_init(&unpcb_pool, sizeof(struct unpcb), 0,
	    IPL_SOFTNET, 0, "unpcb", NULL);
}

static inline void
unp_ref(struct unpcb *unp)
{
	refcnt_take(&unp->unp_refcnt);
}

static inline void
unp_rele(struct unpcb *unp)
{
	refcnt_rele_wake(&unp->unp_refcnt);
}

struct socket *
unp_solock_peer(struct socket *so)
{
	struct unpcb *unp, *unp2;
	struct socket *so2;

	unp = so->so_pcb;

again:
	if ((unp2 = unp->unp_conn) == NULL)
		return NULL;

	so2 = unp2->unp_socket;

	if (so < so2)
		solock(so2);
	else if (so > so2) {
		unp_ref(unp2);
		sounlock(so);
		solock(so2);
		solock(so);

		/* Datagram socket could be reconnected due to re-lock. */
		if (unp->unp_conn != unp2) {
			sounlock(so2);
			unp_rele(unp2);
			goto again;
		}

		unp_rele(unp2);
	}

	return so2;
}

void
uipc_setaddr(const struct unpcb *unp, struct mbuf *nam)
{
	if (unp != NULL && unp->unp_addr != NULL) {
		nam->m_len = unp->unp_addr->m_len;
		memcpy(mtod(nam, caddr_t), mtod(unp->unp_addr, caddr_t),
		    nam->m_len);
	} else {
		nam->m_len = sizeof(sun_noname);
		memcpy(mtod(nam, struct sockaddr *), &sun_noname,
		    nam->m_len);
	}
}

/*
 * Both send and receive buffers are allocated PIPSIZ bytes of buffering
 * for stream sockets, although the total for sender and receiver is
 * actually only PIPSIZ.
 * Datagram sockets really use the sendspace as the maximum datagram size,
 * and don't really want to reserve the sendspace.  Their recvspace should
 * be large enough for at least one max-size datagram plus address.
 */
#define	PIPSIZ	32768
u_int	unpst_sendspace = PIPSIZ;	/* [a] */
u_int	unpst_recvspace = PIPSIZ;	/* [a] */
u_int	unpsq_sendspace = PIPSIZ;	/* [a] */
u_int	unpsq_recvspace = PIPSIZ;	/* [a] */
u_int	unpdg_sendspace = 8192;		/* [a] really max datagram size */
u_int	unpdg_recvspace = PIPSIZ;	/* [a] */

#ifndef SMALL_KERNEL
const struct sysctl_bounded_args unpstctl_vars[] = {
	{ UNPCTL_RECVSPACE, &unpst_recvspace, 0, SB_MAX },
	{ UNPCTL_SENDSPACE, &unpst_sendspace, 0, SB_MAX },
};
const struct sysctl_bounded_args unpsqctl_vars[] = {
	{ UNPCTL_RECVSPACE, &unpsq_recvspace, 0, SB_MAX },
	{ UNPCTL_SENDSPACE, &unpsq_sendspace, 0, SB_MAX },
};
const struct sysctl_bounded_args unpdgctl_vars[] = {
	{ UNPCTL_RECVSPACE, &unpdg_recvspace, 0, SB_MAX },
	{ UNPCTL_SENDSPACE, &unpdg_sendspace, 0, SB_MAX },
};
#endif /* SMALL_KERNEL */

int
uipc_attach(struct socket *so, int proto, int wait)
{
	struct unpcb *unp;
	int error;

	if (so->so_pcb)
		return EISCONN;
	if (so->so_snd.sb_hiwat == 0 || so->so_rcv.sb_hiwat == 0) {
		switch (so->so_type) {

		case SOCK_STREAM:
			error = soreserve(so,
			    atomic_load_int(&unpst_sendspace),
			    atomic_load_int(&unpst_recvspace));
			break;

		case SOCK_SEQPACKET:
			error = soreserve(so,
			    atomic_load_int(&unpsq_sendspace),
			    atomic_load_int(&unpsq_recvspace));
			break;

		case SOCK_DGRAM:
			error = soreserve(so,
			    atomic_load_int(&unpdg_sendspace),
			    atomic_load_int(&unpdg_recvspace));
			break;

		default:
			panic("unp_attach");
		}
		if (error)
			return (error);
	}
	unp = pool_get(&unpcb_pool, (wait == M_WAIT ? PR_WAITOK : PR_NOWAIT) |
	    PR_ZERO);
	if (unp == NULL)
		return (ENOBUFS);
	refcnt_init(&unp->unp_refcnt);
	unp->unp_socket = so;
	so->so_pcb = unp;
	getnanotime(&unp->unp_ctime);

	rw_enter_write(&unp_gc_lock);
	LIST_INSERT_HEAD(&unp_head, unp, unp_link);
	rw_exit_write(&unp_gc_lock);

	return (0);
}

int
uipc_detach(struct socket *so)
{
	struct unpcb *unp = sotounpcb(so);

	if (unp == NULL)
		return (EINVAL);

	unp_detach(unp);

	return (0);
}

int
uipc_bind(struct socket *so, struct mbuf *nam, struct proc *p)
{
	struct unpcb *unp = sotounpcb(so);
	struct sockaddr_un *soun;
	struct mbuf *nam2;
	struct vnode *vp;
	struct vattr vattr;
	int error;
	struct nameidata nd;
	size_t pathlen;

	if (unp->unp_flags & (UNP_BINDING | UNP_CONNECTING))
		return (EINVAL);
	if (unp->unp_vnode != NULL)
		return (EINVAL);
	if ((error = unp_nam2sun(nam, &soun, &pathlen)))
		return (error);

	unp->unp_flags |= UNP_BINDING;

	/*
	 * Enforce `i_lock' -> `solock' because fifo subsystem
	 * requires it. The socket can't be closed concurrently
	 * because the file descriptor reference is still held.
	 */

	sounlock(unp->unp_socket);

	nam2 = m_getclr(M_WAITOK, MT_SONAME);
	nam2->m_len = sizeof(struct sockaddr_un);
	memcpy(mtod(nam2, struct sockaddr_un *), soun,
	    offsetof(struct sockaddr_un, sun_path) + pathlen);
	/* No need to NUL terminate: m_getclr() returns zero'd mbufs. */

	soun = mtod(nam2, struct sockaddr_un *);

	/* Fixup sun_len to keep it in sync with m_len. */
	soun->sun_len = nam2->m_len;

	NDINIT(&nd, CREATE, NOFOLLOW | LOCKPARENT, UIO_SYSSPACE,
	    soun->sun_path, p);
	nd.ni_pledge = PLEDGE_UNIX;
	nd.ni_unveil = UNVEIL_CREATE;

	KERNEL_LOCK();
/* SHOULD BE ABLE TO ADOPT EXISTING AND wakeup() ALA FIFO's */
	error = namei(&nd);
	if (error != 0) {
		m_freem(nam2);
		solock(unp->unp_socket);
		goto out;
	}
	vp = nd.ni_vp;
	if (vp != NULL) {
		VOP_ABORTOP(nd.ni_dvp, &nd.ni_cnd);
		if (nd.ni_dvp == vp)
			vrele(nd.ni_dvp);
		else
			vput(nd.ni_dvp);
		vrele(vp);
		m_freem(nam2);
		error = EADDRINUSE;
		solock(unp->unp_socket);
		goto out;
	}
	vattr_null(&vattr);
	vattr.va_type = VSOCK;
	vattr.va_mode = ACCESSPERMS &~ p->p_fd->fd_cmask;
	error = VOP_CREATE(nd.ni_dvp, &nd.ni_vp, &nd.ni_cnd, &vattr);
	vput(nd.ni_dvp);
	if (error) {
		m_freem(nam2);
		solock(unp->unp_socket);
		goto out;
	}
	solock(unp->unp_socket);
	unp->unp_addr = nam2;
	vp = nd.ni_vp;
	vp->v_socket = unp->unp_socket;
	unp->unp_vnode = vp;
	unp->unp_connid.uid = p->p_ucred->cr_uid;
	unp->unp_connid.gid = p->p_ucred->cr_gid;
	unp->unp_connid.pid = p->p_p->ps_pid;
	unp->unp_flags |= UNP_FEIDSBIND;
	VOP_UNLOCK(vp);
out:
	KERNEL_UNLOCK();
	unp->unp_flags &= ~UNP_BINDING;

	return (error);
}

int
uipc_listen(struct socket *so)
{
	struct unpcb *unp = sotounpcb(so);

	if (unp->unp_flags & (UNP_BINDING | UNP_CONNECTING))
		return (EINVAL);
	if (unp->unp_vnode == NULL)
		return (EINVAL);
	return (0);
}

int
uipc_connect(struct socket *so, struct mbuf *nam)
{
	return unp_connect(so, nam, curproc);
}

int
uipc_accept(struct socket *so, struct mbuf *nam)
{
	struct socket *so2;
	struct unpcb *unp = sotounpcb(so);

	/*
	 * Pass back name of connected socket, if it was bound and
	 * we are still connected (our peer may have closed already!).
	 */
	so2 = unp_solock_peer(so);
	uipc_setaddr(unp->unp_conn, nam);

	if (so2 != NULL && so2 != so)
		sounlock(so2);
	return (0);
}

int
uipc_disconnect(struct socket *so)
{
	struct unpcb *unp = sotounpcb(so);

	unp_disconnect(unp);
	return (0);
}

int
uipc_shutdown(struct socket *so)
{
	struct unpcb *unp = sotounpcb(so);
	struct socket *so2;

	socantsendmore(so);

	if (unp->unp_conn != NULL) {
		so2 = unp->unp_conn->unp_socket;
		socantrcvmore(so2);
	}

	return (0);
}

int
uipc_dgram_shutdown(struct socket *so)
{
	socantsendmore(so);
	return (0);
}

void
uipc_rcvd(struct socket *so)
{
	struct unpcb *unp = sotounpcb(so);
	struct socket *so2;

	if (unp->unp_conn == NULL)
		return;
	so2 = unp->unp_conn->unp_socket;

	/*
	 * Adjust backpressure on sender
	 * and wakeup any waiting to write.
	 */
	mtx_enter(&so->so_rcv.sb_mtx);
	mtx_enter(&so2->so_snd.sb_mtx);
	so2->so_snd.sb_mbcnt = so->so_rcv.sb_mbcnt;
	so2->so_snd.sb_cc = so->so_rcv.sb_cc;
	mtx_leave(&so2->so_snd.sb_mtx);
	mtx_leave(&so->so_rcv.sb_mtx);
	sowwakeup(so2);
}

int
uipc_send(struct socket *so, struct mbuf *m, struct mbuf *nam,
    struct mbuf *control)
{
	struct unpcb *unp = sotounpcb(so);
	struct socket *so2;
	int error = 0, dowakeup = 0;

	if (control) {
		sounlock(so);
		error = unp_internalize(control, curproc);
		solock(so);
		if (error)
			goto out;
	}

	/*
	 * We hold both solock() and `sb_mtx' mutex while modifying
	 * SS_CANTSENDMORE flag. solock() is enough to check it.
	 */
	if (so->so_snd.sb_state & SS_CANTSENDMORE) {
		error = EPIPE;
		goto dispose;
	}
	if (unp->unp_conn == NULL) {
		error = ENOTCONN;
		goto dispose;
	}

	so2 = unp->unp_conn->unp_socket;

	/*
	 * Send to paired receive port, and then raise
	 * send buffer counts to maintain backpressure.
	 * Wake up readers.
	 */
	/*
	 * sbappend*() should be serialized together
	 * with so_snd modification.
	 */
	mtx_enter(&so2->so_rcv.sb_mtx);
	mtx_enter(&so->so_snd.sb_mtx);
	if (control) {
		if (sbappendcontrol(&so2->so_rcv, m, control)) {
			control = NULL;
		} else {
			mtx_leave(&so->so_snd.sb_mtx);
			mtx_leave(&so2->so_rcv.sb_mtx);
			error = ENOBUFS;
			goto dispose;
		}
	} else if (so->so_type == SOCK_SEQPACKET)
		sbappendrecord(&so2->so_rcv, m);
	else
		sbappend(&so2->so_rcv, m);
	so->so_snd.sb_mbcnt = so2->so_rcv.sb_mbcnt;
	so->so_snd.sb_cc = so2->so_rcv.sb_cc;
	if (so2->so_rcv.sb_cc > 0)
		dowakeup = 1;
	mtx_leave(&so->so_snd.sb_mtx);
	mtx_leave(&so2->so_rcv.sb_mtx);

	if (dowakeup)
		sorwakeup(so2);

	m = NULL;

dispose:
	/* we need to undo unp_internalize in case of errors */
	if (control && error)
		unp_dispose(control);

out:
	m_freem(control);
	m_freem(m);

	return (error);
}

int
uipc_dgram_send(struct socket *so, struct mbuf *m, struct mbuf *nam,
    struct mbuf *control)
{
	struct unpcb *unp = sotounpcb(so);
	struct socket *so2;
	const struct sockaddr *from;
	int error = 0, dowakeup = 0;

	if (control) {
		sounlock(so);
		error = unp_internalize(control, curproc);
		solock(so);
		if (error)
			goto out;
	}

	if (nam) {
		if (unp->unp_conn) {
			error = EISCONN;
			goto dispose;
		}
		error = unp_connect(so, nam, curproc);
		if (error)
			goto dispose;
	}

	if (unp->unp_conn == NULL) {
		if (nam != NULL)
			error = ECONNREFUSED;
		else
			error = ENOTCONN;
		goto dispose;
	}

	so2 = unp->unp_conn->unp_socket;

	if (unp->unp_addr)
		from = mtod(unp->unp_addr, struct sockaddr *);
	else
		from = &sun_noname;

	mtx_enter(&so2->so_rcv.sb_mtx);
	if (sbappendaddr(&so2->so_rcv, from, m, control)) {
		dowakeup = 1;
		m = NULL;
		control = NULL;
	} else
		error = ENOBUFS;
	mtx_leave(&so2->so_rcv.sb_mtx);

	if (dowakeup)
		sorwakeup(so2);
	if (nam)
		unp_disconnect(unp);

dispose:
	/* we need to undo unp_internalize in case of errors */
	if (control && error)
		unp_dispose(control);

out:
	m_freem(control);
	m_freem(m);

	return (error);
}

void
uipc_abort(struct socket *so)
{
	struct unpcb *unp = sotounpcb(so);

	unp_detach(unp);
	sofree(so, 1);
}

int
uipc_sense(struct socket *so, struct stat *sb)
{
	struct unpcb *unp = sotounpcb(so);

	sb->st_blksize = so->so_snd.sb_hiwat;
	sb->st_dev = NODEV;
	mtx_enter(&unp_ino_mtx);
	if (unp->unp_ino == 0)
		unp->unp_ino = unp_ino++;
	mtx_leave(&unp_ino_mtx);
	sb->st_atim.tv_sec =
	    sb->st_mtim.tv_sec =
	    sb->st_ctim.tv_sec = unp->unp_ctime.tv_sec;
	sb->st_atim.tv_nsec =
	    sb->st_mtim.tv_nsec =
	    sb->st_ctim.tv_nsec = unp->unp_ctime.tv_nsec;
	sb->st_ino = unp->unp_ino;

	return (0);
}

int
uipc_sockaddr(struct socket *so, struct mbuf *nam)
{
	struct unpcb *unp = sotounpcb(so);

	uipc_setaddr(unp, nam);
	return (0);
}

int
uipc_peeraddr(struct socket *so, struct mbuf *nam)
{
	struct unpcb *unp = sotounpcb(so);
	struct socket *so2;

	so2 = unp_solock_peer(so);
	uipc_setaddr(unp->unp_conn, nam);
	if (so2 != NULL && so2 != so)
		sounlock(so2);
	return (0);
}

int
uipc_connect2(struct socket *so, struct socket *so2)
{
	struct unpcb *unp = sotounpcb(so), *unp2;
	int error;

	if ((error = unp_connect2(so, so2)))
		return (error);

	unp->unp_connid.uid = curproc->p_ucred->cr_uid;
	unp->unp_connid.gid = curproc->p_ucred->cr_gid;
	unp->unp_connid.pid = curproc->p_p->ps_pid;
	unp->unp_flags |= UNP_FEIDS;
	unp2 = sotounpcb(so2);
	unp2->unp_connid.uid = curproc->p_ucred->cr_uid;
	unp2->unp_connid.gid = curproc->p_ucred->cr_gid;
	unp2->unp_connid.pid = curproc->p_p->ps_pid;
	unp2->unp_flags |= UNP_FEIDS;

	return (0);
}

#ifndef SMALL_KERNEL
int
uipc_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen)
{
	int *valp = &unp_defer;

	/* All sysctl names at this level are terminal. */
	switch (name[0]) {
	case SOCK_STREAM:
		if (namelen != 2)
			return (ENOTDIR);
		return sysctl_bounded_arr(unpstctl_vars, nitems(unpstctl_vars),
		    name + 1, namelen - 1, oldp, oldlenp, newp, newlen);
	case SOCK_SEQPACKET:
		if (namelen != 2)
			return (ENOTDIR);
		return sysctl_bounded_arr(unpsqctl_vars, nitems(unpsqctl_vars),
		    name + 1, namelen - 1, oldp, oldlenp, newp, newlen);
	case SOCK_DGRAM:
		if (namelen != 2)
			return (ENOTDIR);
		return sysctl_bounded_arr(unpdgctl_vars, nitems(unpdgctl_vars),
		    name + 1, namelen - 1, oldp, oldlenp, newp, newlen);
	case NET_UNIX_INFLIGHT:
		valp = &unp_rights;
		/* FALLTHROUGH */
	case NET_UNIX_DEFERRED:
		if (namelen != 1)
			return (ENOTDIR);
		return sysctl_rdint(oldp, oldlenp, newp, *valp);
	default:
		return (ENOPROTOOPT);
	}
}
#endif /* SMALL_KERNEL */

void
unp_detach(struct unpcb *unp)
{
	struct socket *so = unp->unp_socket;
	struct vnode *vp = unp->unp_vnode;
	struct unpcb *unp2;

	unp->unp_vnode = NULL;

	rw_enter_write(&unp_gc_lock);
	LIST_REMOVE(unp, unp_link);
	rw_exit_write(&unp_gc_lock);

	if (vp != NULL) {
		/* Enforce `i_lock' -> solock() lock order. */
		sounlock(so);
		VOP_LOCK(vp, LK_EXCLUSIVE);
		vp->v_socket = NULL;

		KERNEL_LOCK();
		vput(vp);
		KERNEL_UNLOCK();
		solock(so);
	}

	if (unp->unp_conn != NULL) {
		/*
		 * Datagram socket could be connected to itself.
		 * Such socket will be disconnected here.
		 */
		unp_disconnect(unp);
	}

	while ((unp2 = SLIST_FIRST(&unp->unp_refs)) != NULL) {
		struct socket *so2 = unp2->unp_socket;

		if (so < so2)
			solock(so2);
		else {
			unp_ref(unp2);
			sounlock(so);
			solock(so2);
			solock(so);

			if (unp2->unp_conn != unp) {
				/* `unp2' was disconnected due to re-lock. */
				sounlock(so2);
				unp_rele(unp2);
				continue;
			}

			unp_rele(unp2);
		}

		unp2->unp_conn = NULL;
		SLIST_REMOVE(&unp->unp_refs, unp2, unpcb, unp_nextref);
		so2->so_error = ECONNRESET;
		so2->so_state &= ~SS_ISCONNECTED;

		sounlock(so2);
	}

	sounlock(so);
	refcnt_finalize(&unp->unp_refcnt, "unpfinal");
	solock(so);

	soisdisconnected(so);
	so->so_pcb = NULL;
	m_freem(unp->unp_addr);
	pool_put(&unpcb_pool, unp);
	if (unp_rights)
		task_add(systqmp, &unp_gc_task);
}

int
unp_connect(struct socket *so, struct mbuf *nam, struct proc *p)
{
	struct sockaddr_un *soun;
	struct vnode *vp;
	struct socket *so2, *so3;
	struct unpcb *unp, *unp2, *unp3;
	struct nameidata nd;
	int error;

	unp = sotounpcb(so);
	if (unp->unp_flags & (UNP_BINDING | UNP_CONNECTING))
		return (EISCONN);
	if ((error = unp_nam2sun(nam, &soun, NULL)))
		return (error);

	NDINIT(&nd, LOOKUP, FOLLOW | LOCKLEAF, UIO_SYSSPACE, soun->sun_path, p);
	nd.ni_pledge = PLEDGE_UNIX;
	nd.ni_unveil = UNVEIL_WRITE;

	unp->unp_flags |= UNP_CONNECTING;

	/*
	 * Enforce `i_lock' -> `solock' because fifo subsystem
	 * requires it. The socket can't be closed concurrently
	 * because the file descriptor reference is still held.
	 */

	sounlock(so);

	KERNEL_LOCK();
	error = namei(&nd);
	if (error != 0)
		goto unlock;
	vp = nd.ni_vp;
	if (vp->v_type != VSOCK) {
		error = ENOTSOCK;
		goto put;
	}
	if ((error = VOP_ACCESS(vp, VWRITE, p->p_ucred, p)) != 0)
		goto put;
	so2 = vp->v_socket;
	if (so2 == NULL) {
		error = ECONNREFUSED;
		goto put;
	}
	if (so->so_type != so2->so_type) {
		error = EPROTOTYPE;
		goto put;
	}

	if (so->so_proto->pr_flags & PR_CONNREQUIRED) {
		solock(so2);

		if ((so2->so_options & SO_ACCEPTCONN) == 0 ||
		    (so3 = sonewconn(so2, 0, M_WAIT)) == NULL) {
			sounlock(so2);
			error = ECONNREFUSED;
			goto put;
		}

		/*
		 * Since `so2' is protected by vnode(9) lock, `so3'
		 * can't be PRU_ABORT'ed here.
		 */
		sounlock(so2);
		sounlock(so3);
		solock_pair(so, so3);

		unp2 = sotounpcb(so2);
		unp3 = sotounpcb(so3);

		/*
		 * `unp_addr', `unp_connid' and 'UNP_FEIDSBIND' flag
		 * are immutable since we set them in uipc_bind().
		 */
		if (unp2->unp_addr)
			unp3->unp_addr =
			    m_copym(unp2->unp_addr, 0, M_COPYALL, M_NOWAIT);
		unp3->unp_connid.uid = p->p_ucred->cr_uid;
		unp3->unp_connid.gid = p->p_ucred->cr_gid;
		unp3->unp_connid.pid = p->p_p->ps_pid;
		unp3->unp_flags |= UNP_FEIDS;

		if (unp2->unp_flags & UNP_FEIDSBIND) {
			unp->unp_connid = unp2->unp_connid;
			unp->unp_flags |= UNP_FEIDS;
		}

		so2 = so3;
	} else
		solock_pair(so, so2);

	error = unp_connect2(so, so2);

	/*
	 * `so2' can't be PRU_ABORT'ed concurrently
	 */
	sounlock_pair(so, so2);
put:
	vput(vp);
unlock:
	KERNEL_UNLOCK();
	solock(so);
	unp->unp_flags &= ~UNP_CONNECTING;

	/*
	 * The peer socket could be closed by concurrent thread
	 * when `so' and `vp' are unlocked.
	 */
	if (error == 0 && unp->unp_conn == NULL)
		error = ECONNREFUSED;

	return (error);
}

int
unp_connect2(struct socket *so, struct socket *so2)
{
	struct unpcb *unp = sotounpcb(so);
	struct unpcb *unp2;

	soassertlocked(so);
	soassertlocked(so2);

	if (so2->so_type != so->so_type)
		return (EPROTOTYPE);
	unp2 = sotounpcb(so2);
	unp->unp_conn = unp2;
	switch (so->so_type) {

	case SOCK_DGRAM:
		SLIST_INSERT_HEAD(&unp2->unp_refs, unp, unp_nextref);
		soisconnected(so);
		break;

	case SOCK_STREAM:
	case SOCK_SEQPACKET:
		unp2->unp_conn = unp;
		soisconnected(so);
		soisconnected(so2);
		break;

	default:
		panic("unp_connect2");
	}
	return (0);
}

void
unp_disconnect(struct unpcb *unp)
{
	struct socket *so2;
	struct unpcb *unp2;

	if ((so2 = unp_solock_peer(unp->unp_socket)) == NULL)
		return;

	unp2 = unp->unp_conn;
	unp->unp_conn = NULL;

	switch (unp->unp_socket->so_type) {

	case SOCK_DGRAM:
		SLIST_REMOVE(&unp2->unp_refs, unp, unpcb, unp_nextref);
		unp->unp_socket->so_state &= ~SS_ISCONNECTED;
		break;

	case SOCK_STREAM:
	case SOCK_SEQPACKET:
		unp->unp_socket->so_snd.sb_mbcnt = 0;
		unp->unp_socket->so_snd.sb_cc = 0;
		soisdisconnected(unp->unp_socket);
		unp2->unp_conn = NULL;
		unp2->unp_socket->so_snd.sb_mbcnt = 0;
		unp2->unp_socket->so_snd.sb_cc = 0;
		soisdisconnected(unp2->unp_socket);
		break;
	}

	if (so2 != unp->unp_socket)
		sounlock(so2);
}

static struct unpcb *
fptounp(struct file *fp)
{
	struct socket *so;

	if (fp->f_type != DTYPE_SOCKET)
		return (NULL);
	if ((so = fp->f_data) == NULL)
		return (NULL);
	if (so->so_proto->pr_domain != &unixdomain)
		return (NULL);
	return (sotounpcb(so));
}

int
unp_externalize(struct mbuf *rights, socklen_t controllen, int flags)
{
	struct proc *p = curproc;		/* XXX */
	struct cmsghdr *cm = mtod(rights, struct cmsghdr *);
	struct filedesc *fdp = p->p_fd;
	int i, *fds = NULL;
	struct fdpass *rp;
	struct file *fp;
	int nfds, error = 0;

	/*
	 * This code only works because SCM_RIGHTS is the only supported
	 * control message type on unix sockets. Enforce this here.
	 */
	if (cm->cmsg_type != SCM_RIGHTS || cm->cmsg_level != SOL_SOCKET)
		return EINVAL;

	nfds = (cm->cmsg_len - CMSG_ALIGN(sizeof(*cm))) /
	    sizeof(struct fdpass);
	if (controllen < CMSG_ALIGN(sizeof(struct cmsghdr)))
		controllen = 0;
	else
		controllen -= CMSG_ALIGN(sizeof(struct cmsghdr));
	if (nfds > controllen / sizeof(int)) {
		error = EMSGSIZE;
		goto out;
	}

	/* Make sure the recipient should be able to see the descriptors.. */
	rp = (struct fdpass *)CMSG_DATA(cm);

	/* fdp->fd_rdir requires KERNEL_LOCK() */
	KERNEL_LOCK();

	for (i = 0; i < nfds; i++) {
		fp = rp->fp;
		rp++;
		error = pledge_recvfd(p, fp);
		if (error)
			break;

		/*
		 * No to block devices.  If passing a directory,
		 * make sure that it is underneath the root.
		 */
		if (fdp->fd_rdir != NULL && fp->f_type == DTYPE_VNODE) {
			struct vnode *vp = (struct vnode *)fp->f_data;

			if (vp->v_type == VBLK ||
			    (vp->v_type == VDIR &&
			    !vn_isunder(vp, fdp->fd_rdir, p))) {
				error = EPERM;
				break;
			}
		}
	}

	KERNEL_UNLOCK();

	if (error)
		goto out;

	fds = mallocarray(nfds, sizeof(int), M_TEMP, M_WAITOK);

	fdplock(fdp);
restart:
	/*
	 * First loop -- allocate file descriptor table slots for the
	 * new descriptors.
	 */
	rp = ((struct fdpass *)CMSG_DATA(cm));
	for (i = 0; i < nfds; i++) {
		if ((error = fdalloc(p, 0, &fds[i])) != 0) {
			/*
			 * Back out what we've done so far.
			 */
			for (--i; i >= 0; i--)
				fdremove(fdp, fds[i]);

			if (error == ENOSPC) {
				fdexpand(p);
				goto restart;
			}

			fdpunlock(fdp);

			/*
			 * This is the error that has historically
			 * been returned, and some callers may
			 * expect it.
			 */

			error = EMSGSIZE;
			goto out;
		}

		/*
		 * Make the slot reference the descriptor so that
		 * fdalloc() works properly.. We finalize it all
		 * in the loop below.
		 */
		mtx_enter(&fdp->fd_fplock);
		KASSERT(fdp->fd_ofiles[fds[i]] == NULL);
		fdp->fd_ofiles[fds[i]] = rp->fp;
		mtx_leave(&fdp->fd_fplock);

		fdp->fd_ofileflags[fds[i]] = (rp->flags & UF_PLEDGED);
		if (flags & MSG_CMSG_CLOEXEC)
			fdp->fd_ofileflags[fds[i]] |= UF_EXCLOSE;
		if (flags & MSG_CMSG_CLOFORK)
			fdp->fd_ofileflags[fds[i]] |= UF_FORKCLOSE;

		rp++;
	}

	/*
	 * Keep `fdp' locked to prevent concurrent close() of just
	 * inserted descriptors. Such descriptors could have the only
	 * `f_count' reference which is now shared between control
	 * message and `fdp'.
	 */

	/*
	 * Now that adding them has succeeded, update all of the
	 * descriptor passing state.
	 */
	rp = (struct fdpass *)CMSG_DATA(cm);

	for (i = 0; i < nfds; i++) {
		struct unpcb *unp;

		fp = rp->fp;
		rp++;
		if ((unp = fptounp(fp)) != NULL) {
			rw_enter_write(&unp_gc_lock);
			unp->unp_msgcount--;
			rw_exit_write(&unp_gc_lock);
		}
	}
	fdpunlock(fdp);

	mtx_enter(&unp_rights_mtx);
	unp_rights -= nfds;
	mtx_leave(&unp_rights_mtx);

	/*
	 * Copy temporary array to message and adjust length, in case of
	 * transition from large struct file pointers to ints.
	 */
	memcpy(CMSG_DATA(cm), fds, nfds * sizeof(int));
	cm->cmsg_len = CMSG_LEN(nfds * sizeof(int));
	rights->m_len = CMSG_LEN(nfds * sizeof(int));
 out:
	if (fds != NULL)
		free(fds, M_TEMP, nfds * sizeof(int));

	if (error) {
		if (nfds > 0) {
			/*
			 * No lock required. We are the only `cm' holder.
			 */
			rp = ((struct fdpass *)CMSG_DATA(cm));
			unp_discard(rp, nfds);
		}
	}

	return (error);
}

int
unp_internalize(struct mbuf *control, struct proc *p)
{
	struct filedesc *fdp = p->p_fd;
	struct cmsghdr *cm = mtod(control, struct cmsghdr *);
	struct fdpass *rp;
	struct file *fp;
	struct unpcb *unp;
	int i, error;
	int nfds, *ip, fd, neededspace;

	/*
	 * Check for two potential msg_controllen values because
	 * IETF stuck their nose in a place it does not belong.
	 */
	if (control->m_len < CMSG_LEN(0) || cm->cmsg_len < CMSG_LEN(0))
		return (EINVAL);
	if (cm->cmsg_type != SCM_RIGHTS || cm->cmsg_level != SOL_SOCKET ||
	    !(cm->cmsg_len == control->m_len ||
	    control->m_len == CMSG_ALIGN(cm->cmsg_len)))
		return (EINVAL);
	nfds = (cm->cmsg_len - CMSG_ALIGN(sizeof(*cm))) / sizeof (int);

	mtx_enter(&unp_rights_mtx);
	if (unp_rights + nfds > maxfiles / 10) {
		mtx_leave(&unp_rights_mtx);
		return (EMFILE);
	}
	unp_rights += nfds;
	mtx_leave(&unp_rights_mtx);

	/* Make sure we have room for the struct file pointers */
morespace:
	neededspace = CMSG_SPACE(nfds * sizeof(struct fdpass)) -
	    control->m_len;
	if (neededspace > m_trailingspace(control)) {
		char *tmp;
		/* if we already have a cluster, the message is just too big */
		if (control->m_flags & M_EXT) {
			error = E2BIG;
			goto nospace;
		}

		/* copy cmsg data temporarily out of the mbuf */
		tmp = malloc(control->m_len, M_TEMP, M_WAITOK);
		memcpy(tmp, mtod(control, caddr_t), control->m_len);

		/* allocate a cluster and try again */
		MCLGET(control, M_WAIT);
		if ((control->m_flags & M_EXT) == 0) {
			free(tmp, M_TEMP, control->m_len);
			error = ENOBUFS;       /* allocation failed */
			goto nospace;
		}

		/* copy the data back into the cluster */
		cm = mtod(control, struct cmsghdr *);
		memcpy(cm, tmp, control->m_len);
		free(tmp, M_TEMP, control->m_len);
		goto morespace;
	}

	/* adjust message & mbuf to note amount of space actually used. */
	cm->cmsg_len = CMSG_LEN(nfds * sizeof(struct fdpass));
	control->m_len = CMSG_SPACE(nfds * sizeof(struct fdpass));

	ip = ((int *)CMSG_DATA(cm)) + nfds - 1;
	rp = ((struct fdpass *)CMSG_DATA(cm)) + nfds - 1;
	fdplock(fdp);
	for (i = 0; i < nfds; i++) {
		memcpy(&fd, ip, sizeof fd);
		ip--;
		if ((fp = fd_getfile(fdp, fd)) == NULL) {
			error = EBADF;
			goto fail;
		}
		if (fp->f_count >= FDUP_MAX_COUNT) {
			error = EDEADLK;
			goto fail;
		}
		error = pledge_sendfd(p, fp);
		if (error)
			goto fail;

		/* kqueue descriptors cannot be copied */
		if (fp->f_type == DTYPE_KQUEUE) {
			error = EINVAL;
			goto fail;
		}
#if NKCOV > 0
		/* kcov descriptors cannot be copied */
		if (fp->f_type == DTYPE_VNODE && kcov_vnode(fp->f_data)) {
			error = EINVAL;
			goto fail;
		}
#endif
		rp->fp = fp;
		rp->flags = fdp->fd_ofileflags[fd] & UF_PLEDGED;
		rp--;
		if ((unp = fptounp(fp)) != NULL) {
			rw_enter_write(&unp_gc_lock);
			unp->unp_msgcount++;
			unp->unp_file = fp;
			rw_exit_write(&unp_gc_lock);
		}
	}
	fdpunlock(fdp);
	return (0);
fail:
	fdpunlock(fdp);
	if (fp != NULL)
		FRELE(fp, p);
	/* Back out what we just did. */
	for ( ; i > 0; i--) {
		rp++;
		fp = rp->fp;
		if ((unp = fptounp(fp)) != NULL) {
			rw_enter_write(&unp_gc_lock);
			unp->unp_msgcount--;
			rw_exit_write(&unp_gc_lock);
		}
		FRELE(fp, p);
	}

nospace:
	mtx_enter(&unp_rights_mtx);
	unp_rights -= nfds;
	mtx_leave(&unp_rights_mtx);

	return (error);
}

void
unp_gc(void *arg __unused)
{
	struct unp_deferral *defer;
	struct file *fp;
	struct socket *so;
	struct unpcb *unp;
	int nunref, i;

	rw_enter_write(&unp_gc_lock);
	if (unp_gcing)
		goto unlock;
	unp_gcing = 1;
	rw_exit_write(&unp_gc_lock);

	rw_enter_write(&unp_df_lock);
	/* close any fds on the deferred list */
	while ((defer = SLIST_FIRST(&unp_deferred)) != NULL) {
		SLIST_REMOVE_HEAD(&unp_deferred, ud_link);
		rw_exit_write(&unp_df_lock);
		for (i = 0; i < defer->ud_n; i++) {
			fp = defer->ud_fp[i].fp;
			if (fp == NULL)
				continue;
			if ((unp = fptounp(fp)) != NULL) {
				rw_enter_write(&unp_gc_lock);
				unp->unp_msgcount--;
				rw_exit_write(&unp_gc_lock);
			}
			mtx_enter(&unp_rights_mtx);
			unp_rights--;
			mtx_leave(&unp_rights_mtx);
			 /* closef() expects a refcount of 2 */
			FREF(fp);
			(void) closef(fp, NULL);
		}
		free(defer, M_TEMP, sizeof(*defer) +
		    sizeof(struct fdpass) * defer->ud_n);
		rw_enter_write(&unp_df_lock);
	}
	rw_exit_write(&unp_df_lock);

	nunref = 0;

	rw_enter_write(&unp_gc_lock);

	/*
	 * Determine sockets which may be prospectively dead. Such
	 * sockets have their `unp_msgcount' equal to the `f_count'.
	 * If `unp_msgcount' is 0, the socket has not been passed
	 * and can't be unreferenced.
	 */
	LIST_FOREACH(unp, &unp_head, unp_link) {
		unp->unp_gcflags = 0;

		if (unp->unp_msgcount == 0)
			continue;
		if ((fp = unp->unp_file) == NULL)
			continue;
		if (fp->f_count == unp->unp_msgcount) {
			unp->unp_gcflags |= UNP_GCDEAD;
			unp->unp_gcrefs = unp->unp_msgcount;
			nunref++;
		}
	}

	/*
	 * Scan all sockets previously marked as dead. Remove
	 * the `unp_gcrefs' reference each socket holds on any
	 * dead socket in its buffer.
	 */
	LIST_FOREACH(unp, &unp_head, unp_link) {
		if ((unp->unp_gcflags & UNP_GCDEAD) == 0)
			continue;
		so = unp->unp_socket;
		mtx_enter(&so->so_rcv.sb_mtx);
		unp_scan(so->so_rcv.sb_mb, unp_remove_gcrefs);
		mtx_leave(&so->so_rcv.sb_mtx);
	}

	/*
	 * If the dead socket has `unp_gcrefs' reference counter
	 * greater than 0, it can't be unreferenced. Mark it as
	 * alive and increment the `unp_gcrefs' reference for each
	 * dead socket within its buffer. Repeat this until we
	 * have no new alive sockets found.
	 */
	do {
		unp_defer = 0;

		LIST_FOREACH(unp, &unp_head, unp_link) {
			if ((unp->unp_gcflags & UNP_GCDEAD) == 0)
				continue;
			if (unp->unp_gcrefs == 0)
				continue;

			unp->unp_gcflags &= ~UNP_GCDEAD;

			so = unp->unp_socket;
			mtx_enter(&so->so_rcv.sb_mtx);
			unp_scan(so->so_rcv.sb_mb, unp_restore_gcrefs);
			mtx_leave(&so->so_rcv.sb_mtx);

			KASSERT(nunref > 0);
			nunref--;
		}
	} while (unp_defer > 0);

	/*
	 * If there are any unreferenced sockets, then for each dispose
	 * of files in its receive buffer and then close it.
	 */
	if (nunref) {
		LIST_FOREACH(unp, &unp_head, unp_link) {
			if (unp->unp_gcflags & UNP_GCDEAD) {
				struct sockbuf *sb = &unp->unp_socket->so_rcv;
				struct mbuf *m;

				/*
				 * This socket could still be connected
				 * and if so it's `so_rcv' is still
				 * accessible by concurrent PRU_SEND
				 * thread.
				 */

				mtx_enter(&sb->sb_mtx);
				m = sb->sb_mb;
				memset(&sb->sb_startzero, 0,
				    (caddr_t)&sb->sb_endzero -
				    (caddr_t)&sb->sb_startzero);
				sb->sb_timeo_nsecs = INFSLP;
				mtx_leave(&sb->sb_mtx);

				unp_scan(m, unp_discard);
				m_purge(m);
			}
		}
	}

	unp_gcing = 0;
unlock:
	rw_exit_write(&unp_gc_lock);
}

void
unp_dispose(struct mbuf *m)
{

	if (m)
		unp_scan(m, unp_discard);
}

void
unp_scan(struct mbuf *m0, void (*op)(struct fdpass *, int))
{
	struct mbuf *m;
	struct fdpass *rp;
	struct cmsghdr *cm;
	int qfds;

	while (m0) {
		for (m = m0; m; m = m->m_next) {
			if (m->m_type == MT_CONTROL &&
			    m->m_len >= sizeof(*cm)) {
				cm = mtod(m, struct cmsghdr *);
				if (cm->cmsg_level != SOL_SOCKET ||
				    cm->cmsg_type != SCM_RIGHTS)
					continue;
				qfds = (cm->cmsg_len - CMSG_ALIGN(sizeof *cm))
				    / sizeof(struct fdpass);
				if (qfds > 0) {
					rp = (struct fdpass *)CMSG_DATA(cm);
					op(rp, qfds);
				}
				break;		/* XXX, but saves time */
			}
		}
		m0 = m0->m_nextpkt;
	}
}

void
unp_discard(struct fdpass *rp, int nfds)
{
	struct unp_deferral *defer;

	/* copy the file pointers to a deferral structure */
	defer = malloc(sizeof(*defer) + sizeof(*rp) * nfds, M_TEMP, M_WAITOK);
	defer->ud_n = nfds;
	memcpy(&defer->ud_fp[0], rp, sizeof(*rp) * nfds);
	memset(rp, 0, sizeof(*rp) * nfds);

	rw_enter_write(&unp_df_lock);
	SLIST_INSERT_HEAD(&unp_deferred, defer, ud_link);
	rw_exit_write(&unp_df_lock);

	task_add(systqmp, &unp_gc_task);
}

void
unp_remove_gcrefs(struct fdpass *rp, int nfds)
{
	struct unpcb *unp;
	int i;

	rw_assert_wrlock(&unp_gc_lock);

	for (i = 0; i < nfds; i++) {
		if (rp[i].fp == NULL)
			continue;
		if ((unp = fptounp(rp[i].fp)) == NULL)
			continue;
		if (unp->unp_gcflags & UNP_GCDEAD) {
			KASSERT(unp->unp_gcrefs > 0);
			unp->unp_gcrefs--;
		}
	}
}

void
unp_restore_gcrefs(struct fdpass *rp, int nfds)
{
	struct unpcb *unp;
	int i;

	rw_assert_wrlock(&unp_gc_lock);

	for (i = 0; i < nfds; i++) {
		if (rp[i].fp == NULL)
			continue;
		if ((unp = fptounp(rp[i].fp)) == NULL)
			continue;
		if (unp->unp_gcflags & UNP_GCDEAD) {
			unp->unp_gcrefs++;
			unp_defer++;
		}
	}
}

int
unp_nam2sun(struct mbuf *nam, struct sockaddr_un **sun, size_t *pathlen)
{
	struct sockaddr *sa = mtod(nam, struct sockaddr *);
	size_t size, len;

	if (nam->m_len < offsetof(struct sockaddr, sa_data))
		return EINVAL;
	if (sa->sa_family != AF_UNIX)
		return EAFNOSUPPORT;
	if (sa->sa_len != nam->m_len)
		return EINVAL;
	if (sa->sa_len > sizeof(struct sockaddr_un))
		return EINVAL;
	*sun = (struct sockaddr_un *)sa;

	/* ensure that sun_path is NUL terminated and fits */
	size = (*sun)->sun_len - offsetof(struct sockaddr_un, sun_path);
	len = strnlen((*sun)->sun_path, size);
	if (len == sizeof((*sun)->sun_path))
		return EINVAL;
	if (len == size) {
		if (m_trailingspace(nam) == 0)
			return EINVAL;
		nam->m_len++;
		(*sun)->sun_len++;
		(*sun)->sun_path[len] = '\0';
	}
	if (pathlen != NULL)
		*pathlen = len;

	return 0;
}
