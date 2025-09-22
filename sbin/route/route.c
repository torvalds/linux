/*	$OpenBSD: route.c,v 1.267 2024/05/09 08:35:40 florian Exp $	*/
/*	$NetBSD: route.c,v 1.16 1996/04/15 18:27:05 cgd Exp $	*/

/*
 * Copyright (c) 1983, 1989, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
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

#include <sys/socket.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netmpls/mpls.h>

#ifdef BFD
#include <sys/time.h>
#include <net/bfd.h>
#endif

#include <arpa/inet.h>
#include <netdb.h>

#include <ifaddrs.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <stdio.h>
#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <paths.h>
#include <err.h>

#include "keywords.h"
#include "show.h"

const struct if_status_description
			if_status_descriptions[] = LINK_STATE_DESCRIPTIONS;

union sockunion so_dst, so_gate, so_mask, so_ifa, so_ifp, so_src, so_label,
    so_source;

typedef union sockunion *sup;
pid_t	pid;
int	rtm_addrs, s;
int	forcehost, forcenet, Fflag, nflag, qflag, tflag, Tflag;
int	iflag, verbose, aflen = sizeof(struct sockaddr_in);
int	locking, lockrest, debugonly;
u_long	mpls_flags = MPLS_OP_LOCAL;
u_long	rtm_inits;
uid_t	uid;
u_int	tableid;

struct rt_metrics	rt_metrics;

int	 flushroutes(int, char **);
int	 newroute(int, char **);
int	 setsource(int, char **);
int	 pushsrc(int, char *, int);
int	 show(int, char *[]);
int	 keycmp(const void *, const void *);
int	 keyword(char *);
void	 monitor(int, char *[]);
int	 nameserver(int, char **);
int	 prefixlen(int, char *);
void	 sockaddr(char *, struct sockaddr *);
void	 sodump(sup, char *);
char	*priorityname(uint8_t);
uint8_t	 getpriority(char *);
void	 print_getmsg(struct rt_msghdr *, int);
#ifdef BFD
const char *bfd_state(unsigned int);
const char *bfd_diag(unsigned int);
const char *bfd_calc_uptime(time_t);
void	 print_bfdmsg(struct rt_msghdr *);
void	 print_sabfd(struct sockaddr_bfd *, int);
#endif
const char *get_linkstate(int, int);
void	 print_rtmsg(struct rt_msghdr *, int);
void	 pmsg_common(struct rt_msghdr *);
void	 pmsg_addrs(char *, int);
void	 bprintf(FILE *, int, char *);
int	 getaddr(int, int, char *, struct hostent **);
void	 getmplslabel(char *, int);
int	 rtmsg(int, int, int, uint8_t);
__dead void usage(char *);
void	 set_metric(char *, int);
void	 inet_makenetandmask(u_int32_t, struct sockaddr_in *, int);
void	 getlabel(char *);
int	 gettable(const char *);
int	 rdomain(int, char **);
void	 print_rtdns(struct sockaddr_rtdns *);
void	 print_rtstatic(struct sockaddr_rtstatic *);
void	 print_rtsearch(struct sockaddr_rtsearch *);
void	 print_80211info(struct if_ieee80211_msghdr *);

__dead void
usage(char *cp)
{
	extern char *__progname;

	if (cp)
		warnx("botched keyword: %s", cp);
	fprintf(stderr,
#ifndef SMALL
	    "usage: %s [-dnqtv] [-T rtable] command [[modifier ...] arg ...]\n",
#else
	    "usage: %s [-dnqtv] command [[modifier ...] arg ...]\n",
#endif
	    __progname);
	exit(1);
}

#define ROUNDUP(a) \
	((a) > 0 ? (1 + (((a) - 1) | (sizeof(long) - 1))) : sizeof(long))
#define ADVANCE(x, n) (x += ROUNDUP((n)->sa_len))

int
main(int argc, char **argv)
{
	unsigned int filter = 0;
	int ch;
	int rval = 0;
	int kw;
	int af = AF_UNSPEC;
#ifndef SMALL
	int Terr = 0;
	u_int rtable_any = RTABLE_ANY;
#endif

	if (argc < 2)
		usage(NULL);

#ifndef SMALL
	tableid = getrtable();
#endif
	while ((ch = getopt(argc, argv, "dnqtT:v")) != -1)
		switch (ch) {
		case 'n':
			nflag = 1;
			break;
		case 'q':
			qflag = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		case 't':
			tflag = 1;
			break;
#ifndef SMALL
		case 'T':
			Terr = gettable(optarg);
			Tflag = 1;
			break;
#endif
		case 'd':
			debugonly = 1;
			break;
		default:
			usage(NULL);
			/* NOTREACHED */
		}
	argc -= optind;
	argv += optind;

	pid = getpid();
	uid = geteuid();
	if (*argv == NULL)
		usage(NULL);

	kw = keyword(*argv);
#ifndef SMALL
	if (Tflag && Terr != 0 && kw != K_ADD) {
		errno = Terr;
		err(1, "routing table %u", tableid);
	}
	if (kw == K_EXEC)
		exit(rdomain(argc - 1, argv + 1));
#endif

	if (kw == K_MONITOR) {
		while (--argc > 0) {
			if (**(++argv)== '-')
				switch (keyword(*argv + 1)) {
				case K_INET:
					af = AF_INET;
					break;
				case K_INET6:
					af = AF_INET6;
					break;
				case K_MPLS:
					af = AF_MPLS;
					break;
				case K_IFACE:
				case K_INTERFACE:
					filter = ROUTE_FILTER(RTM_IFINFO) |
					    ROUTE_FILTER(RTM_IFANNOUNCE) |
					    ROUTE_FILTER(RTM_80211INFO);
					break;
				default:
					usage(*argv);
					/* NOTREACHED */
				}
			else
				usage(*argv);
		}
	}

	if (tflag)
		s = open(_PATH_DEVNULL, O_WRONLY);
	else
		s = socket(AF_ROUTE, SOCK_RAW, af);
	if (s == -1)
		err(1, "socket");

	if (filter != 0) {
		if (setsockopt(s, AF_ROUTE, ROUTE_MSGFILTER, &filter,
		    sizeof(filter)) == -1)
			err(1, "setsockopt(ROUTE_MSGFILTER)");
	}

#ifndef SMALL
	if (!tflag) {
		/* force socket onto table user requested */
		if (Tflag == 1 && Terr == 0) {
			if (setsockopt(s, AF_ROUTE, ROUTE_TABLEFILTER,
			    &tableid, sizeof(tableid)) == -1)
				err(1, "setsockopt(ROUTE_TABLEFILTER)");
		} else {
			if (setsockopt(s, AF_ROUTE, ROUTE_TABLEFILTER,
			    &rtable_any, sizeof(tableid)) == -1)
				err(1, "setsockopt(ROUTE_TABLEFILTER)");
		}
	}
#endif

	if (pledge("stdio dns route", NULL) == -1)
		err(1, "pledge");

	switch (kw) {
	case K_SHOW:
		uid = 0;
		exit(show(argc, argv));
		break;
	case K_FLUSH:
		exit(flushroutes(argc, argv));
		break;
	case K_SOURCEADDR:
		nflag = 1;
		exit(setsource(argc, argv));
		break;
	}

	if (pledge("stdio dns", NULL) == -1)
		err(1, "pledge");

	switch (kw) {
	case K_GET:
		uid = 0;
		/* FALLTHROUGH */
	case K_CHANGE:
	case K_ADD:
	case K_DEL:
	case K_DELETE:
		rval = newroute(argc, argv);
		break;
	case K_MONITOR:
		monitor(argc, argv);
		break;
	case K_NAMESERVER:
		rval = nameserver(argc, argv);
		break;
	default:
		usage(*argv);
		/* NOTREACHED */
	}
	exit(rval);
}

/*
 * Purge all entries in the routing tables not
 * associated with network interfaces.
 */
