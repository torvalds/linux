/*	$FreeBSD$	*/
/*	$KAME: route6d.c,v 1.104 2003/10/31 00:30:20 itojun Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef	lint
static const char _rcsid[] = "$KAME: route6d.c,v 1.104 2003/10/31 00:30:20 itojun Exp $";
#endif

#include <sys/param.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/uio.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/ip6.h>
#include <netinet/udp.h>
#include <err.h>
#include <errno.h>
#include <fnmatch.h>
#include <ifaddrs.h>
#include <netdb.h>
#ifdef HAVE_POLL_H
#include <poll.h>
#endif
#include <signal.h>
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "route6d.h"

#define	MAXFILTER	40
#define RT_DUMP_MAXRETRY	15

#ifdef	DEBUG
#define	INIT_INTERVAL6	6
#else
#define	INIT_INTERVAL6	10	/* Wait to submit an initial riprequest */
#endif

/* alignment constraint for routing socket */
#define ROUNDUP(a) \
	((a) > 0 ? (1 + (((a) - 1) | (sizeof(long) - 1))) : sizeof(long))
#define ADVANCE(x, n) (x += ROUNDUP((n)->sa_len))

struct ifc {			/* Configuration of an interface */
	TAILQ_ENTRY(ifc) ifc_next;

	char	ifc_name[IFNAMSIZ];		/* if name */
	int	ifc_index;			/* if index */
	int	ifc_mtu;			/* if mtu */
	int	ifc_metric;			/* if metric */
	u_int	ifc_flags;			/* flags */
	short	ifc_cflags;			/* IFC_XXX */
	struct	in6_addr ifc_mylladdr;		/* my link-local address */
	struct	sockaddr_in6 ifc_ripsin;	/* rip multicast address */
	TAILQ_HEAD(, ifac) ifc_ifac_head;	/* list of AF_INET6 addrs */
	TAILQ_HEAD(, iff) ifc_iff_head;		/* list of filters */
	int	ifc_joined;			/* joined to ff02::9 */
};
static TAILQ_HEAD(, ifc) ifc_head = TAILQ_HEAD_INITIALIZER(ifc_head);

struct ifac {			/* Adddress associated to an interface */
	TAILQ_ENTRY(ifac) ifac_next;

	struct	ifc *ifac_ifc;		/* back pointer */
	struct	in6_addr ifac_addr;	/* address */
	struct	in6_addr ifac_raddr;	/* remote address, valid in p2p */
	int	ifac_scope_id;		/* scope id */
	int	ifac_plen;		/* prefix length */
};

struct iff {			/* Filters for an interface */
	TAILQ_ENTRY(iff) iff_next;

	int	iff_type;
	struct	in6_addr iff_addr;
	int	iff_plen;
};

static struct	ifc **index2ifc;
static unsigned int	nindex2ifc;
static struct	ifc *loopifcp = NULL;	/* pointing to loopback */
#ifdef HAVE_POLL_H
static struct	pollfd set[2];
#else
static fd_set	*sockvecp;	/* vector to select() for receiving */
static fd_set	*recvecp;
static int	fdmasks;
static int	maxfd;		/* maximum fd for select() */
#endif
static int	rtsock;		/* the routing socket */
static int	ripsock;	/* socket to send/receive RIP datagram */

static struct	rip6 *ripbuf;	/* packet buffer for sending */

/*
 * Maintain the routes in a linked list.  When the number of the routes
 * grows, somebody would like to introduce a hash based or a radix tree
 * based structure.  I believe the number of routes handled by RIP is
 * limited and I don't have to manage a complex data structure, however.
 *
 * One of the major drawbacks of the linear linked list is the difficulty
 * of representing the relationship between a couple of routes.  This may
 * be a significant problem when we have to support route aggregation with
 * suppressing the specifics covered by the aggregate.
 */

struct riprt {
	TAILQ_ENTRY(riprt) rrt_next;	/* next destination */

	struct	riprt *rrt_same;	/* same destination - future use */
	struct	netinfo6 rrt_info;	/* network info */
	struct	in6_addr rrt_gw;	/* gateway */
	u_long	rrt_flags;		/* kernel routing table flags */
	u_long	rrt_rflags;		/* route6d routing table flags */
	time_t	rrt_t;			/* when the route validated */
	int	rrt_index;		/* ifindex from which this route got */
};
static TAILQ_HEAD(, riprt) riprt_head = TAILQ_HEAD_INITIALIZER(riprt_head);

static int	dflag = 0;	/* debug flag */
static int	qflag = 0;	/* quiet flag */
static int	nflag = 0;	/* don't update kernel routing table */
static int	aflag = 0;	/* age out even the statically defined routes */
static int	hflag = 0;	/* don't split horizon */
static int	lflag = 0;	/* exchange site local routes */
static int	Pflag = 0;	/* don't age out routes with RTF_PROTO[123] */
static int	Qflag = RTF_PROTO2;	/* set RTF_PROTO[123] flag to routes by RIPng */
static int	sflag = 0;	/* announce static routes w/ split horizon */
static int	Sflag = 0;	/* announce static routes to every interface */
static unsigned long routetag = 0;	/* route tag attached on originating case */

static char	*filter[MAXFILTER];
static int	filtertype[MAXFILTER];
static int	nfilter = 0;

static pid_t	pid;

static struct	sockaddr_storage ripsin;

static int	interval = 1;
static time_t	nextalarm = 0;
#if 0
static time_t	sup_trig_update = 0;
#endif

static FILE	*rtlog = NULL;

static int logopened = 0;

static	int	seq = 0;

static volatile sig_atomic_t seenalrm;
static volatile sig_atomic_t seenquit;
static volatile sig_atomic_t seenusr1;

#define	RRTF_AGGREGATE		0x08000000
#define	RRTF_NOADVERTISE	0x10000000
#define	RRTF_NH_NOT_LLADDR	0x20000000
#define RRTF_SENDANYWAY		0x40000000
#define	RRTF_CHANGED		0x80000000

static void sighandler(int);
static void ripalarm(void);
static void riprecv(void);
static void ripsend(struct ifc *, struct sockaddr_in6 *, int);
static int out_filter(struct riprt *, struct ifc *);
static void init(void);
static void ifconfig(void);
static int ifconfig1(const char *, const struct sockaddr *, struct ifc *, int);
static void rtrecv(void);
static int rt_del(const struct sockaddr_in6 *, const struct sockaddr_in6 *,
	const struct sockaddr_in6 *);
static int rt_deladdr(struct ifc *, const struct sockaddr_in6 *,
	const struct sockaddr_in6 *);
static void filterconfig(void);
static int getifmtu(int);
static const char *rttypes(struct rt_msghdr *);
static const char *rtflags(struct rt_msghdr *);
static const char *ifflags(int);
static int ifrt(struct ifc *, int);
static void ifrt_p2p(struct ifc *, int);
static void applyplen(struct in6_addr *, int);
static void ifrtdump(int);
static void ifdump(int);
static void ifdump0(FILE *, const struct ifc *);
static void ifremove(int);
static void rtdump(int);
static void rt_entry(struct rt_msghdr *, int);
static void rtdexit(void);
static void riprequest(struct ifc *, struct netinfo6 *, int,
	struct sockaddr_in6 *);
static void ripflush(struct ifc *, struct sockaddr_in6 *, int, struct netinfo6 *np);
static void sendrequest(struct ifc *);
static int sin6mask2len(const struct sockaddr_in6 *);
static int mask2len(const struct in6_addr *, int);
static int sendpacket(struct sockaddr_in6 *, int);
static int addroute(struct riprt *, const struct in6_addr *, struct ifc *);
static int delroute(struct netinfo6 *, struct in6_addr *);
#if 0
static struct in6_addr *getroute(struct netinfo6 *, struct in6_addr *);
#endif
static void krtread(int);
static int tobeadv(struct riprt *, struct ifc *);
static char *allocopy(char *);
static char *hms(void);
static const char *inet6_n2p(const struct in6_addr *);
static struct ifac *ifa_match(const struct ifc *, const struct in6_addr *, int);
static struct in6_addr *plen2mask(int);
static struct riprt *rtsearch(struct netinfo6 *);
static int ripinterval(int);
#if 0
static time_t ripsuptrig(void);
#endif
static void fatal(const char *, ...)
	__attribute__((__format__(__printf__, 1, 2)));
static void trace(int, const char *, ...)
	__attribute__((__format__(__printf__, 2, 3)));
static void tracet(int, const char *, ...)
	__attribute__((__format__(__printf__, 2, 3)));
static struct ifc *ifc_find(char *);
static struct iff *iff_find(struct ifc *, int);
static void setindex2ifc(int, struct ifc *);

#define	MALLOC(type)	((type *)malloc(sizeof(type)))

#define IFIL_TYPE_ANY	0x0
#define IFIL_TYPE_A	'A'
#define IFIL_TYPE_N	'N'
#define IFIL_TYPE_T	'T'
#define IFIL_TYPE_O	'O'
#define IFIL_TYPE_L	'L'

int
main(int argc, char *argv[])
{
	int	ch;
	int	error = 0;
	unsigned long proto;
	struct	ifc *ifcp;
	sigset_t mask, omask;
	const char *pidfile = ROUTE6D_PID;
	FILE *pidfh;
	char *progname;
	char *ep;

	progname = strrchr(*argv, '/');
	if (progname)
		progname++;
	else
		progname = *argv;

	pid = getpid();
	while ((ch = getopt(argc, argv, "A:N:O:R:T:L:t:adDhlnp:P:Q:qsS")) != -1) {
		switch (ch) {
		case 'A':
		case 'N':
		case 'O':
		case 'T':
		case 'L':
			if (nfilter >= MAXFILTER) {
				fatal("Exceeds MAXFILTER");
				/*NOTREACHED*/
			}
			filtertype[nfilter] = ch;
			filter[nfilter++] = allocopy(optarg);
			break;
		case 't':
			ep = NULL;
			routetag = strtoul(optarg, &ep, 0);
			if (!ep || *ep != '\0' || (routetag & ~0xffff) != 0) {
				fatal("invalid route tag");
				/*NOTREACHED*/
			}
			break;
		case 'p':
			pidfile = optarg;
			break;
		case 'P':
			ep = NULL;
			proto = strtoul(optarg, &ep, 0);
			if (!ep || *ep != '\0' || 3 < proto) {
				fatal("invalid P flag");
				/*NOTREACHED*/
			}
			if (proto == 0)
				Pflag = 0;
			if (proto == 1)
				Pflag |= RTF_PROTO1;
			if (proto == 2)
				Pflag |= RTF_PROTO2;
			if (proto == 3)
				Pflag |= RTF_PROTO3;
			break;
		case 'Q':
			ep = NULL;
			proto = strtoul(optarg, &ep, 0);
			if (!ep || *ep != '\0' || 3 < proto) {
				fatal("invalid Q flag");
				/*NOTREACHED*/
			}
			if (proto == 0)
				Qflag = 0;
			if (proto == 1)
				Qflag |= RTF_PROTO1;
			if (proto == 2)
				Qflag |= RTF_PROTO2;
			if (proto == 3)
				Qflag |= RTF_PROTO3;
			break;
		case 'R':
			if ((rtlog = fopen(optarg, "w")) == NULL) {
				fatal("Can not write to routelog");
				/*NOTREACHED*/
			}
			break;
#define	FLAG(c, flag, n)	case c: do { flag = n; break; } while(0)
		FLAG('a', aflag, 1); break;
		FLAG('d', dflag, 1); break;
		FLAG('D', dflag, 2); break;
		FLAG('h', hflag, 1); break;
		FLAG('l', lflag, 1); break;
		FLAG('n', nflag, 1); break;
		FLAG('q', qflag, 1); break;
		FLAG('s', sflag, 1); break;
		FLAG('S', Sflag, 1); break;
#undef	FLAG
		default:
			fatal("Invalid option specified, terminating");
			/*NOTREACHED*/
		}
	}
	argc -= optind;
	argv += optind;
	if (argc > 0) {
		fatal("bogus extra arguments");
		/*NOTREACHED*/
	}

	if (geteuid()) {
		nflag = 1;
		fprintf(stderr, "No kernel update is allowed\n");
	}

	if (dflag == 0) {
		if (daemon(0, 0) < 0) {
			fatal("daemon");
			/*NOTREACHED*/
		}
	}

	openlog(progname, LOG_NDELAY|LOG_PID, LOG_DAEMON);
	logopened++;

	if ((ripbuf = (struct rip6 *)malloc(RIP6_MAXMTU)) == NULL)
		fatal("malloc");
	memset(ripbuf, 0, RIP6_MAXMTU);
	ripbuf->rip6_cmd = RIP6_RESPONSE;
	ripbuf->rip6_vers = RIP6_VERSION;
	ripbuf->rip6_res1[0] = 0;
	ripbuf->rip6_res1[1] = 0;

	init();
	ifconfig();
	TAILQ_FOREACH(ifcp, &ifc_head, ifc_next) {
		if (ifcp->ifc_index < 0) {
			fprintf(stderr, "No ifindex found at %s "
			    "(no link-local address?)\n", ifcp->ifc_name);
			error++;
		}
	}
	if (error)
		exit(1);
	if (loopifcp == NULL) {
		fatal("No loopback found");
		/*NOTREACHED*/
	}
	TAILQ_FOREACH(ifcp, &ifc_head, ifc_next) {
		ifrt(ifcp, 0);
	}
	filterconfig();
	krtread(0);
	if (dflag)
		ifrtdump(0);

	pid = getpid();
	if ((pidfh = fopen(pidfile, "w")) != NULL) {
		fprintf(pidfh, "%d\n", pid);
		fclose(pidfh);
	}

	if ((ripbuf = (struct rip6 *)malloc(RIP6_MAXMTU)) == NULL) {
		fatal("malloc");
		/*NOTREACHED*/
	}
	memset(ripbuf, 0, RIP6_MAXMTU);
	ripbuf->rip6_cmd = RIP6_RESPONSE;
	ripbuf->rip6_vers = RIP6_VERSION;
	ripbuf->rip6_res1[0] = 0;
	ripbuf->rip6_res1[1] = 0;

	if (signal(SIGALRM, sighandler) == SIG_ERR ||
	    signal(SIGQUIT, sighandler) == SIG_ERR ||
	    signal(SIGTERM, sighandler) == SIG_ERR ||
	    signal(SIGUSR1, sighandler) == SIG_ERR ||
	    signal(SIGHUP, sighandler) == SIG_ERR ||
	    signal(SIGINT, sighandler) == SIG_ERR) {
		fatal("signal");
		/*NOTREACHED*/
	}
	/*
	 * To avoid rip packet congestion (not on a cable but in this
	 * process), wait for a moment to send the first RIP6_RESPONSE
	 * packets.
	 */
	alarm(ripinterval(INIT_INTERVAL6));

	TAILQ_FOREACH(ifcp, &ifc_head, ifc_next) {
		if (iff_find(ifcp, IFIL_TYPE_N) != NULL)
			continue;
		if (ifcp->ifc_index > 0 && (ifcp->ifc_flags & IFF_UP))
			sendrequest(ifcp);
	}

	syslog(LOG_INFO, "**** Started ****");
	sigemptyset(&mask);
	sigaddset(&mask, SIGALRM);
	while (1) {
		if (seenalrm) {
			ripalarm();
			seenalrm = 0;
			continue;
		}
		if (seenquit) {
			rtdexit();
			seenquit = 0;
			continue;
		}
		if (seenusr1) {
			ifrtdump(SIGUSR1);
			seenusr1 = 0;
			continue;
		}

#ifdef HAVE_POLL_H
		switch (poll(set, 2, INFTIM))
#else
		memcpy(recvecp, sockvecp, fdmasks);
		switch (select(maxfd + 1, recvecp, 0, 0, 0))
#endif
		{
		case -1:
			if (errno != EINTR) {
				fatal("select");
				/*NOTREACHED*/
			}
			continue;
		case 0:
			continue;
		default:
#ifdef HAVE_POLL_H
			if (set[0].revents & POLLIN)
#else
			if (FD_ISSET(ripsock, recvecp))
#endif
			{
				sigprocmask(SIG_BLOCK, &mask, &omask);
				riprecv();
				sigprocmask(SIG_SETMASK, &omask, NULL);
			}
#ifdef HAVE_POLL_H
			if (set[1].revents & POLLIN)
#else
			if (FD_ISSET(rtsock, recvecp))
#endif
			{
				sigprocmask(SIG_BLOCK, &mask, &omask);
				rtrecv();
				sigprocmask(SIG_SETMASK, &omask, NULL);
			}
		}
	}
}

