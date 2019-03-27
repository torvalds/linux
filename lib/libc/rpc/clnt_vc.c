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

#include "namespace.h"
#include "reentrant.h"
#include <sys/types.h>
#include <sys/poll.h>
#include <sys/syslog.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/uio.h>

#include <arpa/inet.h>
#include <assert.h>
#include <err.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <rpc/rpc.h>
#include <rpc/rpcsec_gss.h>
#include "un-namespace.h"
#include "rpc_com.h"
#include "mt_misc.h"

#define MCALL_MSG_SIZE 24

struct cmessage {
        struct cmsghdr cmsg;
        struct cmsgcred cmcred;
};

static enum clnt_stat clnt_vc_call(CLIENT *, rpcproc_t, xdrproc_t, void *,
    xdrproc_t, void *, struct timeval);
static void clnt_vc_geterr(CLIENT *, struct rpc_err *);
static bool_t clnt_vc_freeres(CLIENT *, xdrproc_t, void *);
static void clnt_vc_abort(CLIENT *);
static bool_t clnt_vc_control(CLIENT *, u_int, void *);
static void clnt_vc_destroy(CLIENT *);
static struct clnt_ops *clnt_vc_ops(void);
static bool_t time_not_ok(struct timeval *);
static int read_vc(void *, void *, int);
static int write_vc(void *, void *, int);
static int __msgwrite(int, void *, size_t);
static int __msgread(int, void *, size_t);

struct ct_data {
	int		ct_fd;		/* connection's fd */
	bool_t		ct_closeit;	/* close it on destroy */
	struct timeval	ct_wait;	/* wait interval in milliseconds */
	bool_t          ct_waitset;	/* wait set by clnt_control? */
	struct netbuf	ct_addr;	/* remote addr */
	struct rpc_err	ct_error;
	union {
		char	ct_mcallc[MCALL_MSG_SIZE];	/* marshalled callmsg */
		u_int32_t ct_mcalli;
	} ct_u;
	u_int		ct_mpos;	/* pos after marshal */
	XDR		ct_xdrs;	/* XDR stream */
};

/*
 *      This machinery implements per-fd locks for MT-safety.  It is not
 *      sufficient to do per-CLIENT handle locks for MT-safety because a
 *      user may create more than one CLIENT handle with the same fd behind
 *      it.  Therfore, we allocate an array of flags (vc_fd_locks), protected
 *      by the clnt_fd_lock mutex, and an array (vc_cv) of condition variables
 *      similarly protected.  Vc_fd_lock[fd] == 1 => a call is activte on some
 *      CLIENT handle created for that fd.
 *      The current implementation holds locks across the entire RPC and reply.
 *      Yes, this is silly, and as soon as this code is proven to work, this
 *      should be the first thing fixed.  One step at a time.
 */
static int      *vc_fd_locks;
static cond_t   *vc_cv;
#define release_fd_lock(fd, mask) {	\
	mutex_lock(&clnt_fd_lock);	\
	vc_fd_locks[fd] = 0;		\
	mutex_unlock(&clnt_fd_lock);	\
	thr_sigsetmask(SIG_SETMASK, &(mask), (sigset_t *) NULL);	\
	cond_signal(&vc_cv[fd]);	\
}

static const char clnt_vc_errstr[] = "%s : %s";
static const char clnt_vc_str[] = "clnt_vc_create";
static const char __no_mem_str[] = "out of memory";

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
 *
 * fd - open file descriptor
 * raddr - servers address
 * prog  - program number
 * vers  - version number
 * sendsz - buffer send size
 * recvsz - buffer recv size
 */