int
flushroutes(int argc, char **argv)
{
	size_t needed;
	int mib[7], mcnt, rlen, seqno, af = AF_UNSPEC;
	char *buf = NULL, *next, *lim = NULL;
	struct rt_msghdr *rtm;
	struct sockaddr *sa;
	uint8_t prio = 0;
	unsigned int ifindex = 0;

	if (uid)
		errx(1, "must be root to alter routing table");
	shutdown(s, SHUT_RD); /* Don't want to read back our messages */
	while (--argc > 0) {
		if (**(++argv) == '-')
			switch (keyword(*argv + 1)) {
			case K_INET:
				af = AF_INET;
				break;
			case K_INET6:
				af = AF_INET6;
				break;
			case K_LINK:
				af = AF_LINK;
				break;
			case K_MPLS:
				af = AF_MPLS;
				break;
			case K_IFACE:
			case K_INTERFACE:
				if (!--argc)
					usage(1+*argv);
				ifindex = if_nametoindex(*++argv);
				if (ifindex == 0)
					errx(1, "no such interface %s", *argv);
				break;
			case K_PRIORITY:
				if (!--argc)
					usage(1+*argv);
				prio = getpriority(*++argv);
				break;
			default:
				usage(*argv);
				/* NOTREACHED */
			}
		else
			usage(*argv);
	}
	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;		/* protocol */
	mib[3] = af;
	mib[4] = NET_RT_DUMP;
	mib[5] = prio;
	mib[6] = tableid;
	mcnt = 7;

	needed = get_sysctl(mib, mcnt, &buf);
	lim = buf + needed;

	if (pledge("stdio dns", NULL) == -1)
		err(1, "pledge");

	if (verbose) {
		printf("Examining routing table from sysctl\n");
		if (af)
			printf("(address family %s)\n", (*argv + 1));
	}
	if (buf == NULL)
		return (1);

	seqno = 0;
	for (next = buf; next < lim; next += rtm->rtm_msglen) {
		rtm = (struct rt_msghdr *)next;
		if (rtm->rtm_version != RTM_VERSION)
			continue;
		if (verbose)
			print_rtmsg(rtm, rtm->rtm_msglen);
		if ((rtm->rtm_flags & (RTF_GATEWAY|RTF_STATIC|RTF_LLINFO)) == 0)
			continue;
		if ((rtm->rtm_flags & (RTF_LOCAL|RTF_BROADCAST)) != 0)
			continue;
		sa = (struct sockaddr *)(next + rtm->rtm_hdrlen);
		if (ifindex && rtm->rtm_index != ifindex)
			continue;
		if (sa->sa_family == AF_KEY)
			continue;  /* Don't flush SPD */
		if (debugonly)
			continue;
		rtm->rtm_type = RTM_DELETE;
		rtm->rtm_seq = seqno;
		rtm->rtm_tableid = tableid;
		rlen = write(s, next, rtm->rtm_msglen);
		if (rlen < (int)rtm->rtm_msglen) {
			warn("write to routing socket");
			printf("got only %d for rlen\n", rlen);
			break;
		}
		seqno++;
		if (qflag)
			continue;
		if (verbose)
			print_rtmsg(rtm, rlen);
		else {
			struct sockaddr	*mask, *rti_info[RTAX_MAX];

			sa = (struct sockaddr *)(next + rtm->rtm_hdrlen);

			get_rtaddrs(rtm->rtm_addrs, sa, rti_info);

			sa = rti_info[RTAX_DST];
			mask = rti_info[RTAX_NETMASK];

			p_sockaddr(sa, mask, rtm->rtm_flags, 20);
			p_sockaddr(rti_info[RTAX_GATEWAY], NULL, RTF_HOST, 20);
			printf("done\n");
		}
	}
	free(buf);
	return (0);
}

void
set_metric(char *value, int key)
{
	long long relative_expire;
	const char *errstr;
	int flag = 0;

	switch (key) {
	case K_MTU:
		rt_metrics.rmx_mtu = strtonum(value, 0, UINT_MAX, &errstr);
		if (errstr)
			errx(1, "set_metric mtu: %s is %s", value, errstr);
		flag = RTV_MTU;
		break;
	case K_EXPIRE:
		relative_expire = strtonum(value, 0, INT_MAX, &errstr);
		if (errstr)
			errx(1, "set_metric expire: %s is %s", value, errstr);
		rt_metrics.rmx_expire = relative_expire ?
		    relative_expire + time(NULL) : 0;
		flag = RTV_EXPIRE;
		break;
	case K_HOPCOUNT:
	case K_RECVPIPE:
	case K_SENDPIPE:
	case K_SSTHRESH:
	case K_RTT:
	case K_RTTVAR:
		/* no longer used, only for compatibility */
		return;
	default:
		errx(1, "king bula sez: set_metric with invalid key");
	}
	rtm_inits |= flag;
	if (lockrest || locking)
		rt_metrics.rmx_locks |= flag;
	if (locking)
		locking = 0;
}


int
setsource(int argc, char **argv)
{
	struct ifaddrs	*ifap, *ifa = NULL;
	char *cmd;
	int af = AF_UNSPEC, ret = 0, key;
	unsigned int ifindex = 0;

	cmd = argv[0];

	if (argc == 1)
		printsource(AF_UNSPEC, tableid);

	while (--argc > 0) {
		if (**(++argv)== '-') {
			switch (key = keyword(1 + *argv)) {
			case K_INET:
				af = AF_INET;
				aflen = sizeof(struct sockaddr_in);
				break;
			case K_INET6:
				af = AF_INET6;
				aflen = sizeof(struct sockaddr_in6);
				break;
			case K_IFP:
				if (!--argc)
					usage(1+*argv);
				ifindex = if_nametoindex(*++argv);
				if (ifindex == 0)
					errx(1, "no such interface %s", *argv);
				break;
			default:
				usage(NULL);
			}
		} else
			break;
	}

	if (!(argc == 1 && ifindex == 0) && !(argc == 0 && ifindex != 0))
		usage(NULL);

	if (uid)
		errx(1, "must be root to alter source address");

	if (ifindex) {
		if (getifaddrs(&ifap) == -1)
			err(1, "getifaddrs");
		for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
			if (if_nametoindex(ifa->ifa_name) != ifindex)
				continue;
			if (ifa->ifa_addr == NULL ||
			    !(ifa->ifa_addr->sa_family == AF_INET ||
			    ifa->ifa_addr->sa_family == AF_INET6))
				continue;
			if ((af != AF_UNSPEC) &&
			    (ifa->ifa_addr->sa_family != af))
				continue;
			if (ifa->ifa_addr->sa_family == AF_INET6) {
				struct sockaddr_in6 *sin6 =
				    (struct sockaddr_in6 *)ifa->ifa_addr;
				if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr) ||
				    IN6_IS_ADDR_MULTICAST(&sin6->sin6_addr))
					continue;
			}
			if (pushsrc(*cmd, routename(ifa->ifa_addr),
			    ifa->ifa_addr->sa_family))
				break;
		}
		freeifaddrs(ifap);
	} else {
		ret = pushsrc(*cmd, *argv, af);
	}

	return (ret != 0);
}

