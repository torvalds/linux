/*	$NetBSD: rpcbind.c,v 1.3 2002/11/08 00:16:40 fvdl Exp $	*/
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
 * Copyright (c) 1984 - 1991 by Sun Microsystems, Inc.
 */

/* #ident	"@(#)rpcbind.c	1.19	94/04/25 SMI" */

#if 0
#ifndef lint
static	char sccsid[] = "@(#)rpcbind.c 1.35 89/04/21 Copyr 1984 Sun Micro";
#endif
#endif

/*
 * rpcbind.c
 * Implements the program, version to address mapping for rpc.
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <rpc/rpc.h>
#include <rpc/rpc_com.h>
#ifdef PORTMAP
#include <netinet/in.h>
#endif
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <netconfig.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <err.h>
#include <pwd.h>
#include <string.h>
#include <errno.h>
#include "rpcbind.h"

/* Global variables */
int debugging = 0;	/* Tell me what's going on */
int doabort = 0;	/* When debugging, do an abort on errors */
int terminate_rfd;	/* Pipefd to wake on signal */
volatile sig_atomic_t doterminate = 0;	/* Terminal signal received */
rpcblist_ptr list_rbl;	/* A list of version 3/4 rpcbind services */
int rpcbindlockfd;

/* who to suid to if -s is given */
#define RUN_AS  "daemon"

#define RPCBINDDLOCK "/var/run/rpcbind.lock"

static int runasdaemon = 0;
int insecure = 0;
int oldstyle_local = 0;
#ifdef LIBWRAP
int libwrap = 0;
#endif
int verboselog = 0;

static char **hosts = NULL;
static struct sockaddr **bound_sa;
static int ipv6_only = 0;
static int nhosts = 0;
static int on = 1;
static int terminate_wfd;

#ifdef WARMSTART
/* Local Variable */
static int warmstart = 0;	/* Grab an old copy of registrations. */
#endif

#ifdef PORTMAP
struct pmaplist *list_pml;	/* A list of version 2 rpcbind services */
char *udptrans;		/* Name of UDP transport */
char *tcptrans;		/* Name of TCP transport */
char *udp_uaddr;	/* Universal UDP address */
char *tcp_uaddr;	/* Universal TCP address */
#endif
static char servname[] = "rpcbind";
static char superuser[] = "superuser";

int main(int, char *[]);

static int init_transport(struct netconfig *);
static void rbllist_add(rpcprog_t, rpcvers_t, struct netconfig *,
			     struct netbuf *);
static void terminate(int);
static void parseargs(int, char *[]);
static void update_bound_sa(void);

