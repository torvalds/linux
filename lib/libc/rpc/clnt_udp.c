/*	$OpenBSD: clnt_udp.c,v 1.42 2024/04/02 08:51:06 jsg Exp $ */

/*
 * Copyright (c) 2010, Oracle America, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials
 *       provided with the distribution.
 *     * Neither the name of the "Oracle America, Inc." nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *   FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *   COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 *   INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 *   GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * clnt_udp.c, Implements a UDP/IP based, client side RPC.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <rpc/rpc.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include "clnt_udp.h"

/*
 * UDP bases client side rpc operations
 */
static enum clnt_stat	clntudp_call(CLIENT *, u_long, xdrproc_t, caddr_t,
			    xdrproc_t, caddr_t, struct timeval);
static void		clntudp_abort(CLIENT *);
static void		clntudp_geterr(CLIENT *, struct rpc_err *);
static bool_t		clntudp_freeres(CLIENT *, xdrproc_t, caddr_t);
static bool_t           clntudp_control(CLIENT *, u_int, void *);
static void		clntudp_destroy(CLIENT *);

static const struct clnt_ops udp_ops = {
	clntudp_call,
	clntudp_abort,
	clntudp_geterr,
	clntudp_freeres,
	clntudp_destroy,
	clntudp_control
};

int
clntudp_bufcreate1(struct clntudp_bufcreate_args *args)
{
	args->cl = (CLIENT *)mem_alloc(sizeof(CLIENT));
	if (args->cl == NULL) {
		rpc_createerr.cf_stat = RPC_SYSTEMERROR;
		rpc_createerr.cf_error.re_errno = errno;
		return -1;
	}
	args->sendsz = ((args->sendsz + 3) / 4) * 4;
	args->recvsz = ((args->recvsz + 3) / 4) * 4;
	args->cu = (struct cu_data *)mem_alloc(sizeof(*args->cu) +
	    args->sendsz + args->recvsz);
	if (args->cu == NULL) {
		rpc_createerr.cf_stat = RPC_SYSTEMERROR;
		rpc_createerr.cf_error.re_errno = errno;
		return -1;
	}
	args->cu->cu_outbuf = &args->cu->cu_inbuf[args->recvsz];
	args->cl->cl_ops = &udp_ops;
	args->cl->cl_private = (caddr_t)args->cu;
	args->cu->cu_connected = 0;
	args->cu->cu_rlen = sizeof (args->cu->cu_raddr);
	args->cu->cu_wait = args->wait;
	args->cu->cu_total.tv_sec = -1;
	args->cu->cu_total.tv_usec = -1;
	args->cu->cu_sendsz = args->sendsz;
	args->cu->cu_recvsz = args->recvsz;
	args->cu->cu_closeit = FALSE;
	args->call_msg.rm_xid = arc4random();
	args->call_msg.rm_direction = CALL;
	args->call_msg.rm_call.cb_rpcvers = RPC_MSG_VERSION;
	args->call_msg.rm_call.cb_prog = args->program;
	args->call_msg.rm_call.cb_vers = args->version;
	return 0;
}

int
clntudp_bufcreate2(struct clntudp_bufcreate_args *args)
{
	xdrmem_create(&(args->cu->cu_outxdrs), args->cu->cu_outbuf,
	    args->sendsz, XDR_ENCODE);
	if (!xdr_callhdr(&(args->cu->cu_outxdrs), &args->call_msg))
		return -1;
	args->cu->cu_xdrpos = XDR_GETPOS(&(args->cu->cu_outxdrs));
	args->cl->cl_auth = authnone_create();
	if (args->cl->cl_auth == NULL) {
		rpc_createerr.cf_stat = RPC_SYSTEMERROR;
		rpc_createerr.cf_error.re_errno = errno;
		return -1;
	}
	return 0;
}

/*
 * Create a UDP based client handle.
 * If *sockp<0, *sockp is set to a newly created UPD socket.  (***)
 * If raddr->sin_port is 0 a binder on the remote machine
 * is consulted for the correct port number.                  (***)
 * NB: It is the client's responsibility to close *sockp, unless
 *	clntudp_bufcreate() was called with *sockp = -1 (so it created
 *	the socket), and CLNT_DESTROY() is used.
 * NB: The rpch->cl_auth is initialized to null authentication.
 *     Caller may wish to set this something more useful.
 *
 * wait is the amount of time used between retransmitting a call if
 * no response has been heard;  retransmission occurs until the actual
 * rpc call times out.
 *
 * sendsz and recvsz are the maximum allowable packet sizes that can be
 * sent and received.
 *
 * This is a reduced-functionality version of clntudp_bufcreate() that
 * does not allocate socket or binding (***, above).
 * The official function clntudp_bufcreate(), which does perform those
 * two steps, is in clnt_udp_bufcreate.c.  This split avoids pulling
 * socket / portmap related code into programs only using getpwent / YP code.
 */

CLIENT *
clntudp_bufcreate_simple(struct sockaddr_in *raddr, u_long program, u_long version,
    struct timeval wait, int *sockp, u_int sendsz, u_int recvsz)
{
	struct clntudp_bufcreate_args args;

