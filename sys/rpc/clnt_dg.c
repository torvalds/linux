/*	$NetBSD: clnt_dg.c,v 1.4 2000/07/14 08:40:41 fvdl Exp $	*/

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
/*
 * Copyright (c) 1986-1991 by Sun Microsystems Inc. 
 */

#if defined(LIBC_SCCS) && !defined(lint)
#ident	"@(#)clnt_dg.c	1.23	94/04/22 SMI"
static char sccsid[] = "@(#)clnt_dg.c 1.19 89/03/16 Copyr 1988 Sun Micro";
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Implements a connectionless client side RPC.
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
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/time.h>
#include <sys/uio.h>

#include <net/vnet.h>

#include <rpc/rpc.h>
#include <rpc/rpc_com.h>


#ifdef _FREEFALL_CONFIG
/*
 * Disable RPC exponential back-off for FreeBSD.org systems.
 */
#define	RPC_MAX_BACKOFF		1 /* second */
#else
#define	RPC_MAX_BACKOFF		30 /* seconds */
#endif

static bool_t time_not_ok(struct timeval *);
static enum clnt_stat clnt_dg_call(CLIENT *, struct rpc_callextra *,
    rpcproc_t, struct mbuf *, struct mbuf **, struct timeval);
static void clnt_dg_geterr(CLIENT *, struct rpc_err *);
static bool_t clnt_dg_freeres(CLIENT *, xdrproc_t, void *);
static void clnt_dg_abort(CLIENT *);
static bool_t clnt_dg_control(CLIENT *, u_int, void *);
static void clnt_dg_close(CLIENT *);
static void clnt_dg_destroy(CLIENT *);
static int clnt_dg_soupcall(struct socket *so, void *arg, int waitflag);

static struct clnt_ops clnt_dg_ops = {
	.cl_call =	clnt_dg_call,
	.cl_abort =	clnt_dg_abort,
	.cl_geterr =	clnt_dg_geterr,
	.cl_freeres =	clnt_dg_freeres,
	.cl_close =	clnt_dg_close,
	.cl_destroy =	clnt_dg_destroy,
	.cl_control =	clnt_dg_control
};

/*
 * A pending RPC request which awaits a reply. Requests which have
 * received their reply will have cr_xid set to zero and cr_mrep to
 * the mbuf chain of the reply.
 */
struct cu_request {
	TAILQ_ENTRY(cu_request) cr_link;
	CLIENT			*cr_client;	/* owner */
	uint32_t		cr_xid;		/* XID of request */
	struct mbuf		*cr_mrep;	/* reply received by upcall */
	int			cr_error;	/* any error from upcall */
	char			cr_verf[MAX_AUTH_BYTES]; /* reply verf */
};

TAILQ_HEAD(cu_request_list, cu_request);

#define MCALL_MSG_SIZE 24

/*
 * This structure is pointed to by the socket buffer's sb_upcallarg
 * member. It is separate from the client private data to facilitate
 * multiple clients sharing the same socket. The cs_lock mutex is used
 * to protect all fields of this structure, the socket's receive
 * buffer SOCKBUF_LOCK is used to ensure that exactly one of these
 * structures is installed on the socket.
 */
struct cu_socket {
	struct mtx		cs_lock;
	int			cs_refs;	/* Count of clients */
	struct cu_request_list	cs_pending;	/* Requests awaiting replies */
	int			cs_upcallrefs;	/* Refcnt of upcalls in prog.*/
};

static void clnt_dg_upcallsdone(struct socket *, struct cu_socket *);

/*
 * Private data kept per client handle
 */
struct cu_data {
	int			cu_threads;	/* # threads in clnt_vc_call */
	bool_t			cu_closing;	/* TRUE if we are closing */
	bool_t			cu_closed;	/* TRUE if we are closed */
	struct socket		*cu_socket;	/* connection socket */
	bool_t			cu_closeit;	/* opened by library */
	struct sockaddr_storage	cu_raddr;	/* remote address */
	int			cu_rlen;
	struct timeval		cu_wait;	/* retransmit interval */
	struct timeval		cu_total;	/* total time for the call */
	struct rpc_err		cu_error;
	uint32_t		cu_xid;
	char			cu_mcallc[MCALL_MSG_SIZE]; /* marshalled callmsg */
	size_t			cu_mcalllen;
	size_t			cu_sendsz;	/* send size */
	size_t			cu_recvsz;	/* recv size */
	int			cu_async;
	int			cu_connect;	/* Use connect(). */
	int			cu_connected;	/* Have done connect(). */
	const char		*cu_waitchan;
	int			cu_waitflag;
	int			cu_cwnd;	/* congestion window */
	int			cu_sent;	/* number of in-flight RPCs */
	bool_t			cu_cwnd_wait;
};

