/*	$NetBSD: clnt_generic.c,v 1.18 2000/07/06 03:10:34 christos Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2010, Oracle America, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of the "Oracle America, Inc." nor the names of its
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

/* #ident	"@(#)clnt_generic.c	1.40	99/04/21 SMI" */

#if defined(LIBC_SCCS) && !defined(lint)
static char *sccsid2 = "from: @(#)clnt_generic.c 1.4 87/08/11 (C) 1987 SMI";
static char *sccsid = "from: @(#)clnt_generic.c	2.2 88/08/01 4.0 RPCSRC";
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Copyright (c) 1986-1996,1998 by Sun Microsystems, Inc.
 * All rights reserved.
 */
#include "namespace.h"
#include "reentrant.h"
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <errno.h>
#include <netdb.h>
#include <syslog.h>
#include <rpc/rpc.h>
#include <rpc/nettype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "un-namespace.h"
#include "rpc_com.h"

extern bool_t __rpc_is_local_host(const char *);
int __rpc_raise_fd(int);

#ifndef NETIDLEN
#define	NETIDLEN 32
#endif


/*
 * Generic client creation with version checking the value of
 * vers_out is set to the highest server supported value
 * vers_low <= vers_out <= vers_high  AND an error results
 * if this can not be done.
 *
 * It calls clnt_create_vers_timed() with a NULL value for the timeout
 * pointer, which indicates that the default timeout should be used.
 */
CLIENT *
clnt_create_vers(const char *hostname, rpcprog_t prog, rpcvers_t *vers_out,
	rpcvers_t vers_low, rpcvers_t vers_high, const char *nettype)
{

	return (clnt_create_vers_timed(hostname, prog, vers_out, vers_low,
				vers_high, nettype, NULL));
}

/*
 * This the routine has the same definition as clnt_create_vers(),
 * except it takes an additional timeout parameter - a pointer to
 * a timeval structure.  A NULL value for the pointer indicates
 * that the default timeout value should be used.
 */
CLIENT *
clnt_create_vers_timed(const char *hostname, rpcprog_t prog,
    rpcvers_t *vers_out, rpcvers_t vers_low, rpcvers_t vers_high,
    const char *nettype, const struct timeval *tp)
{
	CLIENT *clnt;
	struct timeval to;
	enum clnt_stat rpc_stat;
	struct rpc_err rpcerr;

	clnt = clnt_create_timed(hostname, prog, vers_high, nettype, tp);
	if (clnt == NULL) {
		return (NULL);
	}
	to.tv_sec = 10;
	to.tv_usec = 0;
	rpc_stat = clnt_call(clnt, NULLPROC, (xdrproc_t)xdr_void,
			(char *)NULL, (xdrproc_t)xdr_void, (char *)NULL, to);
	if (rpc_stat == RPC_SUCCESS) {
		*vers_out = vers_high;
		return (clnt);
	}
	while (rpc_stat == RPC_PROGVERSMISMATCH && vers_high > vers_low) {
		unsigned int minvers, maxvers;

		clnt_geterr(clnt, &rpcerr);
		minvers = rpcerr.re_vers.low;
		maxvers = rpcerr.re_vers.high;
		if (maxvers < vers_high)
			vers_high = maxvers;
		else
			vers_high--;
		if (minvers > vers_low)
			vers_low = minvers;
		if (vers_low > vers_high) {
			goto error;
		}
		CLNT_CONTROL(clnt, CLSET_VERS, (char *)&vers_high);
		rpc_stat = clnt_call(clnt, NULLPROC, (xdrproc_t)xdr_void,
				(char *)NULL, (xdrproc_t)xdr_void,
				(char *)NULL, to);
		if (rpc_stat == RPC_SUCCESS) {
			*vers_out = vers_high;
			return (clnt);
		}
	}
	clnt_geterr(clnt, &rpcerr);

error:
	rpc_createerr.cf_stat = rpc_stat;
	rpc_createerr.cf_error = rpcerr;
	clnt_destroy(clnt);
	return (NULL);
}

/*
 * Top level client creation routine.
 * Generic client creation: takes (servers name, program-number, nettype) and
 * returns client handle. Default options are set, which the user can
 * change using the rpc equivalent of _ioctl()'s.
 *
 * It tries for all the netids in that particular class of netid until
 * it succeeds.
 * XXX The error message in the case of failure will be the one
 * pertaining to the last create error.
 *
 * It calls clnt_create_timed() with the default timeout.
 */
CLIENT *
clnt_create(const char *hostname, rpcprog_t prog, rpcvers_t vers,
    const char *nettype)
{

	return (clnt_create_timed(hostname, prog, vers, nettype, NULL));
}

/*
 * This the routine has the same definition as clnt_create(),
 * except it takes an additional timeout parameter - a pointer to
 * a timeval structure.  A NULL value for the pointer indicates
 * that the default timeout value should be used.
 *
 * This function calls clnt_tp_create_timed().
 */