static void
sighandler(int signo)
{

	switch (signo) {
	case SIGALRM:
		seenalrm++;
		break;
	case SIGQUIT:
	case SIGTERM:
		seenquit++;
		break;
	case SIGUSR1:
	case SIGHUP:
	case SIGINT:
		seenusr1++;
		break;
	}
}

/*
 * gracefully exits after resetting sockopts.
 */
/* ARGSUSED */
static void
rtdexit(void)
{
	struct	riprt *rrt;

	alarm(0);
	TAILQ_FOREACH(rrt, &riprt_head, rrt_next) {
		if (rrt->rrt_rflags & RRTF_AGGREGATE) {
			delroute(&rrt->rrt_info, &rrt->rrt_gw);
		}
	}
	close(ripsock);
	close(rtsock);
	syslog(LOG_INFO, "**** Terminated ****");
	closelog();
	exit(1);
}

/*
 * Called periodically:
 *	1. age out the learned route. remove it if necessary.
 *	2. submit RIP6_RESPONSE packets.
 * Invoked in every SUPPLY_INTERVAL6 (30) seconds.  I believe we don't have
 * to invoke this function in every 1 or 5 or 10 seconds only to age the
 * routes more precisely.
 */
/* ARGSUSED */
static void
ripalarm(void)
{
	struct	ifc *ifcp;
	struct	riprt *rrt, *rrt_tmp;
	time_t	t_lifetime, t_holddown;

	/* age the RIP routes */
	t_lifetime = time(NULL) - RIP_LIFETIME;
	t_holddown = t_lifetime - RIP_HOLDDOWN;
	TAILQ_FOREACH_SAFE(rrt, &riprt_head, rrt_next, rrt_tmp) {
		if (rrt->rrt_t == 0)
			continue;
		else if (rrt->rrt_t < t_holddown) {
			TAILQ_REMOVE(&riprt_head, rrt, rrt_next);
			delroute(&rrt->rrt_info, &rrt->rrt_gw);
			free(rrt);
		} else if (rrt->rrt_t < t_lifetime)
			rrt->rrt_info.rip6_metric = HOPCNT_INFINITY6;
	}
	/* Supply updates */
	TAILQ_FOREACH(ifcp, &ifc_head, ifc_next) {
		if (ifcp->ifc_index > 0 && (ifcp->ifc_flags & IFF_UP))
			ripsend(ifcp, &ifcp->ifc_ripsin, 0);
	}
	alarm(ripinterval(SUPPLY_INTERVAL6));
}

static void
init(void)
{
	int	error;
	const int int0 = 0, int1 = 1, int255 = 255;
	struct	addrinfo hints, *res;
	char	port[NI_MAXSERV];

	TAILQ_INIT(&ifc_head);
	nindex2ifc = 0;	/*initial guess*/
	index2ifc = NULL;
	snprintf(port, sizeof(port), "%u", RIP6_PORT);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_INET6;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	hints.ai_flags = AI_PASSIVE;
	error = getaddrinfo(NULL, port, &hints, &res);
	if (error) {
		fatal("%s", gai_strerror(error));
		/*NOTREACHED*/
	}
	if (res->ai_next) {
		fatal(":: resolved to multiple address");
		/*NOTREACHED*/
	}

	ripsock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (ripsock < 0) {
		fatal("rip socket");
		/*NOTREACHED*/
	}
#ifdef IPV6_V6ONLY
	if (setsockopt(ripsock, IPPROTO_IPV6, IPV6_V6ONLY,
	    &int1, sizeof(int1)) < 0) {
		fatal("rip IPV6_V6ONLY");
		/*NOTREACHED*/
	}
#endif
	if (bind(ripsock, res->ai_addr, res->ai_addrlen) < 0) {
		fatal("rip bind");
		/*NOTREACHED*/
	}
	if (setsockopt(ripsock, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
	    &int255, sizeof(int255)) < 0) {
		fatal("rip IPV6_MULTICAST_HOPS");
		/*NOTREACHED*/
	}
	if (setsockopt(ripsock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
	    &int0, sizeof(int0)) < 0) {
		fatal("rip IPV6_MULTICAST_LOOP");
		/*NOTREACHED*/
	}

#ifdef IPV6_RECVPKTINFO
	if (setsockopt(ripsock, IPPROTO_IPV6, IPV6_RECVPKTINFO,
	    &int1, sizeof(int1)) < 0) {
		fatal("rip IPV6_RECVPKTINFO");
		/*NOTREACHED*/
	}
#else  /* old adv. API */
	if (setsockopt(ripsock, IPPROTO_IPV6, IPV6_PKTINFO,
	    &int1, sizeof(int1)) < 0) {
		fatal("rip IPV6_PKTINFO");
		/*NOTREACHED*/
	}
#endif

#ifdef IPV6_RECVPKTINFO
	if (setsockopt(ripsock, IPPROTO_IPV6, IPV6_RECVHOPLIMIT,
	    &int1, sizeof(int1)) < 0) {
		fatal("rip IPV6_RECVHOPLIMIT");
		/*NOTREACHED*/
	}
#else  /* old adv. API */
	if (setsockopt(ripsock, IPPROTO_IPV6, IPV6_HOPLIMIT,
	    &int1, sizeof(int1)) < 0) {
		fatal("rip IPV6_HOPLIMIT");
		/*NOTREACHED*/
	}
#endif
	freeaddrinfo(res);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_INET6;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	error = getaddrinfo(RIP6_DEST, port, &hints, &res);
	if (error) {
		fatal("%s", gai_strerror(error));
		/*NOTREACHED*/
	}
	if (res->ai_next) {
		fatal("%s resolved to multiple address", RIP6_DEST);
		/*NOTREACHED*/
	}
	memcpy(&ripsin, res->ai_addr, res->ai_addrlen);
	freeaddrinfo(res);

#ifdef HAVE_POLL_H
	set[0].fd = ripsock;
	set[0].events = POLLIN;
#else
	maxfd = ripsock;
#endif

	if (nflag == 0) {
		if ((rtsock = socket(PF_ROUTE, SOCK_RAW, 0)) < 0) {
			fatal("route socket");
			/*NOTREACHED*/
		}
#ifdef HAVE_POLL_H
		set[1].fd = rtsock;
		set[1].events = POLLIN;
#else
		if (rtsock > maxfd)
			maxfd = rtsock;
#endif
	} else {
#ifdef HAVE_POLL_H
		set[1].fd = -1;
#else
		rtsock = -1;	/*just for safety */
#endif
	}

#ifndef HAVE_POLL_H
	fdmasks = howmany(maxfd + 1, NFDBITS) * sizeof(fd_mask);
	if ((sockvecp = malloc(fdmasks)) == NULL) {
		fatal("malloc");
		/*NOTREACHED*/
	}
	if ((recvecp = malloc(fdmasks)) == NULL) {
		fatal("malloc");
		/*NOTREACHED*/
	}
	memset(sockvecp, 0, fdmasks);
	FD_SET(ripsock, sockvecp);
	if (rtsock >= 0)
		FD_SET(rtsock, sockvecp);
#endif
}

#define	RIPSIZE(n) \
	(sizeof(struct rip6) + ((n)-1) * sizeof(struct netinfo6))

/*
 * ripflush flushes the rip datagram stored in the rip buffer
 */
static void
ripflush(struct ifc *ifcp, struct sockaddr_in6 *sin6, int nrt, struct netinfo6 *np)
{
	int i;
	int error;

	if (ifcp)
		tracet(1, "Send(%s): info(%d) to %s.%d\n",
			ifcp->ifc_name, nrt,
			inet6_n2p(&sin6->sin6_addr), ntohs(sin6->sin6_port));
	else
		tracet(1, "Send: info(%d) to %s.%d\n",
			nrt, inet6_n2p(&sin6->sin6_addr), ntohs(sin6->sin6_port));
	if (dflag >= 2) {
		np = ripbuf->rip6_nets;
		for (i = 0; i < nrt; i++, np++) {
			if (np->rip6_metric == NEXTHOP_METRIC) {
				if (IN6_IS_ADDR_UNSPECIFIED(&np->rip6_dest))
					trace(2, "    NextHop reset");
				else {
					trace(2, "    NextHop %s",
						inet6_n2p(&np->rip6_dest));
				}
			} else {
				trace(2, "    %s/%d[%d]",
					inet6_n2p(&np->rip6_dest),
					np->rip6_plen, np->rip6_metric);
			}
			if (np->rip6_tag) {
				trace(2, "  tag=0x%04x",
					ntohs(np->rip6_tag) & 0xffff);
			}
			trace(2, "\n");
		}
	}
	error = sendpacket(sin6, RIPSIZE(nrt));
	if (error == EAFNOSUPPORT) {
		/* Protocol not supported */
		if (ifcp != NULL) {
			tracet(1, "Could not send info to %s (%s): "
			    "set IFF_UP to 0\n",
			    ifcp->ifc_name,
			    inet6_n2p(&ifcp->ifc_ripsin.sin6_addr));
			/* As if down for AF_INET6 */
			ifcp->ifc_flags &= ~IFF_UP;
		} else {
			tracet(1, "Could not send info to %s\n",
			    inet6_n2p(&sin6->sin6_addr));
		}
	}
}

/*
 * Generate RIP6_RESPONSE packets and send them.
 */
static void
ripsend(struct	ifc *ifcp, struct sockaddr_in6 *sin6, int flag)
{
	struct	riprt *rrt;
	struct	in6_addr *nh;	/* next hop */
	struct netinfo6 *np;
	int	maxrte;
	int nrt;

	if (qflag)
		return;

	if (ifcp == NULL) {
		/*
		 * Request from non-link local address is not
		 * a regular route6d update.
		 */
		maxrte = (IFMINMTU - sizeof(struct ip6_hdr) -
				sizeof(struct udphdr) -
				sizeof(struct rip6) + sizeof(struct netinfo6)) /
				sizeof(struct netinfo6);
		nh = NULL;
		nrt = 0;
		np = ripbuf->rip6_nets;
		TAILQ_FOREACH(rrt, &riprt_head, rrt_next) {
			if (rrt->rrt_rflags & RRTF_NOADVERTISE)
				continue;
			/* Put the route to the buffer */
			*np = rrt->rrt_info;
			np++; nrt++;
			if (nrt == maxrte) {
				ripflush(NULL, sin6, nrt, np);
				nh = NULL;
				nrt = 0;
				np = ripbuf->rip6_nets;
			}
		}
		if (nrt)	/* Send last packet */
			ripflush(NULL, sin6, nrt, np);
		return;
	}

	if ((flag & RRTF_SENDANYWAY) == 0 &&
	    (qflag || (ifcp->ifc_flags & IFF_LOOPBACK)))
		return;

	/* -N: no use */
	if (iff_find(ifcp, IFIL_TYPE_N) != NULL)
		return;

	/* -T: generate default route only */
	if (iff_find(ifcp, IFIL_TYPE_T) != NULL) {
		struct netinfo6 rrt_info;
		memset(&rrt_info, 0, sizeof(struct netinfo6));
		rrt_info.rip6_dest = in6addr_any;
		rrt_info.rip6_plen = 0;
		rrt_info.rip6_metric = 1;
		rrt_info.rip6_metric += ifcp->ifc_metric;
		rrt_info.rip6_tag = htons(routetag & 0xffff);
		np = ripbuf->rip6_nets;
		*np = rrt_info;
		nrt = 1;
		ripflush(ifcp, sin6, nrt, np);
		return;
	}

	maxrte = (ifcp->ifc_mtu - sizeof(struct ip6_hdr) -
			sizeof(struct udphdr) -
			sizeof(struct rip6) + sizeof(struct netinfo6)) /
			sizeof(struct netinfo6);

	nrt = 0; np = ripbuf->rip6_nets; nh = NULL;
	TAILQ_FOREACH(rrt, &riprt_head, rrt_next) {
		if (rrt->rrt_rflags & RRTF_NOADVERTISE)
			continue;

		/* Need to check filter here */
		if (out_filter(rrt, ifcp) == 0)
			continue;

		/* Check split horizon and other conditions */
		if (tobeadv(rrt, ifcp) == 0)
			continue;

		/* Only considers the routes with flag if specified */
		if ((flag & RRTF_CHANGED) &&
		    (rrt->rrt_rflags & RRTF_CHANGED) == 0)
			continue;

		/* Check nexthop */
		if (rrt->rrt_index == ifcp->ifc_index &&
		    !IN6_IS_ADDR_UNSPECIFIED(&rrt->rrt_gw) &&
		    (rrt->rrt_rflags & RRTF_NH_NOT_LLADDR) == 0) {
			if (nh == NULL || !IN6_ARE_ADDR_EQUAL(nh, &rrt->rrt_gw)) {
				if (nrt == maxrte - 2) {
					ripflush(ifcp, sin6, nrt, np);
					nh = NULL;
					nrt = 0;
					np = ripbuf->rip6_nets;
				}

				np->rip6_dest = rrt->rrt_gw;
				np->rip6_plen = 0;
				np->rip6_tag = 0;
				np->rip6_metric = NEXTHOP_METRIC;
				nh = &rrt->rrt_gw;
				np++; nrt++;
			}
		} else if (nh && (rrt->rrt_index != ifcp->ifc_index ||
			          !IN6_ARE_ADDR_EQUAL(nh, &rrt->rrt_gw) ||
				  rrt->rrt_rflags & RRTF_NH_NOT_LLADDR)) {
			/* Reset nexthop */
			if (nrt == maxrte - 2) {
				ripflush(ifcp, sin6, nrt, np);
				nh = NULL;
				nrt = 0;
				np = ripbuf->rip6_nets;
			}
			memset(np, 0, sizeof(struct netinfo6));
			np->rip6_metric = NEXTHOP_METRIC;
			nh = NULL;
			np++; nrt++;
		}

		/* Put the route to the buffer */
		*np = rrt->rrt_info;
		np++; nrt++;
		if (nrt == maxrte) {
			ripflush(ifcp, sin6, nrt, np);
			nh = NULL;
			nrt = 0;
			np = ripbuf->rip6_nets;
		}
	}
	if (nrt)	/* Send last packet */
		ripflush(ifcp, sin6, nrt, np);
}

/*
 * outbound filter logic, per-route/interface.
 */
static int
out_filter(struct riprt *rrt, struct ifc *ifcp)
{
	struct iff *iffp;
	struct in6_addr ia;
	int ok;

	/*
	 * -A: filter out less specific routes, if we have aggregated
	 * route configured.
	 */
	TAILQ_FOREACH(iffp, &ifcp->ifc_iff_head, iff_next) {
		if (iffp->iff_type != 'A')
			continue;
		if (rrt->rrt_info.rip6_plen <= iffp->iff_plen)
			continue;
		ia = rrt->rrt_info.rip6_dest;
		applyplen(&ia, iffp->iff_plen);
		if (IN6_ARE_ADDR_EQUAL(&ia, &iffp->iff_addr))
			return 0;
	}

	/*
	 * if it is an aggregated route, advertise it only to the
	 * interfaces specified on -A.
	 */
	if ((rrt->rrt_rflags & RRTF_AGGREGATE) != 0) {
		ok = 0;
		TAILQ_FOREACH(iffp, &ifcp->ifc_iff_head, iff_next) {
			if (iffp->iff_type != 'A')
				continue;
			if (rrt->rrt_info.rip6_plen == iffp->iff_plen &&
			    IN6_ARE_ADDR_EQUAL(&rrt->rrt_info.rip6_dest,
			    &iffp->iff_addr)) {
				ok = 1;
				break;
			}
		}
		if (!ok)
			return 0;
	}

	/*
	 * -O: advertise only if prefix matches the configured prefix.
	 */
	if (iff_find(ifcp, IFIL_TYPE_O) != NULL) {
		ok = 0;
		TAILQ_FOREACH(iffp, &ifcp->ifc_iff_head, iff_next) {
			if (iffp->iff_type != 'O')
				continue;
			if (rrt->rrt_info.rip6_plen < iffp->iff_plen)
				continue;
			ia = rrt->rrt_info.rip6_dest;
			applyplen(&ia, iffp->iff_plen);
			if (IN6_ARE_ADDR_EQUAL(&ia, &iffp->iff_addr)) {
				ok = 1;
				break;
			}
		}
		if (!ok)
			return 0;
	}

	/* the prefix should be advertised */
	return 1;
}

