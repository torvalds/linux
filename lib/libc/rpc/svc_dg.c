/*	$NetBSD: svc_dg.c,v 1.4 2000/07/06 03:10:35 christos Exp $	*/

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
#ident	"@(#)svc_dg.c	1.17	94/04/24 SMI"
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * svc_dg.c, Server side for connectionless RPC.
 *
 * Does some caching in the hopes of achieving execute-at-most-once semantics.
 */

#include "namespace.h"
#include "reentrant.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <rpc/rpc.h>
#include <rpc/svc_dg.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef RPC_CACHE_DEBUG
#include <netconfig.h>
#include <netdir.h>
#endif
#include <err.h>
#include "un-namespace.h"

#include "rpc_com.h"
#include "mt_misc.h"

#define	su_data(xprt)	((struct svc_dg_data *)((xprt)->xp_p2))
#define	rpc_buffer(xprt) ((xprt)->xp_p1)

#ifndef MAX
#define	MAX(a, b)	(((a) > (b)) ? (a) : (b))
#endif

static void svc_dg_ops(SVCXPRT *);
static enum xprt_stat svc_dg_stat(SVCXPRT *);
static bool_t svc_dg_recv(SVCXPRT *, struct rpc_msg *);
static bool_t svc_dg_reply(SVCXPRT *, struct rpc_msg *);
static bool_t svc_dg_getargs(SVCXPRT *, xdrproc_t, void *);
static bool_t svc_dg_freeargs(SVCXPRT *, xdrproc_t, void *);
static void svc_dg_destroy(SVCXPRT *);
static bool_t svc_dg_control(SVCXPRT *, const u_int, void *);
static int cache_get(SVCXPRT *, struct rpc_msg *, char **, size_t *);
static void cache_set(SVCXPRT *, size_t);
int svc_dg_enablecache(SVCXPRT *, u_int);

/*
 * Usage:
 *	xprt = svc_dg_create(sock, sendsize, recvsize);
 * Does other connectionless specific initializations.
 * Once *xprt is initialized, it is registered.
 * see (svc.h, xprt_register). If recvsize or sendsize are 0 suitable
 * system defaults are chosen.
 * The routines returns NULL if a problem occurred.
 */
static const char svc_dg_str[] = "svc_dg_create: %s";
static const char svc_dg_err1[] = "could not get transport information";
static const char svc_dg_err2[] = "transport does not support data transfer";
static const char svc_dg_err3[] = "getsockname failed";
static const char svc_dg_err4[] = "cannot set IP_RECVDSTADDR";
static const char __no_mem_str[] = "out of memory";

SVCXPRT *
svc_dg_create(int fd, u_int sendsize, u_int recvsize)
{
	SVCXPRT *xprt;
	struct svc_dg_data *su = NULL;
	struct __rpc_sockinfo si;
	struct sockaddr_storage ss;
	socklen_t slen;

	if (!__rpc_fd2sockinfo(fd, &si)) {
		warnx(svc_dg_str, svc_dg_err1);
		return (NULL);
	}
	/*
	 * Find the receive and the send size
	 */
	sendsize = __rpc_get_t_size(si.si_af, si.si_proto, (int)sendsize);
	recvsize = __rpc_get_t_size(si.si_af, si.si_proto, (int)recvsize);
	if ((sendsize == 0) || (recvsize == 0)) {
		warnx(svc_dg_str, svc_dg_err2);
		return (NULL);
	}

	xprt = svc_xprt_alloc();
	if (xprt == NULL)
		goto freedata;

	su = mem_alloc(sizeof (*su));
	if (su == NULL)
		goto freedata;
	su->su_iosz = ((MAX(sendsize, recvsize) + 3) / 4) * 4;
	if ((rpc_buffer(xprt) = mem_alloc(su->su_iosz)) == NULL)
		goto freedata;
	xdrmem_create(&(su->su_xdrs), rpc_buffer(xprt), su->su_iosz,
		XDR_DECODE);
	su->su_cache = NULL;
	xprt->xp_fd = fd;
	xprt->xp_p2 = su;
	xprt->xp_verf.oa_base = su->su_verfbody;
	svc_dg_ops(xprt);
	xprt->xp_rtaddr.maxlen = sizeof (struct sockaddr_storage);

	slen = sizeof ss;
	if (_getsockname(fd, (struct sockaddr *)(void *)&ss, &slen) < 0) {
		warnx(svc_dg_str, svc_dg_err3);
		goto freedata_nowarn;
	}
	xprt->xp_ltaddr.buf = mem_alloc(sizeof (struct sockaddr_storage));
	xprt->xp_ltaddr.maxlen = sizeof (struct sockaddr_storage);
	xprt->xp_ltaddr.len = slen;
	memcpy(xprt->xp_ltaddr.buf, &ss, slen);

	if (ss.ss_family == AF_INET) {
		struct sockaddr_in *sin;
		static const int true_value = 1;

		sin = (struct sockaddr_in *)(void *)&ss;
		if (sin->sin_addr.s_addr == INADDR_ANY) {
		    su->su_srcaddr.buf = mem_alloc(sizeof (ss));
		    su->su_srcaddr.maxlen = sizeof (ss);

		    if (_setsockopt(fd, IPPROTO_IP, IP_RECVDSTADDR,
				    &true_value, sizeof(true_value))) {
			    warnx(svc_dg_str,  svc_dg_err4);
			    goto freedata_nowarn;
		    }
		}
	}

	xprt_register(xprt);
	return (xprt);
freedata:
	(void) warnx(svc_dg_str, __no_mem_str);
freedata_nowarn:
	if (xprt) {
		if (su)
			(void) mem_free(su, sizeof (*su));
		svc_xprt_free(xprt);
	}
	return (NULL);
}

