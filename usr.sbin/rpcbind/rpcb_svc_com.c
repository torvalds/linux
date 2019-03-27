/*	$NetBSD: rpcb_svc_com.c,v 1.9 2002/11/08 00:16:39 fvdl Exp $	*/
/*	$FreeBSD$ */

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
 * Copyright (c) 1986 - 1991 by Sun Microsystems, Inc.
 */

/* #ident	"@(#)rpcb_svc_com.c	1.18	94/05/02 SMI" */

/*
 * rpcb_svc_com.c
 * The commom server procedure for the rpcbind.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <rpc/rpc.h>
#include <rpc/rpcb_prot.h>
#include <rpc/svc_dg.h>
#include <assert.h>
#include <netconfig.h>
#include <errno.h>
#include <syslog.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef PORTMAP
#include <netinet/in.h>
#include <rpc/rpc_com.h>
#include <rpc/pmap_prot.h>
#endif /* PORTMAP */

#include "rpcbind.h"

#define RPC_BUF_MAX	65536	/* can be raised if required */

static char *nullstring = "";
static int rpcb_rmtcalls;

struct rmtcallfd_list {
	int fd;
	SVCXPRT *xprt;
	char *netid;
	struct rmtcallfd_list *next;
};

#define NFORWARD        64
#define MAXTIME_OFF     300     /* 5 minutes */

struct finfo {
	int             flag;
#define FINFO_ACTIVE    0x1
	u_int32_t       caller_xid;
        struct netbuf   *caller_addr;
	u_int32_t       forward_xid;
	int             forward_fd;
	char            *uaddr;
	rpcproc_t       reply_type;
	rpcvers_t       versnum;
	time_t          time;
};
static struct finfo     FINFO[NFORWARD];


static bool_t xdr_encap_parms(XDR *, struct encap_parms *);
static bool_t xdr_rmtcall_args(XDR *, struct r_rmtcall_args *);
static bool_t xdr_rmtcall_result(XDR *, struct r_rmtcall_args *);
static bool_t xdr_opaque_parms(XDR *, struct r_rmtcall_args *);
static int find_rmtcallfd_by_netid(char *);
static SVCXPRT *find_rmtcallxprt_by_fd(int);
static int forward_register(u_int32_t, struct netbuf *, int, char *,
    rpcproc_t, rpcvers_t, u_int32_t *);
static struct finfo *forward_find(u_int32_t);
static int free_slot_by_xid(u_int32_t);
static int free_slot_by_index(int);
static int netbufcmp(struct netbuf *, struct netbuf *);
static struct netbuf *netbufdup(struct netbuf *);
static void netbuffree(struct netbuf *);
static int check_rmtcalls(struct pollfd *, int);
static void xprt_set_caller(SVCXPRT *, struct finfo *);
static void send_svcsyserr(SVCXPRT *, struct finfo *);
static void handle_reply(int, SVCXPRT *);
static void find_versions(rpcprog_t, char *, rpcvers_t *, rpcvers_t *);
static rpcblist_ptr find_service(rpcprog_t, rpcvers_t, char *);
static char *getowner(SVCXPRT *, char *, size_t);
static int add_pmaplist(RPCB *);
static int del_pmaplist(RPCB *);

/*
 * Set a mapping of program, version, netid
 */
/* ARGSUSED */
void *
rpcbproc_set_com(void *arg, struct svc_req *rqstp __unused, SVCXPRT *transp,
		 rpcvers_t rpcbversnum)
{
	RPCB *regp = (RPCB *)arg;
	static bool_t ans;
	char owner[64];

#ifdef RPCBIND_DEBUG
	if (debugging)
		fprintf(stderr, "RPCB_SET request for (%lu, %lu, %s, %s) : ",
		    (unsigned long)regp->r_prog, (unsigned long)regp->r_vers,
		    regp->r_netid, regp->r_addr);
#endif
	ans = map_set(regp, getowner(transp, owner, sizeof owner));
#ifdef RPCBIND_DEBUG
	if (debugging)
		fprintf(stderr, "%s\n", ans == TRUE ? "succeeded" : "failed");
#endif
	/* XXX: should have used some defined constant here */
	rpcbs_set(rpcbversnum - 2, ans);
	return (void *)&ans;
}

bool_t
map_set(RPCB *regp, char *owner)
{
	RPCB reg, *a;
	rpcblist_ptr rbl, fnd;

	reg = *regp;
	/*
	 * check to see if already used
	 * find_service returns a hit even if
	 * the versions don't match, so check for it
	 */
	fnd = find_service(reg.r_prog, reg.r_vers, reg.r_netid);
	if (fnd && (fnd->rpcb_map.r_vers == reg.r_vers)) {
		if (!strcmp(fnd->rpcb_map.r_addr, reg.r_addr))
			/*
			 * if these match then it is already
			 * registered so just say "OK".
			 */
			return (TRUE);
		else
			return (FALSE);
	}
	/*
	 * add to the end of the list
	 */
	rbl = malloc(sizeof (RPCBLIST));
	if (rbl == NULL)
		return (FALSE);
	a = &(rbl->rpcb_map);
	a->r_prog = reg.r_prog;
	a->r_vers = reg.r_vers;
	a->r_netid = strdup(reg.r_netid);
	a->r_addr = strdup(reg.r_addr);
	a->r_owner = strdup(owner);
	if (!a->r_addr || !a->r_netid || !a->r_owner) {
		free(a->r_netid);
		free(a->r_addr);
		free(a->r_owner);
		free(rbl);
		return (FALSE);
	}
	rbl->rpcb_next = (rpcblist_ptr)NULL;
	if (list_rbl == NULL) {
		list_rbl = rbl;
	} else {
		for (fnd = list_rbl; fnd->rpcb_next;
			fnd = fnd->rpcb_next)
			;
		fnd->rpcb_next = rbl;
	}
#ifdef PORTMAP
	(void) add_pmaplist(regp);
#endif
	return (TRUE);
}

/*
 * Unset a mapping of program, version, netid
 */
