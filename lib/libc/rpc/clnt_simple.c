/*	$OpenBSD: clnt_simple.c,v 1.18 2015/08/20 21:49:29 deraadt Exp $ */

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
 * clnt_simple.c
 * Simplified front end to rpc.
 */

#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <rpc/rpc.h>

static struct callrpc_private {
	CLIENT	*client;
	int	socket;
	int	oldprognum, oldversnum, valid;
	char	*oldhost;
} *callrpc_private;

int
callrpc(char *host, int prognum, int versnum, int procnum, xdrproc_t inproc,
    char *in, xdrproc_t outproc, char *out)
{
	struct callrpc_private *save_callrpc_private = callrpc_private;
	struct callrpc_private *crp = callrpc_private;
	struct sockaddr_in server_addr;
	enum clnt_stat clnt_stat;
	struct hostent *hp;
	struct timeval timeout, tottimeout;

	if (crp == NULL) {
		crp = calloc(1, sizeof (*crp));
		if (crp == NULL)
			return RPC_SYSTEMERROR;
		callrpc_private = crp;
	}
	if (crp->oldhost == NULL) {
		crp->oldhost = malloc(HOST_NAME_MAX+1);
		if (crp->oldhost == NULL) {
			free(crp);
			callrpc_private = save_callrpc_private;
			return RPC_SYSTEMERROR;
		}
		crp->oldhost[0] = 0;
		crp->socket = RPC_ANYSOCK;
	}
	if (crp->valid && crp->oldprognum == prognum && crp->oldversnum == versnum
		&& strcmp(crp->oldhost, host) == 0) {
		/* reuse old client */		
	} else {
		crp->valid = 0;
		if (crp->socket != -1) {
			(void)close(crp->socket);
			crp->socket = -1;
		}
		if (crp->client) {
			CLNT_DESTROY(crp->client);
			crp->client = NULL;
		}
		crp->socket = RPC_ANYSOCK;
		if ((hp = gethostbyname(host)) == NULL)
			return ((int) RPC_UNKNOWNHOST);
		timeout.tv_usec = 0;
		timeout.tv_sec = 5;
		memset(&server_addr, 0, sizeof(server_addr));
		memcpy((char *)&server_addr.sin_addr, hp->h_addr, hp->h_length);
		server_addr.sin_len = sizeof(struct sockaddr_in);
		server_addr.sin_family = AF_INET;
		server_addr.sin_port =  0;
		if ((crp->client = clntudp_create(&server_addr, (u_long)prognum,
		    (u_long)versnum, timeout, &crp->socket)) == NULL)
			return ((int) rpc_createerr.cf_stat);
		crp->valid = 1;
		crp->oldprognum = prognum;
		crp->oldversnum = versnum;
		strlcpy(crp->oldhost, host, HOST_NAME_MAX+1);
	}
	tottimeout.tv_sec = 25;
	tottimeout.tv_usec = 0;
	clnt_stat = clnt_call(crp->client, procnum, inproc, in,
	    outproc, out, tottimeout);
	/* 
	 * if call failed, empty cache
	 */
	if (clnt_stat != RPC_SUCCESS)
		crp->valid = 0;
	return ((int) clnt_stat);
}
