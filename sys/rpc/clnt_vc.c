/*	$NetBSD: clnt_vc.c,v 1.4 2000/07/14 08:40:42 fvdl Exp $	*/

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
static char *sccsid2 = "@(#)clnt_tcp.c 1.37 87/10/05 Copyr 1984 Sun Micro";
static char *sccsid = "@(#)clnt_tcp.c	2.2 88/08/01 4.0 RPCSRC";
static char sccsid3[] = "@(#)clnt_vc.c 1.19 89/03/16 Copyr 1988 Sun Micro";
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
 
/*
 * clnt_tcp.c, Implements a TCP/IP based, client side RPC.
 *
 * Copyright (C) 1984, Sun Microsystems, Inc.
 *
 * TCP based RPC supports 'batched calls'.
 * A sequence of calls may be batched-up in a send buffer.  The rpc call
 * return immediately to the client even though the call was not necessarily
 * sent.  The batching occurs if the results' xdr routine is NULL (0) AND
 * the rpc timeout value is zero (see clnt.h, rpc).
 *
 * Clients should NOT casually batch calls that in fact return results; that is,
 * the server side should be aware that a call is batched and not produce any
 * return message.  Batched calls that produce many result messages can
 * deadlock (netlock) the client and the server....
 *
 * Now go hang yourself.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sx.h>
#include <sys/syslog.h>
#include <sys/time.h>
#include <sys/uio.h>

#include <net/vnet.h>

#include <netinet/tcp.h>

#include <rpc/rpc.h>
#include <rpc/rpc_com.h>
#include <rpc/krpc.h>

struct cmessage {
        struct cmsghdr cmsg;
        struct cmsgcred cmcred;
};

static enum clnt_stat clnt_vc_call(CLIENT *, struct rpc_callextra *,
    rpcproc_t, struct mbuf *, struct mbuf **, struct timeval);
static void clnt_vc_geterr(CLIENT *, struct rpc_err *);
static bool_t clnt_vc_freeres(CLIENT *, xdrproc_t, void *);
static void clnt_vc_abort(CLIENT *);
static bool_t clnt_vc_control(CLIENT *, u_int, void *);
static void clnt_vc_close(CLIENT *);
static void clnt_vc_destroy(CLIENT *);
static bool_t time_not_ok(struct timeval *);
static int clnt_vc_soupcall(struct socket *so, void *arg, int waitflag);

static struct clnt_ops clnt_vc_ops = {
	.cl_call =	clnt_vc_call,
	.cl_abort =	clnt_vc_abort,
	.cl_geterr =	clnt_vc_geterr,
	.cl_freeres =	clnt_vc_freeres,
	.cl_close =	clnt_vc_close,
	.cl_destroy =	clnt_vc_destroy,
	.cl_control =	clnt_vc_control
};

static void clnt_vc_upcallsdone(struct ct_data *);

static int	fake_wchan;

/*
 * Create a client handle for a connection.
 * Default options are set, which the user can change using clnt_control()'s.
 * The rpc/vc package does buffering similar to stdio, so the client
 * must pick send and receive buffer sizes, 0 => use the default.
 * NB: fd is copied into a private area.
 * NB: The rpch->cl_auth is set null authentication. Caller may wish to
 * set this something more useful.
 *
 * fd should be an open socket
 */