/* ARGSUSED */
void *
rpcbproc_unset_com(void *arg, struct svc_req *rqstp __unused, SVCXPRT *transp,
		   rpcvers_t rpcbversnum)
{
	RPCB *regp = (RPCB *)arg;
	static bool_t ans;
	char owner[64];

#ifdef RPCBIND_DEBUG
	if (debugging)
		fprintf(stderr, "RPCB_UNSET request for (%lu, %lu, %s) : ",
		    (unsigned long)regp->r_prog, (unsigned long)regp->r_vers,
		    regp->r_netid);
#endif
	ans = map_unset(regp, getowner(transp, owner, sizeof owner));
#ifdef RPCBIND_DEBUG
	if (debugging)
		fprintf(stderr, "%s\n", ans == TRUE ? "succeeded" : "failed");
#endif
	/* XXX: should have used some defined constant here */
	rpcbs_unset(rpcbversnum - 2, ans);
	return (void *)&ans;
}

bool_t
map_unset(RPCB *regp, char *owner)
{
	int ans = 0;
	rpcblist_ptr rbl, prev, tmp;

	if (owner == NULL)
		return (0);

	for (prev = NULL, rbl = list_rbl; rbl; /* cstyle */) {
		if ((rbl->rpcb_map.r_prog != regp->r_prog) ||
			(rbl->rpcb_map.r_vers != regp->r_vers) ||
			(regp->r_netid[0] && strcasecmp(regp->r_netid,
				rbl->rpcb_map.r_netid))) {
			/* both rbl & prev move forwards */
			prev = rbl;
			rbl = rbl->rpcb_next;
			continue;
		}
		/*
		 * Check whether appropriate uid. Unset only
		 * if superuser or the owner itself.
		 */
		if (strcmp(owner, "superuser") &&
			strcmp(rbl->rpcb_map.r_owner, owner))
			return (0);
		/* found it; rbl moves forward, prev stays */
		ans = 1;
		tmp = rbl;
		rbl = rbl->rpcb_next;
		if (prev == NULL)
			list_rbl = rbl;
		else
			prev->rpcb_next = rbl;
		free(tmp->rpcb_map.r_addr);
		free(tmp->rpcb_map.r_netid);
		free(tmp->rpcb_map.r_owner);
		free(tmp);
	}
#ifdef PORTMAP
	if (ans)
		(void) del_pmaplist(regp);
#endif
	/*
	 * We return 1 either when the entry was not there or it
	 * was able to unset it.  It can come to this point only if
	 * atleast one of the conditions is true.
	 */
	return (1);
}

void
delete_prog(unsigned int prog)
{
	RPCB reg;
	register rpcblist_ptr rbl;

	for (rbl = list_rbl; rbl != NULL; rbl = rbl->rpcb_next) {
		if ((rbl->rpcb_map.r_prog != prog))
			continue;
		if (is_bound(rbl->rpcb_map.r_netid, rbl->rpcb_map.r_addr))
			continue;
		reg.r_prog = rbl->rpcb_map.r_prog;
		reg.r_vers = rbl->rpcb_map.r_vers;
		reg.r_netid = strdup(rbl->rpcb_map.r_netid);
		(void) map_unset(&reg, "superuser");
		free(reg.r_netid);
	}
}

void *
rpcbproc_getaddr_com(RPCB *regp, struct svc_req *rqstp __unused,
		     SVCXPRT *transp, rpcvers_t rpcbversnum, rpcvers_t verstype)
{
	static char *uaddr;
	char *saddr = NULL;
	rpcblist_ptr fnd;

	if (uaddr != NULL && uaddr != nullstring) {
		free(uaddr);
		uaddr = NULL;
	}
	fnd = find_service(regp->r_prog, regp->r_vers, transp->xp_netid);
	if (fnd && ((verstype == RPCB_ALLVERS) ||
		    (regp->r_vers == fnd->rpcb_map.r_vers))) {
		if (*(regp->r_addr) != '\0') {  /* may contain a hint about */
			saddr = regp->r_addr;   /* the interface that we    */
		}				/* should use */
		if (!(uaddr = mergeaddr(transp, transp->xp_netid,
				fnd->rpcb_map.r_addr, saddr))) {
			/* Try whatever we have */
			uaddr = strdup(fnd->rpcb_map.r_addr);
		} else if (!uaddr[0]) {
			/*
			 * The server died.  Unset all versions of this prog.
			 */
			delete_prog(regp->r_prog);
			uaddr = nullstring;
		}
	} else {
		uaddr = nullstring;
	}
#ifdef RPCBIND_DEBUG
	if (debugging)
		fprintf(stderr, "getaddr: %s\n", uaddr);
#endif
	/* XXX: should have used some defined constant here */
	rpcbs_getaddr(rpcbversnum - 2, regp->r_prog, regp->r_vers,
		transp->xp_netid, uaddr);
	return (void *)&uaddr;
}

/* ARGSUSED */
void *
rpcbproc_gettime_com(void *arg __unused, struct svc_req *rqstp __unused,
		     SVCXPRT *transp __unused, rpcvers_t rpcbversnum __unused)
{
	static time_t curtime;

	(void) time(&curtime);
	return (void *)&curtime;
}

/*
 * Convert uaddr to taddr. Should be used only by
 * local servers/clients. (kernel level stuff only)
 */
/* ARGSUSED */
void *
rpcbproc_uaddr2taddr_com(void *arg, struct svc_req *rqstp __unused,
			 SVCXPRT *transp, rpcvers_t rpcbversnum __unused)
{
	char **uaddrp = (char **)arg;
	struct netconfig *nconf;
	static struct netbuf nbuf;
	static struct netbuf *taddr;

	netbuffree(taddr);
	taddr = NULL;
	if (((nconf = rpcbind_get_conf(transp->xp_netid)) == NULL) ||
	    ((taddr = uaddr2taddr(nconf, *uaddrp)) == NULL)) {
		(void) memset((char *)&nbuf, 0, sizeof (struct netbuf));
		return (void *)&nbuf;
	}
	return (void *)taddr;
}

/*
 * Convert taddr to uaddr. Should be used only by
 * local servers/clients. (kernel level stuff only)
 */
