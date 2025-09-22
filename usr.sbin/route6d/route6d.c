/*	$OpenBSD: route6d.c,v 1.101 2024/05/21 05:00:48 jsg Exp $	*/
/*	$KAME: route6d.c,v 1.111 2006/10/25 06:38:13 jinmei Exp $	*/

/*
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

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/uio.h>

#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netinet/ip6.h>
#include <netinet/udp.h>
#include <netinet6/in6_var.h>

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "route6d.h"
#include "log.h"

#define	MAXFILTER	40

#ifdef	DEBUG
#define	INIT_INTERVAL6	6
#else
#define	INIT_INTERVAL6	10	/* Wait to submit a initial riprequest */
#endif

/* alignment constraint for routing socket */
#define ROUNDUP(a) \
	((a) > 0 ? (1 + (((a) - 1) | (sizeof(long) - 1))) : sizeof(long))
#define ADVANCE(x, n) (x += ROUNDUP((n)->sa_len))

/*
 * Following two macros are highly depending on KAME Release
 */
#define	IN6_LINKLOCAL_IFINDEX(addr) \
	((addr).s6_addr[2] << 8 | (addr).s6_addr[3])

#define	SET_IN6_LINKLOCAL_IFINDEX(addr, index) \
	do { \
		(addr).s6_addr[2] = ((index) >> 8) & 0xff; \
		(addr).s6_addr[3] = (index) & 0xff; \
	} while (0)

struct	ifc {			/* Configuration of an interface */
	char	*ifc_name;			/* if name */
	struct	ifc *ifc_next;
	int	ifc_index;			/* if index */
	int	ifc_mtu;			/* if mtu */
	int	ifc_metric;			/* if metric */
	u_int	ifc_flags;			/* flags */
	short	ifc_cflags;			/* IFC_XXX */
	struct	in6_addr ifc_mylladdr;		/* my link-local address */
	struct	sockaddr_in6 ifc_ripsin;	/* rip multicast address */
	struct	iff *ifc_filter;		/* filter structure */
	struct	ifac *ifc_addr;			/* list of AF_INET6 addresses */
	int	ifc_joined;			/* joined to ff02::9 */
};

struct	ifac {			/* Address associated to an interface */ 
	struct	ifc *ifa_conf;		/* back pointer */
	struct	ifac *ifa_next;
	struct	in6_addr ifa_addr;	/* address */
	struct	in6_addr ifa_raddr;	/* remote address, valid in p2p */
	int	ifa_plen;		/* prefix length */
};

struct	iff {
	int	iff_type;
	struct	in6_addr iff_addr;
	int	iff_plen;
	struct	iff *iff_next;
};

struct	ifc *ifc;
int	nifc;		/* number of valid ifc's */
struct	ifc **index2ifc;
int	nindex2ifc;
struct	ifc *loopifcp = NULL;	/* pointing to loopback */
struct	pollfd pfd[2];
int	rtsock;		/* the routing socket */
int	ripsock;	/* socket to send/receive RIP datagram */

struct	rip6 *ripbuf;	/* packet buffer for sending */

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

struct	riprt {
	struct	riprt *rrt_next;	/* next destination */
	struct	netinfo6 rrt_info;	/* network info */
	struct	in6_addr rrt_gw;	/* gateway */
	u_long	rrt_flags;		/* kernel routing table flags */
	u_long	rrt_rflags;		/* route6d routing table flags */
	time_t	rrt_t;			/* when the route validated */
	int	rrt_index;		/* ifindex from which this route got */
};

struct	riprt *riprt = 0;

int	dflag = 0;	/* debug flag */
int	qflag = 0;	/* quiet flag */
int	nflag = 0;	/* don't update kernel routing table */
int	aflag = 0;	/* age out even the statically defined routes */
int	hflag = 0;	/* don't split horizon */
int	lflag = 0;	/* exchange site local routes */
int	sflag = 0;	/* announce static routes w/ split horizon */
int	Sflag = 0;	/* announce static routes to every interface */
int	uflag = 0;	/* always log route updates (additions/deletions) */
unsigned long routetag = 0;	/* route tag attached on originating case */

char	*filter[MAXFILTER];
int	filtertype[MAXFILTER];
int	nfilter = 0;

pid_t	pid;

struct	sockaddr_storage ripsin;

time_t	nextalarm = 0;
time_t	sup_trig_update = 0;

static	int	seq = 0;

volatile sig_atomic_t seenalrm;
volatile sig_atomic_t seenquit;
volatile sig_atomic_t seenusr1;

#define	RRTF_AGGREGATE		0x08000000
#define	RRTF_NOADVERTISE	0x10000000
#define	RRTF_NH_NOT_LLADDR	0x20000000
#define RRTF_SENDANYWAY		0x40000000
#define	RRTF_CHANGED		0x80000000

void sighandler(int);
void ripalarm(void);
void riprecv(void);
void ripsend(struct ifc *, struct sockaddr_in6 *, int);
int out_filter(struct riprt *, struct ifc *);
void init(void);
void ifconfig(void);
void ifconfig1(const char *, const struct sockaddr *, struct ifc *, int);
void rtrecv(void);
int rt_del(const struct sockaddr_in6 *, const struct sockaddr_in6 *,
    const struct sockaddr_in6 *);
int rt_deladdr(struct ifc *, const struct sockaddr_in6 *,
    const struct sockaddr_in6 *);
void filterconfig(void);
int getifmtu(int);
const char *rttypes(struct rt_msghdr *);
const char *rtflags(struct rt_msghdr *);
const char *ifflags(int);
int ifrt(struct ifc *, int);
void ifrt_p2p(struct ifc *, int);
void applyplen(struct in6_addr *, int);
void ifrtdump(int);
void ifdump(int);
void ifdump0(const struct ifc *);
void rtdump(int);
void rt_entry(struct rt_msghdr *, int);
__dead void rtdexit(void);
void riprequest(struct ifc *, struct netinfo6 *, int, struct sockaddr_in6 *);
void ripflush(struct ifc *, struct sockaddr_in6 *);
void sendrequest(struct ifc *);
int sin6mask2len(const struct sockaddr_in6 *);
int mask2len(const struct in6_addr *, int);
int sendpacket(struct sockaddr_in6 *, int);
int addroute(struct riprt *, const struct in6_addr *, struct ifc *);
int delroute(struct netinfo6 *, struct in6_addr *);
struct in6_addr *getroute(struct netinfo6 *, struct in6_addr *);
void krtread(int);
int tobeadv(struct riprt *, struct ifc *);
char *xstrdup(const char *);
const char *hms(void);
const char *inet6_n2p(const struct in6_addr *);
struct ifac *ifa_match(const struct ifc *, const struct in6_addr *, int);
struct in6_addr *plen2mask(int);
struct riprt *rtsearch(struct netinfo6 *, struct riprt **);
int ripinterval(int);
time_t ripsuptrig(void);
unsigned int if_maxindex(void);
struct ifc *ifc_find(char *);
struct iff *iff_find(struct ifc *, int);
void setindex2ifc(int, struct ifc *);

int
main(int argc, char *argv[])
{
	int	ch;
	int	error = 0;
	struct	ifc *ifcp;
	sigset_t mask, omask;
	char *ep;

	log_init(1); /* log to stderr until daemonized */

	while ((ch = getopt(argc, argv, "A:N:O:T:L:t:adDhlnqsSu")) != -1) {
		switch (ch) {
		case 'A':
		case 'N':
		case 'O':
		case 'T':
		case 'L':
			if (nfilter >= MAXFILTER) {
				fatalx("Exceeds MAXFILTER");
				/*NOTREACHED*/
			}
			filtertype[nfilter] = ch;
			filter[nfilter++] = xstrdup(optarg);
			break;
		case 't':
			ep = NULL;
			routetag = strtoul(optarg, &ep, 0);
			if (!ep || *ep != '\0' || (routetag & ~0xffff) != 0) {
				fatalx("invalid route tag");
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
		FLAG('u', uflag, 1); break;
#undef	FLAG
		default:
			fatalx("Invalid option specified, terminating");
			/*NOTREACHED*/
		}
	}
	argc -= optind;
	argv += optind;
	if (argc > 0) {
		fatalx("bogus extra arguments");
		/*NOTREACHED*/
	}

	if (geteuid()) {
		nflag = 1;
		log_warn("No kernel update is allowed");
	}

	if (dflag == 0) {
		if (daemon(0, 0) == -1) {
			fatal("daemon");
			/*NOTREACHED*/
		}
	}

	log_init(dflag);

	pid = getpid();

	if ((ripbuf = calloc(RIP6_MAXMTU, 1)) == NULL)
		fatal(NULL);
	ripbuf->rip6_cmd = RIP6_RESPONSE;
	ripbuf->rip6_vers = RIP6_VERSION;
	ripbuf->rip6_res1[0] = 0;
	ripbuf->rip6_res1[1] = 0;

	init();

	if (pledge("stdio inet route mcast", NULL) == -1)
		fatal("pledge");

	ifconfig();

	for (ifcp = ifc; ifcp; ifcp = ifcp->ifc_next) {
		if (ifcp->ifc_index < 0) {
			log_warn(
"No ifindex found at %s (no link-local address?)",
				ifcp->ifc_name);
			error++;
		}
	}
	if (error)
		exit(1);
	if (loopifcp == NULL) {
		fatalx("No loopback found");
		/*NOTREACHED*/
	}
	for (ifcp = ifc; ifcp; ifcp = ifcp->ifc_next)
		ifrt(ifcp, 0);
	filterconfig();
	krtread(0);
	if (dflag)
		ifrtdump(0);

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

	for (ifcp = ifc; ifcp; ifcp = ifcp->ifc_next) {
		if (iff_find(ifcp, 'N'))
			continue;
		if (ifcp->ifc_index > 0 && (ifcp->ifc_flags & IFF_UP))
			sendrequest(ifcp);
	}

	log_info("**** Started ****");
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

		switch (poll(pfd, 2, INFTIM))
		{
		case -1:
			if (errno != EINTR) {
				fatal("poll");
				/*NOTREACHED*/
			}
			continue;
		case 0:
			continue;
		default:
			if (pfd[0].revents & POLLIN) {
				sigprocmask(SIG_BLOCK, &mask, &omask);
				riprecv();
				sigprocmask(SIG_SETMASK, &omask, NULL);
			}
			if (pfd[1].revents & POLLIN) {
				sigprocmask(SIG_BLOCK, &mask, &omask);
				rtrecv();
				sigprocmask(SIG_SETMASK, &omask, NULL);
			}
		}
	}
}

