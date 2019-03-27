/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1995
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * ypserv startup function.
 * We need out own main() since we have to do some additional work
 * that rpcgen won't do for us. Most of this file was generated using
 * rpcgen.new, and later modified.
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include "yp.h"
#include <err.h>
#include <errno.h>
#include <memory.h>
#include <stdio.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h> /* getenv, exit */
#include <string.h> /* strcmp */
#include <syslog.h>
#include <unistd.h>
#ifdef __cplusplus
#include <sysent.h> /* getdtablesize, open */
#endif /* __cplusplus */
#include <netinet/in.h>
#include <netdb.h>
#include "yp_extern.h"
#include <netconfig.h>
#include <rpc/rpc.h>
#include <rpc/rpc_com.h>

#ifndef SIG_PF
#define	SIG_PF void(*)(int)
#endif

#define	_RPCSVC_CLOSEDOWN 120
int _rpcpmstart;		/* Started by a port monitor ? */
static int _rpcfdtype;  /* Whether Stream or Datagram? */
static int _rpcaf;
static int _rpcfd;

/* States a server can be in wrt request */
#define	_IDLE 0
#define	_SERVED 1
#define	_SERVING 2

extern void ypprog_1(struct svc_req *, SVCXPRT *);
extern void ypprog_2(struct svc_req *, SVCXPRT *);
extern int _rpc_dtablesize(void);
extern int _rpcsvcstate;	 /* Set when a request is serviced */
char *progname = "ypserv";
char *yp_dir = _PATH_YP;
/*int debug = 0;*/
int do_dns = 0;
int resfd;

struct socklistent {
	int				sle_sock;
	struct sockaddr_storage		sle_ss;
	SLIST_ENTRY(socklistent)	sle_next;
};
static SLIST_HEAD(, socklistent) sle_head =
	SLIST_HEAD_INITIALIZER(sle_head);

struct bindaddrlistent {
	const char			*ble_hostname;
	SLIST_ENTRY(bindaddrlistent)	ble_next;
};
static SLIST_HEAD(, bindaddrlistent) ble_head =
	SLIST_HEAD_INITIALIZER(ble_head);

static char *servname = "0";

static
void _msgout(char* msg, ...)
{
	va_list ap;

	va_start(ap, msg);
	if (debug) {
		if (_rpcpmstart)
			vsyslog(LOG_ERR, msg, ap);
		else
			vwarnx(msg, ap);
	} else
		vsyslog(LOG_ERR, msg, ap);
	va_end(ap);
}

pid_t	yp_pid;

static void
yp_svc_run(void)
{
#ifdef FD_SETSIZE
	fd_set readfds;
#else
	int readfds;
#endif /* def FD_SETSIZE */
	int fd_setsize = _rpc_dtablesize();
	struct timeval timeout;

	/* Establish the identity of the parent ypserv process. */
	yp_pid = getpid();

	for (;;) {
#ifdef FD_SETSIZE
		readfds = svc_fdset;
#else
		readfds = svc_fds;
#endif /* def FD_SETSIZE */

		FD_SET(resfd, &readfds);

		timeout.tv_sec = RESOLVER_TIMEOUT;
		timeout.tv_usec = 0;
		switch (select(fd_setsize, &readfds, NULL, NULL,
			       &timeout)) {
		case -1:
			if (errno == EINTR) {
				continue;
			}
			warn("svc_run: - select failed");
			return;
		case 0:
			if (getpid() == yp_pid)
				yp_prune_dnsq();
			break;
		default:
			if (getpid() == yp_pid) {
				if (FD_ISSET(resfd, &readfds)) {
					yp_run_dnsq();
					FD_CLR(resfd, &readfds);
				}
				svc_getreqset(&readfds);
			}
		}
		if (yp_pid != getpid())
			_exit(0);
	}
}

static void
unregister(void)
{
	(void)svc_unreg(YPPROG, YPVERS);
	(void)svc_unreg(YPPROG, YPOLDVERS);
}