#define CWNDSCALE	256
#define MAXCWND		(32 * CWNDSCALE)

/*
 * Connection less client creation returns with client handle parameters.
 * Default options are set, which the user can change using clnt_control().
 * fd should be open and bound.
 * NB: The rpch->cl_auth is initialized to null authentication.
 * 	Caller may wish to set this something more useful.
 *
 * sendsz and recvsz are the maximum allowable packet sizes that can be
 * sent and received. Normally they are the same, but they can be
 * changed to improve the program efficiency and buffer allocation.
 * If they are 0, use the transport default.
 *
 * If svcaddr is NULL, returns NULL.
 */
CLIENT *
clnt_dg_create(
	struct socket *so,
	struct sockaddr *svcaddr,	/* servers address */
	rpcprog_t program,		/* program number */
	rpcvers_t version,		/* version number */
	size_t sendsz,			/* buffer recv size */
	size_t recvsz)			/* buffer send size */
{
	CLIENT *cl = NULL;		/* client handle */
	struct cu_data *cu = NULL;	/* private data */
	struct cu_socket *cs = NULL;
	struct sockbuf *sb;
	struct timeval now;
	struct rpc_msg call_msg;
	struct __rpc_sockinfo si;
	XDR xdrs;
	int error;

	if (svcaddr == NULL) {
		rpc_createerr.cf_stat = RPC_UNKNOWNADDR;
		return (NULL);
	}

	if (!__rpc_socket2sockinfo(so, &si)) {
		rpc_createerr.cf_stat = RPC_TLIERROR;
		rpc_createerr.cf_error.re_errno = 0;
		return (NULL);
	}

	/*
	 * Find the receive and the send size
	 */
	sendsz = __rpc_get_t_size(si.si_af, si.si_proto, (int)sendsz);
	recvsz = __rpc_get_t_size(si.si_af, si.si_proto, (int)recvsz);
	if ((sendsz == 0) || (recvsz == 0)) {
		rpc_createerr.cf_stat = RPC_TLIERROR; /* XXX */
		rpc_createerr.cf_error.re_errno = 0;
		return (NULL);
	}

	cl = mem_alloc(sizeof (CLIENT));

	/*
	 * Should be multiple of 4 for XDR.
	 */
	sendsz = rounddown(sendsz + 3, 4);
	recvsz = rounddown(recvsz + 3, 4);
	cu = mem_alloc(sizeof (*cu));
	cu->cu_threads = 0;
	cu->cu_closing = FALSE;
	cu->cu_closed = FALSE;
	(void) memcpy(&cu->cu_raddr, svcaddr, (size_t)svcaddr->sa_len);
	cu->cu_rlen = svcaddr->sa_len;
	/* Other values can also be set through clnt_control() */
	cu->cu_wait.tv_sec = 3;	/* heuristically chosen */
	cu->cu_wait.tv_usec = 0;
	cu->cu_total.tv_sec = -1;
	cu->cu_total.tv_usec = -1;
	cu->cu_sendsz = sendsz;
	cu->cu_recvsz = recvsz;
	cu->cu_async = FALSE;
	cu->cu_connect = FALSE;
	cu->cu_connected = FALSE;
	cu->cu_waitchan = "rpcrecv";
	cu->cu_waitflag = 0;
	cu->cu_cwnd = MAXCWND / 2;
	cu->cu_sent = 0;
	cu->cu_cwnd_wait = FALSE;
	(void) getmicrotime(&now);
	cu->cu_xid = __RPC_GETXID(&now);
	call_msg.rm_xid = cu->cu_xid;
	call_msg.rm_call.cb_prog = program;
	call_msg.rm_call.cb_vers = version;
	xdrmem_create(&xdrs, cu->cu_mcallc, MCALL_MSG_SIZE, XDR_ENCODE);
	if (! xdr_callhdr(&xdrs, &call_msg)) {
		rpc_createerr.cf_stat = RPC_CANTENCODEARGS;  /* XXX */
		rpc_createerr.cf_error.re_errno = 0;
		goto err2;
	}
	cu->cu_mcalllen = XDR_GETPOS(&xdrs);

	/*
	 * By default, closeit is always FALSE. It is users responsibility
	 * to do a close on it, else the user may use clnt_control
	 * to let clnt_destroy do it for him/her.
	 */
	cu->cu_closeit = FALSE;
	cu->cu_socket = so;
	error = soreserve(so, (u_long)sendsz, (u_long)recvsz);
	if (error != 0) {
		rpc_createerr.cf_stat = RPC_FAILED;
		rpc_createerr.cf_error.re_errno = error;
		goto err2;
	}

	sb = &so->so_rcv;
	SOCKBUF_LOCK(&so->so_rcv);
recheck_socket:
	if (sb->sb_upcall) {
		if (sb->sb_upcall != clnt_dg_soupcall) {
			SOCKBUF_UNLOCK(&so->so_rcv);
			printf("clnt_dg_create(): socket already has an incompatible upcall\n");
			goto err2;
		}
		cs = (struct cu_socket *) sb->sb_upcallarg;
		mtx_lock(&cs->cs_lock);
		cs->cs_refs++;
		mtx_unlock(&cs->cs_lock);
	} else {
		/*
		 * We are the first on this socket - allocate the
		 * structure and install it in the socket.
		 */
		SOCKBUF_UNLOCK(&so->so_rcv);
		cs = mem_alloc(sizeof(*cs));
		SOCKBUF_LOCK(&so->so_rcv);
		if (sb->sb_upcall) {
			/*
			 * We have lost a race with some other client.
			 */
			mem_free(cs, sizeof(*cs));
			goto recheck_socket;
		}
		mtx_init(&cs->cs_lock, "cs->cs_lock", NULL, MTX_DEF);
		cs->cs_refs = 1;
		cs->cs_upcallrefs = 0;
		TAILQ_INIT(&cs->cs_pending);
		soupcall_set(so, SO_RCV, clnt_dg_soupcall, cs);
	}
	SOCKBUF_UNLOCK(&so->so_rcv);

	cl->cl_refs = 1;
	cl->cl_ops = &clnt_dg_ops;
	cl->cl_private = (caddr_t)(void *)cu;
	cl->cl_auth = authnone_create();
	cl->cl_tp = NULL;
	cl->cl_netid = NULL;
	return (cl);
err2:
	mem_free(cl, sizeof (CLIENT));
	mem_free(cu, sizeof (*cu));

	return (NULL);
}