CLIENT *
clnt_vc_create(
	struct socket *so,		/* open file descriptor */
	struct sockaddr *raddr,		/* servers address */
	const rpcprog_t prog,		/* program number */
	const rpcvers_t vers,		/* version number */
	size_t sendsz,			/* buffer recv size */
	size_t recvsz,			/* buffer send size */
	int intrflag)			/* interruptible */
{
	CLIENT *cl;			/* client handle */
	struct ct_data *ct = NULL;	/* client handle */
	struct timeval now;
	struct rpc_msg call_msg;
	static uint32_t disrupt;
	struct __rpc_sockinfo si;
	XDR xdrs;
	int error, interrupted, one = 1, sleep_flag;
	struct sockopt sopt;

	if (disrupt == 0)
		disrupt = (uint32_t)(long)raddr;

	cl = (CLIENT *)mem_alloc(sizeof (*cl));
	ct = (struct ct_data *)mem_alloc(sizeof (*ct));

	mtx_init(&ct->ct_lock, "ct->ct_lock", NULL, MTX_DEF);
	ct->ct_threads = 0;
	ct->ct_closing = FALSE;
	ct->ct_closed = FALSE;
	ct->ct_upcallrefs = 0;

	if ((so->so_state & (SS_ISCONNECTED|SS_ISCONFIRMING)) == 0) {
		error = soconnect(so, raddr, curthread);
		SOCK_LOCK(so);
		interrupted = 0;
		sleep_flag = PSOCK;
		if (intrflag != 0)
			sleep_flag |= PCATCH;
		while ((so->so_state & SS_ISCONNECTING)
		    && so->so_error == 0) {
			error = msleep(&so->so_timeo, SOCK_MTX(so),
			    sleep_flag, "connec", 0);
			if (error) {
				if (error == EINTR || error == ERESTART)
					interrupted = 1;
				break;
			}
		}
		if (error == 0) {
			error = so->so_error;
			so->so_error = 0;
		}
		SOCK_UNLOCK(so);
		if (error) {
			if (!interrupted)
				so->so_state &= ~SS_ISCONNECTING;
			rpc_createerr.cf_stat = RPC_SYSTEMERROR;
			rpc_createerr.cf_error.re_errno = error;
			goto err;
		}
	}

	if (!__rpc_socket2sockinfo(so, &si)) {
		goto err;
	}

	if (so->so_proto->pr_flags & PR_CONNREQUIRED) {
		bzero(&sopt, sizeof(sopt));
		sopt.sopt_dir = SOPT_SET;
		sopt.sopt_level = SOL_SOCKET;
		sopt.sopt_name = SO_KEEPALIVE;
		sopt.sopt_val = &one;
		sopt.sopt_valsize = sizeof(one);
		sosetopt(so, &sopt);
	}

	if (so->so_proto->pr_protocol == IPPROTO_TCP) {
		bzero(&sopt, sizeof(sopt));
		sopt.sopt_dir = SOPT_SET;
		sopt.sopt_level = IPPROTO_TCP;
		sopt.sopt_name = TCP_NODELAY;
		sopt.sopt_val = &one;
		sopt.sopt_valsize = sizeof(one);
		sosetopt(so, &sopt);
	}

	ct->ct_closeit = FALSE;

	/*
	 * Set up private data struct
	 */
	ct->ct_socket = so;
	ct->ct_wait.tv_sec = -1;
	ct->ct_wait.tv_usec = -1;
	memcpy(&ct->ct_addr, raddr, raddr->sa_len);

	/*
	 * Initialize call message
	 */
	getmicrotime(&now);
	ct->ct_xid = ((uint32_t)++disrupt) ^ __RPC_GETXID(&now);
	call_msg.rm_xid = ct->ct_xid;
	call_msg.rm_direction = CALL;
	call_msg.rm_call.cb_rpcvers = RPC_MSG_VERSION;
	call_msg.rm_call.cb_prog = (uint32_t)prog;
	call_msg.rm_call.cb_vers = (uint32_t)vers;

	/*
	 * pre-serialize the static part of the call msg and stash it away
	 */
	xdrmem_create(&xdrs, ct->ct_mcallc, MCALL_MSG_SIZE,
	    XDR_ENCODE);
	if (! xdr_callhdr(&xdrs, &call_msg)) {
		if (ct->ct_closeit) {
			soclose(ct->ct_socket);
		}
		goto err;
	}
	ct->ct_mpos = XDR_GETPOS(&xdrs);
	XDR_DESTROY(&xdrs);
	ct->ct_waitchan = "rpcrecv";
	ct->ct_waitflag = 0;

	/*
	 * Create a client handle which uses xdrrec for serialization
	 * and authnone for authentication.
	 */
	sendsz = __rpc_get_t_size(si.si_af, si.si_proto, (int)sendsz);
	recvsz = __rpc_get_t_size(si.si_af, si.si_proto, (int)recvsz);
	error = soreserve(ct->ct_socket, sendsz, recvsz);
	if (error != 0) {
		if (ct->ct_closeit) {
			soclose(ct->ct_socket);
		}
		goto err;
	}
	cl->cl_refs = 1;
	cl->cl_ops = &clnt_vc_ops;
	cl->cl_private = ct;
	cl->cl_auth = authnone_create();

	SOCKBUF_LOCK(&ct->ct_socket->so_rcv);
	soupcall_set(ct->ct_socket, SO_RCV, clnt_vc_soupcall, ct);
	SOCKBUF_UNLOCK(&ct->ct_socket->so_rcv);

	ct->ct_record = NULL;
	ct->ct_record_resid = 0;
	TAILQ_INIT(&ct->ct_pending);
	return (cl);

err:
	mtx_destroy(&ct->ct_lock);
	mem_free(ct, sizeof (struct ct_data));
	mem_free(cl, sizeof (CLIENT));

	return ((CLIENT *)NULL);
}

