/*	$OpenBSD: pmap_rmt.c,v 1.36 2020/12/30 18:56:35 benno Exp $ */

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
 * pmap_rmt.c
 * Client interface to pmap rpc service.
 * remote call and broadcast service
 */

#include <rpc/rpc.h>
#include <rpc/pmap_prot.h>
#include <rpc/pmap_clnt.h>
#include <rpc/pmap_rmt.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#define MAX_BROADCAST_SIZE 1400

static struct timeval timeout = { 3, 0 };


/*
 * pmapper remote-call-service interface.
 * This routine is used to call the pmapper remote call service
 * which will look up a service program in the port maps, and then
 * remotely call that routine with the given parameters.  This allows
 * programs to do a lookup and call in one step.
*/
enum clnt_stat
pmap_rmtcall(struct sockaddr_in *addr, u_long prog, u_long vers, u_long proc,
    xdrproc_t xdrargs, caddr_t argsp, xdrproc_t xdrres, caddr_t resp,
    struct timeval tout, u_long *port_ptr)
{
	int sock = -1;
	CLIENT *client;
	struct rmtcallargs a;
	struct rmtcallres r;
	enum clnt_stat stat;

	addr->sin_port = htons(PMAPPORT);
	client = clntudp_create(addr, PMAPPROG, PMAPVERS, timeout, &sock);
	if (client != NULL) {
		a.prog = prog;
		a.vers = vers;
		a.proc = proc;
		a.args_ptr = argsp;
		a.xdr_args = xdrargs;
		r.port_ptr = port_ptr;
		r.results_ptr = resp;
		r.xdr_results = xdrres;
		stat = CLNT_CALL(client, PMAPPROC_CALLIT, xdr_rmtcall_args, &a,
		    xdr_rmtcallres, &r, tout);
		CLNT_DESTROY(client);
	} else {
		stat = RPC_FAILED;
	}
	addr->sin_port = 0;
	return (stat);
}


/*
 * XDR remote call arguments
 * written for XDR_ENCODE direction only
 */
bool_t
xdr_rmtcall_args(XDR *xdrs, struct rmtcallargs *cap)
{
	u_int lenposition, argposition, position;

	if (xdr_u_long(xdrs, &(cap->prog)) &&
	    xdr_u_long(xdrs, &(cap->vers)) &&
	    xdr_u_long(xdrs, &(cap->proc))) {
		lenposition = XDR_GETPOS(xdrs);
		if (! xdr_u_long(xdrs, &(cap->arglen)))
		    return (FALSE);
		argposition = XDR_GETPOS(xdrs);
		if (! (*(cap->xdr_args))(xdrs, cap->args_ptr))
		    return (FALSE);
		position = XDR_GETPOS(xdrs);
		cap->arglen = (u_long)position - (u_long)argposition;
		XDR_SETPOS(xdrs, lenposition);
		if (! xdr_u_long(xdrs, &(cap->arglen)))
		    return (FALSE);
		XDR_SETPOS(xdrs, position);
		return (TRUE);
	}
	return (FALSE);
}
DEF_WEAK(xdr_rmtcall_args);

/*
 * XDR remote call results
 * written for XDR_DECODE direction only
 */
bool_t
xdr_rmtcallres(XDR *xdrs, struct rmtcallres *crp)
{
	caddr_t port_ptr;

	port_ptr = (caddr_t)crp->port_ptr;
	if (xdr_reference(xdrs, &port_ptr, sizeof (u_long),
	    xdr_u_long) && xdr_u_long(xdrs, &crp->resultslen)) {
		crp->port_ptr = (u_long *)port_ptr;
		return ((*(crp->xdr_results))(xdrs, crp->results_ptr));
	}
	return (FALSE);
}
DEF_WEAK(xdr_rmtcallres);


/*
 * The following is kludged-up support for simple rpc broadcasts.
 * Someday a large, complicated system will replace these trivial 
 * routines which only support udp/ip .
 */