static enum clnt_stat
clnt_dg_call(
	CLIENT		*cl,		/* client handle */
	struct rpc_callextra *ext,	/* call metadata */
	rpcproc_t	proc,		/* procedure number */
	struct mbuf	*args,		/* pointer to args */
	struct mbuf	**resultsp,	/* pointer to results */
	struct timeval	utimeout)	/* seconds to wait before giving up */
{
	struct cu_data *cu = (struct cu_data *)cl->cl_private;
	struct cu_socket *cs;
	struct rpc_timers *rt;
	AUTH *auth;
	struct rpc_err *errp;
	enum clnt_stat stat;
	XDR xdrs;
	struct rpc_msg reply_msg;
	bool_t ok;
	int retrans;			/* number of re-transmits so far */
	int nrefreshes = 2;		/* number of times to refresh cred */
	struct timeval *tvp;
	int timeout;
	int retransmit_time;
	int next_sendtime, starttime, rtt, time_waited, tv = 0;
	struct sockaddr *sa;
	uint32_t xid = 0;
	struct mbuf *mreq = NULL, *results;
	struct cu_request *cr;
	int error;

	cs = cu->cu_socket->so_rcv.sb_upcallarg;
	cr = malloc(sizeof(struct cu_request), M_RPC, M_WAITOK);

	mtx_lock(&cs->cs_lock);

	if (cu->cu_closing || cu->cu_closed) {
		mtx_unlock(&cs->cs_lock);
		free(cr, M_RPC);
		return (RPC_CANTSEND);
	}
	cu->cu_threads++;

	if (ext) {
		auth = ext->rc_auth;
		errp = &ext->rc_err;
	} else {
		auth = cl->cl_auth;
		errp = &cu->cu_error;
	}

	cr->cr_client = cl;
	cr->cr_mrep = NULL;
	cr->cr_error = 0;

	if (cu->cu_total.tv_usec == -1) {
		tvp = &utimeout; /* use supplied timeout */
	} else {
		tvp = &cu->cu_total; /* use default timeout */
	}
	if (tvp->tv_sec || tvp->tv_usec)
		timeout = tvtohz(tvp);
	else
		timeout = 0;

	if (cu->cu_connect && !cu->cu_connected) {
		mtx_unlock(&cs->cs_lock);
		error = soconnect(cu->cu_socket,
		    (struct sockaddr *)&cu->cu_raddr, curthread);
		mtx_lock(&cs->cs_lock);
		if (error) {
			errp->re_errno = error;
			errp->re_status = stat = RPC_CANTSEND;
			goto out;
		}
		cu->cu_connected = 1;
	}
	if (cu->cu_connected)
		sa = NULL;
	else
		sa = (struct sockaddr *)&cu->cu_raddr;
	time_waited = 0;
	retrans = 0;
	if (ext && ext->rc_timers) {
		rt = ext->rc_timers;
		if (!rt->rt_rtxcur)
			rt->rt_rtxcur = tvtohz(&cu->cu_wait);
		retransmit_time = next_sendtime = rt->rt_rtxcur;
	} else {
		rt = NULL;
		retransmit_time = next_sendtime = tvtohz(&cu->cu_wait);
	}