/*
 * Determine if the route is to be advertised on the specified interface.
 * It checks options specified in the arguments and the split horizon rule.
 */
static int
tobeadv(struct riprt *rrt, struct ifc *ifcp)
{

	/* Special care for static routes */
	if (rrt->rrt_flags & RTF_STATIC) {
		/* XXX don't advertise reject/blackhole routes */
		if (rrt->rrt_flags & (RTF_REJECT | RTF_BLACKHOLE))
			return 0;

		if (Sflag)	/* Yes, advertise it anyway */
			return 1;
		if (sflag && rrt->rrt_index != ifcp->ifc_index)
			return 1;
		return 0;
	}
	/* Regular split horizon */
	if (hflag == 0 && rrt->rrt_index == ifcp->ifc_index)
		return 0;
	return 1;
}

/*
 * Send a rip packet actually.
 */
static int
sendpacket(struct sockaddr_in6 *sin6, int len)
{
	struct msghdr m;
	struct cmsghdr *cm;
	struct iovec iov[2];
	struct in6_pktinfo *pi;
	u_char cmsgbuf[256];
	int idx;
	struct sockaddr_in6 sincopy;

	/* do not overwrite the given sin */
	sincopy = *sin6;
	sin6 = &sincopy;

	if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr) ||
	    IN6_IS_ADDR_MULTICAST(&sin6->sin6_addr))
		idx = sin6->sin6_scope_id;
	else
		idx = 0;

	m.msg_name = (caddr_t)sin6;
	m.msg_namelen = sizeof(*sin6);
	iov[0].iov_base = (caddr_t)ripbuf;
	iov[0].iov_len = len;
	m.msg_iov = iov;
	m.msg_iovlen = 1;
	m.msg_flags = 0;
	if (!idx) {
		m.msg_control = NULL;
		m.msg_controllen = 0;
	} else {
		memset(cmsgbuf, 0, sizeof(cmsgbuf));
		cm = (struct cmsghdr *)(void *)cmsgbuf;
		m.msg_control = (caddr_t)cm;
		m.msg_controllen = CMSG_SPACE(sizeof(struct in6_pktinfo));

		cm->cmsg_len = CMSG_LEN(sizeof(struct in6_pktinfo));
		cm->cmsg_level = IPPROTO_IPV6;
		cm->cmsg_type = IPV6_PKTINFO;
		pi = (struct in6_pktinfo *)(void *)CMSG_DATA(cm);
		memset(&pi->ipi6_addr, 0, sizeof(pi->ipi6_addr)); /*::*/
		pi->ipi6_ifindex = idx;
	}

	if (sendmsg(ripsock, &m, 0 /*MSG_DONTROUTE*/) < 0) {
		trace(1, "sendmsg: %s\n", strerror(errno));
		return errno;
	}

	return 0;
}

/*
 * Receive and process RIP packets.  Update the routes/kernel forwarding
 * table if necessary.
 */
static void
riprecv(void)
{
	struct	ifc *ifcp, *ic;
	struct	sockaddr_in6 fsock;
	struct	in6_addr nh;	/* next hop */
	struct	rip6 *rp;
	struct	netinfo6 *np, *nq;
	struct	riprt *rrt;
	ssize_t	len, nn;
	unsigned int need_trigger, idx;
	char	buf[4 * RIP6_MAXMTU];
	time_t	t;
	struct msghdr m;
	struct cmsghdr *cm;
	struct iovec iov[2];
	u_char cmsgbuf[256];
	struct in6_pktinfo *pi = NULL;
	int *hlimp = NULL;
	struct iff *iffp;
	struct in6_addr ia;
	int ok;
	time_t t_half_lifetime;

	need_trigger = 0;

	m.msg_name = (caddr_t)&fsock;
	m.msg_namelen = sizeof(fsock);
	iov[0].iov_base = (caddr_t)buf;
	iov[0].iov_len = sizeof(buf);
	m.msg_iov = iov;
	m.msg_iovlen = 1;
	cm = (struct cmsghdr *)(void *)cmsgbuf;
	m.msg_control = (caddr_t)cm;
	m.msg_controllen = sizeof(cmsgbuf);
	m.msg_flags = 0;
	if ((len = recvmsg(ripsock, &m, 0)) < 0) {
		fatal("recvmsg");
		/*NOTREACHED*/
	}
	idx = 0;
	for (cm = (struct cmsghdr *)CMSG_FIRSTHDR(&m);
	     cm;
	     cm = (struct cmsghdr *)CMSG_NXTHDR(&m, cm)) {
		if (cm->cmsg_level != IPPROTO_IPV6)
		    continue;
		switch (cm->cmsg_type) {
		case IPV6_PKTINFO:
			if (cm->cmsg_len != CMSG_LEN(sizeof(*pi))) {
				trace(1,
				    "invalid cmsg length for IPV6_PKTINFO\n");
				return;
			}
			pi = (struct in6_pktinfo *)(void *)CMSG_DATA(cm);
			idx = pi->ipi6_ifindex;
			break;
		case IPV6_HOPLIMIT:
			if (cm->cmsg_len != CMSG_LEN(sizeof(int))) {
				trace(1,
				    "invalid cmsg length for IPV6_HOPLIMIT\n");
				return;
			}
			hlimp = (int *)(void *)CMSG_DATA(cm);
			break;
		}
	}

	if ((size_t)len < sizeof(struct rip6)) {
		trace(1, "Packet too short\n");
		return;
	}

	if (pi == NULL || hlimp == NULL) {
		/*
		 * This can happen when the kernel failed to allocate memory
		 * for the ancillary data.  Although we might be able to handle
		 * some cases without this info, those are minor and not so
		 * important, so it's better to discard the packet for safer
		 * operation.
		 */
		trace(1, "IPv6 packet information cannot be retrieved\n");
		return;
	}

	nh = fsock.sin6_addr;
	nn = (len - sizeof(struct rip6) + sizeof(struct netinfo6)) /
		sizeof(struct netinfo6);
	rp = (struct rip6 *)(void *)buf;
	np = rp->rip6_nets;

	if (rp->rip6_vers != RIP6_VERSION) {
		trace(1, "Incorrect RIP version %d\n", rp->rip6_vers);
		return;
	}
	if (rp->rip6_cmd == RIP6_REQUEST) {
		if (idx && idx < nindex2ifc) {
			ifcp = index2ifc[idx];
			riprequest(ifcp, np, nn, &fsock);
		} else {
			riprequest(NULL, np, nn, &fsock);
		}
		return;
	}

	if (!IN6_IS_ADDR_LINKLOCAL(&fsock.sin6_addr)) {
		trace(1, "Response from non-ll addr: %s\n",
		    inet6_n2p(&fsock.sin6_addr));
		return;		/* Ignore packets from non-link-local addr */
	}
	if (ntohs(fsock.sin6_port) != RIP6_PORT) {
		trace(1, "Response from non-rip port from %s\n",
		    inet6_n2p(&fsock.sin6_addr));
		return;
	}
	if (IN6_IS_ADDR_MULTICAST(&pi->ipi6_addr) && *hlimp != 255) {
		trace(1,
		    "Response packet with a smaller hop limit (%d) from %s\n",
		    *hlimp, inet6_n2p(&fsock.sin6_addr));
		return;
	}
	/*
	 * Further validation: since this program does not send off-link
	 * requests, an incoming response must always come from an on-link
	 * node.  Although this is normally ensured by the source address
	 * check above, it may not 100% be safe because there are router
	 * implementations that (invalidly) allow a packet with a link-local
	 * source address to be forwarded to a different link.
	 * So we also check whether the destination address is a link-local
	 * address or the hop limit is 255.  Note that RFC2080 does not require
	 * the specific hop limit for a unicast response, so we cannot assume
	 * the limitation.
	 */
	if (!IN6_IS_ADDR_LINKLOCAL(&pi->ipi6_addr) && *hlimp != 255) {
		trace(1,
		    "Response packet possibly from an off-link node: "
		    "from %s to %s hlim=%d\n",
		    inet6_n2p(&fsock.sin6_addr),
		    inet6_n2p(&pi->ipi6_addr), *hlimp);
		return;
	}

	idx = fsock.sin6_scope_id;
	ifcp = (idx < nindex2ifc) ? index2ifc[idx] : NULL;
	if (!ifcp) {
		trace(1, "Packets to unknown interface index %d\n", idx);
		return;		/* Ignore it */
	}
	if (IN6_ARE_ADDR_EQUAL(&ifcp->ifc_mylladdr, &fsock.sin6_addr))
		return;		/* The packet is from me; ignore */
	if (rp->rip6_cmd != RIP6_RESPONSE) {
		trace(1, "Invalid command %d\n", rp->rip6_cmd);
		return;
	}

	/* -N: no use */
	if (iff_find(ifcp, IFIL_TYPE_N) != NULL)
		return;

	tracet(1, "Recv(%s): from %s.%d info(%zd)\n",
	    ifcp->ifc_name, inet6_n2p(&nh), ntohs(fsock.sin6_port), nn);

	t = time(NULL);
	t_half_lifetime = t - (RIP_LIFETIME/2);
	for (; nn; nn--, np++) {
		if (np->rip6_metric == NEXTHOP_METRIC) {
			/* modify neighbor address */
			if (IN6_IS_ADDR_LINKLOCAL(&np->rip6_dest)) {
				nh = np->rip6_dest;
				trace(1, "\tNexthop: %s\n", inet6_n2p(&nh));
			} else if (IN6_IS_ADDR_UNSPECIFIED(&np->rip6_dest)) {
				nh = fsock.sin6_addr;
				trace(1, "\tNexthop: %s\n", inet6_n2p(&nh));
			} else {
				nh = fsock.sin6_addr;
				trace(1, "\tInvalid Nexthop: %s\n",
				    inet6_n2p(&np->rip6_dest));
			}
			continue;
		}
		if (IN6_IS_ADDR_MULTICAST(&np->rip6_dest)) {
			trace(1, "\tMulticast netinfo6: %s/%d [%d]\n",
				inet6_n2p(&np->rip6_dest),
				np->rip6_plen, np->rip6_metric);
			continue;
		}
		if (IN6_IS_ADDR_LOOPBACK(&np->rip6_dest)) {
			trace(1, "\tLoopback netinfo6: %s/%d [%d]\n",
				inet6_n2p(&np->rip6_dest),
				np->rip6_plen, np->rip6_metric);
			continue;
		}
		if (IN6_IS_ADDR_LINKLOCAL(&np->rip6_dest)) {
			trace(1, "\tLink Local netinfo6: %s/%d [%d]\n",
				inet6_n2p(&np->rip6_dest),
				np->rip6_plen, np->rip6_metric);
			continue;
		}
		/* may need to pass sitelocal prefix in some case, however*/
		if (IN6_IS_ADDR_SITELOCAL(&np->rip6_dest) && !lflag) {
			trace(1, "\tSite Local netinfo6: %s/%d [%d]\n",
				inet6_n2p(&np->rip6_dest),
				np->rip6_plen, np->rip6_metric);
			continue;
		}
		trace(2, "\tnetinfo6: %s/%d [%d]",
			inet6_n2p(&np->rip6_dest),
			np->rip6_plen, np->rip6_metric);
		if (np->rip6_tag)
			trace(2, "  tag=0x%04x", ntohs(np->rip6_tag) & 0xffff);
		if (dflag >= 2) {
			ia = np->rip6_dest;
			applyplen(&ia, np->rip6_plen);
			if (!IN6_ARE_ADDR_EQUAL(&ia, &np->rip6_dest))
				trace(2, " [junk outside prefix]");
		}

		/*
		 * -L: listen only if the prefix matches the configuration
		 */
                ok = 1;	/* if there's no L filter, it is ok */
                TAILQ_FOREACH(iffp, &ifcp->ifc_iff_head, iff_next) {
                        if (iffp->iff_type != IFIL_TYPE_L)
                                continue;
                        ok = 0;
                        if (np->rip6_plen < iffp->iff_plen)
                                continue;
                        /* special rule: ::/0 means default, not "in /0" */
                        if (iffp->iff_plen == 0 && np->rip6_plen > 0)
                                continue;
                        ia = np->rip6_dest;
                        applyplen(&ia, iffp->iff_plen);
                        if (IN6_ARE_ADDR_EQUAL(&ia, &iffp->iff_addr)) {
                                ok = 1;
                                break;
                        }
                }
		if (!ok) {
			trace(2, "  (filtered)\n");
			continue;
		}

		trace(2, "\n");
		np->rip6_metric++;
		np->rip6_metric += ifcp->ifc_metric;
		if (np->rip6_metric > HOPCNT_INFINITY6)
			np->rip6_metric = HOPCNT_INFINITY6;

		applyplen(&np->rip6_dest, np->rip6_plen);
		if ((rrt = rtsearch(np)) != NULL) {
			if (rrt->rrt_t == 0)
				continue;	/* Intf route has priority */
			nq = &rrt->rrt_info;
			if (nq->rip6_metric > np->rip6_metric) {
				if (rrt->rrt_index == ifcp->ifc_index &&
				    IN6_ARE_ADDR_EQUAL(&nh, &rrt->rrt_gw)) {
					/* Small metric from the same gateway */
					nq->rip6_metric = np->rip6_metric;
				} else {
					/* Better route found */
					rrt->rrt_index = ifcp->ifc_index;
					/* Update routing table */
					delroute(nq, &rrt->rrt_gw);
					rrt->rrt_gw = nh;
					*nq = *np;
					addroute(rrt, &nh, ifcp);
				}
				rrt->rrt_rflags |= RRTF_CHANGED;
				rrt->rrt_t = t;
				need_trigger = 1;
			} else if (nq->rip6_metric < np->rip6_metric &&
				   rrt->rrt_index == ifcp->ifc_index &&
				   IN6_ARE_ADDR_EQUAL(&nh, &rrt->rrt_gw)) {
				/* Got worse route from same gw */
				nq->rip6_metric = np->rip6_metric;
				rrt->rrt_t = t;
				rrt->rrt_rflags |= RRTF_CHANGED;
				need_trigger = 1;
			} else if (nq->rip6_metric == np->rip6_metric &&
				   np->rip6_metric < HOPCNT_INFINITY6) {
				if (rrt->rrt_index == ifcp->ifc_index &&
				   IN6_ARE_ADDR_EQUAL(&nh, &rrt->rrt_gw)) {
					/* same metric, same route from same gw */
					rrt->rrt_t = t;
				} else if (rrt->rrt_t < t_half_lifetime) {
					/* Better route found */
					rrt->rrt_index = ifcp->ifc_index;
					/* Update routing table */
					delroute(nq, &rrt->rrt_gw);
					rrt->rrt_gw = nh;
					*nq = *np;
					addroute(rrt, &nh, ifcp);
					rrt->rrt_rflags |= RRTF_CHANGED;
					rrt->rrt_t = t;
				}
			}
			/*
			 * if nq->rip6_metric == HOPCNT_INFINITY6 then
			 * do not update age value.  Do nothing.
			 */
		} else if (np->rip6_metric < HOPCNT_INFINITY6) {
			/* Got a new valid route */
			if ((rrt = MALLOC(struct riprt)) == NULL) {
				fatal("malloc: struct riprt");
				/*NOTREACHED*/
			}
			memset(rrt, 0, sizeof(*rrt));
			nq = &rrt->rrt_info;

			rrt->rrt_same = NULL;
			rrt->rrt_index = ifcp->ifc_index;
			rrt->rrt_flags = RTF_UP|RTF_GATEWAY;
			rrt->rrt_gw = nh;
			*nq = *np;
			applyplen(&nq->rip6_dest, nq->rip6_plen);
			if (nq->rip6_plen == sizeof(struct in6_addr) * 8)
				rrt->rrt_flags |= RTF_HOST;

			/* Update routing table */
			addroute(rrt, &nh, ifcp);
			rrt->rrt_rflags |= RRTF_CHANGED;
			need_trigger = 1;
			rrt->rrt_t = t;

			/* Put the route to the list */
			TAILQ_INSERT_HEAD(&riprt_head, rrt, rrt_next);
		}
	}
	/* XXX need to care the interval between triggered updates */
	if (need_trigger) {
		if (nextalarm > time(NULL) + RIP_TRIG_INT6_MAX) {
			TAILQ_FOREACH(ic, &ifc_head, ifc_next) {
				if (ifcp->ifc_index == ic->ifc_index)
					continue;
				if (ic->ifc_flags & IFF_UP)
					ripsend(ic, &ic->ifc_ripsin,
						RRTF_CHANGED);
			}
		}
		/* Reset the flag */
		TAILQ_FOREACH(rrt, &riprt_head, rrt_next) {
			rrt->rrt_rflags &= ~RRTF_CHANGED;
		}
	}
}