	args.raddr = raddr;
	args.program = program;
	args.version = version;
	args.wait = wait;
	args.sockp = sockp;
	args.sendsz = sendsz;
	args.recvsz = recvsz;
	args.cl = NULL;
	args.cu = NULL;

	if (clntudp_bufcreate1(&args) == -1)
		goto fooy;
	args.cu->cu_raddr = *raddr;
	args.cu->cu_sock = *sockp;
	if (clntudp_bufcreate2(&args) == -1)
		goto fooy;
	return (args.cl);
fooy:
	if (args.cu)
		mem_free((caddr_t)args.cu,
		    sizeof(*args.cu) + args.sendsz + args.recvsz);
	if (args.cl)
		mem_free((caddr_t)args.cl, sizeof(CLIENT));
	return (NULL);
}

static enum clnt_stat 
clntudp_call(CLIENT *cl,	/* client handle */
    u_long proc,		/* procedure number */
    xdrproc_t xargs,		/* xdr routine for args */
    caddr_t argsp,		/* pointer to args */
    xdrproc_t xresults,		/* xdr routine for results */
    caddr_t resultsp,		/* pointer to results */
    struct timeval utimeout)	/* seconds to wait before giving up */
{
	struct cu_data *cu = (struct cu_data *)cl->cl_private;
	XDR *xdrs;
	int outlen;
	int inlen;
	int ret;
	socklen_t fromlen;
	struct pollfd pfd[1];
	struct sockaddr_in from;
	struct rpc_msg reply_msg;
	XDR reply_xdrs;
	struct timespec time_waited, start, after, duration, wait;
	bool_t ok;
	int nrefreshes = 2;	/* number of times to refresh cred */
	struct timespec timeout;

	if (cu->cu_total.tv_usec == -1)
		TIMEVAL_TO_TIMESPEC(&utimeout, &timeout);     /* use supplied timeout */
	else
		TIMEVAL_TO_TIMESPEC(&cu->cu_total, &timeout); /* use default timeout */

	pfd[0].fd = cu->cu_sock;
	pfd[0].events = POLLIN;
	timespecclear(&time_waited);
	TIMEVAL_TO_TIMESPEC(&cu->cu_wait, &wait);
call_again:
	xdrs = &(cu->cu_outxdrs);
	xdrs->x_op = XDR_ENCODE;
	XDR_SETPOS(xdrs, cu->cu_xdrpos);
	/*
	 * the transaction is the first thing in the out buffer
	 */
	(*(u_short *)(cu->cu_outbuf))++;
	if (!XDR_PUTLONG(xdrs, (long *)&proc) ||
	    !AUTH_MARSHALL(cl->cl_auth, xdrs) ||
	    !(*xargs)(xdrs, argsp)) {
		return (cu->cu_error.re_status = RPC_CANTENCODEARGS);
	}
	outlen = (int)XDR_GETPOS(xdrs);

send_again:
	if (cu->cu_connected)
		ret = send(cu->cu_sock, cu->cu_outbuf, outlen, 0);
	else
		ret = sendto(cu->cu_sock, cu->cu_outbuf, outlen, 0,
		    (struct sockaddr *)&(cu->cu_raddr), cu->cu_rlen);
	if (ret != outlen) {
		cu->cu_error.re_errno = errno;
		return (cu->cu_error.re_status = RPC_CANTSEND);
	}

	/*
	 * Hack to provide rpc-based message passing
	 */
	if (!timespecisset(&timeout))
		return (cu->cu_error.re_status = RPC_TIMEDOUT);

	/*
	 * sub-optimal code appears here because we have
	 * some clock time to spare while the packets are in flight.
	 * (We assume that this is actually only executed once.)
	 */
	reply_msg.acpted_rply.ar_verf = _null_auth;
	reply_msg.acpted_rply.ar_results.where = resultsp;
	reply_msg.acpted_rply.ar_results.proc = xresults;

	WRAP(clock_gettime)(CLOCK_MONOTONIC, &start);
	for (;;) {
		switch (ppoll(pfd, 1, &wait, NULL)) {
		case 0:
			timespecadd(&time_waited, &wait, &time_waited);
			if (timespeccmp(&time_waited, &timeout, <))
				goto send_again;
			return (cu->cu_error.re_status = RPC_TIMEDOUT);
		case 1:
			if (pfd[0].revents & POLLNVAL)
				errno = EBADF;
			else if (pfd[0].revents & POLLERR)
				errno = EIO;
			else
				break;
			/* FALLTHROUGH */
		case -1:
			if (errno == EINTR) {
				WRAP(clock_gettime)(CLOCK_MONOTONIC, &after);
				timespecsub(&after, &start, &duration);
				timespecadd(&time_waited, &duration, &time_waited);
				if (timespeccmp(&time_waited, &timeout, <))
					continue;
				return (cu->cu_error.re_status = RPC_TIMEDOUT);
			}
			cu->cu_error.re_errno = errno;
			return (cu->cu_error.re_status = RPC_CANTRECV);
		}

		do {
			fromlen = sizeof(struct sockaddr);
			if (cu->cu_connected) {
				inlen = recv(cu->cu_sock, cu->cu_inbuf, 
				    (int) cu->cu_recvsz, 0);
			} else {

				inlen = recvfrom(cu->cu_sock, cu->cu_inbuf, 
				    (int) cu->cu_recvsz, 0,
				    (struct sockaddr *)&from, &fromlen);
			}
		} while (inlen == -1 && errno == EINTR);
		if (inlen == -1) {
			if (errno == EWOULDBLOCK)
				continue;
			cu->cu_error.re_errno = errno;
			return (cu->cu_error.re_status = RPC_CANTRECV);
		}
		if (inlen < sizeof(u_int32_t))
			continue;	
		/* see if reply transaction id matches sent id */
		if (((struct rpc_msg *)(cu->cu_inbuf))->rm_xid !=
		    ((struct rpc_msg *)(cu->cu_outbuf))->rm_xid)
			continue;
		/* we now assume we have the proper reply */
		break;
	}

