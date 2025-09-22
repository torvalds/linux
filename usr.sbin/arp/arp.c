/*	$OpenBSD: arp.c,v 1.89 2023/04/04 21:18:04 bluhm Exp $ */
/*	$NetBSD: arp.c,v 1.12 1995/04/24 13:25:18 cgd Exp $ */

/*
 * Copyright (c) 1984, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Sun Microsystems, Inc.
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
 * arp - display, set, delete arp table entries and wake up hosts.
 */

#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/ioctl.h>
#include <net/bpf.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <arpa/inet.h>

#include <netdb.h>
#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>
#include <ifaddrs.h>

void dump(void);
int delete(const char *);
void search(in_addr_t addr, void (*action)(struct sockaddr_dl *sdl,
	struct sockaddr_inarp *sin, struct rt_msghdr *rtm));
void print_entry(struct sockaddr_dl *sdl,
	struct sockaddr_inarp *sin, struct rt_msghdr *rtm);
void nuke_entry(struct sockaddr_dl *sdl,
	struct sockaddr_inarp *sin, struct rt_msghdr *rtm);
static char *ether_str(struct sockaddr_dl *);
int wake(const char *ether_addr, const char *iface);
int file(char *);
int get(const char *);
void getsocket(void);
int parse_host(const char *, struct in_addr *);
int rtget(struct sockaddr_inarp **, struct sockaddr_dl **);
int rtmsg(int);
int set(int, char **);
void usage(void);
static char *sec2str(time_t);

static pid_t pid;
static int replace;	/* replace entries when adding */
static int nflag;	/* no reverse dns lookups */
static int aflag;	/* do it for all entries */
static int rtsock = -1;
static int rdomain;

/* ROUNDUP() is nasty, but it is identical to what's in the kernel. */
#define ROUNDUP(a) \
	((a) > 0 ? (1 + (((a) - 1) | (sizeof(long) - 1))) : sizeof(long))
#define ADVANCE(x, n) (x += ROUNDUP((n)->sa_len))