CLIENT *
clnt_create_timed(const char *hostname, rpcprog_t prog, rpcvers_t vers,
    const char *netclass, const struct timeval *tp)
{
	struct netconfig *nconf;
	CLIENT *clnt = NULL;
	void *handle;
	enum clnt_stat	save_cf_stat = RPC_SUCCESS;
	struct rpc_err	save_cf_error;
	char nettype_array[NETIDLEN];
	char *nettype = &nettype_array[0];

	if (netclass == NULL)
		nettype = NULL;
	else {
		size_t len = strlen(netclass);
		if (len >= sizeof (nettype_array)) {
			rpc_createerr.cf_stat = RPC_UNKNOWNPROTO;
			return (NULL);
		}
		strcpy(nettype, netclass);
	}

	if ((handle = __rpc_setconf((char *)nettype)) == NULL) {
		rpc_createerr.cf_stat = RPC_UNKNOWNPROTO;
		return (NULL);
	}
	rpc_createerr.cf_stat = RPC_SUCCESS;
	while (clnt == NULL) {
		if ((nconf = __rpc_getconf(handle)) == NULL) {
			if (rpc_createerr.cf_stat == RPC_SUCCESS)
				rpc_createerr.cf_stat = RPC_UNKNOWNPROTO;
			break;
		}
#ifdef CLNT_DEBUG
		printf("trying netid %s\n", nconf->nc_netid);
#endif
		clnt = clnt_tp_create_timed(hostname, prog, vers, nconf, tp);
		if (clnt)
			break;
		else
			/*
			 *	Since we didn't get a name-to-address
			 *	translation failure here, we remember
			 *	this particular error.  The object of
			 *	this is to enable us to return to the
			 *	caller a more-specific error than the
			 *	unhelpful ``Name to address translation
			 *	failed'' which might well occur if we
			 *	merely returned the last error (because
			 *	the local loopbacks are typically the
			 *	last ones in /etc/netconfig and the most
			 *	likely to be unable to translate a host
			 *	name).  We also check for a more
			 *	meaningful error than ``unknown host
			 *	name'' for the same reasons.
			 */
			if (rpc_createerr.cf_stat != RPC_N2AXLATEFAILURE &&
			    rpc_createerr.cf_stat != RPC_UNKNOWNHOST) {
				save_cf_stat = rpc_createerr.cf_stat;
				save_cf_error = rpc_createerr.cf_error;
			}
	}

	/*
	 *	Attempt to return an error more specific than ``Name to address
	 *	translation failed'' or ``unknown host name''
	 */
	if ((rpc_createerr.cf_stat == RPC_N2AXLATEFAILURE ||
				rpc_createerr.cf_stat == RPC_UNKNOWNHOST) &&
					(save_cf_stat != RPC_SUCCESS)) {
		rpc_createerr.cf_stat = save_cf_stat;
		rpc_createerr.cf_error = save_cf_error;
	}
	__rpc_endconf(handle);
	return (clnt);
}

/*
 * Generic client creation: takes (servers name, program-number, netconf) and
 * returns client handle. Default options are set, which the user can
 * change using the rpc equivalent of _ioctl()'s : clnt_control()
 * It finds out the server address from rpcbind and calls clnt_tli_create().
 *
 * It calls clnt_tp_create_timed() with the default timeout.
 */
CLIENT *
clnt_tp_create(const char *hostname, rpcprog_t prog, rpcvers_t vers,
    const struct netconfig *nconf)
{

	return (clnt_tp_create_timed(hostname, prog, vers, nconf, NULL));
}

/*
 * This has the same definition as clnt_tp_create(), except it
 * takes an additional parameter - a pointer to a timeval structure.
 * A NULL value for the timeout pointer indicates that the default
 * value for the timeout should be used.
 */
CLIENT *
clnt_tp_create_timed(const char *hostname, rpcprog_t prog, rpcvers_t vers,
    const struct netconfig *nconf, const struct timeval *tp)
{
	struct netbuf *svcaddr;			/* servers address */
	CLIENT *cl = NULL;			/* client handle */

	if (nconf == NULL) {
		rpc_createerr.cf_stat = RPC_UNKNOWNPROTO;
		return (NULL);
	}

	/*
	 * Get the address of the server
	 */
	if ((svcaddr = __rpcb_findaddr_timed(prog, vers,
			(struct netconfig *)nconf, (char *)hostname,
			&cl, (struct timeval *)tp)) == NULL) {
		/* appropriate error number is set by rpcbind libraries */
		return (NULL);
	}
	if (cl == NULL) {
		cl = clnt_tli_create(RPC_ANYFD, nconf, svcaddr,
					prog, vers, 0, 0);
	} else {
		/* Reuse the CLIENT handle and change the appropriate fields */
		if (CLNT_CONTROL(cl, CLSET_SVC_ADDR, (void *)svcaddr) == TRUE) {
			if (cl->cl_netid == NULL)
				cl->cl_netid = strdup(nconf->nc_netid);
			if (cl->cl_tp == NULL)
				cl->cl_tp = strdup(nconf->nc_device);
			(void) CLNT_CONTROL(cl, CLSET_PROG, (void *)&prog);
			(void) CLNT_CONTROL(cl, CLSET_VERS, (void *)&vers);
		} else {
			CLNT_DESTROY(cl);
			cl = clnt_tli_create(RPC_ANYFD, nconf, svcaddr,
					prog, vers, 0, 0);
		}
	}
	free(svcaddr->buf);
	free(svcaddr);
	return (cl);
}