/*
 * Send all routes request packet to the specified interface.
 */
static void
sendrequest(struct ifc *ifcp)
{
	struct netinfo6 *np;
	int error;

	if (ifcp->ifc_flags & IFF_LOOPBACK)
		return;
	ripbuf->rip6_cmd = RIP6_REQUEST;
	np = ripbuf->rip6_nets;
	memset(np, 0, sizeof(struct netinfo6));
	np->rip6_metric = HOPCNT_INFINITY6;
	tracet(1, "Send rtdump Request to %s (%s)\n",
		ifcp->ifc_name, inet6_n2p(&ifcp->ifc_ripsin.sin6_addr));
	error = sendpacket(&ifcp->ifc_ripsin, RIPSIZE(1));
	if (error == EAFNOSUPPORT) {
		/* Protocol not supported */
		tracet(1, "Could not send rtdump Request to %s (%s): "
			"set IFF_UP to 0\n",
			ifcp->ifc_name, inet6_n2p(&ifcp->ifc_ripsin.sin6_addr));
		ifcp->ifc_flags &= ~IFF_UP;	/* As if down for AF_INET6 */
	}
	ripbuf->rip6_cmd = RIP6_RESPONSE;
}

/*
 * Process a RIP6_REQUEST packet.
 */
static void
riprequest(struct ifc *ifcp,
	struct netinfo6 *np,
	int nn,
	struct sockaddr_in6 *sin6)
{
	int i;
	struct riprt *rrt;

	if (!(nn == 1 && IN6_IS_ADDR_UNSPECIFIED(&np->rip6_dest) &&
	      np->rip6_plen == 0 && np->rip6_metric == HOPCNT_INFINITY6)) {
		/* Specific response, don't split-horizon */
		trace(1, "\tRIP Request\n");
		for (i = 0; i < nn; i++, np++) {
			rrt = rtsearch(np);
			if (rrt)
				np->rip6_metric = rrt->rrt_info.rip6_metric;
			else
				np->rip6_metric = HOPCNT_INFINITY6;
		}
		(void)sendpacket(sin6, RIPSIZE(nn));
		return;
	}
	/* Whole routing table dump */
	trace(1, "\tRIP Request -- whole routing table\n");
	ripsend(ifcp, sin6, RRTF_SENDANYWAY);
}

/*
 * Get information of each interface.
 */
static void
ifconfig(void)
{
	struct ifaddrs *ifap, *ifa;
	struct ifc *ifcp;
	struct ipv6_mreq mreq;
	int s;

	if ((s = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
		fatal("socket");
		/*NOTREACHED*/
	}

	if (getifaddrs(&ifap) != 0) {
		fatal("getifaddrs");
		/*NOTREACHED*/
	}

	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr->sa_family != AF_INET6)
			continue;
		ifcp = ifc_find(ifa->ifa_name);
		/* we are interested in multicast-capable interfaces */
		if ((ifa->ifa_flags & IFF_MULTICAST) == 0)
			continue;
		if (!ifcp) {
			/* new interface */
			if ((ifcp = MALLOC(struct ifc)) == NULL) {
				fatal("malloc: struct ifc");
				/*NOTREACHED*/
			}
			memset(ifcp, 0, sizeof(*ifcp));

			ifcp->ifc_index = -1;
			strlcpy(ifcp->ifc_name, ifa->ifa_name,
			    sizeof(ifcp->ifc_name));
			TAILQ_INIT(&ifcp->ifc_ifac_head);
			TAILQ_INIT(&ifcp->ifc_iff_head);
			ifcp->ifc_flags = ifa->ifa_flags;
			TAILQ_INSERT_HEAD(&ifc_head, ifcp, ifc_next);
			trace(1, "newif %s <%s>\n", ifcp->ifc_name,
				ifflags(ifcp->ifc_flags));
			if (!strcmp(ifcp->ifc_name, LOOPBACK_IF))
				loopifcp = ifcp;
		} else {
			/* update flag, this may be up again */
			if (ifcp->ifc_flags != ifa->ifa_flags) {
				trace(1, "%s: <%s> -> ", ifcp->ifc_name,
					ifflags(ifcp->ifc_flags));
				trace(1, "<%s>\n", ifflags(ifa->ifa_flags));
				ifcp->ifc_cflags |= IFC_CHANGED;
			}
			ifcp->ifc_flags = ifa->ifa_flags;
		}
		if (ifconfig1(ifa->ifa_name, ifa->ifa_addr, ifcp, s) < 0) {
			/* maybe temporary failure */
			continue;
		}
		if ((ifcp->ifc_flags & (IFF_LOOPBACK | IFF_UP)) == IFF_UP
		 && 0 < ifcp->ifc_index && !ifcp->ifc_joined) {
			mreq.ipv6mr_multiaddr = ifcp->ifc_ripsin.sin6_addr;
			mreq.ipv6mr_interface = ifcp->ifc_index;
			if (setsockopt(ripsock, IPPROTO_IPV6, IPV6_JOIN_GROUP,
			    &mreq, sizeof(mreq)) < 0) {
				fatal("IPV6_JOIN_GROUP");
				/*NOTREACHED*/
			}
			trace(1, "join %s %s\n", ifcp->ifc_name, RIP6_DEST);
			ifcp->ifc_joined++;
		}
	}
	close(s);
	freeifaddrs(ifap);
}

static int
ifconfig1(const char *name,
	const struct sockaddr *sa,
	struct ifc *ifcp,
	int s)
{
	struct	in6_ifreq ifr;
	const struct sockaddr_in6 *sin6;
	struct	ifac *ifac;
	int	plen;
	char	buf[BUFSIZ];

	sin6 = (const struct sockaddr_in6 *)(const void *)sa;
	if (IN6_IS_ADDR_SITELOCAL(&sin6->sin6_addr) && !lflag)
		return (-1);
	ifr.ifr_addr = *sin6;
	strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
	if (ioctl(s, SIOCGIFNETMASK_IN6, (char *)&ifr) < 0) {
		syslog(LOG_INFO, "ioctl: SIOCGIFNETMASK_IN6");
		return (-1);
	}
	plen = sin6mask2len(&ifr.ifr_addr);
	if ((ifac = ifa_match(ifcp, &sin6->sin6_addr, plen)) != NULL) {
		/* same interface found */
		/* need check if something changed */
		/* XXX not yet implemented */
		return (-1);
	}
	/*
	 * New address is found
	 */
	if ((ifac = MALLOC(struct ifac)) == NULL) {
		fatal("malloc: struct ifac");
		/*NOTREACHED*/
	}
	memset(ifac, 0, sizeof(*ifac));

	ifac->ifac_ifc = ifcp;
	ifac->ifac_addr = sin6->sin6_addr;
	ifac->ifac_plen = plen;
	ifac->ifac_scope_id = sin6->sin6_scope_id;
	if (ifcp->ifc_flags & IFF_POINTOPOINT) {
		ifr.ifr_addr = *sin6;
		if (ioctl(s, SIOCGIFDSTADDR_IN6, (char *)&ifr) < 0) {
			fatal("ioctl: SIOCGIFDSTADDR_IN6");
			/*NOTREACHED*/
		}
		ifac->ifac_raddr = ifr.ifr_dstaddr.sin6_addr;
		inet_ntop(AF_INET6, (void *)&ifac->ifac_raddr, buf,
		    sizeof(buf));
		trace(1, "found address %s/%d -- %s\n",
			inet6_n2p(&ifac->ifac_addr), ifac->ifac_plen, buf);
	} else {
		trace(1, "found address %s/%d\n",
			inet6_n2p(&ifac->ifac_addr), ifac->ifac_plen);
	}
	if (ifcp->ifc_index < 0 && IN6_IS_ADDR_LINKLOCAL(&ifac->ifac_addr)) {
		ifcp->ifc_mylladdr = ifac->ifac_addr;
		ifcp->ifc_index = ifac->ifac_scope_id;
		memcpy(&ifcp->ifc_ripsin, &ripsin, ripsin.ss_len);
		ifcp->ifc_ripsin.sin6_scope_id = ifcp->ifc_index;
		setindex2ifc(ifcp->ifc_index, ifcp);
		ifcp->ifc_mtu = getifmtu(ifcp->ifc_index);
		if (ifcp->ifc_mtu > RIP6_MAXMTU)
			ifcp->ifc_mtu = RIP6_MAXMTU;
		if (ioctl(s, SIOCGIFMETRIC, (char *)&ifr) < 0) {
			fatal("ioctl: SIOCGIFMETRIC");
			/*NOTREACHED*/
		}
		ifcp->ifc_metric = ifr.ifr_metric;
		trace(1, "\tindex: %d, mtu: %d, metric: %d\n",
			ifcp->ifc_index, ifcp->ifc_mtu, ifcp->ifc_metric);
	} else
		ifcp->ifc_cflags |= IFC_CHANGED;

	TAILQ_INSERT_HEAD(&ifcp->ifc_ifac_head, ifac, ifac_next);

	return 0;
}

static void
ifremove(int ifindex)
{
	struct ifc *ifcp;
	struct riprt *rrt;

	TAILQ_FOREACH(ifcp, &ifc_head, ifc_next) {
		if (ifcp->ifc_index == ifindex)
			break;
	}
	if (ifcp == NULL)
		return;

	tracet(1, "ifremove: %s is departed.\n", ifcp->ifc_name);
	TAILQ_REMOVE(&ifc_head, ifcp, ifc_next);

	TAILQ_FOREACH(rrt, &riprt_head, rrt_next) {
		if (rrt->rrt_index == ifcp->ifc_index &&
		    rrt->rrt_rflags & RRTF_AGGREGATE)
			delroute(&rrt->rrt_info, &rrt->rrt_gw);
	}
	free(ifcp);
}

/*
 * Receive and process routing messages.
 * Update interface information as necesssary.
 */
static void
rtrecv(void)
{
	char buf[BUFSIZ];
	char *p, *q = NULL;
	struct rt_msghdr *rtm;
	struct ifa_msghdr *ifam;
	struct if_msghdr *ifm;
	struct if_announcemsghdr *ifan;
	int len;
	struct ifc *ifcp, *ic;
	int iface = 0, rtable = 0;
	struct sockaddr_in6 *rta[RTAX_MAX];
	struct sockaddr_in6 mask;
	int i, addrs = 0;
	struct riprt *rrt;

	if ((len = read(rtsock, buf, sizeof(buf))) < 0) {
		perror("read from rtsock");
		exit(1);
	}
	if (len == 0)
		return;
#if 0
	if (len < sizeof(*rtm)) {
		trace(1, "short read from rtsock: %d (should be > %lu)\n",
			len, (u_long)sizeof(*rtm));
		return;
	}
#endif
	if (dflag >= 2) {
		fprintf(stderr, "rtmsg:\n");
		for (i = 0; i < len; i++) {
			fprintf(stderr, "%02x ", buf[i] & 0xff);
			if (i % 16 == 15) fprintf(stderr, "\n");
		}
		fprintf(stderr, "\n");
	}

	for (p = buf; p - buf < len; p +=
	    ((struct rt_msghdr *)(void *)p)->rtm_msglen) {
		if (((struct rt_msghdr *)(void *)p)->rtm_version != RTM_VERSION)
			continue;

		/* safety against bogus message */
		if (((struct rt_msghdr *)(void *)p)->rtm_msglen <= 0) {
			trace(1, "bogus rtmsg: length=%d\n",
				((struct rt_msghdr *)(void *)p)->rtm_msglen);
			break;
		}
		rtm = NULL;
		ifam = NULL;
		ifm = NULL;
		switch (((struct rt_msghdr *)(void *)p)->rtm_type) {
		case RTM_NEWADDR:
		case RTM_DELADDR:
			ifam = (struct ifa_msghdr *)(void *)p;
			addrs = ifam->ifam_addrs;
			q = (char *)(ifam + 1);
			break;
		case RTM_IFINFO:
			ifm = (struct if_msghdr *)(void *)p;
			addrs = ifm->ifm_addrs;
			q = (char *)(ifm + 1);
			break;
		case RTM_IFANNOUNCE:
			ifan = (struct if_announcemsghdr *)(void *)p;
			switch (ifan->ifan_what) {
			case IFAN_ARRIVAL:
				iface++;
				break;
			case IFAN_DEPARTURE:
				ifremove(ifan->ifan_index);
				iface++;
				break;
			}
			break;
		default:
			rtm = (struct rt_msghdr *)(void *)p;
			if (rtm->rtm_version != RTM_VERSION) {
				trace(1, "unexpected rtmsg version %d "
					"(should be %d)\n",
					rtm->rtm_version, RTM_VERSION);
				continue;
			}
			/*
			 * Only messages that use the struct rt_msghdr
			 * format are allowed beyond this point.
			 */
			if (rtm->rtm_type > RTM_RESOLVE) {
				trace(1, "rtmsg type %d ignored\n",
					rtm->rtm_type);
				continue;
			}
			addrs = rtm->rtm_addrs;
			q = (char *)(rtm + 1);
			if (rtm->rtm_pid == pid) {
#if 0
				trace(1, "rtmsg looped back to me, ignored\n");
#endif
				continue;
			}
			break;
		}
		memset(&rta, 0, sizeof(rta));
		for (i = 0; i < RTAX_MAX; i++) {
			if (addrs & (1 << i)) {
				rta[i] = (struct sockaddr_in6 *)(void *)q;
				q += ROUNDUP(rta[i]->sin6_len);
			}
		}

		trace(1, "rtsock: %s (addrs=%x)\n",
			rttypes((struct rt_msghdr *)(void *)p), addrs);
		if (dflag >= 2) {
			for (i = 0;
			     i < ((struct rt_msghdr *)(void *)p)->rtm_msglen;
			     i++) {
				fprintf(stderr, "%02x ", p[i] & 0xff);
				if (i % 16 == 15) fprintf(stderr, "\n");
			}
			fprintf(stderr, "\n");
		}

		/*
		 * Easy ones first.
		 *
		 * We may be able to optimize by using ifm->ifm_index or
		 * ifam->ifam_index.  For simplicity we don't do that here.
		 */
		switch (((struct rt_msghdr *)(void *)p)->rtm_type) {
		case RTM_NEWADDR:
		case RTM_IFINFO:
			iface++;
			continue;
		case RTM_ADD:
			rtable++;
			continue;
		case RTM_LOSING:
		case RTM_MISS:
		case RTM_GET:
		case RTM_LOCK:
			/* nothing to be done here */
			trace(1, "\tnothing to be done, ignored\n");
			continue;
		}

#if 0
		if (rta[RTAX_DST] == NULL) {
			trace(1, "\tno destination, ignored\n");
			continue;
		}
		if (rta[RTAX_DST]->sin6_family != AF_INET6) {
			trace(1, "\taf mismatch, ignored\n");
			continue;
		}
		if (IN6_IS_ADDR_LINKLOCAL(&rta[RTAX_DST]->sin6_addr)) {
			trace(1, "\tlinklocal destination, ignored\n");
			continue;
		}
		if (IN6_ARE_ADDR_EQUAL(&rta[RTAX_DST]->sin6_addr, &in6addr_loopback)) {
			trace(1, "\tloopback destination, ignored\n");
			continue;		/* Loopback */
		}
		if (IN6_IS_ADDR_MULTICAST(&rta[RTAX_DST]->sin6_addr)) {
			trace(1, "\tmulticast destination, ignored\n");
			continue;
		}
#endif

		/* hard ones */
		switch (((struct rt_msghdr *)(void *)p)->rtm_type) {
		case RTM_NEWADDR:
		case RTM_IFINFO:
		case RTM_ADD:
		case RTM_LOSING:
		case RTM_MISS:
		case RTM_GET:
		case RTM_LOCK:
			/* should already be handled */
			fatal("rtrecv: never reach here");
			/*NOTREACHED*/
		case RTM_DELETE:
			if (!rta[RTAX_DST] || !rta[RTAX_GATEWAY]) {
				trace(1, "\tsome of dst/gw/netamsk are "
				    "unavailable, ignored\n");
				break;
			}
			if ((rtm->rtm_flags & RTF_HOST) != 0) {
				mask.sin6_len = sizeof(mask);
				memset(&mask.sin6_addr, 0xff,
				    sizeof(mask.sin6_addr));
				rta[RTAX_NETMASK] = &mask;
			} else if (!rta[RTAX_NETMASK]) {
				trace(1, "\tsome of dst/gw/netamsk are "
				    "unavailable, ignored\n");
				break;
			}
			if (rt_del(rta[RTAX_DST], rta[RTAX_GATEWAY],
			    rta[RTAX_NETMASK]) == 0) {
				rtable++;	/*just to be sure*/
			}
			break;
		case RTM_CHANGE:
		case RTM_REDIRECT:
			trace(1, "\tnot supported yet, ignored\n");
			break;
		case RTM_DELADDR:
			if (!rta[RTAX_NETMASK] || !rta[RTAX_IFA]) {
				trace(1, "\tno netmask or ifa given, ignored\n");
				break;
			}
			if (ifam->ifam_index < nindex2ifc)
				ifcp = index2ifc[ifam->ifam_index];
			else
				ifcp = NULL;
			if (!ifcp) {
				trace(1, "\tinvalid ifam_index %d, ignored\n",
					ifam->ifam_index);
				break;
			}
			if (!rt_deladdr(ifcp, rta[RTAX_IFA], rta[RTAX_NETMASK]))
				iface++;
			break;
		}

	}

	if (iface) {
		trace(1, "rtsock: reconfigure interfaces, refresh interface routes\n");
		ifconfig();
		TAILQ_FOREACH(ifcp, &ifc_head, ifc_next) {
			if (ifcp->ifc_cflags & IFC_CHANGED) {
				if (ifrt(ifcp, 1)) {
					TAILQ_FOREACH(ic, &ifc_head, ifc_next) {
						if (ifcp->ifc_index == ic->ifc_index)
							continue;
						if (ic->ifc_flags & IFF_UP)
							ripsend(ic, &ic->ifc_ripsin,
							RRTF_CHANGED);
					}
					/* Reset the flag */
					TAILQ_FOREACH(rrt, &riprt_head, rrt_next) {
						rrt->rrt_rflags &= ~RRTF_CHANGED;
					}
				}
				ifcp->ifc_cflags &= ~IFC_CHANGED;
			}
		}
	}
	if (rtable) {
		trace(1, "rtsock: read routing table again\n");
		krtread(1);
	}
}