static void
reaper(int sig)
{
	int			status;
	int			saved_errno;

	saved_errno = errno;

	if (sig == SIGHUP) {
		load_securenets();
#ifdef DB_CACHE
		yp_flush_all();
#endif
		errno = saved_errno;
		return;
	}

	if (sig == SIGCHLD) {
		while (wait3(&status, WNOHANG, NULL) > 0)
			children--;
	} else {
		unregister();
		exit(0);
	}
	errno = saved_errno;
	return;
}

static void
usage(void)
{
	fprintf(stderr, "usage: ypserv [-h addr] [-d] [-n] [-p path] [-P port]\n");
	exit(1);
}

static void
closedown(int sig)
{
	if (_rpcsvcstate == _IDLE) {
		extern fd_set svc_fdset;
		static int size;
		int i, openfd;

		if (_rpcfdtype == SOCK_DGRAM) {
			unregister();
			exit(0);
		}
		if (size == 0) {
			size = getdtablesize();
		}
		for (i = 0, openfd = 0; i < size && openfd < 2; i++)
			if (FD_ISSET(i, &svc_fdset))
				openfd++;
		if (openfd <= 1) {
			unregister();
			exit(0);
		}
	}
	if (_rpcsvcstate == _SERVED)
		_rpcsvcstate = _IDLE;

	(void) signal(SIGALRM, (SIG_PF) closedown);
	(void) alarm(_RPCSVC_CLOSEDOWN/2);
}

