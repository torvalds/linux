/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1988, 1993
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

#if 0
#ifndef lint
static char sccsid[] = "From: @(#)route.c	8.6 (Berkeley) 4/28/95";
#endif /* not lint */
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/time.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netgraph/ng_socket.h>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <libutil.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <err.h>
#include <libxo/xo.h>
#include "netstat.h"
#include "nl_defs.h"

/*
 * Definitions for showing gateway flags.
 */
static struct bits {
	u_long	b_mask;
	char	b_val;
	const char *b_name;
} bits[] = {
	{ RTF_UP,	'U', "up" },
	{ RTF_GATEWAY,	'G', "gateway" },
	{ RTF_HOST,	'H', "host" },
	{ RTF_REJECT,	'R', "reject" },
	{ RTF_DYNAMIC,	'D', "dynamic" },
	{ RTF_MODIFIED,	'M', "modified" },
	{ RTF_DONE,	'd', "done" }, /* Completed -- for routing msgs only */
	{ RTF_XRESOLVE,	'X', "xresolve" },
	{ RTF_STATIC,	'S', "static" },
	{ RTF_PROTO1,	'1', "proto1" },
	{ RTF_PROTO2,	'2', "proto2" },
	{ RTF_PROTO3,	'3', "proto3" },
	{ RTF_BLACKHOLE,'B', "blackhole" },
	{ RTF_BROADCAST,'b', "broadcast" },
#ifdef RTF_LLINFO
	{ RTF_LLINFO,	'L', "llinfo" },
#endif
	{ 0 , 0, NULL }
};

struct ifmap_entry {
	char ifname[IFNAMSIZ];
};
static struct ifmap_entry *ifmap;
static int ifmap_size;
static struct timespec uptime;

static const char *netname4(in_addr_t, in_addr_t);
#ifdef INET6
static const char *netname6(struct sockaddr_in6 *, struct sockaddr_in6 *);
#endif
static void p_rtable_sysctl(int, int);
static void p_rtentry_sysctl(const char *name, struct rt_msghdr *);
static int p_sockaddr(const char *name, struct sockaddr *, struct sockaddr *,
    int, int);
static const char *fmt_sockaddr(struct sockaddr *sa, struct sockaddr *mask,
    int flags);
static void p_flags(int, const char *);
static const char *fmt_flags(int f);
static void domask(char *, size_t, u_long);


/*
 * Print routing tables.
 */
void
routepr(int fibnum, int af)
{
	size_t intsize;
	int numfibs;

	if (live == 0)
		return;

	intsize = sizeof(int);
	if (fibnum == -1 &&
	    sysctlbyname("net.my_fibnum", &fibnum, &intsize, NULL, 0) == -1)
		fibnum = 0;
	if (sysctlbyname("net.fibs", &numfibs, &intsize, NULL, 0) == -1)
		numfibs = 1;
	if (fibnum < 0 || fibnum > numfibs - 1)
		errx(EX_USAGE, "%d: invalid fib", fibnum);
	/*
	 * Since kernel & userland use different timebase
	 * (time_uptime vs time_second) and we are reading kernel memory
	 * directly we should do rt_expire --> expire_time conversion.
	 */
	if (clock_gettime(CLOCK_UPTIME, &uptime) < 0)
		err(EX_OSERR, "clock_gettime() failed");

	xo_open_container("route-information");
	xo_emit("{T:Routing tables}");
	if (fibnum)
		xo_emit(" ({L:fib}: {:fib/%d})", fibnum);
	xo_emit("\n");
	p_rtable_sysctl(fibnum, af);
	xo_close_container("route-information");
}


/*
 * Print address family header before a section of the routing table.
 */
void
pr_family(int af1)
{
	const char *afname;

	switch (af1) {
	case AF_INET:
		afname = "Internet";
		break;
#ifdef INET6
	case AF_INET6:
		afname = "Internet6";
		break;
#endif /*INET6*/
	case AF_ISO:
		afname = "ISO";
		break;
	case AF_CCITT:
		afname = "X.25";
		break;
	case AF_NETGRAPH:
		afname = "Netgraph";
		break;
	default:
		afname = NULL;
		break;
	}
	if (afname)
		xo_emit("\n{k:address-family/%s}:\n", afname);
	else
		xo_emit("\n{L:Protocol Family} {k:address-family/%d}:\n", af1);
}