CLIENT *
clnt_vc_create(int fd, const struct netbuf *raddr, const rpcprog_t prog,
    const rpcvers_t vers, u_int sendsz, u_int recvsz)
{
	CLIENT *cl;			/* client handle */
	struct ct_data *ct = NULL;	/* client handle */
	struct timeval now;
	struct rpc_msg call_msg;
	static u_int32_t disrupt;
	sigset_t mask;
	sigset_t newmask;
	struct sockaddr_storage ss;
	socklen_t slen;
	struct __rpc_sockinfo si;

	if (disrupt == 0)
		disrupt = (u_int32_t)(long)raddr;

	cl = (CLIENT *)mem_alloc(sizeof (*cl));
	ct = (struct ct_data *)mem_alloc(sizeof (*ct));
	if ((cl == (CLIENT *)NULL) || (ct == (struct ct_data *)NULL)) {
		(void) syslog(LOG_ERR, clnt_vc_errstr,
		    clnt_vc_str, __no_mem_str);
		rpc_createerr.cf_stat = RPC_SYSTEMERROR;
		rpc_createerr.cf_error.re_errno = errno;
		goto err;
	}
	ct->ct_addr.buf = NULL;
	sigfillset(&newmask);
	thr_sigsetmask(SIG_SETMASK, &newmask, &mask);
	mutex_lock(&clnt_fd_lock);
	if (vc_fd_locks == (int *) NULL) {
		int cv_allocsz, fd_allocsz;
		int dtbsize = __rpc_dtbsize();

		fd_allocsz = dtbsize * sizeof (int);
		vc_fd_locks = (int *) mem_alloc(fd_allocsz);
		if (vc_fd_locks == (int *) NULL) {
			mutex_unlock(&clnt_fd_lock);
			thr_sigsetmask(SIG_SETMASK, &(mask), NULL);
			goto err;
		} else
			memset(vc_fd_locks, '\0', fd_allocsz);

		assert(vc_cv == (cond_t *) NULL);
		cv_allocsz = dtbsize * sizeof (cond_t);
		vc_cv = (cond_t *) mem_alloc(cv_allocsz);
		if (vc_cv == (cond_t *) NULL) {
			mem_free(vc_fd_locks, fd_allocsz);
			vc_fd_locks = (int *) NULL;
			mutex_unlock(&clnt_fd_lock);
			thr_sigsetmask(SIG_SETMASK, &(mask), NULL);
			goto err;
		} else {
			int i;

			for (i = 0; i < dtbsize; i++)
				cond_init(&vc_cv[i], 0, (void *) 0);
		}
	} else
		assert(vc_cv != (cond_t *) NULL);

	/*
	 * XXX - fvdl connecting while holding a mutex?
	 */
	slen = sizeof ss;
	if (_getpeername(fd, (struct sockaddr *)(void *)&ss, &slen) < 0) {
		if (errno != ENOTCONN) {
			rpc_createerr.cf_stat = RPC_SYSTEMERROR;
			rpc_createerr.cf_error.re_errno = errno;
			mutex_unlock(&clnt_fd_lock);
			thr_sigsetmask(SIG_SETMASK, &(mask), NULL);
			goto err;
		}
		if (_connect(fd, (struct sockaddr *)raddr->buf, raddr->len) < 0){
			rpc_createerr.cf_stat = RPC_SYSTEMERROR;
			rpc_createerr.cf_error.re_errno = errno;
			mutex_unlock(&clnt_fd_lock);
			thr_sigsetmask(SIG_SETMASK, &(mask), NULL);
			goto err;
		}
	}
	mutex_unlock(&clnt_fd_lock);
	thr_sigsetmask(SIG_SETMASK, &(mask), NULL);
	if (!__rpc_fd2sockinfo(fd, &si))
		goto err;

	ct->ct_closeit = FALSE;

	/*
	 * Set up private data struct
	 */
	ct->ct_fd = fd;
	ct->ct_wait.tv_usec = 0;
	ct->ct_waitset = FALSE;
	ct->ct_addr.buf = malloc(raddr->maxlen);
	if (ct->ct_addr.buf == NULL)
		goto err;
	memcpy(ct->ct_addr.buf, raddr->buf, raddr->len);
	ct->ct_addr.len = raddr->len;
	ct->ct_addr.maxlen = raddr->maxlen;

	/*
	 * Initialize call message
	 */
	(void)gettimeofday(&now, NULL);
	call_msg.rm_xid = ((u_int32_t)++disrupt) ^ __RPC_GETXID(&now);
	call_msg.rm_direction = CALL;
	call_msg.rm_call.cb_rpcvers = RPC_MSG_VERSION;
	call_msg.rm_call.cb_prog = (u_int32_t)prog;
	call_msg.rm_call.cb_vers = (u_int32_t)vers;

	/*
	 * pre-serialize the static part of the call msg and stash it away
	 */
	xdrmem_create(&(ct->ct_xdrs), ct->ct_u.ct_mcallc, MCALL_MSG_SIZE,
	    XDR_ENCODE);
	if (! xdr_callhdr(&(ct->ct_xdrs), &call_msg)) {
		if (ct->ct_closeit) {
			(void)_close(fd);
		}
		goto err;
	}
	ct->ct_mpos = XDR_GETPOS(&(ct->ct_xdrs));
	XDR_DESTROY(&(ct->ct_xdrs));
	assert(ct->ct_mpos + sizeof(uint32_t) <= MCALL_MSG_SIZE);

	/*
	 * Create a client handle which uses xdrrec for serialization
	 * and authnone for authentication.
	 */
	cl->cl_ops = clnt_vc_ops();
	cl->cl_private = ct;
	cl->cl_auth = authnone_create();
	sendsz = __rpc_get_t_size(si.si_af, si.si_proto, (int)sendsz);
	recvsz = __rpc_get_t_size(si.si_af, si.si_proto, (int)recvsz);
	xdrrec_create(&(ct->ct_xdrs), sendsz, recvsz,
	    cl->cl_private, read_vc, write_vc);
	return (cl);

err:
	if (ct) {
		if (ct->ct_addr.len)
			mem_free(ct->ct_addr.buf, ct->ct_addr.len);
		mem_free(ct, sizeof (struct ct_data));
	}
	if (cl)
		mem_free(cl, sizeof (CLIENT));
	return ((CLIENT *)NULL);
}

