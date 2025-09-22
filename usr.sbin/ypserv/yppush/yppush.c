/*	$OpenBSD: yppush.c,v 1.32 2024/05/20 02:00:25 jsg Exp $ */

/*
 * Copyright (c) 1995 Mats O Jansson <moj@stacken.kth.se>
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
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/wait.h>

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <ctype.h>

#include <rpc/rpc.h>
#include <rpc/xdr.h>
#include <rpcsvc/yp.h>
#include <rpcsvc/ypclnt.h>

#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include "yplib_host.h"
#include "ypdef.h"
#include "ypdb.h"

int  Verbose = 0;
char Domain[HOST_NAME_MAX+1], Map[255];
u_int32_t OrderNum;
char *master;

extern void yppush_xfrrespprog_1(struct svc_req *request, SVCXPRT *xprt);

static void
usage(void)
{
	fprintf(stderr,
	    "usage: yppush [-v] [-d domainname] [-h hostname] mapname\n");
	exit(1);
}

static void
my_svc_run(void)
{
	struct pollfd *pfd = NULL, *newp;
	int nready, saved_max_pollfd = 0;

	for (;;) {
		if (svc_max_pollfd > saved_max_pollfd) {
			newp = reallocarray(pfd, svc_max_pollfd, sizeof(*pfd));
			if (newp == NULL) {
				free(pfd);
				perror("svc_run: - realloc failed");
				return;
			}
			pfd = newp;
			saved_max_pollfd = svc_max_pollfd;
		}
		memcpy(pfd, svc_pollfd, sizeof(*pfd) * svc_max_pollfd);

		nready = poll(pfd, svc_max_pollfd, 60 * 1000);
		switch (nready) {
		case -1:
			if (errno == EINTR)
				continue;
			perror("yppush: my_svc_run: poll failed");
			free(pfd);
			return;
		case 0:
			fprintf(stderr, "yppush: Callback timed out.\n");
			exit(0);
		default:
			svc_getreq_poll(pfd, nready);
			break;
		}
	}
}

static void
req_xfr(pid_t pid, u_int prog, SVCXPRT *transp, char *host, CLIENT *client)
{
	struct ypreq_xfr request;
	struct timeval tv;

	tv.tv_sec = 0;
	tv.tv_usec = 0;

	request.map_parms.domain=(char *)&Domain;
	request.map_parms.map=(char *)&Map;
	request.map_parms.peer=master;
	request.map_parms.ordernum=OrderNum;
	request.transid=(u_int)pid;
	request.prog=prog;
	request.port=transp->xp_port;

	if (Verbose)
		printf("%d: %s(%u@%s) -> %s@%s\n",
		    request.transid, request.map_parms.map,
		    request.map_parms.ordernum, host,
		    request.map_parms.peer, request.map_parms.domain);
	switch (clnt_call(client, YPPROC_XFR, xdr_ypreq_xfr, &request,
	    xdr_void, NULL, tv)) {
	case RPC_SUCCESS:
	case RPC_TIMEDOUT:
		break;
	default:
		clnt_perror(client, "yppush: Cannot call YPPROC_XFR");
		kill(pid, SIGTERM);
		break;
	}
}

static void
push(int inlen, char *indata)
{
	char host[HOST_NAME_MAX+1];
	CLIENT *client;
	SVCXPRT *transp;
	int sock = RPC_ANYSOCK, status;
	u_int prog;
	bool_t sts = 0;
	pid_t pid;
	struct rusage res;

	snprintf(host, sizeof host, "%*.*s", inlen, inlen, indata);

	client = clnt_create(host, YPPROG, YPVERS, "tcp");
	if (client == NULL) {
		if (Verbose)
			fprintf(stderr, "Target Host: %s\n", host);
		clnt_pcreateerror("yppush: Cannot create client");
		return;
	}

	transp = svcudp_create(sock);
	if (transp == NULL) {
		fprintf(stderr, "yppush: Cannot create callback transport.\n");
		return;
	}
	if (transp->xp_port >= IPPORT_RESERVED) {
		SVC_DESTROY(transp);
		fprintf(stderr, "yppush: Cannot allocate reserved port.\n");
		return;
	}

	for (prog=0x40000000; prog<0x5fffffff; prog++) {
		if ((sts = svc_register(transp, prog, 1,
		    yppush_xfrrespprog_1, IPPROTO_UDP)))
			break;
	}

	if (!sts) {
		fprintf(stderr, "yppush: Cannot register callback.\n");
		return;
	}

	switch (pid=fork()) {
	case -1:
		fprintf(stderr, "yppush: Cannot fork.\n");
		exit(1);
	case 0:
		my_svc_run();
		exit(0);
	default:
		close(transp->xp_sock);
		transp->xp_sock = -1;
		req_xfr(pid, prog, transp, host, client);
		wait4(pid, &status, 0, &res);
		svc_unregister(prog, 1);
		if (client != NULL)
			clnt_destroy(client);
		/* XXX transp leak? */
	}

}