/* ARGSUSED */
void *
rpcbproc_taddr2uaddr_com(void *arg, struct svc_req *rqstp __unused,
			 SVCXPRT *transp, rpcvers_t rpcbversnum __unused)
{
	struct netbuf *taddr = (struct netbuf *)arg;
	static char *uaddr;
	struct netconfig *nconf;

#ifdef CHEW_FDS
	int fd;

	if ((fd = open("/dev/null", O_RDONLY)) == -1) {
		uaddr = (char *)strerror(errno);
		return (&uaddr);
	}
#endif /* CHEW_FDS */
	if (uaddr != NULL && uaddr != nullstring) {
		free(uaddr);
		uaddr = NULL;
	}
	if (((nconf = rpcbind_get_conf(transp->xp_netid)) == NULL) ||
		((uaddr = taddr2uaddr(nconf, taddr)) == NULL)) {
		uaddr = nullstring;
	}
	return (void *)&uaddr;
}


static bool_t
xdr_encap_parms(XDR *xdrs, struct encap_parms *epp)
{
	return (xdr_bytes(xdrs, &(epp->args), (u_int *) &(epp->arglen),
	    RPC_MAXDATASIZE));
}

/*
 * XDR remote call arguments.  It ignores the address part.
 * written for XDR_DECODE direction only
 */
static bool_t
xdr_rmtcall_args(XDR *xdrs, struct r_rmtcall_args *cap)
{
	/* does not get the address or the arguments */
	if (xdr_rpcprog(xdrs, &(cap->rmt_prog)) &&
	    xdr_rpcvers(xdrs, &(cap->rmt_vers)) &&
	    xdr_rpcproc(xdrs, &(cap->rmt_proc))) {
		return (xdr_encap_parms(xdrs, &(cap->rmt_args)));
	}
	return (FALSE);
}

/*
 * XDR remote call results along with the address.  Ignore
 * program number, version  number and proc number.
 * Written for XDR_ENCODE direction only.
 */
static bool_t
xdr_rmtcall_result(XDR *xdrs, struct r_rmtcall_args *cap)
{
	bool_t result;

#ifdef PORTMAP
	if (cap->rmt_localvers == PMAPVERS) {
		int h1, h2, h3, h4, p1, p2;
		u_long port;

		/* interpret the universal address for TCP/IP */
		if (sscanf(cap->rmt_uaddr, "%d.%d.%d.%d.%d.%d",
			&h1, &h2, &h3, &h4, &p1, &p2) != 6)
			return (FALSE);
		port = ((p1 & 0xff) << 8) + (p2 & 0xff);
		result = xdr_u_long(xdrs, &port);
	} else
#endif
		if ((cap->rmt_localvers == RPCBVERS) ||
		    (cap->rmt_localvers == RPCBVERS4)) {
		result = xdr_wrapstring(xdrs, &(cap->rmt_uaddr));
	} else {
		return (FALSE);
	}
	if (result == TRUE)
		return (xdr_encap_parms(xdrs, &(cap->rmt_args)));
	return (FALSE);
}

/*
 * only worries about the struct encap_parms part of struct r_rmtcall_args.
 * The arglen must already be set!!
 */
static bool_t
xdr_opaque_parms(XDR *xdrs, struct r_rmtcall_args *cap)
{
	return (xdr_opaque(xdrs, cap->rmt_args.args, cap->rmt_args.arglen));
}

static struct rmtcallfd_list *rmthead;
static struct rmtcallfd_list *rmttail;

int
create_rmtcall_fd(struct netconfig *nconf)
{
	int fd;
	struct rmtcallfd_list *rmt;
	SVCXPRT *xprt;

	if ((fd = __rpc_nconf2fd(nconf)) == -1) {
		if (debugging)
			fprintf(stderr,
	"create_rmtcall_fd: couldn't open \"%s\" (errno %d)\n",
			nconf->nc_device, errno);
		return (-1);
	}
	xprt = svc_tli_create(fd, 0, (struct t_bind *) 0, 0, 0);
	if (xprt == NULL) {
		if (debugging)
			fprintf(stderr,
				"create_rmtcall_fd: svc_tli_create failed\n");
		return (-1);
	}
	rmt = malloc(sizeof (struct rmtcallfd_list));
	if (rmt == NULL) {
		syslog(LOG_ERR, "create_rmtcall_fd: no memory!");
		return (-1);
	}
	rmt->xprt = xprt;
	rmt->netid = strdup(nconf->nc_netid);
	xprt->xp_netid = rmt->netid;
	rmt->fd = fd;
	rmt->next = NULL;
	if (rmthead == NULL) {
		rmthead = rmt;
		rmttail = rmt;
	} else {
		rmttail->next = rmt;
		rmttail = rmt;
	}
	/* XXX not threadsafe */
	if (fd > svc_maxfd)
		svc_maxfd = fd;
	FD_SET(fd, &svc_fdset);
	return (fd);
}

static int
find_rmtcallfd_by_netid(char *netid)
{
	struct rmtcallfd_list *rmt;

	for (rmt = rmthead; rmt != NULL; rmt = rmt->next) {
		if (strcmp(netid, rmt->netid) == 0) {
			return (rmt->fd);
		}
	}
	return (-1);
}

static SVCXPRT *
find_rmtcallxprt_by_fd(int fd)
{
	struct rmtcallfd_list *rmt;

	for (rmt = rmthead; rmt != NULL; rmt = rmt->next) {
		if (fd == rmt->fd) {
			return (rmt->xprt);
		}
	}
	return (NULL);
}


/*
 * Call a remote procedure service.  This procedure is very quiet when things
 * go wrong.  The proc is written to support broadcast rpc.  In the broadcast
 * case, a machine should shut-up instead of complain, lest the requestor be
 * overrun with complaints at the expense of not hearing a valid reply.
 * When receiving a request and verifying that the service exists, we
 *
 *	receive the request
 *
 *	open a new TLI endpoint on the same transport on which we received
 *	the original request
 *
 *	remember the original request's XID (which requires knowing the format
 *	of the svc_dg_data structure)
 *
 *	forward the request, with a new XID, to the requested service,
 *	remembering the XID used to send this request (for later use in
 *	reassociating the answer with the original request), the requestor's
 *	address, the file descriptor on which the forwarded request is
 *	made and the service's address.
 *
 *	mark the file descriptor on which we anticipate receiving a reply from
 *	the service and one to select for in our private svc_run procedure
 *
 * At some time in the future, a reply will be received from the service to
 * which we forwarded the request.  At that time, we detect that the socket
 * used was for forwarding (by looking through the finfo structures to see
 * whether the fd corresponds to one of those) and call handle_reply() to
 *
 *	receive the reply
 *
 *	bundle the reply, along with the service's universal address
 *
 *	create a SVCXPRT structure and use a version of svc_sendreply
 *	that allows us to specify the reply XID and destination, send the reply
 *	to the original requestor.
 */

