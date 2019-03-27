/*	$NetBSD: svc_vc.c,v 1.7 2000/08/03 00:01:53 fvdl Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2009, Sun Microsystems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met:
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution.
 * - Neither the name of Sun Microsystems, Inc. nor the names of its 
 *   contributors may be used to endorse or promote products derived 
 *   from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE 
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
 * POSSIBILITY OF SUCH DAMAGE.
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char *sccsid2 = "@(#)svc_tcp.c 1.21 87/08/11 Copyr 1984 Sun Micro";
static char *sccsid = "@(#)svc_tcp.c	2.2 88/08/01 4.0 RPCSRC";
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * svc_vc.c, Server side for Connection Oriented based RPC. 
 *
 * Actually implements two flavors of transporter -
 * a tcp rendezvouser (a listner and connection establisher)
 * and a record/tcp stream.
 */

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/protosw.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sx.h>
#include <sys/systm.h>
#include <sys/uio.h>

#include <net/vnet.h>

#include <netinet/tcp.h>

#include <rpc/rpc.h>

#include <rpc/krpc.h>
#include <rpc/rpc_com.h>

#include <security/mac/mac_framework.h>

static bool_t svc_vc_rendezvous_recv(SVCXPRT *, struct rpc_msg *,
    struct sockaddr **, struct mbuf **);
static enum xprt_stat svc_vc_rendezvous_stat(SVCXPRT *);
static void svc_vc_rendezvous_destroy(SVCXPRT *);
static bool_t svc_vc_null(void);
static void svc_vc_destroy(SVCXPRT *);
static enum xprt_stat svc_vc_stat(SVCXPRT *);
static bool_t svc_vc_ack(SVCXPRT *, uint32_t *);
static bool_t svc_vc_recv(SVCXPRT *, struct rpc_msg *,
    struct sockaddr **, struct mbuf **);
static bool_t svc_vc_reply(SVCXPRT *, struct rpc_msg *,
    struct sockaddr *, struct mbuf *, uint32_t *seq);
static bool_t svc_vc_control(SVCXPRT *xprt, const u_int rq, void *in);
static bool_t svc_vc_rendezvous_control (SVCXPRT *xprt, const u_int rq,
    void *in);
static void svc_vc_backchannel_destroy(SVCXPRT *);
static enum xprt_stat svc_vc_backchannel_stat(SVCXPRT *);
static bool_t svc_vc_backchannel_recv(SVCXPRT *, struct rpc_msg *,
    struct sockaddr **, struct mbuf **);
static bool_t svc_vc_backchannel_reply(SVCXPRT *, struct rpc_msg *,
    struct sockaddr *, struct mbuf *, uint32_t *);
static bool_t svc_vc_backchannel_control(SVCXPRT *xprt, const u_int rq,
    void *in);
static SVCXPRT *svc_vc_create_conn(SVCPOOL *pool, struct socket *so,
    struct sockaddr *raddr);
static int svc_vc_accept(struct socket *head, struct socket **sop);
static int svc_vc_soupcall(struct socket *so, void *arg, int waitflag);
static int svc_vc_rendezvous_soupcall(struct socket *, void *, int);

static struct xp_ops svc_vc_rendezvous_ops = {
	.xp_recv =	svc_vc_rendezvous_recv,
	.xp_stat =	svc_vc_rendezvous_stat,
	.xp_reply =	(bool_t (*)(SVCXPRT *, struct rpc_msg *,
		struct sockaddr *, struct mbuf *, uint32_t *))svc_vc_null,
	.xp_destroy =	svc_vc_rendezvous_destroy,
	.xp_control =	svc_vc_rendezvous_control
};

static struct xp_ops svc_vc_ops = {
	.xp_recv =	svc_vc_recv,
	.xp_stat =	svc_vc_stat,
	.xp_ack =	svc_vc_ack,
	.xp_reply =	svc_vc_reply,
	.xp_destroy =	svc_vc_destroy,
	.xp_control =	svc_vc_control
};