static enum clnt_stat
clnt_vc_call(CLIENT *cl, rpcproc_t proc, xdrproc_t xdr_args, void *args_ptr,
    xdrproc_t xdr_results, void *results_ptr, struct timeval timeout)
{
	struct ct_data *ct = (struct ct_data *) cl->cl_private;
	XDR *xdrs = &(ct->ct_xdrs);
	struct rpc_msg reply_msg;
	u_int32_t x_id;
	u_int32_t *msg_x_id = &ct->ct_u.ct_mcalli;    /* yuk */
	bool_t shipnow;
	int refreshes = 2;
	sigset_t mask, newmask;
	int rpc_lock_value;
	bool_t reply_stat;

	assert(cl != NULL);

	sigfillset(&newmask);
	thr_sigsetmask(SIG_SETMASK, &newmask, &mask);
	mutex_lock(&clnt_fd_lock);
	while (vc_fd_locks[ct->ct_fd])
		cond_wait(&vc_cv[ct->ct_fd], &clnt_fd_lock);
	if (__isthreaded)
                rpc_lock_value = 1;
        else
                rpc_lock_value = 0;
	vc_fd_locks[ct->ct_fd] = rpc_lock_value;
	mutex_unlock(&clnt_fd_lock);
	if (!ct->ct_waitset) {
		/* If time is not within limits, we ignore it. */
		if (time_not_ok(&timeout) == FALSE)
			ct->ct_wait = timeout;
	}

	shipnow =
	    (xdr_results == NULL && timeout.tv_sec == 0
	    && timeout.tv_usec == 0) ? FALSE : TRUE;

call_again:
	xdrs->x_op = XDR_ENCODE;
	ct->ct_error.re_status = RPC_SUCCESS;
	x_id = ntohl(--(*msg_x_id));

	if (cl->cl_auth->ah_cred.oa_flavor != RPCSEC_GSS) {
		if ((! XDR_PUTBYTES(xdrs, ct->ct_u.ct_mcallc, ct->ct_mpos)) ||
		    (! XDR_PUTINT32(xdrs, &proc)) ||
		    (! AUTH_MARSHALL(cl->cl_auth, xdrs)) ||
		    (! (*xdr_args)(xdrs, args_ptr))) {
			if (ct->ct_error.re_status == RPC_SUCCESS)
				ct->ct_error.re_status = RPC_CANTENCODEARGS;
			(void)xdrrec_endofrecord(xdrs, TRUE);
			release_fd_lock(ct->ct_fd, mask);
			return (ct->ct_error.re_status);
		}
	} else {
		*(uint32_t *) &ct->ct_u.ct_mcallc[ct->ct_mpos] = htonl(proc);
		if (! __rpc_gss_wrap(cl->cl_auth, ct->ct_u.ct_mcallc,
			ct->ct_mpos + sizeof(uint32_t),
			xdrs, xdr_args, args_ptr)) {
			if (ct->ct_error.re_status == RPC_SUCCESS)
				ct->ct_error.re_status = RPC_CANTENCODEARGS;
			(void)xdrrec_endofrecord(xdrs, TRUE);
			release_fd_lock(ct->ct_fd, mask);
			return (ct->ct_error.re_status);
		}
	}
	if (! xdrrec_endofrecord(xdrs, shipnow)) {
		release_fd_lock(ct->ct_fd, mask);
		return (ct->ct_error.re_status = RPC_CANTSEND);
	}
	if (! shipnow) {
		release_fd_lock(ct->ct_fd, mask);
		return (RPC_SUCCESS);
	}
	/*
	 * Hack to provide rpc-based message passing
	 */
	if (timeout.tv_sec == 0 && timeout.tv_usec == 0) {
		release_fd_lock(ct->ct_fd, mask);
		return(ct->ct_error.re_status = RPC_TIMEDOUT);
	}


	/*
	 * Keep receiving until we get a valid transaction id
	 */
	xdrs->x_op = XDR_DECODE;
	while (TRUE) {
		reply_msg.acpted_rply.ar_verf = _null_auth;
		reply_msg.acpted_rply.ar_results.where = NULL;
		reply_msg.acpted_rply.ar_results.proc = (xdrproc_t)xdr_void;
		if (! xdrrec_skiprecord(xdrs)) {
			release_fd_lock(ct->ct_fd, mask);
			return (ct->ct_error.re_status);
		}
		/* now decode and validate the response header */
		if (! xdr_replymsg(xdrs, &reply_msg)) {
			if (ct->ct_error.re_status == RPC_SUCCESS)
				continue;
			release_fd_lock(ct->ct_fd, mask);
			return (ct->ct_error.re_status);
		}
		if (reply_msg.rm_xid == x_id)
			break;
	}

	/*
	 * process header
	 */
	_seterr_reply(&reply_msg, &(ct->ct_error));
	if (ct->ct_error.re_status == RPC_SUCCESS) {
		if (! AUTH_VALIDATE(cl->cl_auth,
		    &reply_msg.acpted_rply.ar_verf)) {
			ct->ct_error.re_status = RPC_AUTHERROR;
			ct->ct_error.re_why = AUTH_INVALIDRESP;
		} else {
			if (cl->cl_auth->ah_cred.oa_flavor != RPCSEC_GSS) {
				reply_stat = (*xdr_results)(xdrs, results_ptr);
			} else {
				reply_stat = __rpc_gss_unwrap(cl->cl_auth,
				    xdrs, xdr_results, results_ptr);
			}
			if (! reply_stat) {
				if (ct->ct_error.re_status == RPC_SUCCESS)
					ct->ct_error.re_status =
						RPC_CANTDECODERES;
			}
		}
		/* free verifier ... */
		if (reply_msg.acpted_rply.ar_verf.oa_base != NULL) {
			xdrs->x_op = XDR_FREE;
			(void)xdr_opaque_auth(xdrs,
			    &(reply_msg.acpted_rply.ar_verf));
		}
	}  /* end successful completion */
	else {
		/* maybe our credentials need to be refreshed ... */
		if (refreshes-- && AUTH_REFRESH(cl->cl_auth, &reply_msg))
			goto call_again;
	}  /* end of unsuccessful completion */
	release_fd_lock(ct->ct_fd, mask);
	return (ct->ct_error.re_status);
}