/*
 * remove specified route from the internal routing table.
 */
static int
rt_del(const struct sockaddr_in6 *sdst,
	const struct sockaddr_in6 *sgw,
	const struct sockaddr_in6 *smask)
{
	const struct in6_addr *dst = NULL;
	const struct in6_addr *gw = NULL;
	int prefix;
	struct netinfo6 ni6;
	struct riprt *rrt = NULL;
	time_t t_lifetime;

	if (sdst->sin6_family != AF_INET6) {
		trace(1, "\tother AF, ignored\n");
		return -1;
	}
	if (IN6_IS_ADDR_LINKLOCAL(&sdst->sin6_addr)
	 || IN6_ARE_ADDR_EQUAL(&sdst->sin6_addr, &in6addr_loopback)
	 || IN6_IS_ADDR_MULTICAST(&sdst->sin6_addr)) {
		trace(1, "\taddress %s not interesting, ignored\n",
			inet6_n2p(&sdst->sin6_addr));
		return -1;
	}
	dst = &sdst->sin6_addr;
	if (sgw->sin6_family == AF_INET6) {
		/* easy case */
		gw = &sgw->sin6_addr;
		prefix = sin6mask2len(smask);
	} else if (sgw->sin6_family == AF_LINK) {
		/*
		 * Interface route... a hard case.  We need to get the prefix
		 * length from the kernel, but we now are parsing rtmsg.
		 * We'll purge matching routes from my list, then get the
		 * fresh list.
		 */
		struct riprt *longest;
		trace(1, "\t%s is an interface route, guessing prefixlen\n",
			inet6_n2p(dst));
		longest = NULL;
		TAILQ_FOREACH(rrt, &riprt_head, rrt_next) {
			if (IN6_ARE_ADDR_EQUAL(&rrt->rrt_info.rip6_dest,
					&sdst->sin6_addr)
			 && IN6_IS_ADDR_LOOPBACK(&rrt->rrt_gw)) {
				if (!longest
				 || longest->rrt_info.rip6_plen <
						 rrt->rrt_info.rip6_plen) {
					longest = rrt;
				}
			}
		}
		rrt = longest;
		if (!rrt) {
			trace(1, "\tno matching interface route found\n");
			return -1;
		}
		gw = &in6addr_loopback;
		prefix = rrt->rrt_info.rip6_plen;
	} else {
		trace(1, "\tunsupported af: (gw=%d)\n", sgw->sin6_family);
		return -1;
	}

	trace(1, "\tdeleting %s/%d ", inet6_n2p(dst), prefix);
	trace(1, "gw %s\n", inet6_n2p(gw));
	t_lifetime = time(NULL) - RIP_LIFETIME;
	/* age route for interface address */
	memset(&ni6, 0, sizeof(ni6));
	ni6.rip6_dest = *dst;
	ni6.rip6_plen = prefix;
	applyplen(&ni6.rip6_dest, ni6.rip6_plen);	/*to be sure*/
	trace(1, "\tfind route %s/%d\n", inet6_n2p(&ni6.rip6_dest),
		ni6.rip6_plen);
	if (!rrt && (rrt = rtsearch(&ni6)) == NULL) {
		trace(1, "\tno route found\n");
		return -1;
	}
#if 0
	if ((rrt->rrt_flags & RTF_STATIC) == 0) {
		trace(1, "\tyou can delete static routes only\n");
	} else
#endif
	if (!IN6_ARE_ADDR_EQUAL(&rrt->rrt_gw, gw)) {
		trace(1, "\tgw mismatch: %s <-> ",
			inet6_n2p(&rrt->rrt_gw));
		trace(1, "%s\n", inet6_n2p(gw));
	} else {
		trace(1, "\troute found, age it\n");
		if (rrt->rrt_t == 0 || rrt->rrt_t > t_lifetime) {
			rrt->rrt_t = t_lifetime;
			rrt->rrt_info.rip6_metric = HOPCNT_INFINITY6;
		}
	}
	return 0;
}

/*
 * remove specified address from internal interface/routing table.
 */
static int
rt_deladdr(struct ifc *ifcp,
	const struct sockaddr_in6 *sifa,
	const struct sockaddr_in6 *smask)
{
	const struct in6_addr *addr = NULL;
	int prefix;
	struct ifac *ifac = NULL;
	struct netinfo6 ni6;
	struct riprt *rrt = NULL;
	time_t t_lifetime;
	int updated = 0;

	if (sifa->sin6_family != AF_INET6) {
		trace(1, "\tother AF, ignored\n");
		return -1;
	}
	addr = &sifa->sin6_addr;
	prefix = sin6mask2len(smask);

	trace(1, "\tdeleting %s/%d from %s\n",
		inet6_n2p(addr), prefix, ifcp->ifc_name);
	ifac = ifa_match(ifcp, addr, prefix);
	if (!ifac) {
		trace(1, "\tno matching ifa found for %s/%d on %s\n",
			inet6_n2p(addr), prefix, ifcp->ifc_name);
		return -1;
	}
	if (ifac->ifac_ifc != ifcp) {
		trace(1, "\taddress table corrupt: back pointer does not match "
			"(%s != %s)\n",
			ifcp->ifc_name, ifac->ifac_ifc->ifc_name);
		return -1;
	}
	TAILQ_REMOVE(&ifcp->ifc_ifac_head, ifac, ifac_next);
	t_lifetime = time(NULL) - RIP_LIFETIME;
	/* age route for interface address */
	memset(&ni6, 0, sizeof(ni6));
	ni6.rip6_dest = ifac->ifac_addr;
	ni6.rip6_plen = ifac->ifac_plen;
	applyplen(&ni6.rip6_dest, ni6.rip6_plen);
	trace(1, "\tfind interface route %s/%d on %d\n",
		inet6_n2p(&ni6.rip6_dest), ni6.rip6_plen, ifcp->ifc_index);
	if ((rrt = rtsearch(&ni6)) != NULL) {
		struct in6_addr none;
		memset(&none, 0, sizeof(none));
		if (rrt->rrt_index == ifcp->ifc_index &&
		    (IN6_ARE_ADDR_EQUAL(&rrt->rrt_gw, &none) ||
		     IN6_IS_ADDR_LOOPBACK(&rrt->rrt_gw))) {
			trace(1, "\troute found, age it\n");
			if (rrt->rrt_t == 0 || rrt->rrt_t > t_lifetime) {
				rrt->rrt_t = t_lifetime;
				rrt->rrt_info.rip6_metric = HOPCNT_INFINITY6;
			}
			updated++;
		} else {
			trace(1, "\tnon-interface route found: %s/%d on %d\n",
				inet6_n2p(&rrt->rrt_info.rip6_dest),
				rrt->rrt_info.rip6_plen,
				rrt->rrt_index);
		}
	} else
		trace(1, "\tno interface route found\n");
	/* age route for p2p destination */
	if (ifcp->ifc_flags & IFF_POINTOPOINT) {
		memset(&ni6, 0, sizeof(ni6));
		ni6.rip6_dest = ifac->ifac_raddr;
		ni6.rip6_plen = 128;
		applyplen(&ni6.rip6_dest, ni6.rip6_plen);	/*to be sure*/
		trace(1, "\tfind p2p route %s/%d on %d\n",
			inet6_n2p(&ni6.rip6_dest), ni6.rip6_plen,
			ifcp->ifc_index);
		if ((rrt = rtsearch(&ni6)) != NULL) {
			if (rrt->rrt_index == ifcp->ifc_index &&
			    IN6_ARE_ADDR_EQUAL(&rrt->rrt_gw,
			    &ifac->ifac_addr)) {
				trace(1, "\troute found, age it\n");
				if (rrt->rrt_t == 0 || rrt->rrt_t > t_lifetime) {
					rrt->rrt_t = t_lifetime;
					rrt->rrt_info.rip6_metric =
					    HOPCNT_INFINITY6;
					updated++;
				}
			} else {
				trace(1, "\tnon-p2p route found: %s/%d on %d\n",
					inet6_n2p(&rrt->rrt_info.rip6_dest),
					rrt->rrt_info.rip6_plen,
					rrt->rrt_index);
			}
		} else
			trace(1, "\tno p2p route found\n");
	}
	free(ifac);

	return ((updated) ? 0 : -1);
}

/*
 * Get each interface address and put those interface routes to the route
 * list.
 */
static int
ifrt(struct ifc *ifcp, int again)
{
	struct ifac *ifac;
	struct riprt *rrt = NULL, *search_rrt, *loop_rrt;
	struct netinfo6 *np;
	time_t t_lifetime;
	int need_trigger = 0;

#if 0
	if (ifcp->ifc_flags & IFF_LOOPBACK)
		return 0;			/* ignore loopback */
#endif

	if (ifcp->ifc_flags & IFF_POINTOPOINT) {
		ifrt_p2p(ifcp, again);
		return 0;
	}

	TAILQ_FOREACH(ifac, &ifcp->ifc_ifac_head, ifac_next) {
		if (IN6_IS_ADDR_LINKLOCAL(&ifac->ifac_addr)) {
#if 0
			trace(1, "route: %s on %s: "
			    "skip linklocal interface address\n",
			    inet6_n2p(&ifac->ifac_addr), ifcp->ifc_name);
#endif
			continue;
		}
		if (IN6_IS_ADDR_UNSPECIFIED(&ifac->ifac_addr)) {
#if 0
			trace(1, "route: %s: skip unspec interface address\n",
			    ifcp->ifc_name);
#endif
			continue;
		}
		if (IN6_IS_ADDR_LOOPBACK(&ifac->ifac_addr)) {
#if 0
			trace(1, "route: %s: skip loopback address\n",
			    ifcp->ifc_name);
#endif
			continue;
		}
		if (ifcp->ifc_flags & IFF_UP) {
			if ((rrt = MALLOC(struct riprt)) == NULL)
				fatal("malloc: struct riprt");
			memset(rrt, 0, sizeof(*rrt));
			rrt->rrt_same = NULL;
			rrt->rrt_index = ifcp->ifc_index;
			rrt->rrt_t = 0;	/* don't age */
			rrt->rrt_info.rip6_dest = ifac->ifac_addr;
			rrt->rrt_info.rip6_tag = htons(routetag & 0xffff);
			rrt->rrt_info.rip6_metric = 1 + ifcp->ifc_metric;
			rrt->rrt_info.rip6_plen = ifac->ifac_plen;
			rrt->rrt_flags = RTF_HOST;
			rrt->rrt_rflags |= RRTF_CHANGED;
			applyplen(&rrt->rrt_info.rip6_dest, ifac->ifac_plen);
			memset(&rrt->rrt_gw, 0, sizeof(struct in6_addr));
			rrt->rrt_gw = ifac->ifac_addr;
			np = &rrt->rrt_info;
			search_rrt = rtsearch(np);
			if (search_rrt != NULL) {
				if (search_rrt->rrt_info.rip6_metric <=
				    rrt->rrt_info.rip6_metric) {
					/* Already have better route */
					if (!again) {
						trace(1, "route: %s/%d: "
						    "already registered (%s)\n",
						    inet6_n2p(&np->rip6_dest), np->rip6_plen,
						    ifcp->ifc_name);
					}
					goto next;
				}

				TAILQ_REMOVE(&riprt_head, search_rrt, rrt_next);
				delroute(&search_rrt->rrt_info,
				    &search_rrt->rrt_gw);
				free(search_rrt);
			}
			/* Attach the route to the list */
			trace(1, "route: %s/%d: register route (%s)\n",
			    inet6_n2p(&np->rip6_dest), np->rip6_plen,
			    ifcp->ifc_name);
			TAILQ_INSERT_HEAD(&riprt_head, rrt, rrt_next);
			addroute(rrt, &rrt->rrt_gw, ifcp);
			rrt = NULL;
			sendrequest(ifcp);
			ripsend(ifcp, &ifcp->ifc_ripsin, 0);
			need_trigger = 1;
		} else {
			TAILQ_FOREACH(loop_rrt, &riprt_head, rrt_next) {
				if (loop_rrt->rrt_index == ifcp->ifc_index) {
					t_lifetime = time(NULL) - RIP_LIFETIME;
					if (loop_rrt->rrt_t == 0 || loop_rrt->rrt_t > t_lifetime) {
						loop_rrt->rrt_t = t_lifetime;
						loop_rrt->rrt_info.rip6_metric = HOPCNT_INFINITY6;
						loop_rrt->rrt_rflags |= RRTF_CHANGED;
						need_trigger = 1;
					}
				}
			}
                }
	next:
		if (rrt)
			free(rrt);
	}
	return need_trigger;
}