static enum clnt_stat
clnt_vc_call(
	CLIENT		*cl,		/* client handle */
	struct rpc_callextra *ext,	/* call metadata */
	rpcproc_t	proc,		/* procedure number */
	struct mbuf	*args,		/* pointer to args */
	struct mbuf	**resultsp,	/* pointer to results */
	struct timeval	utimeout)
{
	struct ct_data *ct = (struct ct_data *) cl->cl_private;
	AUTH *auth;
	struct rpc_err *errp;
	enum clnt_stat stat;
	XDR xdrs;
	struct rpc_msg reply_msg;
	bool_t ok;
	int nrefreshes = 2;		/* number of times to refresh cred */
	struct timeval timeout;
	uint32_t xid;
	struct mbuf *mreq = NULL, *results;
	struct ct_request *cr;
	int error, trycnt;

	cr = malloc(sizeof(struct ct_request), M_RPC, M_WAITOK);

	mtx_lock(&ct->ct_lock);

	if (ct->ct_closing || ct->ct_closed) {
		mtx_unlock(&ct->ct_lock);
		free(cr, M_RPC);
		return (RPC_CANTSEND);
	}
	ct->ct_threads++;

	if (ext) {
		auth = ext->rc_auth;
		errp = &ext->rc_err;
	} else {
		auth = cl->cl_auth;
		errp = &ct->ct_error;
	}

	cr->cr_mrep = NULL;
	cr->cr_error = 0;

	if (ct->ct_wait.tv_usec == -1) {
		timeout = utimeout;	/* use supplied timeout */
	} else {
		timeout = ct->ct_wait;	/* use default timeout */
	}

	/*
	 * After 15sec of looping, allow it to return RPC_CANTSEND, which will
	 * cause the clnt_reconnect layer to create a new TCP connection.
	 */
	trycnt = 15 * hz;
call_again:
	mtx_assert(&ct->ct_lock, MA_OWNED);
	if (ct->ct_closing || ct->ct_closed) {
		ct->ct_threads--;
		wakeup(ct);
		mtx_unlock(&ct->ct_lock);
		free(cr, M_RPC);
		return (RPC_CANTSEND);
	}

	ct->ct_xid++;
	xid = ct->ct_xid;

	mtx_unlock(&ct->ct_lock);

	/*
	 * Leave space to pre-pend the record mark.
	 */
	mreq = m_gethdr(M_WAITOK, MT_DATA);
	mreq->m_data += sizeof(uint32_t);
	KASSERT(ct->ct_mpos + sizeof(uint32_t) <= MHLEN,
	    ("RPC header too big"));
	bcopy(ct->ct_mcallc, mreq->m_data, ct->ct_mpos);
	mreq->m_len = ct->ct_mpos;

	/*
	 * The XID is the first thing in the request.
	 */
	*mtod(mreq, uint32_t *) = htonl(xid);

	xdrmbuf_create(&xdrs, mreq, XDR_ENCODE);

	errp->re_status = stat = RPC_SUCCESS;

	if ((! XDR_PUTINT32(&xdrs, &proc)) ||
	    (! AUTH_MARSHALL(auth, xid, &xdrs,
		m_copym(args, 0, M_COPYALL, M_WAITOK)))) {
		errp->re_status = stat = RPC_CANTENCODEARGS;
		mtx_lock(&ct->ct_lock);
		goto out;
	}
	mreq->m_pkthdr.len = m_length(mreq, NULL);

	/*
	 * Prepend a record marker containing the packet length.
	 */
	M_PREPEND(mreq, sizeof(uint32_t), M_WAITOK);
	*mtod(mreq, uint32_t *) =
		htonl(0x80000000 | (mreq->m_pkthdr.len - sizeof(uint32_t)));