void
rpcbproc_callit_com(struct svc_req *rqstp, SVCXPRT *transp,
		    rpcproc_t reply_type, rpcvers_t versnum)
{
	register rpcblist_ptr rbl;
	struct netconfig *nconf;
	struct netbuf *caller;
	struct r_rmtcall_args a;
	char *buf_alloc = NULL, *outbufp;
	char *outbuf_alloc = NULL;
	char buf[RPC_BUF_MAX], outbuf[RPC_BUF_MAX];
	struct netbuf *na = (struct netbuf *) NULL;
	struct rpc_msg call_msg;
	int outlen;
	u_int sendsz;
	XDR outxdr;
	AUTH *auth;
	int fd = -1;
	char *uaddr, *m_uaddr = NULL, *local_uaddr = NULL;
	u_int32_t *xidp;
	struct __rpc_sockinfo si;
	struct sockaddr *localsa;
	struct netbuf tbuf;

	if (!__rpc_fd2sockinfo(transp->xp_fd, &si)) {
		if (reply_type == RPCBPROC_INDIRECT)
			svcerr_systemerr(transp);
		return;
	}
	if (si.si_socktype != SOCK_DGRAM)
		return;	/* Only datagram type accepted */
	sendsz = __rpc_get_t_size(si.si_af, si.si_proto, UDPMSGSIZE);
	if (sendsz == 0) {	/* data transfer not supported */
		if (reply_type == RPCBPROC_INDIRECT)
			svcerr_systemerr(transp);
		return;
	}
	/*
	 * Should be multiple of 4 for XDR.
	 */
	sendsz = roundup(sendsz, 4);
	if (sendsz > RPC_BUF_MAX) {
#ifdef	notyet
		buf_alloc = alloca(sendsz);		/* not in IDR2? */
#else
		buf_alloc = malloc(sendsz);
#endif	/* notyet */
		if (buf_alloc == NULL) {
			if (debugging)
				fprintf(stderr,
					"rpcbproc_callit_com:  No Memory!\n");
			if (reply_type == RPCBPROC_INDIRECT)
				svcerr_systemerr(transp);
			return;
		}
		a.rmt_args.args = buf_alloc;
	} else {
		a.rmt_args.args = buf;
	}

	call_msg.rm_xid = 0;	/* For error checking purposes */
	if (!svc_getargs(transp, (xdrproc_t) xdr_rmtcall_args, (char *) &a)) {
		if (reply_type == RPCBPROC_INDIRECT)
			svcerr_decode(transp);
		if (debugging)
			fprintf(stderr,
			"rpcbproc_callit_com:  svc_getargs failed\n");
		goto error;
	}

	if (!check_callit(transp, &a, versnum)) {
		svcerr_weakauth(transp);
		goto error;
	}
		
	caller = svc_getrpccaller(transp);
#ifdef RPCBIND_DEBUG
	if (debugging) {
		uaddr = taddr2uaddr(rpcbind_get_conf(transp->xp_netid), caller);
		fprintf(stderr, "%s %s req for (%lu, %lu, %lu, %s) from %s : ",
			versnum == PMAPVERS ? "pmap_rmtcall" :
			versnum == RPCBVERS ? "rpcb_rmtcall" :
			versnum == RPCBVERS4 ? "rpcb_indirect" : "unknown",
			reply_type == RPCBPROC_INDIRECT ? "indirect" : "callit",
			(unsigned long)a.rmt_prog, (unsigned long)a.rmt_vers,
			(unsigned long)a.rmt_proc, transp->xp_netid,
			uaddr ? uaddr : "unknown");
		free(uaddr);
	}
#endif

	rbl = find_service(a.rmt_prog, a.rmt_vers, transp->xp_netid);

	rpcbs_rmtcall(versnum - 2, reply_type, a.rmt_prog, a.rmt_vers,
			a.rmt_proc, transp->xp_netid, rbl);

	if (rbl == (rpcblist_ptr)NULL) {
#ifdef RPCBIND_DEBUG
		if (debugging)
			fprintf(stderr, "not found\n");
#endif
		if (reply_type == RPCBPROC_INDIRECT)
			svcerr_noprog(transp);
		goto error;
	}
	if (rbl->rpcb_map.r_vers != a.rmt_vers) {
		if (reply_type == RPCBPROC_INDIRECT) {
			rpcvers_t vers_low, vers_high;

			find_versions(a.rmt_prog, transp->xp_netid,
				&vers_low, &vers_high);
			svcerr_progvers(transp, vers_low, vers_high);
		}
		goto error;
	}

#ifdef RPCBIND_DEBUG
	if (debugging)
		fprintf(stderr, "found at uaddr %s\n", rbl->rpcb_map.r_addr);
#endif
	/*
	 *	Check whether this entry is valid and a server is present
	 *	Mergeaddr() returns NULL if no such entry is present, and
	 *	returns "" if the entry was present but the server is not
	 *	present (i.e., it crashed).
	 */
	if (reply_type == RPCBPROC_INDIRECT) {
		uaddr = mergeaddr(transp, transp->xp_netid,
			rbl->rpcb_map.r_addr, NULL);
		if (uaddr == NULL || uaddr[0] == '\0') {
			svcerr_noprog(transp);
			free(uaddr);
			goto error;
		}
		free(uaddr);
	}
	nconf = rpcbind_get_conf(transp->xp_netid);
	if (nconf == (struct netconfig *)NULL) {
		if (reply_type == RPCBPROC_INDIRECT)
			svcerr_systemerr(transp);
		if (debugging)
			fprintf(stderr,
			"rpcbproc_callit_com:  rpcbind_get_conf failed\n");
		goto error;
	}
	localsa = local_sa(((struct sockaddr *)caller->buf)->sa_family);
	if (localsa == NULL) {
		if (debugging)
			fprintf(stderr,
			"rpcbproc_callit_com: no local address\n");
		goto error;
	}
	tbuf.len = tbuf.maxlen = localsa->sa_len;
	tbuf.buf = localsa;
	local_uaddr =
	    addrmerge(&tbuf, rbl->rpcb_map.r_addr, NULL, nconf->nc_netid);
	m_uaddr = addrmerge(caller, rbl->rpcb_map.r_addr, NULL,
			nconf->nc_netid);
#ifdef RPCBIND_DEBUG
	if (debugging)
		fprintf(stderr, "merged uaddr %s\n", m_uaddr);
#endif
	if ((fd = find_rmtcallfd_by_netid(nconf->nc_netid)) == -1) {
		if (reply_type == RPCBPROC_INDIRECT)
			svcerr_systemerr(transp);
		goto error;
	}
	xidp = __rpcb_get_dg_xidp(transp);
	switch (forward_register(*xidp, caller, fd, m_uaddr, reply_type,
	    versnum, &call_msg.rm_xid)) {
	case 1:
		/* Success; forward_register() will free m_uaddr for us. */
		m_uaddr = NULL;
		break;
	case 0:
		/*
		 * A duplicate request for the slow server.  Let's not
		 * beat on it any more.
		 */
		if (debugging)
			fprintf(stderr,
			"rpcbproc_callit_com:  duplicate request\n");
		goto error;
	case -1:
		/*  forward_register failed.  Perhaps no memory. */
		if (debugging)
			fprintf(stderr,
			"rpcbproc_callit_com:  forward_register failed\n");
		goto error;
	}

#ifdef DEBUG_RMTCALL
	if (debugging)
		fprintf(stderr,
			"rpcbproc_callit_com:  original XID %x, new XID %x\n",
				*xidp, call_msg.rm_xid);
#endif
	call_msg.rm_direction = CALL;
	call_msg.rm_call.cb_rpcvers = RPC_MSG_VERSION;
	call_msg.rm_call.cb_prog = a.rmt_prog;
	call_msg.rm_call.cb_vers = a.rmt_vers;
	if (sendsz > RPC_BUF_MAX) {
#ifdef	notyet
		outbuf_alloc = alloca(sendsz);	/* not in IDR2? */
#else
		outbuf_alloc = malloc(sendsz);
#endif	/* notyet */
		if (outbuf_alloc == NULL) {
			if (reply_type == RPCBPROC_INDIRECT)
				svcerr_systemerr(transp);
			if (debugging)
				fprintf(stderr,
				"rpcbproc_callit_com:  No memory!\n");
			goto error;
		}
		xdrmem_create(&outxdr, outbuf_alloc, sendsz, XDR_ENCODE);
	} else {
		xdrmem_create(&outxdr, outbuf, sendsz, XDR_ENCODE);
	}
	if (!xdr_callhdr(&outxdr, &call_msg)) {
		if (reply_type == RPCBPROC_INDIRECT)
			svcerr_systemerr(transp);
		if (debugging)
			fprintf(stderr,
			"rpcbproc_callit_com:  xdr_callhdr failed\n");
		goto error;
	}
	if (!xdr_u_int32_t(&outxdr, &(a.rmt_proc))) {
		if (reply_type == RPCBPROC_INDIRECT)
			svcerr_systemerr(transp);
		if (debugging)
			fprintf(stderr,
			"rpcbproc_callit_com:  xdr_u_long failed\n");
		goto error;
	}

	if (rqstp->rq_cred.oa_flavor == AUTH_NULL) {
		auth = authnone_create();
	} else if (rqstp->rq_cred.oa_flavor == AUTH_SYS) {
		struct authunix_parms *au;

		au = (struct authunix_parms *)rqstp->rq_clntcred;
		auth = authunix_create(au->aup_machname,
				au->aup_uid, au->aup_gid,
				au->aup_len, au->aup_gids);
		if (auth == NULL) /* fall back */
			auth = authnone_create();
	} else {
		/* we do not support any other authentication scheme */
		if (debugging)
			fprintf(stderr,
"rpcbproc_callit_com:  oa_flavor != AUTH_NONE and oa_flavor != AUTH_SYS\n");
		if (reply_type == RPCBPROC_INDIRECT)
			svcerr_weakauth(transp); /* XXX too strong.. */
		goto error;
	}
	if (auth == NULL) {
		if (reply_type == RPCBPROC_INDIRECT)
			svcerr_systemerr(transp);
		if (debugging)
			fprintf(stderr,
		"rpcbproc_callit_com:  authwhatever_create returned NULL\n");
		goto error;
	}
	if (!AUTH_MARSHALL(auth, &outxdr)) {
		if (reply_type == RPCBPROC_INDIRECT)
			svcerr_systemerr(transp);
		AUTH_DESTROY(auth);
		if (debugging)
			fprintf(stderr,
		"rpcbproc_callit_com:  AUTH_MARSHALL failed\n");
		goto error;
	}
	AUTH_DESTROY(auth);
	if (!xdr_opaque_parms(&outxdr, &a)) {
		if (reply_type == RPCBPROC_INDIRECT)
			svcerr_systemerr(transp);
		if (debugging)
			fprintf(stderr,
		"rpcbproc_callit_com:  xdr_opaque_parms failed\n");
		goto error;
	}
	outlen = (int) XDR_GETPOS(&outxdr);
	if (outbuf_alloc)
		outbufp = outbuf_alloc;
	else
		outbufp = outbuf;

	na = uaddr2taddr(nconf, local_uaddr);
	if (!na) {
		if (reply_type == RPCBPROC_INDIRECT)
			svcerr_systemerr(transp);
		goto error;
	}

	if (sendto(fd, outbufp, outlen, 0, (struct sockaddr *)na->buf, na->len)
	    != outlen) {
		if (debugging)
			fprintf(stderr,
	"rpcbproc_callit_com:  sendto failed:  errno %d\n", errno);
		if (reply_type == RPCBPROC_INDIRECT)
			svcerr_systemerr(transp);
		goto error;
	}
	goto out;

error:
	if (call_msg.rm_xid != 0)
		(void) free_slot_by_xid(call_msg.rm_xid);
out:
	free(local_uaddr);
	free(buf_alloc);
	free(outbuf_alloc);
	netbuffree(na);
	free(m_uaddr);
}