static void
clnt_vc_geterr(CLIENT *cl, struct rpc_err *errp)
{
	struct ct_data *ct;

	assert(cl != NULL);
	assert(errp != NULL);

	ct = (struct ct_data *) cl->cl_private;
	*errp = ct->ct_error;
}

static bool_t
clnt_vc_freeres(CLIENT *cl, xdrproc_t xdr_res, void *res_ptr)
{
	struct ct_data *ct;
	XDR *xdrs;
	bool_t dummy;
	sigset_t mask;
	sigset_t newmask;

	assert(cl != NULL);

	ct = (struct ct_data *)cl->cl_private;
	xdrs = &(ct->ct_xdrs);

	sigfillset(&newmask);
	thr_sigsetmask(SIG_SETMASK, &newmask, &mask);
	mutex_lock(&clnt_fd_lock);
	while (vc_fd_locks[ct->ct_fd])
		cond_wait(&vc_cv[ct->ct_fd], &clnt_fd_lock);
	xdrs->x_op = XDR_FREE;
	dummy = (*xdr_res)(xdrs, res_ptr);
	mutex_unlock(&clnt_fd_lock);
	thr_sigsetmask(SIG_SETMASK, &(mask), NULL);
	cond_signal(&vc_cv[ct->ct_fd]);

	return dummy;
}