static struct xp_ops svc_vc_backchannel_ops = {
	.xp_recv =	svc_vc_backchannel_recv,
	.xp_stat =	svc_vc_backchannel_stat,
	.xp_reply =	svc_vc_backchannel_reply,
	.xp_destroy =	svc_vc_backchannel_destroy,
	.xp_control =	svc_vc_backchannel_control
};

/*
 * Usage:
 *	xprt = svc_vc_create(sock, send_buf_size, recv_buf_size);
 *
 * Creates, registers, and returns a (rpc) tcp based transporter.
 * Once *xprt is initialized, it is registered as a transporter
 * see (svc.h, xprt_register).  This routine returns
 * a NULL if a problem occurred.
 *
 * The filedescriptor passed in is expected to refer to a bound, but
 * not yet connected socket.
 *
 * Since streams do buffered io similar to stdio, the caller can specify
 * how big the send and receive buffers are via the second and third parms;
 * 0 => use the system default.
 */
SVCXPRT *
svc_vc_create(SVCPOOL *pool, struct socket *so, size_t sendsize,
    size_t recvsize)
{
	SVCXPRT *xprt;
	struct sockaddr* sa;
	int error;

	SOCK_LOCK(so);
	if (so->so_state & (SS_ISCONNECTED|SS_ISDISCONNECTED)) {
		SOCK_UNLOCK(so);
		CURVNET_SET(so->so_vnet);
		error = so->so_proto->pr_usrreqs->pru_peeraddr(so, &sa);
		CURVNET_RESTORE();
		if (error)
			return (NULL);
		xprt = svc_vc_create_conn(pool, so, sa);
		free(sa, M_SONAME);
		return (xprt);
	}
	SOCK_UNLOCK(so);

	xprt = svc_xprt_alloc();
	sx_init(&xprt->xp_lock, "xprt->xp_lock");
	xprt->xp_pool = pool;
	xprt->xp_socket = so;
	xprt->xp_p1 = NULL;
	xprt->xp_p2 = NULL;
	xprt->xp_ops = &svc_vc_rendezvous_ops;

	CURVNET_SET(so->so_vnet);
	error = so->so_proto->pr_usrreqs->pru_sockaddr(so, &sa);
	CURVNET_RESTORE();
	if (error) {
		goto cleanup_svc_vc_create;
	}

	memcpy(&xprt->xp_ltaddr, sa, sa->sa_len);
	free(sa, M_SONAME);

	xprt_register(xprt);

	solisten(so, -1, curthread);

	SOLISTEN_LOCK(so);
	xprt->xp_upcallset = 1;
	solisten_upcall_set(so, svc_vc_rendezvous_soupcall, xprt);
	SOLISTEN_UNLOCK(so);

	return (xprt);

cleanup_svc_vc_create:
	sx_destroy(&xprt->xp_lock);
	svc_xprt_free(xprt);

	return (NULL);
}

/*
 * Create a new transport for a socket optained via soaccept().
 */