void
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
void
rtdexit(void)
{
	struct	riprt *rrt;

	alarm(0);
	for (rrt = riprt; rrt; rrt = rrt->rrt_next) {
		if (rrt->rrt_rflags & RRTF_AGGREGATE) {
			delroute(&rrt->rrt_info, &rrt->rrt_gw);
		}
	}
	close(ripsock);
	close(rtsock);
	log_info("**** Terminated ****");
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
void
ripalarm(void)
{
	struct	ifc *ifcp;
	struct	riprt *rrt, *rrt_prev, *rrt_next;
	time_t	t_lifetime, t_holddown;

	/* age the RIP routes */
	rrt_prev = 0;
	t_lifetime = time(NULL) - RIP_LIFETIME;
	t_holddown = t_lifetime - RIP_HOLDDOWN;
	for (rrt = riprt; rrt; rrt = rrt_next) {
		rrt_next = rrt->rrt_next;

		if (rrt->rrt_t == 0) {
			rrt_prev = rrt;
			continue;
		}
		if (rrt->rrt_t < t_holddown) {
			if (rrt_prev) {
				rrt_prev->rrt_next = rrt->rrt_next;
			} else {
				riprt = rrt->rrt_next;
			}
			delroute(&rrt->rrt_info, &rrt->rrt_gw);
			free(rrt);
			continue;
		}
		if (rrt->rrt_t < t_lifetime)
			rrt->rrt_info.rip6_metric = HOPCNT_INFINITY6;
		rrt_prev = rrt;
	}
	/* Supply updates */
	for (ifcp = ifc; ifcp; ifcp = ifcp->ifc_next) {
		if (ifcp->ifc_index > 0 && (ifcp->ifc_flags & IFF_UP))
			ripsend(ifcp, &ifcp->ifc_ripsin, 0);
	}
	alarm(ripinterval(SUPPLY_INTERVAL6));
}

void
init(void)
{
	int	i, error;
	const int int0 = 0, int1 = 1, int255 = 255;
	struct	addrinfo hints, *res;
	char	port[NI_MAXSERV];

	ifc = (struct ifc *)NULL;
	nifc = 0;
	nindex2ifc = 0;	/*initial guess*/
	index2ifc = NULL;
	snprintf(port, sizeof(port), "%u", RIP6_PORT);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_INET6;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE;
	error = getaddrinfo(NULL, port, &hints, &res);
	if (error) {
		fatalx(gai_strerror(error));
		/*NOTREACHED*/
	}
	if (res->ai_next) {
		fatalx(":: resolved to multiple address");
		/*NOTREACHED*/
	}

	ripsock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (ripsock == -1) {
		fatal("rip socket");
		/*NOTREACHED*/
	}
	if (setsockopt(ripsock, IPPROTO_IPV6, IPV6_V6ONLY,
	    &int1, sizeof(int1)) == -1) {
		fatal("rip IPV6_V6ONLY");
		/*NOTREACHED*/
	}
	if (bind(ripsock, res->ai_addr, res->ai_addrlen) == -1) {
		fatal("rip bind");
		/*NOTREACHED*/
	}
	if (setsockopt(ripsock, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
	    &int255, sizeof(int255)) == -1) {
		fatal("rip IPV6_MULTICAST_HOPS");
		/*NOTREACHED*/
	}
	if (setsockopt(ripsock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
	    &int0, sizeof(int0)) == -1) {
		fatal("rip IPV6_MULTICAST_LOOP");
		/*NOTREACHED*/
	}

	i = 1;
	if (setsockopt(ripsock, IPPROTO_IPV6, IPV6_RECVPKTINFO, &i,
	    sizeof(i)) == -1) {
		fatal("rip IPV6_RECVPKTINFO");
		/*NOTREACHED*/
	}

	if (setsockopt(ripsock, IPPROTO_IPV6, IPV6_RECVHOPLIMIT,
	    &int1, sizeof(int1)) == -1) {
		fatal("rip IPV6_RECVHOPLIMIT");
		/*NOTREACHED*/
	}

	freeaddrinfo(res);
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_INET6;
	hints.ai_socktype = SOCK_DGRAM;
	error = getaddrinfo(RIP6_DEST, port, &hints, &res);
	if (error) {
		fatalx(gai_strerror(error));
		/*NOTREACHED*/
	}
	if (res->ai_next) {
		fatalx(RIP6_DEST " resolved to multiple address");
		/*NOTREACHED*/
	}
	memcpy(&ripsin, res->ai_addr, res->ai_addrlen);
	freeaddrinfo(res);

	pfd[0].fd = ripsock;
	pfd[0].events = POLLIN;

	if (nflag == 0) {
		if ((rtsock = socket(AF_ROUTE, SOCK_RAW, 0)) == -1) {
			fatal("route socket");
			/*NOTREACHED*/
		}
		pfd[1].fd = rtsock;
		pfd[1].events = POLLIN;
	} else
		pfd[1].fd = -1;

}

#define	RIPSIZE(n) \
	(sizeof(struct rip6) + ((n)-1) * sizeof(struct netinfo6))

/*
 * ripflush flushes the rip datagram stored in the rip buffer
 */
static int nrt;
static struct netinfo6 *np;

void
ripflush(struct ifc *ifcp, struct sockaddr_in6 *sin6)
{
	int i;
	int error;

	if (ifcp)
		log_debug("Send(%s): info(%d) to %s.%d",
			ifcp->ifc_name, nrt,
			inet6_n2p(&sin6->sin6_addr), ntohs(sin6->sin6_port));
	else
		log_debug("Send: info(%d) to %s.%d",
			nrt, inet6_n2p(&sin6->sin6_addr), ntohs(sin6->sin6_port));
	if (dflag >= 2) {
		np = ripbuf->rip6_nets;
		for (i = 0; i < nrt; i++, np++) {
			if (np->rip6_metric == NEXTHOP_METRIC) {
				if (IN6_IS_ADDR_UNSPECIFIED(&np->rip6_dest))
					log_enqueue("    NextHop reset");
				else {
					log_enqueue("    NextHop %s",
						inet6_n2p(&np->rip6_dest));
				}
			} else {
				log_enqueue("    %s/%d[%d]",
					inet6_n2p(&np->rip6_dest),
					np->rip6_plen, np->rip6_metric);
			}
			if (np->rip6_tag) {
				log_enqueue("  tag=0x%04x",
					ntohs(np->rip6_tag) & 0xffff);
			}
			log_debug("");
		}
	}
	error = sendpacket(sin6, RIPSIZE(nrt));
	if (error == EAFNOSUPPORT) {
		/* Protocol not supported */
		log_debug("Could not send info to %s (%s): "
			"set IFF_UP to 0",
			ifcp->ifc_name, inet6_n2p(&ifcp->ifc_ripsin.sin6_addr));
		ifcp->ifc_flags &= ~IFF_UP;	/* As if down for AF_INET6 */
	}
	nrt = 0; np = ripbuf->rip6_nets;
}

/*
 * Generate RIP6_RESPONSE packets and send them.
 */
void
ripsend(struct ifc *ifcp, struct sockaddr_in6 *sin6, int flag)
{
	struct	riprt *rrt;
	struct	in6_addr *nh;	/* next hop */
	int	maxrte;

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
		nrt = 0; np = ripbuf->rip6_nets; nh = NULL;
		for (rrt = riprt; rrt; rrt = rrt->rrt_next) {
			if (rrt->rrt_rflags & RRTF_NOADVERTISE)
				continue;
			/* Put the route to the buffer */
			*np = rrt->rrt_info;
			np++; nrt++;
			if (nrt == maxrte) {
				ripflush(NULL, sin6);
				nh = NULL;
			}
		}
		if (nrt)	/* Send last packet */
			ripflush(NULL, sin6);
		return;
	}

	if ((flag & RRTF_SENDANYWAY) == 0 &&
	    (qflag || (ifcp->ifc_flags & IFF_LOOPBACK)))
		return;

	/* -N: no use */
	if (iff_find(ifcp, 'N') != NULL)
		return;

	/* -T: generate default route only */
	if (iff_find(ifcp, 'T') != NULL) {
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
		ripflush(ifcp, sin6);
		return;
	}

	maxrte = (ifcp->ifc_mtu - sizeof(struct ip6_hdr) - 
			sizeof(struct udphdr) - 
			sizeof(struct rip6) + sizeof(struct netinfo6)) /
			sizeof(struct netinfo6);

	nrt = 0; np = ripbuf->rip6_nets; nh = NULL;
	for (rrt = riprt; rrt; rrt = rrt->rrt_next) {
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
				if (nrt == maxrte - 2)
					ripflush(ifcp, sin6);
				np->rip6_dest = rrt->rrt_gw;
				if (IN6_IS_ADDR_LINKLOCAL(&np->rip6_dest))
					SET_IN6_LINKLOCAL_IFINDEX(np->rip6_dest, 0);
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
			if (nrt == maxrte - 2)
				ripflush(ifcp, sin6);
			memset(np, 0, sizeof(struct netinfo6));
			np->rip6_metric = NEXTHOP_METRIC;
			nh = NULL;
			np++; nrt++;
		}

		/* Put the route to the buffer */
		*np = rrt->rrt_info;
		np++; nrt++;
		if (nrt == maxrte) {
			ripflush(ifcp, sin6);
			nh = NULL;
		}
	}
	if (nrt)	/* Send last packet */
		ripflush(ifcp, sin6);
}

/*
 * outbound filter logic, per-route/interface.
 */