/* column widths; each followed by one space */
#ifndef INET6
#define	WID_DST_DEFAULT(af) 	18	/* width of destination column */
#define	WID_GW_DEFAULT(af)	18	/* width of gateway column */
#define	WID_IF_DEFAULT(af)	(Wflag ? 10 : 8) /* width of netif column */
#else
#define	WID_DST_DEFAULT(af) \
	((af) == AF_INET6 ? (numeric_addr ? 33: 18) : 18)
#define	WID_GW_DEFAULT(af) \
	((af) == AF_INET6 ? (numeric_addr ? 29 : 18) : 18)
#define	WID_IF_DEFAULT(af)	((af) == AF_INET6 ? 8 : (Wflag ? 10 : 8))
#endif /*INET6*/

static int wid_dst;
static int wid_gw;
static int wid_flags;
static int wid_pksent;
static int wid_mtu;
static int wid_if;
static int wid_expire;

/*
 * Print header for routing table columns.
 */
static void
pr_rthdr(int af1 __unused)
{

	if (Wflag) {
		xo_emit("{T:/%-*.*s} {T:/%-*.*s} {T:/%-*.*s} {T:/%*.*s} "
		    "{T:/%*.*s} {T:/%*.*s} {T:/%*s}\n",
			wid_dst,	wid_dst,	"Destination",
			wid_gw,		wid_gw,		"Gateway",
			wid_flags,	wid_flags,	"Flags",
			wid_pksent,	wid_pksent,	"Use",
			wid_mtu,	wid_mtu,	"Mtu",
			wid_if,		wid_if,		"Netif",
			wid_expire,			"Expire");
	} else {
		xo_emit("{T:/%-*.*s} {T:/%-*.*s} {T:/%-*.*s} {T:/%*.*s} "
		    "{T:/%*s}\n",
			wid_dst,	wid_dst,	"Destination",
			wid_gw,		wid_gw,		"Gateway",
			wid_flags,	wid_flags,	"Flags",
			wid_if,		wid_if,		"Netif",
			wid_expire,			"Expire");
	}
}