/*
 * there are couple of p2p interface routing models.  "behavior" lets
 * you pick one.  it looks that gated behavior fits best with BSDs,
 * since BSD kernels do not look at prefix length on p2p interfaces.
 */
static void
ifrt_p2p(struct ifc *ifcp, int again)
{
	struct ifac *ifac;
	struct riprt *rrt, *orrt;
	struct netinfo6 *np;
	struct in6_addr addr, dest;
	int advert, ignore, i;
#define P2PADVERT_NETWORK	1
#define P2PADVERT_ADDR		2
#define P2PADVERT_DEST		4
#define P2PADVERT_MAX		4
	const enum { CISCO, GATED, ROUTE6D } behavior = GATED;
	const char *category = "";
	const char *noadv;

	TAILQ_FOREACH(ifac, &ifcp->ifc_ifac_head, ifac_next) {
		addr = ifac->ifac_addr;
		dest = ifac->ifac_raddr;
		applyplen(&addr, ifac->ifac_plen);
		applyplen(&dest, ifac->ifac_plen);
		advert = ignore = 0;
		switch (behavior) {
		case CISCO:
			/*
			 * honor addr/plen, just like normal shared medium
			 * interface.  this may cause trouble if you reuse
			 * addr/plen on other interfaces.
			 *
			 * advertise addr/plen.
			 */
			advert |= P2PADVERT_NETWORK;
			break;
		case GATED:
			/*
			 * prefixlen on p2p interface is meaningless.
			 * advertise addr/128 and dest/128.
			 *
			 * do not install network route to route6d routing
			 * table (if we do, it would prevent route installation
			 * for other p2p interface that shares addr/plen).
			 *
			 * XXX what should we do if dest is ::?  it will not
			 * get announced anyways (see following filter),
			 * but we need to think.
			 */
			advert |= P2PADVERT_ADDR;
			advert |= P2PADVERT_DEST;
			ignore |= P2PADVERT_NETWORK;
			break;
		case ROUTE6D:
			/*
			 * just for testing.  actually the code is redundant
			 * given the current p2p interface address assignment
			 * rule for kame kernel.
			 *
			 * intent:
			 *	A/n -> announce A/n
			 *	A B/n, A and B share prefix -> A/n (= B/n)
			 *	A B/n, do not share prefix -> A/128 and B/128
			 * actually, A/64 and A B/128 are the only cases
			 * permitted by the kernel:
			 *	A/64 -> A/64
			 *	A B/128 -> A/128 and B/128
			 */
			if (!IN6_IS_ADDR_UNSPECIFIED(&ifac->ifac_raddr)) {
				if (IN6_ARE_ADDR_EQUAL(&addr, &dest))
					advert |= P2PADVERT_NETWORK;
				else {
					advert |= P2PADVERT_ADDR;
					advert |= P2PADVERT_DEST;
					ignore |= P2PADVERT_NETWORK;
				}
			} else
				advert |= P2PADVERT_NETWORK;
			break;
		}

		for (i = 1; i <= P2PADVERT_MAX; i *= 2) {
			if ((ignore & i) != 0)
				continue;
			if ((rrt = MALLOC(struct riprt)) == NULL) {
				fatal("malloc: struct riprt");
				/*NOTREACHED*/
			}
			memset(rrt, 0, sizeof(*rrt));
			rrt->rrt_same = NULL;
			rrt->rrt_index = ifcp->ifc_index;
			rrt->rrt_t = 0;	/* don't age */
			switch (i) {
			case P2PADVERT_NETWORK:
				rrt->rrt_info.rip6_dest = ifac->ifac_addr;
				rrt->rrt_info.rip6_plen = ifac->ifac_plen;
				applyplen(&rrt->rrt_info.rip6_dest,
				    ifac->ifac_plen);
				category = "network";
				break;
			case P2PADVERT_ADDR:
				rrt->rrt_info.rip6_dest = ifac->ifac_addr;
				rrt->rrt_info.rip6_plen = 128;
				rrt->rrt_gw = in6addr_loopback;
				category = "addr";
				break;
			case P2PADVERT_DEST:
				rrt->rrt_info.rip6_dest = ifac->ifac_raddr;
				rrt->rrt_info.rip6_plen = 128;
				rrt->rrt_gw = ifac->ifac_addr;
				category = "dest";
				break;
			}
			if (IN6_IS_ADDR_UNSPECIFIED(&rrt->rrt_info.rip6_dest) ||
			    IN6_IS_ADDR_LINKLOCAL(&rrt->rrt_info.rip6_dest)) {
#if 0
				trace(1, "route: %s: skip unspec/linklocal "
				    "(%s on %s)\n", category, ifcp->ifc_name);
#endif
				free(rrt);
				continue;
			}
			if ((advert & i) == 0) {
				rrt->rrt_rflags |= RRTF_NOADVERTISE;
				noadv = ", NO-ADV";
			} else
				noadv = "";
			rrt->rrt_info.rip6_tag = htons(routetag & 0xffff);
			rrt->rrt_info.rip6_metric = 1 + ifcp->ifc_metric;
			np = &rrt->rrt_info;
			orrt = rtsearch(np);
			if (!orrt) {
				/* Attach the route to the list */
				trace(1, "route: %s/%d: register route "
				    "(%s on %s%s)\n",
				    inet6_n2p(&np->rip6_dest), np->rip6_plen,
				    category, ifcp->ifc_name, noadv);
				TAILQ_INSERT_HEAD(&riprt_head, rrt, rrt_next);
			} else if (rrt->rrt_index != orrt->rrt_index ||
			    rrt->rrt_info.rip6_metric != orrt->rrt_info.rip6_metric) {
				/* replace route */
				TAILQ_INSERT_BEFORE(orrt, rrt, rrt_next);
				TAILQ_REMOVE(&riprt_head, orrt, rrt_next);
				free(orrt);

				trace(1, "route: %s/%d: update (%s on %s%s)\n",
				    inet6_n2p(&np->rip6_dest), np->rip6_plen,
				    category, ifcp->ifc_name, noadv);
			} else {
				/* Already found */
				if (!again) {
					trace(1, "route: %s/%d: "
					    "already registered (%s on %s%s)\n",
					    inet6_n2p(&np->rip6_dest),
					    np->rip6_plen, category,
					    ifcp->ifc_name, noadv);
				}
				free(rrt);
			}
		}
	}
#undef P2PADVERT_NETWORK
#undef P2PADVERT_ADDR
#undef P2PADVERT_DEST
#undef P2PADVERT_MAX
}

static int
getifmtu(int ifindex)
{
	int	mib[6];
	char	*buf;
	size_t	msize;
	struct	if_msghdr *ifm;
	int	mtu;

	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;
	mib[3] = AF_INET6;
	mib[4] = NET_RT_IFLIST;
	mib[5] = ifindex;
	if (sysctl(mib, nitems(mib), NULL, &msize, NULL, 0) < 0) {
		fatal("sysctl estimate NET_RT_IFLIST");
		/*NOTREACHED*/
	}
	if ((buf = malloc(msize)) == NULL) {
		fatal("malloc");
		/*NOTREACHED*/
	}
	if (sysctl(mib, nitems(mib), buf, &msize, NULL, 0) < 0) {
		fatal("sysctl NET_RT_IFLIST");
		/*NOTREACHED*/
	}
	ifm = (struct if_msghdr *)(void *)buf;
	mtu = ifm->ifm_data.ifi_mtu;
	if (ifindex != ifm->ifm_index) {
		fatal("ifindex does not match with ifm_index");
		/*NOTREACHED*/
	}
	free(buf);
	return mtu;
}

static const char *
rttypes(struct rt_msghdr *rtm)
{
#define	RTTYPE(s, f) \
do { \
	if (rtm->rtm_type == (f)) \
		return (s); \
} while (0)
	RTTYPE("ADD", RTM_ADD);
	RTTYPE("DELETE", RTM_DELETE);
	RTTYPE("CHANGE", RTM_CHANGE);
	RTTYPE("GET", RTM_GET);
	RTTYPE("LOSING", RTM_LOSING);
	RTTYPE("REDIRECT", RTM_REDIRECT);
	RTTYPE("MISS", RTM_MISS);
	RTTYPE("LOCK", RTM_LOCK);
	RTTYPE("NEWADDR", RTM_NEWADDR);
	RTTYPE("DELADDR", RTM_DELADDR);
	RTTYPE("IFINFO", RTM_IFINFO);
#ifdef RTM_OIFINFO
	RTTYPE("OIFINFO", RTM_OIFINFO);
#endif
#ifdef RTM_IFANNOUNCE
	RTTYPE("IFANNOUNCE", RTM_IFANNOUNCE);
#endif
#ifdef RTM_NEWMADDR
	RTTYPE("NEWMADDR", RTM_NEWMADDR);
#endif
#ifdef RTM_DELMADDR
	RTTYPE("DELMADDR", RTM_DELMADDR);
#endif
#undef RTTYPE
	return NULL;
}

static const char *
rtflags(struct rt_msghdr *rtm)
{
	static char buf[BUFSIZ];

	/*
	 * letter conflict should be okay.  painful when *BSD diverges...
	 */
	strlcpy(buf, "", sizeof(buf));
#define	RTFLAG(s, f) \
do { \
	if (rtm->rtm_flags & (f)) \
		strlcat(buf, (s), sizeof(buf)); \
} while (0)
	RTFLAG("U", RTF_UP);
	RTFLAG("G", RTF_GATEWAY);
	RTFLAG("H", RTF_HOST);
	RTFLAG("R", RTF_REJECT);
	RTFLAG("D", RTF_DYNAMIC);
	RTFLAG("M", RTF_MODIFIED);
	RTFLAG("d", RTF_DONE);
#ifdef	RTF_MASK
	RTFLAG("m", RTF_MASK);
#endif
#ifdef RTF_CLONED
	RTFLAG("c", RTF_CLONED);
#endif
	RTFLAG("X", RTF_XRESOLVE);
#ifdef RTF_LLINFO
	RTFLAG("L", RTF_LLINFO);
#endif
	RTFLAG("S", RTF_STATIC);
	RTFLAG("B", RTF_BLACKHOLE);
#ifdef RTF_PROTO3
	RTFLAG("3", RTF_PROTO3);
#endif
	RTFLAG("2", RTF_PROTO2);
	RTFLAG("1", RTF_PROTO1);
#ifdef RTF_BROADCAST
	RTFLAG("b", RTF_BROADCAST);
#endif
#ifdef RTF_DEFAULT
	RTFLAG("d", RTF_DEFAULT);
#endif
#ifdef RTF_ISAROUTER
	RTFLAG("r", RTF_ISAROUTER);
#endif
#ifdef RTF_TUNNEL
	RTFLAG("T", RTF_TUNNEL);
#endif
#ifdef RTF_AUTH
	RTFLAG("A", RTF_AUTH);
#endif
#ifdef RTF_CRYPT
	RTFLAG("E", RTF_CRYPT);
#endif
#undef RTFLAG
	return buf;
}

static const char *
ifflags(int flags)
{
	static char buf[BUFSIZ];

	strlcpy(buf, "", sizeof(buf));
#define	IFFLAG(s, f) \
do { \
	if (flags & (f)) { \
		if (buf[0]) \
			strlcat(buf, ",", sizeof(buf)); \
		strlcat(buf, (s), sizeof(buf)); \
	} \
} while (0)
	IFFLAG("UP", IFF_UP);
	IFFLAG("BROADCAST", IFF_BROADCAST);
	IFFLAG("DEBUG", IFF_DEBUG);
	IFFLAG("LOOPBACK", IFF_LOOPBACK);
	IFFLAG("POINTOPOINT", IFF_POINTOPOINT);
#ifdef IFF_NOTRAILERS
	IFFLAG("NOTRAILERS", IFF_NOTRAILERS);
#endif
	IFFLAG("RUNNING", IFF_RUNNING);
	IFFLAG("NOARP", IFF_NOARP);
	IFFLAG("PROMISC", IFF_PROMISC);
	IFFLAG("ALLMULTI", IFF_ALLMULTI);
	IFFLAG("OACTIVE", IFF_OACTIVE);
	IFFLAG("SIMPLEX", IFF_SIMPLEX);
	IFFLAG("LINK0", IFF_LINK0);
	IFFLAG("LINK1", IFF_LINK1);
	IFFLAG("LINK2", IFF_LINK2);
	IFFLAG("MULTICAST", IFF_MULTICAST);
#undef IFFLAG
	return buf;
}

static void
krtread(int again)
{
	int mib[6];
	size_t msize;
	char *buf, *p, *lim;
	struct rt_msghdr *rtm;
	int retry;
	const char *errmsg;

	retry = 0;
	buf = NULL;
	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;
	mib[3] = AF_INET6;	/* Address family */
	mib[4] = NET_RT_DUMP;	/* Dump the kernel routing table */
	mib[5] = 0;		/* No flags */
	do {
		if (retry)
			sleep(1);
		retry++;
		errmsg = NULL;
		if (buf) {
			free(buf);
			buf = NULL;
		}
		if (sysctl(mib, nitems(mib), NULL, &msize, NULL, 0) < 0) {
			errmsg = "sysctl estimate";
			continue;
		}
		if ((buf = malloc(msize)) == NULL) {
			errmsg = "malloc";
			continue;
		}
		if (sysctl(mib, nitems(mib), buf, &msize, NULL, 0) < 0) {
			errmsg = "sysctl NET_RT_DUMP";
			continue;
		}
	} while (retry < RT_DUMP_MAXRETRY && errmsg != NULL);
	if (errmsg) {
		fatal("%s (with %d retries, msize=%lu)", errmsg, retry,
		    (u_long)msize);
		/*NOTREACHED*/
	} else if (1 < retry)
		syslog(LOG_INFO, "NET_RT_DUMP %d retires", retry);

	lim = buf + msize;
	for (p = buf; p < lim; p += rtm->rtm_msglen) {
		rtm = (struct rt_msghdr *)(void *)p;
		rt_entry(rtm, again);
	}
	free(buf);
}