	starttime = ticks;

call_again:
	mtx_assert(&cs->cs_lock, MA_OWNED);

	cu->cu_xid++;
	xid = cu->cu_xid;

send_again:
	mtx_unlock(&cs->cs_lock);

	mreq = m_gethdr(M_WAITOK, MT_DATA);
	KASSERT(cu->cu_mcalllen <= MHLEN, ("RPC header too big"));
	bcopy(cu->cu_mcallc, mreq->m_data, cu->cu_mcalllen);
	mreq->m_len = cu->cu_mcalllen;

	/*
	 * The XID is the first thing in the request.
	 */
	*mtod(mreq, uint32_t *) = htonl(xid);

	xdrmbuf_create(&xdrs, mreq, XDR_ENCODE);

	if (cu->cu_async == TRUE && args == NULL)
		goto get_reply;

	if ((! XDR_PUTINT32(&xdrs, &proc)) ||
	    (! AUTH_MARSHALL(auth, xid, &xdrs,
		m_copym(args, 0, M_COPYALL, M_WAITOK)))) {
		errp->re_status = stat = RPC_CANTENCODEARGS;
		mtx_lock(&cs->cs_lock);
		goto out;
	}
	mreq->m_pkthdr.len = m_length(mreq, NULL);

	cr->cr_xid = xid;
	mtx_lock(&cs->cs_lock);

	/*
	 * Try to get a place in the congestion window.
	 */
	while (cu->cu_sent >= cu->cu_cwnd) {
		cu->cu_cwnd_wait = TRUE;
		error = msleep(&cu->cu_cwnd_wait, &cs->cs_lock,
		    cu->cu_waitflag, "rpccwnd", 0);
		if (error) {
			errp->re_errno = error;
			if (error == EINTR || error == ERESTART)
				errp->re_status = stat = RPC_INTR;
			else
				errp->re_status = stat = RPC_CANTSEND;
			goto out;
		}
	}
	cu->cu_sent += CWNDSCALE;

	TAILQ_INSERT_TAIL(&cs->cs_pending, cr, cr_link);
	mtx_unlock(&cs->cs_lock);

	/*
	 * sosend consumes mreq.
	 */
	error = sosend(cu->cu_socket, sa, NULL, mreq, NULL, 0, curthread);
	mreq = NULL;

	/*
	 * sub-optimal code appears here because we have
	 * some clock time to spare while the packets are in flight.
	 * (We assume that this is actually only executed once.)
	 */
	reply_msg.acpted_rply.ar_verf.oa_flavor = AUTH_NULL;
	reply_msg.acpted_rply.ar_verf.oa_base = cr->cr_verf;
	reply_msg.acpted_rply.ar_verf.oa_length = 0;
	reply_msg.acpted_rply.ar_results.where = NULL;
	reply_msg.acpted_rply.ar_results.proc = (xdrproc_t)xdr_void;

	mtx_lock(&cs->cs_lock);
	if (error) {
		TAILQ_REMOVE(&cs->cs_pending, cr, cr_link);
		errp->re_errno = error;
		errp->re_status = stat = RPC_CANTSEND;
		cu->cu_sent -= CWNDSCALE;
		if (cu->cu_cwnd_wait) {
			cu->cu_cwnd_wait = FALSE;
			wakeup(&cu->cu_cwnd_wait);
		}
		goto out;
	}

	/*
	 * Check to see if we got an upcall while waiting for the
	 * lock.
	 */
	if (cr->cr_error) {
		TAILQ_REMOVE(&cs->cs_pending, cr, cr_link);
		errp->re_errno = cr->cr_error;
		errp->re_status = stat = RPC_CANTRECV;
		cu->cu_sent -= CWNDSCALE;
		if (cu->cu_cwnd_wait) {
			cu->cu_cwnd_wait = FALSE;
			wakeup(&cu->cu_cwnd_wait);
		}
		goto out;
	}
	if (cr->cr_mrep) {
		TAILQ_REMOVE(&cs->cs_pending, cr, cr_link);
		cu->cu_sent -= CWNDSCALE;
		if (cu->cu_cwnd_wait) {
			cu->cu_cwnd_wait = FALSE;
			wakeup(&cu->cu_cwnd_wait);
		}
		goto got_reply;
	}