int
main(int argc, char *argv[])
{
	struct netconfig *nconf;
	void *nc_handle;	/* Net config handle */
	struct rlimit rl;
	int maxrec = RPC_MAXDATASIZE;
	int error, fds[2];

	parseargs(argc, argv);

	update_bound_sa();

	/* Check that another rpcbind isn't already running. */
	if ((rpcbindlockfd = (open(RPCBINDDLOCK,
	    O_RDONLY|O_CREAT, 0444))) == -1)
		err(1, "%s", RPCBINDDLOCK);

	if(flock(rpcbindlockfd, LOCK_EX|LOCK_NB) == -1 && errno == EWOULDBLOCK)
		errx(1, "another rpcbind is already running. Aborting");

	getrlimit(RLIMIT_NOFILE, &rl);
	if (rl.rlim_cur < 128) {
		if (rl.rlim_max <= 128)
			rl.rlim_cur = rl.rlim_max;
		else
			rl.rlim_cur = 128;
		setrlimit(RLIMIT_NOFILE, &rl);
	}
	openlog("rpcbind", LOG_CONS, LOG_DAEMON);
	if (geteuid()) { /* This command allowed only to root */
		fprintf(stderr, "Sorry. You are not superuser\n");
		exit(1);
	}
	nc_handle = setnetconfig(); 	/* open netconfig file */
	if (nc_handle == NULL) {
		syslog(LOG_ERR, "could not read /etc/netconfig");
		exit(1);
	}
#ifdef PORTMAP
	udptrans = "";
	tcptrans = "";
#endif

	nconf = getnetconfigent("local");
	if (nconf == NULL)
		nconf = getnetconfigent("unix");
	if (nconf == NULL) {
		syslog(LOG_ERR, "%s: can't find local transport\n", argv[0]);
		exit(1);
	}

	rpc_control(RPC_SVC_CONNMAXREC_SET, &maxrec);

	init_transport(nconf);

	while ((nconf = getnetconfig(nc_handle))) {
	    if (nconf->nc_flag & NC_VISIBLE) {
	    	if (ipv6_only == 1 && strcmp(nconf->nc_protofmly,
		    "inet") == 0) {
		    /* DO NOTHING */
		} else
		    init_transport(nconf);
	    }
	}
	endnetconfig(nc_handle);

	/*
	 * Allocate pipe fd to wake main thread from signal handler in non-racy
	 * way.
	 */
	error = pipe(fds);
	if (error != 0)
		err(1, "pipe failed");
	terminate_rfd = fds[0];
	terminate_wfd = fds[1];

	/* catch the usual termination signals for graceful exit */
	(void) signal(SIGCHLD, reap);
	(void) signal(SIGINT, terminate);
	(void) signal(SIGTERM, terminate);
	(void) signal(SIGQUIT, terminate);
	/* ignore others that could get sent */
	(void) signal(SIGPIPE, SIG_IGN);
	(void) signal(SIGHUP, SIG_IGN);
	(void) signal(SIGUSR1, SIG_IGN);
	(void) signal(SIGUSR2, SIG_IGN);
#ifdef WARMSTART
	if (warmstart) {
		read_warmstart();
	}
#endif
	if (debugging) {
		printf("rpcbind debugging enabled.");
		if (doabort) {
			printf("  Will abort on errors!\n");
		} else {
			printf("\n");
		}
	} else {
		if (daemon(0, 0))
			err(1, "fork failed");
	}

	if (runasdaemon) {
		struct passwd *p;

		if((p = getpwnam(RUN_AS)) == NULL) {
			syslog(LOG_ERR, "cannot get uid of daemon: %m");
			exit(1);
		}
		if (setuid(p->pw_uid) == -1) {
			syslog(LOG_ERR, "setuid to daemon failed: %m");
			exit(1);
		}
	}

	network_init();

	my_svc_run();
	syslog(LOG_ERR, "svc_run returned unexpectedly");
	rpcbind_abort();
	/* NOTREACHED */

	return 0;
}

/*
 * Adds the entry into the rpcbind database.
 * If PORTMAP, then for UDP and TCP, it adds the entries for version 2 also
 * Returns 0 if succeeds, else fails
 */