SVCXPRT *
svc_vc_create_conn(SVCPOOL *pool, struct socket *so, struct sockaddr *raddr)
{
	SVCXPRT *xprt;
	struct cf_conn *cd;
	struct sockaddr* sa = NULL;
	struct sockopt opt;
	int one = 1;
	int error;

	bzero(&opt, sizeof(struct sockopt));
	opt.sopt_dir = SOPT_SET;
	opt.sopt_level = SOL_SOCKET;
	opt.sopt_name = SO_KEEPALIVE;
	opt.sopt_val = &one;
	opt.sopt_valsize = sizeof(one);
	error = sosetopt(so, &opt);
	if (error) {
		return (NULL);
	}

	if (so->so_proto->pr_protocol == IPPROTO_TCP) {
		bzero(&opt, sizeof(struct sockopt));
		opt.sopt_dir = SOPT_SET;
		opt.sopt_level = IPPROTO_TCP;
		opt.sopt_name = TCP_NODELAY;
		opt.sopt_val = &one;
		opt.sopt_valsize = sizeof(one);
		error = sosetopt(so, &opt);
		if (error) {
			return (NULL);
		}
	}

	cd = mem_alloc(sizeof(*cd));
	cd->strm_stat = XPRT_IDLE;

	xprt = svc_xprt_alloc();
	sx_init(&xprt->xp_lock, "xprt->xp_lock");
	xprt->xp_pool = pool;
	xprt->xp_socket = so;
	xprt->xp_p1 = cd;
	xprt->xp_p2 = NULL;
	xprt->xp_ops = &svc_vc_ops;

	/*
	 * See http://www.connectathon.org/talks96/nfstcp.pdf - client
	 * has a 5 minute timer, server has a 6 minute timer.
	 */
	xprt->xp_idletimeout = 6 * 60;

	memcpy(&xprt->xp_rtaddr, raddr, raddr->sa_len);

	CURVNET_SET(so->so_vnet);
	error = so->so_proto->pr_usrreqs->pru_sockaddr(so, &sa);
	CURVNET_RESTORE();
	if (error)
		goto cleanup_svc_vc_create;

	memcpy(&xprt->xp_ltaddr, sa, sa->sa_len);
	free(sa, M_SONAME);

	xprt_register(xprt);

	SOCKBUF_LOCK(&so->so_rcv);
	xprt->xp_upcallset = 1;
	soupcall_set(so, SO_RCV, svc_vc_soupcall, xprt);
	SOCKBUF_UNLOCK(&so->so_rcv);

	/*
	 * Throw the transport into the active list in case it already
	 * has some data buffered.
	 */
	sx_xlock(&xprt->xp_lock);
	xprt_active(xprt);
	sx_xunlock(&xprt->xp_lock);

	return (xprt);
cleanup_svc_vc_create:
	sx_destroy(&xprt->xp_lock);
	svc_xprt_free(xprt);
	mem_free(cd, sizeof(*cd));

	return (NULL);
}

/*
 * Create a new transport for a backchannel on a clnt_vc socket.
 */
SVCXPRT *
svc_vc_create_backchannel(SVCPOOL *pool)
{
	SVCXPRT *xprt = NULL;
	struct cf_conn *cd = NULL;

	cd = mem_alloc(sizeof(*cd));
	cd->strm_stat = XPRT_IDLE;

	xprt = svc_xprt_alloc();
	sx_init(&xprt->xp_lock, "xprt->xp_lock");
	xprt->xp_pool = pool;
	xprt->xp_socket = NULL;
	xprt->xp_p1 = cd;
	xprt->xp_p2 = NULL;
	xprt->xp_ops = &svc_vc_backchannel_ops;
	return (xprt);
}

/*
 * This does all of the accept except the final call to soaccept. The
 * caller will call soaccept after dropping its locks (soaccept may
 * call malloc).
 */
int
svc_vc_accept(struct socket *head, struct socket **sop)
{
	struct socket *so;
	int error = 0;
	short nbio;

	/* XXXGL: shouldn't that be an assertion? */
	if ((head->so_options & SO_ACCEPTCONN) == 0) {
		error = EINVAL;
		goto done;
	}
#ifdef MAC
	error = mac_socket_check_accept(curthread->td_ucred, head);
	if (error != 0)
		goto done;
#endif
	/*
	 * XXXGL: we want non-blocking semantics.  The socket could be a
	 * socket created by kernel as well as socket shared with userland,
	 * so we can't be sure about presense of SS_NBIO.  We also shall not
	 * toggle it on the socket, since that may surprise userland.  So we
	 * set SS_NBIO only temporarily.
	 */
	SOLISTEN_LOCK(head);
	nbio = head->so_state & SS_NBIO;
	head->so_state |= SS_NBIO;
	error = solisten_dequeue(head, &so, 0);
	head->so_state &= (nbio & ~SS_NBIO);
	if (error)
		goto done;

	so->so_state |= nbio;
	*sop = so;

	/* connection has been removed from the listen queue */
	KNOTE_UNLOCKED(&head->so_rdsel.si_note, 0);
done:
	return (error);
}

