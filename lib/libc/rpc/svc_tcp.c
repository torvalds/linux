/*	$OpenBSD: svc_tcp.c,v 1.43 2022/12/27 17:10:06 jmc Exp $ */

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
 * svc_tcp.c, Server side for TCP/IP based RPC. 
 *
 * Actually implements two flavors of transporter -
 * a tcp rendezvouser (a listener and connection establisher)
 * and a record/tcp stream.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <rpc/rpc.h>
#include <sys/socket.h>
#include <errno.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>

/*
 * Ops vector for TCP/IP based rpc service handle
 */
static bool_t		svctcp_recv(SVCXPRT *xprt, struct rpc_msg *msg);
static enum xprt_stat	svctcp_stat(SVCXPRT *xprt);
static bool_t		svctcp_getargs(SVCXPRT *xprt, xdrproc_t xdr_args,
			    caddr_t args_ptr);
static bool_t		svctcp_reply(SVCXPRT *xprt, struct rpc_msg *msg);
static bool_t		svctcp_freeargs(SVCXPRT *xprt, xdrproc_t xdr_args,
			    caddr_t args_ptr);
static void		svctcp_destroy(SVCXPRT *xprt);

static const struct xp_ops svctcp_op = {
	svctcp_recv,
	svctcp_stat,
	svctcp_getargs,
	svctcp_reply,
	svctcp_freeargs,
	svctcp_destroy
};

/*
 * Ops vector for TCP/IP rendezvous handler
 */
static bool_t		rendezvous_request(SVCXPRT *xprt, struct rpc_msg *);
static enum xprt_stat	rendezvous_stat(SVCXPRT *xprt);

static const struct xp_ops svctcp_rendezvous_op = {
	rendezvous_request,
	rendezvous_stat,
	/* XXX abort illegal in library */
	(bool_t (*)(struct __rpc_svcxprt *, xdrproc_t, caddr_t))abort,
	(bool_t (*)(struct __rpc_svcxprt *, struct rpc_msg *))abort,
	(bool_t (*)(struct __rpc_svcxprt *, xdrproc_t, caddr_t))abort,
	svctcp_destroy
};

static int readtcp(SVCXPRT *xprt, caddr_t buf, int len),
    writetcp(SVCXPRT *xprt, caddr_t buf, int len);
static SVCXPRT *makefd_xprt(int fd, u_int sendsize, u_int recvsize);

struct tcp_rendezvous { /* kept in xprt->xp_p1 */
	u_int sendsize;
	u_int recvsize;
};

struct tcp_conn {  /* kept in xprt->xp_p1 */
	enum xprt_stat strm_stat;
	u_long x_id;
	XDR xdrs;
	char verf_body[MAX_AUTH_BYTES];
};

/*
 * Usage:
 *	xprt = svctcp_create(sock, send_buf_size, recv_buf_size);
 *
 * Creates, registers, and returns a (rpc) tcp based transporter.
 * Once *xprt is initialized, it is registered as a transporter
 * see (svc.h, xprt_register).  This routine returns
 * a NULL if a problem occurred.
 *
 * If sock<0 then a socket is created, else sock is used.
 * If the socket, sock is not bound to a port then svctcp_create
 * binds it to an arbitrary port.  The routine then starts a tcp
 * listener on the socket's associated port.  In any (successful) case,
 * xprt->xp_sock is the registered socket number and xprt->xp_port is the
 * associated port number.
 *
 * Since tcp streams do buffered io similar to stdio, the caller can specify
 * how big the send and receive buffers are via the second and third parms;
 * 0 => use the system default.
 */