static int
init_transport(struct netconfig *nconf)
{
	int fd;
	struct t_bind taddr;
	struct addrinfo hints, *res = NULL;
	struct __rpc_sockinfo si;
	SVCXPRT	*my_xprt;
	int status;	/* bound checking ? */
	int aicode;
	int addrlen;
	int nhostsbak;
	int bound;
	struct sockaddr *sa;
	u_int32_t host_addr[4];  /* IPv4 or IPv6 */
	struct sockaddr_un sun;
	mode_t oldmask;

	if ((nconf->nc_semantics != NC_TPI_CLTS) &&
	    (nconf->nc_semantics != NC_TPI_COTS) &&
	    (nconf->nc_semantics != NC_TPI_COTS_ORD))
	    return (1);	/* not my type */
#ifdef ND_DEBUG
	if (debugging) {
	    int i;
	    char **s;

	    (void)fprintf(stderr, "%s: %ld lookup routines :\n",
		nconf->nc_netid, nconf->nc_nlookups);
	    for (i = 0, s = nconf->nc_lookups; i < nconf->nc_nlookups;
		i++, s++)
		fprintf(stderr, "[%d] - %s\n", i, *s);
	}
#endif

	/*
	 * XXX - using RPC library internal functions.
	 */
	if ((strcmp(nconf->nc_netid, "local") == 0) ||
	    (strcmp(nconf->nc_netid, "unix") == 0)) {
	    /* 
	     * For other transports we call this later, for each socket we
	     * like to bind.
	     */
	    if ((fd = __rpc_nconf2fd(nconf)) < 0) {
		int non_fatal = 0;
		if (errno == EAFNOSUPPORT)
		    non_fatal = 1;
		syslog(non_fatal?LOG_DEBUG:LOG_ERR, "cannot create socket for %s",
		    nconf->nc_netid);
		return (1);
	    }
	}

	if (!__rpc_nconf2sockinfo(nconf, &si)) {
	    syslog(LOG_ERR, "cannot get information for %s",
		nconf->nc_netid);
	    return (1);
	}

	if ((strcmp(nconf->nc_netid, "local") == 0) ||
	    (strcmp(nconf->nc_netid, "unix") == 0)) {
	    memset(&sun, 0, sizeof sun);
	    sun.sun_family = AF_LOCAL;
	    unlink(_PATH_RPCBINDSOCK);
	    strcpy(sun.sun_path, _PATH_RPCBINDSOCK);
	    sun.sun_len = SUN_LEN(&sun);
	    addrlen = sizeof (struct sockaddr_un);
	    sa = (struct sockaddr *)&sun;
	} else {
	    /* Get rpcbind's address on this transport */

	    memset(&hints, 0, sizeof hints);
	    hints.ai_flags = AI_PASSIVE;
	    hints.ai_family = si.si_af;
	    hints.ai_socktype = si.si_socktype;
	    hints.ai_protocol = si.si_proto;
	}

	if ((strcmp(nconf->nc_netid, "local") != 0) &&
	    (strcmp(nconf->nc_netid, "unix") != 0)) {
	    /*
	     * If no hosts were specified, just bind to INADDR_ANY.
	     * Otherwise  make sure 127.0.0.1 is added to the list.
	     */
	    nhostsbak = nhosts + 1;
	    hosts = realloc(hosts, nhostsbak * sizeof(char *));
	    if (nhostsbak == 1)
	        hosts[0] = "*";
	    else {
		if (hints.ai_family == AF_INET) {
		    hosts[nhostsbak - 1] = "127.0.0.1";
		} else if (hints.ai_family == AF_INET6) {
		    hosts[nhostsbak - 1] = "::1";
		} else
		    return 1;
	    }

	    /*
	     * Bind to specific IPs if asked to
	     */
	    bound = 0;
	    while (nhostsbak > 0) {
		--nhostsbak;
		/*
		 * XXX - using RPC library internal functions.
		 */
		if ((fd = __rpc_nconf2fd(nconf)) < 0) {
		    int non_fatal = 0;
		    if (errno == EAFNOSUPPORT &&
			nconf->nc_semantics != NC_TPI_CLTS) 
			non_fatal = 1;
		    syslog(non_fatal ? LOG_DEBUG : LOG_ERR, 
			"cannot create socket for %s", nconf->nc_netid);
		    return (1);
		}
		switch (hints.ai_family) {
		case AF_INET:
		    if (inet_pton(AF_INET, hosts[nhostsbak],
			host_addr) == 1) {
			hints.ai_flags &= AI_NUMERICHOST;
		    } else {
			/*
			 * Skip if we have an AF_INET6 address.
			 */
			if (inet_pton(AF_INET6,
			    hosts[nhostsbak], host_addr) == 1) {
			    close(fd);
			    continue;
			}
		    }
		    break;
		case AF_INET6:
		    if (inet_pton(AF_INET6, hosts[nhostsbak],
			host_addr) == 1) {
			hints.ai_flags &= AI_NUMERICHOST;
		    } else {
			/*
			 * Skip if we have an AF_INET address.
			 */
			if (inet_pton(AF_INET, hosts[nhostsbak],
			    host_addr) == 1) {
				close(fd);
				continue;
			}
		    }
		    if (setsockopt(fd, IPPROTO_IPV6,
			IPV6_V6ONLY, &on, sizeof on) < 0) {
			syslog(LOG_ERR,
			    "can't set v6-only binding for "
			    "ipv6 socket: %m");
			continue;
		    }
		    break;
		default:
		    break;
		}

		/*
		 * If no hosts were specified, just bind to INADDR_ANY
		 */
		if (strcmp("*", hosts[nhostsbak]) == 0)
		    hosts[nhostsbak] = NULL;
		if ((strcmp(nconf->nc_netid, "local") != 0) &&
		    (strcmp(nconf->nc_netid, "unix") != 0)) {
		    if ((aicode = getaddrinfo(hosts[nhostsbak],
			servname, &hints, &res)) != 0) {
			syslog(LOG_ERR,
			    "cannot get local address for %s: %s",
			    nconf->nc_netid, gai_strerror(aicode));
			continue;
		    }
		    addrlen = res->ai_addrlen;
		    sa = (struct sockaddr *)res->ai_addr;
		}
		oldmask = umask(S_IXUSR|S_IXGRP|S_IXOTH);
		if (bind(fd, sa, addrlen) != 0) {
		    syslog(LOG_ERR, "cannot bind %s on %s: %m",
			(hosts[nhostsbak] == NULL) ? "*" :
			    hosts[nhostsbak], nconf->nc_netid);
		    if (res != NULL)
			freeaddrinfo(res);
		    continue;
		} else
		    bound = 1;
		(void)umask(oldmask);

		/* Copy the address */
		taddr.addr.len = taddr.addr.maxlen = addrlen;
		taddr.addr.buf = malloc(addrlen);
		if (taddr.addr.buf == NULL) {
		    syslog(LOG_ERR,
			"cannot allocate memory for %s address",
			nconf->nc_netid);
		    if (res != NULL)
			freeaddrinfo(res);
		    return 1;
		}
		memcpy(taddr.addr.buf, sa, addrlen);
#ifdef ND_DEBUG
		if (debugging) {
		    /*
		     * for debugging print out our universal
		     * address
		     */
		    char *uaddr;
		    struct netbuf nb;

		    nb.buf = sa;
		    nb.len = nb.maxlen = sa->sa_len;
		    uaddr = taddr2uaddr(nconf, &nb);
		    (void)fprintf(stderr,
			"rpcbind : my address is %s\n", uaddr);
		    (void)free(uaddr);
		}
#endif

		if (nconf->nc_semantics != NC_TPI_CLTS)
		    listen(fd, SOMAXCONN);

		my_xprt = (SVCXPRT *)svc_tli_create(fd, nconf, &taddr,
		    RPC_MAXDATASIZE, RPC_MAXDATASIZE);
		if (my_xprt == (SVCXPRT *)NULL) {
		    syslog(LOG_ERR, "%s: could not create service",
			nconf->nc_netid);
		    goto error;
		}
	    }
	    if (!bound)
		return 1;
	} else {
	    oldmask = umask(S_IXUSR|S_IXGRP|S_IXOTH);
	    if (bind(fd, sa, addrlen) < 0) {
		syslog(LOG_ERR, "cannot bind %s: %m", nconf->nc_netid);
		if (res != NULL)
		    freeaddrinfo(res);
		return 1;
	    }
	    (void) umask(oldmask);

	    /* Copy the address */
	    taddr.addr.len = taddr.addr.maxlen = addrlen;
	    taddr.addr.buf = malloc(addrlen);
	    if (taddr.addr.buf == NULL) {
		syslog(LOG_ERR, "cannot allocate memory for %s address",
		    nconf->nc_netid);
		if (res != NULL)
		    freeaddrinfo(res);
		return 1;
	    }
	    memcpy(taddr.addr.buf, sa, addrlen);
#ifdef ND_DEBUG
	    if (debugging) {
		/* for debugging print out our universal address */
		char *uaddr;
		struct netbuf nb;

		nb.buf = sa;
		nb.len = nb.maxlen = sa->sa_len;
		uaddr = taddr2uaddr(nconf, &nb);
		(void) fprintf(stderr, "rpcbind : my address is %s\n",
		    uaddr);
		(void) free(uaddr);
	    }
#endif

	    if (nconf->nc_semantics != NC_TPI_CLTS)
		listen(fd, SOMAXCONN);

	    my_xprt = (SVCXPRT *)svc_tli_create(fd, nconf, &taddr,
		RPC_MAXDATASIZE, RPC_MAXDATASIZE);
	    if (my_xprt == (SVCXPRT *)NULL) {
		syslog(LOG_ERR, "%s: could not create service",
		    nconf->nc_netid);
		goto error;
	    }
	}

#ifdef PORTMAP
	/*
	 * Register both the versions for tcp/ip, udp/ip and local.
	 */
	if ((strcmp(nconf->nc_protofmly, NC_INET) == 0 &&
		(strcmp(nconf->nc_proto, NC_TCP) == 0 ||
		strcmp(nconf->nc_proto, NC_UDP) == 0)) ||
		(strcmp(nconf->nc_netid, "unix") == 0) ||
		(strcmp(nconf->nc_netid, "local") == 0)) {
		struct pmaplist *pml;

		if (!svc_register(my_xprt, PMAPPROG, PMAPVERS,
			pmap_service, 0)) {
			syslog(LOG_ERR, "could not register on %s",
					nconf->nc_netid);
			goto error;
		}
		pml = malloc(sizeof (struct pmaplist));
		if (pml == NULL) {
			syslog(LOG_ERR, "no memory!");
			exit(1);
		}
		pml->pml_map.pm_prog = PMAPPROG;
		pml->pml_map.pm_vers = PMAPVERS;
		pml->pml_map.pm_port = PMAPPORT;
		if (strcmp(nconf->nc_proto, NC_TCP) == 0) {
			if (tcptrans[0]) {
				free(pml);
				pml = NULL;
				syslog(LOG_ERR,
				"cannot have more than one TCP transport");
				goto error;
			}
			tcptrans = strdup(nconf->nc_netid);
			pml->pml_map.pm_prot = IPPROTO_TCP;

			/* Let's snarf the universal address */
			/* "h1.h2.h3.h4.p1.p2" */
			tcp_uaddr = taddr2uaddr(nconf, &taddr.addr);
		} else if (strcmp(nconf->nc_proto, NC_UDP) == 0) {
			if (udptrans[0]) {
				syslog(LOG_ERR,
				"cannot have more than one UDP transport");
				goto error;
			}
			udptrans = strdup(nconf->nc_netid);
			pml->pml_map.pm_prot = IPPROTO_UDP;

			/* Let's snarf the universal address */
			/* "h1.h2.h3.h4.p1.p2" */
			udp_uaddr = taddr2uaddr(nconf, &taddr.addr);
		} else if (strcmp(nconf->nc_netid, "local") == 0)
			pml->pml_map.pm_prot = IPPROTO_ST;
		else if (strcmp(nconf->nc_netid, "unix") == 0)
			pml->pml_map.pm_prot = IPPROTO_ST;
		pml->pml_next = list_pml;
		list_pml = pml;

		/* Add version 3 information */
		pml = malloc(sizeof (struct pmaplist));
		if (pml == NULL) {
			syslog(LOG_ERR, "no memory!");
			exit(1);
		}
		pml->pml_map = list_pml->pml_map;
		pml->pml_map.pm_vers = RPCBVERS;
		pml->pml_next = list_pml;
		list_pml = pml;

		/* Add version 4 information */
		pml = malloc (sizeof (struct pmaplist));
		if (pml == NULL) {
			syslog(LOG_ERR, "no memory!");
			exit(1);
		}
		pml->pml_map = list_pml->pml_map;
		pml->pml_map.pm_vers = RPCBVERS4;
		pml->pml_next = list_pml;
		list_pml = pml;

		/* Also add version 2 stuff to rpcbind list */
		rbllist_add(PMAPPROG, PMAPVERS, nconf, &taddr.addr);
	}
#endif

	/* version 3 registration */
	if (!svc_reg(my_xprt, RPCBPROG, RPCBVERS, rpcb_service_3, NULL)) {
		syslog(LOG_ERR, "could not register %s version 3",
				nconf->nc_netid);
		goto error;
	}
	rbllist_add(RPCBPROG, RPCBVERS, nconf, &taddr.addr);

	/* version 4 registration */
	if (!svc_reg(my_xprt, RPCBPROG, RPCBVERS4, rpcb_service_4, NULL)) {
		syslog(LOG_ERR, "could not register %s version 4",
				nconf->nc_netid);
		goto error;
	}
	rbllist_add(RPCBPROG, RPCBVERS4, nconf, &taddr.addr);

	/* decide if bound checking works for this transport */
	status = add_bndlist(nconf, &taddr.addr);
#ifdef BIND_DEBUG
	if (debugging) {
		if (status < 0) {
			fprintf(stderr, "Error in finding bind status for %s\n",
				nconf->nc_netid);
		} else if (status == 0) {
			fprintf(stderr, "check binding for %s\n",
				nconf->nc_netid);
		} else if (status > 0) {
			fprintf(stderr, "No check binding for %s\n",
				nconf->nc_netid);
		}
	}
#endif
	/*
	 * rmtcall only supported on CLTS transports for now.
	 */
	if (nconf->nc_semantics == NC_TPI_CLTS) {
		status = create_rmtcall_fd(nconf);

#ifdef BIND_DEBUG
		if (debugging) {
			if (status < 0) {
				fprintf(stderr,
				    "Could not create rmtcall fd for %s\n",
					nconf->nc_netid);
			} else {
				fprintf(stderr, "rmtcall fd for %s is %d\n",
					nconf->nc_netid, status);
			}
		}
#endif
	}
	return (0);
error:
	close(fd);
	return (1);
}