/*
 * Makes an entry into the FIFO for the given request.
 * Returns 1 on success, 0 if this is a duplicate request, or -1 on error.
 * *callxidp is set to the xid of the call.
 */
static int
forward_register(u_int32_t caller_xid, struct netbuf *caller_addr,
		 int forward_fd, char *uaddr, rpcproc_t reply_type,
		 rpcvers_t versnum, u_int32_t *callxidp)
{
	int		i;
	int		j = 0;
	time_t		min_time, time_now;
	static u_int32_t	lastxid;
	int		entry = -1;

	min_time = FINFO[0].time;
	time_now = time((time_t *)0);
	/* initialization */
	if (lastxid == 0)
		lastxid = time_now * NFORWARD;

	/*
	 * Check if it is a duplicate entry. Then,
	 * try to find an empty slot.  If not available, then
	 * use the slot with the earliest time.
	 */
	for (i = 0; i < NFORWARD; i++) {
		if (FINFO[i].flag & FINFO_ACTIVE) {
			if ((FINFO[i].caller_xid == caller_xid) &&
			    (FINFO[i].reply_type == reply_type) &&
			    (FINFO[i].versnum == versnum) &&
			    (!netbufcmp(FINFO[i].caller_addr,
					    caller_addr))) {
				FINFO[i].time = time((time_t *)0);
				return (0);	/* Duplicate entry */
			} else {
				/* Should we wait any longer */
				if ((time_now - FINFO[i].time) > MAXTIME_OFF)
					(void) free_slot_by_index(i);
			}
		}
		if (entry == -1) {
			if ((FINFO[i].flag & FINFO_ACTIVE) == 0) {
				entry = i;
			} else if (FINFO[i].time < min_time) {
				j = i;
				min_time = FINFO[i].time;
			}
		}
	}
	if (entry != -1) {
		/* use this empty slot */
		j = entry;
	} else {
		(void) free_slot_by_index(j);
	}
	if ((FINFO[j].caller_addr = netbufdup(caller_addr)) == NULL) {
		return (-1);
	}
	rpcb_rmtcalls++;	/* no of pending calls */
	FINFO[j].flag = FINFO_ACTIVE;
	FINFO[j].reply_type = reply_type;
	FINFO[j].versnum = versnum;
	FINFO[j].time = time_now;
	FINFO[j].caller_xid = caller_xid;
	FINFO[j].forward_fd = forward_fd;
	/*
	 * Though uaddr is not allocated here, it will still be freed
	 * from free_slot_*().
	 */
	FINFO[j].uaddr = uaddr;
	lastxid = lastxid + NFORWARD;
	/* Don't allow a zero xid below. */
	if ((u_int32_t)(lastxid + NFORWARD) <= NFORWARD)
		lastxid = NFORWARD;
	FINFO[j].forward_xid = lastxid + j;	/* encode slot */
	*callxidp = FINFO[j].forward_xid;	/* forward on this xid */
	return (1);
}

