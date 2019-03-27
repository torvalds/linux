/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1988, 1990, 1993
 *	The Regents of the University of California.
 * Copyright (c) 2004 The FreeBSD Foundation
 * Copyright (c) 2004-2008 Robert N. M. Watson
 * All rights reserved.
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

/*
 * Comments on the socket life cycle:
 *
 * soalloc() sets of socket layer state for a socket, called only by
 * socreate() and sonewconn().  Socket layer private.
 *
 * sodealloc() tears down socket layer state for a socket, called only by
 * sofree() and sonewconn().  Socket layer private.
 *
 * pru_attach() associates protocol layer state with an allocated socket;
 * called only once, may fail, aborting socket allocation.  This is called
 * from socreate() and sonewconn().  Socket layer private.
 *
 * pru_detach() disassociates protocol layer state from an attached socket,
 * and will be called exactly once for sockets in which pru_attach() has
 * been successfully called.  If pru_attach() returned an error,
 * pru_detach() will not be called.  Socket layer private.
 *
 * pru_abort() and pru_close() notify the protocol layer that the last
 * consumer of a socket is starting to tear down the socket, and that the
 * protocol should terminate the connection.  Historically, pru_abort() also
 * detached protocol state from the socket state, but this is no longer the
 * case.
 *
 * socreate() creates a socket and attaches protocol state.  This is a public
 * interface that may be used by socket layer consumers to create new
 * sockets.
 *
 * sonewconn() creates a socket and attaches protocol state.  This is a
 * public interface  that may be used by protocols to create new sockets when
 * a new connection is received and will be available for accept() on a
 * listen socket.
 *
 * soclose() destroys a socket after possibly waiting for it to disconnect.
 * This is a public interface that socket consumers should use to close and
 * release a socket when done with it.
 *
 * soabort() destroys a socket without waiting for it to disconnect (used
 * only for incoming connections that are already partially or fully
 * connected).  This is used internally by the socket layer when clearing
 * listen socket queues (due to overflow or close on the listen socket), but
 * is also a public interface protocols may use to abort connections in
 * their incomplete listen queues should they no longer be required.  Sockets
 * placed in completed connection listen queues should not be aborted for
 * reasons described in the comment above the soclose() implementation.  This
 * is not a general purpose close routine, and except in the specific
 * circumstances described here, should not be used.
 *
 * sofree() will free a socket and its protocol state if all references on
 * the socket have been released, and is the public interface to attempt to
 * free a socket when a reference is removed.  This is a socket layer private
 * interface.
 *
 * NOTE: In addition to socreate() and soclose(), which provide a single
 * socket reference to the consumer to be managed as required, there are two
 * calls to explicitly manage socket references, soref(), and sorele().
 * Currently, these are generally required only when transitioning a socket
 * from a listen queue to a file descriptor, in order to prevent garbage
 * collection of the socket at an untimely moment.  For a number of reasons,
 * these interfaces are not preferred, and should be avoided.
 *
 * NOTE: With regard to VNETs the general rule is that callers do not set
 * curvnet. Exceptions to this rule include soabort(), sodisconnect(),
 * sofree() (and with that sorele(), sotryfree()), as well as sonewconn()
 * and sorflush(), which are usually called from a pre-set VNET context.
 * sopoll() currently does not need a VNET context to be set.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_sctp.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/fcntl.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/mac.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/domain.h>
#include <sys/file.h>			/* for struct knote */
#include <sys/hhook.h>
#include <sys/kernel.h>
#include <sys/khelp.h>
#include <sys/event.h>
#include <sys/eventhandler.h>
#include <sys/poll.h>
#include <sys/proc.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/resourcevar.h>
#include <net/route.h>
#include <sys/signalvar.h>
#include <sys/stat.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>
#include <sys/uio.h>
#include <sys/jail.h>
#include <sys/syslog.h>
#include <netinet/in.h>

#include <net/vnet.h>

#include <security/mac/mac_framework.h>

#include <vm/uma.h>

#ifdef COMPAT_FREEBSD32
#include <sys/mount.h>
#include <sys/sysent.h>
#include <compat/freebsd32/freebsd32.h>
#endif

static int	soreceive_rcvoob(struct socket *so, struct uio *uio,
		    int flags);
static void	so_rdknl_lock(void *);
static void	so_rdknl_unlock(void *);
static void	so_rdknl_assert_locked(void *);
static void	so_rdknl_assert_unlocked(void *);
static void	so_wrknl_lock(void *);
static void	so_wrknl_unlock(void *);
static void	so_wrknl_assert_locked(void *);
static void	so_wrknl_assert_unlocked(void *);

static void	filt_sordetach(struct knote *kn);
static int	filt_soread(struct knote *kn, long hint);
static void	filt_sowdetach(struct knote *kn);
static int	filt_sowrite(struct knote *kn, long hint);
static int	filt_soempty(struct knote *kn, long hint);
static int inline hhook_run_socket(struct socket *so, void *hctx, int32_t h_id);
fo_kqfilter_t	soo_kqfilter;

static struct filterops soread_filtops = {
	.f_isfd = 1,
	.f_detach = filt_sordetach,
	.f_event = filt_soread,
};
static struct filterops sowrite_filtops = {
	.f_isfd = 1,
	.f_detach = filt_sowdetach,
	.f_event = filt_sowrite,
};
static struct filterops soempty_filtops = {
	.f_isfd = 1,
	.f_detach = filt_sowdetach,
	.f_event = filt_soempty,
};

so_gen_t	so_gencnt;	/* generation count for sockets */

MALLOC_DEFINE(M_SONAME, "soname", "socket name");
MALLOC_DEFINE(M_PCB, "pcb", "protocol control block");

#define	VNET_SO_ASSERT(so)						\
	VNET_ASSERT(curvnet != NULL,					\
	    ("%s:%d curvnet is NULL, so=%p", __func__, __LINE__, (so)));

VNET_DEFINE(struct hhook_head *, socket_hhh[HHOOK_SOCKET_LAST + 1]);
#define	V_socket_hhh		VNET(socket_hhh)

/*
 * Limit on the number of connections in the listen queue waiting
 * for accept(2).
 * NB: The original sysctl somaxconn is still available but hidden
 * to prevent confusion about the actual purpose of this number.
 */
static u_int somaxconn = SOMAXCONN;

static int
sysctl_somaxconn(SYSCTL_HANDLER_ARGS)
{
	int error;
	int val;

	val = somaxconn;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error || !req->newptr )
		return (error);

	/*
	 * The purpose of the UINT_MAX / 3 limit, is so that the formula
	 *   3 * so_qlimit / 2
	 * below, will not overflow.
         */

	if (val < 1 || val > UINT_MAX / 3)
		return (EINVAL);

	somaxconn = val;
	return (0);
}
SYSCTL_PROC(_kern_ipc, OID_AUTO, soacceptqueue, CTLTYPE_UINT | CTLFLAG_RW,
    0, sizeof(int), sysctl_somaxconn, "I",
    "Maximum listen socket pending connection accept queue size");
SYSCTL_PROC(_kern_ipc, KIPC_SOMAXCONN, somaxconn,
    CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_SKIP,
    0, sizeof(int), sysctl_somaxconn, "I",
    "Maximum listen socket pending connection accept queue size (compat)");

static int numopensockets;
SYSCTL_INT(_kern_ipc, OID_AUTO, numopensockets, CTLFLAG_RD,
    &numopensockets, 0, "Number of open sockets");

/*
 * accept_mtx locks down per-socket fields relating to accept queues.  See
 * socketvar.h for an annotation of the protected fields of struct socket.
 */
struct mtx accept_mtx;
MTX_SYSINIT(accept_mtx, &accept_mtx, "accept", MTX_DEF);

/*
 * so_global_mtx protects so_gencnt, numopensockets, and the per-socket
 * so_gencnt field.
 */
static struct mtx so_global_mtx;
MTX_SYSINIT(so_global_mtx, &so_global_mtx, "so_glabel", MTX_DEF);

/*
 * General IPC sysctl name space, used by sockets and a variety of other IPC
 * types.
 */
SYSCTL_NODE(_kern, KERN_IPC, ipc, CTLFLAG_RW, 0, "IPC");

/*
 * Initialize the socket subsystem and set up the socket
 * memory allocator.
 */
static uma_zone_t socket_zone;
int	maxsockets;

static void
socket_zone_change(void *tag)
{

	maxsockets = uma_zone_set_max(socket_zone, maxsockets);
}

static void
socket_hhook_register(int subtype)
{
	
	if (hhook_head_register(HHOOK_TYPE_SOCKET, subtype,
	    &V_socket_hhh[subtype],
	    HHOOK_NOWAIT|HHOOK_HEADISINVNET) != 0)
		printf("%s: WARNING: unable to register hook\n", __func__);
}

static void
socket_hhook_deregister(int subtype)
{
	
	if (hhook_head_deregister(V_socket_hhh[subtype]) != 0)
		printf("%s: WARNING: unable to deregister hook\n", __func__);
}

static void
socket_init(void *tag)
{

	socket_zone = uma_zcreate("socket", sizeof(struct socket), NULL, NULL,
	    NULL, NULL, UMA_ALIGN_PTR, UMA_ZONE_NOFREE);
	maxsockets = uma_zone_set_max(socket_zone, maxsockets);
	uma_zone_set_warning(socket_zone, "kern.ipc.maxsockets limit reached");
	EVENTHANDLER_REGISTER(maxsockets_change, socket_zone_change, NULL,
	    EVENTHANDLER_PRI_FIRST);
}
SYSINIT(socket, SI_SUB_PROTO_DOMAININIT, SI_ORDER_ANY, socket_init, NULL);

static void
socket_vnet_init(const void *unused __unused)
{
	int i;

	/* We expect a contiguous range */
	for (i = 0; i <= HHOOK_SOCKET_LAST; i++)
		socket_hhook_register(i);
}
VNET_SYSINIT(socket_vnet_init, SI_SUB_PROTO_DOMAININIT, SI_ORDER_ANY,
    socket_vnet_init, NULL);

static void
socket_vnet_uninit(const void *unused __unused)
{
	int i;

	for (i = 0; i <= HHOOK_SOCKET_LAST; i++)
		socket_hhook_deregister(i);
}
VNET_SYSUNINIT(socket_vnet_uninit, SI_SUB_PROTO_DOMAININIT, SI_ORDER_ANY,
    socket_vnet_uninit, NULL);

/*
 * Initialise maxsockets.  This SYSINIT must be run after
 * tunable_mbinit().
 */
static void
init_maxsockets(void *ignored)
{

	TUNABLE_INT_FETCH("kern.ipc.maxsockets", &maxsockets);
	maxsockets = imax(maxsockets, maxfiles);
}
SYSINIT(param, SI_SUB_TUNABLES, SI_ORDER_ANY, init_maxsockets, NULL);

/*
 * Sysctl to get and set the maximum global sockets limit.  Notify protocols
 * of the change so that they can update their dependent limits as required.
 */
static int
sysctl_maxsockets(SYSCTL_HANDLER_ARGS)
{
	int error, newmaxsockets;

	newmaxsockets = maxsockets;
	error = sysctl_handle_int(oidp, &newmaxsockets, 0, req);
	if (error == 0 && req->newptr) {
		if (newmaxsockets > maxsockets &&
		    newmaxsockets <= maxfiles) {
			maxsockets = newmaxsockets;
			EVENTHANDLER_INVOKE(maxsockets_change);
		} else
			error = EINVAL;
	}
	return (error);
}
SYSCTL_PROC(_kern_ipc, OID_AUTO, maxsockets, CTLTYPE_INT|CTLFLAG_RW,
    &maxsockets, 0, sysctl_maxsockets, "IU",
    "Maximum number of sockets available");

/*
 * Socket operation routines.  These routines are called by the routines in
 * sys_socket.c or from a system process, and implement the semantics of
 * socket operations by switching out to the protocol specific routines.
 */

/*
 * Get a socket structure from our zone, and initialize it.  Note that it
 * would probably be better to allocate socket and PCB at the same time, but
 * I'm not convinced that all the protocols can be easily modified to do
 * this.
 *
 * soalloc() returns a socket with a ref count of 0.
 */
static struct socket *
soalloc(struct vnet *vnet)
{
	struct socket *so;

	so = uma_zalloc(socket_zone, M_NOWAIT | M_ZERO);
	if (so == NULL)
		return (NULL);
#ifdef MAC
	if (mac_socket_init(so, M_NOWAIT) != 0) {
		uma_zfree(socket_zone, so);
		return (NULL);
	}
#endif
	if (khelp_init_osd(HELPER_CLASS_SOCKET, &so->osd)) {
		uma_zfree(socket_zone, so);
		return (NULL);
	}

	/*
	 * The socket locking protocol allows to lock 2 sockets at a time,
	 * however, the first one must be a listening socket.  WITNESS lacks
	 * a feature to change class of an existing lock, so we use DUPOK.
	 */
	mtx_init(&so->so_lock, "socket", NULL, MTX_DEF | MTX_DUPOK);
	SOCKBUF_LOCK_INIT(&so->so_snd, "so_snd");
	SOCKBUF_LOCK_INIT(&so->so_rcv, "so_rcv");
	so->so_rcv.sb_sel = &so->so_rdsel;
	so->so_snd.sb_sel = &so->so_wrsel;
	sx_init(&so->so_snd.sb_sx, "so_snd_sx");
	sx_init(&so->so_rcv.sb_sx, "so_rcv_sx");
	TAILQ_INIT(&so->so_snd.sb_aiojobq);
	TAILQ_INIT(&so->so_rcv.sb_aiojobq);
	TASK_INIT(&so->so_snd.sb_aiotask, 0, soaio_snd, so);
	TASK_INIT(&so->so_rcv.sb_aiotask, 0, soaio_rcv, so);
#ifdef VIMAGE
	VNET_ASSERT(vnet != NULL, ("%s:%d vnet is NULL, so=%p",
	    __func__, __LINE__, so));
	so->so_vnet = vnet;
#endif
	/* We shouldn't need the so_global_mtx */
	if (hhook_run_socket(so, NULL, HHOOK_SOCKET_CREATE)) {
		/* Do we need more comprehensive error returns? */
		uma_zfree(socket_zone, so);
		return (NULL);
	}
	mtx_lock(&so_global_mtx);
	so->so_gencnt = ++so_gencnt;
	++numopensockets;
#ifdef VIMAGE
	vnet->vnet_sockcnt++;
#endif
	mtx_unlock(&so_global_mtx);

	return (so);
}

/*
 * Free the storage associated with a socket at the socket layer, tear down
 * locks, labels, etc.  All protocol state is assumed already to have been
 * torn down (and possibly never set up) by the caller.
 */
static void
sodealloc(struct socket *so)
{

	KASSERT(so->so_count == 0, ("sodealloc(): so_count %d", so->so_count));
	KASSERT(so->so_pcb == NULL, ("sodealloc(): so_pcb != NULL"));

	mtx_lock(&so_global_mtx);
	so->so_gencnt = ++so_gencnt;
	--numopensockets;	/* Could be below, but faster here. */
#ifdef VIMAGE
	VNET_ASSERT(so->so_vnet != NULL, ("%s:%d so_vnet is NULL, so=%p",
	    __func__, __LINE__, so));
	so->so_vnet->vnet_sockcnt--;
#endif
	mtx_unlock(&so_global_mtx);
#ifdef MAC
	mac_socket_destroy(so);
#endif
	hhook_run_socket(so, NULL, HHOOK_SOCKET_CLOSE);

	crfree(so->so_cred);
	khelp_destroy_osd(&so->osd);
	if (SOLISTENING(so)) {
		if (so->sol_accept_filter != NULL)
			accept_filt_setopt(so, NULL);
	} else {
		if (so->so_rcv.sb_hiwat)
			(void)chgsbsize(so->so_cred->cr_uidinfo,
			    &so->so_rcv.sb_hiwat, 0, RLIM_INFINITY);
		if (so->so_snd.sb_hiwat)
			(void)chgsbsize(so->so_cred->cr_uidinfo,
			    &so->so_snd.sb_hiwat, 0, RLIM_INFINITY);
		sx_destroy(&so->so_snd.sb_sx);
		sx_destroy(&so->so_rcv.sb_sx);
		SOCKBUF_LOCK_DESTROY(&so->so_snd);
		SOCKBUF_LOCK_DESTROY(&so->so_rcv);
	}
	mtx_destroy(&so->so_lock);
	uma_zfree(socket_zone, so);
}

/*
 * socreate returns a socket with a ref count of 1.  The socket should be
 * closed with soclose().
 */
int
socreate(int dom, struct socket **aso, int type, int proto,
    struct ucred *cred, struct thread *td)
{
	struct protosw *prp;
	struct socket *so;
	int error;

	if (proto)
		prp = pffindproto(dom, proto, type);
	else
		prp = pffindtype(dom, type);