/*ARGSUSED*/
static bool_t
svc_vc_rendezvous_recv(SVCXPRT *xprt, struct rpc_msg *msg,
    struct sockaddr **addrp, struct mbuf **mp)
{
	struct socket *so = NULL;
	struct sockaddr *sa = NULL;
	int error;
	SVCXPRT *new_xprt;

	/*
	 * The socket upcall calls xprt_active() which will eventually
	 * cause the server to call us here. We attempt to accept a
	 * connection from the socket and turn it into a new
	 * transport. If the accept fails, we have drained all pending
	 * connections so we call xprt_inactive().
	 */
	sx_xlock(&xprt->xp_lock);

	error = svc_vc_accept(xprt->xp_socket, &so);

	if (error == EWOULDBLOCK) {
		/*
		 * We must re-test for new connections after taking
		 * the lock to protect us in the case where a new
		 * connection arrives after our call to accept fails
		 * with EWOULDBLOCK.
		 */
		SOLISTEN_LOCK(xprt->xp_socket);
		if (TAILQ_EMPTY(&xprt->xp_socket->sol_comp))
			xprt_inactive_self(xprt);
		SOLISTEN_UNLOCK(xprt->xp_socket);
		sx_xunlock(&xprt->xp_lock);
		return (FALSE);
	}

	if (error) {
		SOLISTEN_LOCK(xprt->xp_socket);
		if (xprt->xp_upcallset) {
			xprt->xp_upcallset = 0;
			soupcall_clear(xprt->xp_socket, SO_RCV);
		}
		SOLISTEN_UNLOCK(xprt->xp_socket);
		xprt_inactive_self(xprt);
		sx_xunlock(&xprt->xp_lock);
		return (FALSE);
	}

	sx_xunlock(&xprt->xp_lock);

	sa = NULL;
	error = soaccept(so, &sa);

	if (error) {
		/*
		 * XXX not sure if I need to call sofree or soclose here.
		 */
		if (sa)
			free(sa, M_SONAME);
		return (FALSE);
	}

	/*
	 * svc_vc_create_conn will call xprt_register - we don't need
	 * to do anything with the new connection except derefence it.
	 */
	new_xprt = svc_vc_create_conn(xprt->xp_pool, so, sa);
	if (!new_xprt) {
		soclose(so);
	} else {
		SVC_RELEASE(new_xprt);
	}

	free(sa, M_SONAME);

	return (FALSE); /* there is never an rpc msg to be processed */
}

/*ARGSUSED*/
static enum xprt_stat
svc_vc_rendezvous_stat(SVCXPRT *xprt)
{

	return (XPRT_IDLE);
}

static void
svc_vc_destroy_common(SVCXPRT *xprt)
{

	if (xprt->xp_socket)
		(void)soclose(xprt->xp_socket);

	if (xprt->xp_netid)
		(void) mem_free(xprt->xp_netid, strlen(xprt->xp_netid) + 1);
	svc_xprt_free(xprt);
}

static void
svc_vc_rendezvous_destroy(SVCXPRT *xprt)
{

	SOLISTEN_LOCK(xprt->xp_socket);
	if (xprt->xp_upcallset) {
		xprt->xp_upcallset = 0;
		solisten_upcall_set(xprt->xp_socket, NULL, NULL);
	}
	SOLISTEN_UNLOCK(xprt->xp_socket);

	svc_vc_destroy_common(xprt);
}

static void
svc_vc_destroy(SVCXPRT *xprt)
{
	struct cf_conn *cd = (struct cf_conn *)xprt->xp_p1;

	SOCKBUF_LOCK(&xprt->xp_socket->so_rcv);
	if (xprt->xp_upcallset) {
		xprt->xp_upcallset = 0;
		soupcall_clear(xprt->xp_socket, SO_RCV);
	}
	SOCKBUF_UNLOCK(&xprt->xp_socket->so_rcv);

	svc_vc_destroy_common(xprt);

	if (cd->mreq)
		m_freem(cd->mreq);
	if (cd->mpending)
		m_freem(cd->mpending);
	mem_free(cd, sizeof(*cd));
}