static struct finfo *
forward_find(u_int32_t reply_xid)
{
	int		i;

	i = reply_xid % (u_int32_t)NFORWARD;
	if ((FINFO[i].flag & FINFO_ACTIVE) &&
	    (FINFO[i].forward_xid == reply_xid)) {
		return (&FINFO[i]);
	}
	return (NULL);
}

static int
free_slot_by_xid(u_int32_t xid)
{
	int entry;

	entry = xid % (u_int32_t)NFORWARD;
	return (free_slot_by_index(entry));
}

static int
free_slot_by_index(int index)
{
	struct finfo	*fi;

	fi = &FINFO[index];
	if (fi->flag & FINFO_ACTIVE) {
		netbuffree(fi->caller_addr);
		/* XXX may be too big, but can't access xprt array here */
		if (fi->forward_fd >= svc_maxfd)
			svc_maxfd--;
		free(fi->uaddr);
		fi->flag &= ~FINFO_ACTIVE;
		rpcb_rmtcalls--;
		return (1);
	}
	return (0);
}

static int
netbufcmp(struct netbuf *n1, struct netbuf *n2)
{
	return ((n1->len != n2->len) || memcmp(n1->buf, n2->buf, n1->len));
}

static bool_t
netbuf_copybuf(struct netbuf *dst, const struct netbuf *src)
{
	assert(src->len <= src->maxlen);

	if (dst->maxlen < src->len || dst->buf == NULL) {
		free(dst->buf);
		if ((dst->buf = calloc(1, src->maxlen)) == NULL)
			return (FALSE);
		dst->maxlen = src->maxlen;
	}

	dst->len = src->len;
	memcpy(dst->buf, src->buf, src->len);

	return (TRUE);
}

static struct netbuf *
netbufdup(struct netbuf *ap)
{
	struct netbuf  *np;

	if ((np = calloc(1, sizeof(struct netbuf))) == NULL)
		return (NULL);
	if (netbuf_copybuf(np, ap) == FALSE) {
		free(np);
		return (NULL);
	}
	return (np);
}

static void
netbuffree(struct netbuf *ap)
{

	if (ap == NULL)
		return;
	free(ap->buf);
	ap->buf = NULL;
	free(ap);
}


#define	MASKVAL	(POLLIN | POLLPRI | POLLRDNORM | POLLRDBAND)
extern bool_t __svc_clean_idle(fd_set *, int, bool_t);