	cr->cr_xid = xid;
	mtx_lock(&ct->ct_lock);
	/*
	 * Check to see if the other end has already started to close down
	 * the connection. The upcall will have set ct_error.re_status
	 * to RPC_CANTRECV if this is the case.
	 * If the other end starts to close down the connection after this
	 * point, it will be detected later when cr_error is checked,
	 * since the request is in the ct_pending queue.
	 */
	if (ct->ct_error.re_status == RPC_CANTRECV) {
		if (errp != &ct->ct_error) {
			errp->re_errno = ct->ct_error.re_errno;
			errp->re_status = RPC_CANTRECV;
		}
		stat = RPC_CANTRECV;
		goto out;
	}
	TAILQ_INSERT_TAIL(&ct->ct_pending, cr, cr_link);
	mtx_unlock(&ct->ct_lock);

	/*
	 * sosend consumes mreq.
	 */
	error = sosend(ct->ct_socket, NULL, NULL, mreq, NULL, 0, curthread);
	mreq = NULL;
	if (error == EMSGSIZE || (error == ERESTART &&
	    (ct->ct_waitflag & PCATCH) == 0 && trycnt-- > 0)) {
		SOCKBUF_LOCK(&ct->ct_socket->so_snd);
		sbwait(&ct->ct_socket->so_snd);
		SOCKBUF_UNLOCK(&ct->ct_socket->so_snd);
		AUTH_VALIDATE(auth, xid, NULL, NULL);
		mtx_lock(&ct->ct_lock);
		TAILQ_REMOVE(&ct->ct_pending, cr, cr_link);
		/* Sleep for 1 clock tick before trying the sosend() again. */
		msleep(&fake_wchan, &ct->ct_lock, 0, "rpclpsnd", 1);
		goto call_again;
	}

	reply_msg.acpted_rply.ar_verf.oa_flavor = AUTH_NULL;
	reply_msg.acpted_rply.ar_verf.oa_base = cr->cr_verf;
	reply_msg.acpted_rply.ar_verf.oa_length = 0;
	reply_msg.acpted_rply.ar_results.where = NULL;
	reply_msg.acpted_rply.ar_results.proc = (xdrproc_t)xdr_void;

	mtx_lock(&ct->ct_lock);
	if (error) {
		TAILQ_REMOVE(&ct->ct_pending, cr, cr_link);
		errp->re_errno = error;
		errp->re_status = stat = RPC_CANTSEND;
		goto out;
	}

	/*
	 * Check to see if we got an upcall while waiting for the
	 * lock. In both these cases, the request has been removed
	 * from ct->ct_pending.
	 */
	if (cr->cr_error) {
		TAILQ_REMOVE(&ct->ct_pending, cr, cr_link);
		errp->re_errno = cr->cr_error;
		errp->re_status = stat = RPC_CANTRECV;
		goto out;
	}
	if (cr->cr_mrep) {
		TAILQ_REMOVE(&ct->ct_pending, cr, cr_link);
		goto got_reply;
	}

	/*
	 * Hack to provide rpc-based message passing
	 */
	if (timeout.tv_sec == 0 && timeout.tv_usec == 0) {
		TAILQ_REMOVE(&ct->ct_pending, cr, cr_link);
		errp->re_status = stat = RPC_TIMEDOUT;
		goto out;
	}

	error = msleep(cr, &ct->ct_lock, ct->ct_waitflag, ct->ct_waitchan,
	    tvtohz(&timeout));

	TAILQ_REMOVE(&ct->ct_pending, cr, cr_link);

	if (error) {
		/*
		 * The sleep returned an error so our request is still
		 * on the list. Turn the error code into an
		 * appropriate client status.
		 */
		errp->re_errno = error;
		switch (error) {
		case EINTR:
			stat = RPC_INTR;
			break;
		case EWOULDBLOCK:
			stat = RPC_TIMEDOUT;
			break;
		default:
			stat = RPC_CANTRECV;
		}
		errp->re_status = stat;
		goto out;
	} else {
		/*
		 * We were woken up by the upcall.  If the
		 * upcall had a receive error, report that,
		 * otherwise we have a reply.
		 */
		if (cr->cr_error) {
			errp->re_errno = cr->cr_error;
			errp->re_status = stat = RPC_CANTRECV;
			goto out;
		}
	}

got_reply:
	/*
	 * Now decode and validate the response. We need to drop the
	 * lock since xdr_replymsg may end up sleeping in malloc.
	 */
	mtx_unlock(&ct->ct_lock);

	if (ext && ext->rc_feedback)
		ext->rc_feedback(FEEDBACK_OK, proc, ext->rc_feedback_arg);

	xdrmbuf_create(&xdrs, cr->cr_mrep, XDR_DECODE);
	ok = xdr_replymsg(&xdrs, &reply_msg);
	cr->cr_mrep = NULL;