int
pushsrc(int cmd, char *src, int af)
{
	int ret = 0;

	getaddr(RTA_IFA, af, src, NULL);

	errno = 0;
	ret = rtmsg(cmd, 0, 0, 0);
	if (!qflag && ret != 0)
		printf("sourceaddr %s: %s\n", src, strerror(errno));

	return (ret);
}
int
newroute(int argc, char **argv)
{
	char *cmd, *dest = "", *gateway = "", *error;
	int ishost = 0, ret = 0, attempts, oerrno, flags = RTF_STATIC;
	int fmask = 0, af = AF_UNSPEC;
	int key;
	uint8_t prio = 0;
	struct hostent *hp = NULL;
	int sawdest = 0;

	if (uid)
		errx(1, "must be root to alter routing table");
	cmd = argv[0];
	if (*cmd != 'g')
		shutdown(s, SHUT_RD); /* Don't want to read back our messages */
	while (--argc > 0) {
		if (**(++argv) == '-') {
			switch (key = keyword(1 + *argv)) {
			case K_LINK:
				af = AF_LINK;
				aflen = sizeof(struct sockaddr_dl);
				break;
			case K_INET:
				af = AF_INET;
				aflen = sizeof(struct sockaddr_in);
				break;
			case K_INET6:
				af = AF_INET6;
				aflen = sizeof(struct sockaddr_in6);
				break;
			case K_SA:
				af = PF_ROUTE;
				aflen = sizeof(struct sockaddr_storage) - 1;
				break;
			case K_MPLS:
				af = AF_MPLS;
				aflen = sizeof(struct sockaddr_mpls);
				fmask |= RTF_MPLS;
				break;
			case K_MPLSLABEL:
				if (!--argc)
					usage(1+*argv);
				if (af != AF_INET && af != AF_INET6)
					errx(1, "-mplslabel requires "
					    "-inet or -inet6");
				getmplslabel(*++argv, 0);
				mpls_flags = MPLS_OP_PUSH;
				flags |= RTF_MPLS;
				break;
			case K_IN:
				if (!--argc)
					usage(1+*argv);
				if (af != AF_MPLS)
					errx(1, "-in requires -mpls");
				getmplslabel(*++argv, 1);
				break;
			case K_OUT:
				if (!--argc)
					usage(1+*argv);
				if (af != AF_MPLS)
					errx(1, "-out requires -mpls");
				if (mpls_flags == MPLS_OP_LOCAL)
					errx(1, "-out requires -push, -pop, "
					    "-swap");
				getmplslabel(*++argv, 0);
				flags |= RTF_MPLS;
				break;
			case K_POP:
				if (af != AF_MPLS)
					errx(1, "-pop requires -mpls");
				mpls_flags = MPLS_OP_POP;
				break;
			case K_PUSH:
				if (af != AF_MPLS)
					errx(1, "-push requires -mpls");
				mpls_flags = MPLS_OP_PUSH;
				break;
			case K_SWAP:
				if (af != AF_MPLS)
					errx(1, "-swap requires -mpls");
				mpls_flags = MPLS_OP_SWAP;
				break;
			case K_IFACE:
			case K_INTERFACE:
				iflag++;
				break;
			case K_NOSTATIC:
				flags &= ~RTF_STATIC;
				break;
			case K_LLINFO:
				flags |= RTF_LLINFO;
				break;
			case K_LOCK:
				locking = 1;
				break;
			case K_LOCKREST:
				lockrest = 1;
				break;
			case K_HOST:
				forcehost++;
				break;
			case K_REJECT:
				flags |= RTF_REJECT;
				break;
			case K_BLACKHOLE:
				flags |= RTF_BLACKHOLE;
				break;
			case K_PROTO1:
				flags |= RTF_PROTO1;
				break;
			case K_PROTO2:
				flags |= RTF_PROTO2;
				break;
			case K_CLONING:
				flags |= RTF_CLONING;
				break;
			case K_STATIC:
				flags |= RTF_STATIC;
				break;
			case K_IFA:
				if (!--argc)
					usage(1+*argv);
				getaddr(RTA_IFA, af, *++argv, NULL);
				break;
			case K_IFP:
				if (!--argc)
					usage(1+*argv);
				getaddr(RTA_IFP, AF_LINK, *++argv, NULL);
				break;
			case K_GATEWAY:
				if (!--argc)
					usage(1+*argv);
				getaddr(RTA_GATEWAY, af, *++argv, NULL);
				gateway = *argv;
				break;
			case K_DST:
				if (!--argc)
					usage(1+*argv);
				ishost = getaddr(RTA_DST, af, *++argv, &hp);
				dest = *argv;
				sawdest = 1;
				break;
			case K_LABEL:
				if (!--argc)
					usage(1+*argv);
				getlabel(*++argv);
				break;
			case K_NETMASK:
				if (!sawdest)
					errx(1, "-netmask must follow "
					    "destination parameter");
				if (!--argc)
					usage(1+*argv);
				getaddr(RTA_NETMASK, af, *++argv, NULL);
				/* FALLTHROUGH */
			case K_NET:
				forcenet++;
				break;
			case K_PREFIXLEN:
				if (!sawdest)
					errx(1, "-prefixlen must follow "
					    "destination parameter");
				if (!--argc)
					usage(1+*argv);
				ishost = prefixlen(af, *++argv);
				break;
			case K_MPATH:
				flags |= RTF_MPATH;
				break;
			case K_MTU:
			case K_HOPCOUNT:
			case K_EXPIRE:
			case K_RECVPIPE:
			case K_SENDPIPE:
			case K_SSTHRESH:
			case K_RTT:
			case K_RTTVAR:
				if (!--argc)
					usage(1+*argv);
				set_metric(*++argv, key);
				break;
			case K_PRIORITY:
				if (!--argc)
					usage(1+*argv);
				prio = getpriority(*++argv);
				break;
			case K_BFD:
				flags |= RTF_BFD;
				fmask |= RTF_BFD;
				break;
			case K_NOBFD:
				flags &= ~RTF_BFD;
				fmask |= RTF_BFD;
				break;
			default:
				usage(1+*argv);
				/* NOTREACHED */
			}
		} else {
			if ((rtm_addrs & RTA_DST) == 0) {
				dest = *argv;
				sawdest = 1;
				ishost = getaddr(RTA_DST, af, *argv, &hp);
			} else if ((rtm_addrs & RTA_GATEWAY) == 0) {
				gateway = *argv;
				getaddr(RTA_GATEWAY, af, *argv, &hp);
			} else
				usage(NULL);
		}
	}
	if (forcehost)
		ishost = 1;
	if (forcenet)
		ishost = 0;
	if (forcenet && !(rtm_addrs & RTA_NETMASK))
		errx(1, "netmask missing");
	flags |= RTF_UP;
	if (ishost)
		flags |= RTF_HOST;
	if (iflag == 0)
		flags |= RTF_GATEWAY;
	for (attempts = 1; ; attempts++) {
		errno = 0;
		if ((ret = rtmsg(*cmd, flags, fmask, prio)) == 0)
			break;
		if (errno != ENETUNREACH && errno != ESRCH)
			break;
		if (af == AF_INET && *gateway && hp && hp->h_addr_list[1]) {
			hp->h_addr_list++;
			memcpy(&so_gate.sin.sin_addr, hp->h_addr_list[0],
			    hp->h_length);
		} else
			break;
	}
	oerrno = errno;
	if (!qflag && (*cmd != 'g' || ret != 0)) {
		printf("%s %s %s", cmd, ishost ? "host" : "net", dest);
		if (*gateway) {
			printf(": gateway %s", gateway);
			if (attempts > 1 && ret == 0 && af == AF_INET)
			    printf(" (%s)", inet_ntoa(so_gate.sin.sin_addr));
		}
		if (ret == 0)
			printf("\n");
		if (ret != 0) {
			switch (oerrno) {
			case ESRCH:
				error = "not in table";
				break;
			case EBUSY:
				error = "entry in use";
				break;
			case ENOBUFS:
				error = "routing table overflow";
				break;
			default:
				error = strerror(oerrno);
				break;
			}
			printf(": %s\n", error);
		}
	}
	return (ret != 0);
}

int
show(int argc, char *argv[])
{
	int		 af = AF_UNSPEC;
	char		 prio = 0;

	while (--argc > 0) {
		if (**(++argv)== '-')
			switch (keyword(*argv + 1)) {
			case K_INET:
				af = AF_INET;
				break;
			case K_INET6:
				af = AF_INET6;
				break;
			case K_LINK:
				af = AF_LINK;
				break;
			case K_MPLS:
				af = AF_MPLS;
				break;
			case K_GATEWAY:
				Fflag = 1;
				break;
			case K_LABEL:
				if (!--argc)
					usage(1+*argv);
				getlabel(*++argv);
				break;
			case K_PRIORITY:
				if (!--argc)
					usage(1+*argv);
				prio = getpriority(*++argv);
				break;
			default:
				usage(*argv);
				/* NOTREACHED */
			}
		else
			usage(*argv);
	}

	p_rttables(af, tableid, prio);
	return (0);
}

void
inet_makenetandmask(u_int32_t net, struct sockaddr_in *sin, int bits)
{
	u_int32_t mask;

	rtm_addrs |= RTA_NETMASK;
	if (bits == 0 && net == 0)
		mask = 0;
	else {
		if (bits == 0)
			bits = 32;
		mask = 0xffffffff << (32 - bits);
		net &= mask;
	}
	sin->sin_addr.s_addr = htonl(net);
	sin = &so_mask.sin;
	sin->sin_addr.s_addr = htonl(mask);
	sin->sin_family = AF_INET;
	sin->sin_len = sizeof(struct sockaddr_in);
}

/*
 * Interpret an argument as a network address of some kind,
 * returning 1 if a host address, 0 if a network address.
 */
int
getaddr(int which, int af, char *s, struct hostent **hpp)
{
	sup su = NULL;
	struct hostent *hp;
	int aflength, afamily, bits;

	if (af == AF_UNSPEC) {
		if (strchr(s, ':') != NULL) {
			af = AF_INET6;
			aflen = sizeof(struct sockaddr_in6);
		} else {
			af = AF_INET;
			aflen = sizeof(struct sockaddr_in);
		}
	}
	/* local copy of len and af so we can change it */
	aflength = aflen;
	afamily = af;

	rtm_addrs |= which;
	switch (which) {
	case RTA_DST:
		su = &so_dst;
		break;
	case RTA_GATEWAY:
		su = &so_gate;
		break;
	case RTA_NETMASK:
		su = &so_mask;
		break;
	case RTA_IFP:
		su = &so_ifp;
		aflength = sizeof(struct sockaddr_dl);
		afamily = AF_LINK;
		break;
	case RTA_IFA:
		su = &so_ifa;
		break;
	default:
		errx(1, "internal error");
		/* NOTREACHED */
	}
	memset(su, 0, sizeof(union sockunion));
	su->sa.sa_len = aflength;
	su->sa.sa_family = afamily;

	if (strcmp(s, "default") == 0) {
		switch (which) {
		case RTA_DST:
			forcenet++;
			getaddr(RTA_NETMASK, af, s, NULL);
			break;
		case RTA_NETMASK:
			su->sa.sa_len = 0;
		}
		return (0);
	}

	switch (afamily) {
	case AF_INET6:
	    {
		struct addrinfo hints, *res;
		char            buf[
		   sizeof("xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:255:255:255:255/128")
		];
		char           *sep;
		int             error;

		if (strlcpy(buf, s, sizeof buf) >= sizeof buf) {
			errx(1, "%s: bad value", s);
		}

		sep = strchr(buf, '/');
		if (sep != NULL)
			*sep++ = '\0';
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = afamily;	/*AF_INET6*/
		hints.ai_flags = AI_NUMERICHOST;
		hints.ai_socktype = SOCK_DGRAM;		/*dummy*/
		error = getaddrinfo(buf, "0", &hints, &res);
		if (error) {
			hints.ai_flags = 0;
			error = getaddrinfo(buf, "0", &hints, &res);
			if (error)
				errx(1, "%s: %s", s, gai_strerror(error));
		}
		if (res->ai_next)
			errx(1, "%s: resolved to multiple values", s);
		memcpy(&su->sin6, res->ai_addr, sizeof(su->sin6));
		freeaddrinfo(res);
#ifdef __KAME__
		if ((IN6_IS_ADDR_LINKLOCAL(&su->sin6.sin6_addr) ||
		     IN6_IS_ADDR_MC_LINKLOCAL(&su->sin6.sin6_addr) ||
		     IN6_IS_ADDR_MC_INTFACELOCAL(&su->sin6.sin6_addr)) &&
		    su->sin6.sin6_scope_id) {
			*(u_int16_t *)&su->sin6.sin6_addr.s6_addr[2] =
				htons(su->sin6.sin6_scope_id);
			su->sin6.sin6_scope_id = 0;
		}
#endif
		if (hints.ai_flags == AI_NUMERICHOST) {
			if (which == RTA_DST) {
				if (sep == NULL && su->sin6.sin6_scope_id == 0 &&
				    IN6_IS_ADDR_UNSPECIFIED(&su->sin6.sin6_addr))
					sep = "0";
				if (sep == NULL || prefixlen(AF_INET6, sep))
					return (1);
			}
			return (0);
		} else
			return (1);
	    }

	case AF_LINK:
		su->sdl.sdl_index = if_nametoindex(s);
		memset(&su->sdl.sdl_data, 0, sizeof(su->sdl.sdl_data));
		return (1);
	case AF_MPLS:
		errx(1, "mpls labels require -in or -out switch");
	case PF_ROUTE:
		su->sa.sa_len = sizeof(struct sockaddr_storage) - 1;
		sockaddr(s, &su->sa);
		return (1);

	case AF_INET:
		if (hpp != NULL)
			*hpp = NULL;
		if (which == RTA_DST && !forcehost) {
			bits = inet_net_pton(AF_INET, s, &su->sin.sin_addr,
			    sizeof(su->sin.sin_addr));
			if (bits == 32)
				return (1);
			if (bits >= 0) {
				inet_makenetandmask(ntohl(
				    su->sin.sin_addr.s_addr),
				    &su->sin, bits);
				return (0);
			}
		} else if (which != RTA_DST || !forcenet)
			if (inet_pton(AF_INET, s, &su->sin.sin_addr) == 1)
				return (1);
		hp = gethostbyname(s);
		if (hp == NULL)
			errx(1, "%s: bad address", s);
		if (hpp != NULL)
			*hpp = hp;
		su->sin.sin_addr = *(struct in_addr *)hp->h_addr;
		return (1);

	default:
		errx(1, "%d: bad address family", afamily);
		/* NOTREACHED */
	}
}

