/*	$OpenBSD: show.c,v 1.123 2025/07/10 07:55:44 dlg Exp $	*/
/*	$NetBSD: show.c,v 1.1 1996/11/15 18:01:41 gwr Exp $	*/

/*
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

#include <sys/socket.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netmpls/mpls.h>
#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "show.h"

char	*any_ntoa(const struct sockaddr *);
char	*link_print(struct sockaddr *);
char	*label_print(struct sockaddr *);

#define ROUNDUP(a) \
	((a) > 0 ? (1 + (((a) - 1) | (sizeof(long) - 1))) : sizeof(long))
#define ADVANCE(x, n) (x += ROUNDUP((n)->sa_len))

/*
 * Definitions for showing gateway flags.
 */
struct bits {
	int	b_mask;
	char	b_val;
};
static const struct bits bits[] = {
	{ RTF_UP,	'U' },
	{ RTF_GATEWAY,	'G' },
	{ RTF_HOST,	'H' },
	{ RTF_REJECT,	'R' },
	{ RTF_DYNAMIC,	'D' },
	{ RTF_MODIFIED,	'M' },
	{ RTF_CLONING,	'C' },
	{ RTF_MULTICAST,'m' },
	{ RTF_LLINFO,	'L' },
	{ RTF_STATIC,	'S' },
	{ RTF_BLACKHOLE,'B' },
	{ RTF_PROTO3,	'3' },
	{ RTF_PROTO2,	'2' },
	{ RTF_PROTO1,	'1' },
	{ RTF_CLONED,	'c' },
	{ RTF_CACHED,	'h' },
	{ RTF_MPATH,	'P' },
	{ RTF_MPLS,	'T' },
	{ RTF_LOCAL,	'l' },
	{ RTF_BFD,	'F' },
	{ RTF_BROADCAST,'b' },
	{ RTF_CONNECTED,'n' },
	{ 0 }
};

int	 WID_DST(int);
void	 pr_rthdr(int);
void	 p_rtentry(struct rt_msghdr *);
void	 pr_family(int);
void	 p_sockaddr_mpls(struct sockaddr *, struct sockaddr *, int, int);
void	 p_flags(int, char *);
char	*routename4(in_addr_t);
char	*routename6(struct sockaddr_in6 *);
char	*netname4(in_addr_t, struct sockaddr_in *);
char	*netname6(struct sockaddr_in6 *, struct sockaddr_in6 *);

size_t
get_sysctl(const int *mib, u_int mcnt, char **buf)
{
	size_t needed;

	while (1) {
		if (sysctl(mib, mcnt, NULL, &needed, NULL, 0) == -1)
			err(1, "sysctl-estimate");
		if (needed == 0)
			break;
		if ((*buf = realloc(*buf, needed)) == NULL)
			err(1, NULL);
		if (sysctl(mib, mcnt, *buf, &needed, NULL, 0) == -1) {
			if (errno == ENOMEM)
				continue;
			err(1, "sysctl");
		}
		break;
	}

	return needed;
}

/*
 * Print preferred source address
 */
void
printsource(int af, u_int tableid)
{
	struct sockaddr *sa, *sa4 = NULL, *sa6 = NULL;
	char *buf = NULL, *next, *lim = NULL;
	size_t needed;
	int mib[7], mcnt;

	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;
	mib[3] = af;
	mib[4] = NET_RT_SOURCE;
	mib[5] = tableid;
	mcnt = 6;

	needed = get_sysctl(mib, mcnt, &buf);
	lim = buf + needed;

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	if (buf) {
		for (next = buf; next < lim; next += sa->sa_len) {
			sa = (struct sockaddr *)next;
			switch (sa->sa_family) {
			case AF_INET:
				sa4 = sa;
				break;
			case AF_INET6:
				sa6 = sa;
				break;
			}
		}
	}

	printf("Preferred source address set for rdomain %d\n", tableid);
	printf("IPv4: ");
	if (sa4 != NULL)
		p_sockaddr(sa4, NULL, RTF_HOST, WID_DST(sa4->sa_family));
	else 
		printf("default");
	printf("\n");
	printf("IPv6: ");
	if (sa6 != NULL)
		p_sockaddr(sa6, NULL, RTF_HOST, WID_DST(sa6->sa_family));
	else 
		printf("default");
	printf("\n");
	free(buf);

	exit(0);
}
/*
 * Print routing tables.
 */