int
out_filter(struct riprt *rrt, struct ifc *ifcp)
{
	struct iff *iffp;
	struct in6_addr ia;
	int ok;

	/*
	 * -A: filter out less specific routes, if we have aggregated
	 * route configured.
	 */ 
	for (iffp = ifcp->ifc_filter; iffp; iffp = iffp->iff_next) {
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
		for (iffp = ifcp->ifc_filter; iffp; iffp = iffp->iff_next) {
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
	if (iff_find(ifcp, 'O')) {
		ok = 0;
		for (iffp = ifcp->ifc_filter; iffp; iffp = iffp->iff_next) {
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
int
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
int
sendpacket(struct sockaddr_in6 *sin6, int len)
{
	struct msghdr m;
	struct cmsghdr *cm;
	struct iovec iov[2];
	union {
		struct cmsghdr hdr;
		u_char buf[CMSG_SPACE(sizeof(struct in6_pktinfo))];
	} cmsgbuf;
	struct in6_pktinfo *pi;
	int idx;
	struct sockaddr_in6 sincopy;

	/* do not overwrite the given sin */
	sincopy = *sin6;
	sin6 = &sincopy;

	if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr) ||
	    IN6_IS_ADDR_MULTICAST(&sin6->sin6_addr)) {
		/* XXX: do not mix the interface index and link index */
		idx = IN6_LINKLOCAL_IFINDEX(sin6->sin6_addr);
		SET_IN6_LINKLOCAL_IFINDEX(sin6->sin6_addr, 0);
		sin6->sin6_scope_id = idx;
	} else
		idx = 0;

	m.msg_name = (caddr_t)sin6;
	m.msg_namelen = sizeof(*sin6);
	iov[0].iov_base = (caddr_t)ripbuf;
	iov[0].iov_len = len;
	m.msg_iov = iov;
	m.msg_iovlen = 1;
	if (!idx) {
		m.msg_control = NULL;
		m.msg_controllen = 0;
	} else {
		memset(&cmsgbuf, 0, sizeof(cmsgbuf));
		m.msg_control = (caddr_t)&cmsgbuf.buf;
		m.msg_controllen = sizeof(cmsgbuf.buf);
		cm = CMSG_FIRSTHDR(&m);
		cm->cmsg_len = CMSG_LEN(sizeof(struct in6_pktinfo));
		cm->cmsg_level = IPPROTO_IPV6;
		cm->cmsg_type = IPV6_PKTINFO;
		pi = (struct in6_pktinfo *)CMSG_DATA(cm);
		memset(&pi->ipi6_addr, 0, sizeof(pi->ipi6_addr)); /*::*/
		pi->ipi6_ifindex = idx;
	}

	if (sendmsg(ripsock, &m, 0) == -1) {
		log_debug("sendmsg: %s", strerror(errno));
		return errno;
	}

	return 0;
}

/*
 * Receive and process RIP packets.  Update the routes/kernel forwarding
 * table if necessary.
 */
void
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
	union {
		struct cmsghdr hdr;
		u_char buf[CMSG_SPACE(sizeof(struct in6_pktinfo)) +
		    CMSG_SPACE(sizeof(int))];
	} cmsgbuf;
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
	m.msg_control = (caddr_t)&cmsgbuf.buf;
	m.msg_controllen = sizeof(cmsgbuf.buf);
	if ((len = recvmsg(ripsock, &m, 0)) == -1) {
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
				log_debug(
				    "invalid cmsg length for IPV6_PKTINFO");
				return;
			}
			pi = (struct in6_pktinfo *)(CMSG_DATA(cm));
			idx = pi->ipi6_ifindex;
			break;
		case IPV6_HOPLIMIT:
			if (cm->cmsg_len != CMSG_LEN(sizeof(int))) {
				log_debug(
				    "invalid cmsg length for IPV6_HOPLIMIT");
				return;
			}
			hlimp = (int *)CMSG_DATA(cm);
			break;
		}
	}
	if (idx && IN6_IS_ADDR_LINKLOCAL(&fsock.sin6_addr))
		SET_IN6_LINKLOCAL_IFINDEX(fsock.sin6_addr, idx);

	if (len < sizeof(struct rip6)) {
		log_debug("Packet too short");
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
		log_debug("IPv6 packet information cannot be retrieved");
		return;
	}

	nh = fsock.sin6_addr;
	nn = (len - sizeof(struct rip6) + sizeof(struct netinfo6)) /
		sizeof(struct netinfo6);
	rp = (struct rip6 *)buf;
	np = rp->rip6_nets;

	if (rp->rip6_vers != RIP6_VERSION) {
		log_debug("Incorrect RIP version %d", rp->rip6_vers);
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
		log_debug("Response from non-ll addr: %s",
		    inet6_n2p(&fsock.sin6_addr));
		return;		/* Ignore packets from non-link-local addr */
	}
	if (ntohs(fsock.sin6_port) != RIP6_PORT) {
		log_debug("Response from non-rip port from %s",
		    inet6_n2p(&fsock.sin6_addr));
		return;
	}
	if (IN6_IS_ADDR_MULTICAST(&pi->ipi6_addr) && *hlimp != 255) {
		log_debug(
		    "Response packet with a smaller hop limit (%d) from %s",
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
		log_debug(
		    "Response packet possibly from an off-link node: "
		    "from %s to %s hlim=%d",
		    inet6_n2p(&fsock.sin6_addr), inet6_n2p(&pi->ipi6_addr),
		    *hlimp);
		return;
	}

	idx = IN6_LINKLOCAL_IFINDEX(fsock.sin6_addr);
	ifcp = (idx < nindex2ifc) ? index2ifc[idx] : NULL;
	if (!ifcp) {
		log_debug("Packets to unknown interface index %d", idx);
		return;		/* Ignore it */
	}
	if (IN6_ARE_ADDR_EQUAL(&ifcp->ifc_mylladdr, &fsock.sin6_addr))
		return;		/* The packet is from me; ignore */
	if (rp->rip6_cmd != RIP6_RESPONSE) {
		log_debug("Invalid command %d", rp->rip6_cmd);
		return; 
	}

	/* -N: no use */
	if (iff_find(ifcp, 'N') != NULL)
		return;

	log_debug("Recv(%s): from %s.%d info(%zd)",
	    ifcp->ifc_name, inet6_n2p(&nh), ntohs(fsock.sin6_port), nn);

	t = time(NULL);
	t_half_lifetime = t - (RIP_LIFETIME/2);
	for (; nn; nn--, np++) {
		if (np->rip6_metric == NEXTHOP_METRIC) {
			/* modify neighbor address */
			if (IN6_IS_ADDR_LINKLOCAL(&np->rip6_dest)) {
				nh = np->rip6_dest;
				SET_IN6_LINKLOCAL_IFINDEX(nh, idx);
				log_debug("\tNexthop: %s", inet6_n2p(&nh));
			} else if (IN6_IS_ADDR_UNSPECIFIED(&np->rip6_dest)) {
				nh = fsock.sin6_addr;
				log_debug("\tNexthop: %s", inet6_n2p(&nh));
			} else {
				nh = fsock.sin6_addr;
				log_debug("\tInvalid Nexthop: %s",
				    inet6_n2p(&np->rip6_dest));
			}
			continue;
		}
		if (IN6_IS_ADDR_MULTICAST(&np->rip6_dest)) {
			log_debug("\tMulticast netinfo6: %s/%d [%d]",
				inet6_n2p(&np->rip6_dest),
				np->rip6_plen, np->rip6_metric);
			continue;
		}
		if (IN6_IS_ADDR_LOOPBACK(&np->rip6_dest)) {
			log_debug("\tLoopback netinfo6: %s/%d [%d]",
				inet6_n2p(&np->rip6_dest),
				np->rip6_plen, np->rip6_metric);
			continue;
		}
		if (IN6_IS_ADDR_LINKLOCAL(&np->rip6_dest)) {
			log_debug("\tLink Local netinfo6: %s/%d [%d]",
				inet6_n2p(&np->rip6_dest),
				np->rip6_plen, np->rip6_metric);
			continue;
		}
		/* may need to pass sitelocal prefix in some case, however*/
		if (IN6_IS_ADDR_SITELOCAL(&np->rip6_dest) && !lflag) {
			log_debug("\tSite Local netinfo6: %s/%d [%d]",
				inet6_n2p(&np->rip6_dest),
				np->rip6_plen, np->rip6_metric);
			continue;
		}
		if (dflag >= 2) {
			log_enqueue("\tnetinfo6: %s/%d [%d]",
			    inet6_n2p(&np->rip6_dest),
			    np->rip6_plen, np->rip6_metric);
			if (np->rip6_tag)
				log_enqueue("  tag=0x%04x",
				    ntohs(np->rip6_tag) & 0xffff);
			ia = np->rip6_dest;
			applyplen(&ia, np->rip6_plen);
			if (!IN6_ARE_ADDR_EQUAL(&ia, &np->rip6_dest))
				log_enqueue(" [junk outside prefix]");
		}

		/*
		 * -L: listen only if the prefix matches the configuration
		 */
		ok = 1;		/* if there's no L filter, it is ok */
		for (iffp = ifcp->ifc_filter; iffp; iffp = iffp->iff_next) {
			if (iffp->iff_type != 'L')
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
			if (dflag >= 2)
				log_debug("  (filtered)");
			continue;
		}

		if (dflag >= 2)
			log_debug("");

		np->rip6_metric++;
		np->rip6_metric += ifcp->ifc_metric;
		if (np->rip6_metric > HOPCNT_INFINITY6)
			np->rip6_metric = HOPCNT_INFINITY6;

		applyplen(&np->rip6_dest, np->rip6_plen);
		if ((rrt = rtsearch(np, NULL)) != NULL) {
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
			if ((rrt = calloc(1, sizeof(struct riprt))) == NULL) {
				fatal("calloc: struct riprt");
				/*NOTREACHED*/
			}
			nq = &rrt->rrt_info;

			rrt->rrt_index = ifcp->ifc_index;
			rrt->rrt_flags = RTF_UP|RTF_GATEWAY;
			rrt->rrt_gw = nh;
			*nq = *np;
			applyplen(&nq->rip6_dest, nq->rip6_plen);
			if (nq->rip6_plen == sizeof(struct in6_addr) * 8)
				rrt->rrt_flags |= RTF_HOST;

			/* Put the route to the list */
			rrt->rrt_next = riprt;
			riprt = rrt;
			/* Update routing table */
			addroute(rrt, &nh, ifcp);
			rrt->rrt_rflags |= RRTF_CHANGED;
			need_trigger = 1;
			rrt->rrt_t = t;
		}
	}
	/* XXX need to care the interval between triggered updates */
	if (need_trigger) {
		if (nextalarm > time(NULL) + RIP_TRIG_INT6_MAX) {
			for (ic = ifc; ic; ic = ic->ifc_next) {
				if (ifcp->ifc_index == ic->ifc_index)
					continue;
				if (ic->ifc_flags & IFF_UP)
					ripsend(ic, &ic->ifc_ripsin,
						RRTF_CHANGED);
			}
		}
		/* Reset the flag */
		for (rrt = riprt; rrt; rrt = rrt->rrt_next)
			rrt->rrt_rflags &= ~RRTF_CHANGED;
	}
}

/*
 * Send all routes request packet to the specified interface.
 */
void
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
	log_debug("Send rtdump Request to %s (%s)",
		ifcp->ifc_name, inet6_n2p(&ifcp->ifc_ripsin.sin6_addr));
	error = sendpacket(&ifcp->ifc_ripsin, RIPSIZE(1));
	if (error == EAFNOSUPPORT) {
		/* Protocol not supported */
		log_debug("Could not send rtdump Request to %s (%s): "
			"set IFF_UP to 0",
			ifcp->ifc_name, inet6_n2p(&ifcp->ifc_ripsin.sin6_addr));
		ifcp->ifc_flags &= ~IFF_UP;	/* As if down for AF_INET6 */
	}
	ripbuf->rip6_cmd = RIP6_RESPONSE;
}

/*
 * Process a RIP6_REQUEST packet.
 */
void
riprequest(struct ifc *ifcp, struct netinfo6 *np, int nn,
    struct sockaddr_in6 *sin6)
{
	int i;
	struct riprt *rrt;

	if (!(nn == 1 && IN6_IS_ADDR_UNSPECIFIED(&np->rip6_dest) &&
	      np->rip6_plen == 0 && np->rip6_metric == HOPCNT_INFINITY6)) {
		/* Specific response, don't split-horizon */
		log_debug("\tRIP Request");
		for (i = 0; i < nn; i++, np++) {
			rrt = rtsearch(np, NULL);
			if (rrt)
				np->rip6_metric = rrt->rrt_info.rip6_metric;
			else
				np->rip6_metric = HOPCNT_INFINITY6;
		}
		(void)sendpacket(sin6, RIPSIZE(nn));
		return;
	}
	/* Whole routing table dump */
	log_debug("\tRIP Request -- whole routing table");
	ripsend(ifcp, sin6, RRTF_SENDANYWAY);
}