void
getmplslabel(char *s, int in)
{
	sup su = NULL;
	const char *errstr;
	u_int32_t label;

	label = strtonum(s, 0, MPLS_LABEL_MAX, &errstr);
	if (errstr)
		errx(1, "bad label: %s is %s", s, errstr);
	if (in) {
		rtm_addrs |= RTA_DST;
		su = &so_dst;
		su->smpls.smpls_label = htonl(label << MPLS_LABEL_OFFSET);
	} else {
		rtm_addrs |= RTA_SRC;
		su = &so_src;
		su->smpls.smpls_label = htonl(label << MPLS_LABEL_OFFSET);
	}

	su->sa.sa_len = sizeof(struct sockaddr_mpls);
	su->sa.sa_family = AF_MPLS;
}

int
prefixlen(int af, char *s)
{
	const char *errstr;
	int len, q, r;
	int max;

	switch (af) {
	case AF_INET:
		max = sizeof(struct in_addr) * 8;
		break;
	case AF_INET6:
		max = sizeof(struct in6_addr) * 8;
		break;
	default:
		errx(1, "prefixlen is not supported with af %d", af);
		/* NOTREACHED */
	}

	rtm_addrs |= RTA_NETMASK;
	len = strtonum(s, 0, max, &errstr);
	if (errstr)
		errx(1, "prefixlen %s is %s", s, errstr);

	q = len >> 3;
	r = len & 7;
	switch (af) {
	case AF_INET:
		memset(&so_mask, 0, sizeof(so_mask));
		so_mask.sin.sin_family = AF_INET;
		so_mask.sin.sin_len = sizeof(struct sockaddr_in);
		if (len != 0)
			so_mask.sin.sin_addr.s_addr = htonl(0xffffffff << (32 - len));
		break;
	case AF_INET6:
		so_mask.sin6.sin6_family = AF_INET6;
		so_mask.sin6.sin6_len = sizeof(struct sockaddr_in6);
		memset((void *)&so_mask.sin6.sin6_addr, 0,
			sizeof(so_mask.sin6.sin6_addr));
		if (q > 0)
			memset((void *)&so_mask.sin6.sin6_addr, 0xff, q);
		if (r > 0)
			*((u_char *)&so_mask.sin6.sin6_addr + q) =
			    (0xff00 >> r) & 0xff;
		break;
	}
	return (len == max);
}

void
monitor(int argc, char *argv[])
{
	int n;
	char msg[2048];
	time_t now;
	char *ct;

	verbose = 1;
	for (;;) {
		if ((n = read(s, msg, sizeof(msg))) == -1) {
			if (errno == EINTR)
				continue;
			err(1, "read");
		}
		now = time(NULL);
		ct = ctime(&now);
		if (ct)
			printf("got message of size %d on %s", n, ct);
		else
			printf("got message of size %d on %lld\n", n, now);
		print_rtmsg((struct rt_msghdr *)msg, n);
	}
}


int
nameserver(int argc, char *argv[])
{
	struct rt_msghdr         rtm;
	struct sockaddr_rtdns    rtdns;
	struct iovec             iov[3];
	struct addrinfo	 hints, *res;
	struct in_addr           ns4[5];
	struct in6_addr          ns6[5];
	size_t			 ns4_count = 0, ns6_count = 0;
	long			 pad = 0;
	unsigned int		 if_index;
	int			 error = 0, iovcnt = 0, padlen, i;
	char			*if_name, buf[INET6_ADDRSTRLEN];


	argc--;
	argv++;
	if (argc == 0)
		usage(NULL);

	if_name = *argv;
	argc--;
	argv++;

	if ((if_index = if_nametoindex(if_name)) == 0)
		errx(1, "unknown interface: %s", if_name);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;

#ifndef nitems
#define nitems(_a)      (sizeof((_a)) / sizeof((_a)[0]))
#endif

	for (; argc > 0 && ns4_count + ns6_count < 5; argc--, argv++) {
		error = getaddrinfo(*argv, NULL, &hints, &res);
		if (error) {
			errx(1, "%s", gai_strerror(error));
		}
		if (res == NULL) {
			errx(1, "%s: unknown", *argv);
		}

		switch (res->ai_addr->sa_family) {
		case AF_INET:
			memcpy(&ns4[ns4_count++],
			    &((struct sockaddr_in *)res->ai_addr)->sin_addr,
			    sizeof(struct in_addr));
			break;
		case AF_INET6:
			memcpy(&ns6[ns6_count++],
			    &((struct sockaddr_in6 *)res->ai_addr)->sin6_addr,
			    sizeof(struct in6_addr));
			break;
		default:
			errx(1, "unknown address family");
		}
		freeaddrinfo(res);
	}

	if (argc > 0)
		warnx("ignoring additional nameservers");

	if (verbose) {
		for (i = 0; i < ns4_count; i++)
			warnx("v4: %s", inet_ntop(AF_INET, &ns4[i], buf,
			    sizeof(buf)));
		for (i = 0; i < ns6_count; i++)
			warnx("v6: %s", inet_ntop(AF_INET6, &ns6[i], buf,
			    sizeof(buf)));
	}

	memset(&rtm, 0, sizeof(rtm));

	rtm.rtm_version = RTM_VERSION;
	rtm.rtm_type = RTM_PROPOSAL;
	rtm.rtm_msglen = sizeof(rtm);
	rtm.rtm_tableid = tableid;
	rtm.rtm_index = if_index;
	rtm.rtm_seq = 1;
	rtm.rtm_priority = RTP_PROPOSAL_STATIC;
	rtm.rtm_addrs = RTA_DNS;
	rtm.rtm_flags = RTF_UP;

	iov[iovcnt].iov_base = &rtm;
	iov[iovcnt++].iov_len = sizeof(rtm);

	iov[iovcnt].iov_base = &rtdns;
	iov[iovcnt++].iov_len = sizeof(rtdns);
	rtm.rtm_msglen += sizeof(rtdns);

	padlen = ROUNDUP(sizeof(rtdns)) - sizeof(rtdns);
	if (padlen > 0) {
		iov[iovcnt].iov_base = &pad;
		iov[iovcnt++].iov_len = padlen;
		rtm.rtm_msglen += padlen;
	}

	memset(&rtdns, 0, sizeof(rtdns));
	rtdns.sr_family = AF_INET;
	rtdns.sr_len = 2 + ns4_count * sizeof(struct in_addr);
	memcpy(rtdns.sr_dns, ns4, rtdns.sr_len - 2);

	if (debugonly)
		return (0);

	if (writev(s, iov, iovcnt) == -1) {
		warn("failed to send route message");
		error = 1;
	}

	rtm.rtm_seq++;

	memset(&rtdns, 0, sizeof(rtdns));
	rtdns.sr_family = AF_INET6;
	rtdns.sr_len = 2 + ns6_count * sizeof(struct in6_addr);
	memcpy(rtdns.sr_dns, ns6, rtdns.sr_len - 2);

	if (writev(s, iov, iovcnt) == -1) {
		warn("failed to send route message");
		error = 1;
	}

	return (error);
}

struct {
	struct rt_msghdr	m_rtm;
	char			m_space[512];
} m_rtmsg;

