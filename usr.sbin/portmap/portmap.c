/*	$OpenBSD: portmap.c,v 1.51 2023/03/08 04:43:14 guenther Exp $	*/

/*-
 * Copyright (c) 1996, 1997 Theo de Raadt (OpenBSD). All rights reserved.
 * Copyright (c) 1990 The Regents of the University of California.
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
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
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
 * portmap.c, Implements the program,version to port number mapping for
 * rpc.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/resource.h>

#include <rpcsvc/nfs_prot.h>
#include <arpa/inet.h>
#include <rpc/rpc.h>
#include <rpc/pmap_prot.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <netdb.h>
#include <pwd.h>
#include <errno.h>
#include <err.h>

void reg_service(struct svc_req *, SVCXPRT *);
void reap(int);
void callit(struct svc_req *, SVCXPRT *);
int check_callit(struct sockaddr_in *, u_long, u_long);
struct pmaplist *find_service(u_long, u_long, u_long);

struct pmaplist *pmaplist;
int debugging;

SVCXPRT *ludpxprt, *ltcpxprt;

int
main(int argc, char *argv[])
{
	int sock, lsock, c, on = 1;
	socklen_t len = sizeof(struct sockaddr_in);
	struct sockaddr_in addr, laddr;
	struct pmaplist *pml;
	struct passwd *pw;
	SVCXPRT *xprt;

	while ((c = getopt(argc, argv, "d")) != -1) {
		switch (c) {
		case 'd':
			debugging = 1;
			break;
		default:
			(void)fprintf(stderr, "usage: %s [-d]\n", argv[0]);
			exit(1);
		}
	}

	if (!debugging && daemon(0, 0)) {
		(void)fprintf(stderr, "portmap: fork: %s", strerror(errno));
		exit(1);
	}

	openlog("portmap", LOG_NDELAY | (debugging ? LOG_PID | LOG_PERROR :
	    LOG_PID), LOG_DAEMON);

	bzero(&addr, sizeof addr);
	addr.sin_addr.s_addr = 0;
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(PMAPPORT);

	bzero(&laddr, sizeof laddr);
	laddr.sin_addr.s_addr = 0;
	laddr.sin_family = AF_INET;
	laddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	laddr.sin_port = htons(PMAPPORT);

	if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
		syslog(LOG_ERR, "cannot create udp socket: %m");
		exit(1);
	}
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
	if (bind(sock, (struct sockaddr *)&addr, len) == -1) {
		syslog(LOG_ERR, "cannot bind udp: %m");
		exit(1);
	}

	if ((xprt = svcudp_create(sock)) == NULL) {
		syslog(LOG_ERR, "couldn't do udp_create");
		exit(1);
	}

	if ((lsock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
		syslog(LOG_ERR, "cannot create udp socket: %m");
		exit(1);
	}
	setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
	if (bind(lsock, (struct sockaddr *)&laddr, len) == -1) {
		syslog(LOG_ERR, "cannot bind local udp: %m");
		exit(1);
	}

	if ((ludpxprt = svcudp_create(lsock)) == NULL) {
		syslog(LOG_ERR, "couldn't do udp_create");
		exit(1);
	}

	/* make an entry for ourself */
	pml = malloc(sizeof(struct pmaplist));
	if (pml == NULL) {
		syslog(LOG_ERR, "out of memory");
		exit(1);
	}
	pml->pml_next = 0;
	pml->pml_map.pm_prog = PMAPPROG;
	pml->pml_map.pm_vers = PMAPVERS;
	pml->pml_map.pm_prot = IPPROTO_UDP;
	pml->pml_map.pm_port = PMAPPORT;
	pmaplist = pml;

	if ((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
		syslog(LOG_ERR, "cannot create tcp socket: %m");
		exit(1);
	}
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
	if (bind(sock, (struct sockaddr *)&addr, len) == -1) {
		syslog(LOG_ERR, "cannot bind tcp: %m");
		exit(1);
	}
	if ((xprt = svctcp_create(sock, RPCSMALLMSGSIZE, RPCSMALLMSGSIZE)) ==
	    NULL) {
		syslog(LOG_ERR, "couldn't do tcp_create");
		exit(1);
	}

	if ((lsock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
		syslog(LOG_ERR, "cannot create tcp socket: %m");
		exit(1);
	}
	setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
	if (bind(lsock, (struct sockaddr *)&laddr, len) == -1) {
		syslog(LOG_ERR, "cannot bind tcp: %m");
		exit(1);
	}
	if ((ltcpxprt = svctcp_create(lsock, RPCSMALLMSGSIZE,
	    RPCSMALLMSGSIZE)) == NULL) {
		syslog(LOG_ERR, "couldn't do tcp_create");
		exit(1);
	}

	/* make an entry for ourself */
	pml = malloc(sizeof(struct pmaplist));
	if (pml == NULL) {
		syslog(LOG_ERR, "out of memory");
		exit(1);
	}
	pml->pml_map.pm_prog = PMAPPROG;
	pml->pml_map.pm_vers = PMAPVERS;
	pml->pml_map.pm_prot = IPPROTO_TCP;
	pml->pml_map.pm_port = PMAPPORT;
	pml->pml_next = pmaplist;
	pmaplist = pml;

	if ((pw = getpwnam("_portmap")) == NULL) {
		syslog(LOG_ERR, "no such user _portmap");
		exit(1);
	}
	if (chroot("/var/empty") == -1) {
		syslog(LOG_ERR, "cannot chroot to /var/empty.");
		exit(1);
	}
	if (chdir("/") == -1) {
		syslog(LOG_ERR, "cannot chdir to new /.");
		exit(1);
	}

	if (pw) {
		if (setgroups(1, &pw->pw_gid) == -1 ||
		    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) == -1 ||
		    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid) == -1) {
			syslog(LOG_ERR, "revoke privs: %s", strerror(errno));
			exit(1);
		}
	}
	endpwent();

	if (pledge("stdio inet proc", NULL) == -1)
		err(1, "pledge");

	if (svc_register(xprt, PMAPPROG, PMAPVERS, reg_service, FALSE) == 0) {
		syslog(LOG_ERR, "svc_register failed.");
		exit(1);
	}

	(void)signal(SIGCHLD, reap);
	svc_run();
	syslog(LOG_ERR, "svc_run returned unexpectedly");
	abort();
}