	/*
	 * now decode and validate the response
	 */
	xdrmem_create(&reply_xdrs, cu->cu_inbuf, (u_int)inlen, XDR_DECODE);
	ok = xdr_replymsg(&reply_xdrs, &reply_msg);
	/* XDR_DESTROY(&reply_xdrs);  save a few cycles on noop destroy */
	if (ok) {
#if 0
		/*
		 * XXX Would like to check these, but call_msg is not
		 * around.
		 */
		if (reply_msg.rm_call.cb_prog != call_msg.rm_call.cb_prog ||
		    reply_msg.rm_call.cb_vers != call_msg.rm_call.cb_vers ||
		    reply_msg.rm_call.cb_proc != call_msg.rm_call.cb_proc) {
			goto call_again;	/* XXX spin? */
		}
#endif

		_seterr_reply(&reply_msg, &(cu->cu_error));
		if (cu->cu_error.re_status == RPC_SUCCESS) {
			if (!AUTH_VALIDATE(cl->cl_auth,
			    &reply_msg.acpted_rply.ar_verf)) {
				cu->cu_error.re_status = RPC_AUTHERROR;
				cu->cu_error.re_why = AUTH_INVALIDRESP;
			}
			if (reply_msg.acpted_rply.ar_verf.oa_base != NULL) {
				xdrs->x_op = XDR_FREE;
				(void)xdr_opaque_auth(xdrs,
				    &(reply_msg.acpted_rply.ar_verf));
			} 
		} else {
			/* maybe our credentials need to be refreshed ... */
			if (nrefreshes > 0 && AUTH_REFRESH(cl->cl_auth)) {
				nrefreshes--;
				goto call_again;
			}
		}
	} else {
		/* xdr_replymsg() may have left some things allocated */
		int op = reply_xdrs.x_op;
		reply_xdrs.x_op = XDR_FREE;
		xdr_replymsg(&reply_xdrs, &reply_msg);
		reply_xdrs.x_op = op;
		cu->cu_error.re_status = RPC_CANTDECODERES;
	}

	return (cu->cu_error.re_status);
}

static void
clntudp_geterr(CLIENT *cl, struct rpc_err *errp)
{
	struct cu_data *cu = (struct cu_data *)cl->cl_private;

	*errp = cu->cu_error;
}


static bool_t
clntudp_freeres(CLIENT *cl, xdrproc_t xdr_res, caddr_t res_ptr)
{
	struct cu_data *cu = (struct cu_data *)cl->cl_private;
	XDR *xdrs = &(cu->cu_outxdrs);

	xdrs->x_op = XDR_FREE;
	return ((*xdr_res)(xdrs, res_ptr));
}

static void 
clntudp_abort(CLIENT *clnt)
{
}

static bool_t
clntudp_control(CLIENT *cl, u_int request, void *info)
{
	struct cu_data *cu = (struct cu_data *)cl->cl_private;

	switch (request) {
	case CLSET_TIMEOUT:
		cu->cu_total = *(struct timeval *)info;
		break;
	case CLGET_TIMEOUT:
		*(struct timeval *)info = cu->cu_total;
		break;
	case CLSET_RETRY_TIMEOUT:
		cu->cu_wait = *(struct timeval *)info;
		break;
	case CLGET_RETRY_TIMEOUT:
		*(struct timeval *)info = cu->cu_wait;
		break;
	case CLGET_SERVER_ADDR:
		*(struct sockaddr_in *)info = cu->cu_raddr;
		break;
	case CLSET_CONNECTED:
		cu->cu_connected = *(int *)info;
		break;
	default:
		return (FALSE);
	}
	return (TRUE);
}
	
static void
clntudp_destroy(CLIENT *cl)
{
	struct cu_data *cu = (struct cu_data *)cl->cl_private;

	if (cu->cu_closeit && cu->cu_sock != -1) {
		(void)close(cu->cu_sock);
	}
	XDR_DESTROY(&(cu->cu_outxdrs));
	mem_free((caddr_t)cu, (sizeof(*cu) + cu->cu_sendsz + cu->cu_recvsz));
	mem_free((caddr_t)cl, sizeof(CLIENT));
}