int
rtmsg(int cmd, int flags, int fmask, uint8_t prio)
{
	static int seq;
	char *cp = m_rtmsg.m_space;
	int l;

#define NEXTADDR(w, u)				\
	if (rtm_addrs & (w)) {			\
		l = ROUNDUP(u.sa.sa_len);	\
		memcpy(cp, &(u), l);		\
		cp += l;			\
		if (verbose)			\
			sodump(&(u), #u);	\
	}

	errno = 0;
	memset(&m_rtmsg, 0, sizeof(m_rtmsg));
	if (cmd == 'a')
		cmd = RTM_ADD;
	else if (cmd == 'c')
		cmd = RTM_CHANGE;
	else if (cmd == 'g') {
		cmd = RTM_GET;
		if (so_ifp.sa.sa_family == AF_UNSPEC) {
			so_ifp.sa.sa_family = AF_LINK;
			so_ifp.sa.sa_len = sizeof(struct sockaddr_dl);
			rtm_addrs |= RTA_IFP;
		}
	} else if (cmd == 's') {
		cmd = RTM_SOURCE;
	} else
		cmd = RTM_DELETE;
#define rtm m_rtmsg.m_rtm
	rtm.rtm_type = cmd;
	rtm.rtm_flags = flags;
	rtm.rtm_fmask = fmask;
	rtm.rtm_version = RTM_VERSION;
	rtm.rtm_seq = ++seq;
	rtm.rtm_addrs = rtm_addrs;
	rtm.rtm_rmx = rt_metrics;
	rtm.rtm_inits = rtm_inits;
	rtm.rtm_tableid = tableid;
	rtm.rtm_priority = prio;
	rtm.rtm_mpls = mpls_flags;
	rtm.rtm_hdrlen = sizeof(rtm);

	/* store addresses in ascending order of RTA values */
	NEXTADDR(RTA_DST, so_dst);
	NEXTADDR(RTA_GATEWAY, so_gate);
	NEXTADDR(RTA_NETMASK, so_mask);
	NEXTADDR(RTA_IFP, so_ifp);
	NEXTADDR(RTA_IFA, so_ifa);
	NEXTADDR(RTA_SRC, so_src);
	NEXTADDR(RTA_LABEL, so_label);
	rtm.rtm_msglen = l = cp - (char *)&m_rtmsg;
	if (verbose)
		print_rtmsg(&rtm, l);
	if (debugonly)
		return (0);
	if (write(s, &m_rtmsg, l) != l) {
		return (-1);
	}
	if (cmd == RTM_GET) {
		do {
			l = read(s, &m_rtmsg, sizeof(m_rtmsg));
		} while (l > 0 && (rtm.rtm_version != RTM_VERSION ||
		    rtm.rtm_seq != seq || rtm.rtm_pid != pid));
		if (l == -1)
			warn("read from routing socket");
		else
			print_getmsg(&rtm, l);
	}
#undef rtm
	return (0);
}

char *msgtypes[] = {
	"",
	"RTM_ADD: Add Route",
	"RTM_DELETE: Delete Route",
	"RTM_CHANGE: Change Metrics or flags",
	"RTM_GET: Report Metrics",
	"RTM_LOSING: Kernel Suspects Partitioning",
	"RTM_REDIRECT: Told to use different route",
	"RTM_MISS: Lookup failed on this address",
	"",
	"",
	"",
	"RTM_RESOLVE: Route created by cloning",
	"RTM_NEWADDR: address being added to iface",
	"RTM_DELADDR: address being removed from iface",
	"RTM_IFINFO: iface status change",
	"RTM_IFANNOUNCE: iface arrival/departure",
	"RTM_DESYNC: route socket overflow",
	"RTM_INVALIDATE: invalidate cache of L2 route",
	"RTM_BFD: bidirectional forwarding detection",
	"RTM_PROPOSAL: config proposal",
	"RTM_CHGADDRATTR: address attributes being changed",
	"RTM_80211INFO: 802.11 iface status change"
};

char metricnames[] =
"\011priority\010rttvar\7rtt\6ssthresh\5sendpipe\4recvpipe\3expire\2hopcount\1mtu";
char routeflags[] =
"\1UP\2GATEWAY\3HOST\4REJECT\5DYNAMIC\6MODIFIED\7DONE\010XMASK_PRESENT"
"\011CLONING\012MULTICAST\013LLINFO\014STATIC\015BLACKHOLE\016PROTO3\017PROTO2"
"\020PROTO1\021CLONED\022CACHED\023MPATH\025MPLS\026LOCAL\027BROADCAST"
"\030CONNECTED\031BFD";
char ifnetflags[] =
"\1UP\2BROADCAST\3DEBUG\4LOOPBACK\5PTP\6STATICARP\7RUNNING\010NOARP\011PPROMISC"
"\012ALLMULTI\013OACTIVE\014SIMPLEX\015LINK0\016LINK1\017LINK2\020MULTICAST"
"\23AUTOCONF6TEMP\24MPLS\25WOL\26AUTOCONF6\27INET6_NOSOII\30AUTOCONF4";
char addrnames[] =
"\1DST\2GATEWAY\3NETMASK\4GENMASK\5IFP\6IFA\7AUTHOR\010BRD\011SRC\012SRCMASK\013LABEL\014BFD\015DNS\016STATIC\017SEARCH";
char ieee80211flags[] =
    "\1ASCAN\2SIBSS\011WEPON\012IBSSON\013PMGTON\014DESBSSID\016ROAMING"
    "\020TXPOW_FIXED\021TXPOW_AUTO\022SHSLOT\023SHPREAMBLE\024QOS"
    "\025USEPROT\026RSNON\027PSK\030COUNTERM\031MFPR\032HTON\033PBAR"
    "\034BGSCAN\035AUTO_JOIN\036VHTON";
char ieee80211xflags[] =
    "\1TX_MGMT_ONLY";

const char *
get_linkstate(int mt, int link_state)
{
	const struct if_status_description *p;
	static char buf[8];

	for (p = if_status_descriptions; p->ifs_string != NULL; p++) {
		if (LINK_STATE_DESC_MATCH(p, mt, link_state))
			return (p->ifs_string);
	}
	snprintf(buf, sizeof(buf), "[#%d]", link_state);
	return buf;
}

void
print_rtmsg(struct rt_msghdr *rtm, int msglen)
{
	long long relative_expire;
	struct if_msghdr *ifm;
	struct ifa_msghdr *ifam;
	struct if_announcemsghdr *ifan;
	char ifname[IF_NAMESIZE];

	if (verbose == 0)
		return;
	if (rtm->rtm_version != RTM_VERSION) {
		warnx("routing message version %u not understood",
		    rtm->rtm_version);
		return;
	}
	if (rtm->rtm_type > 0 &&
	    rtm->rtm_type < sizeof(msgtypes)/sizeof(msgtypes[0]))
		printf("%s", msgtypes[rtm->rtm_type]);
	else
		printf("[rtm_type %u out of range]", rtm->rtm_type);

	printf(": len %u", rtm->rtm_msglen);
	switch (rtm->rtm_type) {
	case RTM_DESYNC:
		printf("\n");
		break;
	case RTM_IFINFO:
		ifm = (struct if_msghdr *)rtm;
		printf(", if# %u, ", ifm->ifm_index);
		if (if_indextoname(ifm->ifm_index, ifname) != NULL)
			printf("name %s, ", ifname);
		printf("link: %s, mtu: %u, flags:",
		    get_linkstate(ifm->ifm_data.ifi_type,
		        ifm->ifm_data.ifi_link_state),
		    ifm->ifm_data.ifi_mtu);
		bprintf(stdout, ifm->ifm_flags | (ifm->ifm_xflags << 16),
		    ifnetflags);
		pmsg_addrs((char *)ifm + ifm->ifm_hdrlen, ifm->ifm_addrs);
		break;
	case RTM_80211INFO:
		printf(", if# %u, ", rtm->rtm_index);
		if (if_indextoname(rtm->rtm_index, ifname) != NULL)
			printf("name %s, ", ifname);
		print_80211info((struct if_ieee80211_msghdr *)rtm);
		break;
	case RTM_NEWADDR:
	case RTM_DELADDR:
	case RTM_CHGADDRATTR:
		ifam = (struct ifa_msghdr *)rtm;
		printf(", if# %u, ", ifam->ifam_index);
		if (if_indextoname(ifam->ifam_index, ifname) != NULL)
			printf("name %s, ", ifname);
		printf("metric %d, flags:", ifam->ifam_metric);
		bprintf(stdout, ifam->ifam_flags, routeflags);
		pmsg_addrs((char *)ifam + ifam->ifam_hdrlen, ifam->ifam_addrs);
		break;
	case RTM_IFANNOUNCE:
		ifan = (struct if_announcemsghdr *)rtm;
		printf(", if# %u, name %s, what: ",
		    ifan->ifan_index, ifan->ifan_name);
		switch (ifan->ifan_what) {
		case IFAN_ARRIVAL:
			printf("arrival");
			break;
		case IFAN_DEPARTURE:
			printf("departure");
			break;
		default:
			printf("#%u", ifan->ifan_what);
			break;
		}
		printf("\n");
		break;
#ifdef BFD
	case RTM_BFD:
		print_bfdmsg(rtm);
		break;
#endif
	case RTM_PROPOSAL:
		printf(", source ");
		switch (rtm->rtm_priority) {
		case RTP_PROPOSAL_STATIC:
			printf("static");
			break;
		case RTP_PROPOSAL_DHCLIENT:
			printf("dhcp");
			break;
		case RTP_PROPOSAL_SLAAC:
			printf("slaac");
			break;
		case RTP_PROPOSAL_UMB:
			printf("umb");
			break;
		case RTP_PROPOSAL_PPP:
			printf("ppp");
			break;
		case RTP_PROPOSAL_SOLICIT:
			printf("solicit");
			break;
		default:
			printf("unknown");
			break;
		}
		printf(", table %u, if# %u, ",
		    rtm->rtm_tableid, rtm->rtm_index);
		if (if_indextoname(rtm->rtm_index, ifname) != NULL)
			printf("name %s, ", ifname);
		printf("pid: %ld, seq %d, errno %d\nflags:",
		    (long)rtm->rtm_pid, rtm->rtm_seq, rtm->rtm_errno);
		bprintf(stdout, rtm->rtm_flags, routeflags);
		printf("\nfmask:");
		bprintf(stdout, rtm->rtm_fmask, routeflags);
		if (verbose) {
#define lock(f)	((rtm->rtm_rmx.rmx_locks & __CONCAT(RTV_,f)) ? 'L' : ' ')
			relative_expire = rtm->rtm_rmx.rmx_expire ?
			    rtm->rtm_rmx.rmx_expire - time(NULL) : 0;
			printf("\nuse: %8llu   mtu: %8u%c   expire: %8lld%c",
			    rtm->rtm_rmx.rmx_pksent,
			    rtm->rtm_rmx.rmx_mtu, lock(MTU),
			    relative_expire, lock(EXPIRE));
#undef lock
		}
		printf("\nlocks: ");
		bprintf(stdout, rtm->rtm_rmx.rmx_locks, metricnames);
		printf(" inits: ");
		bprintf(stdout, rtm->rtm_inits, metricnames);
		pmsg_addrs(((char *)rtm + rtm->rtm_hdrlen),
		    rtm->rtm_addrs & ~(RTA_STATIC | RTA_SEARCH | RTA_DNS));

		if(!(rtm->rtm_addrs & (RTA_STATIC | RTA_SEARCH | RTA_DNS)))
			break;

		printf("proposals: ");
		bprintf(stdout, rtm->rtm_addrs & (RTA_STATIC | RTA_SEARCH |
		    RTA_DNS), addrnames);
		putchar('\n');

		if (rtm->rtm_addrs & RTA_STATIC) {
			char *next = (char *)rtm + rtm->rtm_hdrlen;
			struct sockaddr	*sa, *rti_info[RTAX_MAX];
			struct sockaddr_rtstatic *rtstatic;
			sa = (struct sockaddr *)next;
			get_rtaddrs(rtm->rtm_addrs, sa, rti_info);
			rtstatic = (struct sockaddr_rtstatic *)
			    rti_info[RTAX_STATIC];
			if (rtstatic != NULL)
				print_rtstatic(rtstatic);
		}

		if (rtm->rtm_addrs & RTA_SEARCH) {
			char *next = (char *)rtm + rtm->rtm_hdrlen;
			struct sockaddr	*sa, *rti_info[RTAX_MAX];
			struct sockaddr_rtsearch *rtsearch;
			sa = (struct sockaddr *)next;
			get_rtaddrs(rtm->rtm_addrs, sa, rti_info);
			rtsearch = (struct sockaddr_rtsearch *)
			    rti_info[RTAX_SEARCH];
			if (rtsearch != NULL)
				print_rtsearch(rtsearch);
		}

		if (rtm->rtm_addrs & RTA_DNS) {
			char *next = (char *)rtm + rtm->rtm_hdrlen;
			struct sockaddr	*sa, *rti_info[RTAX_MAX];
			struct sockaddr_rtdns *rtdns;
			sa = (struct sockaddr *)next;
			get_rtaddrs(rtm->rtm_addrs, sa, rti_info);
			rtdns = (struct sockaddr_rtdns *)rti_info[RTAX_DNS];
			if (rtdns != NULL)
				print_rtdns(rtdns);
		}
		putchar('\n');
		break;
	default:
		printf(", priority %u, table %u, if# %u, ",
		    rtm->rtm_priority, rtm->rtm_tableid, rtm->rtm_index);
		if (if_indextoname(rtm->rtm_index, ifname) != NULL)
			printf("name %s, ", ifname);
		printf("pid: %ld, seq %d, errno %d\nflags:",
		    (long)rtm->rtm_pid, rtm->rtm_seq, rtm->rtm_errno);
		bprintf(stdout, rtm->rtm_flags, routeflags);
		printf("\nfmask:");
		bprintf(stdout, rtm->rtm_fmask, routeflags);
		if (verbose) {
#define lock(f)	((rtm->rtm_rmx.rmx_locks & __CONCAT(RTV_,f)) ? 'L' : ' ')
			relative_expire = rtm->rtm_rmx.rmx_expire ?
			    rtm->rtm_rmx.rmx_expire - time(NULL) : 0;
			printf("\nuse: %8llu   mtu: %8u%c   expire: %8lld%c",
			    rtm->rtm_rmx.rmx_pksent,
			    rtm->rtm_rmx.rmx_mtu, lock(MTU),
			    relative_expire, lock(EXPIRE));
#undef lock
		}
		pmsg_common(rtm);
	}
}

char *
priorityname(uint8_t prio)
{
	switch (prio) {
	case RTP_NONE:
		return ("none");
	case RTP_LOCAL:
		return ("local");
	case RTP_CONNECTED:
		return ("connected");
	case RTP_STATIC:
		return ("static");
	case RTP_OSPF:
		return ("ospf");
	case RTP_ISIS:
		return ("is-is");
	case RTP_RIP:
		return ("rip");
	case RTP_BGP:
		return ("bgp");
	case RTP_DEFAULT:
		return ("default");
	default:
		return ("");
	}
}

uint8_t
getpriority(char *priostr)
{
	const char *errstr;
	uint8_t prio;

	switch (keyword(priostr)) {
	case K_LOCAL:
		prio = RTP_LOCAL;
		break;
	case K_CONNECTED:
		prio = RTP_CONNECTED;
		break;
	case K_STATIC:
		prio = RTP_STATIC;
		break;
	case K_OSPF:
		prio = RTP_OSPF;
		break;
	case K_RIP:
		prio = RTP_RIP;
		break;
	case K_BGP:
		prio = RTP_BGP;
		break;
	default:
		prio = strtonum(priostr, -RTP_MAX, RTP_MAX, &errstr);
		if (errstr)
			errx(1, "priority is %s: %s", errstr, priostr);
	}

	return (prio);
}

void
print_getmsg(struct rt_msghdr *rtm, int msglen)
{
	long long relative_expire;
	struct sockaddr *dst = NULL, *gate = NULL, *mask = NULL, *ifa = NULL;
	struct sockaddr_dl *ifp = NULL;
	struct sockaddr_rtlabel *sa_rl = NULL;
#ifdef BFD
	struct sockaddr_bfd *sa_bfd = NULL;
#endif
	struct sockaddr *mpls = NULL;
	struct sockaddr *sa;
	char *cp;
	int i;

	printf("   route to: %s\n", routename(&so_dst.sa));
	if (rtm->rtm_version != RTM_VERSION) {
		warnx("routing message version %u not understood",
		    rtm->rtm_version);
		return;
	}
	if (rtm->rtm_msglen > msglen)
		warnx("message length mismatch, in packet %u, returned %d",
		    rtm->rtm_msglen, msglen);
	if (rtm->rtm_errno) {
		warnx("RTM_GET: %s (errno %d)",
		    strerror(rtm->rtm_errno), rtm->rtm_errno);
		return;
	}
	cp = ((char *)rtm + rtm->rtm_hdrlen);
	if (rtm->rtm_addrs)
		for (i = 1; i; i <<= 1)
			if (i & rtm->rtm_addrs) {
				sa = (struct sockaddr *)cp;
				switch (i) {
				case RTA_DST:
					dst = sa;
					break;
				case RTA_GATEWAY:
					gate = sa;
					break;
				case RTA_NETMASK:
					mask = sa;
					break;
				case RTA_IFA:
					ifa = sa;
					break;
				case RTA_IFP:
					if (sa->sa_family == AF_LINK &&
					   ((struct sockaddr_dl *)sa)->sdl_nlen)
						ifp = (struct sockaddr_dl *)sa;
					break;
				case RTA_SRC:
					mpls = sa;
					break;
				case RTA_LABEL:
					sa_rl = (struct sockaddr_rtlabel *)sa;
					break;
#ifdef BFD
				case RTA_BFD:
					sa_bfd = (struct sockaddr_bfd *)sa;
					break;
#endif
				}
				ADVANCE(cp, sa);
			}
	if (dst && mask)
		mask->sa_family = dst->sa_family;	/* XXX */
	if (dst)
		printf("destination: %s\n", routename(dst));
	if (mask) {
		int savenflag = nflag;

		nflag = 1;
		printf("       mask: %s\n", routename(mask));
		nflag = savenflag;
	}
	if (gate && rtm->rtm_flags & RTF_GATEWAY)
		printf("    gateway: %s\n", routename(gate));
	if (ifp)
		printf("  interface: %.*s\n",
		    ifp->sdl_nlen, ifp->sdl_data);
	if (ifa)
		printf(" if address: %s\n", routename(ifa));
	if (mpls) {
		printf(" mpls label: %s %s\n", mpls_op(rtm->rtm_mpls),
		    routename(mpls));
	}
	printf("   priority: %u (%s)\n", rtm->rtm_priority,
	   priorityname(rtm->rtm_priority));
	printf("      flags: ");
	bprintf(stdout, rtm->rtm_flags, routeflags);
	printf("\n");
	if (sa_rl != NULL)
		printf("      label: %s\n", sa_rl->sr_label);
#ifdef BFD
	if (sa_bfd)
		print_sabfd(sa_bfd, rtm->rtm_fmask);
#endif

#define lock(f)	((rtm->rtm_rmx.rmx_locks & __CONCAT(RTV_,f)) ? 'L' : ' ')
	relative_expire = rtm->rtm_rmx.rmx_expire ?
	    rtm->rtm_rmx.rmx_expire - time(NULL) : 0;
	printf("     use       mtu    expire\n");
	printf("%8llu  %8u%c %8lld%c\n",
	    rtm->rtm_rmx.rmx_pksent,
	    rtm->rtm_rmx.rmx_mtu, lock(MTU),
	    relative_expire, lock(EXPIRE));
#undef lock
#define	RTA_IGN	(RTA_DST|RTA_GATEWAY|RTA_NETMASK|RTA_IFP|RTA_IFA|RTA_BRD)
	if (verbose)
		pmsg_common(rtm);
	else if (rtm->rtm_addrs &~ RTA_IGN) {
		printf("sockaddrs: ");
		bprintf(stdout, rtm->rtm_addrs, addrnames);
		putchar('\n');
	}
#undef	RTA_IGN
}

#ifdef BFD
const char *
bfd_state(unsigned int state)
{
	switch (state) {
	case BFD_STATE_ADMINDOWN:
		return("admindown");
		break;
	case BFD_STATE_DOWN:
		return("down");
		break;
	case BFD_STATE_INIT:
		return("init");
		break;
	case BFD_STATE_UP:
		return("up");
		break;
	}
	return "invalid";
}

const char *
bfd_diag(unsigned int diag)
{
	switch (diag) {
	case BFD_DIAG_NONE:
		return("none");
		break;
	case BFD_DIAG_EXPIRED:
		return("expired");
		break;
	case BFD_DIAG_ECHO_FAILED:
		return("echo-failed");
		break;
	case BFD_DIAG_NEIGHBOR_SIGDOWN:
		return("neighbor-down");
		break;
	case BFD_DIAG_FIB_RESET:
		return("fib-reset");
		break;
	case BFD_DIAG_PATH_DOWN:
		return("path-down");
		break;
	case BFD_DIAG_CONCAT_PATH_DOWN:
		return("concat-path-down");
		break;
	case BFD_DIAG_ADMIN_DOWN:
		return("admindown");
		break;
	case BFD_DIAG_CONCAT_REVERSE_DOWN:
		return("concat-reverse-down");
		break;
	}
	return "invalid";
}

const char *
bfd_calc_uptime(time_t time)
{
	static char buf[256];
	struct tm *tp;
	const char *fmt;

	if (time > 2*86400)
		fmt = "%dd%kh%Mm%Ss";
	else if (time > 2*3600)
		fmt = "%kh%Mm%Ss";
	else if (time > 2*60)
		fmt = "%Mm%Ss";
	else
		fmt = "%Ss";

	tp = localtime(&time);
	if (tp)
		(void)strftime(buf, sizeof(buf), fmt, tp);
	else
		buf[0] = '\0';
	return (buf);
}

void
print_bfdmsg(struct rt_msghdr *rtm)
{
	struct bfd_msghdr *bfdm = (struct bfd_msghdr *)rtm;

	printf("\n");
	print_sabfd(&bfdm->bm_sa, rtm->rtm_fmask);
	pmsg_addrs(((char *)rtm + rtm->rtm_hdrlen), rtm->rtm_addrs);
}

void
print_sabfd(struct sockaddr_bfd *sa_bfd, int fmask)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);

	printf("        BFD:");

	/* only show the state, unless verbose or -bfd */
	if (!verbose && ((fmask & RTF_BFD) != RTF_BFD)) {
		printf(" %s\n", bfd_state(sa_bfd->bs_state));
		return;
	}

	switch (sa_bfd->bs_mode) {
	case BFD_MODE_ASYNC:
		printf(" async");
		break;
	case BFD_MODE_DEMAND:
		printf(" demand");
		break;
	default:
		printf(" unknown %u", sa_bfd->bs_mode);
		break;
	}

	printf(" state %s", bfd_state(sa_bfd->bs_state));
	printf(" remote %s", bfd_state(sa_bfd->bs_remotestate));
	printf(" laststate %s", bfd_state(sa_bfd->bs_laststate));

	printf(" error %u", sa_bfd->bs_error);
	printf("\n            ");
	printf(" diag %s", bfd_diag(sa_bfd->bs_localdiag));
	printf(" remote %s", bfd_diag(sa_bfd->bs_remotediag));
	printf("\n            ");
	printf(" discr %u", sa_bfd->bs_localdiscr);
	printf(" remote %u", sa_bfd->bs_remotediscr);
	printf("\n            ");
	printf(" uptime %s", bfd_calc_uptime(tv.tv_sec - sa_bfd->bs_uptime));
	if (sa_bfd->bs_lastuptime)
		printf(" last state time %s",
		    bfd_calc_uptime(sa_bfd->bs_lastuptime));
	printf("\n            ");
	printf(" mintx %u", sa_bfd->bs_mintx);
	printf(" minrx %u", sa_bfd->bs_minrx);
	printf(" minecho %u", sa_bfd->bs_minecho);
	printf(" multiplier %u", sa_bfd->bs_multiplier);
	printf("\n");
}
#endif /* BFD */