/*ARGSUSED*/
static enum xprt_stat
svc_dg_stat(SVCXPRT *xprt)
{
	return (XPRT_IDLE);
}

static int
svc_dg_recvfrom(int fd, char *buf, int buflen,
    struct sockaddr *raddr, socklen_t *raddrlen,
    struct sockaddr *laddr, socklen_t *laddrlen)
{
	struct msghdr msg;
	struct iovec msg_iov[1];
	struct sockaddr_in *lin = (struct sockaddr_in *)laddr;
	int rlen;
	bool_t have_lin = FALSE;
	char tmp[CMSG_LEN(sizeof(*lin))];
	struct cmsghdr *cmsg;

	memset((char *)&msg, 0, sizeof(msg));
	msg_iov[0].iov_base = buf;
	msg_iov[0].iov_len = buflen;
	msg.msg_iov = msg_iov;
	msg.msg_iovlen = 1;
	msg.msg_namelen = *raddrlen;
	msg.msg_name = (char *)raddr;
	if (laddr != NULL) {
	    msg.msg_control = (caddr_t)tmp;
	    msg.msg_controllen = CMSG_LEN(sizeof(*lin));
	}
	rlen = _recvmsg(fd, &msg, 0);
	if (rlen >= 0)
		*raddrlen = msg.msg_namelen;

	if (rlen == -1 || laddr == NULL ||
	    msg.msg_controllen < sizeof(struct cmsghdr) ||
	    msg.msg_flags & MSG_CTRUNC)
		return rlen;

	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL;
	     cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		if (cmsg->cmsg_level == IPPROTO_IP &&
		    cmsg->cmsg_type == IP_RECVDSTADDR) {
			have_lin = TRUE;
			memcpy(&lin->sin_addr,
			    (struct in_addr *)CMSG_DATA(cmsg),
			    sizeof(struct in_addr));
			break;
		}
	}

	lin->sin_family = AF_INET;
	lin->sin_port = 0;
	*laddrlen = sizeof(struct sockaddr_in);

	if (!have_lin)
		lin->sin_addr.s_addr = INADDR_ANY;

	return rlen;
}

