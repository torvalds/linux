/*	$NetBSD: clnt_simple.c,v 1.21 2000/07/06 03:10:34 christos Exp $	*/

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
static char *sccsid2 = "from: @(#)clnt_simple.c 1.35 87/08/11 Copyr 1984 Sun Micro";
static char *sccsid = "from: @(#)clnt_simple.c	2.2 88/08/01 4.0 RPCSRC";
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * clnt_simple.c
 * Simplified front end to client rpc.
 *
 */

#include "namespace.h"
#include "reentrant.h"
#include <sys/param.h>
#include <stdio.h>
#include <errno.h>
#include <rpc/rpc.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include "un-namespace.h"
#include "mt_misc.h"

#ifndef MAXHOSTNAMELEN
#define	MAXHOSTNAMELEN 64
#endif

#ifndef NETIDLEN
#define	NETIDLEN 32
#endif

struct rpc_call_private {
	int	valid;			/* Is this entry valid ? */
	CLIENT	*client;		/* Client handle */
	pid_t	pid;			/* process-id at moment of creation */
	rpcprog_t prognum;		/* Program */
	rpcvers_t versnum;		/* Version */
	char	host[MAXHOSTNAMELEN];	/* Servers host */
	char	nettype[NETIDLEN];	/* Network type */
};
static struct rpc_call_private *rpc_call_private_main;
static thread_key_t rpc_call_key;
static once_t rpc_call_once = ONCE_INITIALIZER;
static int rpc_call_key_error;

static void rpc_call_key_init(void);
static void rpc_call_destroy(void *);

static void
rpc_call_destroy(void *vp)
{
	struct rpc_call_private *rcp = (struct rpc_call_private *)vp;

	if (rcp) {
		if (rcp->client)
			CLNT_DESTROY(rcp->client);
		free(rcp);
	}
}

static void
rpc_call_key_init(void)
{

	rpc_call_key_error = thr_keycreate(&rpc_call_key, rpc_call_destroy);
}

/*
 * This is the simplified interface to the client rpc layer.
 * The client handle is not destroyed here and is reused for
 * the future calls to same prog, vers, host and nettype combination.
 *
 * The total time available is 25 seconds.
 *
 * host    - host name 
 * prognum - program number 
 * versnum - version number 
 * procnum - procedure number 
 * inproc, outproc -  in/out XDR procedures 
 * in, out - recv/send data 
 * nettype - nettype
 */
enum clnt_stat
rpc_call(const char *host, const rpcprog_t prognum, const rpcvers_t versnum,
    const rpcproc_t procnum, const xdrproc_t inproc, const char *in,
    const xdrproc_t outproc, char *out, const char *nettype)
{
	struct rpc_call_private *rcp = (struct rpc_call_private *) 0;
	enum clnt_stat clnt_stat;
	struct timeval timeout, tottimeout;
	int main_thread = 1;

	if ((main_thread = thr_main())) {
		rcp = rpc_call_private_main;
	} else {
		if (thr_once(&rpc_call_once, rpc_call_key_init) != 0 ||
		    rpc_call_key_error != 0) {
			rpc_createerr.cf_stat = RPC_SYSTEMERROR;
			rpc_createerr.cf_error.re_errno = rpc_call_key_error;
			return (rpc_createerr.cf_stat);
		}
		rcp = (struct rpc_call_private *)thr_getspecific(rpc_call_key);
	}
	if (rcp == NULL) {
		rcp = malloc(sizeof (*rcp));
		if (rcp == NULL) {
			rpc_createerr.cf_stat = RPC_SYSTEMERROR;
			rpc_createerr.cf_error.re_errno = errno;
			return (rpc_createerr.cf_stat);
		}
		if (main_thread)
			rpc_call_private_main = rcp;
		else
			thr_setspecific(rpc_call_key, (void *) rcp);
		rcp->valid = 0;
		rcp->client = NULL;
	}
	if ((nettype == NULL) || (nettype[0] == 0))
		nettype = "netpath";
	if (!(rcp->valid && rcp->pid == getpid() &&
		(rcp->prognum == prognum) &&
		(rcp->versnum == versnum) &&
		(!strcmp(rcp->host, host)) &&
		(!strcmp(rcp->nettype, nettype)))) {
		int fd;

		rcp->valid = 0;
		if (rcp->client)
			CLNT_DESTROY(rcp->client);
		/*
		 * Using the first successful transport for that type
		 */
		rcp->client = clnt_create(host, prognum, versnum, nettype);
		rcp->pid = getpid();
		if (rcp->client == NULL) {
			return (rpc_createerr.cf_stat);
		}
		/*
		 * Set time outs for connectionless case.  Do it
		 * unconditionally.  Faster than doing a t_getinfo()
		 * and then doing the right thing.
		 */
		timeout.tv_usec = 0;
		timeout.tv_sec = 5;
		(void) CLNT_CONTROL(rcp->client,
				CLSET_RETRY_TIMEOUT, (char *)(void *)&timeout);
		if (CLNT_CONTROL(rcp->client, CLGET_FD, (char *)(void *)&fd))
			_fcntl(fd, F_SETFD, 1);	/* make it "close on exec" */
		rcp->prognum = prognum;
		rcp->versnum = versnum;
		if ((strlen(host) < (size_t)MAXHOSTNAMELEN) &&
		    (strlen(nettype) < (size_t)NETIDLEN)) {
			(void) strcpy(rcp->host, host);
			(void) strcpy(rcp->nettype, nettype);
			rcp->valid = 1;
		} else {
			rcp->valid = 0;
		}
	} /* else reuse old client */
	tottimeout.tv_sec = 25;
	tottimeout.tv_usec = 0;
	/*LINTED const castaway*/
	clnt_stat = CLNT_CALL(rcp->client, procnum, inproc, (char *) in,
	    outproc, out, tottimeout);
	/*
	 * if call failed, empty cache
	 */
	if (clnt_stat != RPC_SUCCESS)
		rcp->valid = 0;
	return (clnt_stat);
}