/*ARGSUSED*/
static void
clnt_vc_abort(CLIENT *cl)
{
}

static __inline void
htonlp(void *dst, const void *src, uint32_t incr)
{
	/* We are aligned, so we think */
	*(uint32_t *)dst = htonl(*(const uint32_t *)src + incr);
}

static __inline void
ntohlp(void *dst, const void *src)
{
	/* We are aligned, so we think */
	*(uint32_t *)dst = htonl(*(const uint32_t *)src);
}

static bool_t
clnt_vc_control(CLIENT *cl, u_int request, void *info)
{
	struct ct_data *ct;
	void *infop = info;
	sigset_t mask;
	sigset_t newmask;
	int rpc_lock_value;

	assert(cl != NULL);

	ct = (struct ct_data *)cl->cl_private;

	sigfillset(&newmask);
	thr_sigsetmask(SIG_SETMASK, &newmask, &mask);
	mutex_lock(&clnt_fd_lock);
	while (vc_fd_locks[ct->ct_fd])
		cond_wait(&vc_cv[ct->ct_fd], &clnt_fd_lock);
	if (__isthreaded)
                rpc_lock_value = 1;
        else
                rpc_lock_value = 0;
	vc_fd_locks[ct->ct_fd] = rpc_lock_value;
	mutex_unlock(&clnt_fd_lock);

	switch (request) {
	case CLSET_FD_CLOSE:
		ct->ct_closeit = TRUE;
		release_fd_lock(ct->ct_fd, mask);
		return (TRUE);
	case CLSET_FD_NCLOSE:
		ct->ct_closeit = FALSE;
		release_fd_lock(ct->ct_fd, mask);
		return (TRUE);
	default:
		break;
	}

	/* for other requests which use info */
	if (info == NULL) {
		release_fd_lock(ct->ct_fd, mask);
		return (FALSE);
	}
	switch (request) {
	case CLSET_TIMEOUT:
		if (time_not_ok((struct timeval *)info)) {
			release_fd_lock(ct->ct_fd, mask);
			return (FALSE);
		}
		ct->ct_wait = *(struct timeval *)infop;
		ct->ct_waitset = TRUE;
		break;
	case CLGET_TIMEOUT:
		*(struct timeval *)infop = ct->ct_wait;
		break;
	case CLGET_SERVER_ADDR:
		(void) memcpy(info, ct->ct_addr.buf, (size_t)ct->ct_addr.len);
		break;
	case CLGET_FD:
		*(int *)info = ct->ct_fd;
		break;
	case CLGET_SVC_ADDR:
		/* The caller should not free this memory area */
		*(struct netbuf *)info = ct->ct_addr;
		break;
	case CLSET_SVC_ADDR:		/* set to new address */
		release_fd_lock(ct->ct_fd, mask);
		return (FALSE);
	case CLGET_XID:
		/*
		 * use the knowledge that xid is the
		 * first element in the call structure
		 * This will get the xid of the PREVIOUS call
		 */
		ntohlp(info, &ct->ct_u.ct_mcalli);
		break;
	case CLSET_XID:
		/* This will set the xid of the NEXT call */
		/* increment by 1 as clnt_vc_call() decrements once */
		htonlp(&ct->ct_u.ct_mcalli, info, 1);
		break;
	case CLGET_VERS:
		/*
		 * This RELIES on the information that, in the call body,
		 * the version number field is the fifth field from the
		 * beginning of the RPC header. MUST be changed if the
		 * call_struct is changed
		 */
		ntohlp(info, ct->ct_u.ct_mcallc + 4 * BYTES_PER_XDR_UNIT);
		break;

	case CLSET_VERS:
		htonlp(ct->ct_u.ct_mcallc + 4 * BYTES_PER_XDR_UNIT, info, 0);
		break;

	case CLGET_PROG:
		/*
		 * This RELIES on the information that, in the call body,
		 * the program number field is the fourth field from the
		 * beginning of the RPC header. MUST be changed if the
		 * call_struct is changed
		 */
		ntohlp(info, ct->ct_u.ct_mcallc + 3 * BYTES_PER_XDR_UNIT);
		break;

	case CLSET_PROG:
		htonlp(ct->ct_u.ct_mcallc + 3 * BYTES_PER_XDR_UNIT, info, 0);
		break;

	default:
		release_fd_lock(ct->ct_fd, mask);
		return (FALSE);
	}
	release_fd_lock(ct->ct_fd, mask);
	return (TRUE);
}


