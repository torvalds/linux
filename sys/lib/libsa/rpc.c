/*	$OpenBSD: rpc.c,v 1.15 2015/08/15 19:42:56 miod Exp $	*/
/*	$NetBSD: rpc.c,v 1.16 1996/10/13 02:29:06 christos Exp $	*/

/*
 * Copyright (c) 1992 Regents of the University of California.
 * All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * @(#) Header: rpc.c,v 1.12 93/09/28 08:31:56 leres Exp  (LBL)
 */

/*
 * RPC functions used by NFS and bootparams.
 * Note that bootparams requires the ability to find out the
 * address of the server from which its response has come.
 * This is supported by keeping the IP/UDP headers in the
 * buffer space provided by the caller.  (See rpc_fromaddr)
 */

#include <sys/param.h>
#include <sys/socket.h>

#include <netinet/in.h>

#include <nfs/rpcv2.h>

#include "stand.h"
#include "net.h"
#include "netif.h"
#include "rpc.h"

struct auth_info {
	int32_t		authtype;	/* auth type */
	u_int32_t	authlen;	/* auth length */
};

struct auth_unix {
	int32_t   ua_time;
	int32_t   ua_hostname;	/* null */
	int32_t   ua_uid;
	int32_t   ua_gid;
	int32_t   ua_gidlist;	/* null */
};

struct rpc_call {
	u_int32_t	rp_xid;		/* request transaction id */
	int32_t		rp_direction;	/* call direction (0) */
	u_int32_t	rp_rpcvers;	/* rpc version (2) */
	u_int32_t	rp_prog;	/* program */
	u_int32_t	rp_vers;	/* version */
	u_int32_t	rp_proc;	/* procedure */
};

struct rpc_reply {
	u_int32_t	rp_xid;		/* request transaction id */
	int32_t		rp_direction;	/* call direction (1) */
	int32_t		rp_astatus;	/* accept status (0: accepted) */
	union {
		u_int32_t	rpu_errno;
		struct {
			struct auth_info rok_auth;
			u_int32_t	rok_status;
		} rpu_rok;
	} rp_u;
};

/* Local forwards */
static	ssize_t recvrpc(struct iodesc *, void *, size_t, time_t);
static	int rpc_getport(struct iodesc *, u_int32_t, u_int32_t);

int rpc_xid;
int rpc_port = 0x400;	/* predecrement */

/*
 * Make a rpc call; return length of answer
 * Note: Caller must leave room for headers.
 */
