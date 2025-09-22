/*	$OpenBSD: clnt_tcp.c,v 1.36 2022/07/15 17:33:28 deraadt Exp $ */

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
 * clnt_tcp.c, Implements a TCP/IP based, client side RPC.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <rpc/rpc.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <rpc/pmap_clnt.h>

#define MCALL_MSG_SIZE 24

static enum clnt_stat	clnttcp_call(CLIENT *, u_long, xdrproc_t, caddr_t,
			    xdrproc_t, caddr_t, struct timeval);
static void		clnttcp_abort(CLIENT *);
static void		clnttcp_geterr(CLIENT *, struct rpc_err *);
static bool_t		clnttcp_freeres(CLIENT *, xdrproc_t, caddr_t);
static bool_t           clnttcp_control(CLIENT *, u_int, void *);
static void		clnttcp_destroy(CLIENT *);

static const struct clnt_ops tcp_ops = {
	clnttcp_call,
	clnttcp_abort,
	clnttcp_geterr,
	clnttcp_freeres,
	clnttcp_destroy,
	clnttcp_control
};

struct ct_data {
	int		ct_sock;
	bool_t		ct_closeit;
	int		ct_connected;	/* pre-connected */
	struct timeval	ct_wait;
	bool_t          ct_waitset;       /* wait set by clnt_control? */
	struct sockaddr_in ct_addr; 
	struct rpc_err	ct_error;
	char		ct_mcall[MCALL_MSG_SIZE];	/* marshalled callmsg */
	u_int		ct_mpos;			/* pos after marshal */
	XDR		ct_xdrs;
};

static int	readtcp(struct ct_data *, caddr_t, int);
static int	writetcp(struct ct_data *, caddr_t, int);

/*
 * Create a client handle for a tcp/ip connection.
 * If *sockp<0, *sockp is set to a newly created TCP socket and it is
 * connected to raddr.  If *sockp non-negative then
 * raddr is ignored.  The rpc/tcp package does buffering
 * similar to stdio, so the client must pick send and receive buffer sizes,];
 * 0 => use the default.
 * If raddr->sin_port is 0, then a binder on the remote machine is
 * consulted for the right port number.
 * NB: *sockp is copied into a private area.
 * NB: It is the client's responsibility to close *sockp, unless
 *     clnttcp_create() was called with *sockp = -1 (so it created
 *     the socket), and CLNT_DESTROY() is used.
 * NB: The rpch->cl_auth is set null authentication.  Caller may wish to set this
 * something more useful.
 */
CLIENT *
clnttcp_create(struct sockaddr_in *raddr, u_long prog, u_long vers, int *sockp,
    u_int sendsz, u_int recvsz)
{
	CLIENT *h;
	struct ct_data *ct = NULL;
	struct rpc_msg call_msg;

	h  = (CLIENT *)mem_alloc(sizeof(*h));
	if (h == NULL) {
		rpc_createerr.cf_stat = RPC_SYSTEMERROR;
		rpc_createerr.cf_error.re_errno = errno;
		goto fooy;
	}
	ct = (struct ct_data *)mem_alloc(sizeof(*ct));
	if (ct == NULL) {
		rpc_createerr.cf_stat = RPC_SYSTEMERROR;
		rpc_createerr.cf_error.re_errno = errno;
		goto fooy;
	}

	/*
	 * If no port number given ask the pmap for one
	 */
	if (raddr->sin_port == 0) {
		u_short port;
		if ((port = pmap_getport(raddr, prog, vers, IPPROTO_TCP)) == 0) {
			mem_free((caddr_t)ct, sizeof(struct ct_data));
			mem_free((caddr_t)h, sizeof(CLIENT));
			return (NULL);
		}
		raddr->sin_port = htons(port);
	}

	/*
	 * If no socket given, open one
	 */
	if (*sockp < 0) {
		*sockp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		(void)bindresvport(*sockp, NULL);
		if ((*sockp == -1)
		    || (connect(*sockp, (struct sockaddr *)raddr,
		    sizeof(*raddr)) == -1)) {
			rpc_createerr.cf_stat = RPC_SYSTEMERROR;
			rpc_createerr.cf_error.re_errno = errno;
			if (*sockp != -1)
				(void)close(*sockp);
			goto fooy;
		}
		ct->ct_closeit = TRUE;
	} else {
		ct->ct_closeit = FALSE;
	}

	/*
	 * Set up private data struct
	 */
	ct->ct_sock = *sockp;
	ct->ct_wait.tv_usec = 0;
	ct->ct_waitset = FALSE;
	ct->ct_addr = *raddr;

	/*
	 * Initialize call message
	 */
	call_msg.rm_xid = arc4random();
	call_msg.rm_direction = CALL;
	call_msg.rm_call.cb_rpcvers = RPC_MSG_VERSION;
	call_msg.rm_call.cb_prog = prog;
	call_msg.rm_call.cb_vers = vers;

	/*
	 * pre-serialize the static part of the call msg and stash it away
	 */
	xdrmem_create(&(ct->ct_xdrs), ct->ct_mcall, MCALL_MSG_SIZE,
	    XDR_ENCODE);
	if (! xdr_callhdr(&(ct->ct_xdrs), &call_msg)) {
		if (ct->ct_closeit) {
			(void)close(*sockp);
		}
		goto fooy;
	}
	ct->ct_mpos = XDR_GETPOS(&(ct->ct_xdrs));
	XDR_DESTROY(&(ct->ct_xdrs));

	/*
	 * Create a client handle which uses xdrrec for serialization
	 * and authnone for authentication.
	 */
	xdrrec_create(&(ct->ct_xdrs), sendsz, recvsz,
	    (caddr_t)ct, (int(*)(caddr_t, caddr_t, int))readtcp,
	    (int(*)(caddr_t, caddr_t, int))writetcp);
	h->cl_ops = &tcp_ops;
	h->cl_private = (caddr_t) ct;
	h->cl_auth = authnone_create();
	if (h->cl_auth == NULL) {
		rpc_createerr.cf_stat = RPC_SYSTEMERROR;
		rpc_createerr.cf_error.re_errno = errno;
		goto fooy;
	}
	return (h);

fooy:
	/*
	 * Something goofed, free stuff and barf
	 */
	if (ct)
		mem_free((caddr_t)ct, sizeof(struct ct_data));
	if (h)
		mem_free((caddr_t)h, sizeof(CLIENT));
	return (NULL);
}
DEF_WEAK(clnttcp_create);