static int
newgetbroadcastnets(struct in_addr **addrsp)
{
	struct ifaddrs *ifap, *ifa;
	struct sockaddr_in *sin;
	struct in_addr *addrs;
	int i = 0, n = 0;

	if (getifaddrs(&ifap) != 0)
		return 0;

	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL ||
		    ifa->ifa_addr->sa_family != AF_INET)
			continue;
		if ((ifa->ifa_flags & IFF_BROADCAST) &&
		    (ifa->ifa_flags & IFF_UP) &&
		    ifa->ifa_broadaddr &&
		    ifa->ifa_broadaddr->sa_family == AF_INET) {
			n++;
		}
	}

	addrs = calloc(n, sizeof(*addrs));
	if (addrs == NULL) {
		freeifaddrs(ifap);
		return 0;
	}

	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL ||
		    ifa->ifa_addr->sa_family != AF_INET)
			continue;
		if ((ifa->ifa_flags & IFF_BROADCAST) &&
		    (ifa->ifa_flags & IFF_UP) &&
		    ifa->ifa_broadaddr &&
		    ifa->ifa_broadaddr->sa_family == AF_INET) {
			sin = (struct sockaddr_in *)ifa->ifa_broadaddr;
			addrs[i++] = sin->sin_addr;
		}
	}

	freeifaddrs(ifap);
	*addrsp = addrs;
	return i;
}

typedef bool_t (*resultproc_t)(caddr_t, struct sockaddr_in *);

enum clnt_stat 
clnt_broadcast(u_long prog,	/* program number */
    u_long vers,		/* version number */
    u_long proc,		/* procedure number */
    xdrproc_t xargs,		/* xdr routine for args */
    caddr_t argsp,		/* pointer to args */
    xdrproc_t xresults,		/* xdr routine for results */
    caddr_t resultsp,		/* pointer to results */
    resultproc_t eachresult)	/* call with each result obtained */
{
	enum clnt_stat stat;
	AUTH *unix_auth;
	XDR xdr_stream;
	XDR *xdrs = &xdr_stream;
	int outlen, inlen, nets;
	socklen_t fromlen;
	int sock = -1;
	int on = 1;
	struct pollfd pfd[1];
	int i;
	int timo;
	bool_t done = FALSE;
	u_long xid;
	u_long port;
	struct in_addr *addrs = NULL;
	struct sockaddr_in baddr, raddr; /* broadcast and response addresses */
	struct rmtcallargs a;
	struct rmtcallres r;
	struct rpc_msg msg;
	char outbuf[MAX_BROADCAST_SIZE], inbuf[UDPMSGSIZE];

	if ((unix_auth = authunix_create_default()) == NULL) {
		stat = RPC_AUTHERROR;
		goto done_broad;
	}

	/*
	 * initialization: create a socket, a broadcast address, and
	 * preserialize the arguments into a send buffer.
	 */
	if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
		stat = RPC_CANTSEND;
		goto done_broad;
	}
#ifdef SO_BROADCAST
	if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &on, sizeof (on)) == -1) {
		stat = RPC_CANTSEND;
		goto done_broad;
	}