static void
clnt_vc_destroy(CLIENT *cl)
{
	struct ct_data *ct = (struct ct_data *) cl->cl_private;
	int ct_fd = ct->ct_fd;
	sigset_t mask;
	sigset_t newmask;

	assert(cl != NULL);

	ct = (struct ct_data *) cl->cl_private;

	sigfillset(&newmask);
	thr_sigsetmask(SIG_SETMASK, &newmask, &mask);
	mutex_lock(&clnt_fd_lock);
	while (vc_fd_locks[ct_fd])
		cond_wait(&vc_cv[ct_fd], &clnt_fd_lock);
	if (ct->ct_closeit && ct->ct_fd != -1) {
		(void)_close(ct->ct_fd);
	}
	XDR_DESTROY(&(ct->ct_xdrs));
	free(ct->ct_addr.buf);
	mem_free(ct, sizeof(struct ct_data));
	if (cl->cl_netid && cl->cl_netid[0])
		mem_free(cl->cl_netid, strlen(cl->cl_netid) +1);
	if (cl->cl_tp && cl->cl_tp[0])
		mem_free(cl->cl_tp, strlen(cl->cl_tp) +1);
	mem_free(cl, sizeof(CLIENT));
	mutex_unlock(&clnt_fd_lock);
	thr_sigsetmask(SIG_SETMASK, &(mask), NULL);
	cond_signal(&vc_cv[ct_fd]);
}

/*
 * Interface between xdr serializer and tcp connection.
 * Behaves like the system calls, read & write, but keeps some error state
 * around for the rpc level.
 */
static int
read_vc(void *ctp, void *buf, int len)
{
	struct sockaddr sa;
	socklen_t sal;
	struct ct_data *ct = (struct ct_data *)ctp;
	struct pollfd fd;
	int milliseconds = (int)((ct->ct_wait.tv_sec * 1000) +
	    (ct->ct_wait.tv_usec / 1000));

	if (len == 0)
		return (0);
	fd.fd = ct->ct_fd;
	fd.events = POLLIN;
	for (;;) {
		switch (_poll(&fd, 1, milliseconds)) {
		case 0:
			ct->ct_error.re_status = RPC_TIMEDOUT;
			return (-1);

		case -1:
			if (errno == EINTR)
				continue;
			ct->ct_error.re_status = RPC_CANTRECV;
			ct->ct_error.re_errno = errno;
			return (-1);
		}
		break;
	}

	sal = sizeof(sa);
	if ((_getpeername(ct->ct_fd, &sa, &sal) == 0) &&
	    (sa.sa_family == AF_LOCAL)) {
		len = __msgread(ct->ct_fd, buf, (size_t)len);
	} else {
		len = _read(ct->ct_fd, buf, (size_t)len);
	}

	switch (len) {
	case 0:
		/* premature eof */
		ct->ct_error.re_errno = ECONNRESET;
		ct->ct_error.re_status = RPC_CANTRECV;
		len = -1;  /* it's really an error */
		break;

	case -1:
		ct->ct_error.re_errno = errno;
		ct->ct_error.re_status = RPC_CANTRECV;
		break;
	}
	return (len);
}