static void
rt_entry(struct rt_msghdr *rtm, int again)
{
	struct	sockaddr_in6 *sin6_dst, *sin6_gw, *sin6_mask;
	struct	sockaddr_in6 *sin6_genmask, *sin6_ifp;
	char	*rtmp, *ifname = NULL;
	struct	riprt *rrt, *orrt;
	struct	netinfo6 *np;
	int ifindex;

	sin6_dst = sin6_gw = sin6_mask = sin6_genmask = sin6_ifp = 0;
	if ((rtm->rtm_flags & RTF_UP) == 0 || rtm->rtm_flags &
		(RTF_XRESOLVE|RTF_BLACKHOLE)) {
		return;		/* not interested in the link route */
	}
	/* do not look at cloned routes */
#ifdef RTF_WASCLONED
	if (rtm->rtm_flags & RTF_WASCLONED)
		return;
#endif
#ifdef RTF_CLONED
	if (rtm->rtm_flags & RTF_CLONED)
		return;
#endif
	/* XXX: Ignore connected routes. */
	if (!(rtm->rtm_flags & (RTF_GATEWAY|RTF_HOST|RTF_STATIC)))
		return;
	/*
	 * do not look at dynamic routes.
	 * netbsd/openbsd cloned routes have UGHD.
	 */
	if (rtm->rtm_flags & RTF_DYNAMIC)
		return;
	rtmp = (char *)(rtm + 1);
	/* Destination */
	if ((rtm->rtm_addrs & RTA_DST) == 0)
		return;		/* ignore routes without destination address */
	sin6_dst = (struct sockaddr_in6 *)(void *)rtmp;
	rtmp += ROUNDUP(sin6_dst->sin6_len);
	if (rtm->rtm_addrs & RTA_GATEWAY) {
		sin6_gw = (struct sockaddr_in6 *)(void *)rtmp;
		rtmp += ROUNDUP(sin6_gw->sin6_len);
	}
	if (rtm->rtm_addrs & RTA_NETMASK) {
		sin6_mask = (struct sockaddr_in6 *)(void *)rtmp;
		rtmp += ROUNDUP(sin6_mask->sin6_len);
	}
	if (rtm->rtm_addrs & RTA_GENMASK) {
		sin6_genmask = (struct sockaddr_in6 *)(void *)rtmp;
		rtmp += ROUNDUP(sin6_genmask->sin6_len);
	}
	if (rtm->rtm_addrs & RTA_IFP) {
		sin6_ifp = (struct sockaddr_in6 *)(void *)rtmp;
		rtmp += ROUNDUP(sin6_ifp->sin6_len);
	}

	/* Destination */
	if (sin6_dst->sin6_family != AF_INET6)
		return;
	if (IN6_IS_ADDR_LINKLOCAL(&sin6_dst->sin6_addr))
		return;		/* Link-local */
	if (IN6_ARE_ADDR_EQUAL(&sin6_dst->sin6_addr, &in6addr_loopback))
		return;		/* Loopback */
	if (IN6_IS_ADDR_MULTICAST(&sin6_dst->sin6_addr))
		return;

	if ((rrt = MALLOC(struct riprt)) == NULL) {
		fatal("malloc: struct riprt");
		/*NOTREACHED*/
	}
	memset(rrt, 0, sizeof(*rrt));
	np = &rrt->rrt_info;
	rrt->rrt_same = NULL;
	rrt->rrt_t = time(NULL);
	if (aflag == 0 && (rtm->rtm_flags & RTF_STATIC))
		rrt->rrt_t = 0;	/* Don't age static routes */
	if (rtm->rtm_flags & Pflag)
		rrt->rrt_t = 0;	/* Don't age PROTO[123] routes */
	if ((rtm->rtm_flags & (RTF_HOST|RTF_GATEWAY)) == RTF_HOST)
		rrt->rrt_t = 0;	/* Don't age non-gateway host routes */
	np->rip6_tag = 0;
	np->rip6_metric = rtm->rtm_rmx.rmx_hopcount;
	if (np->rip6_metric < 1)
		np->rip6_metric = 1;
	rrt->rrt_flags = rtm->rtm_flags;
	np->rip6_dest = sin6_dst->sin6_addr;

	/* Mask or plen */
	if (rtm->rtm_flags & RTF_HOST)
		np->rip6_plen = 128;	/* Host route */
	else if (sin6_mask)
		np->rip6_plen = sin6mask2len(sin6_mask);
	else
		np->rip6_plen = 0;

	orrt = rtsearch(np);
	if (orrt && orrt->rrt_info.rip6_metric != HOPCNT_INFINITY6) {
		/* Already found */
		if (!again) {
			trace(1, "route: %s/%d flags %s: already registered\n",
				inet6_n2p(&np->rip6_dest), np->rip6_plen,
				rtflags(rtm));
		}
		free(rrt);
		return;
	}
	/* Gateway */
	if (!sin6_gw)
		memset(&rrt->rrt_gw, 0, sizeof(struct in6_addr));
	else {
		if (sin6_gw->sin6_family == AF_INET6)
			rrt->rrt_gw = sin6_gw->sin6_addr;
		else if (sin6_gw->sin6_family == AF_LINK) {
			/* XXX in case ppp link? */
			rrt->rrt_gw = in6addr_loopback;
		} else
			memset(&rrt->rrt_gw, 0, sizeof(struct in6_addr));
	}
	trace(1, "route: %s/%d flags %s",
		inet6_n2p(&np->rip6_dest), np->rip6_plen, rtflags(rtm));
	trace(1, " gw %s", inet6_n2p(&rrt->rrt_gw));

	/* Interface */
	ifindex = rtm->rtm_index;
	if ((unsigned int)ifindex < nindex2ifc && index2ifc[ifindex])
		ifname = index2ifc[ifindex]->ifc_name;
	else {
		trace(1, " not configured\n");
		free(rrt);
		return;
	}
	trace(1, " if %s sock %d", ifname, ifindex);
	rrt->rrt_index = ifindex;

	trace(1, "\n");

	/* Check gateway */
	if (!IN6_IS_ADDR_LINKLOCAL(&rrt->rrt_gw) &&
	    !IN6_IS_ADDR_LOOPBACK(&rrt->rrt_gw) &&
	    (rrt->rrt_flags & RTF_LOCAL) == 0) {
		trace(0, "***** Gateway %s is not a link-local address.\n",
			inet6_n2p(&rrt->rrt_gw));
		trace(0, "*****     dest(%s) if(%s) -- Not optimized.\n",
			inet6_n2p(&rrt->rrt_info.rip6_dest), ifname);
		rrt->rrt_rflags |= RRTF_NH_NOT_LLADDR;
	}

	/* Put it to the route list */
	if (orrt && orrt->rrt_info.rip6_metric == HOPCNT_INFINITY6) {
		/* replace route list */
		TAILQ_INSERT_BEFORE(orrt, rrt, rrt_next);
		TAILQ_REMOVE(&riprt_head, orrt, rrt_next);

		trace(1, "route: %s/%d flags %s: replace new route\n",
		    inet6_n2p(&np->rip6_dest), np->rip6_plen,
		    rtflags(rtm));
		free(orrt);
	} else
		TAILQ_INSERT_HEAD(&riprt_head, rrt, rrt_next);
}

static int
addroute(struct riprt *rrt,
	const struct in6_addr *gw,
	struct ifc *ifcp)
{
	struct	netinfo6 *np;
	u_char	buf[BUFSIZ], buf1[BUFSIZ], buf2[BUFSIZ];
	struct	rt_msghdr	*rtm;
	struct	sockaddr_in6	*sin6;
	int	len;

	np = &rrt->rrt_info;
	inet_ntop(AF_INET6, (const void *)gw, (char *)buf1, sizeof(buf1));
	inet_ntop(AF_INET6, (void *)&ifcp->ifc_mylladdr, (char *)buf2, sizeof(buf2));
	tracet(1, "ADD: %s/%d gw %s [%d] ifa %s\n",
		inet6_n2p(&np->rip6_dest), np->rip6_plen, buf1,
		np->rip6_metric - 1, buf2);
	if (rtlog)
		fprintf(rtlog, "%s: ADD: %s/%d gw %s [%d] ifa %s\n", hms(),
			inet6_n2p(&np->rip6_dest), np->rip6_plen, buf1,
			np->rip6_metric - 1, buf2);
	if (nflag)
		return 0;

	memset(buf, 0, sizeof(buf));
	rtm = (struct rt_msghdr *)(void *)buf;
	rtm->rtm_type = RTM_ADD;
	rtm->rtm_version = RTM_VERSION;
	rtm->rtm_seq = ++seq;
	rtm->rtm_pid = pid;
	rtm->rtm_flags = rrt->rrt_flags;
	rtm->rtm_flags |= Qflag;
	rtm->rtm_addrs = RTA_DST | RTA_GATEWAY | RTA_NETMASK;
	rtm->rtm_rmx.rmx_hopcount = np->rip6_metric - 1;
	rtm->rtm_inits = RTV_HOPCOUNT;
	sin6 = (struct sockaddr_in6 *)(void *)&buf[sizeof(struct rt_msghdr)];
	/* Destination */
	sin6->sin6_len = sizeof(struct sockaddr_in6);
	sin6->sin6_family = AF_INET6;
	sin6->sin6_addr = np->rip6_dest;
	sin6 = (struct sockaddr_in6 *)(void *)((char *)sin6 + ROUNDUP(sin6->sin6_len));
	/* Gateway */
	sin6->sin6_len = sizeof(struct sockaddr_in6);
	sin6->sin6_family = AF_INET6;
	sin6->sin6_addr = *gw;
	if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr))
		sin6->sin6_scope_id = ifcp->ifc_index;
	sin6 = (struct sockaddr_in6 *)(void *)((char *)sin6 + ROUNDUP(sin6->sin6_len));
	/* Netmask */
	sin6->sin6_len = sizeof(struct sockaddr_in6);
	sin6->sin6_family = AF_INET6;
	sin6->sin6_addr = *(plen2mask(np->rip6_plen));
	sin6 = (struct sockaddr_in6 *)(void *)((char *)sin6 + ROUNDUP(sin6->sin6_len));

	len = (char *)sin6 - (char *)buf;
	rtm->rtm_msglen = len;
	if (write(rtsock, buf, len) > 0)
		return 0;

	if (errno == EEXIST) {
		trace(0, "ADD: Route already exists %s/%d gw %s\n",
		    inet6_n2p(&np->rip6_dest), np->rip6_plen, buf1);
		if (rtlog)
			fprintf(rtlog, "ADD: Route already exists %s/%d gw %s\n",
			    inet6_n2p(&np->rip6_dest), np->rip6_plen, buf1);
	} else {
		trace(0, "Can not write to rtsock (addroute): %s\n",
		    strerror(errno));
		if (rtlog)
			fprintf(rtlog, "\tCan not write to rtsock: %s\n",
			    strerror(errno));
	}
	return -1;
}

static int
delroute(struct netinfo6 *np, struct in6_addr *gw)
{
	u_char	buf[BUFSIZ], buf2[BUFSIZ];
	struct	rt_msghdr	*rtm;
	struct	sockaddr_in6	*sin6;
	int	len;

	inet_ntop(AF_INET6, (void *)gw, (char *)buf2, sizeof(buf2));
	tracet(1, "DEL: %s/%d gw %s\n", inet6_n2p(&np->rip6_dest),
		np->rip6_plen, buf2);
	if (rtlog)
		fprintf(rtlog, "%s: DEL: %s/%d gw %s\n",
			hms(), inet6_n2p(&np->rip6_dest), np->rip6_plen, buf2);
	if (nflag)
		return 0;

	memset(buf, 0, sizeof(buf));
	rtm = (struct rt_msghdr *)(void *)buf;
	rtm->rtm_type = RTM_DELETE;
	rtm->rtm_version = RTM_VERSION;
	rtm->rtm_seq = ++seq;
	rtm->rtm_pid = pid;
	rtm->rtm_flags = RTF_UP | RTF_GATEWAY;
	rtm->rtm_flags |= Qflag;
	if (np->rip6_plen == sizeof(struct in6_addr) * 8)
		rtm->rtm_flags |= RTF_HOST;
	rtm->rtm_addrs = RTA_DST | RTA_GATEWAY | RTA_NETMASK;
	sin6 = (struct sockaddr_in6 *)(void *)&buf[sizeof(struct rt_msghdr)];
	/* Destination */
	sin6->sin6_len = sizeof(struct sockaddr_in6);
	sin6->sin6_family = AF_INET6;
	sin6->sin6_addr = np->rip6_dest;
	sin6 = (struct sockaddr_in6 *)(void *)((char *)sin6 + ROUNDUP(sin6->sin6_len));
	/* Gateway */
	sin6->sin6_len = sizeof(struct sockaddr_in6);
	sin6->sin6_family = AF_INET6;
	sin6->sin6_addr = *gw;
	sin6 = (struct sockaddr_in6 *)(void *)((char *)sin6 + ROUNDUP(sin6->sin6_len));
	/* Netmask */
	sin6->sin6_len = sizeof(struct sockaddr_in6);
	sin6->sin6_family = AF_INET6;
	sin6->sin6_addr = *(plen2mask(np->rip6_plen));
	sin6 = (struct sockaddr_in6 *)(void *)((char *)sin6 + ROUNDUP(sin6->sin6_len));

	len = (char *)sin6 - (char *)buf;
	rtm->rtm_msglen = len;
	if (write(rtsock, buf, len) >= 0)
		return 0;

	if (errno == ESRCH) {
		trace(0, "RTDEL: Route does not exist: %s/%d gw %s\n",
		    inet6_n2p(&np->rip6_dest), np->rip6_plen, buf2);
		if (rtlog)
			fprintf(rtlog, "RTDEL: Route does not exist: %s/%d gw %s\n",
			    inet6_n2p(&np->rip6_dest), np->rip6_plen, buf2);
	} else {
		trace(0, "Can not write to rtsock (delroute): %s\n",
		    strerror(errno));
		if (rtlog)
			fprintf(rtlog, "\tCan not write to rtsock: %s\n",
			    strerror(errno));
	}
	return -1;
}

#if 0
static struct in6_addr *
getroute(struct netinfo6 *np, struct in6_addr *gw)
{
	u_char buf[BUFSIZ];
	int myseq;
	int len;
	struct rt_msghdr *rtm;
	struct sockaddr_in6 *sin6;

	rtm = (struct rt_msghdr *)(void *)buf;
	len = sizeof(struct rt_msghdr) + sizeof(struct sockaddr_in6);
	memset(rtm, 0, len);
	rtm->rtm_type = RTM_GET;
	rtm->rtm_version = RTM_VERSION;
	myseq = ++seq;
	rtm->rtm_seq = myseq;
	rtm->rtm_addrs = RTA_DST;
	rtm->rtm_msglen = len;
	sin6 = (struct sockaddr_in6 *)(void *)&buf[sizeof(struct rt_msghdr)];
	sin6->sin6_len = sizeof(struct sockaddr_in6);
	sin6->sin6_family = AF_INET6;
	sin6->sin6_addr = np->rip6_dest;
	if (write(rtsock, buf, len) < 0) {
		if (errno == ESRCH)	/* No such route found */
			return NULL;
		perror("write to rtsock");
		exit(1);
	}
	do {
		if ((len = read(rtsock, buf, sizeof(buf))) < 0) {
			perror("read from rtsock");
			exit(1);
		}
		rtm = (struct rt_msghdr *)(void *)buf;
	} while (rtm->rtm_type != RTM_GET || rtm->rtm_seq != myseq ||
	    rtm->rtm_pid != pid);
	sin6 = (struct sockaddr_in6 *)(void *)&buf[sizeof(struct rt_msghdr)];
	if (rtm->rtm_addrs & RTA_DST) {
		sin6 = (struct sockaddr_in6 *)(void *)
			((char *)sin6 + ROUNDUP(sin6->sin6_len));
	}
	if (rtm->rtm_addrs & RTA_GATEWAY) {
		*gw = sin6->sin6_addr;
		return gw;
	}
	return NULL;
}
#endif

static const char *
inet6_n2p(const struct in6_addr *p)
{
	static char buf[BUFSIZ];

	return inet_ntop(AF_INET6, (const void *)p, buf, sizeof(buf));
}

static void
ifrtdump(int sig)
{

	ifdump(sig);
	rtdump(sig);
}

static void
ifdump(int sig)
{
	struct ifc *ifcp;
	FILE *dump;
	int nifc = 0;

	if (sig == 0)
		dump = stderr;
	else
		if ((dump = fopen(ROUTE6D_DUMP, "a")) == NULL)
			dump = stderr;

	fprintf(dump, "%s: Interface Table Dump\n", hms());
	TAILQ_FOREACH(ifcp, &ifc_head, ifc_next)
		nifc++;
	fprintf(dump, "  Number of interfaces: %d\n", nifc);

	fprintf(dump, "  advertising interfaces:\n");
	TAILQ_FOREACH(ifcp, &ifc_head, ifc_next) {
		if ((ifcp->ifc_flags & IFF_UP) == 0)
			continue;
		if (iff_find(ifcp, IFIL_TYPE_N) != NULL)
			continue;
		ifdump0(dump, ifcp);
	}
	fprintf(dump, "\n");
	fprintf(dump, "  non-advertising interfaces:\n");
	TAILQ_FOREACH(ifcp, &ifc_head, ifc_next) {
		if ((ifcp->ifc_flags & IFF_UP) &&
		    (iff_find(ifcp, IFIL_TYPE_N) == NULL))
			continue;
		ifdump0(dump, ifcp);
	}
	fprintf(dump, "\n");
	if (dump != stderr)
		fclose(dump);
}