	/*
	 * Hack to provide rpc-based message passing
	 */
	if (timeout == 0) {
		TAILQ_REMOVE(&cs->cs_pending, cr, cr_link);
		errp->re_status = stat = RPC_TIMEDOUT;
		cu->cu_sent -= CWNDSCALE;
		if (cu->cu_cwnd_wait) {
			cu->cu_cwnd_wait = FALSE;
			wakeup(&cu->cu_cwnd_wait);
		}
		goto out;
	}

get_reply:
	for (;;) {
		/* Decide how long to wait. */
		if (next_sendtime < timeout)
			tv = next_sendtime;
		else
			tv = timeout;
		tv -= time_waited;

		if (tv > 0) {
			if (cu->cu_closing || cu->cu_closed) {
				error = 0;
				cr->cr_error = ESHUTDOWN;
			} else {
				error = msleep(cr, &cs->cs_lock,
				    cu->cu_waitflag, cu->cu_waitchan, tv);
			}
		} else {
			error = EWOULDBLOCK;
		}

		TAILQ_REMOVE(&cs->cs_pending, cr, cr_link);
		cu->cu_sent -= CWNDSCALE;
		if (cu->cu_cwnd_wait) {
			cu->cu_cwnd_wait = FALSE;
			wakeup(&cu->cu_cwnd_wait);
		}

		if (!error) {
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

			cu->cu_cwnd += (CWNDSCALE * CWNDSCALE
			    + cu->cu_cwnd / 2) / cu->cu_cwnd;
			if (cu->cu_cwnd > MAXCWND)
				cu->cu_cwnd = MAXCWND;

			if (rt) {
				/*
				 * Add one to the time since a tick
				 * count of N means that the actual
				 * time taken was somewhere between N
				 * and N+1.
				 */
				rtt = ticks - starttime + 1;

				/*
				 * Update our estimate of the round
				 * trip time using roughly the
				 * algorithm described in RFC
				 * 2988. Given an RTT sample R:
				 *
				 * RTTVAR = (1-beta) * RTTVAR + beta * |SRTT-R|
				 * SRTT = (1-alpha) * SRTT + alpha * R
				 *
				 * where alpha = 0.125 and beta = 0.25.
				 *
				 * The initial retransmit timeout is
				 * SRTT + 4*RTTVAR and doubles on each
				 * retransmision.
				 */
				if (rt->rt_srtt == 0) {
					rt->rt_srtt = rtt;
					rt->rt_deviate = rtt / 2;
				} else {
					int32_t error = rtt - rt->rt_srtt;
					rt->rt_srtt += error / 8;
					error = abs(error) - rt->rt_deviate;
					rt->rt_deviate += error / 4;
				}
				rt->rt_rtxcur = rt->rt_srtt + 4*rt->rt_deviate;
			}

			break;
		}

		/*
		 * The sleep returned an error so our request is still
		 * on the list. If we got EWOULDBLOCK, we may want to
		 * re-send the request.
		 */
		if (error != EWOULDBLOCK) {
			errp->re_errno = error;
			if (error == EINTR || error == ERESTART)
				errp->re_status = stat = RPC_INTR;
			else
				errp->re_status = stat = RPC_CANTRECV;
			goto out;
		}

		time_waited = ticks - starttime;

		/* Check for timeout. */
		if (time_waited > timeout) {
			errp->re_errno = EWOULDBLOCK;
			errp->re_status = stat = RPC_TIMEDOUT;
			goto out;
		}

		/* Retransmit if necessary. */		
		if (time_waited >= next_sendtime) {
			cu->cu_cwnd /= 2;
			if (cu->cu_cwnd < CWNDSCALE)
				cu->cu_cwnd = CWNDSCALE;
			if (ext && ext->rc_feedback) {
				mtx_unlock(&cs->cs_lock);
				if (retrans == 0)
					ext->rc_feedback(FEEDBACK_REXMIT1,
					    proc, ext->rc_feedback_arg);
				else
					ext->rc_feedback(FEEDBACK_REXMIT2,
					    proc, ext->rc_feedback_arg);
				mtx_lock(&cs->cs_lock);
			}
			if (cu->cu_closing || cu->cu_closed) {
				errp->re_errno = ESHUTDOWN;
				errp->re_status = stat = RPC_CANTRECV;
				goto out;
			}
			retrans++;
			/* update retransmit_time */
			if (retransmit_time < RPC_MAX_BACKOFF * hz)
				retransmit_time = 2 * retransmit_time;
			next_sendtime += retransmit_time;
			goto send_again;
		}
		cu->cu_sent += CWNDSCALE;
		TAILQ_INSERT_TAIL(&cs->cs_pending, cr, cr_link);
	}

got_reply:
	/*
	 * Now decode and validate the response. We need to drop the
	 * lock since xdr_replymsg may end up sleeping in malloc.
	 */
	mtx_unlock(&cs->cs_lock);

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
			stat = _seterr_reply(&reply_msg, &(cu->cu_error));