	if (prp == NULL) {
		/* No support for domain. */
		if (pffinddomain(dom) == NULL)
			return (EAFNOSUPPORT);
		/* No support for socket type. */
		if (proto == 0 && type != 0)
			return (EPROTOTYPE);
		return (EPROTONOSUPPORT);
	}
	if (prp->pr_usrreqs->pru_attach == NULL ||
	    prp->pr_usrreqs->pru_attach == pru_attach_notsupp)
		return (EPROTONOSUPPORT);

	if (prison_check_af(cred, prp->pr_domain->dom_family) != 0)
		return (EPROTONOSUPPORT);

	if (prp->pr_type != type)
		return (EPROTOTYPE);
	so = soalloc(CRED_TO_VNET(cred));
	if (so == NULL)
		return (ENOBUFS);

	so->so_type = type;
	so->so_cred = crhold(cred);
	if ((prp->pr_domain->dom_family == PF_INET) ||
	    (prp->pr_domain->dom_family == PF_INET6) ||
	    (prp->pr_domain->dom_family == PF_ROUTE))
		so->so_fibnum = td->td_proc->p_fibnum;
	else
		so->so_fibnum = 0;
	so->so_proto = prp;
#ifdef MAC
	mac_socket_create(cred, so);
#endif
	knlist_init(&so->so_rdsel.si_note, so, so_rdknl_lock, so_rdknl_unlock,
	    so_rdknl_assert_locked, so_rdknl_assert_unlocked);
	knlist_init(&so->so_wrsel.si_note, so, so_wrknl_lock, so_wrknl_unlock,
	    so_wrknl_assert_locked, so_wrknl_assert_unlocked);
	/*
	 * Auto-sizing of socket buffers is managed by the protocols and
	 * the appropriate flags must be set in the pru_attach function.
	 */
	CURVNET_SET(so->so_vnet);
	error = (*prp->pr_usrreqs->pru_attach)(so, proto, td);
	CURVNET_RESTORE();
	if (error) {
		sodealloc(so);
		return (error);
	}
	soref(so);
	*aso = so;
	return (0);
}

#ifdef REGRESSION
static int regression_sonewconn_earlytest = 1;
SYSCTL_INT(_regression, OID_AUTO, sonewconn_earlytest, CTLFLAG_RW,
    &regression_sonewconn_earlytest, 0, "Perform early sonewconn limit test");
#endif

/*
 * When an attempt at a new connection is noted on a socket which accepts
 * connections, sonewconn is called.  If the connection is possible (subject
 * to space constraints, etc.) then we allocate a new structure, properly
 * linked into the data structure of the original socket, and return this.
 * Connstatus may be 0, or SS_ISCONFIRMING, or SS_ISCONNECTED.
 *
 * Note: the ref count on the socket is 0 on return.
 */
