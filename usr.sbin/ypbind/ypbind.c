/*	$OpenBSD: ypbind.c,v 1.80 2024/01/23 14:13:55 deraadt Exp $ */

/*
 * Copyright (c) 1992, 1993, 1996, 1997, 1998 Theo de Raadt <deraadt@openbsd.org>
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
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/syslog.h>

#include <arpa/inet.h>
#include <net/if.h>
#include <rpc/rpc.h>
#include <rpc/xdr.h>
#include <rpc/pmap_clnt.h>
#include <rpc/pmap_prot.h>
#include <rpc/pmap_rmt.h>
#include <rpcsvc/yp.h>
#include <rpcsvc/ypclnt.h>

#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <limits.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SERVERSDIR	"/etc/yp"
#define BINDINGDIR	"/var/yp/binding"

struct _dom_binding {
	struct _dom_binding *dom_pnext;
	char dom_domain[YPMAXDOMAIN + 1];
	struct sockaddr_in dom_server_addr;
	unsigned short int dom_server_port;
	int dom_socket;
	CLIENT *dom_client;
	long dom_vers;
	time_t dom_check_t;
	time_t dom_ask_t;
	int dom_lockfd;
	int dom_alive;
	u_int32_t dom_xid;
	char dom_servlist[PATH_MAX];
	FILE *dom_servlistfp;
};

void rpc_received(char *dom, struct sockaddr_in *raddrp, int force);
void checkwork(void);
enum clnt_stat handle_replies(void);
enum clnt_stat handle_ping(void);
int broadcast(struct _dom_binding *ypdb, char *, int);
int direct(struct _dom_binding *ypdb, char *, int);
int ping(struct _dom_binding *ypdb);
int pings(struct _dom_binding *ypdb);

char *domain;

struct _dom_binding *ypbindlist;
int check;

#define YPSET_NO	0
#define YPSET_LOCAL	1
#define YPSET_ALL	2
int ypsetmode = YPSET_NO;
int insecure = 0;

int rpcsock, pingsock;
struct rmtcallargs rmtca;
struct rmtcallres rmtcr;
bool_t rmtcr_outval;
u_long rmtcr_port;
SVCXPRT *udptransp, *tcptransp;
SVCXPRT *ludptransp, *ltcptransp;

struct _dom_binding *xid2ypdb(u_int32_t xid);
u_int32_t unique_xid(struct _dom_binding *ypdb);

/*
 * We name the local RPC functions ypbindproc_XXX_2x() instead
 * of ypbindproc_XXX_2() because we need to pass an additional
 * parameter. ypbindproc_setdom_2x() does a security check, and
 * hence needs the CLIENT *
 *
 * We are faced with either making ypbindprog_2() do the security
 * check before calling ypbindproc_setdom_2().. or we can simply
 * declare sun's interface insufficient and roll our own.
 */

static void *
ypbindproc_null_2x(SVCXPRT *transp, void *argp, CLIENT *clnt)
{
	static char res;

	memset(&res, 0, sizeof(res));
	return (void *)&res;
}

