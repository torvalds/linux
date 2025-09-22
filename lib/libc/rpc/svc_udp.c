/*	$OpenBSD: svc_udp.c,v 1.27 2022/02/14 03:38:59 guenther Exp $ */

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
 * svc_udp.c,
 * Server side for UDP/IP based RPC.  (Does some caching in the hopes of
 * achieving execute-at-most-once semantics.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rpc/rpc.h>
#include <sys/socket.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>


#define rpc_buffer(xprt) ((xprt)->xp_p1)
#define MAX(a, b)     ((a > b) ? a : b)

static bool_t		svcudp_recv(SVCXPRT *, struct rpc_msg *);
static enum xprt_stat	svcudp_stat(SVCXPRT *);
static bool_t		svcudp_getargs(SVCXPRT *, xdrproc_t, caddr_t);
static bool_t		svcudp_reply(SVCXPRT *, struct rpc_msg *);
static bool_t		svcudp_freeargs(SVCXPRT *, xdrproc_t, caddr_t);
static void		svcudp_destroy(SVCXPRT *);
static void		cache_set(SVCXPRT *, u_long);
static int		cache_get(SVCXPRT *, struct rpc_msg *, char **,
			    u_long *);

static const struct xp_ops svcudp_op = {
	svcudp_recv,
	svcudp_stat,
	svcudp_getargs,
	svcudp_reply,
	svcudp_freeargs,
	svcudp_destroy
};

/*
 * kept in xprt->xp_p2
 */
struct svcudp_data {
	u_int   su_iosz;	/* byte size of send.recv buffer */
	u_long	su_xid;		/* transaction id */
	XDR	su_xdrs;	/* XDR handle */
	char	su_verfbody[MAX_AUTH_BYTES];	/* verifier body */
	char * 	su_cache;	/* cached data, NULL if no cache */
};
#define	su_data(xprt)	((struct svcudp_data *)(xprt->xp_p2))

/*
 * Usage:
 *	xprt = svcudp_create(sock);
 *
 * If sock<0 then a socket is created, else sock is used.
 * If the socket, sock is not bound to a port then svcudp_create
 * binds it to an arbitrary port.  In any (successful) case,
 * xprt->xp_sock is the registered socket number and xprt->xp_port is the
 * associated port number.
 * Once *xprt is initialized, it is registered as a transporter;
 * see (svc.h, xprt_register).
 * The routines returns NULL if a problem occurred.
 */
SVCXPRT *
svcudp_bufcreate(int sock, u_int sendsz, u_int recvsz)
{
	bool_t madesock = FALSE;
	SVCXPRT *xprt;
	struct svcudp_data *su;
	struct sockaddr_in addr;
	socklen_t len = sizeof(struct sockaddr_in);

	if (sock == RPC_ANYSOCK) {
		if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
			return (NULL);
		madesock = TRUE;
	}
	memset(&addr, 0, sizeof (addr));
	addr.sin_len = sizeof(struct sockaddr_in);
	addr.sin_family = AF_INET;
	if (bindresvport(sock, &addr)) {
		addr.sin_port = 0;
		(void)bind(sock, (struct sockaddr *)&addr, len);
	}
	if (getsockname(sock, (struct sockaddr *)&addr, &len) != 0) {
		if (madesock)
			(void)close(sock);
		return (NULL);
	}
	xprt = malloc(sizeof(SVCXPRT));
	if (xprt == NULL) {
		if (madesock)
			(void)close(sock);
		return (NULL);
	}
	su = malloc(sizeof(*su));
	if (su == NULL) {
		if (madesock)
			(void)close(sock);
		free(xprt);
		return (NULL);
	}
	su->su_iosz = ((MAX(sendsz, recvsz) + 3) / 4) * 4;
	if ((rpc_buffer(xprt) = malloc(su->su_iosz)) == NULL) {
		if (madesock)
			(void)close(sock);
		free(xprt);
		free(su);
		return (NULL);
	}
	xdrmem_create(&(su->su_xdrs), rpc_buffer(xprt), su->su_iosz,
	    XDR_DECODE);
	su->su_cache = NULL;
	xprt->xp_p2 = (caddr_t)su;
	xprt->xp_verf.oa_base = su->su_verfbody;
	xprt->xp_ops = &svcudp_op;
	xprt->xp_port = ntohs(addr.sin_port);
	xprt->xp_sock = sock;
	if (__xprt_register(xprt) == 0) {
		if (madesock)
			(void)close(sock);
		free(rpc_buffer(xprt));
		free(xprt);
		free(su);
		return (NULL);
	}
	return (xprt);
}
DEF_WEAK(svcudp_bufcreate);

SVCXPRT *
svcudp_create(int sock)
{

	return(svcudp_bufcreate(sock, UDPMSGSIZE, UDPMSGSIZE));
}
DEF_WEAK(svcudp_create);

static enum xprt_stat
svcudp_stat(SVCXPRT *xprt)
{

	return (XPRT_IDLE); 
}

static bool_t
svcudp_recv(SVCXPRT *xprt, struct rpc_msg *msg)
{
	struct svcudp_data *su = su_data(xprt);
	XDR *xdrs = &(su->su_xdrs);
	int rlen;
	char *reply;
	u_long replylen;

    again:
	xprt->xp_addrlen = sizeof(struct sockaddr_in);
	rlen = recvfrom(xprt->xp_sock, rpc_buffer(xprt), (int) su->su_iosz,
	    0, (struct sockaddr *)&(xprt->xp_raddr), &(xprt->xp_addrlen));
	if (rlen == -1 && errno == EINTR)
		goto again;
	if (rlen == -1 || rlen < 4*sizeof(u_int32_t))
		return (FALSE);
	xdrs->x_op = XDR_DECODE;
	XDR_SETPOS(xdrs, 0);
	if (! xdr_callmsg(xdrs, msg))
		return (FALSE);
	su->su_xid = msg->rm_xid;
	if (su->su_cache != NULL) {
		if (cache_get(xprt, msg, &reply, &replylen)) {
			(void) sendto(xprt->xp_sock, reply, (int) replylen, 0,
			    (struct sockaddr *) &xprt->xp_raddr,
			    xprt->xp_addrlen);
			return (TRUE);
		}
	}
	return (TRUE);
}

static bool_t
svcudp_reply(SVCXPRT *xprt, struct rpc_msg *msg)
{
	struct svcudp_data *su = su_data(xprt);
	XDR *xdrs = &(su->su_xdrs);
	int slen;
	bool_t stat = FALSE;

	xdrs->x_op = XDR_ENCODE;
	XDR_SETPOS(xdrs, 0);
	msg->rm_xid = su->su_xid;
	if (xdr_replymsg(xdrs, msg)) {
		slen = (int)XDR_GETPOS(xdrs);
		if (sendto(xprt->xp_sock, rpc_buffer(xprt), slen, 0,
		    (struct sockaddr *)&(xprt->xp_raddr), xprt->xp_addrlen)
		    == slen) {
			stat = TRUE;
			if (su->su_cache && slen >= 0) {
				cache_set(xprt, (u_long) slen);
			}
		}
	}
	return (stat);
}

static bool_t
svcudp_getargs(SVCXPRT *xprt, xdrproc_t xdr_args, caddr_t args_ptr)
{

	return ((*xdr_args)(&(su_data(xprt)->su_xdrs), args_ptr));
}

static bool_t
svcudp_freeargs(SVCXPRT *xprt, xdrproc_t xdr_args, caddr_t args_ptr)
{
	XDR *xdrs = &(su_data(xprt)->su_xdrs);

	xdrs->x_op = XDR_FREE;
	return ((*xdr_args)(xdrs, args_ptr));
}

static void
svcudp_destroy(SVCXPRT *xprt)
{
	struct svcudp_data *su = su_data(xprt);

	xprt_unregister(xprt);
	if (xprt->xp_sock != -1)
		(void)close(xprt->xp_sock);
	xprt->xp_sock = -1;
	XDR_DESTROY(&(su->su_xdrs));
	mem_free(rpc_buffer(xprt), su->su_iosz);
	mem_free((caddr_t)su, sizeof(struct svcudp_data));
	mem_free((caddr_t)xprt, sizeof(SVCXPRT));
}

/*
 * Fifo cache for udp server
 * Copies pointers to reply buffers into fifo cache
 * Buffers are sent again if retransmissions are detected.
 */

#define SPARSENESS 4	/* 75% sparse */

/*
 * An entry in the cache
 */
typedef struct cache_node *cache_ptr;
struct cache_node {
	/*
	 * Index into cache is xid, proc, vers, prog and address
	 */
	u_long cache_xid;
	u_long cache_proc;
	u_long cache_vers;
	u_long cache_prog;
	struct sockaddr_in cache_addr;
	/*
	 * The cached reply and length
	 */
	char * cache_reply;
	u_long cache_replylen;
	/*
 	 * Next node on the list, if there is a collision
	 */
	cache_ptr cache_next;	
};

/*
 * The entire cache
 */
struct udp_cache {
	u_long uc_size;		/* size of cache */
	cache_ptr *uc_entries;	/* hash table of entries in cache */
	cache_ptr *uc_fifo;	/* fifo list of entries in cache */
	u_long uc_nextvictim;	/* points to next victim in fifo list */
	u_long uc_prog;		/* saved program number */
	u_long uc_vers;		/* saved version number */
	u_long uc_proc;		/* saved procedure number */
	struct sockaddr_in uc_addr; /* saved caller's address */
};


/*
 * the hashing function
 */
#define CACHE_LOC(transp, xid)	\
 (xid % (SPARSENESS*((struct udp_cache *) su_data(transp)->su_cache)->uc_size))	


/*
 * Enable use of the cache. 
 * Note: there is no disable.
 */
int
svcudp_enablecache(SVCXPRT *transp, u_long size)
{
	struct svcudp_data *su = su_data(transp);
	struct udp_cache *uc;

	if (su->su_cache != NULL)
		return(0);	
	uc = malloc(sizeof(*uc));
	if (uc == NULL)
		return(0);
	uc->uc_size = size;
	uc->uc_nextvictim = 0;
	if (size > SIZE_MAX / (sizeof(cache_ptr) * SPARSENESS) ||
	    (uc->uc_entries = calloc(size, sizeof(cache_ptr) * SPARSENESS)) == NULL) {
		free(uc);
		return(0);
	}
	uc->uc_fifo = calloc(sizeof(cache_ptr), size);
	if (uc->uc_fifo == NULL) {
		free(uc->uc_entries);
		free(uc);
		return(0);
	}
	su->su_cache = (char *) uc;
	return(1);
}


/*
 * Set an entry in the cache
 */
static void
cache_set(SVCXPRT *xprt, u_long replylen)
{
	cache_ptr victim;	
	cache_ptr *vicp;
	struct svcudp_data *su = su_data(xprt);
	struct udp_cache *uc = (struct udp_cache *) su->su_cache;
	u_int loc;
	char *newbuf;

	/*
 	 * Find space for the new entry, either by
	 * reusing an old entry, or by mallocing a new one
	 */
	victim = uc->uc_fifo[uc->uc_nextvictim];
	if (victim != NULL) {
		loc = CACHE_LOC(xprt, victim->cache_xid);
		for (vicp = &uc->uc_entries[loc]; 
		  *vicp != NULL && *vicp != victim; 
		  vicp = &(*vicp)->cache_next) 
				;
		if (*vicp == NULL) {
			return;
		}
		*vicp = victim->cache_next;	/* remote from cache */
		newbuf = victim->cache_reply;
	} else {
		victim = malloc(sizeof(struct cache_node));
		if (victim == NULL) {
			return;
		}
		newbuf = malloc(su->su_iosz);
		if (newbuf == NULL) {
			free(victim);
			return;
		}
	}

	/*
	 * Store it away
	 */
	victim->cache_replylen = replylen;
	victim->cache_reply = rpc_buffer(xprt);
	rpc_buffer(xprt) = newbuf;
	xdrmem_create(&(su->su_xdrs), rpc_buffer(xprt), su->su_iosz, XDR_ENCODE);
	victim->cache_xid = su->su_xid;
	victim->cache_proc = uc->uc_proc;
	victim->cache_vers = uc->uc_vers;
	victim->cache_prog = uc->uc_prog;
	victim->cache_addr = uc->uc_addr;
	loc = CACHE_LOC(xprt, victim->cache_xid);
	victim->cache_next = uc->uc_entries[loc];	
	uc->uc_entries[loc] = victim;
	uc->uc_fifo[uc->uc_nextvictim++] = victim;
	uc->uc_nextvictim %= uc->uc_size;
}

/*
 * Try to get an entry from the cache
 * return 1 if found, 0 if not found
 */
static int
cache_get(SVCXPRT *xprt, struct rpc_msg *msg, char **replyp, u_long *replylenp)
{
	u_int loc;
	cache_ptr ent;
	struct svcudp_data *su = su_data(xprt);
	struct udp_cache *uc = (struct udp_cache *) su->su_cache;

#	define EQADDR(a1, a2)	(memcmp(&a1, &a2, sizeof(a1)) == 0)

	loc = CACHE_LOC(xprt, su->su_xid);
	for (ent = uc->uc_entries[loc]; ent != NULL; ent = ent->cache_next) {
		if (ent->cache_xid == su->su_xid &&
		  ent->cache_proc == uc->uc_proc &&
		  ent->cache_vers == uc->uc_vers &&
		  ent->cache_prog == uc->uc_prog &&
		  EQADDR(ent->cache_addr, uc->uc_addr)) {
			*replyp = ent->cache_reply;
			*replylenp = ent->cache_replylen;
			return(1);
		}
	}
	/*
	 * Failed to find entry
	 * Remember a few things so we can do a set later
	 */
	uc->uc_proc = msg->rm_call.cb_proc;
	uc->uc_vers = msg->rm_call.cb_vers;
	uc->uc_prog = msg->rm_call.cb_prog;
	uc->uc_addr = xprt->xp_raddr;
	return(0);
}