struct pmaplist *
find_service(u_long prog, u_long vers, u_long prot)
{
	struct pmaplist *hit = NULL;
	struct pmaplist *pml;

	for (pml = pmaplist; pml != NULL; pml = pml->pml_next) {
		if ((pml->pml_map.pm_prog != prog) ||
		    (pml->pml_map.pm_prot != prot))
			continue;
		hit = pml;
		if (pml->pml_map.pm_vers == vers)
			break;
	}
	return (hit);
}

/*
 * 1 OK, 0 not
 */
void
reg_service(struct svc_req *rqstp, SVCXPRT *xprt)
{
	struct pmap reg;
	struct pmaplist *pml, *prevpml, *fnd;
	struct sockaddr_in *fromsin;
	long ans = 0, port;
	void *t;

	fromsin = svc_getcaller(xprt);

	if (debugging)
		(void)fprintf(stderr, "server: about to do a switch\n");
	switch (rqstp->rq_proc) {
	case PMAPPROC_NULL:
		/*
		 * Null proc call
		 */
		if (!svc_sendreply(xprt, xdr_void, NULL) && debugging) {
			abort();
		}
		break;
	case PMAPPROC_SET:
		/*
		 * Set a program,version to port mapping
		 */
		if (xprt != ltcpxprt && xprt != ludpxprt) {
			syslog(LOG_WARNING,
			    "non-local set attempt (might be from %s)",
			    inet_ntoa(fromsin->sin_addr));
			svcerr_noproc(xprt);
			return;
		}
		if (!svc_getargs(xprt, xdr_pmap, (caddr_t)&reg)) {
			svcerr_decode(xprt);
			break;
		}

		/*
		 * check to see if already used
		 * find_service returns a hit even if
		 * the versions don't match, so check for it
		 */
		fnd = find_service(reg.pm_prog, reg.pm_vers, reg.pm_prot);
		if (fnd && fnd->pml_map.pm_vers == reg.pm_vers) {
			if (fnd->pml_map.pm_port == reg.pm_port)
				ans = 1;
			goto done;
		}

		if (debugging)
			printf("set: prog %lu vers %lu port %lu\n",
			    reg.pm_prog, reg.pm_vers, reg.pm_port);

		if (reg.pm_port & ~0xffff)
			goto done;

		/*
		 * only permit localhost root to create
		 * mappings pointing at sensitive ports
		 */
		if ((reg.pm_port < IPPORT_RESERVED ||
		    reg.pm_port == NFS_PORT) &&
		    htons(fromsin->sin_port) >= IPPORT_RESERVED) {
			syslog(LOG_WARNING,
			    "resvport set attempt by non-root");
			goto done;
		}

		/*
		 * add to END of list
		 */
		pml = malloc(sizeof(struct pmaplist));
		if (pml == NULL) {
			syslog(LOG_ERR, "out of memory");
			svcerr_systemerr(xprt);
			return;
		}

		pml->pml_map = reg;
		pml->pml_next = 0;
		if (pmaplist == NULL) {
			pmaplist = pml;
		} else {
			for (fnd = pmaplist; fnd->pml_next != 0;
			    fnd = fnd->pml_next)
				;
			fnd->pml_next = pml;
		}
		ans = 1;
done:
		if ((!svc_sendreply(xprt, xdr_long, (caddr_t)&ans)) &&
		    debugging) {
			(void)fprintf(stderr, "svc_sendreply\n");
			abort();
		}
		break;
	case PMAPPROC_UNSET:
		/*
		 * Remove a program,version to port mapping.
		 */
		if (xprt != ltcpxprt && xprt != ludpxprt) {
			syslog(LOG_WARNING,
			    "non-local unset attempt (might be from %s)",
			    inet_ntoa(fromsin->sin_addr));
			svcerr_noproc(xprt);
			return;
		}
		if (!svc_getargs(xprt, xdr_pmap, (caddr_t)&reg)) {
			svcerr_decode(xprt);
			break;
		}
		for (prevpml = NULL, pml = pmaplist; pml != NULL; ) {
			if ((pml->pml_map.pm_prog != reg.pm_prog) ||
			    (pml->pml_map.pm_vers != reg.pm_vers)) {
				/* both pml & prevpml move forwards */
				prevpml = pml;
				pml = pml->pml_next;
				continue;
			}
			if ((pml->pml_map.pm_port < IPPORT_RESERVED ||
			    pml->pml_map.pm_port == NFS_PORT) &&
			    htons(fromsin->sin_port) >= IPPORT_RESERVED) {
				syslog(LOG_WARNING,
				    "resvport unset attempt by non-root");
				break;
			}

			/* found it; pml moves forward, prevpml stays */
			ans = 1;
			t = pml;
			pml = pml->pml_next;
			if (prevpml == NULL)
				pmaplist = pml;
			else
				prevpml->pml_next = pml;
			free(t);
		}
		if ((!svc_sendreply(xprt, xdr_long, (caddr_t)&ans)) &&
		    debugging) {
			fprintf(stderr, "svc_sendreply\n");
			abort();
		}
		break;
	case PMAPPROC_GETPORT:
		/*
		 * Lookup the mapping for a program,version and return its port
		 */
		if (!svc_getargs(xprt, xdr_pmap, (caddr_t)&reg)) {
			svcerr_decode(xprt);
			break;
		}
		fnd = find_service(reg.pm_prog, reg.pm_vers, reg.pm_prot);
		if (fnd)
			port = fnd->pml_map.pm_port;
		else
			port = 0;
		if ((!svc_sendreply(xprt, xdr_long, (caddr_t)&port)) &&
		    debugging) {
			fprintf(stderr, "svc_sendreply\n");
			abort();
		}
		break;
	case PMAPPROC_DUMP:
		/*
		 * Return the current set of mapped program,version
		 */
		if (!svc_getargs(xprt, xdr_void, NULL)) {
			svcerr_decode(xprt);
			break;
		}
		if (!svc_sendreply(xprt, xdr_pmaplist, (caddr_t)&pmaplist) &&
		    debugging) {
			fprintf(stderr, "svc_sendreply\n");
			abort();
		}
		break;
	case PMAPPROC_CALLIT:
		/*
		 * Calls a procedure on the local machine.  If the requested
		 * procedure is not registered this procedure does not return
		 * error information!!
		 * This procedure is only supported on rpc/udp and calls via
		 * rpc/udp.  It passes null authentication parameters.
		 */
		callit(rqstp, xprt);
		break;
	default:
		svcerr_noproc(xprt);
		break;
	}
}


