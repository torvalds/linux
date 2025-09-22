/*	$OpenBSD: ypset.c,v 1.21 2024/08/27 06:04:03 florian Exp $ */
/*	$NetBSD: ypset.c,v 1.8 1996/05/13 02:46:33 thorpej Exp $	*/

/*
 * Copyright (c) 1992, 1993 Theo de Raadt <deraadt@theos.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>

#include <rpc/rpc.h>
#include <rpc/xdr.h>
#include <rpcsvc/yp.h>
#include <rpcsvc/ypclnt.h>
#include <arpa/inet.h>

static void
usage(void)
{
	fprintf(stderr, "usage: ypset [-d domain] [-h host] server\n");
	exit(1);
}

static int
bind_tohost(struct sockaddr_in *sin, char *dom, char *server)
{
	struct ypbind_setdom ypsd;
	struct in_addr iaddr;
	struct addrinfo hints, *res;
	struct timeval tv;
	CLIENT *client;
	int sock, port, r;

	port = getrpcport(server, YPPROG, YPPROC_NULL, IPPROTO_UDP);
	if (port == 0)
		errx(1, "%s not running ypserv", server);
	port = htons(port);

	memset(&ypsd, 0, sizeof ypsd);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;

	if (getaddrinfo(server, NULL, &hints, &res) != 0)
		errx(1, "can't find address for %s", server);
	iaddr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
	freeaddrinfo(res);

	ypsd.ypsetdom_domain = dom;
	bcopy(&iaddr.s_addr, &ypsd.ypsetdom_binding.ypbind_binding_addr,
	    sizeof(ypsd.ypsetdom_binding.ypbind_binding_addr));
	bcopy(&port, &ypsd.ypsetdom_binding.ypbind_binding_port,
	    sizeof(ypsd.ypsetdom_binding.ypbind_binding_port));
	ypsd.ypsetdom_vers = YPVERS;

	tv.tv_sec = 15;
	tv.tv_usec = 0;
	sock = RPC_ANYSOCK;
	client = clntudp_create(sin, YPBINDPROG, YPBINDVERS, tv, &sock);
	if (client == NULL) {
		warnx("can't yp_bind: reason: %s", yperr_string(YPERR_YPBIND));
		return YPERR_YPBIND;
	}
	client->cl_auth = authunix_create_default();

	r = clnt_call(client, YPBINDPROC_SETDOM,
	    xdr_ypbind_setdom, &ypsd, xdr_void, NULL, tv);
	if (r) {
		warnx("Cannot ypset for domain %s on host %s: %s", dom,
		    server, clnt_sperrno(r));
		clnt_destroy(client);
		return YPERR_YPBIND;
	}
	clnt_destroy(client);
	return 0;
}

int
main(int argc, char *argv[])
{
	struct sockaddr_in sin;
	struct addrinfo hints, *res;
	extern char *optarg;
	extern int optind;
	char *domainname;
	int c;

	yp_get_default_domain(&domainname);

	bzero(&sin, sizeof sin);
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	while ((c = getopt(argc, argv, "h:d:")) != -1)
		switch(c) {
		case 'd':
			domainname = optarg;
			break;
		case 'h':
			memset(&hints, 0, sizeof(hints));
			hints.ai_family = AF_INET;

			if (getaddrinfo(optarg, NULL, &hints, &res) != 0)
				errx(1, "host %s unknown\n", optarg);

			sin.sin_addr =
			    ((struct sockaddr_in *)res->ai_addr)->sin_addr;
			freeaddrinfo(res);
			break;
		default:
			usage();
		}

	if (optind + 1 != argc)
		usage();

	if (bind_tohost(&sin, domainname, argv[optind]))
		exit(1);
	exit(0);
}