static void
svc_vc_backchannel_destroy(SVCXPRT *xprt)
{
	struct cf_conn *cd = (struct cf_conn *)xprt->xp_p1;
	struct mbuf *m, *m2;

	svc_xprt_free(xprt);
	m = cd->mreq;
	while (m != NULL) {
		m2 = m;
		m = m->m_nextpkt;
		m_freem(m2);
	}
	mem_free(cd, sizeof(*cd));
}

/*ARGSUSED*/
static bool_t
svc_vc_control(SVCXPRT *xprt, const u_int rq, void *in)
{
	return (FALSE);
}

static bool_t
svc_vc_rendezvous_control(SVCXPRT *xprt, const u_int rq, void *in)
{

	return (FALSE);
}

static bool_t
svc_vc_backchannel_control(SVCXPRT *xprt, const u_int rq, void *in)
{

	return (FALSE);
}

static enum xprt_stat
svc_vc_stat(SVCXPRT *xprt)
{
	struct cf_conn *cd;

	cd = (struct cf_conn *)(xprt->xp_p1);

	if (cd->strm_stat == XPRT_DIED)
		return (XPRT_DIED);

	if (cd->mreq != NULL && cd->resid == 0 && cd->eor)
		return (XPRT_MOREREQS);

	if (soreadable(xprt->xp_socket))
		return (XPRT_MOREREQS);

	return (XPRT_IDLE);
}

static bool_t
svc_vc_ack(SVCXPRT *xprt, uint32_t *ack)
{

	*ack = atomic_load_acq_32(&xprt->xp_snt_cnt);
	*ack -= sbused(&xprt->xp_socket->so_snd);
	return (TRUE);
}

static enum xprt_stat
svc_vc_backchannel_stat(SVCXPRT *xprt)
{
	struct cf_conn *cd;

	cd = (struct cf_conn *)(xprt->xp_p1);

	if (cd->mreq != NULL)
		return (XPRT_MOREREQS);

	return (XPRT_IDLE);
}

/*
 * If we have an mbuf chain in cd->mpending, try to parse a record from it,
 * leaving the result in cd->mreq. If we don't have a complete record, leave
 * the partial result in cd->mreq and try to read more from the socket.
 */
static int
svc_vc_process_pending(SVCXPRT *xprt)
{
	struct cf_conn *cd = (struct cf_conn *) xprt->xp_p1;
	struct socket *so = xprt->xp_socket;
	struct mbuf *m;

	/*
	 * If cd->resid is non-zero, we have part of the
	 * record already, otherwise we are expecting a record
	 * marker.
	 */
	if (!cd->resid && cd->mpending) {
		/*
		 * See if there is enough data buffered to
		 * make up a record marker. Make sure we can
		 * handle the case where the record marker is
		 * split across more than one mbuf.
		 */
		size_t n = 0;
		uint32_t header;

		m = cd->mpending;
		while (n < sizeof(uint32_t) && m) {
			n += m->m_len;
			m = m->m_next;
		}
		if (n < sizeof(uint32_t)) {
			so->so_rcv.sb_lowat = sizeof(uint32_t) - n;
			return (FALSE);
		}
		m_copydata(cd->mpending, 0, sizeof(header),
		    (char *)&header);
		header = ntohl(header);
		cd->eor = (header & 0x80000000) != 0;
		cd->resid = header & 0x7fffffff;
		m_adj(cd->mpending, sizeof(uint32_t));
	}

	/*
	 * Start pulling off mbufs from cd->mpending
	 * until we either have a complete record or
	 * we run out of data. We use m_split to pull
	 * data - it will pull as much as possible and
	 * split the last mbuf if necessary.
	 */
	while (cd->mpending && cd->resid) {
		m = cd->mpending;
		if (cd->mpending->m_next
		    || cd->mpending->m_len > cd->resid)
			cd->mpending = m_split(cd->mpending,
			    cd->resid, M_WAITOK);
		else
			cd->mpending = NULL;
		if (cd->mreq)
			m_last(cd->mreq)->m_next = m;
		else
			cd->mreq = m;
		while (m) {
			cd->resid -= m->m_len;
			m = m->m_next;
		}
	}

	/*
	 * Block receive upcalls if we have more data pending,
	 * otherwise report our need.
	 */
	if (cd->mpending)
		so->so_rcv.sb_lowat = INT_MAX;
	else
		so->so_rcv.sb_lowat =
		    imax(1, imin(cd->resid, so->so_rcv.sb_hiwat / 2));
	return (TRUE);
}