static bool_t
svc_dg_recv(SVCXPRT *xprt, struct rpc_msg *msg)
{
	struct svc_dg_data *su = su_data(xprt);
	XDR *xdrs = &(su->su_xdrs);
	char *reply;
	struct sockaddr_storage ss;
	socklen_t alen;
	size_t replylen;
	ssize_t rlen;

again:
	alen = sizeof (struct sockaddr_storage);
	rlen = svc_dg_recvfrom(xprt->xp_fd, rpc_buffer(xprt), su->su_iosz,
	    (struct sockaddr *)(void *)&ss, &alen,
	    (struct sockaddr *)su->su_srcaddr.buf, &su->su_srcaddr.len);
	if (rlen == -1 && errno == EINTR)
		goto again;
	if (rlen == -1 || (rlen < (ssize_t)(4 * sizeof (u_int32_t))))
		return (FALSE);
	if (xprt->xp_rtaddr.len < alen) {
		if (xprt->xp_rtaddr.len != 0)
			mem_free(xprt->xp_rtaddr.buf, xprt->xp_rtaddr.len);
		xprt->xp_rtaddr.buf = mem_alloc(alen);
		xprt->xp_rtaddr.len = alen;
	}
	memcpy(xprt->xp_rtaddr.buf, &ss, alen);
#ifdef PORTMAP
	if (ss.ss_family == AF_INET) {
		xprt->xp_raddr = *(struct sockaddr_in *)xprt->xp_rtaddr.buf;
		xprt->xp_addrlen = sizeof (struct sockaddr_in);
	}
#endif				/* PORTMAP */
	xdrs->x_op = XDR_DECODE;
	XDR_SETPOS(xdrs, 0);
	if (! xdr_callmsg(xdrs, msg)) {
		return (FALSE);
	}
	su->su_xid = msg->rm_xid;
	if (su->su_cache != NULL) {
		if (cache_get(xprt, msg, &reply, &replylen)) {
			(void)_sendto(xprt->xp_fd, reply, replylen, 0,
			    (struct sockaddr *)(void *)&ss, alen);
			return (FALSE);
		}
	}
	return (TRUE);
}

static int
svc_dg_sendto(int fd, char *buf, int buflen,
    const struct sockaddr *raddr, socklen_t raddrlen,
    const struct sockaddr *laddr, socklen_t laddrlen)
{
	struct msghdr msg;
	struct iovec msg_iov[1];
	struct sockaddr_in *laddr_in = (struct sockaddr_in *)laddr;
	struct in_addr *lin = &laddr_in->sin_addr;
	char tmp[CMSG_SPACE(sizeof(*lin))];
	struct cmsghdr *cmsg;

	memset((char *)&msg, 0, sizeof(msg));
	msg_iov[0].iov_base = buf;
	msg_iov[0].iov_len = buflen;
	msg.msg_iov = msg_iov;
	msg.msg_iovlen = 1;
	msg.msg_namelen = raddrlen;
	msg.msg_name = (char *)raddr;

	if (laddr != NULL && laddr->sa_family == AF_INET &&
	    lin->s_addr != INADDR_ANY) {
		msg.msg_control = (caddr_t)tmp;
		msg.msg_controllen = CMSG_LEN(sizeof(*lin));
		cmsg = CMSG_FIRSTHDR(&msg);
		cmsg->cmsg_len = CMSG_LEN(sizeof(*lin));
		cmsg->cmsg_level = IPPROTO_IP;
		cmsg->cmsg_type = IP_SENDSRCADDR;
		memcpy(CMSG_DATA(cmsg), lin, sizeof(*lin));
	}

	return _sendmsg(fd, &msg, 0);
}

static bool_t
svc_dg_reply(SVCXPRT *xprt, struct rpc_msg *msg)
{
	struct svc_dg_data *su = su_data(xprt);
	XDR *xdrs = &(su->su_xdrs);
	bool_t stat = TRUE;
	size_t slen;
	xdrproc_t xdr_proc;
	caddr_t xdr_where;

	xdrs->x_op = XDR_ENCODE;
	XDR_SETPOS(xdrs, 0);
	msg->rm_xid = su->su_xid;
	if (msg->rm_reply.rp_stat == MSG_ACCEPTED &&
	    msg->rm_reply.rp_acpt.ar_stat == SUCCESS) {
		xdr_proc = msg->acpted_rply.ar_results.proc;
		xdr_where = msg->acpted_rply.ar_results.where;
		msg->acpted_rply.ar_results.proc = (xdrproc_t) xdr_void;
		msg->acpted_rply.ar_results.where = NULL;

		if (!xdr_replymsg(xdrs, msg) ||
		    !SVCAUTH_WRAP(&SVC_AUTH(xprt), xdrs, xdr_proc, xdr_where))
			stat = FALSE;
	} else {
		stat = xdr_replymsg(xdrs, msg);
	}
	if (stat) {
		slen = XDR_GETPOS(xdrs);
		if (svc_dg_sendto(xprt->xp_fd, rpc_buffer(xprt), slen,
		    (struct sockaddr *)xprt->xp_rtaddr.buf,
		    (socklen_t)xprt->xp_rtaddr.len,
		    (struct sockaddr *)su->su_srcaddr.buf,
		    (socklen_t)su->su_srcaddr.len) == (ssize_t) slen) {
			stat = TRUE;
			if (su->su_cache)
				cache_set(xprt, slen);
		}
	}
	return (stat);
}

