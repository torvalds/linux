/*	$NetBSD: clnt_bcast.c,v 1.3 2000/07/06 03:05:20 christos Exp $	*/

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
#ident	"@(#)clnt_bcast.c	1.18	94/05/03 SMI"
static char sccsid[] = "@(#)clnt_bcast.c 1.15 89/04/21 Copyr 1988 Sun Micro";
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");


/*
 * clnt_bcast.c
 * Client interface to broadcast service.
 *
 * Copyright (C) 1988, Sun Microsystems, Inc.
 *
 * The following is kludged-up support for simple rpc broadcasts.
 * Someday a large, complicated system will replace these routines.
 */

#include "namespace.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <net/if.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <sys/poll.h>
#include <rpc/rpc.h>
#ifdef PORTMAP
#include <rpc/pmap_prot.h>
#include <rpc/pmap_clnt.h>
#include <rpc/pmap_rmt.h>
#endif				/* PORTMAP */
#include <rpc/nettype.h>
#include <arpa/inet.h>
#ifdef RPC_DEBUG
#include <stdio.h>
#endif
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <err.h>
#include <string.h>
#include "un-namespace.h"

#include "rpc_com.h"

#define	MAXBCAST 20	/* Max no of broadcasting transports */
#define	INITTIME 4000	/* Time to wait initially */
#define	WAITTIME 8000	/* Maximum time to wait */

/*
 * If nettype is NULL, it broadcasts on all the available
 * datagram_n transports. May potentially lead to broadacst storms
 * and hence should be used with caution, care and courage.
 *
 * The current parameter xdr packet size is limited by the max tsdu
 * size of the transport. If the max tsdu size of any transport is
 * smaller than the parameter xdr packet, then broadcast is not
 * sent on that transport.
 *
 * Also, the packet size should be less the packet size of
 * the data link layer (for ethernet it is 1400 bytes).  There is
 * no easy way to find out the max size of the data link layer and
 * we are assuming that the args would be smaller than that.
 *
 * The result size has to be smaller than the transport tsdu size.
 *
 * If PORTMAP has been defined, we send two packets for UDP, one for
 * rpcbind and one for portmap. For those machines which support
 * both rpcbind and portmap, it will cause them to reply twice, and
 * also here it will get two responses ... inefficient and clumsy.
 */

struct broadif {
	int index;
	struct sockaddr_storage broadaddr;
	TAILQ_ENTRY(broadif) link;
};

typedef TAILQ_HEAD(, broadif) broadlist_t;

int __rpc_getbroadifs(int, int, int, broadlist_t *);
void __rpc_freebroadifs(broadlist_t *);
int __rpc_broadenable(int, int, struct broadif *);

int __rpc_lowvers = 0;

int
__rpc_getbroadifs(int af, int proto, int socktype, broadlist_t *list)
{
	int count = 0;
	struct broadif *bip;
	struct ifaddrs *ifap, *ifp;
#ifdef INET6
	struct sockaddr_in6 *sin6;
#endif
	struct sockaddr_in *sin;
	struct addrinfo hints, *res;

	if (getifaddrs(&ifp) < 0)
		return 0;

	memset(&hints, 0, sizeof hints);

	hints.ai_family = af;
	hints.ai_protocol = proto;
	hints.ai_socktype = socktype;

	if (getaddrinfo(NULL, "sunrpc", &hints, &res) != 0) {
		freeifaddrs(ifp);
		return 0;
	}

	for (ifap = ifp; ifap != NULL; ifap = ifap->ifa_next) {
		if (ifap->ifa_addr->sa_family != af ||
		    !(ifap->ifa_flags & IFF_UP))
			continue;
		bip = (struct broadif *)malloc(sizeof *bip);
		if (bip == NULL)
			break;
		bip->index = if_nametoindex(ifap->ifa_name);
		if (
#ifdef INET6
		af != AF_INET6 &&
#endif
		(ifap->ifa_flags & IFF_BROADCAST) &&
		 ifap->ifa_broadaddr) {
			memcpy(&bip->broadaddr, ifap->ifa_broadaddr,
			    (size_t)ifap->ifa_broadaddr->sa_len);
			sin = (struct sockaddr_in *)(void *)&bip->broadaddr;
			sin->sin_port =
			    ((struct sockaddr_in *)
			    (void *)res->ai_addr)->sin_port;
		} else
#ifdef INET6
		if (af == AF_INET6 && (ifap->ifa_flags & IFF_MULTICAST)) {
			sin6 = (struct sockaddr_in6 *)(void *)&bip->broadaddr;
			inet_pton(af, RPCB_MULTICAST_ADDR, &sin6->sin6_addr);
			sin6->sin6_family = af;
			sin6->sin6_len = sizeof *sin6;
			sin6->sin6_port =
			    ((struct sockaddr_in6 *)
			    (void *)res->ai_addr)->sin6_port;
			sin6->sin6_scope_id = bip->index;
		} else
#endif
		{
			free(bip);
			continue;
		}
		TAILQ_INSERT_TAIL(list, bip, link);
		count++;
	}
	freeifaddrs(ifp);
	freeaddrinfo(res);

	return count;
}