void
p_rttables(int af, u_int tableid, char prio)
{
	struct rt_msghdr *rtm;
	char *buf = NULL, *next, *lim = NULL;
	size_t needed;
	int mib[7], mcnt;

	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;
	mib[3] = af;
	mib[4] = NET_RT_DUMP;
	mib[5] = prio;
	mib[6] = tableid;
	mcnt = 7;

	needed = get_sysctl(mib, mcnt, &buf);
	lim = buf + needed;

	if (pledge("stdio dns", NULL) == -1)
		err(1, "pledge");

	printf("Routing tables\n");

	if (buf) {
		for (next = buf; next < lim; next += rtm->rtm_msglen) {
			rtm = (struct rt_msghdr *)next;
			if (rtm->rtm_version != RTM_VERSION)
				continue;
			p_rtentry(rtm);
		}
	}
	free(buf);
}

/*
 * column widths; each followed by one space
 * width of destination/gateway column
 * strlen("2001:0db8:3333:4444:5555:6666:7777:8888") == 39
 */
#define	WID_GW(af)	((af) == AF_INET6 ? 39 : 18)

int
WID_DST(int af)
{

	switch (af) {
	case AF_MPLS:
		return 9;
	case AF_INET6:
		/* WID_GW() + strlen("/128")  == 4 */
		return 43;
	default:
		return 18;
	}
}

/*
 * Print header for routing table columns.
 */
void
pr_rthdr(int af)
{
	switch (af) {
	case PF_KEY:
		printf("%-18s %-5s %-18s %-5s %-5s %-22s\n",
		    "Source", "Port", "Destination",
		    "Port", "Proto", "SA(Address/Proto/Type/Direction)");
		break;
	case PF_MPLS:
		printf("%-9s %-9s %-6s %-18s %-6.6s %5.5s %8.8s %5.5s  %4.4s %s\n",
		    "In label", "Out label", "Op", "Gateway",
		    "Flags", "Refs", "Use", "Mtu", "Prio", "Interface");
		break;
	default:
		printf("%-*.*s %-*.*s %-6.6s %5.5s %8.8s %5.5s  %4.4s %s",
		    WID_DST(af), WID_DST(af), "Destination",
		    WID_GW(af), WID_GW(af), "Gateway",
		    "Flags", "Refs", "Use", "Mtu", "Prio", "Iface");
		if (verbose)
			printf(" %s", "Label");
		putchar('\n');
		break;
	}
}

void
get_rtaddrs(int addrs, struct sockaddr *sa, struct sockaddr **rti_info)
{
	int	i;

	for (i = 0; i < RTAX_MAX; i++) {
		if (addrs & (1 << i)) {
			rti_info[i] = sa;
			sa = (struct sockaddr *)((char *)(sa) +
			    ROUNDUP(sa->sa_len));
		} else
			rti_info[i] = NULL;
	}
}

/*
 * Print a routing table entry.
 */
void
p_rtentry(struct rt_msghdr *rtm)
{
	static int	 old_af = -1;
	struct sockaddr	*sa = (struct sockaddr *)((char *)rtm + rtm->rtm_hdrlen);
	struct sockaddr	*mask, *rti_info[RTAX_MAX];
	char		 ifbuf[IF_NAMESIZE];
	char		*label;

	if (sa->sa_family == AF_KEY)
		return;

	get_rtaddrs(rtm->rtm_addrs, sa, rti_info);

	if (Fflag && rti_info[RTAX_GATEWAY]->sa_family != sa->sa_family) {
		return;
	}

	if (strlen(so_label.rtlabel.sr_label)) {
		if (!rti_info[RTAX_LABEL])
			return;
		label = ((struct sockaddr_rtlabel *)rti_info[RTAX_LABEL])->
		    sr_label;
		if (strcmp(label, so_label.rtlabel.sr_label))
			return;
	}

	if (old_af != sa->sa_family) {
		old_af = sa->sa_family;
		pr_family(sa->sa_family);
		pr_rthdr(sa->sa_family);
	}

	mask = rti_info[RTAX_NETMASK];
	if ((sa = rti_info[RTAX_DST]) == NULL)
		return;

	p_sockaddr(sa, mask, rtm->rtm_flags, WID_DST(sa->sa_family));
	p_sockaddr_mpls(sa, rti_info[RTAX_SRC], rtm->rtm_mpls,
	    WID_DST(sa->sa_family));

	p_sockaddr(rti_info[RTAX_GATEWAY], NULL, RTF_HOST,
	    WID_GW(sa->sa_family));

	p_flags(rtm->rtm_flags, "%-6.6s ");
	printf("%5u %8llu ", rtm->rtm_rmx.rmx_refcnt,
	    rtm->rtm_rmx.rmx_pksent);
	if (rtm->rtm_rmx.rmx_mtu)
		printf("%5u ", rtm->rtm_rmx.rmx_mtu);
	else
		printf("%5s ", "-");
	putchar((rtm->rtm_rmx.rmx_locks & RTV_MTU) ? 'L' : ' ');
	printf("  %2d %-5.16s", rtm->rtm_priority,
	    if_indextoname(rtm->rtm_index, ifbuf));
	if (verbose && rti_info[RTAX_LABEL])
		printf(" %s", routename(rti_info[RTAX_LABEL]));
	putchar('\n');
}