static bool_t
svc_dg_getargs(SVCXPRT *xprt, xdrproc_t xdr_args, void *args_ptr)
{
	struct svc_dg_data *su;

	assert(xprt != NULL);
	su = su_data(xprt);
	return (SVCAUTH_UNWRAP(&SVC_AUTH(xprt),
		&su->su_xdrs, xdr_args, args_ptr));
}

static bool_t
svc_dg_freeargs(SVCXPRT *xprt, xdrproc_t xdr_args, void *args_ptr)
{
	XDR *xdrs = &(su_data(xprt)->su_xdrs);

	xdrs->x_op = XDR_FREE;
	return (*xdr_args)(xdrs, args_ptr);
}

static void
svc_dg_destroy(SVCXPRT *xprt)
{
	struct svc_dg_data *su = su_data(xprt);

	xprt_unregister(xprt);
	if (xprt->xp_fd != -1)
		(void)_close(xprt->xp_fd);
	XDR_DESTROY(&(su->su_xdrs));
	(void) mem_free(rpc_buffer(xprt), su->su_iosz);
	if (su->su_srcaddr.buf)
		(void) mem_free(su->su_srcaddr.buf, su->su_srcaddr.maxlen);
	(void) mem_free(su, sizeof (*su));
	if (xprt->xp_rtaddr.buf)
		(void) mem_free(xprt->xp_rtaddr.buf, xprt->xp_rtaddr.maxlen);
	if (xprt->xp_ltaddr.buf)
		(void) mem_free(xprt->xp_ltaddr.buf, xprt->xp_ltaddr.maxlen);
	free(xprt->xp_tp);
	svc_xprt_free(xprt);
}

static bool_t
/*ARGSUSED*/
svc_dg_control(SVCXPRT *xprt, const u_int rq, void *in)
{
	return (FALSE);
}

static void
svc_dg_ops(SVCXPRT *xprt)
{
	static struct xp_ops ops;
	static struct xp_ops2 ops2;

/* VARIABLES PROTECTED BY ops_lock: ops */

	mutex_lock(&ops_lock);
	if (ops.xp_recv == NULL) {
		ops.xp_recv = svc_dg_recv;
		ops.xp_stat = svc_dg_stat;
		ops.xp_getargs = svc_dg_getargs;
		ops.xp_reply = svc_dg_reply;
		ops.xp_freeargs = svc_dg_freeargs;
		ops.xp_destroy = svc_dg_destroy;
		ops2.xp_control = svc_dg_control;
	}
	xprt->xp_ops = &ops;
	xprt->xp_ops2 = &ops2;
	mutex_unlock(&ops_lock);
}

/*  The CACHING COMPONENT */

/*
 * Could have been a separate file, but some part of it depends upon the
 * private structure of the client handle.
 *
 * Fifo cache for cl server
 * Copies pointers to reply buffers into fifo cache
 * Buffers are sent again if retransmissions are detected.
 */

#define	SPARSENESS 4	/* 75% sparse */

#define	ALLOC(type, size)	\
	(type *) mem_alloc((sizeof (type) * (size)))

#define	MEMZERO(addr, type, size)	 \
	(void) memset((void *) (addr), 0, sizeof (type) * (int) (size))

#define	FREE(addr, type, size)	\
	mem_free((addr), (sizeof (type) * (size)))

/*
 * An entry in the cache
 */
typedef struct cache_node *cache_ptr;
struct cache_node {
	/*
	 * Index into cache is xid, proc, vers, prog and address
	 */
	u_int32_t cache_xid;
	rpcproc_t cache_proc;
	rpcvers_t cache_vers;
	rpcprog_t cache_prog;
	struct netbuf cache_addr;
	/*
	 * The cached reply and length
	 */
	char *cache_reply;
	size_t cache_replylen;
	/*
	 * Next node on the list, if there is a collision
	 */
	cache_ptr cache_next;
};