void
__rpc_freebroadifs(broadlist_t *list)
{
	struct broadif *bip, *next;

	bip = TAILQ_FIRST(list);

	while (bip != NULL) {
		next = TAILQ_NEXT(bip, link);
		free(bip);
		bip = next;
	}
}

int
/*ARGSUSED*/
__rpc_broadenable(int af, int s, struct broadif *bip)
{
	int o = 1;

#if 0
	if (af == AF_INET6) {
		fprintf(stderr, "set v6 multicast if to %d\n", bip->index);
		if (_setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_IF, &bip->index,
		    sizeof bip->index) < 0)
			return -1;
	} else
#endif
		if (_setsockopt(s, SOL_SOCKET, SO_BROADCAST, &o, sizeof o) < 0)
			return -1;

	return 0;
}

/*
 * rpc_broadcast_exp()
 *
 * prog      - program number
 * vers      - version number
 * proc      - procedure number
 * xargs     - xdr routine for args
 * argsp     - pointer to args
 * xresults  - xdr routine for results
 * resultsp  - pointer to results
 * eachresult - call with each result obtained
 * inittime  - how long to wait initially
 * waittime  - maximum time to wait
 * nettype   - transport type
 */
enum clnt_stat
rpc_broadcast_exp(rpcprog_t prog, rpcvers_t vers, rpcproc_t proc,
    xdrproc_t xargs, caddr_t argsp, xdrproc_t xresults, caddr_t resultsp,
    resultproc_t eachresult, int inittime, int waittime,
    const char *nettype)
{
	enum clnt_stat	stat = RPC_SUCCESS; /* Return status */
	XDR 		xdr_stream; /* XDR stream */
	XDR 		*xdrs = &xdr_stream;
	struct rpc_msg	msg;	/* RPC message */
	struct timeval	t;
	char 		*outbuf = NULL;	/* Broadcast msg buffer */
	char		*inbuf = NULL; /* Reply buf */
	int		inlen;
	u_int 		maxbufsize = 0;
	AUTH 		*sys_auth = authunix_create_default();
	u_int		i;
	void		*handle;
	char		uaddress[1024];	/* A self imposed limit */
	char		*uaddrp = uaddress;
	int 		pmap_reply_flag; /* reply recvd from PORTMAP */
	/* An array of all the suitable broadcast transports */
	struct {
		int fd;		/* File descriptor */
		int af;
		int proto;
		struct netconfig *nconf; /* Netconfig structure */
		u_int asize;	/* Size of the addr buf */
		u_int dsize;	/* Size of the data buf */
		struct sockaddr_storage raddr; /* Remote address */
		broadlist_t nal;
	} fdlist[MAXBCAST];
	struct pollfd pfd[MAXBCAST];
	size_t fdlistno = 0;
	struct r_rpcb_rmtcallargs barg;	/* Remote arguments */
	struct r_rpcb_rmtcallres bres; /* Remote results */
	size_t outlen;
	struct netconfig *nconf;
	int msec;
	int pollretval;
	int fds_found;

#ifdef PORTMAP
	size_t outlen_pmap = 0;
	u_long port;		/* Remote port number */
	int pmap_flag = 0;	/* UDP exists ? */
	char *outbuf_pmap = NULL;
	struct rmtcallargs barg_pmap;	/* Remote arguments */
	struct rmtcallres bres_pmap; /* Remote results */
	u_int udpbufsz = 0;
#endif				/* PORTMAP */

	if (sys_auth == NULL) {
		return (RPC_SYSTEMERROR);
	}
	/*
	 * initialization: create a fd, a broadcast address, and send the
	 * request on the broadcast transport.
	 * Listen on all of them and on replies, call the user supplied
	 * function.
	 */

	if (nettype == NULL)
		nettype = "datagram_n";
	if ((handle = __rpc_setconf(nettype)) == NULL) {
		AUTH_DESTROY(sys_auth);
		return (RPC_UNKNOWNPROTO);
	}
	while ((nconf = __rpc_getconf(handle)) != NULL) {
		int fd;
		struct __rpc_sockinfo si;

		if (nconf->nc_semantics != NC_TPI_CLTS)
			continue;
		if (fdlistno >= MAXBCAST)
			break;	/* No more slots available */
		if (!__rpc_nconf2sockinfo(nconf, &si))
			continue;

		TAILQ_INIT(&fdlist[fdlistno].nal);
		if (__rpc_getbroadifs(si.si_af, si.si_proto, si.si_socktype, 
		    &fdlist[fdlistno].nal) == 0)
			continue;

		fd = _socket(si.si_af, si.si_socktype, si.si_proto);
		if (fd < 0) {
			stat = RPC_CANTSEND;
			continue;
		}
		fdlist[fdlistno].af = si.si_af;
		fdlist[fdlistno].proto = si.si_proto;
		fdlist[fdlistno].fd = fd;
		fdlist[fdlistno].nconf = nconf;
		fdlist[fdlistno].asize = __rpc_get_a_size(si.si_af);
		pfd[fdlistno].events = POLLIN | POLLPRI |
			POLLRDNORM | POLLRDBAND;
		pfd[fdlistno].fd = fdlist[fdlistno].fd = fd;
		fdlist[fdlistno].dsize = __rpc_get_t_size(si.si_af, si.si_proto,
							  0);

		if (maxbufsize <= fdlist[fdlistno].dsize)
			maxbufsize = fdlist[fdlistno].dsize;

#ifdef PORTMAP
		if (si.si_af == AF_INET && si.si_proto == IPPROTO_UDP) {
			udpbufsz = fdlist[fdlistno].dsize;
			outbuf_pmap = reallocf(outbuf_pmap, udpbufsz);
			if (outbuf_pmap == NULL) {
				_close(fd);
				stat = RPC_SYSTEMERROR;
				goto done_broad;
			}
			pmap_flag = 1;
		}
#endif				/* PORTMAP */
		fdlistno++;
	}

	if (fdlistno == 0) {
		if (stat == RPC_SUCCESS)
			stat = RPC_UNKNOWNPROTO;
		goto done_broad;
	}
	if (maxbufsize == 0) {
		if (stat == RPC_SUCCESS)
			stat = RPC_CANTSEND;
		goto done_broad;
	}
	inbuf = malloc(maxbufsize);
	outbuf = malloc(maxbufsize);
	if ((inbuf == NULL) || (outbuf == NULL)) {
		stat = RPC_SYSTEMERROR;
		goto done_broad;
	}

	/* Serialize all the arguments which have to be sent */
	(void) gettimeofday(&t, NULL);
	msg.rm_xid = __RPC_GETXID(&t);
	msg.rm_direction = CALL;
	msg.rm_call.cb_rpcvers = RPC_MSG_VERSION;
	msg.rm_call.cb_prog = RPCBPROG;
	msg.rm_call.cb_vers = RPCBVERS;
	msg.rm_call.cb_proc = RPCBPROC_CALLIT;
	barg.prog = prog;
	barg.vers = vers;
	barg.proc = proc;
	barg.args.args_val = argsp;
	barg.xdr_args = xargs;
	bres.addr = uaddrp;
	bres.results.results_val = resultsp;
	bres.xdr_res = xresults;
	msg.rm_call.cb_cred = sys_auth->ah_cred;
	msg.rm_call.cb_verf = sys_auth->ah_verf;
	xdrmem_create(xdrs, outbuf, maxbufsize, XDR_ENCODE);
	if ((!xdr_callmsg(xdrs, &msg)) ||
	    (!xdr_rpcb_rmtcallargs(xdrs,
	    (struct rpcb_rmtcallargs *)(void *)&barg))) {
		stat = RPC_CANTENCODEARGS;
		goto done_broad;
	}
	outlen = xdr_getpos(xdrs);
	xdr_destroy(xdrs);

#ifdef PORTMAP
	/* Prepare the packet for version 2 PORTMAP */
	if (pmap_flag) {
		msg.rm_xid++;	/* One way to distinguish */
		msg.rm_call.cb_prog = PMAPPROG;
		msg.rm_call.cb_vers = PMAPVERS;
		msg.rm_call.cb_proc = PMAPPROC_CALLIT;
		barg_pmap.prog = prog;
		barg_pmap.vers = vers;
		barg_pmap.proc = proc;
		barg_pmap.args_ptr = argsp;
		barg_pmap.xdr_args = xargs;
		bres_pmap.port_ptr = &port;
		bres_pmap.xdr_results = xresults;
		bres_pmap.results_ptr = resultsp;
		xdrmem_create(xdrs, outbuf_pmap, udpbufsz, XDR_ENCODE);
		if ((! xdr_callmsg(xdrs, &msg)) ||
		    (! xdr_rmtcall_args(xdrs, &barg_pmap))) {
			stat = RPC_CANTENCODEARGS;
			goto done_broad;
		}
		outlen_pmap = xdr_getpos(xdrs);
		xdr_destroy(xdrs);
	}
#endif				/* PORTMAP */

	/*
	 * Basic loop: broadcast the packets to transports which
	 * support data packets of size such that one can encode
	 * all the arguments.
	 * Wait a while for response(s).
	 * The response timeout grows larger per iteration.
	 */
	for (msec = inittime; msec <= waittime; msec += msec) {
		struct broadif *bip;

		/* Broadcast all the packets now */
		for (i = 0; i < fdlistno; i++) {
			if (fdlist[i].dsize < outlen) {
				stat = RPC_CANTSEND;
				continue;
			}
			for (bip = TAILQ_FIRST(&fdlist[i].nal); bip != NULL;
			     bip = TAILQ_NEXT(bip, link)) {
				void *addr;

				addr = &bip->broadaddr;

				__rpc_broadenable(fdlist[i].af, fdlist[i].fd,
				    bip);

				/*
				 * Only use version 3 if lowvers is not set
				 */

				if (!__rpc_lowvers)
					if (_sendto(fdlist[i].fd, outbuf,
					    outlen, 0, (struct sockaddr*)addr,
					    (size_t)fdlist[i].asize) !=
					    outlen) {
#ifdef RPC_DEBUG
						perror("sendto");
#endif
						warnx("clnt_bcast: cannot send "
						      "broadcast packet");
						stat = RPC_CANTSEND;
						continue;
					}
#ifdef RPC_DEBUG
				if (!__rpc_lowvers)
					fprintf(stderr, "Broadcast packet sent "
						"for %s\n",
						 fdlist[i].nconf->nc_netid);
#endif
#ifdef PORTMAP
				/*
				 * Send the version 2 packet also
				 * for UDP/IP
				 */
				if (pmap_flag &&
				    fdlist[i].proto == IPPROTO_UDP) {
					if (_sendto(fdlist[i].fd, outbuf_pmap,
					    outlen_pmap, 0, addr,
					    (size_t)fdlist[i].asize) !=
						outlen_pmap) {
						warnx("clnt_bcast: "
						    "Cannot send broadcast packet");
						stat = RPC_CANTSEND;
						continue;
					}
				}
#ifdef RPC_DEBUG
				fprintf(stderr, "PMAP Broadcast packet "
					"sent for %s\n",
					fdlist[i].nconf->nc_netid);
#endif
#endif				/* PORTMAP */
			}
			/* End for sending all packets on this transport */
		}	/* End for sending on all transports */

		if (eachresult == NULL) {
			stat = RPC_SUCCESS;
			goto done_broad;
		}

		/*
		 * Get all the replies from these broadcast requests
		 */
	recv_again:

		switch (pollretval = _poll(pfd, fdlistno, msec)) {
		case 0:		/* timed out */
			stat = RPC_TIMEDOUT;
			continue;
		case -1:	/* some kind of error - we ignore it */
			goto recv_again;
		}		/* end of poll results switch */

		for (i = fds_found = 0;
		     i < fdlistno && fds_found < pollretval; i++) {
			bool_t done = FALSE;

			if (pfd[i].revents == 0)
				continue;
			else if (pfd[i].revents & POLLNVAL) {
				/*
				 * Something bad has happened to this descri-
				 * ptor. We can cause _poll() to ignore
				 * it simply by using a negative fd.  We do that
				 * rather than compacting the pfd[] and fdlist[]
				 * arrays.
				 */
				pfd[i].fd = -1;
				fds_found++;
				continue;
			} else
				fds_found++;
#ifdef RPC_DEBUG
			fprintf(stderr, "response for %s\n",
				fdlist[i].nconf->nc_netid);
#endif
		try_again:
			inlen = _recvfrom(fdlist[i].fd, inbuf, fdlist[i].dsize,
			    0, (struct sockaddr *)(void *)&fdlist[i].raddr,
			    &fdlist[i].asize);
			if (inlen < 0) {
				if (errno == EINTR)
					goto try_again;
				warnx("clnt_bcast: Cannot receive reply to "
					"broadcast");
				stat = RPC_CANTRECV;
				continue;
			}
			if (inlen < sizeof (u_int32_t))
				continue; /* Drop that and go ahead */
			/*
			 * see if reply transaction id matches sent id.
			 * If so, decode the results. If return id is xid + 1
			 * it was a PORTMAP reply
			 */
			if (*((u_int32_t *)(void *)(inbuf)) ==
			    *((u_int32_t *)(void *)(outbuf))) {
				pmap_reply_flag = 0;
				msg.acpted_rply.ar_verf = _null_auth;
				msg.acpted_rply.ar_results.where =
					(caddr_t)(void *)&bres;
				msg.acpted_rply.ar_results.proc =
					(xdrproc_t)xdr_rpcb_rmtcallres;
#ifdef PORTMAP
			} else if (pmap_flag &&
				*((u_int32_t *)(void *)(inbuf)) ==
				*((u_int32_t *)(void *)(outbuf_pmap))) {
				pmap_reply_flag = 1;
				msg.acpted_rply.ar_verf = _null_auth;
				msg.acpted_rply.ar_results.where =
					(caddr_t)(void *)&bres_pmap;
				msg.acpted_rply.ar_results.proc =
					(xdrproc_t)xdr_rmtcallres;
#endif				/* PORTMAP */
			} else
				continue;
			xdrmem_create(xdrs, inbuf, (u_int)inlen, XDR_DECODE);
			if (xdr_replymsg(xdrs, &msg)) {
				if ((msg.rm_reply.rp_stat == MSG_ACCEPTED) &&
				    (msg.acpted_rply.ar_stat == SUCCESS)) {
					struct netbuf taddr, *np;
					struct sockaddr_in *sin;

#ifdef PORTMAP
					if (pmap_flag && pmap_reply_flag) {
						sin = (struct sockaddr_in *)
						    (void *)&fdlist[i].raddr;
						sin->sin_port =
						    htons((u_short)port);
						taddr.len = taddr.maxlen = 
						    fdlist[i].raddr.ss_len;
						taddr.buf = &fdlist[i].raddr;
						done = (*eachresult)(resultsp,
						    &taddr, fdlist[i].nconf);
					} else {
#endif				/* PORTMAP */
#ifdef RPC_DEBUG
						fprintf(stderr, "uaddr %s\n",
						    uaddrp);
#endif
						np = uaddr2taddr(
						    fdlist[i].nconf, uaddrp);
						done = (*eachresult)(resultsp,
						    np, fdlist[i].nconf);
						free(np);
#ifdef PORTMAP
					}
#endif				/* PORTMAP */
				}
				/* otherwise, we just ignore the errors ... */
			}
			/* else some kind of deserialization problem ... */

			xdrs->x_op = XDR_FREE;
			msg.acpted_rply.ar_results.proc = (xdrproc_t) xdr_void;
			(void) xdr_replymsg(xdrs, &msg);
			(void) (*xresults)(xdrs, resultsp);
			XDR_DESTROY(xdrs);
			if (done) {
				stat = RPC_SUCCESS;
				goto done_broad;
			} else {
				goto recv_again;
			}
		}		/* The recv for loop */
	}			/* The giant for loop */

done_broad:
	free(inbuf);
	free(outbuf);
#ifdef PORTMAP
	free(outbuf_pmap);
#endif				/* PORTMAP */
	for (i = 0; i < fdlistno; i++) {
		(void)_close(fdlist[i].fd);
		__rpc_freebroadifs(&fdlist[i].nal);
	}
	AUTH_DESTROY(sys_auth);
	(void) __rpc_endconf(handle);

	return (stat);
}

/*
 * rpc_broadcast()
 *
 * prog       - program number 
 * vers       - version number 
 * proc       - procedure number 
 * xargs      - xdr routine for args 
 * argsp      - pointer to args 
 * xresults   - xdr routine for results 
 * resultsp   - pointer to results 
 * eachresult - call with each result obtained 
 * nettype    - transport type 
 */
enum clnt_stat
rpc_broadcast(rpcprog_t prog, rpcvers_t vers, rpcproc_t proc, xdrproc_t xargs,
    caddr_t argsp, xdrproc_t xresults, caddr_t resultsp,
    resultproc_t eachresult, const char *nettype)
{
	enum clnt_stat	dummy;

	dummy = rpc_broadcast_exp(prog, vers, proc, xargs, argsp,
		xresults, resultsp, eachresult,
		INITTIME, WAITTIME, nettype);
	return (dummy);
}