/*
 * Stuff for the rmtcall service
 */
#define ARGSIZE 9000

struct encap_parms {
	u_int arglen;
	char *args;
};

static bool_t
xdr_encap_parms(XDR *xdrs, struct encap_parms *epp)
{

	return (xdr_bytes(xdrs, &(epp->args), &(epp->arglen), ARGSIZE));
}

struct rmtcallargs {
	u_long	rmt_prog;
	u_long	rmt_vers;
	u_long	rmt_port;
	u_long	rmt_proc;
	struct encap_parms rmt_args;
};

/*
 * Version of xdr_rmtcall_args() that supports both directions
 */
static bool_t
portmap_xdr_rmtcall_args(XDR *xdrs, struct rmtcallargs *cap)
{

	/* does not get a port number */
	if (xdr_u_long(xdrs, &(cap->rmt_prog)) &&
	    xdr_u_long(xdrs, &(cap->rmt_vers)) &&
	    xdr_u_long(xdrs, &(cap->rmt_proc))) {
		return (xdr_encap_parms(xdrs, &(cap->rmt_args)));
	}
	return (FALSE);
}

/*
 * Version of xdr_rmtcallres() that supports both directions
 */
static bool_t
portmap_xdr_rmtcallres(XDR *xdrs, struct rmtcallargs *cap)
{
	if (xdr_u_long(xdrs, &(cap->rmt_port)))
		return (xdr_encap_parms(xdrs, &(cap->rmt_args)));
	return (FALSE);
}