ssize_t
rpc_call(struct iodesc *d, u_int32_t prog, u_int32_t vers, u_int32_t proc, void *sdata,
    size_t slen, void *rdata, size_t rlen)
{
	ssize_t cc;
	struct auth_info *auth;
	struct rpc_call *call;
	struct rpc_reply *reply;
	char *send_head, *send_tail;
	char *recv_head, *recv_tail;
	u_int32_t x;
	int port;	/* host order */

#ifdef RPC_DEBUG
	if (debug)
		printf("rpc_call: prog=0x%x vers=%d proc=%d\n",
		    prog, vers, proc);
#endif

	port = rpc_getport(d, prog, vers);
	if (port == -1)
		return (-1);

	d->destport = htons(port);

	/*
	 * Prepend authorization stuff and headers.
	 * Note, must prepend things in reverse order.
	 */
	send_head = sdata;
	send_tail = (char *)sdata + slen;

	/* Auth verifier is always auth_null */
	send_head -= sizeof(*auth);
	auth = (struct auth_info *)send_head;
	auth->authtype = htonl(RPCAUTH_NULL);
	auth->authlen = 0;

#if 1
	/* Auth credentials: always auth unix (as root) */
	send_head -= sizeof(struct auth_unix);
	bzero(send_head, sizeof(struct auth_unix));
	send_head -= sizeof(*auth);
	auth = (struct auth_info *)send_head;
	auth->authtype = htonl(RPCAUTH_UNIX);
	auth->authlen = htonl(sizeof(struct auth_unix));
#else
	/* Auth credentials: always auth_null (XXX OK?) */
	send_head -= sizeof(*auth);
	auth = send_head;
	auth->authtype = htonl(RPCAUTH_NULL);
	auth->authlen = 0;
#endif

	/* RPC call structure. */
	send_head -= sizeof(*call);
	call = (struct rpc_call *)send_head;
	rpc_xid++;
	call->rp_xid       = htonl(rpc_xid);
	call->rp_direction = htonl(RPC_CALL);
	call->rp_rpcvers   = htonl(RPC_VER2);
	call->rp_prog = htonl(prog);
	call->rp_vers = htonl(vers);
	call->rp_proc = htonl(proc);

	/* Make room for the rpc_reply header. */
	recv_head = rdata;
	recv_tail = (char *)rdata + rlen;
	recv_head -= sizeof(*reply);

	cc = sendrecv(d,
	    sendudp, send_head, send_tail - send_head,
	    recvrpc, recv_head, recv_tail - recv_head);

#ifdef RPC_DEBUG
	if (debug)
		printf("callrpc: cc=%d rlen=%d\n", cc, rlen);
#endif
	if (cc <= -1)
		return (-1);

	if ((size_t)cc <= sizeof(*reply)) {
		errno = EBADRPC;
		return (-1);
	}

	recv_tail = recv_head + cc;

	/*
	 * Check the RPC reply status.
	 * The xid, dir, astatus were already checked.
	 */
	reply = (struct rpc_reply *)recv_head;
	auth = &reply->rp_u.rpu_rok.rok_auth;
	x = ntohl(auth->authlen);
	if (x != 0) {
#ifdef RPC_DEBUG
		if (debug)
			printf("callrpc: reply auth != NULL\n");
#endif
		errno = EBADRPC;
		return(-1);
	}
	x = ntohl(reply->rp_u.rpu_rok.rok_status);
	if (x != 0) {
		printf("callrpc: error = %d\n", x);
		errno = EBADRPC;
		return(-1);
	}
	recv_head += sizeof(*reply);

	return (ssize_t)(recv_tail - recv_head);
}

/*
 * Returns true if packet is the one we're waiting for.
 * This just checks the XID, direction, acceptance.
 * Remaining checks are done by callrpc
 */
static ssize_t
recvrpc(struct iodesc *d, void *pkt, size_t len, time_t tleft)
{
	struct rpc_reply *reply;
	ssize_t	n;
	int	x;

	errno = 0;
#ifdef RPC_DEBUG
	if (debug)
		printf("recvrpc: called len=%d\n", len);
#endif

	n = readudp(d, pkt, len, tleft);
	if (n <= (4 * 4))
		return -1;

	reply = (struct rpc_reply *)pkt;

	x = ntohl(reply->rp_xid);
	if (x != rpc_xid) {
#ifdef RPC_DEBUG
		if (debug)
			printf("recvrpc: rp_xid %d != xid %d\n", x, rpc_xid);
#endif
		return -1;
	}

	x = ntohl(reply->rp_direction);
	if (x != RPC_REPLY) {
#ifdef RPC_DEBUG
		if (debug)
			printf("recvrpc: rp_direction %d != REPLY\n", x);
#endif
		return -1;
	}

	x = ntohl(reply->rp_astatus);
	if (x != RPC_MSGACCEPTED) {
		errno = ntohl(reply->rp_u.rpu_errno);
		printf("recvrpc: reject, astat=%d, errno=%d\n", x, errno);
		return -1;
	}

	/* Return data count (thus indicating success) */
	return (n);
}

/*
 * Given a pointer to a reply just received,
 * dig out the IP address/port from the headers.
 */