void
pmsg_common(struct rt_msghdr *rtm)
{
	printf("\nlocks: ");
	bprintf(stdout, rtm->rtm_rmx.rmx_locks, metricnames);
	printf(" inits: ");
	bprintf(stdout, rtm->rtm_inits, metricnames);
	pmsg_addrs(((char *)rtm + rtm->rtm_hdrlen), rtm->rtm_addrs);
}

void
pmsg_addrs(char *cp, int addrs)
{
	struct sockaddr *sa;
	int family = AF_UNSPEC;
	int i;
	char *p;

	if (addrs != 0) {
		printf("\nsockaddrs: ");
		bprintf(stdout, addrs, addrnames);
		putchar('\n');
		/* first run, search for address family */
		p = cp;
		for (i = 1; i; i <<= 1)
			if (i & addrs) {
				sa = (struct sockaddr *)p;
				if (family == AF_UNSPEC)
					switch (i) {
					case RTA_DST:
					case RTA_IFA:
						family = sa->sa_family;
					}
				ADVANCE(p, sa);
			}
		/* second run, set address family for mask and print */
		p = cp;
		for (i = 1; i; i <<= 1)
			if (i & addrs) {
				sa = (struct sockaddr *)p;
				if (family != AF_UNSPEC)
					switch (i) {
					case RTA_NETMASK:
						sa->sa_family = family;
					}
				printf(" %s", routename(sa));
				ADVANCE(p, sa);
			}
	}
	putchar('\n');
	fflush(stdout);
}