static struct ypbind_resp *
ypbindproc_domain_2x(SVCXPRT *transp, domainname *argp, CLIENT *clnt)
{
	static struct ypbind_resp res;
	struct _dom_binding *ypdb;
	char path[PATH_MAX];
	time_t now;
	int count = 0;

	if (strchr((char *)argp, '/'))
		return NULL;

	memset(&res, 0, sizeof(res));
	res.ypbind_status = YPBIND_FAIL_VAL;

	for (ypdb = ypbindlist; ypdb && count < 100; ypdb = ypdb->dom_pnext)
		count++;
	if (count >= 100)
		return NULL;	/* prevent DOS: sorry, you lose */

	for (ypdb = ypbindlist; ypdb; ypdb = ypdb->dom_pnext)
		if (!strcmp(ypdb->dom_domain, *argp))
			break;

	if (ypdb == NULL) {
		ypdb = malloc(sizeof *ypdb);
		if (ypdb == NULL)
			return NULL;
		memset(ypdb, 0, sizeof *ypdb);
		strlcpy(ypdb->dom_domain, *argp, sizeof ypdb->dom_domain);
		ypdb->dom_vers = YPVERS;
		ypdb->dom_alive = 0;
		ypdb->dom_lockfd = -1;
		snprintf(path, sizeof path, "%s/%s.%d", BINDINGDIR,
		    ypdb->dom_domain, (int)ypdb->dom_vers);
		unlink(path);
		snprintf(ypdb->dom_servlist, sizeof ypdb->dom_servlist,
		    "%s/%s", SERVERSDIR, ypdb->dom_domain);
		ypdb->dom_servlistfp = fopen(ypdb->dom_servlist, "r");
		ypdb->dom_xid = unique_xid(ypdb);
		ypdb->dom_pnext = ypbindlist;
		ypbindlist = ypdb;
		check++;
		return NULL;
	}

	if (ypdb->dom_alive == 0)
		return NULL;

#ifdef HEURISTIC
	time(&now);
	if (now < ypdb->dom_ask_t + 5) {
		/*
		 * Hmm. More than 2 requests in 5 seconds have indicated
		 * that my binding is possibly incorrect.
		 * Ok, do an immediate poll of the server.
		 */
		if (ypdb->dom_check_t >= now) {
			/* don't flood it */
			ypdb->dom_check_t = 0;
			check++;
		}
	}
	ypdb->dom_ask_t = now;
#endif

	res.ypbind_status = YPBIND_SUCC_VAL;
	memmove(&res.ypbind_resp_u.ypbind_bindinfo.ypbind_binding_addr,
	    &ypdb->dom_server_addr.sin_addr,
	    sizeof(res.ypbind_resp_u.ypbind_bindinfo.ypbind_binding_addr));
	memmove(&res.ypbind_resp_u.ypbind_bindinfo.ypbind_binding_port,
	    &ypdb->dom_server_port,
	    sizeof(res.ypbind_resp_u.ypbind_bindinfo.ypbind_binding_port));
#ifdef DEBUG
	printf("domain %s at %s/%d\n", ypdb->dom_domain,
	    inet_ntoa(ypdb->dom_server_addr.sin_addr),
	    ntohs(ypdb->dom_server_addr.sin_port));
#endif
	return &res;
}

static bool_t *
ypbindproc_setdom_2x(SVCXPRT *transp, struct ypbind_setdom *argp, CLIENT *clnt)
{
	struct sockaddr_in *fromsin, bindsin;
	static bool_t res = 1;

	fromsin = svc_getcaller(transp);

	switch (ypsetmode) {
	case YPSET_LOCAL:
		if (transp != ludptransp && transp != ltcptransp) {
			syslog(LOG_WARNING, "attempted spoof of ypsetme");
			svcerr_weakauth(transp);
			return NULL;
		}
		if (fromsin->sin_addr.s_addr != htonl(INADDR_LOOPBACK)) {
			svcerr_weakauth(transp);
			return NULL;
		}
		break;
	case YPSET_ALL:
		break;
	case YPSET_NO:
	default:
		svcerr_weakauth(transp);
		return NULL;
	}

	if (ntohs(fromsin->sin_port) >= IPPORT_RESERVED) {
		svcerr_weakauth(transp);
		return NULL;
	}

	if (argp->ypsetdom_vers != YPVERS) {
		svcerr_noprog(transp);
		return NULL;
	}

	memset(&bindsin, 0, sizeof bindsin);
	bindsin.sin_family = AF_INET;
	bindsin.sin_len = sizeof(bindsin);
	memcpy(&bindsin.sin_addr, &argp->ypsetdom_binding.ypbind_binding_addr,
	    sizeof(argp->ypsetdom_binding.ypbind_binding_addr));
	memcpy(&bindsin.sin_port, &argp->ypsetdom_binding.ypbind_binding_port,
	    sizeof(argp->ypsetdom_binding.ypbind_binding_port));
	rpc_received(argp->ypsetdom_domain, &bindsin, 1);

	return &res;
}