static int
write_vc(void *ctp, void *buf, int len)
{
	struct sockaddr sa;
	socklen_t sal;
	struct ct_data *ct = (struct ct_data *)ctp;
	int i, cnt;

	sal = sizeof(sa);
	if ((_getpeername(ct->ct_fd, &sa, &sal) == 0) &&
	    (sa.sa_family == AF_LOCAL)) {
		for (cnt = len; cnt > 0; cnt -= i, buf = (char *)buf + i) {
			if ((i = __msgwrite(ct->ct_fd, buf,
			     (size_t)cnt)) == -1) {
				ct->ct_error.re_errno = errno;
				ct->ct_error.re_status = RPC_CANTSEND;
				return (-1);
			}
		}
	} else {
		for (cnt = len; cnt > 0; cnt -= i, buf = (char *)buf + i) {
			if ((i = _write(ct->ct_fd, buf, (size_t)cnt)) == -1) {
				ct->ct_error.re_errno = errno;
				ct->ct_error.re_status = RPC_CANTSEND;
				return (-1);
			}
		}
	}
	return (len);
}

static struct clnt_ops *
clnt_vc_ops(void)
{
	static struct clnt_ops ops;
	sigset_t mask, newmask;

	/* VARIABLES PROTECTED BY ops_lock: ops */

	sigfillset(&newmask);
	thr_sigsetmask(SIG_SETMASK, &newmask, &mask);
	mutex_lock(&ops_lock);
	if (ops.cl_call == NULL) {
		ops.cl_call = clnt_vc_call;
		ops.cl_abort = clnt_vc_abort;
		ops.cl_geterr = clnt_vc_geterr;
		ops.cl_freeres = clnt_vc_freeres;
		ops.cl_destroy = clnt_vc_destroy;
		ops.cl_control = clnt_vc_control;
	}
	mutex_unlock(&ops_lock);
	thr_sigsetmask(SIG_SETMASK, &(mask), NULL);
	return (&ops);
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

static int
__msgread(int sock, void *buf, size_t cnt)
{
	struct iovec iov[1];
	struct msghdr msg;
	union {
		struct cmsghdr cmsg;
		char control[CMSG_SPACE(sizeof(struct cmsgcred))];
	} cm;
 
	bzero((char *)&cm, sizeof(cm));
	iov[0].iov_base = buf;
	iov[0].iov_len = cnt;
 
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_control = (caddr_t)&cm;
	msg.msg_controllen = CMSG_SPACE(sizeof(struct cmsgcred));
	msg.msg_flags = 0;
 
	return(_recvmsg(sock, &msg, 0));
}

static int
__msgwrite(int sock, void *buf, size_t cnt)
{
	struct iovec iov[1];
	struct msghdr msg;
	union {
		struct cmsghdr cmsg;
		char control[CMSG_SPACE(sizeof(struct cmsgcred))];
	} cm;
 
	bzero((char *)&cm, sizeof(cm));
	iov[0].iov_base = buf;
	iov[0].iov_len = cnt;
 
	cm.cmsg.cmsg_type = SCM_CREDS;
	cm.cmsg.cmsg_level = SOL_SOCKET;
	cm.cmsg.cmsg_len = CMSG_LEN(sizeof(struct cmsgcred));
 
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_control = (caddr_t)&cm;
	msg.msg_controllen = CMSG_SPACE(sizeof(struct cmsgcred));
	msg.msg_flags = 0;

	return(_sendmsg(sock, &msg, 0));
}