static void
p_rtable_sysctl(int fibnum, int af)
{
	size_t needed;
	int mib[7];
	char *buf, *next, *lim;
	struct rt_msghdr *rtm;
	struct sockaddr *sa;
	int fam = AF_UNSPEC, ifindex = 0, size;
	int need_table_close = false;

	struct ifaddrs *ifap, *ifa;
	struct sockaddr_dl *sdl;

	/*
	 * Retrieve interface list at first
	 * since we need #ifindex -> if_xname match
	 */
	if (getifaddrs(&ifap) != 0)
		err(EX_OSERR, "getifaddrs");

	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		
		if (ifa->ifa_addr->sa_family != AF_LINK)
			continue;

		sdl = (struct sockaddr_dl *)ifa->ifa_addr;
		ifindex = sdl->sdl_index;

		if (ifindex >= ifmap_size) {
			size = roundup(ifindex + 1, 32) *
			    sizeof(struct ifmap_entry);
			if ((ifmap = realloc(ifmap, size)) == NULL)
				errx(2, "realloc(%d) failed", size);
			memset(&ifmap[ifmap_size], 0,
			    size - ifmap_size *
			    sizeof(struct ifmap_entry));

			ifmap_size = roundup(ifindex + 1, 32);
		}

		if (*ifmap[ifindex].ifname != '\0')
			continue;

		strlcpy(ifmap[ifindex].ifname, ifa->ifa_name, IFNAMSIZ);
	}

	freeifaddrs(ifap);

	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;
	mib[3] = af;
	mib[4] = NET_RT_DUMP;
	mib[5] = 0;
	mib[6] = fibnum;
	if (sysctl(mib, nitems(mib), NULL, &needed, NULL, 0) < 0)
		err(EX_OSERR, "sysctl: net.route.0.%d.dump.%d estimate", af,
		    fibnum);
	if ((buf = malloc(needed)) == NULL)
		errx(2, "malloc(%lu)", (unsigned long)needed);
	if (sysctl(mib, nitems(mib), buf, &needed, NULL, 0) < 0)
		err(1, "sysctl: net.route.0.%d.dump.%d", af, fibnum);
	lim  = buf + needed;
	xo_open_container("route-table");
	xo_open_list("rt-family");
	for (next = buf; next < lim; next += rtm->rtm_msglen) {
		rtm = (struct rt_msghdr *)next;
		if (rtm->rtm_version != RTM_VERSION)
			continue;
		/*
		 * Peek inside header to determine AF
		 */
		sa = (struct sockaddr *)(rtm + 1);
		/* Only print family first time. */
		if (fam != sa->sa_family) {
			if (need_table_close) {
				xo_close_list("rt-entry");
				xo_close_instance("rt-family");
			}
			need_table_close = true;

			fam = sa->sa_family;
			wid_dst = WID_DST_DEFAULT(fam);
			wid_gw = WID_GW_DEFAULT(fam);
			wid_flags = 6;
			wid_pksent = 8;
			wid_mtu = 6;
			wid_if = WID_IF_DEFAULT(fam);
			wid_expire = 6;
			xo_open_instance("rt-family");
			pr_family(fam);
			xo_open_list("rt-entry");

			pr_rthdr(fam);
		}
		p_rtentry_sysctl("rt-entry", rtm);
	}
	if (need_table_close) {
		xo_close_list("rt-entry");
		xo_close_instance("rt-family");
	}
	xo_close_list("rt-family");
	xo_close_container("route-table");
	free(buf);
}

static void
p_rtentry_sysctl(const char *name, struct rt_msghdr *rtm)
{
	struct sockaddr *sa, *addr[RTAX_MAX];
	char buffer[128];
	char prettyname[128];
	int i, protrusion;

	xo_open_instance(name);
	sa = (struct sockaddr *)(rtm + 1);
	for (i = 0; i < RTAX_MAX; i++) {
		if (rtm->rtm_addrs & (1 << i)) {
			addr[i] = sa;
			sa = (struct sockaddr *)((char *)sa + SA_SIZE(sa));
		}
	}

	protrusion = p_sockaddr("destination", addr[RTAX_DST],
	    addr[RTAX_NETMASK],
	    rtm->rtm_flags, wid_dst);
	protrusion = p_sockaddr("gateway", addr[RTAX_GATEWAY], NULL, RTF_HOST,
	    wid_gw - protrusion);
	snprintf(buffer, sizeof(buffer), "{[:-%d}{:flags/%%s}{]:} ",
	    wid_flags - protrusion);
	p_flags(rtm->rtm_flags, buffer);
	if (Wflag) {
		xo_emit("{t:use/%*lu} ", wid_pksent, rtm->rtm_rmx.rmx_pksent);

		if (rtm->rtm_rmx.rmx_mtu != 0)
			xo_emit("{t:mtu/%*lu} ", wid_mtu, rtm->rtm_rmx.rmx_mtu);
		else
			xo_emit("{P:/%*s} ", wid_mtu, "");
	}

	memset(prettyname, 0, sizeof(prettyname));
	if (rtm->rtm_index < ifmap_size) {
		strlcpy(prettyname, ifmap[rtm->rtm_index].ifname,
		    sizeof(prettyname));
		if (*prettyname == '\0')
			strlcpy(prettyname, "---", sizeof(prettyname));
	}

	if (Wflag)
		xo_emit("{t:interface-name/%*s}", wid_if, prettyname);
	else
		xo_emit("{t:interface-name/%*.*s}", wid_if, wid_if,
		    prettyname);
	if (rtm->rtm_rmx.rmx_expire) {
		time_t expire_time;

		if ((expire_time = rtm->rtm_rmx.rmx_expire - uptime.tv_sec) > 0)
			xo_emit(" {:expire-time/%*d}", wid_expire,
			    (int)expire_time);
	}

	xo_emit("\n");
	xo_close_instance(name);
}