static void
ypbindprog_2(struct svc_req *rqstp, SVCXPRT *transp)
{
	union argument {
		domainname ypbindproc_domain_2_arg;
		struct ypbind_setdom ypbindproc_setdom_2_arg;
	} argument;
	struct authunix_parms *creds;
	char *result;
	xdrproc_t xdr_argument, xdr_result;
	char *(*local)(SVCXPRT *, union argument *, struct svc_req *);

	switch (rqstp->rq_proc) {
	case YPBINDPROC_NULL:
		xdr_argument = xdr_void;
		xdr_result = xdr_void;
		local = (char *(*)(SVCXPRT *, union argument *, struct svc_req *))
		    ypbindproc_null_2x;
		break;

	case YPBINDPROC_DOMAIN:
		xdr_argument = xdr_domainname;
		xdr_result = xdr_ypbind_resp;
		local = (char *(*)(SVCXPRT *, union argument *, struct svc_req *))
		    ypbindproc_domain_2x;
		break;

	case YPBINDPROC_SETDOM:
		switch (rqstp->rq_cred.oa_flavor) {
		case AUTH_UNIX:
			creds = (struct authunix_parms *)rqstp->rq_clntcred;
			if (creds->aup_uid != 0) {
				svcerr_auth(transp, AUTH_BADCRED);
				return;
			}
			break;
		default:
			svcerr_auth(transp, AUTH_TOOWEAK);
			return;
		}

		xdr_argument = xdr_ypbind_setdom;
		xdr_result = xdr_void;
		local = (char *(*)(SVCXPRT *, union argument *, struct svc_req *))
		    ypbindproc_setdom_2x;
		break;

	default:
		svcerr_noproc(transp);
		return;
	}
	memset(&argument, 0, sizeof(argument));
	if (!svc_getargs(transp, xdr_argument, (caddr_t)&argument)) {
		svcerr_decode(transp);
		return;
	}
	result = (*local)(transp, &argument, rqstp);
	if (result != NULL && !svc_sendreply(transp, xdr_result, result)) {
		svcerr_systemerr(transp);
	}
	return;
}