#endif /* def SO_BROADCAST */

	pfd[0].fd = sock;
	pfd[0].events = POLLIN;

	nets = newgetbroadcastnets(&addrs);
	if (nets == 0) {
		stat = RPC_CANTSEND;
		goto done_broad;
	}

	memset(&baddr, 0, sizeof (baddr));
	baddr.sin_len = sizeof(struct sockaddr_in);
	baddr.sin_family = AF_INET;
	baddr.sin_port = htons(PMAPPORT);
	baddr.sin_addr.s_addr = htonl(INADDR_ANY);
	msg.rm_xid = xid = arc4random();
	msg.rm_direction = CALL;
	msg.rm_call.cb_rpcvers = RPC_MSG_VERSION;
	msg.rm_call.cb_prog = PMAPPROG;
	msg.rm_call.cb_vers = PMAPVERS;
	msg.rm_call.cb_proc = PMAPPROC_CALLIT;
	msg.rm_call.cb_cred = unix_auth->ah_cred;
	msg.rm_call.cb_verf = unix_auth->ah_verf;
	a.prog = prog;
	a.vers = vers;
	a.proc = proc;
	a.xdr_args = xargs;
	a.args_ptr = argsp;
	r.port_ptr = &port;
	r.xdr_results = xresults;
	r.results_ptr = resultsp;
	xdrmem_create(xdrs, outbuf, MAX_BROADCAST_SIZE, XDR_ENCODE);
	if (!xdr_callmsg(xdrs, &msg) || !xdr_rmtcall_args(xdrs, &a)) {
		stat = RPC_CANTENCODEARGS;
		goto done_broad;
	}
	outlen = (int)xdr_getpos(xdrs);
	xdr_destroy(xdrs);

	/*
	 * Basic loop: broadcast a packet and wait a while for response(s).
	 * The response timeout grows larger per iteration.
	 *
	 * XXX This will loop about 5 times the stop. If there are
	 * lots of signals being received by the process it will quit
	 * send them all in one quick burst, not paying attention to
	 * the intended function of sending them slowly over half a
	 * minute or so
	 */
	for (timo = 4000; timo <= 14000; timo += 2000) {
		for (i = 0; i < nets; i++) {
			baddr.sin_addr = addrs[i];
			if (sendto(sock, outbuf, outlen, 0,
			    (struct sockaddr *)&baddr,
			    sizeof (struct sockaddr)) != outlen) {
				stat = RPC_CANTSEND;
				goto done_broad;
			}
		}
		if (eachresult == NULL) {
			stat = RPC_SUCCESS;
			goto done_broad;
		}
	recv_again:
		msg.acpted_rply.ar_verf = _null_auth;
		msg.acpted_rply.ar_results.where = (caddr_t)&r;
		msg.acpted_rply.ar_results.proc = xdr_rmtcallres;

		switch (poll(pfd, 1, timo)) {
		case 0:  /* timed out */
			stat = RPC_TIMEDOUT;
			continue;
		case 1:
			if (pfd[0].revents & POLLNVAL)
				errno = EBADF;
			else if (pfd[0].revents & POLLERR)
				errno = EIO;
			else
				break;
			/* FALLTHROUGH */
		case -1:  /* some kind of error */
			if (errno == EINTR)
				goto recv_again;
			stat = RPC_CANTRECV;
			goto done_broad;
		}
	try_again:
		fromlen = sizeof(struct sockaddr);
		inlen = recvfrom(sock, inbuf, UDPMSGSIZE, 0,
		    (struct sockaddr *)&raddr, &fromlen);
		if (inlen == -1) {
			if (errno == EINTR)
				goto try_again;
			stat = RPC_CANTRECV;
			goto done_broad;
		}
		if (inlen < sizeof(u_int32_t))
			goto recv_again;
		/*
		 * see if reply transaction id matches sent id.
		 * If so, decode the results.
		 */
		xdrmem_create(xdrs, inbuf, (u_int)inlen, XDR_DECODE);
		if (xdr_replymsg(xdrs, &msg)) {
			if ((msg.rm_xid == xid) &&
			    (msg.rm_reply.rp_stat == MSG_ACCEPTED) &&
			    (msg.acpted_rply.ar_stat == SUCCESS)) {
				raddr.sin_port = htons((u_short)port);
				done = (*eachresult)(resultsp, &raddr);
			}
			/* otherwise, we just ignore the errors ... */
		}
		xdrs->x_op = XDR_FREE;
		msg.acpted_rply.ar_results.proc = xdr_void;
		(void)xdr_replymsg(xdrs, &msg);
		(void)(*xresults)(xdrs, resultsp);
		xdr_destroy(xdrs);
		if (done) {
			stat = RPC_SUCCESS;
			goto done_broad;
		} else {
			goto recv_again;
		}
	}
done_broad:
	free(addrs);
	if (sock >= 0)
		(void)close(sock);
	if (unix_auth != NULL)
		AUTH_DESTROY(unix_auth);
	return (stat);
}