/*
 * only worries about the struct encap_parms part of struct rmtcallargs.
 * The arglen must already be set!!
 */
static bool_t
xdr_opaque_parms(XDR *xdrs, struct rmtcallargs *cap)
{

	return (xdr_opaque(xdrs, cap->rmt_args.args, cap->rmt_args.arglen));
}

/*
 * This routine finds and sets the length of incoming opaque paraters
 * and then calls xdr_opaque_parms.
 */
static bool_t
xdr_len_opaque_parms(XDR *xdrs, struct rmtcallargs *cap)
{
	u_int beginpos, lowpos, highpos, currpos, pos;

	beginpos = lowpos = pos = xdr_getpos(xdrs);
	highpos = lowpos + ARGSIZE;
	while (highpos >= lowpos) {
		currpos = (lowpos + highpos) / 2;
		if (xdr_setpos(xdrs, currpos)) {
			pos = currpos;
			lowpos = currpos + 1;
		} else {
			highpos = currpos - 1;
		}
	}
	xdr_setpos(xdrs, beginpos);
	cap->rmt_args.arglen = pos - beginpos;
	return (xdr_opaque_parms(xdrs, cap));
}

/*
 * Call a remote procedure service
 * This procedure is very quiet when things go wrong.
 * The proc is written to support broadcast rpc.  In the broadcast case,
 * a machine should shut-up instead of complain, less the requestor be
 * overrun with complaints at the expense of not hearing a valid reply ...
 *
 * This now forks so that the program & process that it calls can call
 * back to the portmapper.
 */