		if (errp->re_status == RPC_SUCCESS) {
			results = xdrmbuf_getall(&xdrs);
			if (! AUTH_VALIDATE(auth, xid,
				&reply_msg.acpted_rply.ar_verf,
				&results)) {
				errp->re_status = stat = RPC_AUTHERROR;
				errp->re_why = AUTH_INVALIDRESP;
				if (retrans &&
				    auth->ah_cred.oa_flavor == RPCSEC_GSS) {
					/*
					 * If we retransmitted, its
					 * possible that we will
					 * receive a reply for one of
					 * the earlier transmissions
					 * (which will use an older
					 * RPCSEC_GSS sequence
					 * number). In this case, just
					 * go back and listen for a
					 * new reply. We could keep a
					 * record of all the seq
					 * numbers we have transmitted
					 * so far so that we could
					 * accept a reply for any of
					 * them here.
					 */
					XDR_DESTROY(&xdrs);
					mtx_lock(&cs->cs_lock);
					cu->cu_sent += CWNDSCALE;
					TAILQ_INSERT_TAIL(&cs->cs_pending,
					    cr, cr_link);
					cr->cr_mrep = NULL;
					goto get_reply;
				}
			} else {
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
				mtx_lock(&cs->cs_lock);
				goto call_again;
			}
		/* end of unsuccessful completion */
	}	/* end of valid reply message */
	else {
		errp->re_status = stat = RPC_CANTDECODERES;

	}
	XDR_DESTROY(&xdrs);
	mtx_lock(&cs->cs_lock);
out:
	mtx_assert(&cs->cs_lock, MA_OWNED);

	if (mreq)
		m_freem(mreq);
	if (cr->cr_mrep)
		m_freem(cr->cr_mrep);

	cu->cu_threads--;
	if (cu->cu_closing)
		wakeup(cu);
		
	mtx_unlock(&cs->cs_lock);

	if (auth && stat != RPC_SUCCESS)
		AUTH_VALIDATE(auth, xid, NULL, NULL);

	free(cr, M_RPC);

	return (stat);
}

static void
clnt_dg_geterr(CLIENT *cl, struct rpc_err *errp)
{
	struct cu_data *cu = (struct cu_data *)cl->cl_private;

	*errp = cu->cu_error;
}

static bool_t
clnt_dg_freeres(CLIENT *cl, xdrproc_t xdr_res, void *res_ptr)
{
	XDR xdrs;
	bool_t dummy;

	xdrs.x_op = XDR_FREE;
	dummy = (*xdr_res)(&xdrs, res_ptr);

	return (dummy);
}

/*ARGSUSED*/
static void
clnt_dg_abort(CLIENT *h)
{
}