/*
 * Print address family header before a section of the routing table.
 */
void
pr_family(int af)
{
	char *afname;

	switch (af) {
	case AF_INET:
		afname = "Internet";
		break;
	case AF_INET6:
		afname = "Internet6";
		break;
	case PF_KEY:
		afname = "Encap";
		break;
	case AF_MPLS:
		afname = "MPLS";
		break;
	default:
		afname = NULL;
		break;
	}
	if (afname)
		printf("\n%s:\n", afname);
	else
		printf("\nProtocol Family %d:\n", af);
}

void
p_sockaddr(struct sockaddr *sa, struct sockaddr *mask, int flags, int width)
{
	char *cp;

	switch (sa->sa_family) {
	case AF_INET6:
	    {
		struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)sa;
#ifdef __KAME__
		struct in6_addr *in6 = &sa6->sin6_addr;

		/*
		 * XXX: This is a special workaround for KAME kernels.
		 * sin6_scope_id field of SA should be set in the future.
		 */
		if ((IN6_IS_ADDR_LINKLOCAL(in6) ||
		    IN6_IS_ADDR_MC_LINKLOCAL(in6) ||
		    IN6_IS_ADDR_MC_INTFACELOCAL(in6)) &&
		    sa6->sin6_scope_id == 0) {
			/* XXX: override is ok? */
			sa6->sin6_scope_id = (u_int32_t)ntohs(*(u_short *)
			    &in6->s6_addr[2]);
			*(u_short *)&in6->s6_addr[2] = 0;
		}
#endif
		if (flags & RTF_HOST)
			cp = routename((struct sockaddr *)sa6);
		else
			cp = netname((struct sockaddr *)sa6, mask);
		break;
	    }
	case AF_MPLS:
		return;
	default:
		if ((flags & RTF_HOST) || mask == NULL)
			cp = routename(sa);
		else
			cp = netname(sa, mask);
		break;
	}
	if (width < 0)
		printf("%s", cp);
	else {
		if (nflag)
			printf("%-*s ", width, cp);
		else
			printf("%-*.*s ", width, width, cp);
	}
}

static char line[HOST_NAME_MAX+1];
static char domain[HOST_NAME_MAX+1];

void
p_sockaddr_mpls(struct sockaddr *in, struct sockaddr *out, int flags, int width)
{
	if (in->sa_family != AF_MPLS)
		return;

	if (flags & MPLS_OP_POP || flags == MPLS_OP_LOCAL) {
		printf("%-*s ", width, label_print(in));
		printf("%-*s ", width, label_print(NULL));
	} else {
		printf("%-*s ", width, label_print(in));
		printf("%-*s ", width, label_print(out));
	}

	printf("%-6s ", mpls_op(flags));
}

void
p_flags(int f, char *format)
{
	char name[33], *flags;
	const struct bits *p = bits;

	for (flags = name; p->b_mask && flags < &name[sizeof(name) - 2]; p++)
		if (p->b_mask & f)
			*flags++ = p->b_val;
	*flags = '\0';
	printf(format, name);
}