void
bprintf(FILE *fp, int b, char *s)
{
	int i;
	int gotsome = 0;

	if (b == 0)
		return;
	while ((i = *s++)) {
		if ((b & (1 << (i-1)))) {
			if (gotsome == 0)
				i = '<';
			else
				i = ',';
			putc(i, fp);
			gotsome = 1;
			for (; (i = *s) > 32; s++)
				putc(i, fp);
		} else
			while (*s > 32)
				s++;
	}
	if (gotsome)
		putc('>', fp);
}

int
keycmp(const void *key, const void *kt)
{
	return (strcmp(key, ((struct keytab *)kt)->kt_cp));
}

int
keyword(char *cp)
{
	struct keytab *kt;

	kt = bsearch(cp, keywords, sizeof(keywords)/sizeof(keywords[0]),
	    sizeof(keywords[0]), keycmp);
	if (!kt)
		return (0);

	return (kt->kt_i);
}

void
sodump(sup su, char *which)
{
	switch (su->sa.sa_family) {
	case AF_LINK:
		printf("%s: link %s; ", which, link_ntoa(&su->sdl));
		break;
	case AF_INET:
		printf("%s: inet %s; ", which, inet_ntoa(su->sin.sin_addr));
		break;
	case AF_INET6:
	    {
		char ntop_buf[NI_MAXHOST];

		printf("%s: inet6 %s; ",
		    which, inet_ntop(AF_INET6, &su->sin6.sin6_addr,
		    ntop_buf, sizeof(ntop_buf)));
		break;
	    }
	}
	fflush(stdout);
}

/* States*/
#define VIRGIN	0
#define GOTONE	1
#define GOTTWO	2
/* Inputs */
#define	DIGIT	(4*0)
#define	END	(4*1)
#define DELIM	(4*2)

void
sockaddr(char *addr, struct sockaddr *sa)
{
	char *cp = (char *)sa;
	int size = sa->sa_len;
	char *cplim = cp + size;
	int byte = 0, state = VIRGIN, new = 0;

	memset(cp, 0, size);
	cp++;
	do {
		if ((*addr >= '0') && (*addr <= '9')) {
			new = *addr - '0';
		} else if ((*addr >= 'a') && (*addr <= 'f')) {
			new = *addr - 'a' + 10;
		} else if ((*addr >= 'A') && (*addr <= 'F')) {
			new = *addr - 'A' + 10;
		} else if (*addr == '\0')
			state |= END;
		else
			state |= DELIM;
		addr++;
		switch (state /* | INPUT */) {
		case GOTTWO | DIGIT:
			*cp++ = byte; /*FALLTHROUGH*/
		case VIRGIN | DIGIT:
			state = GOTONE; byte = new; continue;
		case GOTONE | DIGIT:
			state = GOTTWO; byte = new + (byte << 4); continue;
		default: /* | DELIM */
			state = VIRGIN; *cp++ = byte; byte = 0; continue;
		case GOTONE | END:
		case GOTTWO | END:
			*cp++ = byte; /* FALLTHROUGH */
		case VIRGIN | END:
			break;
		}
		break;
	} while (cp < cplim);
	sa->sa_len = cp - (char *)sa;
}