void
my_svc_run(void)
{
	size_t nfds;
	struct pollfd pollfds[FD_SETSIZE + 1];
	int poll_ret, check_ret;
	int n;
#ifdef SVC_RUN_DEBUG
	int i;
#endif
	register struct pollfd	*p;
	fd_set cleanfds;

	for (;;) {
		p = pollfds;
		p->fd = terminate_rfd;
		p->events = MASKVAL;
		p++;
		for (n = 0; n <= svc_maxfd; n++) {
			if (FD_ISSET(n, &svc_fdset)) {
				p->fd = n;
				p->events = MASKVAL;
				p++;
			}
		}
		nfds = p - pollfds;
		poll_ret = 0;
#ifdef SVC_RUN_DEBUG
		if (debugging) {
			fprintf(stderr, "polling for read on fd < ");
			for (i = 0, p = pollfds; i < nfds; i++, p++)
				if (p->events)
					fprintf(stderr, "%d ", p->fd);
			fprintf(stderr, ">\n");
		}
#endif
		poll_ret = poll(pollfds, nfds, 30 * 1000);

		if (doterminate != 0) {
			close(rpcbindlockfd);
#ifdef WARMSTART
			syslog(LOG_ERR,
			    "rpcbind terminating on signal %d. Restart with \"rpcbind -w\"",
			    (int)doterminate);
			write_warmstart();	/* Dump yourself */
#endif
			exit(2);
		}

		switch (poll_ret) {
		case -1:
			/*
			 * We ignore all errors, continuing with the assumption
			 * that it was set by the signal handlers (or any
			 * other outside event) and not caused by poll().
			 */
		case 0:
			cleanfds = svc_fdset;
			__svc_clean_idle(&cleanfds, 30, FALSE);
			continue;
		default:
#ifdef SVC_RUN_DEBUG
			if (debugging) {
				fprintf(stderr, "poll returned read fds < ");
				for (i = 0, p = pollfds; i < nfds; i++, p++)
					if (p->revents)
						fprintf(stderr, "%d ", p->fd);
				fprintf(stderr, ">\n");
			}
#endif
			/*
			 * If we found as many replies on callback fds
			 * as the number of descriptors selectable which
			 * poll() returned, there can be no more so we
			 * don't call svc_getreq_poll.  Otherwise, there
			 * must be another so we must call svc_getreq_poll.
			 */
			if ((check_ret = check_rmtcalls(pollfds, nfds)) ==
			    poll_ret)
				continue;
			svc_getreq_poll(pollfds, poll_ret-check_ret);
		}
#ifdef SVC_RUN_DEBUG
		if (debugging) {
			fprintf(stderr, "svc_maxfd now %u\n", svc_maxfd);
		}
#endif
	}
}

static int
check_rmtcalls(struct pollfd *pfds, int nfds)
{
	int j, ncallbacks_found = 0, rmtcalls_pending;
	SVCXPRT *xprt;

	if (rpcb_rmtcalls == 0)
		return (0);

	rmtcalls_pending = rpcb_rmtcalls;
	for (j = 0; j < nfds; j++) {
		if ((xprt = find_rmtcallxprt_by_fd(pfds[j].fd)) != NULL) {
			if (pfds[j].revents) {
				ncallbacks_found++;
#ifdef DEBUG_RMTCALL
			if (debugging)
				fprintf(stderr,
"my_svc_run:  polled on forwarding fd %d, netid %s - calling handle_reply\n",
		pfds[j].fd, xprt->xp_netid);
#endif
				handle_reply(pfds[j].fd, xprt);
				pfds[j].revents = 0;
				if (ncallbacks_found >= rmtcalls_pending) {
					break;
				}
			}
		}
	}
	return (ncallbacks_found);
}

static void
xprt_set_caller(SVCXPRT *xprt, struct finfo *fi)
{
	u_int32_t *xidp;

	netbuf_copybuf(svc_getrpccaller(xprt), fi->caller_addr);
	xidp = __rpcb_get_dg_xidp(xprt);
	*xidp = fi->caller_xid;
}

/*
 * Call svcerr_systemerr() only if RPCBVERS4
 */
static void
send_svcsyserr(SVCXPRT *xprt, struct finfo *fi)
{
	if (fi->reply_type == RPCBPROC_INDIRECT) {
		xprt_set_caller(xprt, fi);
		svcerr_systemerr(xprt);
	}
	return;
}

static void
handle_reply(int fd, SVCXPRT *xprt)
{
	XDR		reply_xdrs;
	struct rpc_msg	reply_msg;
	struct rpc_err	reply_error;
	char		*buffer;
	struct finfo	*fi;
	int		inlen, pos, len;
	struct r_rmtcall_args a;
	struct sockaddr_storage ss;
	socklen_t fromlen;
#ifdef SVC_RUN_DEBUG
	char *uaddr;
#endif

	buffer = malloc(RPC_BUF_MAX);
	if (buffer == NULL)
		goto done;

	do {
		fromlen = sizeof(ss);
		inlen = recvfrom(fd, buffer, RPC_BUF_MAX, 0,
			    (struct sockaddr *)&ss, &fromlen);
	} while (inlen < 0 && errno == EINTR);
	if (inlen < 0) {
		if (debugging)
			fprintf(stderr,
	"handle_reply:  recvfrom returned %d, errno %d\n", inlen, errno);
		goto done;
	}

	reply_msg.acpted_rply.ar_verf = _null_auth;
	reply_msg.acpted_rply.ar_results.where = 0;
	reply_msg.acpted_rply.ar_results.proc = (xdrproc_t) xdr_void;

	xdrmem_create(&reply_xdrs, buffer, (u_int)inlen, XDR_DECODE);
	if (!xdr_replymsg(&reply_xdrs, &reply_msg)) {
		if (debugging)
			(void) fprintf(stderr,
				"handle_reply:  xdr_replymsg failed\n");
		goto done;
	}
	fi = forward_find(reply_msg.rm_xid);
#ifdef	SVC_RUN_DEBUG
	if (debugging) {
		fprintf(stderr, "handle_reply:  reply xid: %d fi addr: %p\n",
			reply_msg.rm_xid, fi);
	}
#endif
	if (fi == NULL) {
		goto done;
	}
	_seterr_reply(&reply_msg, &reply_error);
	if (reply_error.re_status != RPC_SUCCESS) {
		if (debugging)
			(void) fprintf(stderr, "handle_reply:  %s\n",
				clnt_sperrno(reply_error.re_status));
		send_svcsyserr(xprt, fi);
		goto done;
	}
	pos = XDR_GETPOS(&reply_xdrs);
	len = inlen - pos;
	a.rmt_args.args = &buffer[pos];
	a.rmt_args.arglen = len;
	a.rmt_uaddr = fi->uaddr;
	a.rmt_localvers = fi->versnum;

	xprt_set_caller(xprt, fi);
#ifdef	SVC_RUN_DEBUG
	uaddr =	taddr2uaddr(rpcbind_get_conf("udp"),
				    svc_getrpccaller(xprt));
	if (debugging) {
		fprintf(stderr, "handle_reply:  forwarding address %s to %s\n",
			a.rmt_uaddr, uaddr ? uaddr : "unknown");
	}
	free(uaddr);
#endif
	svc_sendreply(xprt, (xdrproc_t) xdr_rmtcall_result, (char *) &a);
done:
	free(buffer);

	if (reply_msg.rm_xid == 0) {
#ifdef	SVC_RUN_DEBUG
	if (debugging) {
		fprintf(stderr, "handle_reply:  NULL xid on exit!\n");
	}
#endif
	} else
		(void) free_slot_by_xid(reply_msg.rm_xid);
	return;
}