char *
routename(struct sockaddr *sa)
{
	char *cp = NULL;
	static int first = 1;

	if (first) {
		first = 0;
		if (gethostname(domain, sizeof(domain)) == 0 &&
		    (cp = strchr(domain, '.')))
			(void)strlcpy(domain, cp + 1, sizeof(domain));
		else
			domain[0] = '\0';
		cp = NULL;
	}

	if (sa->sa_len == 0) {
		(void)strlcpy(line, "default", sizeof(line));
		return (line);
	}

	switch (sa->sa_family) {
	case AF_INET:
		return
		    (routename4(((struct sockaddr_in *)sa)->sin_addr.s_addr));

	case AF_INET6:
	    {
		struct sockaddr_in6 sin6;

		memset(&sin6, 0, sizeof(sin6));
		memcpy(&sin6, sa, sa->sa_len);
		sin6.sin6_len = sizeof(struct sockaddr_in6);
		sin6.sin6_family = AF_INET6;
#ifdef __KAME__
		if (sa->sa_len == sizeof(struct sockaddr_in6) &&
		    (IN6_IS_ADDR_LINKLOCAL(&sin6.sin6_addr) ||
		     IN6_IS_ADDR_MC_LINKLOCAL(&sin6.sin6_addr) ||
		     IN6_IS_ADDR_MC_INTFACELOCAL(&sin6.sin6_addr)) &&
		    sin6.sin6_scope_id == 0) {
			sin6.sin6_scope_id =
			    ntohs(*(u_int16_t *)&sin6.sin6_addr.s6_addr[2]);
			sin6.sin6_addr.s6_addr[2] = 0;
			sin6.sin6_addr.s6_addr[3] = 0;
		}
#endif
		return (routename6(&sin6));
	    }

	case AF_LINK:
		return (link_print(sa));
	case AF_MPLS:
		return (label_print(sa));
	case AF_UNSPEC:
		if (sa->sa_len == sizeof(struct sockaddr_rtlabel)) {
			static char name[RTLABEL_LEN + 2];
			struct sockaddr_rtlabel *sr;

			sr = (struct sockaddr_rtlabel *)sa;
			snprintf(name, sizeof(name), "\"%s\"", sr->sr_label);
			return (name);
		}
		/* FALLTHROUGH */
	default:
		(void)snprintf(line, sizeof(line), "(%d) %s",
		    sa->sa_family, any_ntoa(sa));
		break;
	}
	return (line);
}

char *
routename4(in_addr_t in)
{
	char		*cp = NULL;
	struct in_addr	 ina;
	struct hostent	*hp;

	if (!cp && !nflag) {
		if ((hp = gethostbyaddr((char *)&in,
		    sizeof(in), AF_INET)) != NULL) {
			if ((cp = strchr(hp->h_name, '.')) &&
			    !strcmp(cp + 1, domain))
				*cp = '\0';
			cp = hp->h_name;
		}
	}
	ina.s_addr = in;
	strlcpy(line, cp ? cp : inet_ntoa(ina), sizeof(line));

	return (line);
}

char *
routename6(struct sockaddr_in6 *sin6)
{
	int	 niflags = 0;

	if (nflag)
		niflags |= NI_NUMERICHOST;
	else
		niflags |= NI_NOFQDN;

	if (getnameinfo((struct sockaddr *)sin6, sin6->sin6_len,
	    line, sizeof(line), NULL, 0, niflags) != 0)
		strncpy(line, "invalid", sizeof(line));

	return (line);
}

char *
netname4(in_addr_t in, struct sockaddr_in *maskp)
{
	char *cp = NULL;
	struct hostent *hp;
	in_addr_t mask;
	int mbits;

	mask = maskp && maskp->sin_len != 0 ? ntohl(maskp->sin_addr.s_addr) : 0;
	if (!nflag && in != INADDR_ANY) {
		if ((hp = gethostbyaddr((char *)&in,
		    sizeof(in), AF_INET)) != NULL)
			cp = hp->h_name;
	}
	if (in == INADDR_ANY && mask == INADDR_ANY)
		cp = "default";
	mbits = mask ? 33 - ffs(mask) : 0;
	in = ntohl(in);
	if (cp)
		strlcpy(line, cp, sizeof(line));
#define C(x)	((x) & 0xff)
	else if (mbits < 9)
		snprintf(line, sizeof(line), "%u/%d", C(in >> 24), mbits);
	else if (mbits < 17)
		snprintf(line, sizeof(line), "%u.%u/%d",
		    C(in >> 24) , C(in >> 16), mbits);
	else if (mbits < 25)
		snprintf(line, sizeof(line), "%u.%u.%u/%d",
		    C(in >> 24), C(in >> 16), C(in >> 8), mbits);
	else
		snprintf(line, sizeof(line), "%u.%u.%u.%u/%d", C(in >> 24),
		    C(in >> 16), C(in >> 8), C(in), mbits);
#undef C
	return (line);
}