void
rpc_fromaddr(void *pkt, struct in_addr *addr, u_short *port)
{
	struct hackhdr {
		/* Tail of IP header: just IP addresses */
		u_int32_t ip_src;
		u_int32_t ip_dst;
		/* UDP header: */
		u_int16_t uh_sport;		/* source port */
		u_int16_t uh_dport;		/* destination port */
		int16_t	  uh_ulen;		/* udp length */
		u_int16_t uh_sum;		/* udp checksum */
		/* RPC reply header: */
		struct rpc_reply rpc;
	} *hhdr;

	hhdr = ((struct hackhdr *)pkt) - 1;
	addr->s_addr = hhdr->ip_src;
	*port = hhdr->uh_sport;
}

/*
 * RPC Portmapper cache
 */
#define PMAP_NUM 8			/* need at most 5 pmap entries */

int rpc_pmap_num;
struct pmap_list {
	struct in_addr	addr;	/* server, net order */
	u_int	prog;		/* host order */
	u_int	vers;		/* host order */
	int	port;		/* host order */
} rpc_pmap_list[PMAP_NUM];

/* return port number in host order, or -1 */
int
rpc_pmap_getcache(struct in_addr addr, u_int prog, u_int vers)
{
	struct pmap_list *pl;

	for (pl = rpc_pmap_list; pl < &rpc_pmap_list[rpc_pmap_num]; pl++) {
		if (pl->addr.s_addr == addr.s_addr &&
		    pl->prog == prog && pl->vers == vers)
			return (pl->port);
	}
	return (-1);
}

void
rpc_pmap_putcache(struct in_addr addr, u_int prog, u_int vers, int port)
{
	struct pmap_list *pl;

	/* Don't overflow cache... */
	if (rpc_pmap_num >= PMAP_NUM) {
		/* ... just re-use the last entry. */
		rpc_pmap_num = PMAP_NUM - 1;
#ifdef	RPC_DEBUG
		printf("rpc_pmap_putcache: cache overflow\n");
#endif
	}

	pl = &rpc_pmap_list[rpc_pmap_num];
	rpc_pmap_num++;

	/* Cache answer */
	pl->addr = addr;
	pl->prog = prog;
	pl->vers = vers;
	pl->port = port;
}


/*
 * Request a port number from the port mapper.
 * Returns the port in host order.
 */
int
rpc_getport(struct iodesc *d, u_int32_t prog, u_int32_t vers)
{
	struct args {
		u_int32_t	prog;		/* call program */
		u_int32_t	vers;		/* call version */
		u_int32_t	proto;		/* call protocol */
		u_int32_t	port;		/* call port (unused) */
	} *args;
	struct res {
		u_int32_t port;
	} *res;
	struct {
		u_int32_t	h[RPC_HEADER_WORDS];
		struct args d;
	} sdata;
	struct {
		u_int32_t	h[RPC_HEADER_WORDS];
		struct res d;
		u_int32_t  pad;
	} rdata;
	ssize_t cc;
	int port;

#ifdef RPC_DEBUG
	if (debug)
		printf("getport: prog=0x%x vers=%d\n", prog, vers);
#endif

	/* This one is fixed forever. */
	if (prog == PMAPPROG)
		return (PMAPPORT);

	/* Try for cached answer first */
	port = rpc_pmap_getcache(d->destip, prog, vers);
	if (port != -1)
		return (port);

	args = &sdata.d;
	args->prog = htonl(prog);
	args->vers = htonl(vers);
	args->proto = htonl(IPPROTO_UDP);
	args->port = 0;
	res = &rdata.d;

	cc = rpc_call(d, PMAPPROG, PMAPVERS, PMAPPROC_GETPORT,
		args, sizeof(*args), res, sizeof(*res));
	if (cc < 0 || (size_t)cc < sizeof(*res)) {
		printf("getport: %s", strerror(errno));
		errno = EBADRPC;
		return (-1);
	}
	port = (int)ntohl(res->port);

	rpc_pmap_putcache(d->destip, prog, vers, port);

	return (port);
}