static void
usage(void)
{
	fprintf(stderr, "usage: ypbind [-insecure] [-ypset] [-ypsetme]\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	char path[PATH_MAX];
	struct sockaddr_in sin;
	struct pollfd *pfd = NULL;
	int width = 0, nready, lsock;
	socklen_t len;
	int evil = 0, one = 1;
	DIR *dirp;
	struct dirent *dent;

	if (yp_get_default_domain(&domain) != 0 || domain[0] == '\0') {
		fprintf(stderr, "domainname not set. Aborting.\n");
		exit(1);
	}

	while (--argc) {
		++argv;
		if (!strcmp("-insecure", *argv))
			insecure = 1;
		else if (!strcmp("-ypset", *argv))
			ypsetmode = YPSET_ALL;
		else if (!strcmp("-ypsetme", *argv))
			ypsetmode = YPSET_LOCAL;
		else
			usage();
	}

	/* blow away everything in BINDINGDIR */
	dirp = opendir(BINDINGDIR);
	if (dirp) {
		while ((dent = readdir(dirp))) {
			if (!strcmp(dent->d_name, ".") ||
			    !strcmp(dent->d_name, ".."))
				continue;
			(void)unlinkat(dirfd(dirp), dent->d_name, 0);
		}
		closedir(dirp);
	} else {
		(void)mkdir(BINDINGDIR, 0755);
	}

	(void)pmap_unset(YPBINDPROG, YPBINDVERS);

	udptransp = svcudp_create(RPC_ANYSOCK);
	if (udptransp == NULL) {
		fprintf(stderr, "cannot create udp service.\n");
		exit(1);
	}
	if (!svc_register(udptransp, YPBINDPROG, YPBINDVERS, ypbindprog_2,
	    IPPROTO_UDP)) {
		fprintf(stderr,
		    "unable to register (YPBINDPROG, YPBINDVERS, udp).\n");
		exit(1);
	}

	tcptransp = svctcp_create(RPC_ANYSOCK, 0, 0);
	if (tcptransp == NULL) {
		fprintf(stderr, "cannot create tcp service.\n");
		exit(1);
	}
	if (!svc_register(tcptransp, YPBINDPROG, YPBINDVERS, ypbindprog_2,
	    IPPROTO_TCP)) {
		fprintf(stderr,
		    "unable to register (YPBINDPROG, YPBINDVERS, tcp).\n");
		exit(1);
	}

	if (ypsetmode == YPSET_LOCAL) {
		/* build UDP local port */
		if ((lsock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
			syslog(LOG_ERR, "cannot create local udp socket: %m");
			exit(1);
		}
		(void)setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, &one,
		    (socklen_t)sizeof one);
		len = sizeof(sin);
		if (getsockname(udptransp->xp_sock, (struct sockaddr *)&sin,
		    &len) == -1) {
			syslog(LOG_ERR, "cannot getsockname local udp: %m");
			exit(1);
		}
		sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		sin.sin_port = htons(udptransp->xp_port);
		if (bind(lsock, (struct sockaddr *)&sin, len) != 0) {
			syslog(LOG_ERR, "cannot bind local udp: %m");
			exit(1);
		}
		if ((ludptransp = svcudp_create(lsock)) == NULL) {
			fprintf(stderr, "cannot create udp service.\n");
			exit(1);
		}

		/* build TCP local port */
		if ((lsock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
			syslog(LOG_ERR, "cannot create udp socket: %m");
			exit(1);
		}
		(void)setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, &one,
		    (socklen_t)sizeof one);
		len = sizeof(sin);
		if (getsockname(tcptransp->xp_sock, (struct sockaddr *)&sin,
		    &len) == -1) {
			syslog(LOG_ERR, "cannot getsockname udp: %m");
			exit(1);
		}
		sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		sin.sin_port = htons(tcptransp->xp_port);
		if (bind(lsock, (struct sockaddr *)&sin, len) == -1) {
			syslog(LOG_ERR, "cannot bind local tcp: %m");
			exit(1);
		}
		if ((ltcptransp = svctcp_create(lsock, 0, 0)) == NULL) {
			fprintf(stderr, "cannot create tcp service.\n");
			exit(1);
		}
	}

	if ((rpcsock = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0)) == -1) {
		perror("socket");
		return -1;
	}
	memset(&sin, 0, sizeof sin);
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_ANY);
	sin.sin_port = 0;
	bindresvport(rpcsock, &sin);

	if ((pingsock = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0)) == -1) {
		perror("socket");
		return -1;
	}
	memset(&sin, 0, sizeof sin);
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_ANY);
	sin.sin_port = 0;
	bindresvport(pingsock, &sin);

	setsockopt(rpcsock, SOL_SOCKET, SO_BROADCAST, &one,
	    (socklen_t)sizeof(one));
	rmtca.prog = YPPROG;
	rmtca.vers = YPVERS;
	rmtca.proc = YPPROC_DOMAIN_NONACK;
	rmtca.xdr_args = NULL;		/* set at call time */
	rmtca.args_ptr = NULL;		/* set at call time */
	rmtcr.port_ptr = &rmtcr_port;
	rmtcr.xdr_results = xdr_bool;
	rmtcr.results_ptr = (caddr_t)&rmtcr_outval;

	if (strchr(domain, '/'))
		errx(1, "bad domainname %s", domain);

	/* build initial domain binding, make it "unsuccessful" */
	ypbindlist = malloc(sizeof *ypbindlist);
	if (ypbindlist == NULL)
		errx(1, "no memory");
	memset(ypbindlist, 0, sizeof *ypbindlist);
	strlcpy(ypbindlist->dom_domain, domain, sizeof ypbindlist->dom_domain);
	ypbindlist->dom_vers = YPVERS;
	snprintf(ypbindlist->dom_servlist, sizeof ypbindlist->dom_servlist,
	    "%s/%s", SERVERSDIR, ypbindlist->dom_domain);
	ypbindlist->dom_servlistfp = fopen(ypbindlist->dom_servlist, "r");
	ypbindlist->dom_alive = 0;
	ypbindlist->dom_lockfd = -1;
	ypbindlist->dom_xid = unique_xid(ypbindlist);
	snprintf(path, sizeof path, "%s/%s.%d", BINDINGDIR,
	    ypbindlist->dom_domain, (int)ypbindlist->dom_vers);
	(void)unlink(path);

	checkwork();

	while (1) {
		if (pfd == NULL || width != svc_max_pollfd + 2) {
			width = svc_max_pollfd + 2;
			pfd = reallocarray(pfd, width, sizeof *pfd);
			if (pfd == NULL)
				err(1, NULL);
		}

		pfd[0].fd = rpcsock;
		pfd[0].events = POLLIN;
		pfd[1].fd = pingsock;
		pfd[1].events = POLLIN;
		memcpy(pfd + 2, svc_pollfd, sizeof(*pfd) * svc_max_pollfd);

		nready = poll(pfd, width, 1000);
		switch (nready) {
		case 0:
			checkwork();
			break;
		case -1:
			if (errno != EINTR)
				perror("poll");
			break;
		default:
			/* No need to check for POLLHUP on UDP sockets. */
			if (pfd[0].revents & POLLIN) {
				handle_replies();
				nready--;
			}
			if (pfd[1].revents & POLLIN) {
				handle_ping();
				nready--;
			}
			svc_getreq_poll(pfd + 2, nready);
			if (check)
				checkwork();
			break;
		}

#ifdef DAEMON
		if (!evil && ypbindlist->dom_alive) {
			evil = 1;
			daemon(0, 0);
		}
#endif
	}
}

/*
 * State transition is done like this:
 *
 * STATE	EVENT		ACTION			NEWSTATE	TIMEOUT
 * no binding	timeout		broadcast		no binding	5 sec
 * no binding	answer		--			binding		60 sec
 * binding	timeout		ping server		checking	5 sec
 * checking	timeout		ping server + broadcast	checking	5 sec
 * checking	answer		--			binding		60 sec
 */
