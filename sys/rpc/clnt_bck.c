/*	$NetBSD: clnt_vc.c,v 1.4 2000/07/14 08:40:42 fvdl Exp $	*/

/*-
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

/*
 * This code handles the special case of a NFSv4.n backchannel for
 * callback RPCs. It is similar to clnt_vc.c, but uses the TCP
 * connection provided by the client to the server.
 */

#include <sys/param.h>
#include <sys/systm.h>
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

static void clnt_bck_geterr(CLIENT *, struct rpc_err *);
static bool_t clnt_bck_freeres(CLIENT *, xdrproc_t, void *);
static void clnt_bck_abort(CLIENT *);
static bool_t clnt_bck_control(CLIENT *, u_int, void *);
static void clnt_bck_close(CLIENT *);
static void clnt_bck_destroy(CLIENT *);

static struct clnt_ops clnt_bck_ops = {
	.cl_abort =	clnt_bck_abort,
	.cl_geterr =	clnt_bck_geterr,
	.cl_freeres =	clnt_bck_freeres,
	.cl_close =	clnt_bck_close,
	.cl_destroy =	clnt_bck_destroy,
	.cl_control =	clnt_bck_control
};

/*
 * Create a client handle for a connection.
 * Default options are set, which the user can change using clnt_control()'s.
 * This code handles the special case of an NFSv4.1 session backchannel
 * call, which is sent on a TCP connection created against the server
 * by a client.
 */
void *
clnt_bck_create(
	struct socket *so,		/* Server transport socket. */
	const rpcprog_t prog,		/* program number */
	const rpcvers_t vers)		/* version number */
{
	CLIENT *cl;			/* client handle */
	struct ct_data *ct = NULL;	/* client handle */
	struct timeval now;
	struct rpc_msg call_msg;
	static uint32_t disrupt;
	XDR xdrs;

	if (disrupt == 0)
		disrupt = (uint32_t)(long)so;

	cl = (CLIENT *)mem_alloc(sizeof (*cl));
	ct = (struct ct_data *)mem_alloc(sizeof (*ct));

	mtx_init(&ct->ct_lock, "ct->ct_lock", NULL, MTX_DEF);
	ct->ct_threads = 0;
	ct->ct_closing = FALSE;
	ct->ct_closed = FALSE;
	ct->ct_upcallrefs = 0;
	ct->ct_closeit = FALSE;

	/*
	 * Set up private data struct
	 */
	ct->ct_wait.tv_sec = -1;
	ct->ct_wait.tv_usec = -1;

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
	if (!xdr_callhdr(&xdrs, &call_msg))
		goto err;
	ct->ct_mpos = XDR_GETPOS(&xdrs);
	XDR_DESTROY(&xdrs);
	ct->ct_waitchan = "rpcbck";
	ct->ct_waitflag = 0;
	cl->cl_refs = 1;
	cl->cl_ops = &clnt_bck_ops;
	cl->cl_private = ct;
	cl->cl_auth = authnone_create();
	TAILQ_INIT(&ct->ct_pending);
	return (cl);

err:
	mtx_destroy(&ct->ct_lock);
	mem_free(ct, sizeof (struct ct_data));
	mem_free(cl, sizeof (CLIENT));
	return (NULL);
}

enum clnt_stat
clnt_bck_call(
	CLIENT		*cl,		/* client handle */
	struct rpc_callextra *ext,	/* call metadata */
	rpcproc_t	proc,		/* procedure number */
	struct mbuf	*args,		/* pointer to args */
	struct mbuf	**resultsp,	/* pointer to results */
	struct timeval	utimeout,
	SVCXPRT		*xprt)
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
	int error;

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

	if (ct->ct_wait.tv_usec == -1)
		timeout = utimeout;	/* use supplied timeout */
	else
		timeout = ct->ct_wait;	/* use default timeout */

