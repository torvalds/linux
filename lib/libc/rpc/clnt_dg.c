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

#include "namespace.h"
#include "reentrant.h"
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <rpc/rpc.h>
#include <rpc/rpcsec_gss.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <err.h>
#include "un-namespace.h"
#include "rpc_com.h"
#include "mt_misc.h"


#ifdef _FREEFALL_CONFIG
/*
 * Disable RPC exponential back-off for FreeBSD.org systems.
 */
#define	RPC_MAX_BACKOFF		1 /* second */
#else
#define	RPC_MAX_BACKOFF		30 /* seconds */
#endif


static struct clnt_ops *clnt_dg_ops(void);
static bool_t time_not_ok(struct timeval *);
static enum clnt_stat clnt_dg_call(CLIENT *, rpcproc_t, xdrproc_t, void *,
	    xdrproc_t, void *, struct timeval);
static void clnt_dg_geterr(CLIENT *, struct rpc_err *);
static bool_t clnt_dg_freeres(CLIENT *, xdrproc_t, void *);
static void clnt_dg_abort(CLIENT *);
static bool_t clnt_dg_control(CLIENT *, u_int, void *);
static void clnt_dg_destroy(CLIENT *);




/*
 *	This machinery implements per-fd locks for MT-safety.  It is not
 *	sufficient to do per-CLIENT handle locks for MT-safety because a
 *	user may create more than one CLIENT handle with the same fd behind
 *	it.  Therfore, we allocate an array of flags (dg_fd_locks), protected
 *	by the clnt_fd_lock mutex, and an array (dg_cv) of condition variables
 *	similarly protected.  Dg_fd_lock[fd] == 1 => a call is activte on some
 *	CLIENT handle created for that fd.
 *	The current implementation holds locks across the entire RPC and reply,
 *	including retransmissions.  Yes, this is silly, and as soon as this
 *	code is proven to work, this should be the first thing fixed.  One step
 *	at a time.
 */
static int	*dg_fd_locks;
static cond_t	*dg_cv;
#define	release_fd_lock(fd, mask) {		\
	mutex_lock(&clnt_fd_lock);	\
	dg_fd_locks[fd] = 0;		\
	mutex_unlock(&clnt_fd_lock);	\
	thr_sigsetmask(SIG_SETMASK, &(mask), NULL); \
	cond_signal(&dg_cv[fd]);	\
}

static const char mem_err_clnt_dg[] = "clnt_dg_create: out of memory";

/* VARIABLES PROTECTED BY clnt_fd_lock: dg_fd_locks, dg_cv */

#define	MCALL_MSG_SIZE 24

/*
 * Private data kept per client handle
 */
struct cu_data {
	int			cu_fd;		/* connections fd */
	bool_t			cu_closeit;	/* opened by library */
	struct sockaddr_storage	cu_raddr;	/* remote address */
	int			cu_rlen;
	struct timeval		cu_wait;	/* retransmit interval */
	struct timeval		cu_total;	/* total time for the call */
	struct rpc_err		cu_error;
	XDR			cu_outxdrs;
	u_int			cu_xdrpos;
	u_int			cu_sendsz;	/* send size */
	char			cu_outhdr[MCALL_MSG_SIZE];
	char			*cu_outbuf;
	u_int			cu_recvsz;	/* recv size */
	int			cu_async;
	int			cu_connect;	/* Use connect(). */
	int			cu_connected;	/* Have done connect(). */
	struct kevent		cu_kin;
	int			cu_kq;
	char			cu_inbuf[1];
};

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
 *
 * fd      - open file descriptor
 * svcaddr - servers address
 * program - program number
 * version - version number
 * sendsz  - buffer recv size
 * recvsz  - buffer send size
 */