	if (ok) {
		if ((reply_msg.rm_reply.rp_stat == MSG_ACCEPTED) &&
		    (reply_msg.acpted_rply.ar_stat == SUCCESS))
			errp->re_status = stat = RPC_SUCCESS;
		else
			stat = _seterr_reply(&reply_msg, errp);

		if (stat == RPC_SUCCESS) {
			results = xdrmbuf_getall(&xdrs);
			if (!AUTH_VALIDATE(auth, xid,
				&reply_msg.acpted_rply.ar_verf,
				&results)) {
				errp->re_status = stat = RPC_AUTHERROR;
				errp->re_why = AUTH_INVALIDRESP;
			} else {
				KASSERT(results,
				    ("auth validated but no result"));
				*resultsp = results;
			}
		}		/* end successful completion */
		/*
		 * If unsuccessful AND error is an authentication error
		 * then refresh credentials and try again, else break
		 */
		else if (stat == RPC_AUTHERROR)
			/* maybe our credentials need to be refreshed ... */
			if (nrefreshes > 0 &&
			    AUTH_REFRESH(auth, &reply_msg)) {
				nrefreshes--;
				XDR_DESTROY(&xdrs);
				mtx_lock(&ct->ct_lock);
				goto call_again;
			}
		/* end of unsuccessful completion */
	}	/* end of valid reply message */
	else {
		errp->re_status = stat = RPC_CANTDECODERES;
	}
	XDR_DESTROY(&xdrs);
	mtx_lock(&ct->ct_lock);
out:
	mtx_assert(&ct->ct_lock, MA_OWNED);

	KASSERT(stat != RPC_SUCCESS || *resultsp,
	    ("RPC_SUCCESS without reply"));

	if (mreq)
		m_freem(mreq);
	if (cr->cr_mrep)
		m_freem(cr->cr_mrep);

	ct->ct_threads--;
	if (ct->ct_closing)
		wakeup(ct);
		
	mtx_unlock(&ct->ct_lock);

	if (auth && stat != RPC_SUCCESS)
		AUTH_VALIDATE(auth, xid, NULL, NULL);

	free(cr, M_RPC);

	return (stat);
}

static void
clnt_vc_geterr(CLIENT *cl, struct rpc_err *errp)
{
	struct ct_data *ct = (struct ct_data *) cl->cl_private;

	*errp = ct->ct_error;
}

static bool_t
clnt_vc_freeres(CLIENT *cl, xdrproc_t xdr_res, void *res_ptr)
{
	XDR xdrs;
	bool_t dummy;

	xdrs.x_op = XDR_FREE;
	dummy = (*xdr_res)(&xdrs, res_ptr);

	return (dummy);
}

/*ARGSUSED*/
static void
clnt_vc_abort(CLIENT *cl)
{
}