call_again:
	mtx_assert(&ct->ct_lock, MA_OWNED);

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

	if ((!XDR_PUTINT32(&xdrs, &proc)) ||
	    (!AUTH_MARSHALL(auth, xid, &xdrs,
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
	 * Check to see if the client end has already started to close down
	 * the connection. The svc code will have set ct_error.re_status
	 * to RPC_CANTRECV if this is the case.
	 * If the client starts to close down the connection after this
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
	sx_xlock(&xprt->xp_lock);
	error = sosend(xprt->xp_socket, NULL, NULL, mreq, NULL, 0, curthread);
if (error != 0) printf("sosend=%d\n", error);
	mreq = NULL;
	if (error == EMSGSIZE) {
printf("emsgsize\n");
		SOCKBUF_LOCK(&xprt->xp_socket->so_snd);
		sbwait(&xprt->xp_socket->so_snd);
		SOCKBUF_UNLOCK(&xprt->xp_socket->so_snd);
		sx_xunlock(&xprt->xp_lock);
		AUTH_VALIDATE(auth, xid, NULL, NULL);
		mtx_lock(&ct->ct_lock);
		TAILQ_REMOVE(&ct->ct_pending, cr, cr_link);
		goto call_again;
	}
	sx_xunlock(&xprt->xp_lock);

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
		 * We were woken up by the svc thread.  If the
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
			    &reply_msg.acpted_rply.ar_verf, &results)) {
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
			if (nrefreshes > 0 && AUTH_REFRESH(auth, &reply_msg)) {
				nrefreshes--;
				XDR_DESTROY(&xdrs);
				mtx_lock(&ct->ct_lock);
				goto call_again;
			}
			/* end of unsuccessful completion */
		/* end of valid reply message */
	} else
		errp->re_status = stat = RPC_CANTDECODERES;
	XDR_DESTROY(&xdrs);
	mtx_lock(&ct->ct_lock);
out:
	mtx_assert(&ct->ct_lock, MA_OWNED);

	KASSERT(stat != RPC_SUCCESS || *resultsp,
	    ("RPC_SUCCESS without reply"));

	if (mreq != NULL)
		m_freem(mreq);
	if (cr->cr_mrep != NULL)
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
clnt_bck_geterr(CLIENT *cl, struct rpc_err *errp)
{
	struct ct_data *ct = (struct ct_data *) cl->cl_private;

	*errp = ct->ct_error;
}

static bool_t
clnt_bck_freeres(CLIENT *cl, xdrproc_t xdr_res, void *res_ptr)
{
	XDR xdrs;
	bool_t dummy;

	xdrs.x_op = XDR_FREE;
	dummy = (*xdr_res)(&xdrs, res_ptr);

	return (dummy);
}

/*ARGSUSED*/
static void
clnt_bck_abort(CLIENT *cl)
{
}

static bool_t
clnt_bck_control(CLIENT *cl, u_int request, void *info)
{

	return (TRUE);
}

static void
clnt_bck_close(CLIENT *cl)
{
	struct ct_data *ct = (struct ct_data *) cl->cl_private;

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

	ct->ct_closing = FALSE;
	ct->ct_closed = TRUE;
	mtx_unlock(&ct->ct_lock);
	wakeup(ct);
}

static void
clnt_bck_destroy(CLIENT *cl)
{
	struct ct_data *ct = (struct ct_data *) cl->cl_private;

	clnt_bck_close(cl);

	mtx_destroy(&ct->ct_lock);
	mem_free(ct, sizeof(struct ct_data));
	if (cl->cl_netid && cl->cl_netid[0])
		mem_free(cl->cl_netid, strlen(cl->cl_netid) +1);
	if (cl->cl_tp && cl->cl_tp[0])
		mem_free(cl->cl_tp, strlen(cl->cl_tp) +1);
	mem_free(cl, sizeof(CLIENT));
}

/*
 * This call is done by the svc code when a backchannel RPC reply is
 * received.
 */
void
clnt_bck_svccall(void *arg, struct mbuf *mrep, uint32_t xid)
{
	struct ct_data *ct = (struct ct_data *)arg;
	struct ct_request *cr;
	int foundreq;

	mtx_lock(&ct->ct_lock);
	ct->ct_upcallrefs++;
	/*
	 * See if we can match this reply to a request.
	 */
	foundreq = 0;
	TAILQ_FOREACH(cr, &ct->ct_pending, cr_link) {
		if (cr->cr_xid == xid) {
			/*
			 * This one matches. We leave the reply mbuf list in
			 * cr->cr_mrep. Set the XID to zero so that we will
			 * ignore any duplicated replies.
			 */
			cr->cr_xid = 0;
			cr->cr_mrep = mrep;
			cr->cr_error = 0;
			foundreq = 1;
			wakeup(cr);
			break;
		}
	}

	ct->ct_upcallrefs--;
	if (ct->ct_upcallrefs < 0)
		panic("rpcvc svccall refcnt");
	if (ct->ct_upcallrefs == 0)
		wakeup(&ct->ct_upcallrefs);
	mtx_unlock(&ct->ct_lock);
	if (foundreq == 0)
		m_freem(mrep);
}