static void
ifdump0(FILE *dump, const struct ifc *ifcp)
{
	struct ifac *ifac;
	struct iff *iffp;
	char buf[BUFSIZ];
	const char *ft;
	int addr;

	fprintf(dump, "    %s: index(%d) flags(%s) addr(%s) mtu(%d) metric(%d)\n",
		ifcp->ifc_name, ifcp->ifc_index, ifflags(ifcp->ifc_flags),
		inet6_n2p(&ifcp->ifc_mylladdr),
		ifcp->ifc_mtu, ifcp->ifc_metric);
	TAILQ_FOREACH(ifac, &ifcp->ifc_ifac_head, ifac_next) {
		if (ifcp->ifc_flags & IFF_POINTOPOINT) {
			inet_ntop(AF_INET6, (void *)&ifac->ifac_raddr,
				buf, sizeof(buf));
			fprintf(dump, "\t%s/%d -- %s\n",
				inet6_n2p(&ifac->ifac_addr),
				ifac->ifac_plen, buf);
		} else {
			fprintf(dump, "\t%s/%d\n",
				inet6_n2p(&ifac->ifac_addr),
				ifac->ifac_plen);
		}
	}

	fprintf(dump, "\tFilter:\n");
	TAILQ_FOREACH(iffp, &ifcp->ifc_iff_head, iff_next) {
		addr = 0;
		switch (iffp->iff_type) {
		case IFIL_TYPE_A:
			ft = "Aggregate"; addr++; break;
		case IFIL_TYPE_N:
			ft = "No-use"; break;
		case IFIL_TYPE_O:
			ft = "Advertise-only"; addr++; break;
		case IFIL_TYPE_T:
			ft = "Default-only"; break;
		case IFIL_TYPE_L:
			ft = "Listen-only"; addr++; break;
		default:
			snprintf(buf, sizeof(buf), "Unknown-%c", iffp->iff_type);
			ft = buf;
			addr++;
			break;
		}
		fprintf(dump, "\t\t%s", ft);
		if (addr)
			fprintf(dump, "(%s/%d)", inet6_n2p(&iffp->iff_addr),
				iffp->iff_plen);
		fprintf(dump, "\n");
	}
	fprintf(dump, "\n");
}

static void
rtdump(int sig)
{
	struct	riprt *rrt;
	char	buf[BUFSIZ];
	FILE	*dump;
	time_t	t, age;

	if (sig == 0)
		dump = stderr;
	else
		if ((dump = fopen(ROUTE6D_DUMP, "a")) == NULL)
			dump = stderr;

	t = time(NULL);
	fprintf(dump, "\n%s: Routing Table Dump\n", hms());
	TAILQ_FOREACH(rrt, &riprt_head, rrt_next) {
		if (rrt->rrt_t == 0)
			age = 0;
		else
			age = t - rrt->rrt_t;
		inet_ntop(AF_INET6, (void *)&rrt->rrt_info.rip6_dest,
			buf, sizeof(buf));
		fprintf(dump, "    %s/%d if(%d:%s) gw(%s) [%d] age(%ld)",
			buf, rrt->rrt_info.rip6_plen, rrt->rrt_index,
			index2ifc[rrt->rrt_index]->ifc_name,
			inet6_n2p(&rrt->rrt_gw),
			rrt->rrt_info.rip6_metric, (long)age);
		if (rrt->rrt_info.rip6_tag) {
			fprintf(dump, " tag(0x%04x)",
				ntohs(rrt->rrt_info.rip6_tag) & 0xffff);
		}
		if (rrt->rrt_rflags & RRTF_NH_NOT_LLADDR)
			fprintf(dump, " NOT-LL");
		if (rrt->rrt_rflags & RRTF_NOADVERTISE)
			fprintf(dump, " NO-ADV");
		fprintf(dump, "\n");
	}
	fprintf(dump, "\n");
	if (dump != stderr)
		fclose(dump);
}

/*
 * Parse the -A (and -O) options and put corresponding filter object to the
 * specified interface structures.  Each of the -A/O option has the following
 * syntax:	-A 5f09:c400::/32,ef0,ef1  (aggregate)
 * 		-O 5f09:c400::/32,ef0,ef1  (only when match)
 */
static void
filterconfig(void)
{
	int i;
	char *p, *ap, *iflp, *ifname, *ep;
	struct iff iff, *iffp;
	struct ifc *ifcp;
	struct riprt *rrt;
#if 0
	struct in6_addr gw;
#endif
	u_long plen;

	for (i = 0; i < nfilter; i++) {
		ap = filter[i];
		iflp = NULL;
		iffp = &iff;
		memset(iffp, 0, sizeof(*iffp));
		if (filtertype[i] == 'N' || filtertype[i] == 'T') {
			iflp = ap;
			goto ifonly;
		}
		if ((p = strchr(ap, ',')) != NULL) {
			*p++ = '\0';
			iflp = p;
		}
		if ((p = strchr(ap, '/')) == NULL) {
			fatal("no prefixlen specified for '%s'", ap);
			/*NOTREACHED*/
		}
		*p++ = '\0';
		if (inet_pton(AF_INET6, ap, &iffp->iff_addr) != 1) {
			fatal("invalid prefix specified for '%s'", ap);
			/*NOTREACHED*/
		}
		errno = 0;
		ep = NULL;
		plen = strtoul(p, &ep, 10);
		if (errno || !*p || *ep || plen > sizeof(iffp->iff_addr) * 8) {
			fatal("invalid prefix length specified for '%s'", ap);
			/*NOTREACHED*/
		}
		iffp->iff_plen = plen;
		applyplen(&iffp->iff_addr, iffp->iff_plen);
ifonly:
		iffp->iff_type = filtertype[i];
		if (iflp == NULL || *iflp == '\0') {
			fatal("no interface specified for '%s'", ap);
			/*NOTREACHED*/
		}
		/* parse the interface listing portion */
		while (iflp) {
			ifname = iflp;
			if ((iflp = strchr(iflp, ',')) != NULL)
				*iflp++ = '\0';

			TAILQ_FOREACH(ifcp, &ifc_head, ifc_next) {
				if (fnmatch(ifname, ifcp->ifc_name, 0) != 0)
					continue;

				iffp = malloc(sizeof(*iffp));
				if (iffp == NULL) {
					fatal("malloc of iff");
					/*NOTREACHED*/
				}
				memcpy(iffp, &iff, sizeof(*iffp));
#if 0
				syslog(LOG_INFO, "Add filter: type %d, ifname %s.", iffp->iff_type, ifname);
#endif
				TAILQ_INSERT_HEAD(&ifcp->ifc_iff_head, iffp, iff_next);
			}
		}

		/*
		 * -A: aggregate configuration.
		 */
		if (filtertype[i] != IFIL_TYPE_A)
			continue;
		/* put the aggregate to the kernel routing table */
		rrt = (struct riprt *)malloc(sizeof(struct riprt));
		if (rrt == NULL) {
			fatal("malloc: rrt");
			/*NOTREACHED*/
		}
		memset(rrt, 0, sizeof(struct riprt));
		rrt->rrt_info.rip6_dest = iff.iff_addr;
		rrt->rrt_info.rip6_plen = iff.iff_plen;
		rrt->rrt_info.rip6_metric = 1;
		rrt->rrt_info.rip6_tag = htons(routetag & 0xffff);
		rrt->rrt_gw = in6addr_loopback;
		rrt->rrt_flags = RTF_UP | RTF_REJECT;
		rrt->rrt_rflags = RRTF_AGGREGATE;
		rrt->rrt_t = 0;
		rrt->rrt_index = loopifcp->ifc_index;
#if 0
		if (getroute(&rrt->rrt_info, &gw)) {
#if 0
			/*
			 * When the address has already been registered in the
			 * kernel routing table, it should be removed
			 */
			delroute(&rrt->rrt_info, &gw);
#else
			/* it is safer behavior */
			errno = EINVAL;
			fatal("%s/%u already in routing table, "
			    "cannot aggregate",
			    inet6_n2p(&rrt->rrt_info.rip6_dest),
			    rrt->rrt_info.rip6_plen);
			/*NOTREACHED*/
#endif
		}
#endif
		/* Put the route to the list */
		TAILQ_INSERT_HEAD(&riprt_head, rrt, rrt_next);
		trace(1, "Aggregate: %s/%d for %s\n",
			inet6_n2p(&iff.iff_addr), iff.iff_plen,
			loopifcp->ifc_name);
		/* Add this route to the kernel */
		if (nflag) 	/* do not modify kernel routing table */
			continue;
		addroute(rrt, &in6addr_loopback, loopifcp);
	}
}

/***************** utility functions *****************/

/*
 * Returns a pointer to ifac whose address and prefix length matches
 * with the address and prefix length specified in the arguments.
 */
static struct ifac *
ifa_match(const struct ifc *ifcp,
	const struct in6_addr *ia,
	int plen)
{
	struct ifac *ifac;

	TAILQ_FOREACH(ifac, &ifcp->ifc_ifac_head, ifac_next) {
		if (IN6_ARE_ADDR_EQUAL(&ifac->ifac_addr, ia) &&
		    ifac->ifac_plen == plen)
			break;
	}

	return (ifac);
}

/*
 * Return a pointer to riprt structure whose address and prefix length
 * matches with the address and prefix length found in the argument.
 * Note: This is not a rtalloc().  Therefore exact match is necessary.
 */
static struct riprt *
rtsearch(struct netinfo6 *np)
{
	struct	riprt	*rrt;

	TAILQ_FOREACH(rrt, &riprt_head, rrt_next) {
		if (rrt->rrt_info.rip6_plen == np->rip6_plen &&
		    IN6_ARE_ADDR_EQUAL(&rrt->rrt_info.rip6_dest,
				       &np->rip6_dest))
			break;
	}

	return (rrt);
}

static int
sin6mask2len(const struct sockaddr_in6 *sin6)
{

	return mask2len(&sin6->sin6_addr,
	    sin6->sin6_len - offsetof(struct sockaddr_in6, sin6_addr));
}

static int
mask2len(const struct in6_addr *addr, int lenlim)
{
	int i = 0, j;
	const u_char *p = (const u_char *)addr;

	for (j = 0; j < lenlim; j++, p++) {
		if (*p != 0xff)
			break;
		i += 8;
	}
	if (j < lenlim) {
		switch (*p) {
#define	MASKLEN(m, l)	case m: do { i += l; break; } while (0)
		MASKLEN(0xfe, 7); break;
		MASKLEN(0xfc, 6); break;
		MASKLEN(0xf8, 5); break;
		MASKLEN(0xf0, 4); break;
		MASKLEN(0xe0, 3); break;
		MASKLEN(0xc0, 2); break;
		MASKLEN(0x80, 1); break;
#undef	MASKLEN
		}
	}
	return i;
}

static const u_char plent[8] = {
	0x00, 0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe
};

static void
applyplen(struct in6_addr *ia, int plen)
{
	u_char	*p;
	int	i;

	p = ia->s6_addr;
	for (i = 0; i < 16; i++) {
		if (plen <= 0)
			*p = 0;
		else if (plen < 8)
			*p &= plent[plen];
		p++, plen -= 8;
	}
}

static const int pl2m[9] = {
	0x00, 0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe, 0xff
};

static struct in6_addr *
plen2mask(int n)
{
	static struct in6_addr ia;
	u_char	*p;
	int	i;

	memset(&ia, 0, sizeof(struct in6_addr));
	p = (u_char *)&ia;
	for (i = 0; i < 16; i++, p++, n -= 8) {
		if (n >= 8) {
			*p = 0xff;
			continue;
		}
		*p = pl2m[n];
		break;
	}
	return &ia;
}

static char *
allocopy(char *p)
{
	int len = strlen(p) + 1;
	char *q = (char *)malloc(len);

	if (!q) {
		fatal("malloc");
		/*NOTREACHED*/
	}

	strlcpy(q, p, len);
	return q;
}

static char *
hms(void)
{
	static char buf[BUFSIZ];
	time_t t;
	struct	tm *tm;

	t = time(NULL);
	if ((tm = localtime(&t)) == 0) {
		fatal("localtime");
		/*NOTREACHED*/
	}
	snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm->tm_hour, tm->tm_min,
	    tm->tm_sec);
	return buf;
}

#define	RIPRANDDEV	1.0	/* 30 +- 15, max - min = 30 */

static int
ripinterval(int timer)
{
	double r = rand();

	interval = (int)(timer + timer * RIPRANDDEV * (r / RAND_MAX - 0.5));
	nextalarm = time(NULL) + interval;
	return interval;
}

#if 0
static time_t
ripsuptrig(void)
{
	time_t t;

	double r = rand();
	t  = (int)(RIP_TRIG_INT6_MIN +
		(RIP_TRIG_INT6_MAX - RIP_TRIG_INT6_MIN) * (r / RAND_MAX));
	sup_trig_update = time(NULL) + t;
	return t;
}
#endif

static void
fatal(const char *fmt, ...)
{
	va_list ap;
	char buf[1024];

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	perror(buf);
	if (errno)
		syslog(LOG_ERR, "%s: %s", buf, strerror(errno));
	else
		syslog(LOG_ERR, "%s", buf);
	rtdexit();
}

static void
tracet(int level, const char *fmt, ...)
{
	va_list ap;

	if (level <= dflag) {
		va_start(ap, fmt);
		fprintf(stderr, "%s: ", hms());
		vfprintf(stderr, fmt, ap);
		va_end(ap);
	}
	if (dflag) {
		va_start(ap, fmt);
		if (level > 0)
			vsyslog(LOG_DEBUG, fmt, ap);
		else
			vsyslog(LOG_WARNING, fmt, ap);
		va_end(ap);
	}
}

static void
trace(int level, const char *fmt, ...)
{
	va_list ap;

	if (level <= dflag) {
		va_start(ap, fmt);
		vfprintf(stderr, fmt, ap);
		va_end(ap);
	}
	if (dflag) {
		va_start(ap, fmt);
		if (level > 0)
			vsyslog(LOG_DEBUG, fmt, ap);
		else
			vsyslog(LOG_WARNING, fmt, ap);
		va_end(ap);
	}
}

static struct ifc *
ifc_find(char *name)
{
	struct ifc *ifcp;

	TAILQ_FOREACH(ifcp, &ifc_head, ifc_next) {
		if (strcmp(name, ifcp->ifc_name) == 0)
			break;
	}
	return (ifcp);
}

static struct iff *
iff_find(struct ifc *ifcp, int type)
{
	struct iff *iffp;

	TAILQ_FOREACH(iffp, &ifcp->ifc_iff_head, iff_next) {
		if (type == IFIL_TYPE_ANY ||
		    type == iffp->iff_type)
			break;
	}

	return (iffp);
}

static void
setindex2ifc(int idx, struct ifc *ifcp)
{
	int n, nsize;
	struct ifc **p;

	if (!index2ifc) {
		nindex2ifc = 5;	/*initial guess*/
		index2ifc = (struct ifc **)
			malloc(sizeof(*index2ifc) * nindex2ifc);
		if (index2ifc == NULL) {
			fatal("malloc");
			/*NOTREACHED*/
		}
		memset(index2ifc, 0, sizeof(*index2ifc) * nindex2ifc);
	}
	n = nindex2ifc;
	for (nsize = nindex2ifc; nsize <= idx; nsize *= 2)
		;
	if (n != nsize) {
		p = (struct ifc **)realloc(index2ifc,
		    sizeof(*index2ifc) * nsize);
		if (p == NULL) {
			fatal("realloc");
			/*NOTREACHED*/
		}
		memset(p + n, 0, sizeof(*index2ifc) * (nindex2ifc - n));
		index2ifc = p;
		nindex2ifc = nsize;
	}
	index2ifc[idx] = ifcp;
}