/*
 * Create the list of addresses that we're bound to.  Normally, this
 * list is empty because we're listening on the wildcard address
 * (nhost == 0).  If -h is specified on the command line, then
 * bound_sa will have a list of the addresses that the program binds
 * to specifically.  This function takes that list and converts them to
 * struct sockaddr * and stores them in bound_sa.
 */
static void
update_bound_sa(void)
{
	struct addrinfo hints, *res = NULL;
	int i;

	if (nhosts == 0)
		return;
	bound_sa = malloc(sizeof(*bound_sa) * nhosts);
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	for (i = 0; i < nhosts; i++)  {
		if (getaddrinfo(hosts[i], NULL, &hints, &res) != 0)
			continue;
		bound_sa[i] = malloc(res->ai_addrlen);
		memcpy(bound_sa[i], res->ai_addr, res->ai_addrlen);
	}
}

/*
 * Match the sa against the list of addresses we've bound to.  If
 * we've not specifically bound to anything, we match everything.
 * Otherwise, if the IPv4 or IPv6 address matches one of the addresses
 * in bound_sa, we return true.  If not, we return false.
 */
int
listen_addr(const struct sockaddr *sa)
{
	int i;

	/*
	 * If nhosts == 0, then there were no -h options on the
	 * command line, so all addresses are addresses we're
	 * listening to.
	 */
	if (nhosts == 0)
		return 1;
	for (i = 0; i < nhosts; i++) {
		if (bound_sa[i] == NULL ||
		    sa->sa_family != bound_sa[i]->sa_family)
			continue;
		switch (sa->sa_family) {
		case AF_INET:
		  	if (memcmp(&SA2SINADDR(sa), &SA2SINADDR(bound_sa[i]),
			    sizeof(struct in_addr)) == 0)
				return (1);
			break;
#ifdef INET6
		case AF_INET6:
		  	if (memcmp(&SA2SIN6ADDR(sa), &SA2SIN6ADDR(bound_sa[i]),
			    sizeof(struct in6_addr)) == 0)
				return (1);
			break;
#endif
		default:
			break;
		}
	}
	return (0);
}

