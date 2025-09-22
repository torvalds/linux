/*	$OpenBSD: clnt_udp_bufcreate.c,v 1.1 2024/01/22 16:18:06 deraadt Exp $ */

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
 * clnt_udp.c, Implements a UDP/IP based, client side RPC.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <rpc/rpc.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <rpc/pmap_clnt.h>
#include "clnt_udp.h"

/*
 * Create a UDP based client handle.
 * If *sockp<0, *sockp is set to a newly created UPD socket.
 * If raddr->sin_port is 0 a binder on the remote machine
 * is consulted for the correct port number.
 * NB: It is the client's responsibility to close *sockp, unless
 *	clntudp_bufcreate() was called with *sockp = -1 (so it created
 *	the socket), and CLNT_DESTROY() is used.
 * NB: The rpch->cl_auth is initialized to null authentication.
 *     Caller may wish to set this something more useful.
 *
 * wait is the amount of time used between retransmitting a call if
 * no response has been heard;  retransmission occurs until the actual
 * rpc call times out.
 *
 * sendsz and recvsz are the maximum allowable packet sizes that can be
 * sent and received.
 */

CLIENT *
clntudp_bufcreate(struct sockaddr_in *raddr, u_long program, u_long version,
    struct timeval wait, int *sockp, u_int sendsz, u_int recvsz)
{
	struct clntudp_bufcreate_args args;

	args.raddr = raddr;
	args.program = program;
	args.version = version;
	args.wait = wait;
	args.sockp = sockp;
	args.sendsz = sendsz;
	args.recvsz = recvsz;

	if (clntudp_bufcreate1(&args) == -1)
		goto fooy;

	if (raddr->sin_port == 0) {
		u_short port;
		if ((port =
		    pmap_getport(raddr, program, version, IPPROTO_UDP)) == 0) {
			goto fooy;
		}
		raddr->sin_port = htons(port);
	}
	args.cu->cu_raddr = *raddr;
	if (*sockp < 0) {
		*sockp = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK,
		    IPPROTO_UDP);
		if (*sockp == -1) {
			rpc_createerr.cf_stat = RPC_SYSTEMERROR;
			rpc_createerr.cf_error.re_errno = errno;
			goto fooy;
		}
		/* attempt to bind to priv port */
		(void)bindresvport(*sockp, NULL);
		args.cu->cu_closeit = TRUE;
	}
	args.cu->cu_sock = *args.sockp;

	if (clntudp_bufcreate2(&args) == -1)
		goto fooy;
	return (args.cl);
fooy:
	if (args.cu)
		mem_free((caddr_t)args.cu,
		    sizeof(*args.cu) + args.sendsz + args.recvsz);
	if (args.cl)
		mem_free((caddr_t)args.cl, sizeof(CLIENT));
	return (NULL);
}
DEF_WEAK(clntudp_bufcreate);

CLIENT *
clntudp_create(struct sockaddr_in *raddr, u_long program, u_long version,
    struct timeval wait, int *sockp)
{

	return(clntudp_bufcreate(raddr, program, version, wait, sockp,
	    UDPMSGSIZE, UDPMSGSIZE));
}
DEF_WEAK(clntudp_create);