static bool_t
svc_vc_recv(SVCXPRT *xprt, struct rpc_msg *msg,
    struct sockaddr **addrp, struct mbuf **mp)
{
	struct cf_conn *cd = (struct cf_conn *) xprt->xp_p1;
	struct uio uio;
	struct mbuf *m;
	struct socket* so = xprt->xp_socket;
	XDR xdrs;
	int error, rcvflag;
	uint32_t xid_plus_direction[2];

	/*
	 * Serialise access to the socket and our own record parsing
	 * state.
	 */
	sx_xlock(&xprt->xp_lock);

	for (;;) {
		/* If we have no request ready, check pending queue. */
		while (cd->mpending &&
		    (cd->mreq == NULL || cd->resid != 0 || !cd->eor)) {
			if (!svc_vc_process_pending(xprt))
				break;
		}

		/* Process and return complete request in cd->mreq. */
		if (cd->mreq != NULL && cd->resid == 0 && cd->eor) {

			/*
			 * Now, check for a backchannel reply.
			 * The XID is in the first uint32_t of the reply
			 * and the message direction is the second one.
			 */
			if ((cd->mreq->m_len >= sizeof(xid_plus_direction) ||
			    m_length(cd->mreq, NULL) >=
			    sizeof(xid_plus_direction)) &&
			    xprt->xp_p2 != NULL) {
				m_copydata(cd->mreq, 0,
				    sizeof(xid_plus_direction),
				    (char *)xid_plus_direction);
				xid_plus_direction[0] =
				    ntohl(xid_plus_direction[0]);
				xid_plus_direction[1] =
				    ntohl(xid_plus_direction[1]);
				/* Check message direction. */
				if (xid_plus_direction[1] == REPLY) {
					clnt_bck_svccall(xprt->xp_p2,
					    cd->mreq,
					    xid_plus_direction[0]);
					cd->mreq = NULL;
					continue;
				}
			}

			xdrmbuf_create(&xdrs, cd->mreq, XDR_DECODE);
			cd->mreq = NULL;

			/* Check for next request in a pending queue. */
			svc_vc_process_pending(xprt);
			if (cd->mreq == NULL || cd->resid != 0) {
				SOCKBUF_LOCK(&so->so_rcv);
				if (!soreadable(so))
					xprt_inactive_self(xprt);
				SOCKBUF_UNLOCK(&so->so_rcv);
			}

			sx_xunlock(&xprt->xp_lock);

			if (! xdr_callmsg(&xdrs, msg)) {
				XDR_DESTROY(&xdrs);
				return (FALSE);
			}

			*addrp = NULL;
			*mp = xdrmbuf_getall(&xdrs);
			XDR_DESTROY(&xdrs);

			return (TRUE);
		}

		/*
		 * The socket upcall calls xprt_active() which will eventually
		 * cause the server to call us here. We attempt to
		 * read as much as possible from the socket and put
		 * the result in cd->mpending. If the read fails,
		 * we have drained both cd->mpending and the socket so
		 * we can call xprt_inactive().
		 */
		uio.uio_resid = 1000000000;
		uio.uio_td = curthread;
		m = NULL;
		rcvflag = MSG_DONTWAIT;
		error = soreceive(so, NULL, &uio, &m, NULL, &rcvflag);

		if (error == EWOULDBLOCK) {
			/*
			 * We must re-test for readability after
			 * taking the lock to protect us in the case
			 * where a new packet arrives on the socket
			 * after our call to soreceive fails with
			 * EWOULDBLOCK.
			 */
			SOCKBUF_LOCK(&so->so_rcv);
			if (!soreadable(so))
				xprt_inactive_self(xprt);
			SOCKBUF_UNLOCK(&so->so_rcv);
			sx_xunlock(&xprt->xp_lock);
			return (FALSE);
		}

		if (error) {
			SOCKBUF_LOCK(&so->so_rcv);
			if (xprt->xp_upcallset) {
				xprt->xp_upcallset = 0;
				soupcall_clear(so, SO_RCV);
			}
			SOCKBUF_UNLOCK(&so->so_rcv);
			xprt_inactive_self(xprt);
			cd->strm_stat = XPRT_DIED;
			sx_xunlock(&xprt->xp_lock);
			return (FALSE);
		}

		if (!m) {
			/*
			 * EOF - the other end has closed the socket.
			 */
			xprt_inactive_self(xprt);
			cd->strm_stat = XPRT_DIED;
			sx_xunlock(&xprt->xp_lock);
			return (FALSE);
		}

		if (cd->mpending)
			m_last(cd->mpending)->m_next = m;
		else
			cd->mpending = m;
	}
}