static int
pushit(u_long instatus, char *inkey, int inkeylen, char *inval, int invallen,
    void *indata)
{
	if (instatus != YP_TRUE)
		return instatus;
	push(invallen, inval);
	return 0;
}

int
main(int argc, char *argv[])
{
	struct ypall_callback ypcb;
	extern char *optarg;
	extern int optind;
	char	*domain, *map, *hostname;
	int c, r, i;
	char *ypmap = "ypservers";
	CLIENT *client;
	static char map_path[PATH_MAX];
	struct stat finfo;
	DBM *yp_databas;
	char order_key[YP_LAST_LEN] = YP_LAST_KEY;
	datum o;

	yp_get_default_domain(&domain);
	hostname = NULL;
	while ((c=getopt(argc, argv, "d:h:v")) != -1)
		switch (c) {
		case 'd':
			domain = optarg;
			break;
		case 'h':
			hostname = optarg;
			break;
		case 'v':
			Verbose = 1;
			break;
		default:
			usage();
			/*NOTREACHED*/
		}

	if (optind + 1 != argc )
		usage();

	map = argv[optind];

	strncpy(Domain, domain, sizeof(Domain)-1);
	Domain[sizeof(Domain)-1] = '\0';
	strncpy(Map, map, sizeof(Map)-1);
	Map[sizeof(Map)-1] = '\0';

	/* Check domain */
	snprintf(map_path, sizeof map_path, "%s/%s", YP_DB_PATH, domain);
	if (!((stat(map_path, &finfo) == 0) && S_ISDIR(finfo.st_mode))) {
		fprintf(stderr, "yppush: Map does not exist.\n");
		exit(1);
	}

	/* Check map */
	snprintf(map_path, sizeof map_path, "%s/%s/%s%s",
	    YP_DB_PATH, domain, Map, YPDB_SUFFIX);
	if (!(stat(map_path, &finfo) == 0)) {
		fprintf(stderr, "yppush: Map does not exist.\n");
		exit(1);
	}

	snprintf(map_path, sizeof map_path, "%s/%s/%s",
	    YP_DB_PATH, domain, Map);
	yp_databas = ypdb_open(map_path, 0, O_RDONLY);
	OrderNum=0xffffffff;
	if (yp_databas == 0) {
		fprintf(stderr, "yppush: %s%s: Cannot open database\n",
		    map_path, YPDB_SUFFIX);
	} else {
		o.dptr = (char *) &order_key;
		o.dsize = YP_LAST_LEN;
		o = ypdb_fetch(yp_databas, o);
		if (o.dptr == NULL) {
			fprintf(stderr,
			    "yppush: %s: Cannot determine order number\n",
			    Map);
		} else {
			OrderNum=0;
			for (i=0; i<o.dsize-1; i++) {
				if (!isdigit((unsigned char)o.dptr[i]))
					OrderNum=0xffffffff;
			}
			if (OrderNum != 0) {
				fprintf(stderr,
				    "yppush: %s: Invalid order number '%s'\n",
				    Map, o.dptr);
			} else {
				OrderNum = atoi(o.dptr);
			}
		}
	}

	yp_bind(Domain);

	r = yp_master(Domain, ypmap, &master);
	if (r != 0) {
		fprintf(stderr, "yppush: could not get ypservers map\n");
		exit(1);
	}

	if (hostname != NULL) {
		push(strlen(hostname), hostname);
	} else {
		if (Verbose) {
			printf("Contacting master for ypservers (%s).\n",
			    master);
		}

		client = yp_bind_host(master, YPPROG, YPVERS, 0, 1);

		ypcb.foreach = pushit;
		ypcb.data = NULL;
		r = yp_all_host(client, Domain, ypmap, &ypcb);
	}

	exit(0);
}