static bool_t
clnt_vc_control(CLIENT *cl, u_int request, void *info)
{
	struct ct_data *ct = (struct ct_data *)cl->cl_private;
	void *infop = info;
	SVCXPRT *xprt;

	mtx_lock(&ct->ct_lock);

	switch (request) {
	case CLSET_FD_CLOSE:
		ct->ct_closeit = TRUE;
		mtx_unlock(&ct->ct_lock);
		return (TRUE);
	case CLSET_FD_NCLOSE:
		ct->ct_closeit = FALSE;
		mtx_unlock(&ct->ct_lock);
		return (TRUE);
	default:
		break;
	}

	/* for other requests which use info */
	if (info == NULL) {
		mtx_unlock(&ct->ct_lock);
		return (FALSE);
	}
	switch (request) {
	case CLSET_TIMEOUT:
		if (time_not_ok((struct timeval *)info)) {
			mtx_unlock(&ct->ct_lock);
			return (FALSE);
		}
		ct->ct_wait = *(struct timeval *)infop;
		break;
	case CLGET_TIMEOUT:
		*(struct timeval *)infop = ct->ct_wait;
		break;
	case CLGET_SERVER_ADDR:
		(void) memcpy(info, &ct->ct_addr, (size_t)ct->ct_addr.ss_len);
		break;
	case CLGET_SVC_ADDR:
		/*
		 * Slightly different semantics to userland - we use
		 * sockaddr instead of netbuf.
		 */
		memcpy(info, &ct->ct_addr, ct->ct_addr.ss_len);
		break;
	case CLSET_SVC_ADDR:		/* set to new address */
		mtx_unlock(&ct->ct_lock);
		return (FALSE);
	case CLGET_XID:
		*(uint32_t *)info = ct->ct_xid;
		break;
	case CLSET_XID:
		/* This will set the xid of the NEXT call */
		/* decrement by 1 as clnt_vc_call() increments once */
		ct->ct_xid = *(uint32_t *)info - 1;
		break;
	case CLGET_VERS:
		/*
		 * This RELIES on the information that, in the call body,
		 * the version number field is the fifth field from the
		 * beginning of the RPC header. MUST be changed if the
		 * call_struct is changed
		 */
		*(uint32_t *)info =
		    ntohl(*(uint32_t *)(void *)(ct->ct_mcallc +
		    4 * BYTES_PER_XDR_UNIT));
		break;

	case CLSET_VERS:
		*(uint32_t *)(void *)(ct->ct_mcallc +
		    4 * BYTES_PER_XDR_UNIT) =
		    htonl(*(uint32_t *)info);
		break;

	case CLGET_PROG:
		/*
		 * This RELIES on the information that, in the call body,
		 * the program number field is the fourth field from the
		 * beginning of the RPC header. MUST be changed if the
		 * call_struct is changed
		 */
		*(uint32_t *)info =
		    ntohl(*(uint32_t *)(void *)(ct->ct_mcallc +
		    3 * BYTES_PER_XDR_UNIT));
		break;

	case CLSET_PROG:
		*(uint32_t *)(void *)(ct->ct_mcallc +
		    3 * BYTES_PER_XDR_UNIT) =
		    htonl(*(uint32_t *)info);
		break;

	case CLSET_WAITCHAN:
		ct->ct_waitchan = (const char *)info;
		break;

	case CLGET_WAITCHAN:
		*(const char **) info = ct->ct_waitchan;
		break;

	case CLSET_INTERRUPTIBLE:
		if (*(int *) info)
			ct->ct_waitflag = PCATCH;
		else
			ct->ct_waitflag = 0;
		break;

	case CLGET_INTERRUPTIBLE:
		if (ct->ct_waitflag)
			*(int *) info = TRUE;
		else
			*(int *) info = FALSE;
		break;

	case CLSET_BACKCHANNEL:
		xprt = (SVCXPRT *)info;
		if (ct->ct_backchannelxprt == NULL) {
			xprt->xp_p2 = ct;
			ct->ct_backchannelxprt = xprt;
		}
		break;

	default:
		mtx_unlock(&ct->ct_lock);
		return (FALSE);
	}

	mtx_unlock(&ct->ct_lock);
	return (TRUE);
}

static void
clnt_vc_close(CLIENT *cl)
{
	struct ct_data *ct = (struct ct_data *) cl->cl_private;
	struct ct_request *cr;

	mtx_lock(&ct->ct_lock);

	if (ct->ct_closed) {
		mtx_unlock(&ct->ct_lock);
		return;
	}

	if (ct->ct_closing) {
		while (ct->ct_closing)
			msleep(ct, &ct->ct_lock, 0, "rpcclose", 0);
		KASSERT(ct->ct_closed, ("client should be closed"));
		mtx_unlock(&ct->ct_lock);
		return;
	}

	if (ct->ct_socket) {
		ct->ct_closing = TRUE;
		mtx_unlock(&ct->ct_lock);

		SOCKBUF_LOCK(&ct->ct_socket->so_rcv);
		soupcall_clear(ct->ct_socket, SO_RCV);
		clnt_vc_upcallsdone(ct);
		SOCKBUF_UNLOCK(&ct->ct_socket->so_rcv);

		/*
		 * Abort any pending requests and wait until everyone
		 * has finished with clnt_vc_call.
		 */
		mtx_lock(&ct->ct_lock);
		TAILQ_FOREACH(cr, &ct->ct_pending, cr_link) {
			cr->cr_xid = 0;
			cr->cr_error = ESHUTDOWN;
			wakeup(cr);
		}

		while (ct->ct_threads)
			msleep(ct, &ct->ct_lock, 0, "rpcclose", 0);
	}

	ct->ct_closing = FALSE;
	ct->ct_closed = TRUE;
	mtx_unlock(&ct->ct_lock);
	wakeup(ct);
}