void
callit(struct svc_req *rqstp, SVCXPRT *xprt)
{
	struct rmtcallargs a;
	struct pmaplist *pml;
	u_short port;
	struct sockaddr_in me;
	pid_t pid;
	int so = -1;
	CLIENT *client;
	struct authunix_parms *au = (struct authunix_parms *)rqstp->rq_clntcred;
	struct timeval timeout;
	char buf[ARGSIZE];

	timeout.tv_sec = 5;
	timeout.tv_usec = 0;
	a.rmt_args.args = buf;
	if (!svc_getargs(xprt, portmap_xdr_rmtcall_args, (caddr_t)&a))
		return;
	if (!check_callit(svc_getcaller(xprt), a.rmt_prog, a.rmt_proc))
		return;
	if ((pml = find_service(a.rmt_prog, a.rmt_vers,
	    (u_long)IPPROTO_UDP)) == NULL)
		return;

	/*
	 * fork a child to do the work.  Parent immediately returns.
	 * Child exits upon completion.
	 */
	if ((pid = fork()) != 0) {
		if (pid == -1)
			syslog(LOG_ERR, "CALLIT (prog %lu): fork: %m",
			    a.rmt_prog);
		return;
	}

	if (pledge("stdio inet", NULL) == -1)
		err(1, "pledge");

	port = pml->pml_map.pm_port;
	get_myaddress(&me);
	me.sin_port = htons(port);

	/* Avoid implicit binding to reserved port by clntudp_create() */
	so = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_UDP);
	if (so == -1)
		exit(1);

	client = clntudp_create(&me, a.rmt_prog, a.rmt_vers, timeout, &so);
	if (client != NULL) {
		if (rqstp->rq_cred.oa_flavor == AUTH_UNIX)
			client->cl_auth = authunix_create(au->aup_machname,
			    au->aup_uid, au->aup_gid, au->aup_len, au->aup_gids);
		a.rmt_port = (u_long)port;
		if (clnt_call(client, a.rmt_proc, xdr_opaque_parms, &a,
		    xdr_len_opaque_parms, &a, timeout) == RPC_SUCCESS)
			svc_sendreply(xprt, portmap_xdr_rmtcallres, (caddr_t)&a);
		AUTH_DESTROY(client->cl_auth);
		clnt_destroy(client);
	}
	(void)close(so);
	exit(0);
}

void
reap(int signo)
{
	int save_errno = errno;

	while (wait3(NULL, WNOHANG, NULL) > 0)
		;
	errno = save_errno;
}

#define	NFSPROG			((u_long) 100003)
#define	MOUNTPROG		((u_long) 100005)
#define	YPXPROG			((u_long) 100069)
#define	YPPROG			((u_long) 100004)
#define	YPPROC_DOMAIN_NONACK	((u_long) 2)
#define	MOUNTPROC_MNT		((u_long) 1)
#define XXXPROC_NOP		((u_long) 0)

int
check_callit(struct sockaddr_in *addr, u_long prog, u_long aproc)
{
	if ((prog == PMAPPROG && aproc != XXXPROC_NOP) ||
	    (prog == NFSPROG && aproc != XXXPROC_NOP) ||
	    (prog == YPXPROG && aproc != XXXPROC_NOP) ||
	    (prog == MOUNTPROG && aproc == MOUNTPROC_MNT) ||
	    (prog == YPPROG && aproc != YPPROC_DOMAIN_NONACK)) {
		syslog(LOG_WARNING,
		    "callit prog %ld aproc %ld (might be from %s)",
		    prog, aproc, inet_ntoa(addr->sin_addr));
		return (FALSE);
	}
	return (TRUE);
}