/*
 * Get information of each interface.
 */
void
ifconfig(void)
{
	struct ifaddrs *ifap, *ifa;
	struct ifc *ifcp;
	struct ipv6_mreq mreq;
	int s;

	if ((s = socket(AF_INET6, SOCK_DGRAM, 0)) == -1) {
		fatal("socket");
		/*NOTREACHED*/
	}

	if (getifaddrs(&ifap) != 0) {
		fatal("getifaddrs");
		/*NOTREACHED*/
	}

	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL ||
		    ifa->ifa_addr->sa_family != AF_INET6)
			continue;
		ifcp = ifc_find(ifa->ifa_name);
		/* we are interested in multicast-capable interfaces */
		if ((ifa->ifa_flags & IFF_MULTICAST) == 0)
			continue;
		if (!ifcp) {
			/* new interface */
			if ((ifcp = calloc(1, sizeof(struct ifc))) == NULL) {
				fatal("calloc: struct ifc");
				/*NOTREACHED*/
			}
			ifcp->ifc_index = -1;
			ifcp->ifc_next = ifc;
			ifc = ifcp;
			nifc++;
			ifcp->ifc_name = xstrdup(ifa->ifa_name);
			ifcp->ifc_addr = 0;
			ifcp->ifc_filter = 0;
			ifcp->ifc_flags = ifa->ifa_flags;
			log_debug("newif %s <%s>", ifcp->ifc_name,
				ifflags(ifcp->ifc_flags));
			if (!strcmp(ifcp->ifc_name, LOOPBACK_IF))
				loopifcp = ifcp;
		} else {
			/* update flag, this may be up again */
			if (ifcp->ifc_flags != ifa->ifa_flags) {
				log_enqueue("%s: <%s> -> ", ifcp->ifc_name,
					ifflags(ifcp->ifc_flags));
				log_debug("<%s>", ifflags(ifa->ifa_flags));
				ifcp->ifc_cflags |= IFC_CHANGED;
			}
			ifcp->ifc_flags = ifa->ifa_flags;
		}
		ifconfig1(ifa->ifa_name, ifa->ifa_addr, ifcp, s);
		if ((ifcp->ifc_flags & (IFF_LOOPBACK | IFF_UP)) == IFF_UP
		 && 0 < ifcp->ifc_index && !ifcp->ifc_joined) {
			mreq.ipv6mr_multiaddr = ifcp->ifc_ripsin.sin6_addr;
			mreq.ipv6mr_interface = ifcp->ifc_index;
			if (setsockopt(ripsock, IPPROTO_IPV6, IPV6_JOIN_GROUP,
			    &mreq, sizeof(mreq)) == -1) {
				fatalx("IPV6_JOIN_GROUP");
				/*NOTREACHED*/
			}
			log_debug("join %s %s", ifcp->ifc_name, RIP6_DEST);
			ifcp->ifc_joined++;
		}
	}
	close(s);
	freeifaddrs(ifap);
}

void
ifconfig1(const char *name, const struct sockaddr *sa, struct ifc *ifcp, int s)
{
	struct	in6_ifreq ifr;
	const struct sockaddr_in6 *sin6;
	struct	ifac *ifa;
	int	plen;
	char	buf[BUFSIZ];

	sin6 = (const struct sockaddr_in6 *)sa;
	if (IN6_IS_ADDR_SITELOCAL(&sin6->sin6_addr) && !lflag)
		return;
	ifr.ifr_addr = *sin6;
	strncpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
	if (ioctl(s, SIOCGIFNETMASK_IN6, (char *)&ifr) == -1) {
		fatal("ioctl: SIOCGIFNETMASK_IN6");
		/*NOTREACHED*/
	}
	plen = sin6mask2len(&ifr.ifr_addr);
	if ((ifa = ifa_match(ifcp, &sin6->sin6_addr, plen)) != NULL) {
		/* same interface found */
		/* need check if something changed */
		/* XXX not yet implemented */
		return;
	}
	/*
	 * New address is found
	 */
	if ((ifa = calloc(1, sizeof(struct ifac))) == NULL) {
		fatal("calloc: struct ifac");
		/*NOTREACHED*/
	}
	ifa->ifa_conf = ifcp;
	ifa->ifa_next = ifcp->ifc_addr;
	ifcp->ifc_addr = ifa;
	ifa->ifa_addr = sin6->sin6_addr;
	ifa->ifa_plen = plen;
	if (ifcp->ifc_flags & IFF_POINTOPOINT) {
		ifr.ifr_addr = *sin6;
		if (ioctl(s, SIOCGIFDSTADDR_IN6, (char *)&ifr) == -1) {
			fatal("ioctl: SIOCGIFDSTADDR_IN6");
			/*NOTREACHED*/
		}
		ifa->ifa_raddr = ifr.ifr_dstaddr.sin6_addr;
		inet_ntop(AF_INET6, (void *)&ifa->ifa_raddr, buf, sizeof(buf));
		log_debug("found address %s/%d -- %s",
			inet6_n2p(&ifa->ifa_addr), ifa->ifa_plen, buf);
	} else {
		log_debug("found address %s/%d",
			inet6_n2p(&ifa->ifa_addr), ifa->ifa_plen);
	}
	if (ifcp->ifc_index < 0 && IN6_IS_ADDR_LINKLOCAL(&ifa->ifa_addr)) {
		ifcp->ifc_mylladdr = ifa->ifa_addr;
		ifcp->ifc_index = IN6_LINKLOCAL_IFINDEX(ifa->ifa_addr);
		memcpy(&ifcp->ifc_ripsin, &ripsin, ripsin.ss_len);
		SET_IN6_LINKLOCAL_IFINDEX(ifcp->ifc_ripsin.sin6_addr,
			ifcp->ifc_index);
		setindex2ifc(ifcp->ifc_index, ifcp);
		ifcp->ifc_mtu = getifmtu(ifcp->ifc_index);
		if (ifcp->ifc_mtu > RIP6_MAXMTU)
			ifcp->ifc_mtu = RIP6_MAXMTU;
		if (ioctl(s, SIOCGIFMETRIC, (char *)&ifr) == -1) {
			fatal("ioctl: SIOCGIFMETRIC");
			/*NOTREACHED*/
		}
		ifcp->ifc_metric = ifr.ifr_metric;
		log_debug("\tindex: %d, mtu: %d, metric: %d",
			ifcp->ifc_index, ifcp->ifc_mtu, ifcp->ifc_metric);
	} else
		ifcp->ifc_cflags |= IFC_CHANGED;
}

/*
 * Receive and process routing messages.
 * Update interface information as necesssary.
 */
void
rtrecv(void)
{
	char buf[BUFSIZ];
	char *p, *q;
	struct rt_msghdr *rtm;
	struct ifa_msghdr *ifam;
	struct if_msghdr *ifm;
	int len;
	struct ifc *ifcp, *ic;
	int iface = 0, rtable = 0;
	struct sockaddr_in6 *rta[RTAX_MAX];
	struct sockaddr_in6 mask;
	int i, addrs;
	struct riprt *rrt;

	if ((len = read(rtsock, buf, sizeof(buf))) == -1) {
		perror("read from rtsock");
		exit(1);
	}
	if (len < sizeof(*rtm)) {
		log_debug("short read from rtsock: %d (should be > %zu)",
			len, sizeof(*rtm));
		return;
	}
	if (dflag >= 2) {
		log_debug("rtmsg:");
		for (i = 0; i < len; i++) {
			log_enqueue("%02x ", buf[i] & 0xff);
			if (i % 16 == 15)
				log_debug("");
		}
		log_debug("");
	}

	p = buf;
	/* safety against bogus message */
	if (((struct rt_msghdr *)p)->rtm_msglen <= 0) {
		log_debug("bogus rtmsg: length=%d",
		    ((struct rt_msghdr *)p)->rtm_msglen);
		return;
	}
	if (((struct rt_msghdr *)p)->rtm_version != RTM_VERSION)
		return;

	rtm = NULL;
	ifam = NULL;
	ifm = NULL;
	switch (((struct rt_msghdr *)p)->rtm_type) {
	case RTM_NEWADDR:
	case RTM_DELADDR:
		ifam = (struct ifa_msghdr *)p;
		addrs = ifam->ifam_addrs;
		q = (char *)(ifam + 1);
		break;
	case RTM_IFINFO:
		ifm = (struct if_msghdr *)p;
		addrs = ifm->ifm_addrs;
		q = (char *)(ifm + 1);
		break;
	default:
		rtm = (struct rt_msghdr *)p;
		addrs = rtm->rtm_addrs;
		q = (char *)(p + rtm->rtm_hdrlen);
		if (rtm->rtm_pid == pid) {
#if 0
			log_debug("rtmsg looped back to me, ignored");
#endif
			return;
		}
		break;
	}
	memset(&rta, 0, sizeof(rta));
	for (i = 0; i < RTAX_MAX; i++) {
		if (addrs & (1 << i)) {
			rta[i] = (struct sockaddr_in6 *)q;
			q += ROUNDUP(rta[i]->sin6_len);
		}
	}

	log_debug("rtsock: %s (addrs=%x)",
	    rttypes((struct rt_msghdr *)p), addrs);
	if (dflag >= 2) {
		for (i = 0;
		     i < ((struct rt_msghdr *)p)->rtm_msglen;
		     i++) {
			log_enqueue("%02x ", p[i] & 0xff);
			if (i % 16 == 15)
				log_debug("");
		}
		log_debug("");
	}
	/*
	 * Easy ones first.
	 *
	 * We may be able to optimize by using ifm->ifm_index or
	 * ifam->ifam_index.  For simplicity we don't do that here.
	 */
	switch (((struct rt_msghdr *)p)->rtm_type) {
	case RTM_NEWADDR:
	case RTM_IFINFO:
		iface++;
		return;
	case RTM_ADD:
		rtable++;
		return;
	case RTM_MISS:
	case RTM_RESOLVE:
	case RTM_GET:
		/* nothing to be done here */
		log_debug("\tnothing to be done, ignored");
		return;
	}

#if 0
	if (rta[RTAX_DST] == NULL) {
		log_debug("\tno destination, ignored");
		return;
	}
	if (rta[RTAX_DST]->sin6_family != AF_INET6) {
		log_debug("\taf mismatch, ignored");
		return;
	}
	if (IN6_IS_ADDR_LINKLOCAL(&rta[RTAX_DST]->sin6_addr)) {
		log_debug("\tlinklocal destination, ignored");
		return;
	}
	if (IN6_ARE_ADDR_EQUAL(&rta[RTAX_DST]->sin6_addr, &in6addr_loopback)) {
		log_debug("\tloopback destination, ignored");
		return;		/* Loopback */
	}
	if (IN6_IS_ADDR_MULTICAST(&rta[RTAX_DST]->sin6_addr)) {
		log_debug("\tmulticast destination, ignored");
		return;
	}
#endif

	/* hard ones */
	switch (((struct rt_msghdr *)p)->rtm_type) {
	case RTM_NEWADDR:
	case RTM_IFINFO:
	case RTM_ADD:
	case RTM_MISS:
	case RTM_RESOLVE:
	case RTM_GET:
		/* should already be handled */
		fatalx("rtrecv: never reach here");
		/*NOTREACHED*/
	case RTM_DELETE:
		if (!rta[RTAX_DST] || !rta[RTAX_GATEWAY]) {
			log_debug("\tsome of dst/gw/netmask are "
			    "unavailable, ignored");
			break;
		}
		if ((rtm->rtm_flags & RTF_HOST) != 0) {
			mask.sin6_len = sizeof(mask);
			memset(&mask.sin6_addr, 0xff,
			    sizeof(mask.sin6_addr));
			rta[RTAX_NETMASK] = &mask;
		} else if (!rta[RTAX_NETMASK]) {
			log_debug("\tsome of dst/gw/netmask are "
			    "unavailable, ignored");
			break;
		}
		if (rt_del(rta[RTAX_DST], rta[RTAX_GATEWAY],
			rta[RTAX_NETMASK]) == 0) {
			rtable++;	/*just to be sure*/
		}
		break;
	case RTM_CHANGE:
	case RTM_REDIRECT:
		log_debug("\tnot supported yet, ignored");
		break;
	case RTM_DELADDR:
		if (!rta[RTAX_NETMASK] || !rta[RTAX_IFA]) {
			log_debug("\tno netmask or ifa given, ignored");
			break;
		}
		if (ifam->ifam_index < nindex2ifc)
			ifcp = index2ifc[ifam->ifam_index];
		else
			ifcp = NULL;
		if (!ifcp) {
			log_debug("\tinvalid ifam_index %d, ignored",
			    ifam->ifam_index);
			break;
		}
		if (!rt_deladdr(ifcp, rta[RTAX_IFA], rta[RTAX_NETMASK]))
			iface++;
		break;
	}

	if (iface) {
		log_debug("rtsock: reconfigure interfaces, refresh interface routes");
		ifconfig();
		for (ifcp = ifc; ifcp; ifcp = ifcp->ifc_next)
			if (ifcp->ifc_cflags & IFC_CHANGED) {
				if (ifrt(ifcp, 1)) {
					for (ic = ifc; ic; ic = ic->ifc_next) {
						if (ifcp->ifc_index == ic->ifc_index)
							continue;
						if (ic->ifc_flags & IFF_UP)
							ripsend(ic, &ic->ifc_ripsin,
							RRTF_CHANGED);
					}
					/* Reset the flag */
					for (rrt = riprt; rrt; rrt = rrt->rrt_next)
						rrt->rrt_rflags &= ~RRTF_CHANGED;
				}
				ifcp->ifc_cflags &= ~IFC_CHANGED;
			}
	}
	if (rtable) {
		log_debug("rtsock: read routing table again");
		krtread(1);
	}
}