static void
clnt_vc_destroy(CLIENT *cl)
{
	struct ct_data *ct = (struct ct_data *) cl->cl_private;
	struct socket *so = NULL;
	SVCXPRT *xprt;

	clnt_vc_close(cl);

	mtx_lock(&ct->ct_lock);
	xprt = ct->ct_backchannelxprt;
	ct->ct_backchannelxprt = NULL;
	if (xprt != NULL) {
		mtx_unlock(&ct->ct_lock);	/* To avoid a LOR. */
		sx_xlock(&xprt->xp_lock);
		mtx_lock(&ct->ct_lock);
		xprt->xp_p2 = NULL;
		sx_xunlock(&xprt->xp_lock);
	}

	if (ct->ct_socket) {
		if (ct->ct_closeit) {
			so = ct->ct_socket;
		}
	}

	mtx_unlock(&ct->ct_lock);

	mtx_destroy(&ct->ct_lock);
	if (so) {
		soshutdown(so, SHUT_WR);
		soclose(so);
	}
	mem_free(ct, sizeof(struct ct_data));
	if (cl->cl_netid && cl->cl_netid[0])
		mem_free(cl->cl_netid, strlen(cl->cl_netid) +1);
	if (cl->cl_tp && cl->cl_tp[0])
		mem_free(cl->cl_tp, strlen(cl->cl_tp) +1);
	mem_free(cl, sizeof(CLIENT));
}

/*
 * Make sure that the time is not garbage.   -1 value is disallowed.
 * Note this is different from time_not_ok in clnt_dg.c
 */
static bool_t
time_not_ok(struct timeval *t)
{
	return (t->tv_sec <= -1 || t->tv_sec > 100000000 ||
		t->tv_usec <= -1 || t->tv_usec > 1000000);
}