void
checkwork(void)
{
	struct _dom_binding *ypdb;
	time_t t;

	time(&t);
	for (ypdb = ypbindlist; ypdb; ypdb = ypdb->dom_pnext) {
		if (ypdb->dom_check_t < t) {
			if (ypdb->dom_alive == 1)
				ping(ypdb);
			else
				pings(ypdb);
			time(&t);
			ypdb->dom_check_t = t + 5;
		}
	}
	check = 0;
}

int
ping(struct _dom_binding *ypdb)
{
	domainname dom = ypdb->dom_domain;
	struct rpc_msg msg;
	char buf[1400];
	enum clnt_stat st;
	int outlen;
	AUTH *rpcua;
	XDR xdr;

	memset(&xdr, 0, sizeof xdr);
	memset(&msg, 0, sizeof msg);

	rpcua = authunix_create_default();
	if (rpcua == (AUTH *)NULL) {
		/*printf("cannot get unix auth\n");*/
		return RPC_SYSTEMERROR;
	}
	msg.rm_direction = CALL;
	msg.rm_call.cb_rpcvers = RPC_MSG_VERSION;
	msg.rm_call.cb_prog = YPPROG;
	msg.rm_call.cb_vers = YPVERS;
	msg.rm_call.cb_proc = YPPROC_DOMAIN_NONACK;
	msg.rm_call.cb_cred = rpcua->ah_cred;
	msg.rm_call.cb_verf = rpcua->ah_verf;

	msg.rm_xid = ypdb->dom_xid;
	xdrmem_create(&xdr, buf, sizeof buf, XDR_ENCODE);
	if (!xdr_callmsg(&xdr, &msg)) {
		st = RPC_CANTENCODEARGS;
		AUTH_DESTROY(rpcua);
		return st;
	}
	if (!xdr_domainname(&xdr, &dom)) {
		st = RPC_CANTENCODEARGS;
		AUTH_DESTROY(rpcua);
		return st;
	}
	outlen = (int)xdr_getpos(&xdr);
	xdr_destroy(&xdr);
	if (outlen < 1) {
		st = RPC_CANTENCODEARGS;
		AUTH_DESTROY(rpcua);
		return st;
	}
	AUTH_DESTROY(rpcua);

	ypdb->dom_alive = 2;
	if (sendto(pingsock, buf, outlen, 0,
	    (struct sockaddr *)&ypdb->dom_server_addr,
	    (socklen_t)sizeof ypdb->dom_server_addr) == -1)
		perror("sendto");
	return 0;

}

int
pings(struct _dom_binding *ypdb)
{
	domainname dom = ypdb->dom_domain;
	struct rpc_msg msg;
	struct sockaddr_in bindsin;
	char buf[1400];
	char path[PATH_MAX];
	enum clnt_stat st;
	int outlen;
	AUTH *rpcua;
	XDR xdr;

	rmtca.xdr_args = xdr_domainname;
	rmtca.args_ptr = (char *)&dom;

	memset(&xdr, 0, sizeof xdr);
	memset(&msg, 0, sizeof msg);

	rpcua = authunix_create_default();
	if (rpcua == (AUTH *)NULL) {
		/*printf("cannot get unix auth\n");*/
		return RPC_SYSTEMERROR;
	}
	msg.rm_direction = CALL;
	msg.rm_call.cb_rpcvers = RPC_MSG_VERSION;
	msg.rm_call.cb_prog = PMAPPROG;
	msg.rm_call.cb_vers = PMAPVERS;
	msg.rm_call.cb_proc = PMAPPROC_CALLIT;
	msg.rm_call.cb_cred = rpcua->ah_cred;
	msg.rm_call.cb_verf = rpcua->ah_verf;

	msg.rm_xid = ypdb->dom_xid;
	xdrmem_create(&xdr, buf, sizeof buf, XDR_ENCODE);
	if (!xdr_callmsg(&xdr, &msg)) {
		st = RPC_CANTENCODEARGS;
		AUTH_DESTROY(rpcua);
		return st;
	}
	if (!xdr_rmtcall_args(&xdr, &rmtca)) {
		st = RPC_CANTENCODEARGS;
		AUTH_DESTROY(rpcua);
		return st;
	}
	outlen = (int)xdr_getpos(&xdr);
	xdr_destroy(&xdr);
	if (outlen < 1) {
		st = RPC_CANTENCODEARGS;
		AUTH_DESTROY(rpcua);
		return st;
	}
	AUTH_DESTROY(rpcua);

	if (ypdb->dom_lockfd != -1) {
		close(ypdb->dom_lockfd);
		ypdb->dom_lockfd = -1;
		snprintf(path, sizeof path, "%s/%s.%d", BINDINGDIR,
		    ypdb->dom_domain, (int)ypdb->dom_vers);
		unlink(path);
	}

	if (ypdb->dom_alive == 2) {
		/*
		 * This resolves the following situation:
		 * ypserver on other subnet was once bound,
		 * but rebooted and is now using a different port
		 */
		memset(&bindsin, 0, sizeof bindsin);
		bindsin.sin_family = AF_INET;
		bindsin.sin_len = sizeof(bindsin);
		bindsin.sin_port = htons(PMAPPORT);
		bindsin.sin_addr = ypdb->dom_server_addr.sin_addr;
		if (sendto(rpcsock, buf, outlen, 0, (struct sockaddr *)&bindsin,
		    (socklen_t)sizeof bindsin) == -1)
			perror("sendto");
	}
	if (ypdb->dom_servlistfp)
		return direct(ypdb, buf, outlen);
	return broadcast(ypdb, buf, outlen);
}