static int
create_service(const int sock, const struct netconfig *nconf,
	const struct __rpc_sockinfo *si)
{
	int error;

	SVCXPRT *transp;
	struct addrinfo hints, *res, *res0;
	struct socklistent *slep;
	struct bindaddrlistent *blep;
	struct netbuf svcaddr;

	SLIST_INIT(&sle_head);
	memset(&hints, 0, sizeof(hints));
	memset(&svcaddr, 0, sizeof(svcaddr));

	hints.ai_family = si->si_af;
	hints.ai_socktype = si->si_socktype;
	hints.ai_protocol = si->si_proto;

	/*
	 * Build socketlist from bindaddrlist.
	 */
	if (sock == RPC_ANYFD) {
		SLIST_FOREACH(blep, &ble_head, ble_next) {
			if (blep->ble_hostname == NULL)
				hints.ai_flags = AI_PASSIVE;
			else
				hints.ai_flags = 0;
			error = getaddrinfo(blep->ble_hostname, servname,
				    &hints, &res0);
			if (error) {
				_msgout("getaddrinfo(): %s",
				    gai_strerror(error));
				return -1;
			}
			for (res = res0; res; res = res->ai_next) {
				int s;

				s = __rpc_nconf2fd(nconf);
				if (s < 0) {
					if (errno == EAFNOSUPPORT)
						_msgout("unsupported"
						    " transport: %s",
						    nconf->nc_netid);
					else
						_msgout("cannot create"
						    " %s socket: %s",
						    nconf->nc_netid,
						    strerror(errno));
					freeaddrinfo(res0);
					return -1;
				}
				if (bindresvport_sa(s, res->ai_addr) == -1) {
					if ((errno != EPERM) ||
					    (bind(s, res->ai_addr,
					    res->ai_addrlen) == -1)) {
						_msgout("cannot bind "
						    "%s socket: %s",
						    nconf->nc_netid,
						strerror(errno));
						freeaddrinfo(res0);
						close(sock);
						return -1;
					}
				}
				if (nconf->nc_semantics != NC_TPI_CLTS)
					listen(s, SOMAXCONN);

				slep = malloc(sizeof(*slep));
				if (slep == NULL) {
					_msgout("malloc failed: %s",
					    strerror(errno));
					freeaddrinfo(res0);
					close(s);
					return -1;
				}
				memset(slep, 0, sizeof(*slep));
				memcpy(&slep->sle_ss, res->ai_addr,
				    res->ai_addrlen);
				slep->sle_sock = s;
				SLIST_INSERT_HEAD(&sle_head, slep, sle_next);

				/*
				 * If servname == "0", redefine it by using
				 * the bound socket.
				 */
				if (strncmp("0", servname, 1) == 0) {
					struct sockaddr *sap;
					socklen_t slen;
					char *sname;

					sname = malloc(NI_MAXSERV);
					if (sname == NULL) {
						_msgout("malloc(): %s",
						    strerror(errno));
						freeaddrinfo(res0);
						close(s);
						return -1;
					}
					memset(sname, 0, NI_MAXSERV);

					sap = (struct sockaddr *)&slep->sle_ss;
					slen = sizeof(*sap);
					error = getsockname(s, sap, &slen);
					if (error) {
						_msgout("getsockname(): %s",
						    strerror(errno));
						freeaddrinfo(res0);
						close(s);
						free(sname);
						return -1;
					}
					error = getnameinfo(sap, slen,
					    NULL, 0,
					    sname, NI_MAXSERV,
					    NI_NUMERICHOST | NI_NUMERICSERV);
					if (error) {
						_msgout("getnameinfo(): %s",
						    strerror(errno));
						freeaddrinfo(res0);
						close(s);
						free(sname);
						return -1;
					}
					servname = sname;
				}
			}
			freeaddrinfo(res0);
		}
	} else {
		slep = malloc(sizeof(*slep));
		if (slep == NULL) {
			_msgout("malloc failed: %s", strerror(errno));
			return -1;
		}
		memset(slep, 0, sizeof(*slep));
		slep->sle_sock = sock;
		SLIST_INSERT_HEAD(&sle_head, slep, sle_next);
	}

	/*
	 * Traverse socketlist and create rpc service handles for each socket.
	 */
	SLIST_FOREACH(slep, &sle_head, sle_next) {
		if (nconf->nc_semantics == NC_TPI_CLTS)
			transp = svc_dg_create(slep->sle_sock, 0, 0);
		else
			transp = svc_vc_create(slep->sle_sock, RPC_MAXDATASIZE,
			    RPC_MAXDATASIZE);
		if (transp == NULL) {
			_msgout("unable to create service: %s",
			    nconf->nc_netid);
			continue;
		}
		if (!svc_reg(transp, YPPROG, YPOLDVERS, ypprog_1, NULL)) {
			svc_destroy(transp);
			close(slep->sle_sock);
			_msgout("unable to register (YPPROG, YPOLDVERS, %s):"
			    " %s", nconf->nc_netid, strerror(errno));
			continue;
		}
		if (!svc_reg(transp, YPPROG, YPVERS, ypprog_2, NULL)) {
			svc_destroy(transp);
			close(slep->sle_sock);
			_msgout("unable to register (YPPROG, YPVERS, %s): %s",
			    nconf->nc_netid, strerror(errno));
			continue;
		}
	}
	while(!(SLIST_EMPTY(&sle_head)))
		SLIST_REMOVE_HEAD(&sle_head, sle_next);

	/*
	 * Register RPC service to rpcbind by using AI_PASSIVE address.
	 */
	hints.ai_flags = AI_PASSIVE;
	error = getaddrinfo(NULL, servname, &hints, &res0);
	if (error) {
		_msgout("getaddrinfo(): %s", gai_strerror(error));
		return -1;
	}
	svcaddr.buf = res0->ai_addr;
	svcaddr.len = res0->ai_addrlen;

	if (si->si_af == AF_INET) {
		/* XXX: ignore error intentionally */
		rpcb_set(YPPROG, YPOLDVERS, nconf, &svcaddr);
	}
	/* XXX: ignore error intentionally */
	rpcb_set(YPPROG, YPVERS, nconf, &svcaddr);
	freeaddrinfo(res0);
	return 0;
}