struct socket *
sonewconn(struct socket *head, int connstatus)
{
	static struct timeval lastover;
	static struct timeval overinterval = { 60, 0 };
	static int overcount;

	struct socket *so;
	u_int over;

	SOLISTEN_LOCK(head);
	over = (head->sol_qlen > 3 * head->sol_qlimit / 2);
	SOLISTEN_UNLOCK(head);
#ifdef REGRESSION
	if (regression_sonewconn_earlytest && over) {
#else
	if (over) {
#endif
		overcount++;

		if (ratecheck(&lastover, &overinterval)) {
			log(LOG_DEBUG, "%s: pcb %p: Listen queue overflow: "
			    "%i already in queue awaiting acceptance "
			    "(%d occurrences)\n",
			    __func__, head->so_pcb, head->sol_qlen, overcount);

			overcount = 0;
		}

		return (NULL);
	}
	VNET_ASSERT(head->so_vnet != NULL, ("%s: so %p vnet is NULL",
	    __func__, head));
	so = soalloc(head->so_vnet);
	if (so == NULL) {
		log(LOG_DEBUG, "%s: pcb %p: New socket allocation failure: "
		    "limit reached or out of memory\n",
		    __func__, head->so_pcb);
		return (NULL);
	}
	so->so_listen = head;
	so->so_type = head->so_type;
	so->so_linger = head->so_linger;
	so->so_state = head->so_state | SS_NOFDREF;
	so->so_fibnum = head->so_fibnum;
	so->so_proto = head->so_proto;
	so->so_cred = crhold(head->so_cred);
#ifdef MAC
	mac_socket_newconn(head, so);
#endif
	knlist_init(&so->so_rdsel.si_note, so, so_rdknl_lock, so_rdknl_unlock,
	    so_rdknl_assert_locked, so_rdknl_assert_unlocked);
	knlist_init(&so->so_wrsel.si_note, so, so_wrknl_lock, so_wrknl_unlock,
	    so_wrknl_assert_locked, so_wrknl_assert_unlocked);
	VNET_SO_ASSERT(head);
	if (soreserve(so, head->sol_sbsnd_hiwat, head->sol_sbrcv_hiwat)) {
		sodealloc(so);
		log(LOG_DEBUG, "%s: pcb %p: soreserve() failed\n",
		    __func__, head->so_pcb);
		return (NULL);
	}
	if ((*so->so_proto->pr_usrreqs->pru_attach)(so, 0, NULL)) {
		sodealloc(so);
		log(LOG_DEBUG, "%s: pcb %p: pru_attach() failed\n",
		    __func__, head->so_pcb);
		return (NULL);
	}
	so->so_rcv.sb_lowat = head->sol_sbrcv_lowat;
	so->so_snd.sb_lowat = head->sol_sbsnd_lowat;
	so->so_rcv.sb_timeo = head->sol_sbrcv_timeo;
	so->so_snd.sb_timeo = head->sol_sbsnd_timeo;
	so->so_rcv.sb_flags |= head->sol_sbrcv_flags & SB_AUTOSIZE;
	so->so_snd.sb_flags |= head->sol_sbsnd_flags & SB_AUTOSIZE;

	SOLISTEN_LOCK(head);
	if (head->sol_accept_filter != NULL)
		connstatus = 0;
	so->so_state |= connstatus;
	so->so_options = head->so_options & ~SO_ACCEPTCONN;
	soref(head); /* A socket on (in)complete queue refs head. */
	if (connstatus) {
		TAILQ_INSERT_TAIL(&head->sol_comp, so, so_list);
		so->so_qstate = SQ_COMP;
		head->sol_qlen++;
		solisten_wakeup(head);	/* unlocks */
	} else {
		/*
		 * Keep removing sockets from the head until there's room for
		 * us to insert on the tail.  In pre-locking revisions, this
		 * was a simple if(), but as we could be racing with other
		 * threads and soabort() requires dropping locks, we must
		 * loop waiting for the condition to be true.
		 */
		while (head->sol_incqlen > head->sol_qlimit) {
			struct socket *sp;

			sp = TAILQ_FIRST(&head->sol_incomp);
			TAILQ_REMOVE(&head->sol_incomp, sp, so_list);
			head->sol_incqlen--;
			SOCK_LOCK(sp);
			sp->so_qstate = SQ_NONE;
			sp->so_listen = NULL;
			SOCK_UNLOCK(sp);
			sorele(head);	/* does SOLISTEN_UNLOCK, head stays */
			soabort(sp);
			SOLISTEN_LOCK(head);
		}
		TAILQ_INSERT_TAIL(&head->sol_incomp, so, so_list);
		so->so_qstate = SQ_INCOMP;
		head->sol_incqlen++;
		SOLISTEN_UNLOCK(head);
	}
	return (so);
}

#ifdef SCTP
/*
 * Socket part of sctp_peeloff().  Detach a new socket from an
 * association.  The new socket is returned with a reference.
 */
struct socket *
sopeeloff(struct socket *head)
{
	struct socket *so;

	VNET_ASSERT(head->so_vnet != NULL, ("%s:%d so_vnet is NULL, head=%p",
	    __func__, __LINE__, head));
	so = soalloc(head->so_vnet);
	if (so == NULL) {
		log(LOG_DEBUG, "%s: pcb %p: New socket allocation failure: "
		    "limit reached or out of memory\n",
		    __func__, head->so_pcb);
		return (NULL);
	}
	so->so_type = head->so_type;
	so->so_options = head->so_options;
	so->so_linger = head->so_linger;
	so->so_state = (head->so_state & SS_NBIO) | SS_ISCONNECTED;
	so->so_fibnum = head->so_fibnum;
	so->so_proto = head->so_proto;
	so->so_cred = crhold(head->so_cred);
#ifdef MAC
	mac_socket_newconn(head, so);
#endif
	knlist_init(&so->so_rdsel.si_note, so, so_rdknl_lock, so_rdknl_unlock,
	    so_rdknl_assert_locked, so_rdknl_assert_unlocked);
	knlist_init(&so->so_wrsel.si_note, so, so_wrknl_lock, so_wrknl_unlock,
	    so_wrknl_assert_locked, so_wrknl_assert_unlocked);
	VNET_SO_ASSERT(head);
	if (soreserve(so, head->so_snd.sb_hiwat, head->so_rcv.sb_hiwat)) {
		sodealloc(so);
		log(LOG_DEBUG, "%s: pcb %p: soreserve() failed\n",
		    __func__, head->so_pcb);
		return (NULL);
	}
	if ((*so->so_proto->pr_usrreqs->pru_attach)(so, 0, NULL)) {
		sodealloc(so);
		log(LOG_DEBUG, "%s: pcb %p: pru_attach() failed\n",
		    __func__, head->so_pcb);
		return (NULL);
	}
	so->so_rcv.sb_lowat = head->so_rcv.sb_lowat;
	so->so_snd.sb_lowat = head->so_snd.sb_lowat;
	so->so_rcv.sb_timeo = head->so_rcv.sb_timeo;
	so->so_snd.sb_timeo = head->so_snd.sb_timeo;
	so->so_rcv.sb_flags |= head->so_rcv.sb_flags & SB_AUTOSIZE;
	so->so_snd.sb_flags |= head->so_snd.sb_flags & SB_AUTOSIZE;

	soref(so);

	return (so);
}
#endif	/* SCTP */

int
sobind(struct socket *so, struct sockaddr *nam, struct thread *td)
{
	int error;

	CURVNET_SET(so->so_vnet);
	error = (*so->so_proto->pr_usrreqs->pru_bind)(so, nam, td);
	CURVNET_RESTORE();
	return (error);
}

int
sobindat(int fd, struct socket *so, struct sockaddr *nam, struct thread *td)
{
	int error;

	CURVNET_SET(so->so_vnet);
	error = (*so->so_proto->pr_usrreqs->pru_bindat)(fd, so, nam, td);
	CURVNET_RESTORE();
	return (error);
}

/*
 * solisten() transitions a socket from a non-listening state to a listening
 * state, but can also be used to update the listen queue depth on an
 * existing listen socket.  The protocol will call back into the sockets
 * layer using solisten_proto_check() and solisten_proto() to check and set
 * socket-layer listen state.  Call backs are used so that the protocol can
 * acquire both protocol and socket layer locks in whatever order is required
 * by the protocol.
 *
 * Protocol implementors are advised to hold the socket lock across the
 * socket-layer test and set to avoid races at the socket layer.
 */
int
solisten(struct socket *so, int backlog, struct thread *td)
{
	int error;

	CURVNET_SET(so->so_vnet);
	error = (*so->so_proto->pr_usrreqs->pru_listen)(so, backlog, td);
	CURVNET_RESTORE();
	return (error);
}

int
solisten_proto_check(struct socket *so)
{

	SOCK_LOCK_ASSERT(so);

	if (so->so_state & (SS_ISCONNECTED | SS_ISCONNECTING |
	    SS_ISDISCONNECTING))
		return (EINVAL);
	return (0);
}

void
solisten_proto(struct socket *so, int backlog)
{
	int sbrcv_lowat, sbsnd_lowat;
	u_int sbrcv_hiwat, sbsnd_hiwat;
	short sbrcv_flags, sbsnd_flags;
	sbintime_t sbrcv_timeo, sbsnd_timeo;

	SOCK_LOCK_ASSERT(so);

	if (SOLISTENING(so))
		goto listening;

	/*
	 * Change this socket to listening state.
	 */
	sbrcv_lowat = so->so_rcv.sb_lowat;
	sbsnd_lowat = so->so_snd.sb_lowat;
	sbrcv_hiwat = so->so_rcv.sb_hiwat;
	sbsnd_hiwat = so->so_snd.sb_hiwat;
	sbrcv_flags = so->so_rcv.sb_flags;
	sbsnd_flags = so->so_snd.sb_flags;
	sbrcv_timeo = so->so_rcv.sb_timeo;
	sbsnd_timeo = so->so_snd.sb_timeo;

	sbdestroy(&so->so_snd, so);
	sbdestroy(&so->so_rcv, so);
	sx_destroy(&so->so_snd.sb_sx);
	sx_destroy(&so->so_rcv.sb_sx);
	SOCKBUF_LOCK_DESTROY(&so->so_snd);
	SOCKBUF_LOCK_DESTROY(&so->so_rcv);

#ifdef INVARIANTS
	bzero(&so->so_rcv,
	    sizeof(struct socket) - offsetof(struct socket, so_rcv));
#endif

	so->sol_sbrcv_lowat = sbrcv_lowat;
	so->sol_sbsnd_lowat = sbsnd_lowat;
	so->sol_sbrcv_hiwat = sbrcv_hiwat;
	so->sol_sbsnd_hiwat = sbsnd_hiwat;
	so->sol_sbrcv_flags = sbrcv_flags;
	so->sol_sbsnd_flags = sbsnd_flags;
	so->sol_sbrcv_timeo = sbrcv_timeo;
	so->sol_sbsnd_timeo = sbsnd_timeo;

	so->sol_qlen = so->sol_incqlen = 0;
	TAILQ_INIT(&so->sol_incomp);
	TAILQ_INIT(&so->sol_comp);

	so->sol_accept_filter = NULL;
	so->sol_accept_filter_arg = NULL;
	so->sol_accept_filter_str = NULL;

	so->sol_upcall = NULL;
	so->sol_upcallarg = NULL;

	so->so_options |= SO_ACCEPTCONN;

listening:
	if (backlog < 0 || backlog > somaxconn)
		backlog = somaxconn;
	so->sol_qlimit = backlog;
}

/*
 * Wakeup listeners/subsystems once we have a complete connection.
 * Enters with lock, returns unlocked.
 */
void
solisten_wakeup(struct socket *sol)
{

	if (sol->sol_upcall != NULL)
		(void )sol->sol_upcall(sol, sol->sol_upcallarg, M_NOWAIT);
	else {
		selwakeuppri(&sol->so_rdsel, PSOCK);
		KNOTE_LOCKED(&sol->so_rdsel.si_note, 0);
	}
	SOLISTEN_UNLOCK(sol);
	wakeup_one(&sol->sol_comp);
	if ((sol->so_state & SS_ASYNC) && sol->so_sigio != NULL)
		pgsigio(&sol->so_sigio, SIGIO, 0);
}

/*
 * Return single connection off a listening socket queue.  Main consumer of
 * the function is kern_accept4().  Some modules, that do their own accept
 * management also use the function.
 *
 * Listening socket must be locked on entry and is returned unlocked on
 * return.
 * The flags argument is set of accept4(2) flags and ACCEPT4_INHERIT.
 */
int
solisten_dequeue(struct socket *head, struct socket **ret, int flags)
{
	struct socket *so;
	int error;

	SOLISTEN_LOCK_ASSERT(head);

	while (!(head->so_state & SS_NBIO) && TAILQ_EMPTY(&head->sol_comp) &&
	    head->so_error == 0) {
		error = msleep(&head->sol_comp, &head->so_lock, PSOCK | PCATCH,
		    "accept", 0);
		if (error != 0) {
			SOLISTEN_UNLOCK(head);
			return (error);
		}
	}
	if (head->so_error) {
		error = head->so_error;
		head->so_error = 0;
	} else if ((head->so_state & SS_NBIO) && TAILQ_EMPTY(&head->sol_comp))
		error = EWOULDBLOCK;
	else
		error = 0;
	if (error) {
		SOLISTEN_UNLOCK(head);
		return (error);
	}
	so = TAILQ_FIRST(&head->sol_comp);
	SOCK_LOCK(so);
	KASSERT(so->so_qstate == SQ_COMP,
	    ("%s: so %p not SQ_COMP", __func__, so));
	soref(so);
	head->sol_qlen--;
	so->so_qstate = SQ_NONE;
	so->so_listen = NULL;
	TAILQ_REMOVE(&head->sol_comp, so, so_list);
	if (flags & ACCEPT4_INHERIT)
		so->so_state |= (head->so_state & SS_NBIO);
	else
		so->so_state |= (flags & SOCK_NONBLOCK) ? SS_NBIO : 0;
	SOCK_UNLOCK(so);
	sorele(head);

	*ret = so;
	return (0);
}

/*
 * Evaluate the reference count and named references on a socket; if no
 * references remain, free it.  This should be called whenever a reference is
 * released, such as in sorele(), but also when named reference flags are
 * cleared in socket or protocol code.
 *
 * sofree() will free the socket if:
 *
 * - There are no outstanding file descriptor references or related consumers
 *   (so_count == 0).
 *
 * - The socket has been closed by user space, if ever open (SS_NOFDREF).
 *
 * - The protocol does not have an outstanding strong reference on the socket
 *   (SS_PROTOREF).
 *
 * - The socket is not in a completed connection queue, so a process has been
 *   notified that it is present.  If it is removed, the user process may
 *   block in accept() despite select() saying the socket was ready.
 */
void
sofree(struct socket *so)
{
	struct protosw *pr = so->so_proto;

	SOCK_LOCK_ASSERT(so);

	if ((so->so_state & SS_NOFDREF) == 0 || so->so_count != 0 ||
	    (so->so_state & SS_PROTOREF) || (so->so_qstate == SQ_COMP)) {
		SOCK_UNLOCK(so);
		return;
	}

	if (!SOLISTENING(so) && so->so_qstate == SQ_INCOMP) {
		struct socket *sol;

		sol = so->so_listen;
		KASSERT(sol, ("%s: so %p on incomp of NULL", __func__, so));

		/*
		 * To solve race between close of a listening socket and
		 * a socket on its incomplete queue, we need to lock both.
		 * The order is first listening socket, then regular.
		 * Since we don't have SS_NOFDREF neither SS_PROTOREF, this
		 * function and the listening socket are the only pointers
		 * to so.  To preserve so and sol, we reference both and then
		 * relock.
		 * After relock the socket may not move to so_comp since it
		 * doesn't have PCB already, but it may be removed from
		 * so_incomp. If that happens, we share responsiblity on
		 * freeing the socket, but soclose() has already removed
		 * it from queue.
		 */
		soref(sol);
		soref(so);
		SOCK_UNLOCK(so);
		SOLISTEN_LOCK(sol);
		SOCK_LOCK(so);
		if (so->so_qstate == SQ_INCOMP) {
			KASSERT(so->so_listen == sol,
			    ("%s: so %p migrated out of sol %p",
			    __func__, so, sol));
			TAILQ_REMOVE(&sol->sol_incomp, so, so_list);
			sol->sol_incqlen--;
			/* This is guarenteed not to be the last. */
			refcount_release(&sol->so_count);
			so->so_qstate = SQ_NONE;
			so->so_listen = NULL;
		} else
			KASSERT(so->so_listen == NULL,
			    ("%s: so %p not on (in)comp with so_listen",
			    __func__, so));
		sorele(sol);
		KASSERT(so->so_count == 1,
		    ("%s: so %p count %u", __func__, so, so->so_count));
		so->so_count = 0;
	}
	if (SOLISTENING(so))
		so->so_error = ECONNABORTED;
	SOCK_UNLOCK(so);

	if (so->so_dtor != NULL)
		so->so_dtor(so);

	VNET_SO_ASSERT(so);
	if (pr->pr_flags & PR_RIGHTS && pr->pr_domain->dom_dispose != NULL)
		(*pr->pr_domain->dom_dispose)(so);
	if (pr->pr_usrreqs->pru_detach != NULL)
		(*pr->pr_usrreqs->pru_detach)(so);

	/*
	 * From this point on, we assume that no other references to this
	 * socket exist anywhere else in the stack.  Therefore, no locks need
	 * to be acquired or held.
	 *
	 * We used to do a lot of socket buffer and socket locking here, as
	 * well as invoke sorflush() and perform wakeups.  The direct call to
	 * dom_dispose() and sbrelease_internal() are an inlining of what was
	 * necessary from sorflush().
	 *
	 * Notice that the socket buffer and kqueue state are torn down
	 * before calling pru_detach.  This means that protocols shold not
	 * assume they can perform socket wakeups, etc, in their detach code.
	 */
	if (!SOLISTENING(so)) {
		sbdestroy(&so->so_snd, so);
		sbdestroy(&so->so_rcv, so);
	}
	seldrain(&so->so_rdsel);
	seldrain(&so->so_wrsel);
	knlist_destroy(&so->so_rdsel.si_note);
	knlist_destroy(&so->so_wrsel.si_note);
	sodealloc(so);
}

/*
 * Close a socket on last file table reference removal.  Initiate disconnect
 * if connected.  Free socket when disconnect complete.
 *
 * This function will sorele() the socket.  Note that soclose() may be called
 * prior to the ref count reaching zero.  The actual socket structure will
 * not be freed until the ref count reaches zero.
 */
int
soclose(struct socket *so)
{
	struct accept_queue lqueue;
	bool listening;
	int error = 0;

	KASSERT(!(so->so_state & SS_NOFDREF), ("soclose: SS_NOFDREF on enter"));

	CURVNET_SET(so->so_vnet);
	funsetown(&so->so_sigio);
	if (so->so_state & SS_ISCONNECTED) {
		if ((so->so_state & SS_ISDISCONNECTING) == 0) {
			error = sodisconnect(so);
			if (error) {
				if (error == ENOTCONN)
					error = 0;
				goto drop;
			}
		}
		if (so->so_options & SO_LINGER) {
			if ((so->so_state & SS_ISDISCONNECTING) &&
			    (so->so_state & SS_NBIO))
				goto drop;
			while (so->so_state & SS_ISCONNECTED) {
				error = tsleep(&so->so_timeo,
				    PSOCK | PCATCH, "soclos",
				    so->so_linger * hz);
				if (error)
					break;
			}
		}
	}

drop:
	if (so->so_proto->pr_usrreqs->pru_close != NULL)
		(*so->so_proto->pr_usrreqs->pru_close)(so);

	SOCK_LOCK(so);
	if ((listening = (so->so_options & SO_ACCEPTCONN))) {
		struct socket *sp;

		TAILQ_INIT(&lqueue);
		TAILQ_SWAP(&lqueue, &so->sol_incomp, socket, so_list);
		TAILQ_CONCAT(&lqueue, &so->sol_comp, so_list);

		so->sol_qlen = so->sol_incqlen = 0;

		TAILQ_FOREACH(sp, &lqueue, so_list) {
			SOCK_LOCK(sp);
			sp->so_qstate = SQ_NONE;
			sp->so_listen = NULL;
			SOCK_UNLOCK(sp);
			/* Guaranteed not to be the last. */
			refcount_release(&so->so_count);
		}
	}
	KASSERT((so->so_state & SS_NOFDREF) == 0, ("soclose: NOFDREF"));
	so->so_state |= SS_NOFDREF;
	sorele(so);
	if (listening) {
		struct socket *sp;

		TAILQ_FOREACH(sp, &lqueue, so_list) {
			SOCK_LOCK(sp);
			if (sp->so_count == 0) {
				SOCK_UNLOCK(sp);
				soabort(sp);
			} else
				/* sp is now in sofree() */
				SOCK_UNLOCK(sp);
		}
	}
	CURVNET_RESTORE();
	return (error);
}

/*
 * soabort() is used to abruptly tear down a connection, such as when a
 * resource limit is reached (listen queue depth exceeded), or if a listen
 * socket is closed while there are sockets waiting to be accepted.
 *
 * This interface is tricky, because it is called on an unreferenced socket,
 * and must be called only by a thread that has actually removed the socket
 * from the listen queue it was on, or races with other threads are risked.
 *
 * This interface will call into the protocol code, so must not be called
 * with any socket locks held.  Protocols do call it while holding their own
 * recursible protocol mutexes, but this is something that should be subject
 * to review in the future.
 */
void
soabort(struct socket *so)
{

	/*
	 * In as much as is possible, assert that no references to this
	 * socket are held.  This is not quite the same as asserting that the
	 * current thread is responsible for arranging for no references, but
	 * is as close as we can get for now.
	 */
	KASSERT(so->so_count == 0, ("soabort: so_count"));
	KASSERT((so->so_state & SS_PROTOREF) == 0, ("soabort: SS_PROTOREF"));
	KASSERT(so->so_state & SS_NOFDREF, ("soabort: !SS_NOFDREF"));
	VNET_SO_ASSERT(so);

	if (so->so_proto->pr_usrreqs->pru_abort != NULL)
		(*so->so_proto->pr_usrreqs->pru_abort)(so);
	SOCK_LOCK(so);
	sofree(so);
}

int
soaccept(struct socket *so, struct sockaddr **nam)
{
	int error;

	SOCK_LOCK(so);
	KASSERT((so->so_state & SS_NOFDREF) != 0, ("soaccept: !NOFDREF"));
	so->so_state &= ~SS_NOFDREF;
	SOCK_UNLOCK(so);

	CURVNET_SET(so->so_vnet);
	error = (*so->so_proto->pr_usrreqs->pru_accept)(so, nam);
	CURVNET_RESTORE();
	return (error);
}

int
soconnect(struct socket *so, struct sockaddr *nam, struct thread *td)
{

	return (soconnectat(AT_FDCWD, so, nam, td));
}

int
soconnectat(int fd, struct socket *so, struct sockaddr *nam, struct thread *td)
{
	int error;

	if (so->so_options & SO_ACCEPTCONN)
		return (EOPNOTSUPP);

	CURVNET_SET(so->so_vnet);
	/*
	 * If protocol is connection-based, can only connect once.
	 * Otherwise, if connected, try to disconnect first.  This allows
	 * user to disconnect by connecting to, e.g., a null address.
	 */
	if (so->so_state & (SS_ISCONNECTED|SS_ISCONNECTING) &&
	    ((so->so_proto->pr_flags & PR_CONNREQUIRED) ||
	    (error = sodisconnect(so)))) {
		error = EISCONN;
	} else {
		/*
		 * Prevent accumulated error from previous connection from
		 * biting us.
		 */
		so->so_error = 0;
		if (fd == AT_FDCWD) {
			error = (*so->so_proto->pr_usrreqs->pru_connect)(so,
			    nam, td);
		} else {
			error = (*so->so_proto->pr_usrreqs->pru_connectat)(fd,
			    so, nam, td);
		}
	}
	CURVNET_RESTORE();

	return (error);
}

int
soconnect2(struct socket *so1, struct socket *so2)
{
	int error;

	CURVNET_SET(so1->so_vnet);
	error = (*so1->so_proto->pr_usrreqs->pru_connect2)(so1, so2);
	CURVNET_RESTORE();
	return (error);
}

int
sodisconnect(struct socket *so)
{
	int error;

	if ((so->so_state & SS_ISCONNECTED) == 0)
		return (ENOTCONN);
	if (so->so_state & SS_ISDISCONNECTING)
		return (EALREADY);
	VNET_SO_ASSERT(so);
	error = (*so->so_proto->pr_usrreqs->pru_disconnect)(so);
	return (error);
}

#define	SBLOCKWAIT(f)	(((f) & MSG_DONTWAIT) ? 0 : SBL_WAIT)

int
sosend_dgram(struct socket *so, struct sockaddr *addr, struct uio *uio,
    struct mbuf *top, struct mbuf *control, int flags, struct thread *td)
{
	long space;
	ssize_t resid;
	int clen = 0, error, dontroute;

	KASSERT(so->so_type == SOCK_DGRAM, ("sosend_dgram: !SOCK_DGRAM"));
	KASSERT(so->so_proto->pr_flags & PR_ATOMIC,
	    ("sosend_dgram: !PR_ATOMIC"));

	if (uio != NULL)
		resid = uio->uio_resid;
	else
		resid = top->m_pkthdr.len;
	/*
	 * In theory resid should be unsigned.  However, space must be
	 * signed, as it might be less than 0 if we over-committed, and we
	 * must use a signed comparison of space and resid.  On the other
	 * hand, a negative resid causes us to loop sending 0-length
	 * segments to the protocol.
	 */
	if (resid < 0) {
		error = EINVAL;
		goto out;
	}

	dontroute =
	    (flags & MSG_DONTROUTE) && (so->so_options & SO_DONTROUTE) == 0;
	if (td != NULL)
		td->td_ru.ru_msgsnd++;
	if (control != NULL)
		clen = control->m_len;

	SOCKBUF_LOCK(&so->so_snd);
	if (so->so_snd.sb_state & SBS_CANTSENDMORE) {
		SOCKBUF_UNLOCK(&so->so_snd);
		error = EPIPE;
		goto out;
	}
	if (so->so_error) {
		error = so->so_error;
		so->so_error = 0;
		SOCKBUF_UNLOCK(&so->so_snd);
		goto out;
	}
	if ((so->so_state & SS_ISCONNECTED) == 0) {
		/*
		 * `sendto' and `sendmsg' is allowed on a connection-based
		 * socket if it supports implied connect.  Return ENOTCONN if
		 * not connected and no address is supplied.
		 */
		if ((so->so_proto->pr_flags & PR_CONNREQUIRED) &&
		    (so->so_proto->pr_flags & PR_IMPLOPCL) == 0) {
			if ((so->so_state & SS_ISCONFIRMING) == 0 &&
			    !(resid == 0 && clen != 0)) {
				SOCKBUF_UNLOCK(&so->so_snd);
				error = ENOTCONN;
				goto out;
			}
		} else if (addr == NULL) {
			if (so->so_proto->pr_flags & PR_CONNREQUIRED)
				error = ENOTCONN;
			else
				error = EDESTADDRREQ;
			SOCKBUF_UNLOCK(&so->so_snd);
			goto out;
		}
	}

	/*
	 * Do we need MSG_OOB support in SOCK_DGRAM?  Signs here may be a
	 * problem and need fixing.
	 */
	space = sbspace(&so->so_snd);
	if (flags & MSG_OOB)
		space += 1024;
	space -= clen;
	SOCKBUF_UNLOCK(&so->so_snd);
	if (resid > space) {
		error = EMSGSIZE;
		goto out;
	}
	if (uio == NULL) {
		resid = 0;
		if (flags & MSG_EOR)
			top->m_flags |= M_EOR;
	} else {
		/*
		 * Copy the data from userland into a mbuf chain.
		 * If no data is to be copied in, a single empty mbuf
		 * is returned.
		 */
		top = m_uiotombuf(uio, M_WAITOK, space, max_hdr,
		    (M_PKTHDR | ((flags & MSG_EOR) ? M_EOR : 0)));
		if (top == NULL) {
			error = EFAULT;	/* only possible error */
			goto out;
		}
		space -= resid - uio->uio_resid;
		resid = uio->uio_resid;
	}
	KASSERT(resid == 0, ("sosend_dgram: resid != 0"));
	/*
	 * XXXRW: Frobbing SO_DONTROUTE here is even worse without sblock
	 * than with.
	 */
	if (dontroute) {
		SOCK_LOCK(so);
		so->so_options |= SO_DONTROUTE;
		SOCK_UNLOCK(so);
	}
	/*
	 * XXX all the SBS_CANTSENDMORE checks previously done could be out
	 * of date.  We could have received a reset packet in an interrupt or
	 * maybe we slept while doing page faults in uiomove() etc.  We could
	 * probably recheck again inside the locking protection here, but
	 * there are probably other places that this also happens.  We must
	 * rethink this.
	 */
	VNET_SO_ASSERT(so);
	error = (*so->so_proto->pr_usrreqs->pru_send)(so,
	    (flags & MSG_OOB) ? PRUS_OOB :
	/*
	 * If the user set MSG_EOF, the protocol understands this flag and
	 * nothing left to send then use PRU_SEND_EOF instead of PRU_SEND.
	 */
	    ((flags & MSG_EOF) &&
	     (so->so_proto->pr_flags & PR_IMPLOPCL) &&
	     (resid <= 0)) ?
		PRUS_EOF :
		/* If there is more to send set PRUS_MORETOCOME */
		(flags & MSG_MORETOCOME) ||
		(resid > 0 && space > 0) ? PRUS_MORETOCOME : 0,
		top, addr, control, td);
	if (dontroute) {
		SOCK_LOCK(so);
		so->so_options &= ~SO_DONTROUTE;
		SOCK_UNLOCK(so);
	}
	clen = 0;
	control = NULL;
	top = NULL;
out:
	if (top != NULL)
		m_freem(top);
	if (control != NULL)
		m_freem(control);
	return (error);
}

/*
 * Send on a socket.  If send must go all at once and message is larger than
 * send buffering, then hard error.  Lock against other senders.  If must go
 * all at once and not enough room now, then inform user that this would
 * block and do nothing.  Otherwise, if nonblocking, send as much as
 * possible.  The data to be sent is described by "uio" if nonzero, otherwise
 * by the mbuf chain "top" (which must be null if uio is not).  Data provided
 * in mbuf chain must be small enough to send all at once.
 *
 * Returns nonzero on error, timeout or signal; callers must check for short
 * counts if EINTR/ERESTART are returned.  Data and control buffers are freed
 * on return.
 */
int
sosend_generic(struct socket *so, struct sockaddr *addr, struct uio *uio,
    struct mbuf *top, struct mbuf *control, int flags, struct thread *td)
{
	long space;
	ssize_t resid;
	int clen = 0, error, dontroute;
	int atomic = sosendallatonce(so) || top;

	if (uio != NULL)
		resid = uio->uio_resid;
	else
		resid = top->m_pkthdr.len;
	/*
	 * In theory resid should be unsigned.  However, space must be
	 * signed, as it might be less than 0 if we over-committed, and we
	 * must use a signed comparison of space and resid.  On the other
	 * hand, a negative resid causes us to loop sending 0-length
	 * segments to the protocol.
	 *
	 * Also check to make sure that MSG_EOR isn't used on SOCK_STREAM
	 * type sockets since that's an error.
	 */
	if (resid < 0 || (so->so_type == SOCK_STREAM && (flags & MSG_EOR))) {
		error = EINVAL;
		goto out;
	}

	dontroute =
	    (flags & MSG_DONTROUTE) && (so->so_options & SO_DONTROUTE) == 0 &&
	    (so->so_proto->pr_flags & PR_ATOMIC);
	if (td != NULL)
		td->td_ru.ru_msgsnd++;
	if (control != NULL)
		clen = control->m_len;

	error = sblock(&so->so_snd, SBLOCKWAIT(flags));
	if (error)
		goto out;

restart:
	do {
		SOCKBUF_LOCK(&so->so_snd);
		if (so->so_snd.sb_state & SBS_CANTSENDMORE) {
			SOCKBUF_UNLOCK(&so->so_snd);
			error = EPIPE;
			goto release;
		}
		if (so->so_error) {
			error = so->so_error;
			so->so_error = 0;
			SOCKBUF_UNLOCK(&so->so_snd);
			goto release;
		}
		if ((so->so_state & SS_ISCONNECTED) == 0) {
			/*
			 * `sendto' and `sendmsg' is allowed on a connection-
			 * based socket if it supports implied connect.
			 * Return ENOTCONN if not connected and no address is
			 * supplied.
			 */
			if ((so->so_proto->pr_flags & PR_CONNREQUIRED) &&
			    (so->so_proto->pr_flags & PR_IMPLOPCL) == 0) {
				if ((so->so_state & SS_ISCONFIRMING) == 0 &&
				    !(resid == 0 && clen != 0)) {
					SOCKBUF_UNLOCK(&so->so_snd);
					error = ENOTCONN;
					goto release;
				}
			} else if (addr == NULL) {
				SOCKBUF_UNLOCK(&so->so_snd);
				if (so->so_proto->pr_flags & PR_CONNREQUIRED)
					error = ENOTCONN;
				else
					error = EDESTADDRREQ;
				goto release;
			}
		}
		space = sbspace(&so->so_snd);
		if (flags & MSG_OOB)
			space += 1024;
		if ((atomic && resid > so->so_snd.sb_hiwat) ||
		    clen > so->so_snd.sb_hiwat) {
			SOCKBUF_UNLOCK(&so->so_snd);
			error = EMSGSIZE;
			goto release;
		}
		if (space < resid + clen &&
		    (atomic || space < so->so_snd.sb_lowat || space < clen)) {
			if ((so->so_state & SS_NBIO) ||
			    (flags & (MSG_NBIO | MSG_DONTWAIT)) != 0) {
				SOCKBUF_UNLOCK(&so->so_snd);
				error = EWOULDBLOCK;
				goto release;
			}
			error = sbwait(&so->so_snd);
			SOCKBUF_UNLOCK(&so->so_snd);
			if (error)
				goto release;
			goto restart;
		}
		SOCKBUF_UNLOCK(&so->so_snd);
		space -= clen;
		do {
			if (uio == NULL) {
				resid = 0;
				if (flags & MSG_EOR)
					top->m_flags |= M_EOR;
			} else {
				/*
				 * Copy the data from userland into a mbuf
				 * chain.  If resid is 0, which can happen
				 * only if we have control to send, then
				 * a single empty mbuf is returned.  This
				 * is a workaround to prevent protocol send
				 * methods to panic.
				 */
				top = m_uiotombuf(uio, M_WAITOK, space,
				    (atomic ? max_hdr : 0),
				    (atomic ? M_PKTHDR : 0) |
				    ((flags & MSG_EOR) ? M_EOR : 0));
				if (top == NULL) {
					error = EFAULT; /* only possible error */
					goto release;
				}
				space -= resid - uio->uio_resid;
				resid = uio->uio_resid;
			}
			if (dontroute) {
				SOCK_LOCK(so);
				so->so_options |= SO_DONTROUTE;
				SOCK_UNLOCK(so);
			}
			/*
			 * XXX all the SBS_CANTSENDMORE checks previously
			 * done could be out of date.  We could have received
			 * a reset packet in an interrupt or maybe we slept
			 * while doing page faults in uiomove() etc.  We
			 * could probably recheck again inside the locking
			 * protection here, but there are probably other
			 * places that this also happens.  We must rethink
			 * this.
			 */
			VNET_SO_ASSERT(so);
			error = (*so->so_proto->pr_usrreqs->pru_send)(so,
			    (flags & MSG_OOB) ? PRUS_OOB :
			/*
			 * If the user set MSG_EOF, the protocol understands
			 * this flag and nothing left to send then use
			 * PRU_SEND_EOF instead of PRU_SEND.
			 */
			    ((flags & MSG_EOF) &&
			     (so->so_proto->pr_flags & PR_IMPLOPCL) &&
			     (resid <= 0)) ?
				PRUS_EOF :
			/* If there is more to send set PRUS_MORETOCOME. */
			    (flags & MSG_MORETOCOME) ||
			    (resid > 0 && space > 0) ? PRUS_MORETOCOME : 0,
			    top, addr, control, td);
			if (dontroute) {
				SOCK_LOCK(so);
				so->so_options &= ~SO_DONTROUTE;
				SOCK_UNLOCK(so);
			}
			clen = 0;
			control = NULL;
			top = NULL;
			if (error)
				goto release;
		} while (resid && space > 0);
	} while (resid);

release:
	sbunlock(&so->so_snd);
out:
	if (top != NULL)
		m_freem(top);
	if (control != NULL)
		m_freem(control);
	return (error);
}

int
sosend(struct socket *so, struct sockaddr *addr, struct uio *uio,
    struct mbuf *top, struct mbuf *control, int flags, struct thread *td)
{
	int error;

	CURVNET_SET(so->so_vnet);
	if (!SOLISTENING(so))
		error = so->so_proto->pr_usrreqs->pru_sosend(so, addr, uio,
		    top, control, flags, td);
	else {
		m_freem(top);
		m_freem(control);
		error = ENOTCONN;
	}
	CURVNET_RESTORE();
	return (error);
}

/*
 * The part of soreceive() that implements reading non-inline out-of-band
 * data from a socket.  For more complete comments, see soreceive(), from
 * which this code originated.
 *
 * Note that soreceive_rcvoob(), unlike the remainder of soreceive(), is
 * unable to return an mbuf chain to the caller.
 */
static int
soreceive_rcvoob(struct socket *so, struct uio *uio, int flags)
{
	struct protosw *pr = so->so_proto;
	struct mbuf *m;
	int error;

	KASSERT(flags & MSG_OOB, ("soreceive_rcvoob: (flags & MSG_OOB) == 0"));
	VNET_SO_ASSERT(so);

	m = m_get(M_WAITOK, MT_DATA);
	error = (*pr->pr_usrreqs->pru_rcvoob)(so, m, flags & MSG_PEEK);
	if (error)
		goto bad;
	do {
		error = uiomove(mtod(m, void *),
		    (int) min(uio->uio_resid, m->m_len), uio);
		m = m_free(m);
	} while (uio->uio_resid && error == 0 && m);
bad:
	if (m != NULL)
		m_freem(m);
	return (error);
}

/*
 * Following replacement or removal of the first mbuf on the first mbuf chain
 * of a socket buffer, push necessary state changes back into the socket
 * buffer so that other consumers see the values consistently.  'nextrecord'
 * is the callers locally stored value of the original value of
 * sb->sb_mb->m_nextpkt which must be restored when the lead mbuf changes.
 * NOTE: 'nextrecord' may be NULL.
 */
static __inline void
sockbuf_pushsync(struct sockbuf *sb, struct mbuf *nextrecord)
{

	SOCKBUF_LOCK_ASSERT(sb);
	/*
	 * First, update for the new value of nextrecord.  If necessary, make
	 * it the first record.
	 */
	if (sb->sb_mb != NULL)
		sb->sb_mb->m_nextpkt = nextrecord;
	else
		sb->sb_mb = nextrecord;

	/*
	 * Now update any dependent socket buffer fields to reflect the new
	 * state.  This is an expanded inline of SB_EMPTY_FIXUP(), with the
	 * addition of a second clause that takes care of the case where
	 * sb_mb has been updated, but remains the last record.
	 */
	if (sb->sb_mb == NULL) {
		sb->sb_mbtail = NULL;
		sb->sb_lastrecord = NULL;
	} else if (sb->sb_mb->m_nextpkt == NULL)
		sb->sb_lastrecord = sb->sb_mb;
}

/*
 * Implement receive operations on a socket.  We depend on the way that
 * records are added to the sockbuf by sbappend.  In particular, each record
 * (mbufs linked through m_next) must begin with an address if the protocol
 * so specifies, followed by an optional mbuf or mbufs containing ancillary
 * data, and then zero or more mbufs of data.  In order to allow parallelism
 * between network receive and copying to user space, as well as avoid
 * sleeping with a mutex held, we release the socket buffer mutex during the
 * user space copy.  Although the sockbuf is locked, new data may still be
 * appended, and thus we must maintain consistency of the sockbuf during that
 * time.
 *
 * The caller may receive the data as a single mbuf chain by supplying an
 * mbuf **mp0 for use in returning the chain.  The uio is then used only for
 * the count in uio_resid.
 */
int
soreceive_generic(struct socket *so, struct sockaddr **psa, struct uio *uio,
    struct mbuf **mp0, struct mbuf **controlp, int *flagsp)
{
	struct mbuf *m, **mp;
	int flags, error, offset;
	ssize_t len;
	struct protosw *pr = so->so_proto;
	struct mbuf *nextrecord;
	int moff, type = 0;
	ssize_t orig_resid = uio->uio_resid;

	mp = mp0;
	if (psa != NULL)
		*psa = NULL;
	if (controlp != NULL)
		*controlp = NULL;
	if (flagsp != NULL)
		flags = *flagsp &~ MSG_EOR;
	else
		flags = 0;
	if (flags & MSG_OOB)
		return (soreceive_rcvoob(so, uio, flags));
	if (mp != NULL)
		*mp = NULL;
	if ((pr->pr_flags & PR_WANTRCVD) && (so->so_state & SS_ISCONFIRMING)
	    && uio->uio_resid) {
		VNET_SO_ASSERT(so);
		(*pr->pr_usrreqs->pru_rcvd)(so, 0);
	}

	error = sblock(&so->so_rcv, SBLOCKWAIT(flags));
	if (error)
		return (error);

restart:
	SOCKBUF_LOCK(&so->so_rcv);
	m = so->so_rcv.sb_mb;
	/*
	 * If we have less data than requested, block awaiting more (subject
	 * to any timeout) if:
	 *   1. the current count is less than the low water mark, or
	 *   2. MSG_DONTWAIT is not set
	 */
	if (m == NULL || (((flags & MSG_DONTWAIT) == 0 &&
	    sbavail(&so->so_rcv) < uio->uio_resid) &&
	    sbavail(&so->so_rcv) < so->so_rcv.sb_lowat &&
	    m->m_nextpkt == NULL && (pr->pr_flags & PR_ATOMIC) == 0)) {
		KASSERT(m != NULL || !sbavail(&so->so_rcv),
		    ("receive: m == %p sbavail == %u",
		    m, sbavail(&so->so_rcv)));
		if (so->so_error) {
			if (m != NULL)
				goto dontblock;
			error = so->so_error;
			if ((flags & MSG_PEEK) == 0)
				so->so_error = 0;
			SOCKBUF_UNLOCK(&so->so_rcv);
			goto release;
		}
		SOCKBUF_LOCK_ASSERT(&so->so_rcv);
		if (so->so_rcv.sb_state & SBS_CANTRCVMORE) {
			if (m == NULL) {
				SOCKBUF_UNLOCK(&so->so_rcv);
				goto release;
			} else
				goto dontblock;
		}
		for (; m != NULL; m = m->m_next)
			if (m->m_type == MT_OOBDATA  || (m->m_flags & M_EOR)) {
				m = so->so_rcv.sb_mb;
				goto dontblock;
			}
		if ((so->so_state & (SS_ISCONNECTED|SS_ISCONNECTING)) == 0 &&
		    (so->so_proto->pr_flags & PR_CONNREQUIRED)) {
			SOCKBUF_UNLOCK(&so->so_rcv);
			error = ENOTCONN;
			goto release;
		}
		if (uio->uio_resid == 0) {
			SOCKBUF_UNLOCK(&so->so_rcv);
			goto release;
		}
		if ((so->so_state & SS_NBIO) ||
		    (flags & (MSG_DONTWAIT|MSG_NBIO))) {
			SOCKBUF_UNLOCK(&so->so_rcv);
			error = EWOULDBLOCK;
			goto release;
		}
		SBLASTRECORDCHK(&so->so_rcv);
		SBLASTMBUFCHK(&so->so_rcv);
		error = sbwait(&so->so_rcv);
		SOCKBUF_UNLOCK(&so->so_rcv);
		if (error)
			goto release;
		goto restart;
	}
dontblock:
	/*
	 * From this point onward, we maintain 'nextrecord' as a cache of the
	 * pointer to the next record in the socket buffer.  We must keep the
	 * various socket buffer pointers and local stack versions of the
	 * pointers in sync, pushing out modifications before dropping the
	 * socket buffer mutex, and re-reading them when picking it up.
	 *
	 * Otherwise, we will race with the network stack appending new data
	 * or records onto the socket buffer by using inconsistent/stale
	 * versions of the field, possibly resulting in socket buffer
	 * corruption.
	 *
	 * By holding the high-level sblock(), we prevent simultaneous
	 * readers from pulling off the front of the socket buffer.
	 */
	SOCKBUF_LOCK_ASSERT(&so->so_rcv);
	if (uio->uio_td)
		uio->uio_td->td_ru.ru_msgrcv++;
	KASSERT(m == so->so_rcv.sb_mb, ("soreceive: m != so->so_rcv.sb_mb"));
	SBLASTRECORDCHK(&so->so_rcv);
	SBLASTMBUFCHK(&so->so_rcv);
	nextrecord = m->m_nextpkt;
	if (pr->pr_flags & PR_ADDR) {
		KASSERT(m->m_type == MT_SONAME,
		    ("m->m_type == %d", m->m_type));
		orig_resid = 0;
		if (psa != NULL)
			*psa = sodupsockaddr(mtod(m, struct sockaddr *),
			    M_NOWAIT);
		if (flags & MSG_PEEK) {
			m = m->m_next;
		} else {
			sbfree(&so->so_rcv, m);
			so->so_rcv.sb_mb = m_free(m);
			m = so->so_rcv.sb_mb;
			sockbuf_pushsync(&so->so_rcv, nextrecord);
		}
	}

	/*
	 * Process one or more MT_CONTROL mbufs present before any data mbufs
	 * in the first mbuf chain on the socket buffer.  If MSG_PEEK, we
	 * just copy the data; if !MSG_PEEK, we call into the protocol to
	 * perform externalization (or freeing if controlp == NULL).
	 */
	if (m != NULL && m->m_type == MT_CONTROL) {
		struct mbuf *cm = NULL, *cmn;
		struct mbuf **cme = &cm;

		do {
			if (flags & MSG_PEEK) {
				if (controlp != NULL) {
					*controlp = m_copym(m, 0, m->m_len,
					    M_NOWAIT);
					controlp = &(*controlp)->m_next;
				}
				m = m->m_next;
			} else {
				sbfree(&so->so_rcv, m);
				so->so_rcv.sb_mb = m->m_next;
				m->m_next = NULL;
				*cme = m;
				cme = &(*cme)->m_next;
				m = so->so_rcv.sb_mb;
			}
		} while (m != NULL && m->m_type == MT_CONTROL);
		if ((flags & MSG_PEEK) == 0)
			sockbuf_pushsync(&so->so_rcv, nextrecord);
		while (cm != NULL) {
			cmn = cm->m_next;
			cm->m_next = NULL;
			if (pr->pr_domain->dom_externalize != NULL) {
				SOCKBUF_UNLOCK(&so->so_rcv);
				VNET_SO_ASSERT(so);
				error = (*pr->pr_domain->dom_externalize)
				    (cm, controlp, flags);
				SOCKBUF_LOCK(&so->so_rcv);
			} else if (controlp != NULL)
				*controlp = cm;
			else
				m_freem(cm);
			if (controlp != NULL) {
				orig_resid = 0;
				while (*controlp != NULL)
					controlp = &(*controlp)->m_next;
			}
			cm = cmn;
		}
		if (m != NULL)
			nextrecord = so->so_rcv.sb_mb->m_nextpkt;
		else
			nextrecord = so->so_rcv.sb_mb;
		orig_resid = 0;
	}
	if (m != NULL) {
		if ((flags & MSG_PEEK) == 0) {
			KASSERT(m->m_nextpkt == nextrecord,
			    ("soreceive: post-control, nextrecord !sync"));
			if (nextrecord == NULL) {
				KASSERT(so->so_rcv.sb_mb == m,
				    ("soreceive: post-control, sb_mb!=m"));
				KASSERT(so->so_rcv.sb_lastrecord == m,
				    ("soreceive: post-control, lastrecord!=m"));
			}
		}
		type = m->m_type;
		if (type == MT_OOBDATA)
			flags |= MSG_OOB;
	} else {
		if ((flags & MSG_PEEK) == 0) {
			KASSERT(so->so_rcv.sb_mb == nextrecord,
			    ("soreceive: sb_mb != nextrecord"));
			if (so->so_rcv.sb_mb == NULL) {
				KASSERT(so->so_rcv.sb_lastrecord == NULL,
				    ("soreceive: sb_lastercord != NULL"));
			}
		}
	}
	SOCKBUF_LOCK_ASSERT(&so->so_rcv);
	SBLASTRECORDCHK(&so->so_rcv);
	SBLASTMBUFCHK(&so->so_rcv);

	/*
	 * Now continue to read any data mbufs off of the head of the socket
	 * buffer until the read request is satisfied.  Note that 'type' is
	 * used to store the type of any mbuf reads that have happened so far
	 * such that soreceive() can stop reading if the type changes, which
	 * causes soreceive() to return only one of regular data and inline
	 * out-of-band data in a single socket receive operation.
	 */
	moff = 0;
	offset = 0;
	while (m != NULL && !(m->m_flags & M_NOTAVAIL) && uio->uio_resid > 0
	    && error == 0) {
		/*
		 * If the type of mbuf has changed since the last mbuf
		 * examined ('type'), end the receive operation.
		 */
		SOCKBUF_LOCK_ASSERT(&so->so_rcv);
		if (m->m_type == MT_OOBDATA || m->m_type == MT_CONTROL) {
			if (type != m->m_type)
				break;
		} else if (type == MT_OOBDATA)
			break;
		else
		    KASSERT(m->m_type == MT_DATA,
			("m->m_type == %d", m->m_type));
		so->so_rcv.sb_state &= ~SBS_RCVATMARK;
		len = uio->uio_resid;
		if (so->so_oobmark && len > so->so_oobmark - offset)
			len = so->so_oobmark - offset;
		if (len > m->m_len - moff)
			len = m->m_len - moff;
		/*
		 * If mp is set, just pass back the mbufs.  Otherwise copy
		 * them out via the uio, then free.  Sockbuf must be
		 * consistent here (points to current mbuf, it points to next
		 * record) when we drop priority; we must note any additions
		 * to the sockbuf when we block interrupts again.
		 */
		if (mp == NULL) {
			SOCKBUF_LOCK_ASSERT(&so->so_rcv);
			SBLASTRECORDCHK(&so->so_rcv);
			SBLASTMBUFCHK(&so->so_rcv);
			SOCKBUF_UNLOCK(&so->so_rcv);
			error = uiomove(mtod(m, char *) + moff, (int)len, uio);
			SOCKBUF_LOCK(&so->so_rcv);
			if (error) {
				/*
				 * The MT_SONAME mbuf has already been removed
				 * from the record, so it is necessary to
				 * remove the data mbufs, if any, to preserve
				 * the invariant in the case of PR_ADDR that
				 * requires MT_SONAME mbufs at the head of
				 * each record.
				 */
				if (pr->pr_flags & PR_ATOMIC &&
				    ((flags & MSG_PEEK) == 0))
					(void)sbdroprecord_locked(&so->so_rcv);
				SOCKBUF_UNLOCK(&so->so_rcv);
				goto release;
			}
		} else
			uio->uio_resid -= len;
		SOCKBUF_LOCK_ASSERT(&so->so_rcv);
		if (len == m->m_len - moff) {
			if (m->m_flags & M_EOR)
				flags |= MSG_EOR;
			if (flags & MSG_PEEK) {
				m = m->m_next;
				moff = 0;
			} else {
				nextrecord = m->m_nextpkt;
				sbfree(&so->so_rcv, m);
				if (mp != NULL) {
					m->m_nextpkt = NULL;
					*mp = m;
					mp = &m->m_next;
					so->so_rcv.sb_mb = m = m->m_next;
					*mp = NULL;
				} else {
					so->so_rcv.sb_mb = m_free(m);
					m = so->so_rcv.sb_mb;
				}
				sockbuf_pushsync(&so->so_rcv, nextrecord);
				SBLASTRECORDCHK(&so->so_rcv);
				SBLASTMBUFCHK(&so->so_rcv);
			}
		} else {
			if (flags & MSG_PEEK)
				moff += len;
			else {
				if (mp != NULL) {
					if (flags & MSG_DONTWAIT) {
						*mp = m_copym(m, 0, len,
						    M_NOWAIT);
						if (*mp == NULL) {
							/*
							 * m_copym() couldn't
							 * allocate an mbuf.
							 * Adjust uio_resid back
							 * (it was adjusted
							 * down by len bytes,
							 * which we didn't end
							 * up "copying" over).
							 */
							uio->uio_resid += len;
							break;
						}
					} else {
						SOCKBUF_UNLOCK(&so->so_rcv);
						*mp = m_copym(m, 0, len,
						    M_WAITOK);
						SOCKBUF_LOCK(&so->so_rcv);
					}
				}
				sbcut_locked(&so->so_rcv, len);
			}
		}
		SOCKBUF_LOCK_ASSERT(&so->so_rcv);
		if (so->so_oobmark) {
			if ((flags & MSG_PEEK) == 0) {
				so->so_oobmark -= len;
				if (so->so_oobmark == 0) {
					so->so_rcv.sb_state |= SBS_RCVATMARK;
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
		 * If the MSG_WAITALL flag is set (for non-atomic socket), we
		 * must not quit until "uio->uio_resid == 0" or an error
		 * termination.  If a signal/timeout occurs, return with a
		 * short count but without error.  Keep sockbuf locked
		 * against other readers.
		 */
		while (flags & MSG_WAITALL && m == NULL && uio->uio_resid > 0 &&
		    !sosendallatonce(so) && nextrecord == NULL) {
			SOCKBUF_LOCK_ASSERT(&so->so_rcv);
			if (so->so_error ||
			    so->so_rcv.sb_state & SBS_CANTRCVMORE)
				break;
			/*
			 * Notify the protocol that some data has been
			 * drained before blocking.
			 */
			if (pr->pr_flags & PR_WANTRCVD) {
				SOCKBUF_UNLOCK(&so->so_rcv);
				VNET_SO_ASSERT(so);
				(*pr->pr_usrreqs->pru_rcvd)(so, flags);
				SOCKBUF_LOCK(&so->so_rcv);
			}
			SBLASTRECORDCHK(&so->so_rcv);
			SBLASTMBUFCHK(&so->so_rcv);
			/*
			 * We could receive some data while was notifying
			 * the protocol. Skip blocking in this case.
			 */
			if (so->so_rcv.sb_mb == NULL) {
				error = sbwait(&so->so_rcv);
				if (error) {
					SOCKBUF_UNLOCK(&so->so_rcv);
					goto release;
				}
			}
			m = so->so_rcv.sb_mb;
			if (m != NULL)
				nextrecord = m->m_nextpkt;
		}
	}

	SOCKBUF_LOCK_ASSERT(&so->so_rcv);
	if (m != NULL && pr->pr_flags & PR_ATOMIC) {
		flags |= MSG_TRUNC;
		if ((flags & MSG_PEEK) == 0)
			(void) sbdroprecord_locked(&so->so_rcv);
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
		SBLASTRECORDCHK(&so->so_rcv);
		SBLASTMBUFCHK(&so->so_rcv);
		/*
		 * If soreceive() is being done from the socket callback,
		 * then don't need to generate ACK to peer to update window,
		 * since ACK will be generated on return to TCP.
		 */
		if (!(flags & MSG_SOCALLBCK) &&
		    (pr->pr_flags & PR_WANTRCVD)) {
			SOCKBUF_UNLOCK(&so->so_rcv);
			VNET_SO_ASSERT(so);
			(*pr->pr_usrreqs->pru_rcvd)(so, flags);
			SOCKBUF_LOCK(&so->so_rcv);
		}
	}
	SOCKBUF_LOCK_ASSERT(&so->so_rcv);
	if (orig_resid == uio->uio_resid && orig_resid &&
	    (flags & MSG_EOR) == 0 && (so->so_rcv.sb_state & SBS_CANTRCVMORE) == 0) {
		SOCKBUF_UNLOCK(&so->so_rcv);
		goto restart;
	}
	SOCKBUF_UNLOCK(&so->so_rcv);

	if (flagsp != NULL)
		*flagsp |= flags;
release:
	sbunlock(&so->so_rcv);
	return (error);
}

/*
 * Optimized version of soreceive() for stream (TCP) sockets.
 */
int
soreceive_stream(struct socket *so, struct sockaddr **psa, struct uio *uio,
    struct mbuf **mp0, struct mbuf **controlp, int *flagsp)
{
	int len = 0, error = 0, flags, oresid;
	struct sockbuf *sb;
	struct mbuf *m, *n = NULL;

	/* We only do stream sockets. */
	if (so->so_type != SOCK_STREAM)
		return (EINVAL);
	if (psa != NULL)
		*psa = NULL;
	if (flagsp != NULL)
		flags = *flagsp &~ MSG_EOR;
	else
		flags = 0;
	if (controlp != NULL)
		*controlp = NULL;
	if (flags & MSG_OOB)
		return (soreceive_rcvoob(so, uio, flags));
	if (mp0 != NULL)
		*mp0 = NULL;

	sb = &so->so_rcv;

	/* Prevent other readers from entering the socket. */
	error = sblock(sb, SBLOCKWAIT(flags));
	if (error)
		goto out;
	SOCKBUF_LOCK(sb);

	/* Easy one, no space to copyout anything. */
	if (uio->uio_resid == 0) {
		error = EINVAL;
		goto out;
	}
	oresid = uio->uio_resid;

	/* We will never ever get anything unless we are or were connected. */
	if (!(so->so_state & (SS_ISCONNECTED|SS_ISDISCONNECTED))) {
		error = ENOTCONN;
		goto out;
	}

restart:
	SOCKBUF_LOCK_ASSERT(&so->so_rcv);

	/* Abort if socket has reported problems. */
	if (so->so_error) {
		if (sbavail(sb) > 0)
			goto deliver;
		if (oresid > uio->uio_resid)
			goto out;
		error = so->so_error;
		if (!(flags & MSG_PEEK))
			so->so_error = 0;
		goto out;
	}

	/* Door is closed.  Deliver what is left, if any. */
	if (sb->sb_state & SBS_CANTRCVMORE) {
		if (sbavail(sb) > 0)
			goto deliver;
		else
			goto out;
	}

	/* Socket buffer is empty and we shall not block. */
	if (sbavail(sb) == 0 &&
	    ((so->so_state & SS_NBIO) || (flags & (MSG_DONTWAIT|MSG_NBIO)))) {
		error = EAGAIN;
		goto out;
	}

	/* Socket buffer got some data that we shall deliver now. */
	if (sbavail(sb) > 0 && !(flags & MSG_WAITALL) &&
	    ((so->so_state & SS_NBIO) ||
	     (flags & (MSG_DONTWAIT|MSG_NBIO)) ||
	     sbavail(sb) >= sb->sb_lowat ||
	     sbavail(sb) >= uio->uio_resid ||
	     sbavail(sb) >= sb->sb_hiwat) ) {
		goto deliver;
	}

	/* On MSG_WAITALL we must wait until all data or error arrives. */
	if ((flags & MSG_WAITALL) &&
	    (sbavail(sb) >= uio->uio_resid || sbavail(sb) >= sb->sb_hiwat))
		goto deliver;

	/*
	 * Wait and block until (more) data comes in.
	 * NB: Drops the sockbuf lock during wait.
	 */
	error = sbwait(sb);
	if (error)
		goto out;
	goto restart;

deliver:
	SOCKBUF_LOCK_ASSERT(&so->so_rcv);
	KASSERT(sbavail(sb) > 0, ("%s: sockbuf empty", __func__));
	KASSERT(sb->sb_mb != NULL, ("%s: sb_mb == NULL", __func__));

	/* Statistics. */
	if (uio->uio_td)
		uio->uio_td->td_ru.ru_msgrcv++;

	/* Fill uio until full or current end of socket buffer is reached. */
	len = min(uio->uio_resid, sbavail(sb));
	if (mp0 != NULL) {
		/* Dequeue as many mbufs as possible. */
		if (!(flags & MSG_PEEK) && len >= sb->sb_mb->m_len) {
			if (*mp0 == NULL)
				*mp0 = sb->sb_mb;
			else
				m_cat(*mp0, sb->sb_mb);
			for (m = sb->sb_mb;
			     m != NULL && m->m_len <= len;
			     m = m->m_next) {
				KASSERT(!(m->m_flags & M_NOTAVAIL),
				    ("%s: m %p not available", __func__, m));
				len -= m->m_len;
				uio->uio_resid -= m->m_len;
				sbfree(sb, m);
				n = m;
			}
			n->m_next = NULL;
			sb->sb_mb = m;
			sb->sb_lastrecord = sb->sb_mb;
			if (sb->sb_mb == NULL)
				SB_EMPTY_FIXUP(sb);
		}
		/* Copy the remainder. */
		if (len > 0) {
			KASSERT(sb->sb_mb != NULL,
			    ("%s: len > 0 && sb->sb_mb empty", __func__));

			m = m_copym(sb->sb_mb, 0, len, M_NOWAIT);
			if (m == NULL)
				len = 0;	/* Don't flush data from sockbuf. */
			else
				uio->uio_resid -= len;
			if (*mp0 != NULL)
				m_cat(*mp0, m);
			else
				*mp0 = m;
			if (*mp0 == NULL) {
				error = ENOBUFS;
				goto out;
			}
		}
	} else {
		/* NB: Must unlock socket buffer as uiomove may sleep. */
		SOCKBUF_UNLOCK(sb);
		error = m_mbuftouio(uio, sb->sb_mb, len);
		SOCKBUF_LOCK(sb);
		if (error)
			goto out;
	}
	SBLASTRECORDCHK(sb);
	SBLASTMBUFCHK(sb);

	/*
	 * Remove the delivered data from the socket buffer unless we
	 * were only peeking.
	 */
	if (!(flags & MSG_PEEK)) {
		if (len > 0)
			sbdrop_locked(sb, len);

		/* Notify protocol that we drained some data. */
		if ((so->so_proto->pr_flags & PR_WANTRCVD) &&
		    (((flags & MSG_WAITALL) && uio->uio_resid > 0) ||
		     !(flags & MSG_SOCALLBCK))) {
			SOCKBUF_UNLOCK(sb);
			VNET_SO_ASSERT(so);
			(*so->so_proto->pr_usrreqs->pru_rcvd)(so, flags);
			SOCKBUF_LOCK(sb);
		}
	}

	/*
	 * For MSG_WAITALL we may have to loop again and wait for
	 * more data to come in.
	 */
	if ((flags & MSG_WAITALL) && uio->uio_resid > 0)
		goto restart;
out:
	SOCKBUF_LOCK_ASSERT(sb);
	SBLASTRECORDCHK(sb);
	SBLASTMBUFCHK(sb);
	SOCKBUF_UNLOCK(sb);
	sbunlock(sb);
	return (error);
}

/*
 * Optimized version of soreceive() for simple datagram cases from userspace.
 * Unlike in the stream case, we're able to drop a datagram if copyout()
 * fails, and because we handle datagrams atomically, we don't need to use a
 * sleep lock to prevent I/O interlacing.
 */
int
soreceive_dgram(struct socket *so, struct sockaddr **psa, struct uio *uio,
    struct mbuf **mp0, struct mbuf **controlp, int *flagsp)
{
	struct mbuf *m, *m2;
	int flags, error;
	ssize_t len;
	struct protosw *pr = so->so_proto;
	struct mbuf *nextrecord;

	if (psa != NULL)
		*psa = NULL;
	if (controlp != NULL)
		*controlp = NULL;
	if (flagsp != NULL)
		flags = *flagsp &~ MSG_EOR;
	else
		flags = 0;

	/*
	 * For any complicated cases, fall back to the full
	 * soreceive_generic().
	 */
	if (mp0 != NULL || (flags & MSG_PEEK) || (flags & MSG_OOB))
		return (soreceive_generic(so, psa, uio, mp0, controlp,
		    flagsp));

	/*
	 * Enforce restrictions on use.
	 */
	KASSERT((pr->pr_flags & PR_WANTRCVD) == 0,
	    ("soreceive_dgram: wantrcvd"));
	KASSERT(pr->pr_flags & PR_ATOMIC, ("soreceive_dgram: !atomic"));
	KASSERT((so->so_rcv.sb_state & SBS_RCVATMARK) == 0,
	    ("soreceive_dgram: SBS_RCVATMARK"));
	KASSERT((so->so_proto->pr_flags & PR_CONNREQUIRED) == 0,
	    ("soreceive_dgram: P_CONNREQUIRED"));

	/*
	 * Loop blocking while waiting for a datagram.
	 */
	SOCKBUF_LOCK(&so->so_rcv);
	while ((m = so->so_rcv.sb_mb) == NULL) {
		KASSERT(sbavail(&so->so_rcv) == 0,
		    ("soreceive_dgram: sb_mb NULL but sbavail %u",
		    sbavail(&so->so_rcv)));
		if (so->so_error) {
			error = so->so_error;
			so->so_error = 0;
			SOCKBUF_UNLOCK(&so->so_rcv);
			return (error);
		}
		if (so->so_rcv.sb_state & SBS_CANTRCVMORE ||
		    uio->uio_resid == 0) {
			SOCKBUF_UNLOCK(&so->so_rcv);
			return (0);
		}
		if ((so->so_state & SS_NBIO) ||
		    (flags & (MSG_DONTWAIT|MSG_NBIO))) {
			SOCKBUF_UNLOCK(&so->so_rcv);
			return (EWOULDBLOCK);
		}
		SBLASTRECORDCHK(&so->so_rcv);
		SBLASTMBUFCHK(&so->so_rcv);
		error = sbwait(&so->so_rcv);
		if (error) {
			SOCKBUF_UNLOCK(&so->so_rcv);
			return (error);
		}
	}
	SOCKBUF_LOCK_ASSERT(&so->so_rcv);

	if (uio->uio_td)
		uio->uio_td->td_ru.ru_msgrcv++;
	SBLASTRECORDCHK(&so->so_rcv);
	SBLASTMBUFCHK(&so->so_rcv);
	nextrecord = m->m_nextpkt;
	if (nextrecord == NULL) {
		KASSERT(so->so_rcv.sb_lastrecord == m,
		    ("soreceive_dgram: lastrecord != m"));
	}

	KASSERT(so->so_rcv.sb_mb->m_nextpkt == nextrecord,
	    ("soreceive_dgram: m_nextpkt != nextrecord"));

	/*
	 * Pull 'm' and its chain off the front of the packet queue.
	 */
	so->so_rcv.sb_mb = NULL;
	sockbuf_pushsync(&so->so_rcv, nextrecord);

	/*
	 * Walk 'm's chain and free that many bytes from the socket buffer.
	 */
	for (m2 = m; m2 != NULL; m2 = m2->m_next)
		sbfree(&so->so_rcv, m2);

	/*
	 * Do a few last checks before we let go of the lock.
	 */
	SBLASTRECORDCHK(&so->so_rcv);
	SBLASTMBUFCHK(&so->so_rcv);
	SOCKBUF_UNLOCK(&so->so_rcv);

	if (pr->pr_flags & PR_ADDR) {
		KASSERT(m->m_type == MT_SONAME,
		    ("m->m_type == %d", m->m_type));
		if (psa != NULL)
			*psa = sodupsockaddr(mtod(m, struct sockaddr *),
			    M_NOWAIT);
		m = m_free(m);
	}
	if (m == NULL) {
		/* XXXRW: Can this happen? */
		return (0);
	}

	/*
	 * Packet to copyout() is now in 'm' and it is disconnected from the
	 * queue.
	 *
	 * Process one or more MT_CONTROL mbufs present before any data mbufs
	 * in the first mbuf chain on the socket buffer.  We call into the
	 * protocol to perform externalization (or freeing if controlp ==
	 * NULL). In some cases there can be only MT_CONTROL mbufs without
	 * MT_DATA mbufs.
	 */
	if (m->m_type == MT_CONTROL) {
		struct mbuf *cm = NULL, *cmn;
		struct mbuf **cme = &cm;

		do {
			m2 = m->m_next;
			m->m_next = NULL;
			*cme = m;
			cme = &(*cme)->m_next;
			m = m2;
		} while (m != NULL && m->m_type == MT_CONTROL);
		while (cm != NULL) {
			cmn = cm->m_next;
			cm->m_next = NULL;
			if (pr->pr_domain->dom_externalize != NULL) {
				error = (*pr->pr_domain->dom_externalize)
				    (cm, controlp, flags);
			} else if (controlp != NULL)
				*controlp = cm;
			else
				m_freem(cm);
			if (controlp != NULL) {
				while (*controlp != NULL)
					controlp = &(*controlp)->m_next;
			}
			cm = cmn;
		}
	}
	KASSERT(m == NULL || m->m_type == MT_DATA,
	    ("soreceive_dgram: !data"));
	while (m != NULL && uio->uio_resid > 0) {
		len = uio->uio_resid;
		if (len > m->m_len)
			len = m->m_len;
		error = uiomove(mtod(m, char *), (int)len, uio);
		if (error) {
			m_freem(m);
			return (error);
		}
		if (len == m->m_len)
			m = m_free(m);
		else {
			m->m_data += len;
			m->m_len -= len;
		}
	}
	if (m != NULL) {
		flags |= MSG_TRUNC;
		m_freem(m);
	}
	if (flagsp != NULL)
		*flagsp |= flags;
	return (0);
}

int
soreceive(struct socket *so, struct sockaddr **psa, struct uio *uio,
    struct mbuf **mp0, struct mbuf **controlp, int *flagsp)
{
	int error;

	CURVNET_SET(so->so_vnet);
	if (!SOLISTENING(so))
		error = (so->so_proto->pr_usrreqs->pru_soreceive(so, psa, uio,
		    mp0, controlp, flagsp));
	else
		error = ENOTCONN;
	CURVNET_RESTORE();
	return (error);
}

int
soshutdown(struct socket *so, int how)
{
	struct protosw *pr = so->so_proto;
	int error, soerror_enotconn;

	if (!(how == SHUT_RD || how == SHUT_WR || how == SHUT_RDWR))
		return (EINVAL);

	soerror_enotconn = 0;
	if ((so->so_state &
	    (SS_ISCONNECTED | SS_ISCONNECTING | SS_ISDISCONNECTING)) == 0) {
		/*
		 * POSIX mandates us to return ENOTCONN when shutdown(2) is
		 * invoked on a datagram sockets, however historically we would
		 * actually tear socket down. This is known to be leveraged by
		 * some applications to unblock process waiting in recvXXX(2)
		 * by other process that it shares that socket with. Try to meet
		 * both backward-compatibility and POSIX requirements by forcing
		 * ENOTCONN but still asking protocol to perform pru_shutdown().
		 */
		if (so->so_type != SOCK_DGRAM && !SOLISTENING(so))
			return (ENOTCONN);
		soerror_enotconn = 1;
	}

	if (SOLISTENING(so)) {
		if (how != SHUT_WR) {
			SOLISTEN_LOCK(so);
			so->so_error = ECONNABORTED;
			solisten_wakeup(so);	/* unlocks so */
		}
		goto done;
	}

	CURVNET_SET(so->so_vnet);
	if (pr->pr_usrreqs->pru_flush != NULL)
		(*pr->pr_usrreqs->pru_flush)(so, how);
	if (how != SHUT_WR)
		sorflush(so);
	if (how != SHUT_RD) {
		error = (*pr->pr_usrreqs->pru_shutdown)(so);
		wakeup(&so->so_timeo);
		CURVNET_RESTORE();
		return ((error == 0 && soerror_enotconn) ? ENOTCONN : error);
	}
	wakeup(&so->so_timeo);
	CURVNET_RESTORE();

done:
	return (soerror_enotconn ? ENOTCONN : 0);
}

void
sorflush(struct socket *so)
{
	struct sockbuf *sb = &so->so_rcv;
	struct protosw *pr = so->so_proto;
	struct socket aso;

	VNET_SO_ASSERT(so);

	/*
	 * In order to avoid calling dom_dispose with the socket buffer mutex
	 * held, and in order to generally avoid holding the lock for a long
	 * time, we make a copy of the socket buffer and clear the original
	 * (except locks, state).  The new socket buffer copy won't have
	 * initialized locks so we can only call routines that won't use or
	 * assert those locks.
	 *
	 * Dislodge threads currently blocked in receive and wait to acquire
	 * a lock against other simultaneous readers before clearing the
	 * socket buffer.  Don't let our acquire be interrupted by a signal
	 * despite any existing socket disposition on interruptable waiting.
	 */
	socantrcvmore(so);
	(void) sblock(sb, SBL_WAIT | SBL_NOINTR);

	/*
	 * Invalidate/clear most of the sockbuf structure, but leave selinfo
	 * and mutex data unchanged.
	 */
	SOCKBUF_LOCK(sb);
	bzero(&aso, sizeof(aso));
	aso.so_pcb = so->so_pcb;
	bcopy(&sb->sb_startzero, &aso.so_rcv.sb_startzero,
	    sizeof(*sb) - offsetof(struct sockbuf, sb_startzero));
	bzero(&sb->sb_startzero,
	    sizeof(*sb) - offsetof(struct sockbuf, sb_startzero));
	SOCKBUF_UNLOCK(sb);
	sbunlock(sb);

	/*
	 * Dispose of special rights and flush the copied socket.  Don't call
	 * any unsafe routines (that rely on locks being initialized) on aso.
	 */
	if (pr->pr_flags & PR_RIGHTS && pr->pr_domain->dom_dispose != NULL)
		(*pr->pr_domain->dom_dispose)(&aso);
	sbrelease_internal(&aso.so_rcv, so);
}

/*
 * Wrapper for Socket established helper hook.
 * Parameters: socket, context of the hook point, hook id.
 */
static int inline
hhook_run_socket(struct socket *so, void *hctx, int32_t h_id)
{
	struct socket_hhook_data hhook_data = {
		.so = so,
		.hctx = hctx,
		.m = NULL,
		.status = 0
	};

	CURVNET_SET(so->so_vnet);
	HHOOKS_RUN_IF(V_socket_hhh[h_id], &hhook_data, &so->osd);
	CURVNET_RESTORE();

	/* Ugly but needed, since hhooks return void for now */
	return (hhook_data.status);
}

/*
 * Perhaps this routine, and sooptcopyout(), below, ought to come in an
 * additional variant to handle the case where the option value needs to be
 * some kind of integer, but not a specific size.  In addition to their use
 * here, these functions are also called by the protocol-level pr_ctloutput()
 * routines.
 */
int
sooptcopyin(struct sockopt *sopt, void *buf, size_t len, size_t minlen)
{
	size_t	valsize;

	/*
	 * If the user gives us more than we wanted, we ignore it, but if we
	 * don't get the minimum length the caller wants, we return EINVAL.
	 * On success, sopt->sopt_valsize is set to however much we actually
	 * retrieved.
	 */
	if ((valsize = sopt->sopt_valsize) < minlen)
		return EINVAL;
	if (valsize > len)
		sopt->sopt_valsize = valsize = len;

	if (sopt->sopt_td != NULL)
		return (copyin(sopt->sopt_val, buf, valsize));

	bcopy(sopt->sopt_val, buf, valsize);
	return (0);
}

/*
 * Kernel version of setsockopt(2).
 *
 * XXX: optlen is size_t, not socklen_t
 */
int
so_setsockopt(struct socket *so, int level, int optname, void *optval,
    size_t optlen)
{
	struct sockopt sopt;

	sopt.sopt_level = level;
	sopt.sopt_name = optname;
	sopt.sopt_dir = SOPT_SET;
	sopt.sopt_val = optval;
	sopt.sopt_valsize = optlen;
	sopt.sopt_td = NULL;
	return (sosetopt(so, &sopt));
}

int
sosetopt(struct socket *so, struct sockopt *sopt)
{
	int	error, optval;
	struct	linger l;
	struct	timeval tv;
	sbintime_t val;
	uint32_t val32;
#ifdef MAC
	struct mac extmac;
#endif

	CURVNET_SET(so->so_vnet);
	error = 0;
	if (sopt->sopt_level != SOL_SOCKET) {
		if (so->so_proto->pr_ctloutput != NULL)
			error = (*so->so_proto->pr_ctloutput)(so, sopt);
		else
			error = ENOPROTOOPT;
	} else {
		switch (sopt->sopt_name) {
		case SO_ACCEPTFILTER:
			error = accept_filt_setopt(so, sopt);
			if (error)
				goto bad;
			break;

		case SO_LINGER:
			error = sooptcopyin(sopt, &l, sizeof l, sizeof l);
			if (error)
				goto bad;

			SOCK_LOCK(so);
			so->so_linger = l.l_linger;
			if (l.l_onoff)
				so->so_options |= SO_LINGER;
			else
				so->so_options &= ~SO_LINGER;
			SOCK_UNLOCK(so);
			break;

		case SO_DEBUG:
		case SO_KEEPALIVE:
		case SO_DONTROUTE:
		case SO_USELOOPBACK:
		case SO_BROADCAST:
		case SO_REUSEADDR:
		case SO_REUSEPORT:
		case SO_REUSEPORT_LB:
		case SO_OOBINLINE:
		case SO_TIMESTAMP:
		case SO_BINTIME:
		case SO_NOSIGPIPE:
		case SO_NO_DDP:
		case SO_NO_OFFLOAD:
			error = sooptcopyin(sopt, &optval, sizeof optval,
			    sizeof optval);
			if (error)
				goto bad;
			SOCK_LOCK(so);
			if (optval)
				so->so_options |= sopt->sopt_name;
			else
				so->so_options &= ~sopt->sopt_name;
			SOCK_UNLOCK(so);
			break;

		case SO_SETFIB:
			error = sooptcopyin(sopt, &optval, sizeof optval,
			    sizeof optval);
			if (error)
				goto bad;

			if (optval < 0 || optval >= rt_numfibs) {
				error = EINVAL;
				goto bad;
			}
			if (((so->so_proto->pr_domain->dom_family == PF_INET) ||
			   (so->so_proto->pr_domain->dom_family == PF_INET6) ||
			   (so->so_proto->pr_domain->dom_family == PF_ROUTE)))
				so->so_fibnum = optval;
			else
				so->so_fibnum = 0;
			break;

		case SO_USER_COOKIE:
			error = sooptcopyin(sopt, &val32, sizeof val32,
			    sizeof val32);
			if (error)
				goto bad;
			so->so_user_cookie = val32;
			break;

		case SO_SNDBUF:
		case SO_RCVBUF:
		case SO_SNDLOWAT:
		case SO_RCVLOWAT:
			error = sooptcopyin(sopt, &optval, sizeof optval,
			    sizeof optval);
			if (error)
				goto bad;

			/*
			 * Values < 1 make no sense for any of these options,
			 * so disallow them.
			 */
			if (optval < 1) {
				error = EINVAL;
				goto bad;
			}

			error = sbsetopt(so, sopt->sopt_name, optval);
			break;

		case SO_SNDTIMEO:
		case SO_RCVTIMEO:
#ifdef COMPAT_FREEBSD32
			if (SV_CURPROC_FLAG(SV_ILP32)) {
				struct timeval32 tv32;

				error = sooptcopyin(sopt, &tv32, sizeof tv32,
				    sizeof tv32);
				CP(tv32, tv, tv_sec);
				CP(tv32, tv, tv_usec);
			} else
#endif
				error = sooptcopyin(sopt, &tv, sizeof tv,
				    sizeof tv);
			if (error)
				goto bad;
			if (tv.tv_sec < 0 || tv.tv_usec < 0 ||
			    tv.tv_usec >= 1000000) {
				error = EDOM;
				goto bad;
			}
			if (tv.tv_sec > INT32_MAX)
				val = SBT_MAX;
			else
				val = tvtosbt(tv);
			switch (sopt->sopt_name) {
			case SO_SNDTIMEO:
				so->so_snd.sb_timeo = val;
				break;
			case SO_RCVTIMEO:
				so->so_rcv.sb_timeo = val;
				break;
			}
			break;

		case SO_LABEL:
#ifdef MAC
			error = sooptcopyin(sopt, &extmac, sizeof extmac,
			    sizeof extmac);
			if (error)
				goto bad;
			error = mac_setsockopt_label(sopt->sopt_td->td_ucred,
			    so, &extmac);
#else
			error = EOPNOTSUPP;
#endif
			break;

		case SO_TS_CLOCK:
			error = sooptcopyin(sopt, &optval, sizeof optval,
			    sizeof optval);
			if (error)
				goto bad;
			if (optval < 0 || optval > SO_TS_CLOCK_MAX) {
				error = EINVAL;
				goto bad;
			}
			so->so_ts_clock = optval;
			break;

		case SO_MAX_PACING_RATE:
			error = sooptcopyin(sopt, &val32, sizeof(val32),
			    sizeof(val32));
			if (error)
				goto bad;
			so->so_max_pacing_rate = val32;
			break;

		default:
			if (V_socket_hhh[HHOOK_SOCKET_OPT]->hhh_nhooks > 0)
				error = hhook_run_socket(so, sopt,
				    HHOOK_SOCKET_OPT);
			else
				error = ENOPROTOOPT;
			break;
		}
		if (error == 0 && so->so_proto->pr_ctloutput != NULL)
			(void)(*so->so_proto->pr_ctloutput)(so, sopt);
	}
bad:
	CURVNET_RESTORE();
	return (error);
}

/*
 * Helper routine for getsockopt.
 */
int
sooptcopyout(struct sockopt *sopt, const void *buf, size_t len)
{
	int	error;
	size_t	valsize;

	error = 0;

	/*
	 * Documented get behavior is that we always return a value, possibly
	 * truncated to fit in the user's buffer.  Traditional behavior is
	 * that we always tell the user precisely how much we copied, rather
	 * than something useful like the total amount we had available for
	 * her.  Note that this interface is not idempotent; the entire
	 * answer must be generated ahead of time.
	 */
	valsize = min(len, sopt->sopt_valsize);
	sopt->sopt_valsize = valsize;
	if (sopt->sopt_val != NULL) {
		if (sopt->sopt_td != NULL)
			error = copyout(buf, sopt->sopt_val, valsize);
		else
			bcopy(buf, sopt->sopt_val, valsize);
	}
	return (error);
}

int
sogetopt(struct socket *so, struct sockopt *sopt)
{
	int	error, optval;
	struct	linger l;
	struct	timeval tv;
#ifdef MAC
	struct mac extmac;
#endif

	CURVNET_SET(so->so_vnet);
	error = 0;
	if (sopt->sopt_level != SOL_SOCKET) {
		if (so->so_proto->pr_ctloutput != NULL)
			error = (*so->so_proto->pr_ctloutput)(so, sopt);
		else
			error = ENOPROTOOPT;
		CURVNET_RESTORE();
		return (error);
	} else {
		switch (sopt->sopt_name) {
		case SO_ACCEPTFILTER:
			error = accept_filt_getopt(so, sopt);
			break;

		case SO_LINGER:
			SOCK_LOCK(so);
			l.l_onoff = so->so_options & SO_LINGER;
			l.l_linger = so->so_linger;
			SOCK_UNLOCK(so);
			error = sooptcopyout(sopt, &l, sizeof l);
			break;

		case SO_USELOOPBACK:
		case SO_DONTROUTE:
		case SO_DEBUG:
		case SO_KEEPALIVE:
		case SO_REUSEADDR:
		case SO_REUSEPORT:
		case SO_REUSEPORT_LB:
		case SO_BROADCAST:
		case SO_OOBINLINE:
		case SO_ACCEPTCONN:
		case SO_TIMESTAMP:
		case SO_BINTIME:
		case SO_NOSIGPIPE:
			optval = so->so_options & sopt->sopt_name;
integer:
			error = sooptcopyout(sopt, &optval, sizeof optval);
			break;

		case SO_DOMAIN:
			optval = so->so_proto->pr_domain->dom_family;
			goto integer;

		case SO_TYPE:
			optval = so->so_type;
			goto integer;

		case SO_PROTOCOL:
			optval = so->so_proto->pr_protocol;
			goto integer;

		case SO_ERROR:
			SOCK_LOCK(so);
			optval = so->so_error;
			so->so_error = 0;
			SOCK_UNLOCK(so);
			goto integer;

		case SO_SNDBUF:
			optval = SOLISTENING(so) ? so->sol_sbsnd_hiwat :
			    so->so_snd.sb_hiwat;
			goto integer;

		case SO_RCVBUF:
			optval = SOLISTENING(so) ? so->sol_sbrcv_hiwat :
			    so->so_rcv.sb_hiwat;
			goto integer;

		case SO_SNDLOWAT:
			optval = SOLISTENING(so) ? so->sol_sbsnd_lowat :
			    so->so_snd.sb_lowat;
			goto integer;

		case SO_RCVLOWAT:
			optval = SOLISTENING(so) ? so->sol_sbrcv_lowat :
			    so->so_rcv.sb_lowat;
			goto integer;

		case SO_SNDTIMEO:
		case SO_RCVTIMEO:
			tv = sbttotv(sopt->sopt_name == SO_SNDTIMEO ?
			    so->so_snd.sb_timeo : so->so_rcv.sb_timeo);
#ifdef COMPAT_FREEBSD32
			if (SV_CURPROC_FLAG(SV_ILP32)) {
				struct timeval32 tv32;

				CP(tv, tv32, tv_sec);
				CP(tv, tv32, tv_usec);
				error = sooptcopyout(sopt, &tv32, sizeof tv32);
			} else
#endif
				error = sooptcopyout(sopt, &tv, sizeof tv);
			break;

		case SO_LABEL:
#ifdef MAC
			error = sooptcopyin(sopt, &extmac, sizeof(extmac),
			    sizeof(extmac));
			if (error)
				goto bad;
			error = mac_getsockopt_label(sopt->sopt_td->td_ucred,
			    so, &extmac);
			if (error)
				goto bad;
			error = sooptcopyout(sopt, &extmac, sizeof extmac);
#else
			error = EOPNOTSUPP;
#endif
			break;

		case SO_PEERLABEL:
#ifdef MAC
			error = sooptcopyin(sopt, &extmac, sizeof(extmac),
			    sizeof(extmac));
			if (error)
				goto bad;
			error = mac_getsockopt_peerlabel(
			    sopt->sopt_td->td_ucred, so, &extmac);
			if (error)
				goto bad;
			error = sooptcopyout(sopt, &extmac, sizeof extmac);
#else
			error = EOPNOTSUPP;
#endif
			break;

		case SO_LISTENQLIMIT:
			optval = SOLISTENING(so) ? so->sol_qlimit : 0;
			goto integer;

		case SO_LISTENQLEN:
			optval = SOLISTENING(so) ? so->sol_qlen : 0;
			goto integer;

		case SO_LISTENINCQLEN:
			optval = SOLISTENING(so) ? so->sol_incqlen : 0;
			goto integer;

		case SO_TS_CLOCK:
			optval = so->so_ts_clock;
			goto integer;

		case SO_MAX_PACING_RATE:
			optval = so->so_max_pacing_rate;
			goto integer;

		default:
			if (V_socket_hhh[HHOOK_SOCKET_OPT]->hhh_nhooks > 0)
				error = hhook_run_socket(so, sopt,
				    HHOOK_SOCKET_OPT);
			else
				error = ENOPROTOOPT;
			break;
		}
	}
#ifdef MAC
bad:
#endif
	CURVNET_RESTORE();
	return (error);
}

int
soopt_getm(struct sockopt *sopt, struct mbuf **mp)
{
	struct mbuf *m, *m_prev;
	int sopt_size = sopt->sopt_valsize;

	MGET(m, sopt->sopt_td ? M_WAITOK : M_NOWAIT, MT_DATA);
	if (m == NULL)
		return ENOBUFS;
	if (sopt_size > MLEN) {
		MCLGET(m, sopt->sopt_td ? M_WAITOK : M_NOWAIT);
		if ((m->m_flags & M_EXT) == 0) {
			m_free(m);
			return ENOBUFS;
		}
		m->m_len = min(MCLBYTES, sopt_size);
	} else {
		m->m_len = min(MLEN, sopt_size);
	}
	sopt_size -= m->m_len;
	*mp = m;
	m_prev = m;

	while (sopt_size) {
		MGET(m, sopt->sopt_td ? M_WAITOK : M_NOWAIT, MT_DATA);
		if (m == NULL) {
			m_freem(*mp);
			return ENOBUFS;
		}
		if (sopt_size > MLEN) {
			MCLGET(m, sopt->sopt_td != NULL ? M_WAITOK :
			    M_NOWAIT);
			if ((m->m_flags & M_EXT) == 0) {
				m_freem(m);
				m_freem(*mp);
				return ENOBUFS;
			}
			m->m_len = min(MCLBYTES, sopt_size);
		} else {
			m->m_len = min(MLEN, sopt_size);
		}
		sopt_size -= m->m_len;
		m_prev->m_next = m;
		m_prev = m;
	}
	return (0);
}

int
soopt_mcopyin(struct sockopt *sopt, struct mbuf *m)
{
	struct mbuf *m0 = m;

	if (sopt->sopt_val == NULL)
		return (0);
	while (m != NULL && sopt->sopt_valsize >= m->m_len) {
		if (sopt->sopt_td != NULL) {
			int error;

			error = copyin(sopt->sopt_val, mtod(m, char *),
			    m->m_len);
			if (error != 0) {
				m_freem(m0);
				return(error);
			}
		} else
			bcopy(sopt->sopt_val, mtod(m, char *), m->m_len);
		sopt->sopt_valsize -= m->m_len;
		sopt->sopt_val = (char *)sopt->sopt_val + m->m_len;
		m = m->m_next;
	}
	if (m != NULL) /* should be allocated enoughly at ip6_sooptmcopyin() */
		panic("ip6_sooptmcopyin");
	return (0);
}

int
soopt_mcopyout(struct sockopt *sopt, struct mbuf *m)
{
	struct mbuf *m0 = m;
	size_t valsize = 0;

	if (sopt->sopt_val == NULL)
		return (0);
	while (m != NULL && sopt->sopt_valsize >= m->m_len) {
		if (sopt->sopt_td != NULL) {
			int error;

			error = copyout(mtod(m, char *), sopt->sopt_val,
			    m->m_len);
			if (error != 0) {
				m_freem(m0);
				return(error);
			}
		} else
			bcopy(mtod(m, char *), sopt->sopt_val, m->m_len);
		sopt->sopt_valsize -= m->m_len;
		sopt->sopt_val = (char *)sopt->sopt_val + m->m_len;
		valsize += m->m_len;
		m = m->m_next;
	}
	if (m != NULL) {
		/* enough soopt buffer should be given from user-land */
		m_freem(m0);
		return(EINVAL);
	}
	sopt->sopt_valsize = valsize;
	return (0);
}

/*
 * sohasoutofband(): protocol notifies socket layer of the arrival of new
 * out-of-band data, which will then notify socket consumers.
 */
void
sohasoutofband(struct socket *so)
{

	if (so->so_sigio != NULL)
		pgsigio(&so->so_sigio, SIGURG, 0);
	selwakeuppri(&so->so_rdsel, PSOCK);
}

int
sopoll(struct socket *so, int events, struct ucred *active_cred,
    struct thread *td)
{

	/*
	 * We do not need to set or assert curvnet as long as everyone uses
	 * sopoll_generic().
	 */
	return (so->so_proto->pr_usrreqs->pru_sopoll(so, events, active_cred,
	    td));
}

int
sopoll_generic(struct socket *so, int events, struct ucred *active_cred,
    struct thread *td)
{
	int revents;

	SOCK_LOCK(so);
	if (SOLISTENING(so)) {
		if (!(events & (POLLIN | POLLRDNORM)))
			revents = 0;
		else if (!TAILQ_EMPTY(&so->sol_comp))
			revents = events & (POLLIN | POLLRDNORM);
		else if ((events & POLLINIGNEOF) == 0 && so->so_error)
			revents = (events & (POLLIN | POLLRDNORM)) | POLLHUP;
		else {
			selrecord(td, &so->so_rdsel);
			revents = 0;
		}
	} else {
		revents = 0;
		SOCKBUF_LOCK(&so->so_snd);
		SOCKBUF_LOCK(&so->so_rcv);
		if (events & (POLLIN | POLLRDNORM))
			if (soreadabledata(so))
				revents |= events & (POLLIN | POLLRDNORM);
		if (events & (POLLOUT | POLLWRNORM))
			if (sowriteable(so))
				revents |= events & (POLLOUT | POLLWRNORM);
		if (events & (POLLPRI | POLLRDBAND))
			if (so->so_oobmark ||
			    (so->so_rcv.sb_state & SBS_RCVATMARK))
				revents |= events & (POLLPRI | POLLRDBAND);
		if ((events & POLLINIGNEOF) == 0) {
			if (so->so_rcv.sb_state & SBS_CANTRCVMORE) {
				revents |= events & (POLLIN | POLLRDNORM);
				if (so->so_snd.sb_state & SBS_CANTSENDMORE)
					revents |= POLLHUP;
			}
		}
		if (revents == 0) {
			if (events &
			    (POLLIN | POLLPRI | POLLRDNORM | POLLRDBAND)) {
				selrecord(td, &so->so_rdsel);
				so->so_rcv.sb_flags |= SB_SEL;
			}
			if (events & (POLLOUT | POLLWRNORM)) {
				selrecord(td, &so->so_wrsel);
				so->so_snd.sb_flags |= SB_SEL;
			}
		}
		SOCKBUF_UNLOCK(&so->so_rcv);
		SOCKBUF_UNLOCK(&so->so_snd);
	}
	SOCK_UNLOCK(so);
	return (revents);
}

int
soo_kqfilter(struct file *fp, struct knote *kn)
{
	struct socket *so = kn->kn_fp->f_data;
	struct sockbuf *sb;
	struct knlist *knl;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &soread_filtops;
		knl = &so->so_rdsel.si_note;
		sb = &so->so_rcv;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &sowrite_filtops;
		knl = &so->so_wrsel.si_note;
		sb = &so->so_snd;
		break;
	case EVFILT_EMPTY:
		kn->kn_fop = &soempty_filtops;
		knl = &so->so_wrsel.si_note;
		sb = &so->so_snd;
		break;
	default:
		return (EINVAL);
	}

	SOCK_LOCK(so);
	if (SOLISTENING(so)) {
		knlist_add(knl, kn, 1);
	} else {
		SOCKBUF_LOCK(sb);
		knlist_add(knl, kn, 1);
		sb->sb_flags |= SB_KNOTE;
		SOCKBUF_UNLOCK(sb);
	}
	SOCK_UNLOCK(so);
	return (0);
}

/*
 * Some routines that return EOPNOTSUPP for entry points that are not
 * supported by a protocol.  Fill in as needed.
 */
int
pru_accept_notsupp(struct socket *so, struct sockaddr **nam)
{

	return EOPNOTSUPP;
}

int
pru_aio_queue_notsupp(struct socket *so, struct kaiocb *job)
{

	return EOPNOTSUPP;
}

int
pru_attach_notsupp(struct socket *so, int proto, struct thread *td)
{

	return EOPNOTSUPP;
}

int
pru_bind_notsupp(struct socket *so, struct sockaddr *nam, struct thread *td)
{

	return EOPNOTSUPP;
}

int
pru_bindat_notsupp(int fd, struct socket *so, struct sockaddr *nam,
    struct thread *td)
{

	return EOPNOTSUPP;
}

int
pru_connect_notsupp(struct socket *so, struct sockaddr *nam, struct thread *td)
{

	return EOPNOTSUPP;
}

int
pru_connectat_notsupp(int fd, struct socket *so, struct sockaddr *nam,
    struct thread *td)
{

	return EOPNOTSUPP;
}

int
pru_connect2_notsupp(struct socket *so1, struct socket *so2)
{

	return EOPNOTSUPP;
}

int
pru_control_notsupp(struct socket *so, u_long cmd, caddr_t data,
    struct ifnet *ifp, struct thread *td)
{

	return EOPNOTSUPP;
}

int
pru_disconnect_notsupp(struct socket *so)
{

	return EOPNOTSUPP;
}

int
pru_listen_notsupp(struct socket *so, int backlog, struct thread *td)
{

	return EOPNOTSUPP;
}

int
pru_peeraddr_notsupp(struct socket *so, struct sockaddr **nam)
{

	return EOPNOTSUPP;
}

int
pru_rcvd_notsupp(struct socket *so, int flags)
{

	return EOPNOTSUPP;
}

int
pru_rcvoob_notsupp(struct socket *so, struct mbuf *m, int flags)
{

	return EOPNOTSUPP;
}

int
pru_send_notsupp(struct socket *so, int flags, struct mbuf *m,
    struct sockaddr *addr, struct mbuf *control, struct thread *td)
{

	return EOPNOTSUPP;
}

int
pru_ready_notsupp(struct socket *so, struct mbuf *m, int count)
{

	return (EOPNOTSUPP);
}

/*
 * This isn't really a ``null'' operation, but it's the default one and
 * doesn't do anything destructive.
 */
int
pru_sense_null(struct socket *so, struct stat *sb)
{

	sb->st_blksize = so->so_snd.sb_hiwat;
	return 0;
}

int
pru_shutdown_notsupp(struct socket *so)
{

	return EOPNOTSUPP;
}

int
pru_sockaddr_notsupp(struct socket *so, struct sockaddr **nam)
{

	return EOPNOTSUPP;
}

int
pru_sosend_notsupp(struct socket *so, struct sockaddr *addr, struct uio *uio,
    struct mbuf *top, struct mbuf *control, int flags, struct thread *td)
{

	return EOPNOTSUPP;
}

int
pru_soreceive_notsupp(struct socket *so, struct sockaddr **paddr,
    struct uio *uio, struct mbuf **mp0, struct mbuf **controlp, int *flagsp)
{

	return EOPNOTSUPP;
}

int
pru_sopoll_notsupp(struct socket *so, int events, struct ucred *cred,
    struct thread *td)
{

	return EOPNOTSUPP;
}

static void
filt_sordetach(struct knote *kn)
{
	struct socket *so = kn->kn_fp->f_data;

	so_rdknl_lock(so);
	knlist_remove(&so->so_rdsel.si_note, kn, 1);
	if (!SOLISTENING(so) && knlist_empty(&so->so_rdsel.si_note))
		so->so_rcv.sb_flags &= ~SB_KNOTE;
	so_rdknl_unlock(so);
}

/*ARGSUSED*/
static int
filt_soread(struct knote *kn, long hint)
{
	struct socket *so;

	so = kn->kn_fp->f_data;

	if (SOLISTENING(so)) {
		SOCK_LOCK_ASSERT(so);
		kn->kn_data = so->sol_qlen;
		if (so->so_error) {
			kn->kn_flags |= EV_EOF;
			kn->kn_fflags = so->so_error;
			return (1);
		}
		return (!TAILQ_EMPTY(&so->sol_comp));
	}

	SOCKBUF_LOCK_ASSERT(&so->so_rcv);

	kn->kn_data = sbavail(&so->so_rcv) - so->so_rcv.sb_ctl;
	if (so->so_rcv.sb_state & SBS_CANTRCVMORE) {
		kn->kn_flags |= EV_EOF;
		kn->kn_fflags = so->so_error;
		return (1);
	} else if (so->so_error)	/* temporary udp error */
		return (1);

	if (kn->kn_sfflags & NOTE_LOWAT) {
		if (kn->kn_data >= kn->kn_sdata)
			return (1);
	} else if (sbavail(&so->so_rcv) >= so->so_rcv.sb_lowat)
		return (1);

	/* This hook returning non-zero indicates an event, not error */
	return (hhook_run_socket(so, NULL, HHOOK_FILT_SOREAD));
}

static void
filt_sowdetach(struct knote *kn)
{
	struct socket *so = kn->kn_fp->f_data;

	so_wrknl_lock(so);
	knlist_remove(&so->so_wrsel.si_note, kn, 1);
	if (!SOLISTENING(so) && knlist_empty(&so->so_wrsel.si_note))
		so->so_snd.sb_flags &= ~SB_KNOTE;
	so_wrknl_unlock(so);
}

/*ARGSUSED*/
static int
filt_sowrite(struct knote *kn, long hint)
{
	struct socket *so;

	so = kn->kn_fp->f_data;

	if (SOLISTENING(so))
		return (0);

	SOCKBUF_LOCK_ASSERT(&so->so_snd);
	kn->kn_data = sbspace(&so->so_snd);

	hhook_run_socket(so, kn, HHOOK_FILT_SOWRITE);

	if (so->so_snd.sb_state & SBS_CANTSENDMORE) {
		kn->kn_flags |= EV_EOF;
		kn->kn_fflags = so->so_error;
		return (1);
	} else if (so->so_error)	/* temporary udp error */
		return (1);
	else if (((so->so_state & SS_ISCONNECTED) == 0) &&
	    (so->so_proto->pr_flags & PR_CONNREQUIRED))
		return (0);
	else if (kn->kn_sfflags & NOTE_LOWAT)
		return (kn->kn_data >= kn->kn_sdata);
	else
		return (kn->kn_data >= so->so_snd.sb_lowat);
}

static int
filt_soempty(struct knote *kn, long hint)
{
	struct socket *so;

	so = kn->kn_fp->f_data;

	if (SOLISTENING(so))
		return (1);

	SOCKBUF_LOCK_ASSERT(&so->so_snd);
	kn->kn_data = sbused(&so->so_snd);

	if (kn->kn_data == 0)
		return (1);
	else
		return (0);
}

int
socheckuid(struct socket *so, uid_t uid)
{

	if (so == NULL)
		return (EPERM);
	if (so->so_cred->cr_uid != uid)
		return (EPERM);
	return (0);
}

/*
 * These functions are used by protocols to notify the socket layer (and its
 * consumers) of state changes in the sockets driven by protocol-side events.
 */

/*
 * Procedures to manipulate state flags of socket and do appropriate wakeups.
 *
 * Normal sequence from the active (originating) side is that
 * soisconnecting() is called during processing of connect() call, resulting
 * in an eventual call to soisconnected() if/when the connection is
 * established.  When the connection is torn down soisdisconnecting() is
 * called during processing of disconnect() call, and soisdisconnected() is
 * called when the connection to the peer is totally severed.  The semantics
 * of these routines are such that connectionless protocols can call
 * soisconnected() and soisdisconnected() only, bypassing the in-progress
 * calls when setting up a ``connection'' takes no time.
 *
 * From the passive side, a socket is created with two queues of sockets:
 * so_incomp for connections in progress and so_comp for connections already
 * made and awaiting user acceptance.  As a protocol is preparing incoming
 * connections, it creates a socket structure queued on so_incomp by calling
 * sonewconn().  When the connection is established, soisconnected() is
 * called, and transfers the socket structure to so_comp, making it available
 * to accept().
 *
 * If a socket is closed with sockets on either so_incomp or so_comp, these
 * sockets are dropped.
 *
 * If higher-level protocols are implemented in the kernel, the wakeups done
 * here will sometimes cause software-interrupt process scheduling.
 */
void
soisconnecting(struct socket *so)
{

	SOCK_LOCK(so);
	so->so_state &= ~(SS_ISCONNECTED|SS_ISDISCONNECTING);
	so->so_state |= SS_ISCONNECTING;
	SOCK_UNLOCK(so);
}

void
soisconnected(struct socket *so)
{

	SOCK_LOCK(so);
	so->so_state &= ~(SS_ISCONNECTING|SS_ISDISCONNECTING|SS_ISCONFIRMING);
	so->so_state |= SS_ISCONNECTED;

	if (so->so_qstate == SQ_INCOMP) {
		struct socket *head = so->so_listen;
		int ret;

		KASSERT(head, ("%s: so %p on incomp of NULL", __func__, so));
		/*
		 * Promoting a socket from incomplete queue to complete, we
		 * need to go through reverse order of locking.  We first do
		 * trylock, and if that doesn't succeed, we go the hard way
		 * leaving a reference and rechecking consistency after proper
		 * locking.
		 */
		if (__predict_false(SOLISTEN_TRYLOCK(head) == 0)) {
			soref(head);
			SOCK_UNLOCK(so);
			SOLISTEN_LOCK(head);
			SOCK_LOCK(so);
			if (__predict_false(head != so->so_listen)) {
				/*
				 * The socket went off the listen queue,
				 * should be lost race to close(2) of sol.
				 * The socket is about to soabort().
				 */
				SOCK_UNLOCK(so);
				sorele(head);
				return;
			}
			/* Not the last one, as so holds a ref. */
			refcount_release(&head->so_count);
		}
again:
		if ((so->so_options & SO_ACCEPTFILTER) == 0) {
			TAILQ_REMOVE(&head->sol_incomp, so, so_list);
			head->sol_incqlen--;
			TAILQ_INSERT_TAIL(&head->sol_comp, so, so_list);
			head->sol_qlen++;
			so->so_qstate = SQ_COMP;
			SOCK_UNLOCK(so);
			solisten_wakeup(head);	/* unlocks */
		} else {
			SOCKBUF_LOCK(&so->so_rcv);
			soupcall_set(so, SO_RCV,
			    head->sol_accept_filter->accf_callback,
			    head->sol_accept_filter_arg);
			so->so_options &= ~SO_ACCEPTFILTER;
			ret = head->sol_accept_filter->accf_callback(so,
			    head->sol_accept_filter_arg, M_NOWAIT);
			if (ret == SU_ISCONNECTED) {
				soupcall_clear(so, SO_RCV);
				SOCKBUF_UNLOCK(&so->so_rcv);
				goto again;
			}
			SOCKBUF_UNLOCK(&so->so_rcv);
			SOCK_UNLOCK(so);
			SOLISTEN_UNLOCK(head);
		}
		return;
	}
	SOCK_UNLOCK(so);
	wakeup(&so->so_timeo);
	sorwakeup(so);
	sowwakeup(so);
}

void
soisdisconnecting(struct socket *so)
{

	SOCK_LOCK(so);
	so->so_state &= ~SS_ISCONNECTING;
	so->so_state |= SS_ISDISCONNECTING;

	if (!SOLISTENING(so)) {
		SOCKBUF_LOCK(&so->so_rcv);
		socantrcvmore_locked(so);
		SOCKBUF_LOCK(&so->so_snd);
		socantsendmore_locked(so);
	}
	SOCK_UNLOCK(so);
	wakeup(&so->so_timeo);
}

void
soisdisconnected(struct socket *so)
{

	SOCK_LOCK(so);
	so->so_state &= ~(SS_ISCONNECTING|SS_ISCONNECTED|SS_ISDISCONNECTING);
	so->so_state |= SS_ISDISCONNECTED;

	if (!SOLISTENING(so)) {
		SOCK_UNLOCK(so);
		SOCKBUF_LOCK(&so->so_rcv);
		socantrcvmore_locked(so);
		SOCKBUF_LOCK(&so->so_snd);
		sbdrop_locked(&so->so_snd, sbused(&so->so_snd));
		socantsendmore_locked(so);
	} else
		SOCK_UNLOCK(so);
	wakeup(&so->so_timeo);
}

/*
 * Make a copy of a sockaddr in a malloced buffer of type M_SONAME.
 */
struct sockaddr *
sodupsockaddr(const struct sockaddr *sa, int mflags)
{
	struct sockaddr *sa2;

	sa2 = malloc(sa->sa_len, M_SONAME, mflags);
	if (sa2)
		bcopy(sa, sa2, sa->sa_len);
	return sa2;
}

/*
 * Register per-socket destructor.
 */
void
sodtor_set(struct socket *so, so_dtor_t *func)
{

	SOCK_LOCK_ASSERT(so);
	so->so_dtor = func;
}

/*
 * Register per-socket buffer upcalls.
 */
void
soupcall_set(struct socket *so, int which, so_upcall_t func, void *arg)
{
	struct sockbuf *sb;

	KASSERT(!SOLISTENING(so), ("%s: so %p listening", __func__, so));

	switch (which) {
	case SO_RCV:
		sb = &so->so_rcv;
		break;
	case SO_SND:
		sb = &so->so_snd;
		break;
	default:
		panic("soupcall_set: bad which");
	}
	SOCKBUF_LOCK_ASSERT(sb);
	sb->sb_upcall = func;
	sb->sb_upcallarg = arg;
	sb->sb_flags |= SB_UPCALL;
}

void
soupcall_clear(struct socket *so, int which)
{
	struct sockbuf *sb;

	KASSERT(!SOLISTENING(so), ("%s: so %p listening", __func__, so));

	switch (which) {
	case SO_RCV:
		sb = &so->so_rcv;
		break;
	case SO_SND:
		sb = &so->so_snd;
		break;
	default:
		panic("soupcall_clear: bad which");
	}
	SOCKBUF_LOCK_ASSERT(sb);
	KASSERT(sb->sb_upcall != NULL,
	    ("%s: so %p no upcall to clear", __func__, so));
	sb->sb_upcall = NULL;
	sb->sb_upcallarg = NULL;
	sb->sb_flags &= ~SB_UPCALL;
}

void
solisten_upcall_set(struct socket *so, so_upcall_t func, void *arg)
{

	SOLISTEN_LOCK_ASSERT(so);
	so->sol_upcall = func;
	so->sol_upcallarg = arg;
}

static void
so_rdknl_lock(void *arg)
{
	struct socket *so = arg;

	if (SOLISTENING(so))
		SOCK_LOCK(so);
	else
		SOCKBUF_LOCK(&so->so_rcv);
}

static void
so_rdknl_unlock(void *arg)
{
	struct socket *so = arg;

	if (SOLISTENING(so))
		SOCK_UNLOCK(so);
	else
		SOCKBUF_UNLOCK(&so->so_rcv);
}

static void
so_rdknl_assert_locked(void *arg)
{
	struct socket *so = arg;

	if (SOLISTENING(so))
		SOCK_LOCK_ASSERT(so);
	else
		SOCKBUF_LOCK_ASSERT(&so->so_rcv);
}

static void
so_rdknl_assert_unlocked(void *arg)
{
	struct socket *so = arg;

	if (SOLISTENING(so))
		SOCK_UNLOCK_ASSERT(so);
	else
		SOCKBUF_UNLOCK_ASSERT(&so->so_rcv);
}

static void
so_wrknl_lock(void *arg)
{
	struct socket *so = arg;

	if (SOLISTENING(so))
		SOCK_LOCK(so);
	else
		SOCKBUF_LOCK(&so->so_snd);
}

static void
so_wrknl_unlock(void *arg)
{
	struct socket *so = arg;

	if (SOLISTENING(so))
		SOCK_UNLOCK(so);
	else
		SOCKBUF_UNLOCK(&so->so_snd);
}

static void
so_wrknl_assert_locked(void *arg)
{
	struct socket *so = arg;

	if (SOLISTENING(so))
		SOCK_LOCK_ASSERT(so);
	else
		SOCKBUF_LOCK_ASSERT(&so->so_snd);
}

static void
so_wrknl_assert_unlocked(void *arg)
{
	struct socket *so = arg;

	if (SOLISTENING(so))
		SOCK_UNLOCK_ASSERT(so);
	else
		SOCKBUF_UNLOCK_ASSERT(&so->so_snd);
}

/*
 * Create an external-format (``xsocket'') structure using the information in
 * the kernel-format socket structure pointed to by so.  This is done to
 * reduce the spew of irrelevant information over this interface, to isolate
 * user code from changes in the kernel structure, and potentially to provide
 * information-hiding if we decide that some of this information should be
 * hidden from users.
 */
void
sotoxsocket(struct socket *so, struct xsocket *xso)
{

	bzero(xso, sizeof(*xso));
	xso->xso_len = sizeof *xso;
	xso->xso_so = (uintptr_t)so;
	xso->so_type = so->so_type;
	xso->so_options = so->so_options;
	xso->so_linger = so->so_linger;
	xso->so_state = so->so_state;
	xso->so_pcb = (uintptr_t)so->so_pcb;
	xso->xso_protocol = so->so_proto->pr_protocol;
	xso->xso_family = so->so_proto->pr_domain->dom_family;
	xso->so_timeo = so->so_timeo;
	xso->so_error = so->so_error;
	xso->so_uid = so->so_cred->cr_uid;
	xso->so_pgid = so->so_sigio ? so->so_sigio->sio_pgid : 0;
	if (SOLISTENING(so)) {
		xso->so_qlen = so->sol_qlen;
		xso->so_incqlen = so->sol_incqlen;
		xso->so_qlimit = so->sol_qlimit;
		xso->so_oobmark = 0;
	} else {
		xso->so_state |= so->so_qstate;
		xso->so_qlen = xso->so_incqlen = xso->so_qlimit = 0;
		xso->so_oobmark = so->so_oobmark;
		sbtoxsockbuf(&so->so_snd, &xso->so_snd);
		sbtoxsockbuf(&so->so_rcv, &xso->so_rcv);
	}
}

struct sockbuf *
so_sockbuf_rcv(struct socket *so)
{

	return (&so->so_rcv);
}

struct sockbuf *
so_sockbuf_snd(struct socket *so)
{

	return (&so->so_snd);
}

int
so_state_get(const struct socket *so)
{

	return (so->so_state);
}

void
so_state_set(struct socket *so, int val)
{

	so->so_state = val;
}

int
so_options_get(const struct socket *so)
{

	return (so->so_options);
}

void
so_options_set(struct socket *so, int val)
{

	so->so_options = val;
}

int
so_error_get(const struct socket *so)
{

	return (so->so_error);
}

void
so_error_set(struct socket *so, int val)
{

	so->so_error = val;
}

int
so_linger_get(const struct socket *so)
{

	return (so->so_linger);
}

void
so_linger_set(struct socket *so, int val)
{

	so->so_linger = val;
}

struct protosw *
so_protosw_get(const struct socket *so)
{

	return (so->so_proto);
}

void
so_protosw_set(struct socket *so, struct protosw *val)
{

	so->so_proto = val;
}

void
so_sorwakeup(struct socket *so)
{

	sorwakeup(so);
}

void
so_sowwakeup(struct socket *so)
{

	sowwakeup(so);
}

void
so_sorwakeup_locked(struct socket *so)
{

	sorwakeup_locked(so);
}

void
so_sowwakeup_locked(struct socket *so)
{

	sowwakeup_locked(so);
}

void
so_lock(struct socket *so)
{

	SOCK_LOCK(so);
}

void
so_unlock(struct socket *so)
{

	SOCK_UNLOCK(so);
}