SVCXPRT *
svctcp_create(int sock, u_int sendsize, u_int recvsize)
{
	bool_t madesock = FALSE;
	SVCXPRT *xprt;
	struct tcp_rendezvous *r;
	struct sockaddr_in addr;
	socklen_t len = sizeof(struct sockaddr_in);

	if (sock == RPC_ANYSOCK) {
		if ((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1)
			return (NULL);
		madesock = TRUE;
	}
	memset(&addr, 0, sizeof (addr));
	addr.sin_len = sizeof(struct sockaddr_in);
	addr.sin_family = AF_INET;
	if (bindresvport(sock, &addr) == -1) {
		addr.sin_port = 0;
		(void)bind(sock, (struct sockaddr *)&addr, len);
	}
	if ((getsockname(sock, (struct sockaddr *)&addr, &len) == -1)  ||
	    (listen(sock, 2) != 0)) {
		if (madesock)
			(void)close(sock);
		return (NULL);
	}
	r = (struct tcp_rendezvous *)mem_alloc(sizeof(*r));
	if (r == NULL) {
		if (madesock)
			(void)close(sock);
		return (NULL);
	}
	r->sendsize = sendsize;
	r->recvsize = recvsize;
	xprt = (SVCXPRT *)mem_alloc(sizeof(SVCXPRT));
	if (xprt == NULL) {
		if (madesock)
			(void)close(sock);
		free(r);
		return (NULL);
	}
	xprt->xp_p2 = NULL;
	xprt->xp_p1 = (caddr_t)r;
	xprt->xp_verf = _null_auth;
	xprt->xp_ops = &svctcp_rendezvous_op;
	xprt->xp_port = ntohs(addr.sin_port);
	xprt->xp_sock = sock;
	if (__xprt_register(xprt) == 0) {
		if (madesock)
			(void)close(sock);
		free(r);
		free(xprt);
		return (NULL);
	}
	return (xprt);
}
DEF_WEAK(svctcp_create);

/*
 * Like svtcp_create(), except the routine takes any *open* UNIX file
 * descriptor as its first input.
 */
SVCXPRT *
svcfd_create(int fd, u_int sendsize, u_int recvsize)
{

	return (makefd_xprt(fd, sendsize, recvsize));
}

static SVCXPRT *
makefd_xprt(int fd, u_int sendsize, u_int recvsize)
{
	SVCXPRT *xprt;
	struct tcp_conn *cd;
 
	xprt = (SVCXPRT *)mem_alloc(sizeof(SVCXPRT));
	if (xprt == NULL)
		goto done;
	cd = (struct tcp_conn *)mem_alloc(sizeof(struct tcp_conn));
	if (cd == NULL) {
		mem_free((char *) xprt, sizeof(SVCXPRT));
		xprt = NULL;
		goto done;
	}
	cd->strm_stat = XPRT_IDLE;
	xdrrec_create(&(cd->xdrs), sendsize, recvsize,
	    (caddr_t)xprt, (int(*)(caddr_t, caddr_t, int))readtcp,
	    (int(*)(caddr_t, caddr_t, int))writetcp);
	xprt->xp_p2 = NULL;
	xprt->xp_p1 = (caddr_t)cd;
	xprt->xp_verf.oa_base = cd->verf_body;
	xprt->xp_addrlen = 0;
	xprt->xp_ops = &svctcp_op;  /* truly deals with calls */
	xprt->xp_port = 0;  /* this is a connection, not a rendezvouser */
	xprt->xp_sock = fd;
	if (__xprt_register(xprt) == 0) {
		free(xprt);
		free(cd);
		return (NULL);
	}
    done:
	return (xprt);
}

static bool_t
rendezvous_request(SVCXPRT *xprt, struct rpc_msg *ignored)
{
	int sock;
	struct tcp_rendezvous *r;
	struct sockaddr_in addr;
	socklen_t len;

	r = (struct tcp_rendezvous *)xprt->xp_p1;
    again:
	len = sizeof(struct sockaddr_in);
	if ((sock = accept(xprt->xp_sock, (struct sockaddr *)&addr,
	    &len)) == -1) {
		if (errno == EINTR || errno == EWOULDBLOCK ||
		    errno == ECONNABORTED)
			goto again;
	       return (FALSE);
	}

#ifdef IP_OPTIONS
	{
		struct ipoption opts;
		socklen_t optsize = sizeof(opts);
		int i;

		if (getsockopt(sock, IPPROTO_IP, IP_OPTIONS,
		    (char *)&opts, &optsize) == 0 &&
		    optsize != 0) {
			for (i = 0; (char *)&opts.ipopt_list[i] - (char *)&opts <
			    optsize; ) {	
				u_char c = (u_char)opts.ipopt_list[i];
				if (c == IPOPT_LSRR || c == IPOPT_SSRR) {
					close(sock);
					return (FALSE);
				}
				if (c == IPOPT_EOL)
					break;
				i += (c == IPOPT_NOP) ? 1 :
				    (u_char)opts.ipopt_list[i+1];
			}
		}
	}
#endif

	/*
	 * XXX careful for ftp bounce attacks. If discovered, close the
	 * socket and look for another connection.
	 */
	if (addr.sin_port == htons(20)) {
		close(sock);
		return (FALSE);
	}

	/*
	 * make a new transporter (re-uses xprt)
	 */
	xprt = makefd_xprt(sock, r->sendsize, r->recvsize);
	xprt->xp_raddr = addr;
	xprt->xp_addrlen = len;
	return (FALSE); /* there is never an rpc msg to be processed */
}

static enum xprt_stat
rendezvous_stat(SVCXPRT *xprt)
{

	return (XPRT_IDLE);
}

static void
svctcp_destroy(SVCXPRT *xprt)
{
	struct tcp_conn *cd = (struct tcp_conn *)xprt->xp_p1;

	xprt_unregister(xprt);
	if (xprt->xp_sock != -1)
		(void)close(xprt->xp_sock);
	xprt->xp_sock = -1;
	if (xprt->xp_port != 0) {
		/* a rendezvouser socket */
		xprt->xp_port = 0;
	} else {
		/* an actual connection socket */
		XDR_DESTROY(&(cd->xdrs));
	}
	mem_free((caddr_t)cd, sizeof(struct tcp_conn));
	mem_free((caddr_t)xprt, sizeof(SVCXPRT));
}

/*
 * All read operations timeout after 35 seconds.
 * A timeout is fatal for the connection.
 */
static struct timespec wait_per_try = { 35, 0 };

/*
 * reads data from the tcp connection.
 * any error is fatal and the connection is closed.
 * (And a read of zero bytes is a half closed stream => error.)
 */
static int
readtcp(SVCXPRT *xprt, caddr_t buf, int len)
{
	int sock = xprt->xp_sock;
	int nready;
	struct timespec start, after, duration, delta;
	struct pollfd pfd[1];

	/*
	 * All read operations timeout after 35 seconds.
	 * A timeout is fatal for the connection.
	 */
	delta = wait_per_try;
	WRAP(clock_gettime)(CLOCK_MONOTONIC, &start);
	pfd[0].fd = sock;
	pfd[0].events = POLLIN;
	do {
		nready = ppoll(pfd, 1, &delta, NULL);
		switch (nready) {
		case -1:
			if (errno != EINTR)
				goto fatal_err;
			WRAP(clock_gettime)(CLOCK_MONOTONIC, &after);
			timespecsub(&after, &start, &duration);
			timespecsub(&wait_per_try, &duration, &delta);
			if (delta.tv_sec < 0 || !timespecisset(&delta))
				goto fatal_err;
			continue;
		case 0:
			goto fatal_err;
		}
	} while (pfd[0].revents == 0);
	if ((len = read(sock, buf, len)) > 0)
		return (len);
fatal_err:
	((struct tcp_conn *)(xprt->xp_p1))->strm_stat = XPRT_DIED;
	return (-1);
}

/*
 * writes data to the tcp connection.
 * Any error is fatal and the connection is closed.
 */
static int
writetcp(SVCXPRT *xprt, caddr_t buf, int len)
{
	int i, cnt;

	for (cnt = len; cnt > 0; cnt -= i, buf += i) {
		if ((i = write(xprt->xp_sock, buf, cnt)) == -1) {
			((struct tcp_conn *)(xprt->xp_p1))->strm_stat =
			    XPRT_DIED;
			return (-1);
		}
	}
	return (len);
}

static enum xprt_stat
svctcp_stat(SVCXPRT *xprt)
{
	struct tcp_conn *cd =
	    (struct tcp_conn *)(xprt->xp_p1);

	if (cd->strm_stat == XPRT_DIED)
		return (XPRT_DIED);
	if (! xdrrec_eof(&(cd->xdrs)))
		return (XPRT_MOREREQS);
	return (XPRT_IDLE);
}

static bool_t
svctcp_recv(SVCXPRT *xprt, struct rpc_msg *msg)
{
	struct tcp_conn *cd =
	    (struct tcp_conn *)(xprt->xp_p1);
	XDR *xdrs = &(cd->xdrs);

	xdrs->x_op = XDR_DECODE;
	(void)xdrrec_skiprecord(xdrs);
	if (xdr_callmsg(xdrs, msg)) {
		cd->x_id = msg->rm_xid;
		return (TRUE);
	}
	cd->strm_stat = XPRT_DIED;	/* XXX */
	return (FALSE);
}

static bool_t
svctcp_getargs(SVCXPRT *xprt, xdrproc_t xdr_args, caddr_t args_ptr)
{

	return ((*xdr_args)(&(((struct tcp_conn *)(xprt->xp_p1))->xdrs), args_ptr));
}

static bool_t
svctcp_freeargs(SVCXPRT *xprt, xdrproc_t xdr_args, caddr_t args_ptr)
{
	XDR *xdrs =
	    &(((struct tcp_conn *)(xprt->xp_p1))->xdrs);

	xdrs->x_op = XDR_FREE;
	return ((*xdr_args)(xdrs, args_ptr));
}

static bool_t
svctcp_reply(SVCXPRT *xprt, struct rpc_msg *msg)
{
	struct tcp_conn *cd =
	    (struct tcp_conn *)(xprt->xp_p1);
	XDR *xdrs = &(cd->xdrs);
	bool_t stat;

	xdrs->x_op = XDR_ENCODE;
	msg->rm_xid = cd->x_id;
	stat = xdr_replymsg(xdrs, msg);
	(void)xdrrec_endofrecord(xdrs, TRUE);
	return (stat);
}