static bool_t
svc_vc_backchannel_recv(SVCXPRT *xprt, struct rpc_msg *msg,
    struct sockaddr **addrp, struct mbuf **mp)
{
	struct cf_conn *cd = (struct cf_conn *) xprt->xp_p1;
	struct ct_data *ct;
	struct mbuf *m;
	XDR xdrs;

	sx_xlock(&xprt->xp_lock);
	ct = (struct ct_data *)xprt->xp_p2;
	if (ct == NULL) {
		sx_xunlock(&xprt->xp_lock);
		return (FALSE);
	}
	mtx_lock(&ct->ct_lock);
	m = cd->mreq;
	if (m == NULL) {
		xprt_inactive_self(xprt);
		mtx_unlock(&ct->ct_lock);
		sx_xunlock(&xprt->xp_lock);
		return (FALSE);
	}
	cd->mreq = m->m_nextpkt;
	mtx_unlock(&ct->ct_lock);
	sx_xunlock(&xprt->xp_lock);

	xdrmbuf_create(&xdrs, m, XDR_DECODE);
	if (! xdr_callmsg(&xdrs, msg)) {
		XDR_DESTROY(&xdrs);
		return (FALSE);
	}
	*addrp = NULL;
	*mp = xdrmbuf_getall(&xdrs);
	XDR_DESTROY(&xdrs);
	return (TRUE);
}

static bool_t
svc_vc_reply(SVCXPRT *xprt, struct rpc_msg *msg,
    struct sockaddr *addr, struct mbuf *m, uint32_t *seq)
{
	XDR xdrs;
	struct mbuf *mrep;
	bool_t stat = TRUE;
	int error, len;

	/*
	 * Leave space for record mark.
	 */
	mrep = m_gethdr(M_WAITOK, MT_DATA);
	mrep->m_data += sizeof(uint32_t);

	xdrmbuf_create(&xdrs, mrep, XDR_ENCODE);

	if (msg->rm_reply.rp_stat == MSG_ACCEPTED &&
	    msg->rm_reply.rp_acpt.ar_stat == SUCCESS) {
		if (!xdr_replymsg(&xdrs, msg))
			stat = FALSE;
		else
			xdrmbuf_append(&xdrs, m);
	} else {
		stat = xdr_replymsg(&xdrs, msg);
	}