int
main(int argc, char *argv[])
{
	int ch;
	int error;
	int ntrans;
	
	void *nc_handle;
	struct netconfig *nconf;
	struct __rpc_sockinfo si;
	struct bindaddrlistent *blep;

	memset(&si, 0, sizeof(si));
	SLIST_INIT(&ble_head);

	while ((ch = getopt(argc, argv, "dh:np:P:")) != -1) {
		switch (ch) {
		case 'd':
			debug = ypdb_debug = 1;
			break;
		case 'h':
			blep = malloc(sizeof(*blep));
			if (blep == NULL)
				err(1, "malloc() failed: -h %s", optarg);
			blep->ble_hostname = optarg;
			SLIST_INSERT_HEAD(&ble_head, blep, ble_next);
			break;
		case 'n':
			do_dns = 1;
			break;
		case 'p':
			yp_dir = optarg;
			break;
		case 'P':
			servname = optarg;
			break;
		default:
			usage();
		}
	}
	/*
	 * Add "anyaddr" entry if no -h is specified.
	 */
	if (SLIST_EMPTY(&ble_head)) {
		blep = malloc(sizeof(*blep));
		if (blep == NULL)
			err(1, "malloc() failed");
		memset(blep, 0, sizeof(*blep));
		SLIST_INSERT_HEAD(&ble_head, blep, ble_next);
	}

	load_securenets();
	yp_init_resolver();
#ifdef DB_CACHE
	yp_init_dbs();
#endif
	nc_handle = setnetconfig();
	if (nc_handle == NULL)
		err(1, "cannot read %s", NETCONFIG);
	if (__rpc_fd2sockinfo(0, &si) != 0) {
		/* invoked from inetd */
		_rpcpmstart = 1;
		_rpcfdtype = si.si_socktype;
		_rpcaf = si.si_af;
		_rpcfd = 0;
		openlog("ypserv", LOG_PID, LOG_DAEMON);
	} else {
		/* standalone mode */
		if (!debug) {
			if (daemon(0,0)) {
				err(1,"cannot fork");
			}
			openlog("ypserv", LOG_PID, LOG_DAEMON);
		}
		_rpcpmstart = 0;
		_rpcaf = AF_INET;
		_rpcfd = RPC_ANYFD;
		unregister();
	}

	if (madvise(NULL, 0, MADV_PROTECT) != 0)
		_msgout("madvise(): %s", strerror(errno));

	/*
	 * Create RPC service for each transport.
	 */
	ntrans = 0;
	while((nconf = getnetconfig(nc_handle))) {
		if ((nconf->nc_flag & NC_VISIBLE)) {
			if (__rpc_nconf2sockinfo(nconf, &si) == 0) {
				_msgout("cannot get information for %s.  "
				    "Ignored.", nconf->nc_netid);
				continue;
			}
			if (_rpcpmstart) {
				if (si.si_socktype != _rpcfdtype ||
				    si.si_af != _rpcaf)
					continue;
			} else if (si.si_af != _rpcaf)
					continue;
			error = create_service(_rpcfd, nconf, &si);
			if (error) {
				endnetconfig(nc_handle);
				exit(1);
			}
			ntrans++;
		}
	}
	endnetconfig(nc_handle);
	while(!(SLIST_EMPTY(&ble_head)))
		SLIST_REMOVE_HEAD(&ble_head, ble_next);
	if (ntrans == 0) {
		_msgout("no transport is available.  Aborted.");
		exit(1);
	}
	if (_rpcpmstart) {
		(void) signal(SIGALRM, (SIG_PF) closedown);
		(void) alarm(_RPCSVC_CLOSEDOWN/2);
	}
/*
 * Make sure SIGPIPE doesn't blow us away while servicing TCP
 * connections.
 */
	(void) signal(SIGPIPE, SIG_IGN);
	(void) signal(SIGCHLD, (SIG_PF) reaper);
	(void) signal(SIGTERM, (SIG_PF) reaper);
	(void) signal(SIGINT, (SIG_PF) reaper);
	(void) signal(SIGHUP, (SIG_PF) reaper);
	yp_svc_run();
	_msgout("svc_run returned");
	exit(1);
	/* NOTREACHED */
}