static int
p_sockaddr(const char *name, struct sockaddr *sa, struct sockaddr *mask,
    int flags, int width)
{
	const char *cp;
	char buf[128];
	int protrusion;

	cp = fmt_sockaddr(sa, mask, flags);

	if (width < 0) {
		snprintf(buf, sizeof(buf), "{:%s/%%s} ", name);
		xo_emit(buf, cp);
		protrusion = 0;
	} else {
		if (Wflag != 0 || numeric_addr) {
			snprintf(buf, sizeof(buf), "{[:%d}{:%s/%%s}{]:} ",
			    -width, name);
			xo_emit(buf, cp);
			protrusion = strlen(cp) - width;
			if (protrusion < 0)
				protrusion = 0;
		} else {
			snprintf(buf, sizeof(buf), "{[:%d}{:%s/%%-.*s}{]:} ",
			    -width, name);
			xo_emit(buf, width, cp);
			protrusion = 0;
		}
	}
	return (protrusion);
}

static const char *
fmt_sockaddr(struct sockaddr *sa, struct sockaddr *mask, int flags)
{
	static char buf[128];
	const char *cp;

	if (sa == NULL)
		return ("null");

	switch(sa->sa_family) {
#ifdef INET6
	case AF_INET6:
		/*
		 * The sa6->sin6_scope_id must be filled here because
		 * this sockaddr is extracted from kmem(4) directly
		 * and has KAME-specific embedded scope id in
		 * sa6->sin6_addr.s6_addr[2].
		 */
		in6_fillscopeid(satosin6(sa));
		/* FALLTHROUGH */
#endif /*INET6*/
	case AF_INET:
		if (flags & RTF_HOST)
			cp = routename(sa, numeric_addr);
		else if (mask)
			cp = netname(sa, mask);
		else
			cp = netname(sa, NULL);
		break;
	case AF_NETGRAPH:
	    {
		strlcpy(buf, ((struct sockaddr_ng *)sa)->sg_data,
		    sizeof(buf));
		cp = buf;
		break;
	    }
	case AF_LINK:
	    {
#if 0
		struct sockaddr_dl *sdl = (struct sockaddr_dl *)sa;

		/* Interface route. */
		if (sdl->sdl_nlen)
			cp = sdl->sdl_data;
		else
#endif
			cp = routename(sa, 1);
		break;
	    }
	default:
	    {
		u_char *s = (u_char *)sa->sa_data, *slim;
		char *cq, *cqlim;

		cq = buf;
		slim =  sa->sa_len + (u_char *) sa;
		cqlim = cq + sizeof(buf) - sizeof(" ffff");
		snprintf(cq, sizeof(buf), "(%d)", sa->sa_family);
		cq += strlen(cq);
		while (s < slim && cq < cqlim) {
			snprintf(cq, sizeof(" ff"), " %02x", *s++);
			cq += strlen(cq);
			if (s < slim) {
			    snprintf(cq, sizeof("ff"), "%02x", *s++);
			    cq += strlen(cq);
			}
		}
		cp = buf;
	    }
	}

	return (cp);
}

static void
p_flags(int f, const char *format)
{
	struct bits *p;

	xo_emit(format, fmt_flags(f));

	xo_open_list("flags_pretty");
	for (p = bits; p->b_mask; p++)
		if (p->b_mask & f)
			xo_emit("{le:flags_pretty/%s}", p->b_name);
	xo_close_list("flags_pretty");
}

static const char *
fmt_flags(int f)
{
	static char name[33];
	char *flags;
	struct bits *p = bits;

	for (flags = name; p->b_mask; p++)
		if (p->b_mask & f)
			*flags++ = p->b_val;
	*flags = '\0';
	return (name);
}