static void
rbllist_add(rpcprog_t prog, rpcvers_t vers, struct netconfig *nconf,
	    struct netbuf *addr)
{
	rpcblist_ptr rbl;

	rbl = malloc(sizeof (rpcblist));
	if (rbl == NULL) {
		syslog(LOG_ERR, "no memory!");
		exit(1);
	}

	rbl->rpcb_map.r_prog = prog;
	rbl->rpcb_map.r_vers = vers;
	rbl->rpcb_map.r_netid = strdup(nconf->nc_netid);
	rbl->rpcb_map.r_addr = taddr2uaddr(nconf, addr);
	rbl->rpcb_map.r_owner = strdup(superuser);
	rbl->rpcb_next = list_rbl;	/* Attach to global list */
	list_rbl = rbl;
}

/*
 * Catch the signal and die
 */
static void
terminate(int signum)
{
	char c = '\0';
	ssize_t wr;

	doterminate = signum;
	wr = write(terminate_wfd, &c, 1);
	if (wr < 1)
		_exit(2);
}

void
rpcbind_abort(void)
{
#ifdef WARMSTART
	write_warmstart();	/* Dump yourself */
#endif
	abort();
}

/* get command line options */
static void
parseargs(int argc, char *argv[])
{
	int c;

#ifdef WARMSTART
#define	WSOP	"w"
#else
#define	WSOP	""
#endif
#ifdef LIBWRAP
#define WRAPOP	"W"
#else
#define WRAPOP	""
#endif
	while ((c = getopt(argc, argv, "6adh:iLls" WRAPOP WSOP)) != -1) {
		switch (c) {
		case '6':
			ipv6_only = 1;
			break;
		case 'a':
			doabort = 1;	/* when debugging, do an abort on */
			break;		/* errors; for rpcbind developers */
					/* only! */
		case 'd':
			debugging = 1;
			break;
		case 'h':
			++nhosts;
			hosts = realloc(hosts, nhosts * sizeof(char *));
			if (hosts == NULL)
				errx(1, "Out of memory");
			hosts[nhosts - 1] = strdup(optarg);
			if (hosts[nhosts - 1] == NULL)
				errx(1, "Out of memory");
			break;
		case 'i':
			insecure = 1;
			break;
		case 'L':
			oldstyle_local = 1;
			break;
		case 'l':
			verboselog = 1;
			break;
		case 's':
			runasdaemon = 1;
			break;
#ifdef LIBWRAP
		case 'W':
			libwrap = 1;
			break;
#endif
#ifdef WARMSTART
		case 'w':
			warmstart = 1;
			break;
#endif
		default:	/* error */
			fprintf(stderr,
			    "usage: rpcbind [-6adiLls%s%s] [-h bindip]\n",
			    WRAPOP, WSOP);
			exit (1);
		}
	}
	if (doabort && !debugging) {
	    fprintf(stderr,
		"-a (abort) specified without -d (debugging) -- ignored.\n");
	    doabort = 0;
	}
#undef WSOP
}

void
reap(int dummy __unused)
{
	int save_errno = errno;
 
	while (wait3(NULL, WNOHANG, NULL) > 0)
		;       
	errno = save_errno;
}

void
toggle_verboselog(int dummy __unused)
{
	verboselog = !verboselog;
}