int
broadcast(struct _dom_binding *ypdb, char *buf, int outlen)
{
	struct ifaddrs *ifap, *ifa;
	struct sockaddr_in bindsin;
	struct in_addr in;

	memset(&bindsin, 0, sizeof bindsin);
	bindsin.sin_family = AF_INET;
	bindsin.sin_len = sizeof(bindsin);
	bindsin.sin_port = htons(PMAPPORT);

	if (getifaddrs(&ifap) != 0) {
		perror("getifaddrs");
		return -1;
	}
	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL ||
		    ifa->ifa_addr->sa_family != AF_INET)
			continue;
		if ((ifa->ifa_flags & IFF_UP) == 0)
			continue;

		switch (ifa->ifa_flags & (IFF_LOOPBACK | IFF_BROADCAST)) {
		case IFF_BROADCAST:
			if (!ifa->ifa_broadaddr)
				continue;
			if (ifa->ifa_broadaddr->sa_family != AF_INET)
				continue;
			in = ((struct sockaddr_in *)ifa->ifa_broadaddr)->sin_addr;
			break;
		case IFF_LOOPBACK:
			in = ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
			break;
		default:
			continue;
		}

		bindsin.sin_addr = in;
		if (sendto(rpcsock, buf, outlen, 0, (struct sockaddr *)&bindsin,
		    (socklen_t)bindsin.sin_len) == -1)
			perror("sendto");
	}
	freeifaddrs(ifap);
	return 0;
}

int
direct(struct _dom_binding *ypdb, char *buf, int outlen)
{
	char line[1024], *p;
	struct hostent *hp;
	struct sockaddr_in bindsin;
	int i, c;
	struct stat fst, st;

	if (fstat(fileno(ypdb->dom_servlistfp), &fst) != -1 &&
	    stat(ypdb->dom_servlist, &st) != -1 &&
	    (st.st_dev != fst.st_dev || st.st_ino != fst.st_ino)) {
		FILE *fp;

		fp = fopen(ypdb->dom_servlist, "r");
		if (fp) {
			fclose(ypdb->dom_servlistfp);
			ypdb->dom_servlistfp = fp;
		}
	}
	(void) rewind(ypdb->dom_servlistfp);

	memset(&bindsin, 0, sizeof bindsin);
	bindsin.sin_family = AF_INET;
	bindsin.sin_len = sizeof(bindsin);
	bindsin.sin_port = htons(PMAPPORT);

	while (fgets(line, sizeof(line), ypdb->dom_servlistfp) != NULL) {
		/* skip lines that are too big */
		p = strchr(line, '\n');
		if (p == NULL) {
			while ((c = getc(ypdb->dom_servlistfp)) != '\n' && c != EOF)
				;
			continue;
		}
		*p = '\0';
		p = line;
		while (isspace((unsigned char)*p))
			p++;
		if (*p == '#')
			continue;
		hp = gethostbyname(p);
		if (!hp)
			continue;
		/* step through all addresses in case first is unavailable */
		for (i = 0; hp->h_addr_list[i]; i++) {
			memmove(&bindsin.sin_addr, hp->h_addr_list[0],
			    hp->h_length);
			if (sendto(rpcsock, buf, outlen, 0,
			    (struct sockaddr *)&bindsin,
			    (socklen_t)sizeof bindsin) == -1) {
				perror("sendto");
				continue;
			}
		}
	}
	return 0;
}