void
getlabel(char *name)
{
	so_label.rtlabel.sr_len = sizeof(so_label.rtlabel);
	so_label.rtlabel.sr_family = AF_UNSPEC;
	if (strlcpy(so_label.rtlabel.sr_label, name,
	    sizeof(so_label.rtlabel.sr_label)) >=
	    sizeof(so_label.rtlabel.sr_label))
		errx(1, "label too long");
	rtm_addrs |= RTA_LABEL;
}

#ifndef SMALL
int
gettable(const char *s)
{
	const char		*errstr;
	struct rt_tableinfo      info;
	int			 mib[6];
	size_t			 len;

	tableid = strtonum(s, 0, RT_TABLEID_MAX, &errstr);
	if (errstr)
		errx(1, "invalid table id: %s", errstr);

	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;
	mib[3] = 0;
	mib[4] = NET_RT_TABLE;
	mib[5] = tableid;

	len = sizeof(info);
	if (sysctl(mib, 6, &info, &len, NULL, 0) == -1)
		return (errno);
	else
		return (0);
}

int
rdomain(int argc, char **argv)
{
	if (!argc)
		usage(NULL);
	if (setrtable(tableid) == -1)
		err(1, "setrtable");
	execvp(*argv, argv);
	warn("%s", argv[0]);
	return (errno == ENOENT ? 127 : 126);
}
#endif	/* SMALL */

/*
 * Print RTM_PROPOSAL DNS server addresses.
 */
void
print_rtdns(struct sockaddr_rtdns *rtdns)
{
	struct in_addr	 server;
	struct in6_addr	 in6;
	size_t		 srclen, offset;
	unsigned int	 servercnt;
	int		 i;
	char		*src = rtdns->sr_dns;
	char		 ntopbuf[INET6_ADDRSTRLEN];

	offset = offsetof(struct sockaddr_rtdns, sr_dns);
	if (rtdns->sr_len < offset) {
		printf("<invalid sr_len (%u <= %zu)>\n", rtdns->sr_len,
		    offset);
		return;
	}
	srclen = rtdns->sr_len - offset;
	if (srclen > sizeof(rtdns->sr_dns)) {
		printf("<invalid sr_len (%zu > %zu)>\n", srclen,
		    sizeof(rtdns->sr_dns));
		return;
	}
	switch (rtdns->sr_family) {
	case AF_INET:
		printf(" INET [");
		/* An array of IPv4 addresses. */
		servercnt = srclen / sizeof(struct in_addr);
		if (servercnt * sizeof(struct in_addr) != srclen) {
			printf("<invalid server count>\n");
			return;
		}
		for (i = 0; i < servercnt; i++) {
			memcpy(&server.s_addr, src, sizeof(server.s_addr));
			printf("%s%s", inet_ntoa(server), i == servercnt - 1 ?
			    "": ", ");
			src += sizeof(struct in_addr);
		}
		break;
	case AF_INET6:
		printf(" INET6 [");
		servercnt = srclen / sizeof(struct in6_addr);
		if (servercnt * sizeof(struct in6_addr) != srclen) {
			printf("<invalid server count>\n");
			return;
		}
		for (i = 0; i < servercnt; i++) {
			memcpy(&in6, src, sizeof(in6));
			src += sizeof(in6);
			printf("%s%s", inet_ntop(AF_INET6, &in6, ntopbuf,
			    INET6_ADDRSTRLEN), i == servercnt - 1 ? "": ", ");
		}
		break;
	default:
		printf(" UNKNOWN [");
		break;
	}
	printf("]");
}

/*
 * Print RTM_PROPOSAL static routes.
 */
void
print_rtstatic(struct sockaddr_rtstatic *rtstatic)
{
	struct sockaddr_in6	 gateway6;
	struct in6_addr		 prefix;
	struct in_addr		 dest, gateway;
	size_t			 srclen, offset;
	int			 bits, bytes, error, first = 1;
	uint8_t			 prefixlen;
	unsigned char		*src = rtstatic->sr_static;
	char			 ntoabuf[INET_ADDRSTRLEN];
	char			 hbuf[NI_MAXHOST];
	char			 ntopbuf[INET6_ADDRSTRLEN];

	offset = offsetof(struct sockaddr_rtstatic, sr_static);
	if (rtstatic->sr_len <= offset) {
		printf("<invalid sr_len (%u <= %zu)>\n", rtstatic->sr_len,
		    offset);
		return;
	}
	srclen = rtstatic->sr_len - offset;
	if (srclen > sizeof(rtstatic->sr_static)) {
		printf("<invalid sr_len (%zu > %zu)>\n", srclen,
		    sizeof(rtstatic->sr_static));
		return;
	}
	printf(" [");
	switch (rtstatic->sr_family) {
	case AF_INET:
		/* AF_INET -> RFC 3442 encoded static routes. */
		while (srclen) {
			bits = *src;
			src++;
			srclen--;
			bytes = (bits + 7) / 8;
			if (srclen < bytes || bytes > sizeof(dest.s_addr))
				break;
			memset(&dest, 0, sizeof(dest));
			memcpy(&dest.s_addr, src, bytes);
			src += bytes;
			srclen -= bytes;
			strlcpy(ntoabuf, inet_ntoa(dest), sizeof(ntoabuf));
			if (srclen < sizeof(gateway.s_addr))
				break;
			memcpy(&gateway.s_addr, src, sizeof(gateway.s_addr));
			src += sizeof(gateway.s_addr);
			srclen -= sizeof(gateway.s_addr);
			printf("%s%s/%u %s ", first ? "" : ", ", ntoabuf, bits,
			    inet_ntoa(gateway));
			first = 0;
		}
		break;
	case AF_INET6:
		while (srclen >= sizeof(prefixlen) + sizeof(prefix) +
		    sizeof(gateway6)) {
			memcpy(&prefixlen, src, sizeof(prefixlen));
			srclen -= sizeof(prefixlen);
			src += sizeof(prefixlen);

			memcpy(&prefix, src, sizeof(prefix));
			srclen -= sizeof(prefix);
			src += sizeof(prefix);

			memcpy(&gateway6, src, sizeof(gateway6));
			srclen -= sizeof(gateway6);
			src += sizeof(gateway6);

			if ((error = getnameinfo((struct sockaddr *)&gateway6,
			    gateway6.sin6_len, hbuf, sizeof(hbuf), NULL, 0,
			    NI_NUMERICHOST | NI_NUMERICSERV))) {
				warnx("cannot get gateway address: %s",
				    gai_strerror(error));
				return;
			}
			printf("%s%s/%u %s ", first ? "" : ", ",
			    inet_ntop(AF_INET6, &prefix, ntopbuf,
			    INET6_ADDRSTRLEN), prefixlen, hbuf);
			first = 0;
		}
		break;
	default:
		printf("<unknown address family %u>", rtstatic->sr_family);
		break;
	}
	printf("]");
}

/*
 * Print RTM_PROPOSAL domain search list.
 */
void
print_rtsearch(struct sockaddr_rtsearch *rtsearch)
{
	char	*src = rtsearch->sr_search;
	size_t	 srclen, offset;

	offset = offsetof(struct sockaddr_rtsearch, sr_search);
	if (rtsearch->sr_len <= offset) {
		printf("<invalid sr_len (%u <= %zu)>\n", rtsearch->sr_len,
		    offset);
		return;
	}
	srclen = rtsearch->sr_len - offset;
	if (srclen > sizeof(rtsearch->sr_search)) {
		printf("<invalid sr_len (%zu > %zu)>\n", srclen,
		    sizeof(rtsearch->sr_search));
		return;
	}

	printf(" [%.*s]", (int)srclen, src);
}

/*
 * Print RTM_80211INFO info.
 */
void
print_80211info(struct if_ieee80211_msghdr *ifim)
{
	unsigned int ascii, nwidlen, i;
	u_int8_t *nwid, *bssid;

	ascii = 1;
	nwid = ifim->ifim_ifie.ifie_nwid;
	nwidlen = ifim->ifim_ifie.ifie_nwid_len;
	for (i = 0; i < nwidlen; i++) {
		if (i == 0)
			printf("nwid ");
		else
			printf(":");
		printf("%02x", nwid[i]);
		if (!isprint((unsigned int)nwid[i]))
			ascii = 0;
	}
	if (i > 0) {
		if (ascii == 1)
			printf(" (%.*s)", nwidlen, nwid);
		printf(", ");
	}
	printf("channel %u, ", ifim->ifim_ifie.ifie_channel);
	bssid = ifim->ifim_ifie.ifie_addr;
	printf("bssid %02x:%02x:%02x:%02x:%02x:%02x\n",
	    bssid[0], bssid[1], bssid[2],
	    bssid[3], bssid[4], bssid[5]);
	printf("flags:");
	bprintf(stdout, ifim->ifim_ifie.ifie_flags, ieee80211flags);
	printf("\nxflags:");
	bprintf(stdout, ifim->ifim_ifie.ifie_xflags, ieee80211xflags);
	printf("\n");
}