static bool_t
clnt_dg_control(CLIENT *cl, u_int request, void *info)
{
	struct cu_data *cu = (struct cu_data *)cl->cl_private;
	struct cu_socket *cs;
	struct sockaddr *addr;

	cs = cu->cu_socket->so_rcv.sb_upcallarg;
	mtx_lock(&cs->cs_lock);

	switch (request) {
	case CLSET_FD_CLOSE:
		cu->cu_closeit = TRUE;
		mtx_unlock(&cs->cs_lock);
		return (TRUE);
	case CLSET_FD_NCLOSE:
		cu->cu_closeit = FALSE;
		mtx_unlock(&cs->cs_lock);
		return (TRUE);
	}

	/* for other requests which use info */
	if (info == NULL) {
		mtx_unlock(&cs->cs_lock);
		return (FALSE);
	}
	switch (request) {
	case CLSET_TIMEOUT:
		if (time_not_ok((struct timeval *)info)) {
			mtx_unlock(&cs->cs_lock);
			return (FALSE);
		}
		cu->cu_total = *(struct timeval *)info;
		break;
	case CLGET_TIMEOUT:
		*(struct timeval *)info = cu->cu_total;
		break;
	case CLSET_RETRY_TIMEOUT:
		if (time_not_ok((struct timeval *)info)) {
			mtx_unlock(&cs->cs_lock);
			return (FALSE);
		}
		cu->cu_wait = *(struct timeval *)info;
		break;
	case CLGET_RETRY_TIMEOUT:
		*(struct timeval *)info = cu->cu_wait;
		break;
	case CLGET_SVC_ADDR:
		/*
		 * Slightly different semantics to userland - we use
		 * sockaddr instead of netbuf.
		 */
		memcpy(info, &cu->cu_raddr, cu->cu_raddr.ss_len);
		break;
	case CLSET_SVC_ADDR:		/* set to new address */
		addr = (struct sockaddr *)info;
		(void) memcpy(&cu->cu_raddr, addr, addr->sa_len);
		break;
	case CLGET_XID:
		*(uint32_t *)info = cu->cu_xid;
		break;

	case CLSET_XID:
		/* This will set the xid of the NEXT call */
		/* decrement by 1 as clnt_dg_call() increments once */
		cu->cu_xid = *(uint32_t *)info - 1;
		break;

	case CLGET_VERS:
		/*
		 * This RELIES on the information that, in the call body,
		 * the version number field is the fifth field from the
		 * beginning of the RPC header. MUST be changed if the
		 * call_struct is changed
		 */
		*(uint32_t *)info =
		    ntohl(*(uint32_t *)(void *)(cu->cu_mcallc +
		    4 * BYTES_PER_XDR_UNIT));
		break;

	case CLSET_VERS:
		*(uint32_t *)(void *)(cu->cu_mcallc + 4 * BYTES_PER_XDR_UNIT)
			= htonl(*(uint32_t *)info);
		break;

	case CLGET_PROG:
		/*
		 * This RELIES on the information that, in the call body,
		 * the program number field is the fourth field from the
		 * beginning of the RPC header. MUST be changed if the
		 * call_struct is changed
		 */
		*(uint32_t *)info =
		    ntohl(*(uint32_t *)(void *)(cu->cu_mcallc +
		    3 * BYTES_PER_XDR_UNIT));
		break;

	case CLSET_PROG:
		*(uint32_t *)(void *)(cu->cu_mcallc + 3 * BYTES_PER_XDR_UNIT)
			= htonl(*(uint32_t *)info);
		break;
	case CLSET_ASYNC:
		cu->cu_async = *(int *)info;
		break;
	case CLSET_CONNECT:
		cu->cu_connect = *(int *)info;
		break;
	case CLSET_WAITCHAN:
		cu->cu_waitchan = (const char *)info;
		break;
	case CLGET_WAITCHAN:
		*(const char **) info = cu->cu_waitchan;
		break;
	case CLSET_INTERRUPTIBLE:
		if (*(int *) info)
			cu->cu_waitflag = PCATCH;
		else
			cu->cu_waitflag = 0;
		break;
	case CLGET_INTERRUPTIBLE:
		if (cu->cu_waitflag)
			*(int *) info = TRUE;
		else
			*(int *) info = FALSE;
		break;
	default:
		mtx_unlock(&cs->cs_lock);
		return (FALSE);
	}
	mtx_unlock(&cs->cs_lock);
	return (TRUE);
}

static void
clnt_dg_close(CLIENT *cl)
{
	struct cu_data *cu = (struct cu_data *)cl->cl_private;
	struct cu_socket *cs;
	struct cu_request *cr;

	cs = cu->cu_socket->so_rcv.sb_upcallarg;
	mtx_lock(&cs->cs_lock);

	if (cu->cu_closed) {
		mtx_unlock(&cs->cs_lock);
		return;
	}

	if (cu->cu_closing) {
		while (cu->cu_closing)
			msleep(cu, &cs->cs_lock, 0, "rpcclose", 0);
		KASSERT(cu->cu_closed, ("client should be closed"));
		mtx_unlock(&cs->cs_lock);
		return;
	}

	/*
	 * Abort any pending requests and wait until everyone
	 * has finished with clnt_vc_call.
	 */
	cu->cu_closing = TRUE;
	TAILQ_FOREACH(cr, &cs->cs_pending, cr_link) {
		if (cr->cr_client == cl) {
			cr->cr_xid = 0;
			cr->cr_error = ESHUTDOWN;
			wakeup(cr);
		}
	}

	while (cu->cu_threads)
		msleep(cu, &cs->cs_lock, 0, "rpcclose", 0);

	cu->cu_closing = FALSE;
	cu->cu_closed = TRUE;

	mtx_unlock(&cs->cs_lock);
	wakeup(cu);
}