/*
 * remove specified route from the internal routing table.
 */
int
rt_del(const struct sockaddr_in6 *sdst, const struct sockaddr_in6 *sgw,
    const struct sockaddr_in6 *smask)
{
	const struct in6_addr *dst = NULL;
	const struct in6_addr *gw = NULL;
	int prefix;
	struct netinfo6 ni6;
	struct riprt *rrt = NULL;
	time_t t_lifetime;

	if (sdst->sin6_family != AF_INET6) {
		log_debug("\tother AF, ignored");
		return -1;
	}
	if (IN6_IS_ADDR_LINKLOCAL(&sdst->sin6_addr)
	 || IN6_ARE_ADDR_EQUAL(&sdst->sin6_addr, &in6addr_loopback)
	 || IN6_IS_ADDR_MULTICAST(&sdst->sin6_addr)) {
		log_debug("\taddress %s not interesting, ignored",
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
		log_debug("\t%s is a interface route, guessing prefixlen",
			inet6_n2p(dst));
		longest = NULL;
		for (rrt = riprt; rrt; rrt = rrt->rrt_next) {
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
			log_debug("\tno matching interface route found");
			return -1;
		}
		gw = &in6addr_loopback;
		prefix = rrt->rrt_info.rip6_plen;
	} else {
		log_debug("\tunsupported af: (gw=%d)", sgw->sin6_family);
		return -1;
	}

	log_enqueue("\tdeleting %s/%d ", inet6_n2p(dst), prefix);
	log_debug("gw %s", inet6_n2p(gw));
	t_lifetime = time(NULL) - RIP_LIFETIME;
	/* age route for interface address */
	memset(&ni6, 0, sizeof(ni6));
	ni6.rip6_dest = *dst;
	ni6.rip6_plen = prefix;
	applyplen(&ni6.rip6_dest, ni6.rip6_plen);	/*to be sure*/
	log_debug("\tfind route %s/%d", inet6_n2p(&ni6.rip6_dest),
		ni6.rip6_plen);
	if (!rrt && (rrt = rtsearch(&ni6, NULL)) == NULL) {
		log_debug("\tno route found");
		return -1;
	}
#if 0
	if ((rrt->rrt_flags & RTF_STATIC) == 0) {
		log_debug("\tyou can delete static routes only");
	} else
#endif
	if (!IN6_ARE_ADDR_EQUAL(&rrt->rrt_gw, gw)) {
		log_enqueue("\tgw mismatch: %s <-> ",
			inet6_n2p(&rrt->rrt_gw));
		log_debug("%s", inet6_n2p(gw));
	} else {
		log_debug("\troute found, age it");
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
int
rt_deladdr(struct ifc *ifcp, const struct sockaddr_in6 *sifa,
    const struct sockaddr_in6 *smask)
{
	const struct in6_addr *addr = NULL;
	int prefix;
	struct ifac *ifa = NULL;
	struct netinfo6 ni6;
	struct riprt *rrt = NULL;
	time_t t_lifetime;
	int updated = 0;

	if (sifa->sin6_family != AF_INET6) {
		log_debug("\tother AF, ignored");
		return -1;
	}
	addr = &sifa->sin6_addr;
	prefix = sin6mask2len(smask);

	log_debug("\tdeleting %s/%d from %s",
		inet6_n2p(addr), prefix, ifcp->ifc_name);
	ifa = ifa_match(ifcp, addr, prefix);
	if (!ifa) {
		log_debug("\tno matching ifa found for %s/%d on %s",
			inet6_n2p(addr), prefix, ifcp->ifc_name);
		return -1;
	}
	if (ifa->ifa_conf != ifcp) {
		log_debug("\taddress table corrupt: back pointer does not match "
			"(%s != %s)",
			ifcp->ifc_name, ifa->ifa_conf->ifc_name);
		return -1;
	}
	/* remove ifa from interface */
	if (ifcp->ifc_addr == ifa)
		ifcp->ifc_addr = ifa->ifa_next;
	else {
		struct ifac *p;
		for (p = ifcp->ifc_addr; p; p = p->ifa_next) {
			if (p->ifa_next == ifa) {
				p->ifa_next = ifa->ifa_next;
				break;
			}
		}
	}
	ifa->ifa_next = NULL;
	ifa->ifa_conf = NULL;
	t_lifetime = time(NULL) - RIP_LIFETIME;
	/* age route for interface address */
	memset(&ni6, 0, sizeof(ni6));
	ni6.rip6_dest = ifa->ifa_addr;
	ni6.rip6_plen = ifa->ifa_plen;
	applyplen(&ni6.rip6_dest, ni6.rip6_plen);
	log_debug("\tfind interface route %s/%d on %d",
		inet6_n2p(&ni6.rip6_dest), ni6.rip6_plen, ifcp->ifc_index);
	if ((rrt = rtsearch(&ni6, NULL)) != NULL) {
		struct in6_addr none;
		memset(&none, 0, sizeof(none));
		if (rrt->rrt_index == ifcp->ifc_index &&
		    (IN6_ARE_ADDR_EQUAL(&rrt->rrt_gw, &none) ||
		     IN6_IS_ADDR_LOOPBACK(&rrt->rrt_gw))) {
			log_debug("\troute found, age it");
			if (rrt->rrt_t == 0 || rrt->rrt_t > t_lifetime) {
				rrt->rrt_t = t_lifetime;
				rrt->rrt_info.rip6_metric = HOPCNT_INFINITY6;
			}
			updated++;
		} else {
			log_debug("\tnon-interface route found: %s/%d on %d",
				inet6_n2p(&rrt->rrt_info.rip6_dest),
				rrt->rrt_info.rip6_plen,
				rrt->rrt_index);
		}
	} else
		log_debug("\tno interface route found");
	/* age route for p2p destination */
	if (ifcp->ifc_flags & IFF_POINTOPOINT) {
		memset(&ni6, 0, sizeof(ni6));
		ni6.rip6_dest = ifa->ifa_raddr;
		ni6.rip6_plen = 128;
		applyplen(&ni6.rip6_dest, ni6.rip6_plen);	/*to be sure*/
		log_debug("\tfind p2p route %s/%d on %d",
			inet6_n2p(&ni6.rip6_dest), ni6.rip6_plen,
			ifcp->ifc_index);
		if ((rrt = rtsearch(&ni6, NULL)) != NULL) {
			if (rrt->rrt_index == ifcp->ifc_index &&
			    IN6_ARE_ADDR_EQUAL(&rrt->rrt_gw, &ifa->ifa_addr)) {
				log_debug("\troute found, age it");
				if (rrt->rrt_t == 0 || rrt->rrt_t > t_lifetime) {
					rrt->rrt_t = t_lifetime;
					rrt->rrt_info.rip6_metric =
					    HOPCNT_INFINITY6;
					updated++;
				}
			} else {
				log_debug("\tnon-p2p route found: %s/%d on %d",
					inet6_n2p(&rrt->rrt_info.rip6_dest),
					rrt->rrt_info.rip6_plen,
					rrt->rrt_index);
			}
		} else
			log_debug("\tno p2p route found");
	}
	return updated ? 0 : -1;
}

/*
 * Get each interface address and put those interface routes to the route
 * list.
 */
int
ifrt(struct ifc *ifcp, int again)
{
	struct ifac *ifa;
	struct riprt *rrt = NULL, *search_rrt, *prev_rrt, *loop_rrt;
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

	for (ifa = ifcp->ifc_addr; ifa; ifa = ifa->ifa_next) {
		if (IN6_IS_ADDR_LINKLOCAL(&ifa->ifa_addr)) {
#if 0
			log_debug("route: %s on %s: "
			    "skip linklocal interface address",
			    inet6_n2p(&ifa->ifa_addr), ifcp->ifc_name);
#endif
			continue;
		}
		if (IN6_IS_ADDR_UNSPECIFIED(&ifa->ifa_addr)) {
#if 0
			log_debug("route: %s: skip unspec interface address",
			    ifcp->ifc_name);
#endif
			continue;
		}
		if (IN6_IS_ADDR_LOOPBACK(&ifa->ifa_addr)) {
#if 0
			log_debug("route: %s: skip loopback address",
			    ifcp->ifc_name);
#endif
			continue;
		}
		if (ifcp->ifc_flags & IFF_UP) {
			if ((rrt = calloc(1, sizeof(struct riprt))) == NULL)
				fatal("calloc: struct riprt");
			rrt->rrt_index = ifcp->ifc_index;
			rrt->rrt_t = 0;	/* don't age */
			rrt->rrt_info.rip6_dest = ifa->ifa_addr;
			rrt->rrt_info.rip6_tag = htons(routetag & 0xffff);
			rrt->rrt_info.rip6_metric = 1 + ifcp->ifc_metric;
			rrt->rrt_info.rip6_plen = ifa->ifa_plen;
			if (ifa->ifa_plen == 128)
				rrt->rrt_flags = RTF_HOST;
			else
				rrt->rrt_flags = RTF_CLONING;
			rrt->rrt_rflags |= RRTF_CHANGED;
			applyplen(&rrt->rrt_info.rip6_dest, ifa->ifa_plen);
			memset(&rrt->rrt_gw, 0, sizeof(struct in6_addr));
			rrt->rrt_gw = ifa->ifa_addr;
			np = &rrt->rrt_info;
			search_rrt = rtsearch(np, &prev_rrt);
			if (search_rrt != NULL) {
				if (search_rrt->rrt_info.rip6_metric <=
				    rrt->rrt_info.rip6_metric) {
					/* Already have better route */
					if (!again) {
						log_debug("route: %s/%d: "
						    "already registered (%s)",
						    inet6_n2p(&np->rip6_dest), np->rip6_plen,
						    ifcp->ifc_name);
					}
					goto next;
				}

				if (prev_rrt)
					prev_rrt->rrt_next = rrt->rrt_next;
				else
					riprt = rrt->rrt_next;
				delroute(&rrt->rrt_info, &rrt->rrt_gw);
			}
			/* Attach the route to the list */
			log_debug("route: %s/%d: register route (%s)",
			    inet6_n2p(&np->rip6_dest), np->rip6_plen,
			    ifcp->ifc_name);
			rrt->rrt_next = riprt;
			riprt = rrt;
			addroute(rrt, &rrt->rrt_gw, ifcp);
			rrt = NULL;
			sendrequest(ifcp);
			ripsend(ifcp, &ifcp->ifc_ripsin, 0);
			need_trigger = 1;
		} else {
			for (loop_rrt = riprt; loop_rrt; loop_rrt = loop_rrt->rrt_next) {
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
		free(rrt);
	}
	return need_trigger;
}

/*
 * there are couple of p2p interface routing models.  "behavior" lets
 * you pick one.  it looks that gated behavior fits best with BSDs,
 * since BSD kernels do not look at prefix length on p2p interfaces.
 */
void
ifrt_p2p(struct ifc *ifcp, int again)
{
	struct ifac *ifa;
	struct riprt *rrt, *orrt, *prevrrt;
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

	for (ifa = ifcp->ifc_addr; ifa; ifa = ifa->ifa_next) {
		addr = ifa->ifa_addr;
		dest = ifa->ifa_raddr;
		applyplen(&addr, ifa->ifa_plen);
		applyplen(&dest, ifa->ifa_plen);
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
			if (!IN6_IS_ADDR_UNSPECIFIED(&ifa->ifa_raddr)) {
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
			if ((rrt = calloc(1, sizeof(struct riprt))) == NULL) {
				fatal("calloc: struct riprt");
				/*NOTREACHED*/
			}
			rrt->rrt_index = ifcp->ifc_index;
			rrt->rrt_t = 0;	/* don't age */
			switch (i) {
			case P2PADVERT_NETWORK:
				rrt->rrt_info.rip6_dest = ifa->ifa_addr;
				rrt->rrt_info.rip6_plen = ifa->ifa_plen;
				applyplen(&rrt->rrt_info.rip6_dest,
				    ifa->ifa_plen);
				category = "network";
				break;
			case P2PADVERT_ADDR:
				rrt->rrt_info.rip6_dest = ifa->ifa_addr;
				rrt->rrt_info.rip6_plen = 128;
				rrt->rrt_gw = in6addr_loopback;
				category = "addr";
				break;
			case P2PADVERT_DEST:
				rrt->rrt_info.rip6_dest = ifa->ifa_raddr;
				rrt->rrt_info.rip6_plen = 128;
				rrt->rrt_gw = ifa->ifa_addr;
				category = "dest";
				break;
			}
			if (IN6_IS_ADDR_UNSPECIFIED(&rrt->rrt_info.rip6_dest) ||
			    IN6_IS_ADDR_LINKLOCAL(&rrt->rrt_info.rip6_dest)) {
#if 0
				log_debug("route: %s: skip unspec/linklocal "
				    "(%s on %s)", category, ifcp->ifc_name);
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
			orrt = rtsearch(np, &prevrrt);
			if (!orrt) {
				/* Attach the route to the list */
				log_debug("route: %s/%d: register route "
				    "(%s on %s%s)",
				    inet6_n2p(&np->rip6_dest), np->rip6_plen,
				    category, ifcp->ifc_name, noadv);
				rrt->rrt_next = riprt;
				riprt = rrt;
			} else if (rrt->rrt_index != orrt->rrt_index ||
			    rrt->rrt_info.rip6_metric != orrt->rrt_info.rip6_metric) {
				/* swap route */
				rrt->rrt_next = orrt->rrt_next;
				if (prevrrt)
					prevrrt->rrt_next = rrt;
				else
					riprt = rrt;
				free(orrt);

				log_debug("route: %s/%d: update (%s on %s%s)",
				    inet6_n2p(&np->rip6_dest), np->rip6_plen,
				    category, ifcp->ifc_name, noadv);
			} else {
				/* Already found */
				if (!again) {
					log_debug("route: %s/%d: "
					    "already registered (%s on %s%s)",
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

int
getifmtu(int ifindex)
{
	int	mib[6];
	char	*buf = NULL;
	size_t	needed;
	struct	if_msghdr *ifm;
	int	mtu;

	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;
	mib[3] = AF_INET6;
	mib[4] = NET_RT_IFLIST;
	mib[5] = ifindex;
	while (1) {
		if (sysctl(mib, 6, NULL, &needed, NULL, 0) == -1)
			fatal("sysctl estimate NET_RT_IFLIST");
		if (needed == 0)
			break;
		if ((buf = realloc(buf, needed)) == NULL)
			fatal(NULL);
		if (sysctl(mib, 6, buf, &needed, NULL, 0) == -1) {
			if (errno == ENOMEM)
				continue;
			fatal("sysctl NET_RT_IFLIST");
		}
		break;
	}
	ifm = (struct if_msghdr *)buf;
	mtu = ifm->ifm_data.ifi_mtu;
	free(buf);
	return mtu;
}

const char *
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
	RTTYPE("REDIRECT", RTM_REDIRECT);
	RTTYPE("MISS", RTM_MISS);
	RTTYPE("RESOLVE", RTM_RESOLVE);
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

const char *
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
	RTFLAG("m", RTF_MULTICAST);
	RTFLAG("C", RTF_CLONING);
	RTFLAG("c", RTF_CLONED);
	RTFLAG("L", RTF_LLINFO);
	RTFLAG("S", RTF_STATIC);
	RTFLAG("B", RTF_BLACKHOLE);
	RTFLAG("3", RTF_PROTO3);
	RTFLAG("2", RTF_PROTO2);
	RTFLAG("1", RTF_PROTO1);
	RTFLAG("b", RTF_BROADCAST);
#undef RTFLAG
	return buf;
}

const char *
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
	IFFLAG("STATICARP", IFF_STATICARP);
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

void
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
		retry++;
		free(buf);
		buf = NULL;
		errmsg = NULL;
		if (sysctl(mib, 6, NULL, &msize, NULL, 0) == -1) {
			errmsg = "sysctl estimate";
			continue;
		}
		if ((buf = malloc(msize)) == NULL) {
			errmsg = "malloc";
			continue;
		}
		if (sysctl(mib, 6, buf, &msize, NULL, 0) == -1) {
			errmsg = "sysctl NET_RT_DUMP";
			continue;
		}
	} while (retry < 5 && errmsg != NULL);
	if (errmsg) {
		fatal(errmsg);
		/*NOTREACHED*/
	} else if (1 < retry)
		log_info("NET_RT_DUMP %d retries", retry);

	lim = buf + msize;
	for (p = buf; p < lim; p += rtm->rtm_msglen) {
		rtm = (struct rt_msghdr *)p;
		if (rtm->rtm_version != RTM_VERSION)
			continue;
		rt_entry(rtm, again);
	}
	free(buf);
}

void
rt_entry(struct rt_msghdr *rtm, int again)
{
	struct	sockaddr_in6 *sin6_dst, *sin6_gw, *sin6_mask;
	struct	sockaddr_in6 *sin6_ifp;
	char	*rtmp, *ifname = NULL;
	struct	riprt *rrt, *orrt;
	struct	netinfo6 *np;
	int	s;

	sin6_dst = sin6_gw = sin6_mask = sin6_ifp = 0;
	if ((rtm->rtm_flags & RTF_UP) == 0 || rtm->rtm_flags &
		(RTF_CLONING|RTF_LLINFO|RTF_BLACKHOLE)) {
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
	/*
	 * do not look at dynamic routes.
	 * netbsd/openbsd cloned routes have UGHD.
	 */
	if (rtm->rtm_flags & RTF_DYNAMIC)
		return;
	rtmp = (char *)((char *)rtm + rtm->rtm_hdrlen);
	/* Destination */
	if ((rtm->rtm_addrs & RTA_DST) == 0)
		return;		/* ignore routes without destination address */
	sin6_dst = (struct sockaddr_in6 *)rtmp;
	rtmp += ROUNDUP(sin6_dst->sin6_len);
	if (rtm->rtm_addrs & RTA_GATEWAY) {
		sin6_gw = (struct sockaddr_in6 *)rtmp;
		rtmp += ROUNDUP(sin6_gw->sin6_len);
	}
	if (rtm->rtm_addrs & RTA_NETMASK) {
		sin6_mask = (struct sockaddr_in6 *)rtmp;
		rtmp += ROUNDUP(sin6_mask->sin6_len);
	}
	if (rtm->rtm_addrs & RTA_IFP) {
		sin6_ifp = (struct sockaddr_in6 *)rtmp;
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

	if ((rrt = calloc(1, sizeof(struct riprt))) == NULL) {
		fatal("calloc: struct riprt");
		/*NOTREACHED*/
	}
	np = &rrt->rrt_info;
	rrt->rrt_t = time(NULL);
	if (aflag == 0 && (rtm->rtm_flags & RTF_STATIC))
		rrt->rrt_t = 0;	/* Don't age static routes */
	if ((rtm->rtm_flags & (RTF_HOST|RTF_GATEWAY)) == RTF_HOST)
		rrt->rrt_t = 0;	/* Don't age non-gateway host routes */
	np->rip6_tag = 0;
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

	orrt = rtsearch(np, NULL);
	if (orrt && orrt->rrt_info.rip6_metric != HOPCNT_INFINITY6) {
		/* Already found */
		if (!again) {
			log_debug("route: %s/%d flags %s: already registered",
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
	log_enqueue("route: %s/%d flags %s",
		inet6_n2p(&np->rip6_dest), np->rip6_plen, rtflags(rtm));
	log_enqueue(" gw %s", inet6_n2p(&rrt->rrt_gw));

	/* Interface */
	s = rtm->rtm_index;
	if (s < nindex2ifc && index2ifc[s])
		ifname = index2ifc[s]->ifc_name;
	else {
		log_debug(" not configured");
		free(rrt);
		return;
	}
	log_debug(" if %s sock %d", ifname, s);
	rrt->rrt_index = s;

	/* Check gateway */
	if (!IN6_IS_ADDR_LINKLOCAL(&rrt->rrt_gw) &&
	    !IN6_IS_ADDR_LOOPBACK(&rrt->rrt_gw)) {
		log_warnx("***** Gateway %s is not a link-local address.",
			inet6_n2p(&rrt->rrt_gw));
		log_warnx("*****     dest(%s) if(%s) -- Not optimized.",
			inet6_n2p(&rrt->rrt_info.rip6_dest), ifname);
		rrt->rrt_rflags |= RRTF_NH_NOT_LLADDR;
	}

	/* Put it to the route list */
	if (orrt && orrt->rrt_info.rip6_metric == HOPCNT_INFINITY6) {
		/* replace route list */
		rrt->rrt_next = orrt->rrt_next;
		*orrt = *rrt;
		log_debug("route: %s/%d flags %s: replace new route",
		    inet6_n2p(&np->rip6_dest), np->rip6_plen,
		    rtflags(rtm));
		free(rrt);
	} else {
		rrt->rrt_next = riprt;
		riprt = rrt;
	}
}

int
addroute(struct riprt *rrt, const struct in6_addr *gw, struct ifc *ifcp)
{
	struct	netinfo6 *np;
	u_char	buf[BUFSIZ], buf1[BUFSIZ], buf2[BUFSIZ];
	struct	rt_msghdr	*rtm;
	struct	sockaddr_in6	*sin6;
	int	len;

	np = &rrt->rrt_info;
	inet_ntop(AF_INET6, (const void *)gw, (char *)buf1, sizeof(buf1));
	inet_ntop(AF_INET6, (void *)&ifcp->ifc_mylladdr, (char *)buf2, sizeof(buf2));
	if (uflag)
		log_info("RTADD: %s/%d gw %s [%d] ifa %s",
		    inet6_n2p(&np->rip6_dest), np->rip6_plen, buf1,
		    np->rip6_metric - 1, buf2);
	else
		log_debug("RTADD: %s/%d gw %s [%d] ifa %s",
		    inet6_n2p(&np->rip6_dest), np->rip6_plen, buf1,
		    np->rip6_metric - 1, buf2);

	if (nflag)
		return 0;

	memset(buf, 0, sizeof(buf));
	rtm = (struct rt_msghdr *)buf;
	rtm->rtm_type = RTM_ADD;
	rtm->rtm_version = RTM_VERSION;
	rtm->rtm_seq = ++seq;
	rtm->rtm_flags = rrt->rrt_flags;
	rtm->rtm_addrs = RTA_DST | RTA_GATEWAY | RTA_NETMASK;
	rtm->rtm_inits = RTV_HOPCOUNT;
	sin6 = (struct sockaddr_in6 *)&buf[sizeof(struct rt_msghdr)];
	/* Destination */
	sin6->sin6_len = sizeof(struct sockaddr_in6);
	sin6->sin6_family = AF_INET6;
	sin6->sin6_addr = np->rip6_dest;
	sin6 = (struct sockaddr_in6 *)((char *)sin6 + ROUNDUP(sin6->sin6_len));
	/* Gateway */
	sin6->sin6_len = sizeof(struct sockaddr_in6);
	sin6->sin6_family = AF_INET6;
	sin6->sin6_addr = *gw;
	sin6 = (struct sockaddr_in6 *)((char *)sin6 + ROUNDUP(sin6->sin6_len));
	/* Netmask */
	sin6->sin6_len = sizeof(struct sockaddr_in6);
	sin6->sin6_family = AF_INET6;
	sin6->sin6_addr = *(plen2mask(np->rip6_plen));
	sin6 = (struct sockaddr_in6 *)((char *)sin6 + ROUNDUP(sin6->sin6_len));

	len = (char *)sin6 - (char *)buf;
	rtm->rtm_msglen = len;
	if (write(rtsock, buf, len) > 0)
		return 0;

	if (errno == EEXIST) {
		log_warnx("RTADD: Route already exists %s/%d gw %s",
		    inet6_n2p(&np->rip6_dest), np->rip6_plen, buf1);
	} else {
		log_warnx("RTADD: Can not write to rtsock (addroute): %s",
		    strerror(errno));
	}
	return -1;
}

int
delroute(struct netinfo6 *np, struct in6_addr *gw)
{
	u_char	buf[BUFSIZ], buf2[BUFSIZ];
	struct	rt_msghdr	*rtm;
	struct	sockaddr_in6	*sin6;
	int	len;

	inet_ntop(AF_INET6, (void *)gw, (char *)buf2, sizeof(buf2));
	if (uflag)
		log_info("RTDEL: %s/%d gw %s", inet6_n2p(&np->rip6_dest),
		    np->rip6_plen, buf2);
	else
		log_debug("RTDEL: %s/%d gw %s", inet6_n2p(&np->rip6_dest),
		    np->rip6_plen, buf2);

	if (nflag)
		return 0;

	memset(buf, 0, sizeof(buf));
	rtm = (struct rt_msghdr *)buf;
	rtm->rtm_type = RTM_DELETE;
	rtm->rtm_version = RTM_VERSION;
	rtm->rtm_seq = ++seq;
	rtm->rtm_flags = RTF_UP | RTF_GATEWAY;
	if (np->rip6_plen == sizeof(struct in6_addr) * 8)
		rtm->rtm_flags |= RTF_HOST;
	rtm->rtm_addrs = RTA_DST | RTA_GATEWAY | RTA_NETMASK;
	sin6 = (struct sockaddr_in6 *)&buf[sizeof(struct rt_msghdr)];
	/* Destination */
	sin6->sin6_len = sizeof(struct sockaddr_in6);
	sin6->sin6_family = AF_INET6;
	sin6->sin6_addr = np->rip6_dest;
	sin6 = (struct sockaddr_in6 *)((char *)sin6 + ROUNDUP(sin6->sin6_len));
	/* Gateway */
	sin6->sin6_len = sizeof(struct sockaddr_in6);
	sin6->sin6_family = AF_INET6;
	sin6->sin6_addr = *gw;
	sin6 = (struct sockaddr_in6 *)((char *)sin6 + ROUNDUP(sin6->sin6_len));
	/* Netmask */
	sin6->sin6_len = sizeof(struct sockaddr_in6);
	sin6->sin6_family = AF_INET6;
	sin6->sin6_addr = *(plen2mask(np->rip6_plen));
	sin6 = (struct sockaddr_in6 *)((char *)sin6 + ROUNDUP(sin6->sin6_len));

	len = (char *)sin6 - (char *)buf;
	rtm->rtm_msglen = len;
	if (write(rtsock, buf, len) >= 0)
		return 0;

	if (errno == ESRCH) {
		log_warnx("RTDEL: Route does not exist: %s/%d gw %s",
		    inet6_n2p(&np->rip6_dest), np->rip6_plen, buf2);
	} else {
		log_warnx("RTDEL: Can not write to rtsock (delroute): %s",
		    strerror(errno));
	}
	return -1;
}

struct in6_addr *
getroute(struct netinfo6 *np, struct in6_addr *gw)
{
	u_char buf[BUFSIZ];
	int len;
	struct rt_msghdr *rtm;
	struct sockaddr_in6 *sin6;

	rtm = (struct rt_msghdr *)buf;
	len = sizeof(struct rt_msghdr) + sizeof(struct sockaddr_in6);
	memset(rtm, 0, len);
	rtm->rtm_type = RTM_GET;
	rtm->rtm_version = RTM_VERSION;
	rtm->rtm_seq = ++seq;
	rtm->rtm_addrs = RTA_DST;
	rtm->rtm_msglen = len;
	sin6 = (struct sockaddr_in6 *)&buf[sizeof(struct rt_msghdr)];
	sin6->sin6_len = sizeof(struct sockaddr_in6);
	sin6->sin6_family = AF_INET6;
	sin6->sin6_addr = np->rip6_dest;
	if (write(rtsock, buf, len) == -1) {
		if (errno == ESRCH)	/* No such route found */
			return NULL;
		perror("write to rtsock");
		exit(1);
	}
	do {
		if ((len = read(rtsock, buf, sizeof(buf))) == -1) {
			perror("read from rtsock");
			exit(1);
		}
		rtm = (struct rt_msghdr *)buf;
	} while (rtm->rtm_version != RTM_VERSION ||
	    rtm->rtm_seq != seq || rtm->rtm_pid != pid);
	sin6 = (struct sockaddr_in6 *)&buf[sizeof(struct rt_msghdr)];
	if (rtm->rtm_addrs & RTA_DST) {
		sin6 = (struct sockaddr_in6 *)
			((char *)sin6 + ROUNDUP(sin6->sin6_len));
	}
	if (rtm->rtm_addrs & RTA_GATEWAY) {
		*gw = sin6->sin6_addr;
		return gw;
	}
	return NULL;
}

const char *
inet6_n2p(const struct in6_addr *p)
{
	static char buf[BUFSIZ];

	return inet_ntop(AF_INET6, (const void *)p, buf, sizeof(buf));
}

void
ifrtdump(int sig)
{

	ifdump(sig);
	rtdump(sig);
}

void
ifdump(int sig)
{
	struct ifc *ifcp;
	int i;

	log_info("%s: Interface Table Dump", hms());
	log_info("  Number of interfaces: %d", nifc);
	for (i = 0; i < 2; i++) {
		log_info("  %sadvertising interfaces:", i ? "non-" : "");
		for (ifcp = ifc; ifcp; ifcp = ifcp->ifc_next) {
			if (i == 0) {
				if ((ifcp->ifc_flags & IFF_UP) == 0)
					continue;
				if (iff_find(ifcp, 'N') != NULL)
					continue;
			} else {
				if (ifcp->ifc_flags & IFF_UP)
					continue;
			}
			ifdump0(ifcp);
		}
	}
	log_info("");
}

void
ifdump0(const struct ifc *ifcp)
{
	struct ifac *ifa;
	struct iff *iffp;
	char buf[BUFSIZ];
	const char *ft;
	int addr;

	log_info("    %s: index(%d) flags(%s) addr(%s) mtu(%d) metric(%d)",
		ifcp->ifc_name, ifcp->ifc_index, ifflags(ifcp->ifc_flags),
		inet6_n2p(&ifcp->ifc_mylladdr),
		ifcp->ifc_mtu, ifcp->ifc_metric);
	for (ifa = ifcp->ifc_addr; ifa; ifa = ifa->ifa_next) {
		if (ifcp->ifc_flags & IFF_POINTOPOINT) {
			inet_ntop(AF_INET6, (void *)&ifa->ifa_raddr,
				buf, sizeof(buf));
			log_info("\t%s/%d -- %s",
				inet6_n2p(&ifa->ifa_addr),
				ifa->ifa_plen, buf);
		} else {
			log_info("\t%s/%d",
				inet6_n2p(&ifa->ifa_addr),
				ifa->ifa_plen);
		}
	}
	if (ifcp->ifc_filter) {
		log_enqueue("\tFilter:");
		for (iffp = ifcp->ifc_filter; iffp; iffp = iffp->iff_next) {
			addr = 0;
			switch (iffp->iff_type) {
			case 'A':
				ft = "Aggregate"; addr++; break;
			case 'N':
				ft = "No-use"; break;
			case 'O':
				ft = "Advertise-only"; addr++; break;
			case 'T':
				ft = "Default-only"; break;
			case 'L':
				ft = "Listen-only"; addr++; break;
			default:
				snprintf(buf, sizeof(buf), "Unknown-%c", iffp->iff_type);
				ft = buf;
				addr++;
				break;
			}
			log_enqueue(" %s", ft);
			if (addr) {
				log_enqueue("(%s/%d)",
				    inet6_n2p(&iffp->iff_addr), iffp->iff_plen);
			}
		}
		log_info("");
	}
}

void
rtdump(int sig)
{
	struct	riprt *rrt;
	char	buf[BUFSIZ];
	time_t	t, age;

	t = time(NULL);
	log_info("%s: Routing Table Dump", hms());
	for (rrt = riprt; rrt; rrt = rrt->rrt_next) {
		if (rrt->rrt_t == 0)
			age = 0;
		else
			age = t - rrt->rrt_t;
		inet_ntop(AF_INET6, (void *)&rrt->rrt_info.rip6_dest,
			buf, sizeof(buf));
		log_enqueue("    %s/%d if(%d:%s) gw(%s) [%d] age(%lld)",
			buf, rrt->rrt_info.rip6_plen, rrt->rrt_index,
			index2ifc[rrt->rrt_index]->ifc_name,
			inet6_n2p(&rrt->rrt_gw),
			rrt->rrt_info.rip6_metric, (long long)age);
		if (rrt->rrt_info.rip6_tag) {
			log_enqueue(" tag(0x%04x)",
				ntohs(rrt->rrt_info.rip6_tag) & 0xffff);
		}
		if (rrt->rrt_rflags & RRTF_NH_NOT_LLADDR)
			log_enqueue(" NOT-LL");
		if (rrt->rrt_rflags & RRTF_NOADVERTISE)
			log_enqueue(" NO-ADV");
		log_info("");
	}
}

/*
 * Parse the -A (and -O) options and put corresponding filter object to the
 * specified interface structures.  Each of the -A/O option has the following
 * syntax:	-A 5f09:c400::/32,ef0,ef1  (aggregate)
 * 		-O 5f09:c400::/32,ef0,ef1  (only when match)
 */
void
filterconfig(void)
{
	int i;
	char *p, *ap, *iflp, *ifname, *ep;
	struct iff ftmp, *iff_obj;
	struct ifc *ifcp;
	struct riprt *rrt;
#if 0
	struct in6_addr gw;
#endif
	u_long plen;

	for (i = 0; i < nfilter; i++) {
		ap = filter[i];
		iflp = NULL;
		ifcp = NULL;
		if (filtertype[i] == 'N' || filtertype[i] == 'T') {
			iflp = ap;
			goto ifonly;
		}
		if ((p = strchr(ap, ',')) != NULL) {
			*p++ = '\0';
			iflp = p;
		}
		if ((p = strchr(ap, '/')) == NULL) {
			log_warnx("no prefixlen specified for '%s'", ap);
			fatalx("exiting");
			/*NOTREACHED*/
		}
		*p++ = '\0';
		if (inet_pton(AF_INET6, ap, &ftmp.iff_addr) != 1) {
			log_warnx("invalid prefix specified for '%s'", ap);
			fatalx("exiting");
			/*NOTREACHED*/
		}
		errno = 0;
		ep = NULL;
		plen = strtoul(p, &ep, 10);
		if (errno || !*p || *ep || plen > sizeof(ftmp.iff_addr) * 8) {
			log_warnx("invalid prefix length specified for '%s'", ap);
			fatalx("exiting");
			/*NOTREACHED*/
		}
		ftmp.iff_plen = plen;
		ftmp.iff_next = NULL;
		applyplen(&ftmp.iff_addr, ftmp.iff_plen);
ifonly:
		ftmp.iff_type = filtertype[i];
		if (iflp == NULL || *iflp == '\0') {
			log_warnx("no interface specified for '%s'", ap);
			fatal("exiting");
			/*NOTREACHED*/
		}
		/* parse the interface listing portion */
		while (iflp) {
			ifname = iflp;
			if ((iflp = strchr(iflp, ',')) != NULL)
				*iflp++ = '\0';
			ifcp = ifc_find(ifname);
			if (ifcp == NULL) {
				log_warnx("no interface %s exists", ifname);
				fatalx("exiting");
				/*NOTREACHED*/
			}
			iff_obj = malloc(sizeof(struct iff));
			if (iff_obj == NULL) {
				fatal("malloc of iff_obj");
				/*NOTREACHED*/
			}
			memcpy((void *)iff_obj, (void *)&ftmp,
			    sizeof(struct iff));
			/* link it to the interface filter */
			iff_obj->iff_next = ifcp->ifc_filter;
			ifcp->ifc_filter = iff_obj;
		}

		/*
		 * -A: aggregate configuration.
		 */
		if (filtertype[i] != 'A')
			continue;
		/* put the aggregate to the kernel routing table */
		rrt = calloc(1, sizeof(struct riprt));
		if (rrt == NULL) {
			fatal("calloc: rrt");
			/*NOTREACHED*/
		}
		rrt->rrt_info.rip6_dest = ftmp.iff_addr;
		rrt->rrt_info.rip6_plen = ftmp.iff_plen;
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
		rrt->rrt_next = riprt;
		riprt = rrt;
		log_debug("Aggregate: %s/%d for %s",
			inet6_n2p(&ftmp.iff_addr), ftmp.iff_plen,
			ifcp->ifc_name);
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
struct ifac *
ifa_match(const struct ifc *ifcp, const struct in6_addr *ia, int plen)
{
	struct ifac *ifa;

	for (ifa = ifcp->ifc_addr; ifa; ifa = ifa->ifa_next) {
		if (IN6_ARE_ADDR_EQUAL(&ifa->ifa_addr, ia) &&
		    ifa->ifa_plen == plen)
			break;
	}
	return ifa;
}

/*
 * Return a pointer to riprt structure whose address and prefix length
 * matches with the address and prefix length found in the argument.
 * Note: This is not a rtalloc().  Therefore exact match is necessary.
 */
struct riprt *
rtsearch(struct	netinfo6 *np, struct riprt **prev_rrt)
{
	struct	riprt	*rrt;

	if (prev_rrt)
		*prev_rrt = NULL;
	for (rrt = riprt; rrt; rrt = rrt->rrt_next) {
		if (rrt->rrt_info.rip6_plen == np->rip6_plen &&
		    IN6_ARE_ADDR_EQUAL(&rrt->rrt_info.rip6_dest,
				       &np->rip6_dest))
			return rrt;
		if (prev_rrt)
			*prev_rrt = rrt;
	}
	if (prev_rrt)
		*prev_rrt = NULL;
	return 0;
}

int
sin6mask2len(const struct sockaddr_in6 *sin6)
{

	return mask2len(&sin6->sin6_addr,
	    sin6->sin6_len - offsetof(struct sockaddr_in6, sin6_addr));
}

int
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

void
applyplen(struct in6_addr *ia, int plen)
{
	static const u_char plent[8] = {
		0x00, 0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe
	};
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

struct in6_addr *
plen2mask(int n)
{
	static const int pl2m[9] = {
		0x00, 0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe, 0xff
	};
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

char *
xstrdup(const char *p)
{
	char *q;

	q = strdup(p);
	if (q == NULL) {
		fatal("strdup");
		/*NOTREACHED*/
	}
	return q;
}

const char *
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

int
ripinterval(int timer)
{
	double r = arc4random();
	int interval;

	interval = (int)(timer + timer * RIPRANDDEV * (r / UINT32_MAX - 0.5));
	nextalarm = time(NULL) + interval;
	return interval;
}

time_t
ripsuptrig(void)
{
	time_t t;

	double r = arc4random();
	t  = (int)(RIP_TRIG_INT6_MIN + 
		(RIP_TRIG_INT6_MAX - RIP_TRIG_INT6_MIN) * (r / UINT32_MAX));
	sup_trig_update = time(NULL) + t;
	return t;
}

unsigned int
if_maxindex(void)
{
	struct if_nameindex *p, *p0;
	unsigned int max = 0;

	p0 = if_nameindex();
	for (p = p0; p && p->if_index && p->if_name; p++) {
		if (max < p->if_index)
			max = p->if_index;
	}
	if_freenameindex(p0);
	return max;
}

struct ifc *
ifc_find(char *name)
{
	struct ifc *ifcp;

	for (ifcp = ifc; ifcp; ifcp = ifcp->ifc_next) {
		if (strcmp(name, ifcp->ifc_name) == 0)
			return ifcp;
	}
	return (struct ifc *)NULL;
}

struct iff *
iff_find(struct ifc *ifcp, int type)
{
	struct iff *iffp;

	for (iffp = ifcp->ifc_filter; iffp; iffp = iffp->iff_next) {
		if (iffp->iff_type == type)
			return iffp;
	}
	return NULL;
}

void
setindex2ifc(int idx, struct ifc *ifcp)
{
	int n;
	struct ifc **p;

	if (!index2ifc) {
		nindex2ifc = 5;	/*initial guess*/
		index2ifc = calloc(nindex2ifc, sizeof(*index2ifc));
		if (index2ifc == NULL) {
			fatal("calloc");
			/*NOTREACHED*/
		}
	}
	n = nindex2ifc;
	while (nindex2ifc <= idx)
		nindex2ifc *= 2;
	if (n != nindex2ifc) {
		p = reallocarray(index2ifc, nindex2ifc, sizeof(*index2ifc));
		if (p == NULL) {
			fatal("reallocarray");
			/*NOTREACHED*/
		}
		memset(p + n, 0, (nindex2ifc - n) * sizeof(*index2ifc));
		index2ifc = p;
	}
	index2ifc[idx] = ifcp;
}