char *
routename(struct sockaddr *sa, int flags)
{
	static char line[NI_MAXHOST];
	int error, f;

	f = (flags) ? NI_NUMERICHOST : 0;
	error = getnameinfo(sa, sa->sa_len, line, sizeof(line),
	    NULL, 0, f);
	if (error) {
		const void *src;
		switch (sa->sa_family) {
#ifdef INET
		case AF_INET:
			src = &satosin(sa)->sin_addr;
			break;
#endif /* INET */
#ifdef INET6
		case AF_INET6:
			src = &satosin6(sa)->sin6_addr;
			break;
#endif /* INET6 */
		default:
			return(line);
		}
		inet_ntop(sa->sa_family, src, line, sizeof(line) - 1);
		return (line);
	}
	trimdomain(line, strlen(line));

	return (line);
}

#define	NSHIFT(m) (							\
	(m) == IN_CLASSA_NET ? IN_CLASSA_NSHIFT :			\
	(m) == IN_CLASSB_NET ? IN_CLASSB_NSHIFT :			\
	(m) == IN_CLASSC_NET ? IN_CLASSC_NSHIFT :			\
	0)

static void
domask(char *dst, size_t buflen, u_long mask)
{
	int b, i;

	if (mask == 0) {
		*dst = '\0';
		return;
	}
	i = 0;
	for (b = 0; b < 32; b++)
		if (mask & (1 << b)) {
			int bb;

			i = b;
			for (bb = b+1; bb < 32; bb++)
				if (!(mask & (1 << bb))) {
					i = -1;	/* noncontig */
					break;
				}
			break;
		}
	if (i == -1)
		snprintf(dst, buflen, "&0x%lx", mask);
	else
		snprintf(dst, buflen, "/%d", 32-i);
}

/*
 * Return the name of the network whose address is given.
 */
const char *
netname(struct sockaddr *sa, struct sockaddr *mask)
{
	switch (sa->sa_family) {
	case AF_INET:
		if (mask != NULL)
			return (netname4(satosin(sa)->sin_addr.s_addr,
			    satosin(mask)->sin_addr.s_addr));
		else
			return (netname4(satosin(sa)->sin_addr.s_addr,
			    INADDR_ANY));
		break;
#ifdef INET6
	case AF_INET6:
		return (netname6(satosin6(sa), satosin6(mask)));
#endif /* INET6 */
	default:
		return (NULL);
	}
}

static const char *
netname4(in_addr_t in, in_addr_t mask)
{
	char *cp = 0;
	static char line[MAXHOSTNAMELEN + sizeof("&0xffffffff")];
	char nline[INET_ADDRSTRLEN];
	struct netent *np = 0;
	in_addr_t i;

	if (in == INADDR_ANY && mask == 0) {
		strlcpy(line, "default", sizeof(line));
		return (line);
	}

	/* It is ok to supply host address. */
	in &= mask;

	i = ntohl(in);
	if (!numeric_addr && i) {
		np = getnetbyaddr(i >> NSHIFT(ntohl(mask)), AF_INET);
		if (np != NULL) {
			cp = np->n_name;
			trimdomain(cp, strlen(cp));
		}
	}
	if (cp != NULL)
		strlcpy(line, cp, sizeof(line));
	else {
		inet_ntop(AF_INET, &in, nline, sizeof(nline));
		strlcpy(line, nline, sizeof(line));
		domask(line + strlen(line), sizeof(line) - strlen(line), ntohl(mask));
	}

	return (line);
}

#undef NSHIFT

#ifdef INET6
void
in6_fillscopeid(struct sockaddr_in6 *sa6)
{
#if defined(__KAME__)
	/*
	 * XXX: This is a special workaround for KAME kernels.
	 * sin6_scope_id field of SA should be set in the future.
	 */
	if (IN6_IS_ADDR_LINKLOCAL(&sa6->sin6_addr) ||
	    IN6_IS_ADDR_MC_NODELOCAL(&sa6->sin6_addr) ||
	    IN6_IS_ADDR_MC_LINKLOCAL(&sa6->sin6_addr)) {
		if (sa6->sin6_scope_id == 0)
			sa6->sin6_scope_id =
			    ntohs(*(u_int16_t *)&sa6->sin6_addr.s6_addr[2]);
		sa6->sin6_addr.s6_addr[2] = sa6->sin6_addr.s6_addr[3] = 0;
	}
#endif
}