static void
clnt_dg_destroy(CLIENT *cl)
{
	struct cu_data *cu = (struct cu_data *)cl->cl_private;
	struct cu_socket *cs;
	struct socket *so = NULL;
	bool_t lastsocketref;

	cs = cu->cu_socket->so_rcv.sb_upcallarg;
	clnt_dg_close(cl);

	SOCKBUF_LOCK(&cu->cu_socket->so_rcv);
	mtx_lock(&cs->cs_lock);

	cs->cs_refs--;
	if (cs->cs_refs == 0) {
		mtx_unlock(&cs->cs_lock);
		soupcall_clear(cu->cu_socket, SO_RCV);
		clnt_dg_upcallsdone(cu->cu_socket, cs);
		SOCKBUF_UNLOCK(&cu->cu_socket->so_rcv);
		mtx_destroy(&cs->cs_lock);
		mem_free(cs, sizeof(*cs));
		lastsocketref = TRUE;
	} else {
		mtx_unlock(&cs->cs_lock);
		SOCKBUF_UNLOCK(&cu->cu_socket->so_rcv);
		lastsocketref = FALSE;
	}

	if (cu->cu_closeit && lastsocketref) {
		so = cu->cu_socket;
		cu->cu_socket = NULL;
	}

	if (so)
		soclose(so);

	if (cl->cl_netid && cl->cl_netid[0])
		mem_free(cl->cl_netid, strlen(cl->cl_netid) +1);
	if (cl->cl_tp && cl->cl_tp[0])
		mem_free(cl->cl_tp, strlen(cl->cl_tp) +1);
	mem_free(cu, sizeof (*cu));
	mem_free(cl, sizeof (CLIENT));
}

/*
 * Make sure that the time is not garbage.  -1 value is allowed.
 */
static bool_t
time_not_ok(struct timeval *t)
{
	return (t->tv_sec < -1 || t->tv_sec > 100000000 ||
		t->tv_usec < -1 || t->tv_usec > 1000000);
}

int
clnt_dg_soupcall(struct socket *so, void *arg, int waitflag)
{
	struct cu_socket *cs = (struct cu_socket *) arg;
	struct uio uio;
	struct mbuf *m;
	struct mbuf *control;
	struct cu_request *cr;
	int error, rcvflag, foundreq;
	uint32_t xid;

	cs->cs_upcallrefs++;
	uio.uio_resid = 1000000000;
	uio.uio_td = curthread;
	do {
		SOCKBUF_UNLOCK(&so->so_rcv);
		m = NULL;
		control = NULL;
		rcvflag = MSG_DONTWAIT;
		error = soreceive(so, NULL, &uio, &m, &control, &rcvflag);
		if (control)
			m_freem(control);
		SOCKBUF_LOCK(&so->so_rcv);

		if (error == EWOULDBLOCK)
			break;

		/*
		 * If there was an error, wake up all pending
		 * requests.
		 */
		if (error) {
			mtx_lock(&cs->cs_lock);
			TAILQ_FOREACH(cr, &cs->cs_pending, cr_link) {
				cr->cr_xid = 0;
				cr->cr_error = error;
				wakeup(cr);
			}
			mtx_unlock(&cs->cs_lock);
			break;
		}

		/*
		 * The XID is in the first uint32_t of the reply.
		 */
		if (m->m_len < sizeof(xid) && m_length(m, NULL) < sizeof(xid)) {
			/*
			 * Should never happen.
			 */
			m_freem(m);
			continue;
		}

		m_copydata(m, 0, sizeof(xid), (char *)&xid);
		xid = ntohl(xid);

		/*
		 * Attempt to match this reply with a pending request.
		 */
		mtx_lock(&cs->cs_lock);
		foundreq = 0;
		TAILQ_FOREACH(cr, &cs->cs_pending, cr_link) {
			if (cr->cr_xid == xid) {
				/*
				 * This one matches. We leave the
				 * reply mbuf in cr->cr_mrep. Set the
				 * XID to zero so that we will ignore
				 * any duplicated replies that arrive
				 * before clnt_dg_call removes it from
				 * the queue.
				 */
				cr->cr_xid = 0;
				cr->cr_mrep = m;
				cr->cr_error = 0;
				foundreq = 1;
				wakeup(cr);
				break;
			}
		}
		mtx_unlock(&cs->cs_lock);

		/*
		 * If we didn't find the matching request, just drop
		 * it - its probably a repeated reply.
		 */
		if (!foundreq)
			m_freem(m);
	} while (m);
	cs->cs_upcallrefs--;
	if (cs->cs_upcallrefs < 0)
		panic("rpcdg upcall refcnt");
	if (cs->cs_upcallrefs == 0)
		wakeup(&cs->cs_upcallrefs);
	return (SU_OK);
}

/*
 * Wait for all upcalls in progress to complete.
 */
static void
clnt_dg_upcallsdone(struct socket *so, struct cu_socket *cs)
{

	SOCKBUF_LOCK_ASSERT(&so->so_rcv);

	while (cs->cs_upcallrefs > 0)
		(void) msleep(&cs->cs_upcallrefs, SOCKBUF_MTX(&so->so_rcv), 0,
		    "rpcdgup", 0);
}