char *
netname6(struct sockaddr_in6 *sa6, struct sockaddr_in6 *mask)
{
	struct sockaddr_in6 sin6;
	u_char *p;
	int masklen, final = 0, illegal = 0;
	int i, lim, flag, error;
	char hbuf[NI_MAXHOST];

	sin6 = *sa6;

	flag = 0;
	masklen = 0;
	if (mask) {
		lim = mask->sin6_len - offsetof(struct sockaddr_in6, sin6_addr);
		lim = lim < (int)sizeof(struct in6_addr) ?
		    lim : (int)sizeof(struct in6_addr);
		for (p = (u_char *)&mask->sin6_addr, i = 0; i < lim; p++) {
			if (final && *p) {
				illegal++;
				sin6.sin6_addr.s6_addr[i++] = 0x00;
				continue;
			}

			switch (*p & 0xff) {
			case 0xff:
				masklen += 8;
				break;
			case 0xfe:
				masklen += 7;
				final++;
				break;
			case 0xfc:
				masklen += 6;
				final++;
				break;
			case 0xf8:
				masklen += 5;
				final++;
				break;
			case 0xf0:
				masklen += 4;
				final++;
				break;
			case 0xe0:
				masklen += 3;
				final++;
				break;
			case 0xc0:
				masklen += 2;
				final++;
				break;
			case 0x80:
				masklen += 1;
				final++;
				break;
			case 0x00:
				final++;
				break;
			default:
				final++;
				illegal++;
				break;
			}

			if (!illegal)
				sin6.sin6_addr.s6_addr[i++] &= *p;
			else
				sin6.sin6_addr.s6_addr[i++] = 0x00;
		}
		while (i < (int)sizeof(struct in6_addr))
			sin6.sin6_addr.s6_addr[i++] = 0x00;
	} else
		masklen = 128;

	if (masklen == 0 && IN6_IS_ADDR_UNSPECIFIED(&sin6.sin6_addr))
		return ("default");

	if (illegal)
		warnx("illegal prefixlen");

	if (nflag)
		flag |= NI_NUMERICHOST;
	error = getnameinfo((struct sockaddr *)&sin6, sin6.sin6_len,
	    hbuf, sizeof(hbuf), NULL, 0, flag);
	if (error)
		snprintf(hbuf, sizeof(hbuf), "invalid");

	snprintf(line, sizeof(line), "%s/%d", hbuf, masklen);
	return (line);
}

/*
 * Return the name of the network whose address is given.
 * The address is assumed to be that of a net or subnet, not a host.
 */
char *
netname(struct sockaddr *sa, struct sockaddr *mask)
{
	switch (sa->sa_family) {
	case AF_INET:
		return netname4(((struct sockaddr_in *)sa)->sin_addr.s_addr,
		    (struct sockaddr_in *)mask);
	case AF_INET6:
		return netname6((struct sockaddr_in6 *)sa,
		    (struct sockaddr_in6 *)mask);
	case AF_LINK:
		return (link_print(sa));
	case AF_MPLS:
		return (label_print(sa));
	default:
		snprintf(line, sizeof(line), "af %d: %s",
		    sa->sa_family, any_ntoa(sa));
		break;
	}
	return (line);
}

static const char hexlist[] = "0123456789abcdef";

char *
any_ntoa(const struct sockaddr *sa)
{
	static char obuf[240];
	const char *in = sa->sa_data;
	char *out = obuf;
	int len = sa->sa_len - offsetof(struct sockaddr, sa_data);

	*out++ = 'Q';
	do {
		*out++ = hexlist[(*in >> 4) & 15];
		*out++ = hexlist[(*in++)    & 15];
		*out++ = '.';
	} while (--len > 0 && (out + 3) < &obuf[sizeof(obuf) - 1]);
	out[-1] = '\0';
	return (obuf);
}

char *
link_print(struct sockaddr *sa)
{
	struct sockaddr_dl	*sdl = (struct sockaddr_dl *)sa;
	u_char			*lla = (u_char *)sdl->sdl_data + sdl->sdl_nlen;

	if (sdl->sdl_nlen == 0 && sdl->sdl_alen == 0 &&
	    sdl->sdl_slen == 0) {
		(void)snprintf(line, sizeof(line), "link#%d", sdl->sdl_index);
		return (line);
	}
	switch (sdl->sdl_type) {
	case IFT_ETHER:
	case IFT_CARP:
		return (ether_ntoa((struct ether_addr *)lla));
	default:
		return (link_ntoa(sdl));
	}
}

char *
mpls_op(u_int32_t type)
{
	switch (type & (MPLS_OP_PUSH | MPLS_OP_POP | MPLS_OP_SWAP)) {
	case MPLS_OP_LOCAL:
		return ("LOCAL");
	case MPLS_OP_POP:
		return ("POP");
	case MPLS_OP_SWAP:
		return ("SWAP");
	case MPLS_OP_PUSH:
		return ("PUSH");
	default:
		return ("?");
	}
}

char *
label_print(struct sockaddr *sa)
{
	struct sockaddr_mpls	*smpls = (struct sockaddr_mpls *)sa;

	if (smpls)
		(void)snprintf(line, sizeof(line), "%u",
		    ntohl(smpls->smpls_label) >> MPLS_LABEL_OFFSET);
	else
		(void)snprintf(line, sizeof(line), "-");

	return (line);
}