static void
find_versions(rpcprog_t prog, char *netid, rpcvers_t *lowvp, rpcvers_t *highvp)
{
	register rpcblist_ptr rbl;
	unsigned int lowv = 0;
	unsigned int highv = 0;

	for (rbl = list_rbl; rbl != NULL; rbl = rbl->rpcb_next) {
		if ((rbl->rpcb_map.r_prog != prog) ||
		    ((rbl->rpcb_map.r_netid != NULL) &&
			(strcasecmp(rbl->rpcb_map.r_netid, netid) != 0)))
			continue;
		if (lowv == 0) {
			highv = rbl->rpcb_map.r_vers;
			lowv = highv;
		} else if (rbl->rpcb_map.r_vers < lowv) {
			lowv = rbl->rpcb_map.r_vers;
		} else if (rbl->rpcb_map.r_vers > highv) {
			highv = rbl->rpcb_map.r_vers;
		}
	}
	*lowvp = lowv;
	*highvp = highv;
	return;
}

/*
 * returns the item with the given program, version number and netid.
 * If that version number is not found, it returns the item with that
 * program number, so that address is now returned to the caller. The
 * caller when makes a call to this program, version number, the call
 * will fail and it will return with PROGVERS_MISMATCH. The user can
 * then determine the highest and the lowest version number for this
 * program using clnt_geterr() and use those program version numbers.
 *
 * Returns the RPCBLIST for the given prog, vers and netid
 */
static rpcblist_ptr
find_service(rpcprog_t prog, rpcvers_t vers, char *netid)
{
	register rpcblist_ptr hit = NULL;
	register rpcblist_ptr rbl;

	for (rbl = list_rbl; rbl != NULL; rbl = rbl->rpcb_next) {
		if ((rbl->rpcb_map.r_prog != prog) ||
		    ((rbl->rpcb_map.r_netid != NULL) &&
			(strcasecmp(rbl->rpcb_map.r_netid, netid) != 0)))
			continue;
		hit = rbl;
		if (rbl->rpcb_map.r_vers == vers)
			break;
	}
	return (hit);
}

/*
 * Copies the name associated with the uid of the caller and returns
 * a pointer to it.  Similar to getwd().
 */
static char *
getowner(SVCXPRT *transp, char *owner, size_t ownersize)
{
	uid_t uid;
 
	if (__rpc_get_local_uid(transp, &uid) < 0)
                strlcpy(owner, "unknown", ownersize);
	else if (uid == 0)
		strlcpy(owner, "superuser", ownersize);
	else
		snprintf(owner, ownersize, "%d", uid);  

	return owner;
}

#ifdef PORTMAP
/*
 * Add this to the pmap list only if it is UDP or TCP.
 */
static int
add_pmaplist(RPCB *arg)
{
	struct pmap pmap;
	struct pmaplist *pml;
	int h1, h2, h3, h4, p1, p2;

	if (strcmp(arg->r_netid, udptrans) == 0) {
		/* It is UDP! */
		pmap.pm_prot = IPPROTO_UDP;
	} else if (strcmp(arg->r_netid, tcptrans) == 0) {
		/* It is TCP */
		pmap.pm_prot = IPPROTO_TCP;
	} else
		/* Not an IP protocol */
		return (0);

	/* interpret the universal address for TCP/IP */
	if (sscanf(arg->r_addr, "%d.%d.%d.%d.%d.%d",
		&h1, &h2, &h3, &h4, &p1, &p2) != 6)
		return (0);
	pmap.pm_port = ((p1 & 0xff) << 8) + (p2 & 0xff);
	pmap.pm_prog = arg->r_prog;
	pmap.pm_vers = arg->r_vers;
	/*
	 * add to END of list
	 */
	pml = malloc(sizeof (struct pmaplist));
	if (pml == NULL) {
		(void) syslog(LOG_ERR, "rpcbind: no memory!\n");
		return (1);
	}
	pml->pml_map = pmap;
	pml->pml_next = NULL;
	if (list_pml == NULL) {
		list_pml = pml;
	} else {
		struct pmaplist *fnd;

		/* Attach to the end of the list */
		for (fnd = list_pml; fnd->pml_next; fnd = fnd->pml_next)
			;
		fnd->pml_next = pml;
	}
	return (0);
}

/*
 * Delete this from the pmap list only if it is UDP or TCP.
 */
static int
del_pmaplist(RPCB *arg)
{
	struct pmaplist *pml;
	struct pmaplist *prevpml, *fnd;
	unsigned long prot;

	if (strcmp(arg->r_netid, udptrans) == 0) {
		/* It is UDP! */
		prot = IPPROTO_UDP;
	} else if (strcmp(arg->r_netid, tcptrans) == 0) {
		/* It is TCP */
		prot = IPPROTO_TCP;
	} else if (arg->r_netid[0] == 0) {
		prot = 0;	/* Remove all occurrences */
	} else {
		/* Not an IP protocol */
		return (0);
	}
	for (prevpml = NULL, pml = list_pml; pml; /* cstyle */) {
		if ((pml->pml_map.pm_prog != arg->r_prog) ||
			(pml->pml_map.pm_vers != arg->r_vers) ||
			(prot && (pml->pml_map.pm_prot != prot))) {
			/* both pml & prevpml move forwards */
			prevpml = pml;
			pml = pml->pml_next;
			continue;
		}
		/* found it; pml moves forward, prevpml stays */
		fnd = pml;
		pml = pml->pml_next;
		if (prevpml == NULL)
			list_pml = pml;
		else
			prevpml->pml_next = pml;
		free(fnd);
	}
	return (0);
}
#endif /* PORTMAP */