CLIENT *
clnt_dg_create(int fd, const struct netbuf *svcaddr, rpcprog_t program,
    rpcvers_t version, u_int sendsz, u_int recvsz)
{
	CLIENT *cl = NULL;		/* client handle */
	struct cu_data *cu = NULL;	/* private data */
	struct timeval now;
	struct rpc_msg call_msg;
	sigset_t mask;
	sigset_t newmask;
	struct __rpc_sockinfo si;
	int one = 1;

	sigfillset(&newmask);
	thr_sigsetmask(SIG_SETMASK, &newmask, &mask);
	mutex_lock(&clnt_fd_lock);
	if (dg_fd_locks == (int *) NULL) {
		int cv_allocsz;
		size_t fd_allocsz;
		int dtbsize = __rpc_dtbsize();

		fd_allocsz = dtbsize * sizeof (int);
		dg_fd_locks = (int *) mem_alloc(fd_allocsz);
		if (dg_fd_locks == (int *) NULL) {
			mutex_unlock(&clnt_fd_lock);
			thr_sigsetmask(SIG_SETMASK, &(mask), NULL);
			goto err1;
		} else
			memset(dg_fd_locks, '\0', fd_allocsz);

		cv_allocsz = dtbsize * sizeof (cond_t);
		dg_cv = (cond_t *) mem_alloc(cv_allocsz);
		if (dg_cv == (cond_t *) NULL) {
			mem_free(dg_fd_locks, fd_allocsz);
			dg_fd_locks = (int *) NULL;
			mutex_unlock(&clnt_fd_lock);
			thr_sigsetmask(SIG_SETMASK, &(mask), NULL);
			goto err1;
		} else {
			int i;

			for (i = 0; i < dtbsize; i++)
				cond_init(&dg_cv[i], 0, (void *) 0);
		}
	}

	mutex_unlock(&clnt_fd_lock);
	thr_sigsetmask(SIG_SETMASK, &(mask), NULL);

	if (svcaddr == NULL) {
		rpc_createerr.cf_stat = RPC_UNKNOWNADDR;
		return (NULL);
	}

	if (!__rpc_fd2sockinfo(fd, &si)) {
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

	if ((cl = mem_alloc(sizeof (CLIENT))) == NULL)
		goto err1;
	/*
	 * Should be multiple of 4 for XDR.
	 */
	sendsz = ((sendsz + 3) / 4) * 4;
	recvsz = ((recvsz + 3) / 4) * 4;
	cu = mem_alloc(sizeof (*cu) + sendsz + recvsz);
	if (cu == NULL)
		goto err1;
	(void) memcpy(&cu->cu_raddr, svcaddr->buf, (size_t)svcaddr->len);
	cu->cu_rlen = svcaddr->len;
	cu->cu_outbuf = &cu->cu_inbuf[recvsz];
	/* Other values can also be set through clnt_control() */
	cu->cu_wait.tv_sec = 15;	/* heuristically chosen */
	cu->cu_wait.tv_usec = 0;
	cu->cu_total.tv_sec = -1;
	cu->cu_total.tv_usec = -1;
	cu->cu_sendsz = sendsz;
	cu->cu_recvsz = recvsz;
	cu->cu_async = FALSE;
	cu->cu_connect = FALSE;
	cu->cu_connected = FALSE;
	(void) gettimeofday(&now, NULL);
	call_msg.rm_xid = __RPC_GETXID(&now);
	call_msg.rm_call.cb_prog = program;
	call_msg.rm_call.cb_vers = version;
	xdrmem_create(&(cu->cu_outxdrs), cu->cu_outhdr, MCALL_MSG_SIZE,
	    XDR_ENCODE);
	if (! xdr_callhdr(&cu->cu_outxdrs, &call_msg)) {
		rpc_createerr.cf_stat = RPC_CANTENCODEARGS;  /* XXX */
		rpc_createerr.cf_error.re_errno = 0;
		goto err2;
	}
	cu->cu_xdrpos = XDR_GETPOS(&(cu->cu_outxdrs));
	XDR_DESTROY(&cu->cu_outxdrs);
	xdrmem_create(&cu->cu_outxdrs, cu->cu_outbuf, sendsz, XDR_ENCODE);

	/* XXX fvdl - do we still want this? */
#if 0
	(void)bindresvport_sa(fd, (struct sockaddr *)svcaddr->buf);
#endif
	_ioctl(fd, FIONBIO, (char *)(void *)&one);

	/*
	 * By default, closeit is always FALSE. It is users responsibility
	 * to do a close on it, else the user may use clnt_control
	 * to let clnt_destroy do it for him/her.
	 */
	cu->cu_closeit = FALSE;
	cu->cu_fd = fd;
	cl->cl_ops = clnt_dg_ops();
	cl->cl_private = (caddr_t)(void *)cu;
	cl->cl_auth = authnone_create();
	cl->cl_tp = NULL;
	cl->cl_netid = NULL;
	cu->cu_kq = -1;
	EV_SET(&cu->cu_kin, cu->cu_fd, EVFILT_READ, EV_ADD, 0, 0, 0);
	return (cl);
err1:
	warnx(mem_err_clnt_dg);
	rpc_createerr.cf_stat = RPC_SYSTEMERROR;
	rpc_createerr.cf_error.re_errno = errno;
err2:
	if (cl) {
		mem_free(cl, sizeof (CLIENT));
		if (cu)
			mem_free(cu, sizeof (*cu) + sendsz + recvsz);
	}
	return (NULL);
}

/*
 * cl       - client handle
 * proc     - procedure number
 * xargs    - xdr routine for args
 * argsp    - pointer to args
 * xresults - xdr routine for results
 * resultsp - pointer to results
 * utimeout - seconds to wait before giving up
 */
static enum clnt_stat
clnt_dg_call(CLIENT *cl, rpcproc_t proc, xdrproc_t xargs, void *argsp,
    xdrproc_t xresults, void *resultsp, struct timeval utimeout)
{
	struct cu_data *cu = (struct cu_data *)cl->cl_private;
	XDR *xdrs;
	size_t outlen = 0;
	struct rpc_msg reply_msg;
	XDR reply_xdrs;
	bool_t ok;
	int nrefreshes = 2;		/* number of times to refresh cred */
	int nretries = 0;		/* number of times we retransmitted */
	struct timeval timeout;
	struct timeval retransmit_time;
	struct timeval next_sendtime, starttime, time_waited, tv;
	struct timespec ts;
	struct kevent kv;
	struct sockaddr *sa;
	sigset_t mask;
	sigset_t newmask;
	socklen_t salen;
	ssize_t recvlen = 0;
	int kin_len, n, rpc_lock_value;
	u_int32_t xid;

	outlen = 0;
	sigfillset(&newmask);
	thr_sigsetmask(SIG_SETMASK, &newmask, &mask);
	mutex_lock(&clnt_fd_lock);
	while (dg_fd_locks[cu->cu_fd])
		cond_wait(&dg_cv[cu->cu_fd], &clnt_fd_lock);
	if (__isthreaded)
		rpc_lock_value = 1;
	else
		rpc_lock_value = 0;
	dg_fd_locks[cu->cu_fd] = rpc_lock_value;
	mutex_unlock(&clnt_fd_lock);
	if (cu->cu_total.tv_usec == -1) {
		timeout = utimeout;	/* use supplied timeout */
	} else {
		timeout = cu->cu_total;	/* use default timeout */
	}

	if (cu->cu_connect && !cu->cu_connected) {
		if (_connect(cu->cu_fd, (struct sockaddr *)&cu->cu_raddr,
		    cu->cu_rlen) < 0) {
			cu->cu_error.re_errno = errno;
			cu->cu_error.re_status = RPC_CANTSEND;
			goto out;
		}
		cu->cu_connected = 1;
	}
	if (cu->cu_connected) {
		sa = NULL;
		salen = 0;
	} else {
		sa = (struct sockaddr *)&cu->cu_raddr;
		salen = cu->cu_rlen;
	}
	time_waited.tv_sec = 0;
	time_waited.tv_usec = 0;
	retransmit_time = next_sendtime = cu->cu_wait;
	gettimeofday(&starttime, NULL);

	/* Clean up in case the last call ended in a longjmp(3) call. */
	if (cu->cu_kq >= 0)
		_close(cu->cu_kq);
	if ((cu->cu_kq = kqueue()) < 0) {
		cu->cu_error.re_errno = errno;
		cu->cu_error.re_status = RPC_CANTSEND;
		goto out;
	}
	kin_len = 1;

call_again:
	if (cu->cu_async == TRUE && xargs == NULL)
		goto get_reply;
	/*
	 * the transaction is the first thing in the out buffer
	 * XXX Yes, and it's in network byte order, so we should to
	 * be careful when we increment it, shouldn't we.
	 */
	xid = ntohl(*(u_int32_t *)(void *)(cu->cu_outhdr));
	xid++;
	*(u_int32_t *)(void *)(cu->cu_outhdr) = htonl(xid);
call_again_same_xid:
	xdrs = &(cu->cu_outxdrs);
	xdrs->x_op = XDR_ENCODE;
	XDR_SETPOS(xdrs, 0);

	if (cl->cl_auth->ah_cred.oa_flavor != RPCSEC_GSS) {
		if ((! XDR_PUTBYTES(xdrs, cu->cu_outhdr, cu->cu_xdrpos)) ||
		    (! XDR_PUTINT32(xdrs, &proc)) ||
		    (! AUTH_MARSHALL(cl->cl_auth, xdrs)) ||
		    (! (*xargs)(xdrs, argsp))) {
			cu->cu_error.re_status = RPC_CANTENCODEARGS;
			goto out;
		}
	} else {
		*(uint32_t *) &cu->cu_outhdr[cu->cu_xdrpos] = htonl(proc);
		if (!__rpc_gss_wrap(cl->cl_auth, cu->cu_outhdr,
			cu->cu_xdrpos + sizeof(uint32_t),
			xdrs, xargs, argsp)) {
			cu->cu_error.re_status = RPC_CANTENCODEARGS;
			goto out;
		}
	}
	outlen = (size_t)XDR_GETPOS(xdrs);

send_again:
	if (_sendto(cu->cu_fd, cu->cu_outbuf, outlen, 0, sa, salen) != outlen) {
		cu->cu_error.re_errno = errno;
		cu->cu_error.re_status = RPC_CANTSEND;
		goto out;
	}

	/*
	 * Hack to provide rpc-based message passing
	 */
	if (timeout.tv_sec == 0 && timeout.tv_usec == 0) {
		cu->cu_error.re_status = RPC_TIMEDOUT;
		goto out;
	}

get_reply:

	/*
	 * sub-optimal code appears here because we have
	 * some clock time to spare while the packets are in flight.
	 * (We assume that this is actually only executed once.)
	 */
	reply_msg.acpted_rply.ar_verf = _null_auth;
	if (cl->cl_auth->ah_cred.oa_flavor != RPCSEC_GSS) {
		reply_msg.acpted_rply.ar_results.where = resultsp;
		reply_msg.acpted_rply.ar_results.proc = xresults;
	} else {
		reply_msg.acpted_rply.ar_results.where = NULL;
		reply_msg.acpted_rply.ar_results.proc = (xdrproc_t)xdr_void;
	}

	for (;;) {
		/* Decide how long to wait. */
		if (timercmp(&next_sendtime, &timeout, <))
			timersub(&next_sendtime, &time_waited, &tv);
		else
			timersub(&timeout, &time_waited, &tv);
		if (tv.tv_sec < 0 || tv.tv_usec < 0)
			tv.tv_sec = tv.tv_usec = 0;
		TIMEVAL_TO_TIMESPEC(&tv, &ts);

		n = _kevent(cu->cu_kq, &cu->cu_kin, kin_len, &kv, 1, &ts);
		/* We don't need to register the event again. */
		kin_len = 0;

		if (n == 1) {
			if (kv.flags & EV_ERROR) {
				cu->cu_error.re_errno = kv.data;
				cu->cu_error.re_status = RPC_CANTRECV;
				goto out;
			}
			/* We have some data now */
			do {
				recvlen = _recvfrom(cu->cu_fd, cu->cu_inbuf,
				    cu->cu_recvsz, 0, NULL, NULL);
			} while (recvlen < 0 && errno == EINTR);
			if (recvlen < 0 && errno != EWOULDBLOCK) {
				cu->cu_error.re_errno = errno;
				cu->cu_error.re_status = RPC_CANTRECV;
				goto out;
			}
			if (recvlen >= sizeof(u_int32_t) &&
			    (cu->cu_async == TRUE ||
			    *((u_int32_t *)(void *)(cu->cu_inbuf)) ==
			    *((u_int32_t *)(void *)(cu->cu_outbuf)))) {
				/* We now assume we have the proper reply. */
				break;
			}
		}
		if (n == -1 && errno != EINTR) {
			cu->cu_error.re_errno = errno;
			cu->cu_error.re_status = RPC_CANTRECV;
			goto out;
		}
		gettimeofday(&tv, NULL);
		timersub(&tv, &starttime, &time_waited);

		/* Check for timeout. */
		if (timercmp(&time_waited, &timeout, >)) {
			cu->cu_error.re_status = RPC_TIMEDOUT;
			goto out;
		}

		/* Retransmit if necessary. */		
		if (timercmp(&time_waited, &next_sendtime, >)) {
			/* update retransmit_time */
			if (retransmit_time.tv_sec < RPC_MAX_BACKOFF)
				timeradd(&retransmit_time, &retransmit_time,
				    &retransmit_time);
			timeradd(&next_sendtime, &retransmit_time,
			    &next_sendtime);
			nretries++;

			/*
			 * When retransmitting a RPCSEC_GSS message,
			 * we must use a new sequence number (handled
			 * by __rpc_gss_wrap above).
			 */
			if (cl->cl_auth->ah_cred.oa_flavor != RPCSEC_GSS)
				goto send_again;
			else
				goto call_again_same_xid;
		}
	}

	/*
	 * now decode and validate the response
	 */

	xdrmem_create(&reply_xdrs, cu->cu_inbuf, (u_int)recvlen, XDR_DECODE);
	ok = xdr_replymsg(&reply_xdrs, &reply_msg);
	/* XDR_DESTROY(&reply_xdrs);	save a few cycles on noop destroy */
	if (ok) {
		if ((reply_msg.rm_reply.rp_stat == MSG_ACCEPTED) &&
			(reply_msg.acpted_rply.ar_stat == SUCCESS))
			cu->cu_error.re_status = RPC_SUCCESS;
		else
			_seterr_reply(&reply_msg, &(cu->cu_error));

		if (cu->cu_error.re_status == RPC_SUCCESS) {
			if (! AUTH_VALIDATE(cl->cl_auth,
					    &reply_msg.acpted_rply.ar_verf)) {
				if (nretries &&
				    cl->cl_auth->ah_cred.oa_flavor
				    == RPCSEC_GSS)
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
					goto get_reply;
				cu->cu_error.re_status = RPC_AUTHERROR;
				cu->cu_error.re_why = AUTH_INVALIDRESP;
			} else {
				if (cl->cl_auth->ah_cred.oa_flavor
				    == RPCSEC_GSS) {
					if (!__rpc_gss_unwrap(cl->cl_auth,
						&reply_xdrs, xresults,
						resultsp))
						cu->cu_error.re_status =
							RPC_CANTDECODERES;
				}
			}
			if (reply_msg.acpted_rply.ar_verf.oa_base != NULL) {
				xdrs->x_op = XDR_FREE;
				(void) xdr_opaque_auth(xdrs,
					&(reply_msg.acpted_rply.ar_verf));
			}
		}		/* end successful completion */
		/*
		 * If unsuccessful AND error is an authentication error
		 * then refresh credentials and try again, else break
		 */
		else if (cu->cu_error.re_status == RPC_AUTHERROR)
			/* maybe our credentials need to be refreshed ... */
			if (nrefreshes > 0 &&
			    AUTH_REFRESH(cl->cl_auth, &reply_msg)) {
				nrefreshes--;
				goto call_again;
			}
		/* end of unsuccessful completion */
	}	/* end of valid reply message */
	else {
		cu->cu_error.re_status = RPC_CANTDECODERES;

	}
out:
	if (cu->cu_kq >= 0)
		_close(cu->cu_kq);
	cu->cu_kq = -1;
	release_fd_lock(cu->cu_fd, mask);
	return (cu->cu_error.re_status);
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
	struct cu_data *cu = (struct cu_data *)cl->cl_private;
	XDR *xdrs = &(cu->cu_outxdrs);
	bool_t dummy;
	sigset_t mask;
	sigset_t newmask;

	sigfillset(&newmask);
	thr_sigsetmask(SIG_SETMASK, &newmask, &mask);
	mutex_lock(&clnt_fd_lock);
	while (dg_fd_locks[cu->cu_fd])
		cond_wait(&dg_cv[cu->cu_fd], &clnt_fd_lock);
	xdrs->x_op = XDR_FREE;
	dummy = (*xdr_res)(xdrs, res_ptr);
	mutex_unlock(&clnt_fd_lock);
	thr_sigsetmask(SIG_SETMASK, &mask, NULL);
	cond_signal(&dg_cv[cu->cu_fd]);
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
	struct netbuf *addr;
	sigset_t mask;
	sigset_t newmask;
	int rpc_lock_value;

	sigfillset(&newmask);
	thr_sigsetmask(SIG_SETMASK, &newmask, &mask);
	mutex_lock(&clnt_fd_lock);
	while (dg_fd_locks[cu->cu_fd])
		cond_wait(&dg_cv[cu->cu_fd], &clnt_fd_lock);
	if (__isthreaded)
                rpc_lock_value = 1;
        else
                rpc_lock_value = 0;
	dg_fd_locks[cu->cu_fd] = rpc_lock_value;
	mutex_unlock(&clnt_fd_lock);
	switch (request) {
	case CLSET_FD_CLOSE:
		cu->cu_closeit = TRUE;
		release_fd_lock(cu->cu_fd, mask);
		return (TRUE);
	case CLSET_FD_NCLOSE:
		cu->cu_closeit = FALSE;
		release_fd_lock(cu->cu_fd, mask);
		return (TRUE);
	}

	/* for other requests which use info */
	if (info == NULL) {
		release_fd_lock(cu->cu_fd, mask);
		return (FALSE);
	}
	switch (request) {
	case CLSET_TIMEOUT:
		if (time_not_ok((struct timeval *)info)) {
			release_fd_lock(cu->cu_fd, mask);
			return (FALSE);
		}
		cu->cu_total = *(struct timeval *)info;
		break;
	case CLGET_TIMEOUT:
		*(struct timeval *)info = cu->cu_total;
		break;
	case CLGET_SERVER_ADDR:		/* Give him the fd address */
		/* Now obsolete. Only for backward compatibility */
		(void) memcpy(info, &cu->cu_raddr, (size_t)cu->cu_rlen);
		break;
	case CLSET_RETRY_TIMEOUT:
		if (time_not_ok((struct timeval *)info)) {
			release_fd_lock(cu->cu_fd, mask);
			return (FALSE);
		}
		cu->cu_wait = *(struct timeval *)info;
		break;
	case CLGET_RETRY_TIMEOUT:
		*(struct timeval *)info = cu->cu_wait;
		break;
	case CLGET_FD:
		*(int *)info = cu->cu_fd;
		break;
	case CLGET_SVC_ADDR:
		addr = (struct netbuf *)info;
		addr->buf = &cu->cu_raddr;
		addr->len = cu->cu_rlen;
		addr->maxlen = sizeof cu->cu_raddr;
		break;
	case CLSET_SVC_ADDR:		/* set to new address */
		addr = (struct netbuf *)info;
		if (addr->len < sizeof cu->cu_raddr) {
			release_fd_lock(cu->cu_fd, mask);
			return (FALSE);
		}
		(void) memcpy(&cu->cu_raddr, addr->buf, addr->len);
		cu->cu_rlen = addr->len;
		break;
	case CLGET_XID:
		/*
		 * use the knowledge that xid is the
		 * first element in the call structure *.
		 * This will get the xid of the PREVIOUS call
		 */
		*(u_int32_t *)info =
		    ntohl(*(u_int32_t *)(void *)cu->cu_outhdr);
		break;

	case CLSET_XID:
		/* This will set the xid of the NEXT call */
		*(u_int32_t *)(void *)cu->cu_outhdr =
		    htonl(*(u_int32_t *)info - 1);
		/* decrement by 1 as clnt_dg_call() increments once */
		break;

	case CLGET_VERS:
		/*
		 * This RELIES on the information that, in the call body,
		 * the version number field is the fifth field from the
		 * beginning of the RPC header. MUST be changed if the
		 * call_struct is changed
		 */
		*(u_int32_t *)info =
		    ntohl(*(u_int32_t *)(void *)(cu->cu_outhdr +
		    4 * BYTES_PER_XDR_UNIT));
		break;

	case CLSET_VERS:
		*(u_int32_t *)(void *)(cu->cu_outhdr + 4 * BYTES_PER_XDR_UNIT)
			= htonl(*(u_int32_t *)info);
		break;

	case CLGET_PROG:
		/*
		 * This RELIES on the information that, in the call body,
		 * the program number field is the fourth field from the
		 * beginning of the RPC header. MUST be changed if the
		 * call_struct is changed
		 */
		*(u_int32_t *)info =
		    ntohl(*(u_int32_t *)(void *)(cu->cu_outhdr +
		    3 * BYTES_PER_XDR_UNIT));
		break;

	case CLSET_PROG:
		*(u_int32_t *)(void *)(cu->cu_outhdr + 3 * BYTES_PER_XDR_UNIT)
			= htonl(*(u_int32_t *)info);
		break;
	case CLSET_ASYNC:
		cu->cu_async = *(int *)info;
		break;
	case CLSET_CONNECT:
		cu->cu_connect = *(int *)info;
		break;
	default:
		release_fd_lock(cu->cu_fd, mask);
		return (FALSE);
	}
	release_fd_lock(cu->cu_fd, mask);
	return (TRUE);
}

static void
clnt_dg_destroy(CLIENT *cl)
{
	struct cu_data *cu = (struct cu_data *)cl->cl_private;
	int cu_fd = cu->cu_fd;
	sigset_t mask;
	sigset_t newmask;

	sigfillset(&newmask);
	thr_sigsetmask(SIG_SETMASK, &newmask, &mask);
	mutex_lock(&clnt_fd_lock);
	while (dg_fd_locks[cu_fd])
		cond_wait(&dg_cv[cu_fd], &clnt_fd_lock);
	if (cu->cu_closeit)
		(void)_close(cu_fd);
	if (cu->cu_kq >= 0)
		_close(cu->cu_kq);
	XDR_DESTROY(&(cu->cu_outxdrs));
	mem_free(cu, (sizeof (*cu) + cu->cu_sendsz + cu->cu_recvsz));
	if (cl->cl_netid && cl->cl_netid[0])
		mem_free(cl->cl_netid, strlen(cl->cl_netid) +1);
	if (cl->cl_tp && cl->cl_tp[0])
		mem_free(cl->cl_tp, strlen(cl->cl_tp) +1);
	mem_free(cl, sizeof (CLIENT));
	mutex_unlock(&clnt_fd_lock);
	thr_sigsetmask(SIG_SETMASK, &mask, NULL);
	cond_signal(&dg_cv[cu_fd]);
}

static struct clnt_ops *
clnt_dg_ops(void)
{
	static struct clnt_ops ops;
	sigset_t mask;
	sigset_t newmask;

/* VARIABLES PROTECTED BY ops_lock: ops */

	sigfillset(&newmask);
	thr_sigsetmask(SIG_SETMASK, &newmask, &mask);
	mutex_lock(&ops_lock);
	if (ops.cl_call == NULL) {
		ops.cl_call = clnt_dg_call;
		ops.cl_abort = clnt_dg_abort;
		ops.cl_geterr = clnt_dg_geterr;
		ops.cl_freeres = clnt_dg_freeres;
		ops.cl_destroy = clnt_dg_destroy;
		ops.cl_control = clnt_dg_control;
	}
	mutex_unlock(&ops_lock);
	thr_sigsetmask(SIG_SETMASK, &mask, NULL);
	return (&ops);
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