enum clnt_stat
handle_replies(void)
{
	char buf[1400];
	int inlen;
	socklen_t fromlen;
	struct _dom_binding *ypdb;
	struct sockaddr_in raddr;
	struct rpc_msg msg;
	XDR xdr;

recv_again:
	memset(&xdr, 0, sizeof(xdr));
	memset(&msg, 0, sizeof(msg));
	msg.acpted_rply.ar_verf = _null_auth;
	msg.acpted_rply.ar_results.where = (caddr_t)&rmtcr;
	msg.acpted_rply.ar_results.proc = xdr_rmtcallres;

try_again:
	fromlen = sizeof (struct sockaddr);
	inlen = recvfrom(rpcsock, buf, sizeof buf, 0,
	    (struct sockaddr *)&raddr, &fromlen);
	if (inlen == -1) {
		if (errno == EINTR)
			goto try_again;
		return RPC_CANTRECV;
	}
	if (inlen < sizeof(u_int32_t))
		goto recv_again;

	/*
	 * see if reply transaction id matches sent id.
	 * If so, decode the results.
	 */
	xdrmem_create(&xdr, buf, (u_int)inlen, XDR_DECODE);
	if (xdr_replymsg(&xdr, &msg)) {
		if ((msg.rm_reply.rp_stat == MSG_ACCEPTED) &&
		    (msg.acpted_rply.ar_stat == SUCCESS)) {
			raddr.sin_port = htons((u_short)rmtcr_port);
			ypdb = xid2ypdb(msg.rm_xid);
			if (ypdb)
				rpc_received(ypdb->dom_domain, &raddr, 0);
		}
	}
	xdr.x_op = XDR_FREE;
	msg.acpted_rply.ar_results.proc = xdr_void;
	xdr_destroy(&xdr);

	return RPC_SUCCESS;
}

enum clnt_stat
handle_ping(void)
{
	char buf[1400];
	int inlen;
	socklen_t fromlen;
	struct _dom_binding *ypdb;
	struct sockaddr_in raddr;
	struct rpc_msg msg;
	XDR xdr;
	bool_t res;

recv_again:
	memset(&xdr, 0, sizeof(xdr));
	memset(&msg, 0, sizeof(msg));
	msg.acpted_rply.ar_verf = _null_auth;
	msg.acpted_rply.ar_results.where = (caddr_t)&res;
	msg.acpted_rply.ar_results.proc = xdr_bool;

try_again:
	fromlen = sizeof (struct sockaddr);
	inlen = recvfrom(pingsock, buf, sizeof buf, 0,
	    (struct sockaddr *)&raddr, &fromlen);
	if (inlen == -1) {
		if (errno == EINTR)
			goto try_again;
		return RPC_CANTRECV;
	}
	if (inlen < sizeof(u_int32_t))
		goto recv_again;

	/*
	 * see if reply transaction id matches sent id.
	 * If so, decode the results.
	 */
	xdrmem_create(&xdr, buf, (u_int)inlen, XDR_DECODE);
	if (xdr_replymsg(&xdr, &msg)) {
		if ((msg.rm_reply.rp_stat == MSG_ACCEPTED) &&
		    (msg.acpted_rply.ar_stat == SUCCESS)) {
			ypdb = xid2ypdb(msg.rm_xid);
			if (ypdb)
				rpc_received(ypdb->dom_domain, &raddr, 0);
		}
	}
	xdr.x_op = XDR_FREE;
	msg.acpted_rply.ar_results.proc = xdr_void;
	xdr_destroy(&xdr);

	return RPC_SUCCESS;
}

/*
 * We prefer loopback connections.
 */