/*
 * The entire cache
 */
struct cl_cache {
	u_int uc_size;		/* size of cache */
	cache_ptr *uc_entries;	/* hash table of entries in cache */
	cache_ptr *uc_fifo;	/* fifo list of entries in cache */
	u_int uc_nextvictim;	/* points to next victim in fifo list */
	rpcprog_t uc_prog;	/* saved program number */
	rpcvers_t uc_vers;	/* saved version number */
	rpcproc_t uc_proc;	/* saved procedure number */
};


/*
 * the hashing function
 */
#define	CACHE_LOC(transp, xid)	\
	(xid % (SPARSENESS * ((struct cl_cache *) \
		su_data(transp)->su_cache)->uc_size))

/*
 * Enable use of the cache. Returns 1 on success, 0 on failure.
 * Note: there is no disable.
 */
static const char cache_enable_str[] = "svc_enablecache: %s %s";
static const char alloc_err[] = "could not allocate cache ";
static const char enable_err[] = "cache already enabled";

int
svc_dg_enablecache(SVCXPRT *transp, u_int size)
{
	struct svc_dg_data *su = su_data(transp);
	struct cl_cache *uc;

	mutex_lock(&dupreq_lock);
	if (su->su_cache != NULL) {
		(void) warnx(cache_enable_str, enable_err, " ");
		mutex_unlock(&dupreq_lock);
		return (0);
	}
	uc = ALLOC(struct cl_cache, 1);
	if (uc == NULL) {
		warnx(cache_enable_str, alloc_err, " ");
		mutex_unlock(&dupreq_lock);
		return (0);
	}
	uc->uc_size = size;
	uc->uc_nextvictim = 0;
	uc->uc_entries = ALLOC(cache_ptr, size * SPARSENESS);
	if (uc->uc_entries == NULL) {
		warnx(cache_enable_str, alloc_err, "data");
		FREE(uc, struct cl_cache, 1);
		mutex_unlock(&dupreq_lock);
		return (0);
	}
	MEMZERO(uc->uc_entries, cache_ptr, size * SPARSENESS);
	uc->uc_fifo = ALLOC(cache_ptr, size);
	if (uc->uc_fifo == NULL) {
		warnx(cache_enable_str, alloc_err, "fifo");
		FREE(uc->uc_entries, cache_ptr, size * SPARSENESS);
		FREE(uc, struct cl_cache, 1);
		mutex_unlock(&dupreq_lock);
		return (0);
	}
	MEMZERO(uc->uc_fifo, cache_ptr, size);
	su->su_cache = (char *)(void *)uc;
	mutex_unlock(&dupreq_lock);
	return (1);
}

/*
 * Set an entry in the cache.  It assumes that the uc entry is set from
 * the earlier call to cache_get() for the same procedure.  This will always
 * happen because cache_get() is calle by svc_dg_recv and cache_set() is called
 * by svc_dg_reply().  All this hoopla because the right RPC parameters are
 * not available at svc_dg_reply time.
 */

static const char cache_set_str[] = "cache_set: %s";
static const char cache_set_err1[] = "victim not found";
static const char cache_set_err2[] = "victim alloc failed";
static const char cache_set_err3[] = "could not allocate new rpc buffer";

static void
cache_set(SVCXPRT *xprt, size_t replylen)
{
	cache_ptr victim;
	cache_ptr *vicp;
	struct svc_dg_data *su = su_data(xprt);
	struct cl_cache *uc = (struct cl_cache *) su->su_cache;
	u_int loc;
	char *newbuf;
#ifdef RPC_CACHE_DEBUG
	struct netconfig *nconf;
	char *uaddr;
#endif

	mutex_lock(&dupreq_lock);
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
			warnx(cache_set_str, cache_set_err1);
			mutex_unlock(&dupreq_lock);
			return;
		}
		*vicp = victim->cache_next;	/* remove from cache */
		newbuf = victim->cache_reply;
	} else {
		victim = ALLOC(struct cache_node, 1);
		if (victim == NULL) {
			warnx(cache_set_str, cache_set_err2);
			mutex_unlock(&dupreq_lock);
			return;
		}
		newbuf = mem_alloc(su->su_iosz);
		if (newbuf == NULL) {
			warnx(cache_set_str, cache_set_err3);
			FREE(victim, struct cache_node, 1);
			mutex_unlock(&dupreq_lock);
			return;
		}
	}

	/*
	 * Store it away
	 */