/*
 * Generic client creation:  returns client handle.
 * Default options are set, which the user can
 * change using the rpc equivalent of _ioctl()'s : clnt_control().
 * If fd is RPC_ANYFD, it will be opened using nconf.
 * It will be bound if not so.
 * If sizes are 0; appropriate defaults will be chosen.
 */
CLIENT *
clnt_tli_create(int fd, const struct netconfig *nconf,
	struct netbuf *svcaddr, rpcprog_t prog, rpcvers_t vers,
	uint sendsz, uint recvsz)
{
	CLIENT *cl;			/* client handle */
	bool_t madefd = FALSE;		/* whether fd opened here */
	long servtype;
	int one = 1;
	struct __rpc_sockinfo si;
	extern int __rpc_minfd;

	if (fd == RPC_ANYFD) {
		if (nconf == NULL) {
			rpc_createerr.cf_stat = RPC_UNKNOWNPROTO;
			return (NULL);
		}

		fd = __rpc_nconf2fd(nconf);

		if (fd == -1)
			goto err;
		if (fd < __rpc_minfd)
			fd = __rpc_raise_fd(fd);
		madefd = TRUE;
		servtype = nconf->nc_semantics;
		if (!__rpc_fd2sockinfo(fd, &si))
			goto err;
		bindresvport(fd, NULL);
	} else {
		if (!__rpc_fd2sockinfo(fd, &si))
			goto err;
		servtype = __rpc_socktype2seman(si.si_socktype);
		if (servtype == -1) {
			rpc_createerr.cf_stat = RPC_UNKNOWNPROTO;
			return (NULL);
		}
	}

	if (si.si_af != ((struct sockaddr *)svcaddr->buf)->sa_family) {
		rpc_createerr.cf_stat = RPC_UNKNOWNHOST;	/* XXX */
		goto err1;
	}

	switch (servtype) {
	case NC_TPI_COTS:
		cl = clnt_vc_create(fd, svcaddr, prog, vers, sendsz, recvsz);
		break;
	case NC_TPI_COTS_ORD:
		if (nconf && ((strcmp(nconf->nc_protofmly, "inet") == 0))) {
			_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one,
			    sizeof (one));
		}
		cl = clnt_vc_create(fd, svcaddr, prog, vers, sendsz, recvsz);
		break;
	case NC_TPI_CLTS:
		cl = clnt_dg_create(fd, svcaddr, prog, vers, sendsz, recvsz);
		break;
	default:
		goto err;
	}

	if (cl == NULL)
		goto err1; /* borrow errors from clnt_dg/vc creates */
	if (nconf) {
		cl->cl_netid = strdup(nconf->nc_netid);
		cl->cl_tp = strdup(nconf->nc_device);
	} else {
		cl->cl_netid = "";
		cl->cl_tp = "";
	}
	if (madefd) {
		(void) CLNT_CONTROL(cl, CLSET_FD_CLOSE, NULL);
/*		(void) CLNT_CONTROL(cl, CLSET_POP_TIMOD, NULL);  */
	}

	return (cl);

err:
	rpc_createerr.cf_stat = RPC_SYSTEMERROR;
	rpc_createerr.cf_error.re_errno = errno;
err1:	if (madefd)
		(void)_close(fd);
	return (NULL);
}

/*
 *  To avoid conflicts with the "magic" file descriptors (0, 1, and 2),
 *  we try to not use them.  The __rpc_raise_fd() routine will dup
 *  a descriptor to a higher value.  If we fail to do it, we continue
 *  to use the old one (and hope for the best).
 */
int __rpc_minfd = 3;

int
__rpc_raise_fd(int fd)
{
	int nfd;

	if (fd >= __rpc_minfd)
		return (fd);

	if ((nfd = _fcntl(fd, F_DUPFD, __rpc_minfd)) == -1)
		return (fd);

	if (_fsync(nfd) == -1) {
		_close(nfd);
		return (fd);
	}

	if (_close(fd) == -1) {
		/* this is okay, we will syslog an error, then use the new fd */
		(void) syslog(LOG_ERR,
			"could not close() fd %d; mem & fd leak", fd);
	}

	return (nfd);
}