int
main(int argc, char *argv[])
{
	int		 ch, func = 0, error = 0;
	const char	*errstr;

	pid = getpid();
	opterr = 0;
	rdomain = getrtable();
	while ((ch = getopt(argc, argv, "andsFfV:W")) != -1) {
		switch (ch) {
		case 'a':
			aflag = 1;
			break;
		case 'n':
			nflag = 1;
			break;
		case 'd':
		case 's':
		case 'f':
		case 'W':
			if (func)
				usage();
			func = ch;
			break;
		case 'F':
			replace = 1;
			break;
		case 'V':
			rdomain = strtonum(optarg, 0, RT_TABLEID_MAX, &errstr);
			if (errstr != NULL) {
				warn("bad rdomain: %s", errstr);
				usage();
			}
			break;
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;

	switch (func) {
	case 0:
		if (aflag && argc == 0)
			dump();
		else if (!aflag && argc == 1)
			error = get(argv[0]);
		else
			usage();
		break;
	case 's':
		if (argc < 2 || argc > 5)
			usage();
		if (replace)
			delete(argv[0]);
		error = set(argc, argv) ? 1 : 0;
		break;
	case 'd':
		if (aflag && argc == 0)
			search(0, nuke_entry);
		else if (!aflag && argc == 1)
			error = delete(argv[0]);
		else
			usage();
		break;
	case 'f':
		if (argc != 1)
			usage();
		error = file(argv[0]);
		break;
	case 'W':
		if (aflag || nflag || replace || rdomain > 0)
			usage();
		if (argc == 1)
			error = wake(argv[0], NULL);
		else if (argc == 2)
			error = wake(argv[0], argv[1]);
		else
			usage();
		break;
	}
	return (error);
}

/*
 * Process a file to set standard arp entries
 */
int
file(char *name)
{
	char	 line[100], arg[5][50], *args[5];
	int	 i, retval;
	FILE	*fp;

	if ((fp = fopen(name, "r")) == NULL)
		err(1, "cannot open %s", name);
	args[0] = &arg[0][0];
	args[1] = &arg[1][0];
	args[2] = &arg[2][0];
	args[3] = &arg[3][0];
	args[4] = &arg[4][0];
	retval = 0;
	while (fgets(line, sizeof(line), fp) != NULL) {
		i = sscanf(line, "%49s %49s %49s %49s %49s", arg[0], arg[1],
		    arg[2], arg[3], arg[4]);
		if (i < 2) {
			warnx("bad line: %s", line);
			retval = 1;
			continue;
		}
		if (replace)
			delete(arg[0]);
		if (set(i, args))
			retval = 1;
	}
	fclose(fp);
	return (retval);
}

void
getsocket(void)
{
	socklen_t len = sizeof(rdomain);

	if (rtsock >= 0)
		return;
	rtsock = socket(AF_ROUTE, SOCK_RAW, 0);
	if (rtsock == -1)
		err(1, "routing socket");
	if (setsockopt(rtsock, AF_ROUTE, ROUTE_TABLEFILTER, &rdomain, len) == -1)
		err(1, "ROUTE_TABLEFILTER");

	if (pledge("stdio dns", NULL) == -1)
		err(1, "pledge");
}

int
parse_host(const char *host, struct in_addr *in)
{
	struct addrinfo hints, *res;
	struct sockaddr_in *sin;
	int gai_error;

	bzero(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	if (nflag)
		hints.ai_flags = AI_NUMERICHOST;

	gai_error = getaddrinfo(host, NULL, &hints, &res);
	if (gai_error) {
		warnx("%s: %s", host, gai_strerror(gai_error));
		return 1;
	}

	sin = (struct sockaddr_in *)res->ai_addr;
	*in = sin->sin_addr;

	freeaddrinfo(res);
	return 0;
}

struct sockaddr_in	so_mask = { 8, 0, 0, { 0xffffffff } };
struct sockaddr_inarp	blank_sin = { sizeof(blank_sin), AF_INET }, sin_m;
struct sockaddr_dl	blank_sdl = { sizeof(blank_sdl), AF_LINK }, sdl_m;
struct sockaddr_dl	ifp_m = { sizeof(ifp_m), AF_LINK };
time_t			expire_time;
int			flags, export_only, doing_proxy, found_entry;
struct	{
	struct rt_msghdr	m_rtm;
	char			m_space[512];
}	m_rtmsg;

/*
 * Set an individual arp entry
 */
int
set(int argc, char *argv[])
{
	struct sockaddr_inarp *sin;
	struct sockaddr_dl *sdl;
	struct rt_msghdr *rtm;
	const char *host = argv[0], *eaddr = argv[1];
	struct ether_addr *ea;

	sin = &sin_m;
	rtm = &(m_rtmsg.m_rtm);

	getsocket();
	argc -= 2;
	argv += 2;
	sdl_m = blank_sdl;		/* struct copy */
	sin_m = blank_sin;		/* struct copy */
	if (parse_host(host, &sin->sin_addr))
		return (1);
	ea = ether_aton(eaddr);
	if (ea == NULL)
		errx(1, "invalid ethernet address: %s", eaddr);
	memcpy(LLADDR(&sdl_m), ea, sizeof(*ea));
	sdl_m.sdl_alen = 6;
	expire_time = 0;
	doing_proxy = flags = export_only = 0;
	while (argc-- > 0) {
		if (strncmp(argv[0], "temp", 4) == 0) {
			expire_time = time(NULL) + 20 * 60;
			if (flags & RTF_PERMANENT_ARP) {
				/* temp or permanent, not both */
				usage();
				return (0);
			}
		} else if (strncmp(argv[0], "pub", 3) == 0) {
			flags |= RTF_ANNOUNCE;
			doing_proxy = SIN_PROXY;
		} else if (strncmp(argv[0], "permanent", 9) == 0) {
			flags |= RTF_PERMANENT_ARP;
			if (expire_time != 0) {
				/* temp or permanent, not both */
				usage();
				return (0);
			}
		}

		argv++;
	}

tryagain:
	if (rtget(&sin, &sdl)) {
		warn("%s", host);
		return (1);
	}

	if (sin->sin_addr.s_addr == sin_m.sin_addr.s_addr) {
		if (sdl->sdl_family == AF_LINK &&
		    (rtm->rtm_flags & RTF_LLINFO) &&
		    !(rtm->rtm_flags & RTF_GATEWAY))
			switch (sdl->sdl_type) {
			case IFT_ETHER:
			case IFT_FDDI:
			case IFT_ISO88023:
			case IFT_ISO88024:
			case IFT_ISO88025:
			case IFT_CARP:
				goto overwrite;
			}

		if (doing_proxy == 0) {
			printf("set: can only proxy for %s\n", host);
			return (1);
		}
		if (sin_m.sin_other & SIN_PROXY) {
			printf("set: proxy entry exists for non 802 device\n");
			return (1);
		}
		sin_m.sin_other = SIN_PROXY;
		export_only = 1;
		goto tryagain;
	}

overwrite:
	if (sdl->sdl_family != AF_LINK) {
		printf("cannot intuit interface index and type for %s\n", host);
		return (1);
	}
	sdl_m.sdl_type = sdl->sdl_type;
	sdl_m.sdl_index = sdl->sdl_index;
	return (rtmsg(RTM_ADD));
}

#define W_ADDR	36
#define W_LL	17
#define W_IF	7

/*
 * Display an individual arp entry
 */
int
get(const char *host)
{
	struct sockaddr_inarp *sin;

	sin = &sin_m;
	sin_m = blank_sin;		/* struct copy */
	if (parse_host(host, &sin->sin_addr))
		return (1);

	printf("%-*.*s %-*.*s %*.*s %-9.9s %5s\n",
	    W_ADDR, W_ADDR, "Host", W_LL, W_LL, "Ethernet Address",
	    W_IF, W_IF, "Netif", "Expire", "Flags");

	search(sin->sin_addr.s_addr, print_entry);
	if (found_entry == 0) {
		printf("%-*.*s no entry\n", W_ADDR, W_ADDR,
		    inet_ntoa(sin->sin_addr));
		return (1);
	}
	return (0);
}

/*
 * Delete an arp entry
 */
int
delete(const char *host)
{
	struct sockaddr_inarp *sin;
	struct rt_msghdr *rtm;
	struct sockaddr_dl *sdl;

	sin = &sin_m;
	rtm = &m_rtmsg.m_rtm;

	getsocket();
	sin_m = blank_sin;		/* struct copy */
	if (parse_host(host, &sin->sin_addr))
		return 1;
tryagain:
	if (rtget(&sin, &sdl)) {
		warn("%s", host);
		return 1;
	}
	if (sin->sin_addr.s_addr == sin_m.sin_addr.s_addr) {
		if (sdl->sdl_family == AF_LINK && rtm->rtm_flags & RTF_LLINFO) {
			if (rtm->rtm_flags & RTF_LOCAL)
				return 0;
			if ((rtm->rtm_flags & RTF_GATEWAY) == 0)
				switch (sdl->sdl_type) {
				case IFT_ETHER:
				case IFT_FDDI:
				case IFT_ISO88023:
				case IFT_ISO88024:
				case IFT_ISO88025:
				case IFT_CARP:
					goto delete;
				}
		}
	}

	if (sin_m.sin_other & SIN_PROXY) {
		warnx("delete: cannot locate %s", host);
		return 1;
	} else {
		sin_m.sin_other = SIN_PROXY;
		goto tryagain;
	}
delete:
	if (sdl->sdl_family != AF_LINK) {
		printf("cannot locate %s\n", host);
		return 1;
	}
	if (rtmsg(RTM_DELETE))
		return 1;
	printf("%s (%s) deleted\n", host, inet_ntoa(sin->sin_addr));
	return 0;
}

/*
 * Search the entire arp table, and do some action on matching entries.
 */
void
search(in_addr_t addr, void (*action)(struct sockaddr_dl *sdl,
    struct sockaddr_inarp *sin, struct rt_msghdr *rtm))
{
	int mib[7];
	size_t needed;
	char *lim, *buf = NULL, *next;
	struct rt_msghdr *rtm;
	struct sockaddr_inarp *sin;
	struct sockaddr_dl *sdl;

	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;
	mib[3] = AF_INET;
	mib[4] = NET_RT_FLAGS;
	mib[5] = RTF_LLINFO;
	mib[6] = rdomain;
	while (1) {
		if (sysctl(mib, 7, NULL, &needed, NULL, 0) == -1)
			err(1, "route-sysctl-estimate");
		if (needed == 0)
			return;
		if ((buf = realloc(buf, needed)) == NULL)
			err(1, "malloc");
		if (sysctl(mib, 7, buf, &needed, NULL, 0) == -1) {
			if (errno == ENOMEM)
				continue;
			err(1, "actual retrieval of routing table");
		}
		lim = buf + needed;
		break;
	}
	for (next = buf; next < lim; next += rtm->rtm_msglen) {
		rtm = (struct rt_msghdr *)next;
		if (rtm->rtm_version != RTM_VERSION)
			continue;
		sin = (struct sockaddr_inarp *)(next + rtm->rtm_hdrlen);
		sdl = (struct sockaddr_dl *)(sin + 1);
		if (addr) {
			if (addr != sin->sin_addr.s_addr)
				continue;
			found_entry = 1;
		}
		(*action)(sdl, sin, rtm);
	}
	free(buf);
}

/*
 * Dump the entire ARP table
 */
void
dump(void)
{
	printf("%-*.*s %-*.*s %*.*s %-9.9s %5s\n",
	    W_ADDR, W_ADDR, "Host", W_LL, W_LL, "Ethernet Address",
	    W_IF, W_IF, "Netif", "Expire", "Flags");

	search(0, print_entry);
}

/*
 * Display an arp entry
 */
void
print_entry(struct sockaddr_dl *sdl, struct sockaddr_inarp *sin,
    struct rt_msghdr *rtm)
{
	char ifix_buf[IFNAMSIZ], *ifname, *host;
	struct hostent *hp = NULL;
	int addrwidth, llwidth, ifwidth ;
	time_t now;

	now = time(NULL);

	if (nflag == 0)
		hp = gethostbyaddr((caddr_t)&(sin->sin_addr),
		    sizeof(sin->sin_addr), AF_INET);
	if (hp)
		host = hp->h_name;
	else
		host = inet_ntoa(sin->sin_addr);

	addrwidth = strlen(host);
	if (addrwidth < W_ADDR)
		addrwidth = W_ADDR;
	llwidth = strlen(ether_str(sdl));
	if (W_ADDR + W_LL - addrwidth > llwidth)
		llwidth = W_ADDR + W_LL - addrwidth;
	ifname = if_indextoname(sdl->sdl_index, ifix_buf);
	if (!ifname)
		ifname = "?";
	ifwidth = strlen(ifname);
	if (W_ADDR + W_LL + W_IF - addrwidth - llwidth > ifwidth)
		ifwidth = W_ADDR + W_LL + W_IF - addrwidth - llwidth;

	printf("%-*.*s %-*.*s %*.*s", addrwidth, addrwidth, host,
	    llwidth, llwidth, ether_str(sdl), ifwidth, ifwidth, ifname);

	if (rtm->rtm_flags & (RTF_PERMANENT_ARP|RTF_LOCAL))
		printf(" %-9.9s", "permanent");
	else if (rtm->rtm_rmx.rmx_expire == 0)
		printf(" %-9.9s", "static");
	else if (rtm->rtm_rmx.rmx_expire > now)
		printf(" %-9.9s",
		    sec2str(rtm->rtm_rmx.rmx_expire - now));
	else
		printf(" %-9.9s", "expired");

	printf(" %s%s\n",
	    (rtm->rtm_flags & RTF_LOCAL) ? "l" : "",
	    (rtm->rtm_flags & RTF_ANNOUNCE) ? "p" : "");
}

/*
 * Nuke an arp entry
 */
void
nuke_entry(struct sockaddr_dl *sdl, struct sockaddr_inarp *sin,
    struct rt_msghdr *rtm)
{
	char ip[20];

	strlcpy(ip, inet_ntoa(sin->sin_addr), sizeof(ip));
	delete(ip);
}

static char *
ether_str(struct sockaddr_dl *sdl)
{
	static char hbuf[NI_MAXHOST];
	u_char *cp;

	if (sdl->sdl_alen) {
		cp = (u_char *)LLADDR(sdl);
		snprintf(hbuf, sizeof(hbuf), "%02x:%02x:%02x:%02x:%02x:%02x",
		    cp[0], cp[1], cp[2], cp[3], cp[4], cp[5]);
	} else
		snprintf(hbuf, sizeof(hbuf), "(incomplete)");

	return(hbuf);
}

void
usage(void)
{
	fprintf(stderr, "usage: arp [-adn] [-V rdomain] hostname\n");
	fprintf(stderr, "       arp [-F] [-f file] [-V rdomain] "
	    "-s hostname ether_addr\n"
	    "           [temp | permanent] [pub]\n");
	fprintf(stderr, "       arp -W ether_addr [iface]\n");
	exit(1);
}

int
rtmsg(int cmd)
{
	static int seq;
	struct rt_msghdr *rtm;
	char *cp;
	int l;

	rtm = &m_rtmsg.m_rtm;
	cp = m_rtmsg.m_space;
	errno = 0;

	if (cmd == RTM_DELETE)
		goto doit;
	memset(&m_rtmsg, 0, sizeof(m_rtmsg));
	rtm->rtm_flags = flags;
	rtm->rtm_version = RTM_VERSION;
	rtm->rtm_hdrlen = sizeof(*rtm);
	rtm->rtm_tableid = rdomain;

	switch (cmd) {
	default:
		errx(1, "internal wrong cmd");
	case RTM_ADD:
		rtm->rtm_addrs |= RTA_GATEWAY;
		rtm->rtm_rmx.rmx_expire = expire_time;
		rtm->rtm_inits = RTV_EXPIRE;
		rtm->rtm_flags |= (RTF_HOST | RTF_STATIC);
		sin_m.sin_other = 0;
		if (doing_proxy) {
			if (export_only)
				sin_m.sin_other = SIN_PROXY;
			else {
				rtm->rtm_addrs |= RTA_NETMASK;
				rtm->rtm_flags &= ~RTF_HOST;
			}
		}
		/* FALLTHROUGH */
	case RTM_GET:
		rtm->rtm_addrs |= (RTA_DST | RTA_IFP);
	}

#define NEXTADDR(w, s)							\
	if (rtm->rtm_addrs & (w)) {					\
		memcpy(cp, &(s), sizeof(s));				\
		ADVANCE(cp, (struct sockaddr *)&(s));			\
	}

	NEXTADDR(RTA_DST, sin_m);
	NEXTADDR(RTA_GATEWAY, sdl_m);
	NEXTADDR(RTA_NETMASK, so_mask);
	NEXTADDR(RTA_IFP, ifp_m);

	rtm->rtm_msglen = cp - (char *)&m_rtmsg;
doit:
	l = rtm->rtm_msglen;
	rtm->rtm_seq = ++seq;
	rtm->rtm_type = cmd;
	if (write(rtsock, (char *)&m_rtmsg, l) == -1)
		if (errno != ESRCH || cmd != RTM_DELETE) {
			warn("writing to routing socket");
			return (-1);
		}

	do {
		l = read(rtsock, (char *)&m_rtmsg, sizeof(m_rtmsg));
	} while (l > 0 && (rtm->rtm_version != RTM_VERSION ||
	    rtm->rtm_seq != seq || rtm->rtm_pid != pid));

	if (l < 0)
		warn("read from routing socket");
	return (0);
}

int
rtget(struct sockaddr_inarp **sinp, struct sockaddr_dl **sdlp)
{
	struct rt_msghdr *rtm = &(m_rtmsg.m_rtm);
	struct sockaddr_inarp *sin = NULL;
	struct sockaddr_dl *sdl = NULL;
	struct sockaddr *sa;
	char *cp;
	unsigned int i;

	if (rtmsg(RTM_GET) < 0)
		return (1);

	if (rtm->rtm_addrs) {
		cp = ((char *)rtm + rtm->rtm_hdrlen);
		for (i = 1; i; i <<= 1) {
			if (i & rtm->rtm_addrs) {
				sa = (struct sockaddr *)cp;
				switch (i) {
				case RTA_DST:
					sin = (struct sockaddr_inarp *)sa;
					break;
				case RTA_IFP:
					sdl = (struct sockaddr_dl *)sa;
					break;
				default:
					break;
				}
				ADVANCE(cp, sa);
			}
		}
	}

	if (sin == NULL || sdl == NULL)
		return (1);

	*sinp = sin;
	*sdlp = sdl;

	return (0);
}

static char *
sec2str(time_t total)
{
	static char result[256];
	int days, hours, mins, secs;
	int first = 1;
	char *p = result;
	char *ep = &result[sizeof(result)];
	int n;

	days = total / 3600 / 24;
	hours = (total / 3600) % 24;
	mins = (total / 60) % 60;
	secs = total % 60;

	if (days) {
		first = 0;
		n = snprintf(p, ep - p, "%dd", days);
		if (n < 0 || n >= ep - p)
			return "?";
		p += n;
	}
	if (!first || hours) {
		first = 0;
		n = snprintf(p, ep - p, "%dh", hours);
		if (n < 0 || n >= ep - p)
			return "?";
		p += n;
	}
	if (!first || mins) {
		first = 0;
		n = snprintf(p, ep - p, "%dm", mins);
		if (n < 0 || n >= ep - p)
			return "?";
		p += n;
	}
	snprintf(p, ep - p, "%ds", secs);

	return(result);
}

/*
 * Copyright (c) 2011 Jasper Lievisse Adriaanse <jasper@openbsd.org>
 * Copyright (C) 2006,2007,2008,2009 Marc Balmer <mbalmer@openbsd.org>
 * Copyright (C) 2000 Eugene M. Kim.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Author's name may not be used endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

int	do_wakeup(const char *, const char *, int);
int	bind_if_to_bpf(const char *, int);
int	get_ether(const char *, struct ether_addr *);
int	send_frame(int, const struct ether_addr *);

int
wake(const char *ether_addr, const char *iface)
{
	struct ifaddrs		*ifa, *ifap;
	char			*pname = NULL;
	int			 bpf;

	if ((bpf = open("/dev/bpf", O_RDWR)) == -1)
		err(1, "Failed to bind to bpf");

	if (iface == NULL) {
		if (getifaddrs(&ifa) == -1)
			errx(1, "Could not get interface addresses.");

		for (ifap = ifa; ifap != NULL; ifap = ifap->ifa_next){
			if (pname && !strcmp(pname, ifap->ifa_name))
				continue;
			pname = ifap->ifa_name;

			/*
			 * We're only interested in sending the WoL frame on
			 * certain interfaces. So skip the loopback interface,
			 * as well as point-to-point and down interfaces.
			 */
			if ((ifap->ifa_flags & IFF_LOOPBACK) ||
			    (ifap->ifa_flags & IFF_POINTOPOINT) ||
			    (!(ifap->ifa_flags & IFF_UP)) ||
			    (!(ifap->ifa_flags & IFF_BROADCAST)))
				continue;

			do_wakeup(ether_addr, ifap->ifa_name, bpf);
		}
		freeifaddrs(ifa);
	} else {
		do_wakeup(ether_addr, iface, bpf);
	}

	(void)close(bpf);

	return 0;
}