/* Mask to length table.  To check an invalid value, (length + 1) is used. */
static const u_char masktolen[256] = {
	[0xff] = 8 + 1,
	[0xfe] = 7 + 1,
	[0xfc] = 6 + 1,
	[0xf8] = 5 + 1,
	[0xf0] = 4 + 1,
	[0xe0] = 3 + 1,
	[0xc0] = 2 + 1,
	[0x80] = 1 + 1,
	[0x00] = 0 + 1,
};

static const char *
netname6(struct sockaddr_in6 *sa6, struct sockaddr_in6 *mask)
{
	static char line[NI_MAXHOST + sizeof("/xxx") - 1];
	struct sockaddr_in6 addr;
	char nline[NI_MAXHOST];
	char maskbuf[sizeof("/xxx")];
	u_char *p, *lim;
	u_char masklen;
	int i;
	bool illegal = false;

	if (mask) {
		p = (u_char *)&mask->sin6_addr;
		for (masklen = 0, lim = p + 16; p < lim; p++) {
			if (masktolen[*p] > 0) {
				/* -1 is required. */
				masklen += (masktolen[*p] - 1);
			} else
				illegal = true;
		}
		if (illegal)
			xo_error("illegal prefixlen\n");

		memcpy(&addr, sa6, sizeof(addr));
		for (i = 0; i < 16; ++i)
			addr.sin6_addr.s6_addr[i] &=
			    mask->sin6_addr.s6_addr[i];
		sa6 = &addr;
	}
	else
		masklen = 128;

	if (masklen == 0 && IN6_IS_ADDR_UNSPECIFIED(&sa6->sin6_addr))
		return("default");

	getnameinfo((struct sockaddr *)sa6, sa6->sin6_len, nline, sizeof(nline),
	    NULL, 0, NI_NUMERICHOST);
	if (numeric_addr)
		strlcpy(line, nline, sizeof(line));
	else
		getnameinfo((struct sockaddr *)sa6, sa6->sin6_len, line,
		    sizeof(line), NULL, 0, 0);
	if (numeric_addr || strcmp(line, nline) == 0) {
		snprintf(maskbuf, sizeof(maskbuf), "/%d", masklen);
		strlcat(line, maskbuf, sizeof(line));
	}

	return (line);
}
#endif /*INET6*/

/*
 * Print routing statistics
 */
void
rt_stats(void)
{
	struct rtstat rtstat;
	u_long rtsaddr, rttaddr;
	int rttrash;

	if ((rtsaddr = nl[N_RTSTAT].n_value) == 0) {
		xo_emit("{W:rtstat: symbol not in namelist}\n");
		return;
	}
	if ((rttaddr = nl[N_RTTRASH].n_value) == 0) {
		xo_emit("{W:rttrash: symbol not in namelist}\n");
		return;
	}
	kread(rtsaddr, (char *)&rtstat, sizeof (rtstat));
	kread(rttaddr, (char *)&rttrash, sizeof (rttrash));
	xo_emit("{T:routing}:\n");

#define	p(f, m) if (rtstat.f || sflag <= 1) \
	xo_emit(m, rtstat.f, plural(rtstat.f))

	p(rts_badredirect, "\t{:bad-redirects/%hu} "
	    "{N:/bad routing redirect%s}\n");
	p(rts_dynamic, "\t{:dynamically-created/%hu} "
	    "{N:/dynamically created route%s}\n");
	p(rts_newgateway, "\t{:new-gateways/%hu} "
	    "{N:/new gateway%s due to redirects}\n");
	p(rts_unreach, "\t{:unreachable-destination/%hu} "
	    "{N:/destination%s found unreachable}\n");
	p(rts_wildcard, "\t{:wildcard-uses/%hu} "
	    "{N:/use%s of a wildcard route}\n");
#undef p

	if (rttrash || sflag <= 1)
		xo_emit("\t{:unused-but-not-freed/%u} "
		    "{N:/route%s not in table but not freed}\n",
		    rttrash, plural(rttrash));
}