int
clnt_vc_soupcall(struct socket *so, void *arg, int waitflag)
{
	struct ct_data *ct = (struct ct_data *) arg;
	struct uio uio;
	struct mbuf *m, *m2;
	struct ct_request *cr;
	int error, rcvflag, foundreq;
	uint32_t xid_plus_direction[2], header;
	bool_t do_read;
	SVCXPRT *xprt;
	struct cf_conn *cd;

	CTASSERT(sizeof(xid_plus_direction) == 2 * sizeof(uint32_t));
	ct->ct_upcallrefs++;
	uio.uio_td = curthread;
	do {
		/*
		 * If ct_record_resid is zero, we are waiting for a
		 * record mark.
		 */
		if (ct->ct_record_resid == 0) {

			/*
			 * Make sure there is either a whole record
			 * mark in the buffer or there is some other
			 * error condition
			 */
			do_read = FALSE;
			if (sbavail(&so->so_rcv) >= sizeof(uint32_t)
			    || (so->so_rcv.sb_state & SBS_CANTRCVMORE)
			    || so->so_error)
				do_read = TRUE;

			if (!do_read)
				break;

			SOCKBUF_UNLOCK(&so->so_rcv);
			uio.uio_resid = sizeof(uint32_t);
			m = NULL;
			rcvflag = MSG_DONTWAIT | MSG_SOCALLBCK;
			error = soreceive(so, NULL, &uio, &m, NULL, &rcvflag);
			SOCKBUF_LOCK(&so->so_rcv);

			if (error == EWOULDBLOCK)
				break;
			
			/*
			 * If there was an error, wake up all pending
			 * requests.
			 */
			if (error || uio.uio_resid > 0) {
			wakeup_all:
				mtx_lock(&ct->ct_lock);
				if (!error) {
					/*
					 * We must have got EOF trying
					 * to read from the stream.
					 */
					error = ECONNRESET;
				}
				ct->ct_error.re_status = RPC_CANTRECV;
				ct->ct_error.re_errno = error;
				TAILQ_FOREACH(cr, &ct->ct_pending, cr_link) {
					cr->cr_error = error;
					wakeup(cr);
				}
				mtx_unlock(&ct->ct_lock);
				break;
			}
			m_copydata(m, 0, sizeof(uint32_t), (char *)&header);
			header = ntohl(header);
			ct->ct_record = NULL;
			ct->ct_record_resid = header & 0x7fffffff;
			ct->ct_record_eor = ((header & 0x80000000) != 0);
			m_freem(m);
		} else {
			/*
			 * Wait until the socket has the whole record
			 * buffered.
			 */
			do_read = FALSE;
			if (sbavail(&so->so_rcv) >= ct->ct_record_resid
			    || (so->so_rcv.sb_state & SBS_CANTRCVMORE)
			    || so->so_error)
				do_read = TRUE;

			if (!do_read)
				break;

			/*
			 * We have the record mark. Read as much as
			 * the socket has buffered up to the end of
			 * this record.
			 */
			SOCKBUF_UNLOCK(&so->so_rcv);
			uio.uio_resid = ct->ct_record_resid;
			m = NULL;
			rcvflag = MSG_DONTWAIT | MSG_SOCALLBCK;
			error = soreceive(so, NULL, &uio, &m, NULL, &rcvflag);
			SOCKBUF_LOCK(&so->so_rcv);

			if (error == EWOULDBLOCK)
				break;

			if (error || uio.uio_resid == ct->ct_record_resid)
				goto wakeup_all;

			/*
			 * If we have part of the record already,
			 * chain this bit onto the end.
			 */
			if (ct->ct_record)
				m_last(ct->ct_record)->m_next = m;
			else
				ct->ct_record = m;

			ct->ct_record_resid = uio.uio_resid;

			/*
			 * If we have the entire record, see if we can
			 * match it to a request.
			 */
			if (ct->ct_record_resid == 0
			    && ct->ct_record_eor) {
				/*
				 * The XID is in the first uint32_t of
				 * the reply and the message direction
				 * is the second one.
				 */
				if (ct->ct_record->m_len <
				    sizeof(xid_plus_direction) &&
				    m_length(ct->ct_record, NULL) <
				    sizeof(xid_plus_direction)) {
					m_freem(ct->ct_record);
					break;
				}
				m_copydata(ct->ct_record, 0,
				    sizeof(xid_plus_direction),
				    (char *)xid_plus_direction);
				xid_plus_direction[0] =
				    ntohl(xid_plus_direction[0]);
				xid_plus_direction[1] =
				    ntohl(xid_plus_direction[1]);
				/* Check message direction. */
				if (xid_plus_direction[1] == CALL) {
					/* This is a backchannel request. */
					mtx_lock(&ct->ct_lock);
					xprt = ct->ct_backchannelxprt;
					if (xprt == NULL) {
						mtx_unlock(&ct->ct_lock);
						/* Just throw it away. */
						m_freem(ct->ct_record);
						ct->ct_record = NULL;
					} else {
						cd = (struct cf_conn *)
						    xprt->xp_p1;
						m2 = cd->mreq;
						/*
						 * The requests are chained
						 * in the m_nextpkt list.
						 */
						while (m2 != NULL &&
						    m2->m_nextpkt != NULL)
							/* Find end of list. */
							m2 = m2->m_nextpkt;
						if (m2 != NULL)
							m2->m_nextpkt =
							    ct->ct_record;
						else
							cd->mreq =
							    ct->ct_record;
						ct->ct_record->m_nextpkt =
						    NULL;
						ct->ct_record = NULL;
						xprt_active(xprt);
						mtx_unlock(&ct->ct_lock);
					}
				} else {
					mtx_lock(&ct->ct_lock);
					foundreq = 0;
					TAILQ_FOREACH(cr, &ct->ct_pending,
					    cr_link) {
						if (cr->cr_xid ==
						    xid_plus_direction[0]) {
							/*
							 * This one
							 * matches. We leave
							 * the reply mbuf in
							 * cr->cr_mrep. Set
							 * the XID to zero so
							 * that we will ignore
							 * any duplicated
							 * replies.
							 */
							cr->cr_xid = 0;
							cr->cr_mrep =
							    ct->ct_record;
							cr->cr_error = 0;
							foundreq = 1;
							wakeup(cr);
							break;
						}
					}
					mtx_unlock(&ct->ct_lock);

					if (!foundreq)
						m_freem(ct->ct_record);
					ct->ct_record = NULL;
				}
			}
		}
	} while (m);
	ct->ct_upcallrefs--;
	if (ct->ct_upcallrefs < 0)
		panic("rpcvc upcall refcnt");
	if (ct->ct_upcallrefs == 0)
		wakeup(&ct->ct_upcallrefs);
	return (SU_OK);
}

/*
 * Wait for all upcalls in progress to complete.
 */
static void
clnt_vc_upcallsdone(struct ct_data *ct)
{

	SOCKBUF_LOCK_ASSERT(&ct->ct_socket->so_rcv);

	while (ct->ct_upcallrefs > 0)
		(void) msleep(&ct->ct_upcallrefs,
		    SOCKBUF_MTX(&ct->ct_socket->so_rcv), 0, "rpcvcup", 0);
}