#ifdef RPC_CACHE_DEBUG
	if (nconf = getnetconfigent(xprt->xp_netid)) {
		uaddr = taddr2uaddr(nconf, &xprt->xp_rtaddr);
		freenetconfigent(nconf);
		printf(
	"cache set for xid= %x prog=%d vers=%d proc=%d for rmtaddr=%s\n",
			su->su_xid, uc->uc_prog, uc->uc_vers,
			uc->uc_proc, uaddr);
		free(uaddr);
	}
#endif
	victim->cache_replylen = replylen;
	victim->cache_reply = rpc_buffer(xprt);
	rpc_buffer(xprt) = newbuf;
	xdrmem_create(&(su->su_xdrs), rpc_buffer(xprt),
			su->su_iosz, XDR_ENCODE);
	victim->cache_xid = su->su_xid;
	victim->cache_proc = uc->uc_proc;
	victim->cache_vers = uc->uc_vers;
	victim->cache_prog = uc->uc_prog;
	victim->cache_addr = xprt->xp_rtaddr;
	victim->cache_addr.buf = ALLOC(char, xprt->xp_rtaddr.len);
	(void) memcpy(victim->cache_addr.buf, xprt->xp_rtaddr.buf,
	    (size_t)xprt->xp_rtaddr.len);
	loc = CACHE_LOC(xprt, victim->cache_xid);
	victim->cache_next = uc->uc_entries[loc];
	uc->uc_entries[loc] = victim;
	uc->uc_fifo[uc->uc_nextvictim++] = victim;
	uc->uc_nextvictim %= uc->uc_size;
	mutex_unlock(&dupreq_lock);
}

/*
 * Try to get an entry from the cache
 * return 1 if found, 0 if not found and set the stage for cache_set()
 */
static int
cache_get(SVCXPRT *xprt, struct rpc_msg *msg, char **replyp, size_t *replylenp)
{
	u_int loc;
	cache_ptr ent;
	struct svc_dg_data *su = su_data(xprt);
	struct cl_cache *uc = (struct cl_cache *) su->su_cache;
#ifdef RPC_CACHE_DEBUG
	struct netconfig *nconf;
	char *uaddr;
#endif

	mutex_lock(&dupreq_lock);
	loc = CACHE_LOC(xprt, su->su_xid);
	for (ent = uc->uc_entries[loc]; ent != NULL; ent = ent->cache_next) {
		if (ent->cache_xid == su->su_xid &&
			ent->cache_proc == msg->rm_call.cb_proc &&
			ent->cache_vers == msg->rm_call.cb_vers &&
			ent->cache_prog == msg->rm_call.cb_prog &&
			ent->cache_addr.len == xprt->xp_rtaddr.len &&
			(memcmp(ent->cache_addr.buf, xprt->xp_rtaddr.buf,
				xprt->xp_rtaddr.len) == 0)) {
#ifdef RPC_CACHE_DEBUG
			if (nconf = getnetconfigent(xprt->xp_netid)) {
				uaddr = taddr2uaddr(nconf, &xprt->xp_rtaddr);
				freenetconfigent(nconf);
				printf(
	"cache entry found for xid=%x prog=%d vers=%d proc=%d for rmtaddr=%s\n",
					su->su_xid, msg->rm_call.cb_prog,
					msg->rm_call.cb_vers,
					msg->rm_call.cb_proc, uaddr);
				free(uaddr);
			}
#endif
			*replyp = ent->cache_reply;
			*replylenp = ent->cache_replylen;
			mutex_unlock(&dupreq_lock);
			return (1);
		}
	}
	/*
	 * Failed to find entry
	 * Remember a few things so we can do a set later
	 */
	uc->uc_proc = msg->rm_call.cb_proc;
	uc->uc_vers = msg->rm_call.cb_vers;
	uc->uc_prog = msg->rm_call.cb_prog;
	mutex_unlock(&dupreq_lock);
	return (0);
}