	if (stat) {
		m_fixhdr(mrep);

		/*
		 * Prepend a record marker containing the reply length.
		 */
		M_PREPEND(mrep, sizeof(uint32_t), M_WAITOK);
		len = mrep->m_pkthdr.len;
		*mtod(mrep, uint32_t *) =
			htonl(0x80000000 | (len - sizeof(uint32_t)));
		atomic_add_32(&xprt->xp_snd_cnt, len);
		error = sosend(xprt->xp_socket, NULL, NULL, mrep, NULL,
		    0, curthread);
		if (!error) {
			atomic_add_rel_32(&xprt->xp_snt_cnt, len);
			if (seq)
				*seq = xprt->xp_snd_cnt;
			stat = TRUE;
		} else
			atomic_subtract_32(&xprt->xp_snd_cnt, len);
	} else {
		m_freem(mrep);
	}

	XDR_DESTROY(&xdrs);

	return (stat);
}

static bool_t
svc_vc_backchannel_reply(SVCXPRT *xprt, struct rpc_msg *msg,
    struct sockaddr *addr, struct mbuf *m, uint32_t *seq)
{
	struct ct_data *ct;
	XDR xdrs;
	struct mbuf *mrep;
	bool_t stat = TRUE;
	int error;

	/*
	 * Leave space for record mark.
	 */
	mrep = m_gethdr(M_WAITOK, MT_DATA);
	mrep->m_data += sizeof(uint32_t);

	xdrmbuf_create(&xdrs, mrep, XDR_ENCODE);

	if (msg->rm_reply.rp_stat == MSG_ACCEPTED &&
	    msg->rm_reply.rp_acpt.ar_stat == SUCCESS) {
		if (!xdr_replymsg(&xdrs, msg))
			stat = FALSE;
		else
			xdrmbuf_append(&xdrs, m);
	} else {
		stat = xdr_replymsg(&xdrs, msg);
	}

	if (stat) {
		m_fixhdr(mrep);

		/*
		 * Prepend a record marker containing the reply length.
		 */
		M_PREPEND(mrep, sizeof(uint32_t), M_WAITOK);
		*mtod(mrep, uint32_t *) =
			htonl(0x80000000 | (mrep->m_pkthdr.len
				- sizeof(uint32_t)));
		sx_xlock(&xprt->xp_lock);
		ct = (struct ct_data *)xprt->xp_p2;
		if (ct != NULL)
			error = sosend(ct->ct_socket, NULL, NULL, mrep, NULL,
			    0, curthread);
		else
			error = EPIPE;
		sx_xunlock(&xprt->xp_lock);
		if (!error) {
			stat = TRUE;
		}
	} else {
		m_freem(mrep);
	}

	XDR_DESTROY(&xdrs);

	return (stat);
}

static bool_t
svc_vc_null()
{

	return (FALSE);
}

static int
svc_vc_soupcall(struct socket *so, void *arg, int waitflag)
{
	SVCXPRT *xprt = (SVCXPRT *) arg;

	if (soreadable(xprt->xp_socket))
		xprt_active(xprt);
	return (SU_OK);
}

static int
svc_vc_rendezvous_soupcall(struct socket *head, void *arg, int waitflag)
{
	SVCXPRT *xprt = (SVCXPRT *) arg;

	if (!TAILQ_EMPTY(&head->sol_comp))
		xprt_active(xprt);
	return (SU_OK);
}

#if 0
/*
 * Get the effective UID of the sending process. Used by rpcbind, keyserv
 * and rpc.yppasswdd on AF_LOCAL.
 */
int
__rpc_get_local_uid(SVCXPRT *transp, uid_t *uid) {
	int sock, ret;
	gid_t egid;
	uid_t euid;
	struct sockaddr *sa;

	sock = transp->xp_fd;
	sa = (struct sockaddr *)transp->xp_rtaddr;
	if (sa->sa_family == AF_LOCAL) {
		ret = getpeereid(sock, &euid, &egid);
		if (ret == 0)
			*uid = euid;
		return (ret);
	} else
		return (-1);
}
#endif