int
do_wakeup(const char *eaddr, const char *iface, int bpf)
{
	struct ether_addr	 macaddr;

	if (get_ether(eaddr, &macaddr) != 0)
		errx(1, "Invalid Ethernet address: %s", eaddr);
	if (bind_if_to_bpf(iface, bpf) != 0)
		errx(1, "Failed to bind %s to bpf.", iface);
	if (send_frame(bpf, &macaddr) != 0)
		errx(1, "Failed to send WoL frame on %s", iface);
	return 0;
}

int
bind_if_to_bpf(const char *ifname, int bpf)
{
	struct ifreq ifr;
	u_int dlt;

	if (strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name)) >=
	    sizeof(ifr.ifr_name))
		return -1;
	if (ioctl(bpf, BIOCSETIF, &ifr) == -1)
		return -1;
	if (ioctl(bpf, BIOCGDLT, &dlt) == -1)
		return -1;
	if (dlt != DLT_EN10MB)
		return -1;
	return 0;
}

int
get_ether(const char *text, struct ether_addr *addr)
{
	struct ether_addr *eaddr;

	eaddr = ether_aton(text);

	if (eaddr == NULL) {
		if (ether_hostton(text, addr))
			return -1;
	} else {
		*addr = *eaddr;
		return 0;
	}

	return 0;
}

#define SYNC_LEN 6
#define DESTADDR_COUNT 16

int
send_frame(int bpf, const struct ether_addr *addr)
{
	struct {
		struct ether_header hdr;
		u_char sync[SYNC_LEN];
		u_char dest[ETHER_ADDR_LEN * DESTADDR_COUNT];
	} __packed pkt;
	u_char *p;
	int i;

	(void)memset(&pkt, 0, sizeof(pkt));
	(void)memset(&pkt.hdr.ether_dhost, 0xff, sizeof(pkt.hdr.ether_dhost));
	pkt.hdr.ether_type = htons(0);
	(void)memset(pkt.sync, 0xff, SYNC_LEN);
	for (p = pkt.dest, i = 0; i < DESTADDR_COUNT; p += ETHER_ADDR_LEN, i++)
		bcopy(addr->ether_addr_octet, p, ETHER_ADDR_LEN);
	if (write(bpf, &pkt, sizeof(pkt)) != sizeof(pkt))
		return (errno);
	return (0);
}