static enum clnt_stat
clnttcp_call(CLIENT *h, u_long proc, xdrproc_t xdr_args, caddr_t args_ptr,
    xdrproc_t xdr_results, caddr_t results_ptr, struct timeval timeout)
{
	struct ct_data *ct = (struct ct_data *) h->cl_private;
	XDR *xdrs = &(ct->ct_xdrs);
	struct rpc_msg reply_msg;
	u_long x_id;
	u_int32_t *msg_x_id = (u_int32_t *)(ct->ct_mcall);	/* yuk */
	bool_t shipnow;
	int refreshes = 2;

	if (!ct->ct_waitset) {
		ct->ct_wait = timeout;
	}

	shipnow =
	    (xdr_results == NULL && timeout.tv_sec == 0
	    && timeout.tv_usec == 0) ? FALSE : TRUE;

call_again:
	xdrs->x_op = XDR_ENCODE;
	ct->ct_error.re_status = RPC_SUCCESS;
	x_id = ntohl(--(*msg_x_id));
	if ((! XDR_PUTBYTES(xdrs, ct->ct_mcall, ct->ct_mpos)) ||
	    (! XDR_PUTLONG(xdrs, (long *)&proc)) ||
	    (! AUTH_MARSHALL(h->cl_auth, xdrs)) ||
	    (! (*xdr_args)(xdrs, args_ptr))) {
		if (ct->ct_error.re_status == RPC_SUCCESS)
			ct->ct_error.re_status = RPC_CANTENCODEARGS;
		(void)xdrrec_endofrecord(xdrs, TRUE);
		return (ct->ct_error.re_status);
	}
	if (! xdrrec_endofrecord(xdrs, shipnow))
		return (ct->ct_error.re_status = RPC_CANTSEND);
	if (! shipnow)
		return (RPC_SUCCESS);
	/*
	 * Hack to provide rpc-based message passing
	 */
	if (timeout.tv_sec == 0 && timeout.tv_usec == 0) {
		return(ct->ct_error.re_status = RPC_TIMEDOUT);
	}


	/*
	 * Keep receiving until we get a valid transaction id
	 */
	xdrs->x_op = XDR_DECODE;
	while (TRUE) {
		reply_msg.acpted_rply.ar_verf = _null_auth;
		reply_msg.acpted_rply.ar_results.where = NULL;
		reply_msg.acpted_rply.ar_results.proc = xdr_void;
		if (! xdrrec_skiprecord(xdrs))
			return (ct->ct_error.re_status);
		/* now decode and validate the response header */
		if (! xdr_replymsg(xdrs, &reply_msg)) {
			if (ct->ct_error.re_status == RPC_SUCCESS)
				continue;
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
		if (! AUTH_VALIDATE(h->cl_auth, &reply_msg.acpted_rply.ar_verf)) {
			ct->ct_error.re_status = RPC_AUTHERROR;
			ct->ct_error.re_why = AUTH_INVALIDRESP;
		} else if (! (*xdr_results)(xdrs, results_ptr)) {
			if (ct->ct_error.re_status == RPC_SUCCESS)
				ct->ct_error.re_status = RPC_CANTDECODERES;
		}
		/* free verifier ... */
		if (reply_msg.acpted_rply.ar_verf.oa_base != NULL) {
			xdrs->x_op = XDR_FREE;
			(void)xdr_opaque_auth(xdrs, &(reply_msg.acpted_rply.ar_verf));
		}
	}  /* end successful completion */
	else {
		/* maybe our credentials need to be refreshed ... */
		if (refreshes-- && AUTH_REFRESH(h->cl_auth))
			goto call_again;
	}  /* end of unsuccessful completion */
	return (ct->ct_error.re_status);
}

static void
clnttcp_geterr(CLIENT *h, struct rpc_err *errp)
{
	struct ct_data *ct =
	    (struct ct_data *) h->cl_private;

	*errp = ct->ct_error;
}

static bool_t
clnttcp_freeres(CLIENT *cl, xdrproc_t xdr_res, caddr_t res_ptr)
{
	struct ct_data *ct = (struct ct_data *)cl->cl_private;
	XDR *xdrs = &(ct->ct_xdrs);

	xdrs->x_op = XDR_FREE;
	return ((*xdr_res)(xdrs, res_ptr));
}

static void
clnttcp_abort(CLIENT *clnt)
{
}

static bool_t
clnttcp_control(CLIENT *cl, u_int request, void *info)
{
	struct ct_data *ct = (struct ct_data *)cl->cl_private;

	switch (request) {
	case CLSET_TIMEOUT:
		ct->ct_wait = *(struct timeval *)info;
		ct->ct_waitset = TRUE;
		break;
	case CLGET_TIMEOUT:
		*(struct timeval *)info = ct->ct_wait;
		break;
	case CLGET_SERVER_ADDR:
		*(struct sockaddr_in *)info = ct->ct_addr;
		break;
	case CLSET_CONNECTED:
		ct->ct_connected = *(int *)info;
		break;
	default:
		return (FALSE);
	}
	return (TRUE);
}


static void
clnttcp_destroy(CLIENT *h)
{
	struct ct_data *ct =
	    (struct ct_data *) h->cl_private;

	if (ct->ct_closeit && ct->ct_sock != -1) {
		(void)close(ct->ct_sock);
	}
	XDR_DESTROY(&(ct->ct_xdrs));
	mem_free((caddr_t)ct, sizeof(struct ct_data));
	mem_free((caddr_t)h, sizeof(CLIENT));
}

/*
 * Interface between xdr serializer and tcp connection.
 * Behaves like the system calls, read & write, but keeps some error state
 * around for the rpc level.
 */
static int
readtcp(struct ct_data *ct, caddr_t buf, int len)
{
	struct pollfd pfd[1];
	struct timespec start, after, duration, delta, wait;
	int r, save_errno;

	if (len == 0)
		return (0);

	pfd[0].fd = ct->ct_sock;
	pfd[0].events = POLLIN;
	TIMEVAL_TO_TIMESPEC(&ct->ct_wait, &wait);
	delta = wait;
	WRAP(clock_gettime)(CLOCK_MONOTONIC, &start);
	for (;;) {
		r = ppoll(pfd, 1, &delta, NULL);
		save_errno = errno;

		WRAP(clock_gettime)(CLOCK_MONOTONIC, &after);
		timespecsub(&start, &after, &duration);
		timespecsub(&wait, &duration, &delta);
		if (delta.tv_sec < 0 || !timespecisset(&delta))
			r = 0;

		switch (r) {
		case 0:
			ct->ct_error.re_status = RPC_TIMEDOUT;
			return (-1);
		case 1:
			if (pfd[0].revents & POLLNVAL)
				errno = EBADF;
			else if (pfd[0].revents & POLLERR)
				errno = EIO;
			else
				break;
			/* FALLTHROUGH */
		case -1:
			if (errno == EINTR)
				continue;
			ct->ct_error.re_status = RPC_CANTRECV;
			ct->ct_error.re_errno = save_errno;
			return (-1);
		}
		break;
	}

	switch (len = read(ct->ct_sock, buf, len)) {
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
writetcp(struct ct_data *ct, caddr_t buf, int len)
{
	int i, cnt;

	for (cnt = len; cnt > 0; cnt -= i, buf += i) {
		if ((i = write(ct->ct_sock, buf, cnt)) == -1) {
			ct->ct_error.re_errno = errno;
			ct->ct_error.re_status = RPC_CANTSEND;
			return (-1);
		}
	}
	return (len);
}