void
rpc_received(char *dom, struct sockaddr_in *raddrp, int force)
{
	struct _dom_binding *ypdb;
	struct iovec iov[3];
	struct ypbind_resp ybr;
	char path[PATH_MAX];
	u_short ypserv_tcp, ypserv_udp;
	int fd;

	if (strchr(dom, '/'))
		return;

#ifdef DEBUG
	printf("returned from %s about %s\n", inet_ntoa(raddrp->sin_addr), dom);
#endif

	if (dom == NULL)
		return;

	for (ypdb = ypbindlist; ypdb; ypdb = ypdb->dom_pnext)
		if (!strcmp(ypdb->dom_domain, dom))
			break;

	if (ypdb == NULL) {
		if (force == 0)
			return;
		ypdb = malloc(sizeof *ypdb);
		if (ypdb == NULL)
			return;
		memset(ypdb, 0, sizeof *ypdb);
		strlcpy(ypdb->dom_domain, dom, sizeof ypdb->dom_domain);
		ypdb->dom_lockfd = -1;
		ypdb->dom_xid = unique_xid(ypdb);
		ypdb->dom_pnext = ypbindlist;
		ypbindlist = ypdb;
	}

	/* we do not support sunos 3.0 insecure servers */
	if (insecure == 0 && ntohs(raddrp->sin_port) >= IPPORT_RESERVED)
		return;

	/* soft update, alive */
	if (ypdb->dom_alive == 1 && force == 0) {
		if (!memcmp(&ypdb->dom_server_addr, raddrp,
		    sizeof ypdb->dom_server_addr)) {
			ypdb->dom_alive = 1;
			/* recheck binding in 60 sec */
			ypdb->dom_check_t = time(NULL) + 60;
		}
		if (raddrp->sin_addr.s_addr == htonl(INADDR_LOOPBACK)) {
			/*
			 * we are alive and already have a binding, but
			 * after a broadcast we prefer the localhost
			 */
			memcpy(&ypdb->dom_server_addr, raddrp,
			    sizeof ypdb->dom_server_addr);
		}
		return;
	}

	/* synchronously ask for the matching ypserv TCP port number */
	ypserv_udp = raddrp->sin_port;
	ypserv_tcp = pmap_getport(raddrp, YPPROG,
	    YPVERS, IPPROTO_TCP);
	if (ypserv_tcp == 0) {
		clnt_pcreateerror("pmap_getport");
		return;
	}
	if (ypserv_tcp >= IPPORT_RESERVED || ypserv_tcp == 20)
		return;
	ypserv_tcp = htons(ypserv_tcp);

	memcpy(&ypdb->dom_server_addr, raddrp, sizeof ypdb->dom_server_addr);
	/* recheck binding in 60 seconds */
	ypdb->dom_check_t = time(NULL) + 60;
	ypdb->dom_vers = YPVERS;
	ypdb->dom_alive = 1;

	if (ypdb->dom_lockfd != -1)
		close(ypdb->dom_lockfd);

	snprintf(path, sizeof path, "%s/%s.%d", BINDINGDIR,
	    ypdb->dom_domain, (int)ypdb->dom_vers);
	if ((fd = open(path, O_CREAT|O_SHLOCK|O_RDWR|O_TRUNC, 0644)) == -1) {
		(void)mkdir(BINDINGDIR, 0755);
		if ((fd = open(path, O_CREAT|O_SHLOCK|O_RDWR|O_TRUNC,
		    0644)) == -1)
			return;
	}

	/*
	 * ok, if BINDINGDIR exists, and we can create the binding file,
	 * then write to it..
	 */
	ypdb->dom_lockfd = fd;

	iov[0].iov_base = (caddr_t)&(udptransp->xp_port);
	iov[0].iov_len = sizeof udptransp->xp_port;
	iov[1].iov_base = (caddr_t)&ybr;
	iov[1].iov_len = sizeof ybr;
	iov[2].iov_base = (caddr_t)&ypserv_tcp;
	iov[2].iov_len = sizeof ypserv_tcp;

	memset(&ybr, 0, sizeof ybr);
	ybr.ypbind_status = YPBIND_SUCC_VAL;
	memmove(&ybr.ypbind_resp_u.ypbind_bindinfo.ypbind_binding_addr,
	    &raddrp->sin_addr,
	    sizeof(ybr.ypbind_resp_u.ypbind_bindinfo.ypbind_binding_addr));
	memmove(&ybr.ypbind_resp_u.ypbind_bindinfo.ypbind_binding_port,
	    &ypserv_udp,
	    sizeof(ybr.ypbind_resp_u.ypbind_bindinfo.ypbind_binding_port));

	if (writev(ypdb->dom_lockfd, iov, sizeof(iov)/sizeof(iov[0])) !=
	    iov[0].iov_len + iov[1].iov_len + iov[2].iov_len) {
		perror("write");
		close(ypdb->dom_lockfd);
		unlink(path);
		ypdb->dom_lockfd = -1;
		return;
	}
}

struct _dom_binding *
xid2ypdb(u_int32_t xid)
{
	struct _dom_binding *ypdb;

	for (ypdb = ypbindlist; ypdb; ypdb = ypdb->dom_pnext)
		if (ypdb->dom_xid == xid)
			break;
	return (ypdb);
}

u_int32_t
unique_xid(struct _dom_binding *ypdb)
{
	u_int32_t xid;

	xid = arc4random();
	while (xid2ypdb(xid) != NULL)
		xid++;

	return (xid);
}
