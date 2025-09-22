/*	$OpenBSD: ifaddr.c,v 1.8 2023/03/08 04:43:06 guenther Exp $	*/

/*
 * This file has been copied from ifconfig and adapted to test
 * SIOCSIFADDR, SIOCSIFNETMASK, SIOCSIFDSTADDR, SIOCSIFBRDADDR
 * ioctls.  Usually ifconfig uses SIOCAIFADDR and SIOCDIFADDR, but
 * the old kernel interface has to be tested, too.
 */

/*
 * Copyright (c) 1983, 1993
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

/*-
 * Copyright (c) 1997, 1998, 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 2019 Alexander Bluhm <bluhm@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet6/in6_var.h>
#include <netinet6/nd6.h>
#include <arpa/inet.h>
#include <netinet/ip_ipsp.h>
#include <netinet/if_ether.h>

#include <netdb.h>

#include <net/if_vlan_var.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <resolv.h>
#include <util.h>
#include <ifaddrs.h>

#define MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))
#define MAXIMUM(a, b)	(((a) > (b)) ? (a) : (b))

#define HWFEATURESBITS							\
	"\024\1CSUM_IPv4\2CSUM_TCPv4\3CSUM_UDPv4"			\
	"\5VLAN_MTU\6VLAN_HWTAGGING\10CSUM_TCPv6"			\
	"\11CSUM_UDPv6\20WOL"

struct	ifreq		ifr, ridreq;
struct	in_aliasreq	in_addreq;
struct	in6_ifreq	ifr6;
struct	in6_ifreq	in6_ridreq;
struct	in6_aliasreq	in6_addreq;
struct	sockaddr_in	netmask;

char	ifname[IFNAMSIZ];
int	flags, xflags, setaddr, setmask, setipdst, setbroad, doalias;
u_long	metric, mtu;
int	rdomainid;
int	llprio;
int	clearaddr, sock;
int	newaddr = 0;
int	af = AF_INET;
int	explicit_prefix = 0;
int	Lflag = 1;

int	showcapsflag;

void	notealias(const char *, int);
void	setifaddr(const char *, int);
void	setifrtlabel(const char *, int);
void	setifdstaddr(const char *, int);
void	addaf(const char *, int);
void	removeaf(const char *, int);
void	setifbroadaddr(const char *, int);
void	setifnetmask(const char *, int);
void	setifprefixlen(const char *, int);
void	settunnel(const char *, const char *);
void	settunneladdr(const char *, int);
void	deletetunnel(const char *, int);
void	settunnelinst(const char *, int);
void	unsettunnelinst(const char *, int);
void	settunnelttl(const char *, int);
void	setia6flags(const char *, int);
void	setia6pltime(const char *, int);
void	setia6vltime(const char *, int);
void	setia6lifetime(const char *, const char *);
void	setia6eui64(const char *, int);
void	setrdomain(const char *, int);
void	unsetrdomain(const char *, int);
int	prefix(void *val, int);
int	printgroup(char *, int);
void	setifipdst(const char *, int);
void	setignore(const char *, int);

int	actions;			/* Actions performed */

#define A_SILENT	0x8000000	/* doing operation, do not print */

#define	NEXTARG0	0xffffff
#define NEXTARG		0xfffffe
#define	NEXTARG2	0xfffffd

const struct	cmd {
	char	*c_name;
	int	c_parameter;		/* NEXTARG means next argv */
	int	c_action;		/* defered action */
	void	(*c_func)(const char *, int);
	void	(*c_func2)(const char *, const char *);
} cmds[] = {
	{ "alias",	IFF_UP,		0,		notealias },
	{ "-alias",	-IFF_UP,	0,		notealias },
	{ "delete",	-IFF_UP,	0,		notealias },
	{ "netmask",	NEXTARG,	0,		setifnetmask },
	{ "broadcast",	NEXTARG,	0,		setifbroadaddr },
	{ "prefixlen",  NEXTARG,	0,		setifprefixlen},
	{ "anycast",	IN6_IFF_ANYCAST,	0,	setia6flags },
	{ "-anycast",	-IN6_IFF_ANYCAST,	0,	setia6flags },
	{ "tentative",	IN6_IFF_TENTATIVE,	0,	setia6flags },
	{ "-tentative",	-IN6_IFF_TENTATIVE,	0,	setia6flags },
	{ "pltime",	NEXTARG,	0,		setia6pltime },
	{ "vltime",	NEXTARG,	0,		setia6vltime },
	{ "eui64",	0,		0,		setia6eui64 },
#ifndef SMALL
	{ "rtlabel",	NEXTARG,	0,		setifrtlabel },
	{ "-rtlabel",	-1,		0,		setifrtlabel },
	{ "rdomain",	NEXTARG,	0,		setrdomain },
	{ "-rdomain",	0,		0,		unsetrdomain },
	{ "tunnel",	NEXTARG2,	0,		NULL, settunnel },
	{ "tunneladdr",	NEXTARG,	0,		settunneladdr },
	{ "-tunnel",	0,		0,		deletetunnel },
	{ "tunneldomain", NEXTARG,	0,		settunnelinst },
	{ "-tunneldomain", 0,		0,		unsettunnelinst },
	{ "tunnelttl",	NEXTARG,	0,		settunnelttl },
	{ "-inet",	AF_INET,	0,		removeaf },
	{ "-inet6",	AF_INET6,	0,		removeaf },
	{ "ipdst",	NEXTARG,	0,		setifipdst },
#endif /* SMALL */
	{ NULL, /*src*/	0,		0,		setifaddr },
	{ NULL, /*dst*/	0,		0,		setifdstaddr },
	{ NULL, /*illegal*/0,		0,		NULL },
};

#define	IFFBITS								\
	"\024\1UP\2BROADCAST\3DEBUG\4LOOPBACK\5POINTOPOINT\6STATICARP"	\
	"\7RUNNING\10NOARP\11PROMISC\12ALLMULTI\13OACTIVE\14SIMPLEX"	\
	"\15LINK0\16LINK1\17LINK2\20MULTICAST"				\
	"\23AUTOCONF6TEMP\24MPLS\25WOL\26AUTOCONF6\27INET6_NOSOII"	\
	"\30AUTOCONF4"

int	getinfo(struct ifreq *, int);
void	getsock(int);
void	printif(char *, int);
void	printb(char *, unsigned int, unsigned char *);
void	printb_status(unsigned short, unsigned char *);
const char *get_linkstate(int, int);
void	status(int, struct sockaddr_dl *, int);
__dead void	usage(void);
const char *get_string(const char *, const char *, u_int8_t *, int *);
int	len_string(const u_int8_t *, int);
int	print_string(const u_int8_t *, int);
char	*sec2str(time_t);

unsigned long get_ts_map(int, int, int);

void	in_status(int);
void	in_getaddr(const char *, int);
void	in_getprefix(const char *, int);
void	in6_fillscopeid(struct sockaddr_in6 *);
void	in6_alias(struct in6_ifreq *);
void	in6_status(int);
void	in6_getaddr(const char *, int);
void	in6_getprefix(const char *, int);

/* Known address families */
const struct afswtch {
	char *af_name;
	short af_af;
	void (*af_status)(int);
	void (*af_getaddr)(const char *, int);
	void (*af_getprefix)(const char *, int);
	u_long af_difaddr;
	u_long af_aifaddr;
	caddr_t af_ridreq;
	caddr_t af_addreq;
} afs[] = {
#define C(x) ((caddr_t) &x)
	{ "inet", AF_INET, in_status, in_getaddr, in_getprefix,
	    SIOCDIFADDR, SIOCAIFADDR, C(ridreq), C(in_addreq) },
	{ "inet6", AF_INET6, in6_status, in6_getaddr, in6_getprefix,
	    SIOCDIFADDR_IN6, SIOCAIFADDR_IN6, C(in6_ridreq), C(in6_addreq) },
	{ 0,	0,	    0,		0 }
};

const struct afswtch *afp;	/*the address family being set or asked about*/

int ifaliases = 0;
int aflag = 0;

int
main(int argc, char *argv[])
{
	const struct afswtch *rafp = NULL;
	int create = 0;
	int i;

	/* If no args at all, print all interfaces.  */
	if (argc < 2) {
		/* no filesystem visibility */
		if (unveil("/", "") == -1)
			err(1, "unveil /");
		if (unveil(NULL, NULL) == -1)
			err(1, "unveil");
		aflag = 1;
		printif(NULL, 0);
		return (0);
	}
	argc--, argv++;
	if (*argv[0] == '-') {
		int nomore = 0;

		for (i = 1; argv[0][i]; i++) {
			switch (argv[0][i]) {
			case 'a':
				aflag = 1;
				nomore = 1;
				break;
			case 'A':
				aflag = 1;
				ifaliases = 1;
				nomore = 1;
				break;
			default:
				usage();
				break;
			}
		}
		if (nomore == 0) {
			argc--, argv++;
			if (argc < 1)
				usage();
			if (strlcpy(ifname, *argv, sizeof(ifname)) >= IFNAMSIZ)
				errx(1, "interface name '%s' too long", *argv);
		}
	} else if (strlcpy(ifname, *argv, sizeof(ifname)) >= IFNAMSIZ)
		errx(1, "interface name '%s' too long", *argv);
	argc--, argv++;

	if (unveil(_PATH_RESCONF, "r") == -1)
		err(1, "unveil %s", _PATH_RESCONF);
	if (unveil(_PATH_HOSTS, "r") == -1)
		err(1, "unveil %s", _PATH_HOSTS);
	if (unveil(_PATH_SERVICES, "r") == -1)
		err(1, "unveil %s", _PATH_SERVICES);
	if (unveil(NULL, NULL) == -1)
		err(1, "unveil");

	if (argc > 0) {
		for (afp = rafp = afs; rafp->af_name; rafp++)
			if (strcmp(rafp->af_name, *argv) == 0) {
				afp = rafp;
				argc--;
				argv++;
				break;
			}
		rafp = afp;
		af = ifr.ifr_addr.sa_family = rafp->af_af;
	}
	(void) strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));

	/* initialization */
	in6_addreq.ifra_lifetime.ia6t_pltime = ND6_INFINITE_LIFETIME;
	in6_addreq.ifra_lifetime.ia6t_vltime = ND6_INFINITE_LIFETIME;

	if (aflag == 0) {
		create = (argc > 0) && strcmp(argv[0], "destroy") != 0;
		(void)getinfo(&ifr, create);
	}

	if (argc != 0 && af == AF_INET6)
		addaf(ifname, AF_INET6);

	while (argc > 0) {
		const struct cmd *p;

		for (p = cmds; p->c_name; p++)
			if (strcmp(*argv, p->c_name) == 0)
				break;
		if (p->c_name == 0 && setaddr)
			for (i = setaddr; i > 0; i--) {
				p++;
				if (p->c_func == NULL)
					errx(1, "%s: bad value", *argv);
			}
		if (p->c_func || p->c_func2) {
			if (p->c_parameter == NEXTARG0) {
				const struct cmd *p0;
				int noarg = 1;

				if (argv[1]) {
					for (p0 = cmds; p0->c_name; p0++)
						if (strcmp(argv[1],
						    p0->c_name) == 0) {
							noarg = 0;
							break;
						}
				} else
					noarg = 0;

				if (noarg == 0)
					(*p->c_func)(NULL, 0);
				else
					goto nextarg;
			} else if (p->c_parameter == NEXTARG) {
nextarg:
				if (argv[1] == NULL)
					errx(1, "'%s' requires argument",
					    p->c_name);
				(*p->c_func)(argv[1], 0);
				argc--, argv++;
				actions = actions | A_SILENT | p->c_action;
			} else if (p->c_parameter == NEXTARG2) {
				if ((argv[1] == NULL) ||
				    (argv[2] == NULL))
					errx(1, "'%s' requires 2 arguments",
					    p->c_name);
				(*p->c_func2)(argv[1], argv[2]);
				argc -= 2;
				argv += 2;
				actions = actions | A_SILENT | p->c_action;
			} else {
				(*p->c_func)(*argv, p->c_parameter);
				actions = actions | A_SILENT | p->c_action;
			}
		}
		argc--, argv++;
	}

	if (argc == 0 && actions == 0) {
		printif(ifr.ifr_name, aflag ? ifaliases : 1);
		return (0);
	}

	if (af == AF_INET6 && explicit_prefix == 0) {
		/*
		 * Aggregatable address architecture defines all prefixes
		 * are 64. So, it is convenient to set prefixlen to 64 if
		 * it is not specified. If we are setting a destination
		 * address on a point-to-point interface, 128 is required.
		 */
		if (setipdst && (flags & IFF_POINTOPOINT))
			setifprefixlen("128", 0);
		else
			setifprefixlen("64", 0);
		/* in6_getprefix("64", MASK) if MASK is available here... */
	}

	if (doalias == 0 || (newaddr && clearaddr)) {
		(void) strlcpy(rafp->af_ridreq, ifname, sizeof(ifr.ifr_name));
		/* IPv4 only, inet6 does not have such ioctls */
		if (setaddr) {
			memcpy(&ridreq.ifr_addr, &in_addreq.ifra_addr,
			    in_addreq.ifra_addr.sin_len);
			if (ioctl(sock, SIOCSIFADDR, rafp->af_ridreq) == -1)
				err(1, "SIOCSIFADDR");
		}
		if (setmask) {
			memcpy(&ridreq.ifr_addr, &in_addreq.ifra_mask,
			    in_addreq.ifra_mask.sin_len);
			if (ioctl(sock, SIOCSIFNETMASK, rafp->af_ridreq) == -1)
				err(1, "SIOCSIFNETMASK");
		}
		if (setipdst) {
			memcpy(&ridreq.ifr_addr, &in_addreq.ifra_dstaddr,
			    in_addreq.ifra_dstaddr.sin_len);
			if (ioctl(sock, SIOCSIFDSTADDR, rafp->af_ridreq) == -1)
				err(1, "SIOCSIFDSTADDR");
		}
		if (setbroad) {
			memcpy(&ridreq.ifr_addr, &in_addreq.ifra_broadaddr,
			    in_addreq.ifra_broadaddr.sin_len);
			if (ioctl(sock, SIOCSIFBRDADDR, rafp->af_ridreq) == -1)
				err(1, "SIOCSIFBRDADDR");
		}
		return (0);
	}
	if (clearaddr) {
		(void) strlcpy(rafp->af_ridreq, ifname, sizeof(ifr.ifr_name));
		if (ioctl(sock, rafp->af_difaddr, rafp->af_ridreq) == -1) {
			if (errno == EADDRNOTAVAIL && (doalias >= 0)) {
				/* means no previous address for interface */
			} else
				err(1, "SIOCDIFADDR");
		}
	}
	if (newaddr) {
		(void) strlcpy(rafp->af_addreq, ifname, sizeof(ifr.ifr_name));
		if (ioctl(sock, rafp->af_aifaddr, rafp->af_addreq) == -1)
			err(1, "SIOCAIFADDR");
	}
	return (0);
}

void
getsock(int naf)
{
	static int oaf = -1;

	if (oaf == naf)
		return;
	if (oaf != -1)
		close(sock);
	sock = socket(naf, SOCK_DGRAM, 0);
	if (sock == -1)
		oaf = -1;
	else
		oaf = naf;
}

int
getinfo(struct ifreq *ifr, int create)
{

	getsock(af);
	if (sock == -1)
		err(1, "socket");
	if (!isdigit((unsigned char)ifname[strlen(ifname) - 1]))
		return (-1);	/* ignore groups here */
	if (ioctl(sock, SIOCGIFFLAGS, (caddr_t)ifr) == -1) {
		int oerrno = errno;

		if (!create)
			return (-1);
		if (ioctl(sock, SIOCIFCREATE, (caddr_t)ifr) == -1) {
			errno = oerrno;
			return (-1);
		}
		if (ioctl(sock, SIOCGIFFLAGS, (caddr_t)ifr) == -1)
			return (-1);
	}
	flags = ifr->ifr_flags & 0xffff;
	if (ioctl(sock, SIOCGIFXFLAGS, (caddr_t)ifr) == -1)
		ifr->ifr_flags = 0;
	xflags = ifr->ifr_flags;
	if (ioctl(sock, SIOCGIFMETRIC, (caddr_t)ifr) == -1)
		metric = 0;
	else
		metric = ifr->ifr_metric;
	if (ioctl(sock, SIOCGIFMTU, (caddr_t)ifr) == -1)
		mtu = 0;
	else
		mtu = ifr->ifr_mtu;
#ifndef SMALL
	if (ioctl(sock, SIOCGIFRDOMAIN, (caddr_t)ifr) == -1)
		rdomainid = 0;
	else
		rdomainid = ifr->ifr_rdomainid;
#endif
	if (ioctl(sock, SIOCGIFLLPRIO, (caddr_t)ifr) == -1)
		llprio = 0;
	else
		llprio = ifr->ifr_llprio;

	return (0);
}

int
printgroup(char *groupname, int ifaliases)
{
	struct ifgroupreq	 ifgr;
	struct ifg_req		*ifg;
	int			 len, cnt = 0;

	getsock(AF_INET);
	bzero(&ifgr, sizeof(ifgr));
	strlcpy(ifgr.ifgr_name, groupname, sizeof(ifgr.ifgr_name));
	if (ioctl(sock, SIOCGIFGMEMB, (caddr_t)&ifgr) == -1) {
		if (errno == EINVAL || errno == ENOTTY ||
		    errno == ENOENT)
			return (-1);
		else
			err(1, "SIOCGIFGMEMB");
	}

	len = ifgr.ifgr_len;
	if ((ifgr.ifgr_groups = calloc(1, len)) == NULL)
		err(1, "printgroup");
	if (ioctl(sock, SIOCGIFGMEMB, (caddr_t)&ifgr) == -1)
		err(1, "SIOCGIFGMEMB");

	for (ifg = ifgr.ifgr_groups; ifg && len >= sizeof(struct ifg_req);
	    ifg++) {
		len -= sizeof(struct ifg_req);
		printif(ifg->ifgrq_member, ifaliases);
		cnt++;
	}
	free(ifgr.ifgr_groups);

	return (cnt);
}

void
printif(char *name, int ifaliases)
{
	struct ifaddrs *ifap, *ifa;
	struct if_data *ifdata;
	const char *namep;
	char *oname = NULL;
	struct ifreq *ifrp;
	int count = 0, noinet = 1;
	size_t nlen = 0;

	if (aflag)
		name = NULL;
	if (name) {
		if ((oname = strdup(name)) == NULL)
			err(1, "strdup");
		nlen = strlen(oname);
		/* is it a group? */
		if (nlen && !isdigit((unsigned char)oname[nlen - 1]))
			if (printgroup(oname, ifaliases) != -1) {
				free(oname);
				return;
			}
	}

	if (getifaddrs(&ifap) != 0)
		err(1, "getifaddrs");

	namep = NULL;
	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		if (oname) {
			if (nlen && isdigit((unsigned char)oname[nlen - 1])) {
				/* must have exact match */
				if (strcmp(oname, ifa->ifa_name) != 0)
					continue;
			} else {
				/* partial match OK if it ends w/ digit */
				if (strncmp(oname, ifa->ifa_name, nlen) != 0 ||
				    !isdigit((unsigned char)ifa->ifa_name[nlen]))
					continue;
			}
		}
		/* quickhack: sizeof(ifr) < sizeof(ifr6) */
		if (ifa->ifa_addr->sa_family == AF_INET6) {
			memset(&ifr6, 0, sizeof(ifr6));
			memcpy(&ifr6.ifr_addr, ifa->ifa_addr,
			    MINIMUM(sizeof(ifr6.ifr_addr), ifa->ifa_addr->sa_len));
			ifrp = (struct ifreq *)&ifr6;
		} else {
			memset(&ifr, 0, sizeof(ifr));
			memcpy(&ifr.ifr_addr, ifa->ifa_addr,
			    MINIMUM(sizeof(ifr.ifr_addr), ifa->ifa_addr->sa_len));
			ifrp = &ifr;
		}
		strlcpy(ifname, ifa->ifa_name, sizeof(ifname));
		strlcpy(ifrp->ifr_name, ifa->ifa_name, sizeof(ifrp->ifr_name));

		if (ifa->ifa_addr->sa_family == AF_LINK) {
			namep = ifa->ifa_name;
			if (getinfo(ifrp, 0) < 0)
				continue;
			ifdata = ifa->ifa_data;
			status(1, (struct sockaddr_dl *)ifa->ifa_addr,
			    ifdata->ifi_link_state);
			count++;
			noinet = 1;
			continue;
		}

		if (!namep || !strcmp(namep, ifa->ifa_name)) {
			const struct afswtch *p;

			if (ifa->ifa_addr->sa_family == AF_INET &&
			    ifaliases == 0 && noinet == 0)
				continue;
			if ((p = afp) != NULL) {
				if (ifa->ifa_addr->sa_family == p->af_af)
					p->af_status(1);
			} else {
				for (p = afs; p->af_name; p++) {
					if (ifa->ifa_addr->sa_family ==
					    p->af_af)
						p->af_status(0);
				}
			}
			count++;
			if (ifa->ifa_addr->sa_family == AF_INET)
				noinet = 0;
			continue;
		}
	}
	freeifaddrs(ifap);
	free(oname);
	if (count == 0) {
		fprintf(stderr, "%s: no such interface\n", ifname);
		exit(1);
	}
}

#define RIDADDR 0
#define ADDR	1
#define MASK	2
#define DSTADDR	3

void
setifaddr(const char *addr, int param)
{
	/*
	 * Delay the ioctl to set the interface addr until flags are all set.
	 * The address interpretation may depend on the flags,
	 * and the flags may change when the address is set.
	 */
	setaddr++;
	if (doalias >= 0)
		newaddr = 1;
	if (doalias == 0)
		clearaddr = 1;
	afp->af_getaddr(addr, (doalias >= 0 ? ADDR : RIDADDR));
}

#ifndef SMALL
void
setifrtlabel(const char *label, int d)
{
	if (d != 0)
		ifr.ifr_data = (caddr_t)(const char *)"";
	else
		ifr.ifr_data = (caddr_t)label;
	if (ioctl(sock, SIOCSIFRTLABEL, &ifr) == -1)
		warn("SIOCSIFRTLABEL");
}
#endif

void
setifnetmask(const char *addr, int ignored)
{
	setmask++;
	afp->af_getaddr(addr, MASK);
	explicit_prefix = 1;
}

void
setifbroadaddr(const char *addr, int ignored)
{
	setbroad++;
	afp->af_getaddr(addr, DSTADDR);
}

void
setifipdst(const char *addr, int ignored)
{
	in_getaddr(addr, DSTADDR);
	setipdst++;
	clearaddr = 0;
	newaddr = 0;
}

#define rqtosa(x) (&(((struct ifreq *)(afp->x))->ifr_addr))
void
notealias(const char *addr, int param)
{
	if (setaddr && doalias == 0 && param < 0)
		memcpy(rqtosa(af_ridreq), rqtosa(af_addreq),
		    rqtosa(af_addreq)->sa_len);
	doalias = param;
	if (param < 0) {
		clearaddr = 1;
		newaddr = 0;
	} else
		clearaddr = 0;
}

void
setifdstaddr(const char *addr, int param)
{
	setaddr++;
	setipdst++;
	afp->af_getaddr(addr, DSTADDR);
}

void
addaf(const char *vname, int value)
{
	struct if_afreq	ifar;

	strlcpy(ifar.ifar_name, ifname, sizeof(ifar.ifar_name));
	ifar.ifar_af = value;
	if (ioctl(sock, SIOCIFAFATTACH, (caddr_t)&ifar) == -1)
		warn("SIOCIFAFATTACH");
}

void
removeaf(const char *vname, int value)
{
	struct if_afreq	ifar;

	strlcpy(ifar.ifar_name, ifname, sizeof(ifar.ifar_name));
	ifar.ifar_af = value;
	if (ioctl(sock, SIOCIFAFDETACH, (caddr_t)&ifar) == -1)
		warn("SIOCIFAFDETACH");
}

void
setia6flags(const char *vname, int value)
{

	if (value < 0) {
		value = -value;
		in6_addreq.ifra_flags &= ~value;
	} else
		in6_addreq.ifra_flags |= value;
}

void
setia6pltime(const char *val, int d)
{

	setia6lifetime("pltime", val);
}

void
setia6vltime(const char *val, int d)
{

	setia6lifetime("vltime", val);
}

void
setia6lifetime(const char *cmd, const char *val)
{
	const char *errmsg = NULL;
	time_t newval, t;

	newval = strtonum(val, 0, 1000000, &errmsg);
	if (errmsg)
		errx(1, "invalid %s %s: %s", cmd, val, errmsg);

	t = time(NULL);

	if (afp->af_af != AF_INET6)
		errx(1, "%s not allowed for this address family", cmd);
	if (strcmp(cmd, "vltime") == 0) {
		in6_addreq.ifra_lifetime.ia6t_expire = t + newval;
		in6_addreq.ifra_lifetime.ia6t_vltime = newval;
	} else if (strcmp(cmd, "pltime") == 0) {
		in6_addreq.ifra_lifetime.ia6t_preferred = t + newval;
		in6_addreq.ifra_lifetime.ia6t_pltime = newval;
	}
}

void
setia6eui64(const char *cmd, int val)
{
	struct ifaddrs *ifap, *ifa;
	const struct sockaddr_in6 *sin6 = NULL;
	const struct in6_addr *lladdr = NULL;
	struct in6_addr *in6;

	if (afp->af_af != AF_INET6)
		errx(1, "%s not allowed for this address family", cmd);

	addaf(ifname, AF_INET6);

	in6 = (struct in6_addr *)&in6_addreq.ifra_addr.sin6_addr;
	if (memcmp(&in6addr_any.s6_addr[8], &in6->s6_addr[8], 8) != 0)
		errx(1, "interface index is already filled");
	if (getifaddrs(&ifap) != 0)
		err(1, "getifaddrs");
	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr->sa_family == AF_INET6 &&
		    strcmp(ifa->ifa_name, ifname) == 0) {
			sin6 = (const struct sockaddr_in6 *)ifa->ifa_addr;
			if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr)) {
				lladdr = &sin6->sin6_addr;
				break;
			}
		}
	}
	if (!lladdr)
		errx(1, "could not determine link local address");

	memcpy(&in6->s6_addr[8], &lladdr->s6_addr[8], 8);

	freeifaddrs(ifap);
}

const char *
get_string(const char *val, const char *sep, u_int8_t *buf, int *lenp)
{
	int len = *lenp, hexstr;
	u_int8_t *p = buf;

	hexstr = (val[0] == '0' && tolower((u_char)val[1]) == 'x');
	if (hexstr)
		val += 2;
	for (;;) {
		if (*val == '\0')
			break;
		if (sep != NULL && strchr(sep, *val) != NULL) {
			val++;
			break;
		}
		if (hexstr) {
			if (!isxdigit((u_char)val[0]) ||
			    !isxdigit((u_char)val[1])) {
				warnx("bad hexadecimal digits");
				return NULL;
			}
		}
		if (p > buf + len) {
			if (hexstr)
				warnx("hexadecimal digits too long");
			else
				warnx("strings too long");
			return NULL;
		}
		if (hexstr) {
#define	tohex(x)	(isdigit(x) ? (x) - '0' : tolower(x) - 'a' + 10)
			*p++ = (tohex((u_char)val[0]) << 4) |
			    tohex((u_char)val[1]);
#undef tohex
			val += 2;
		} else {
			if (*val == '\\' &&
			    sep != NULL && strchr(sep, *(val + 1)) != NULL)
				val++;
			*p++ = *val++;
		}
	}
	len = p - buf;
	if (len < *lenp)
		memset(p, 0, *lenp - len);
	*lenp = len;
	return val;
}

int
len_string(const u_int8_t *buf, int len)
{
	int i = 0, hasspc = 0;

	if (len < 2 || buf[0] != '0' || tolower(buf[1]) != 'x') {
		for (; i < len; i++) {
			/* Only print 7-bit ASCII keys */
			if (buf[i] & 0x80 || !isprint(buf[i]))
				break;
			if (isspace(buf[i]))
				hasspc++;
		}
	}
	if (i == len) {
		if (hasspc || len == 0)
			return len + 2;
		else
			return len;
	} else
		return (len * 2) + 2;
}

int
print_string(const u_int8_t *buf, int len)
{
	int i = 0, hasspc = 0;

	if (len < 2 || buf[0] != '0' || tolower(buf[1]) != 'x') {
		for (; i < len; i++) {
			/* Only print 7-bit ASCII keys */
			if (buf[i] & 0x80 || !isprint(buf[i]))
				break;
			if (isspace(buf[i]))
				hasspc++;
		}
	}
	if (i == len) {
		if (hasspc || len == 0) {
			printf("\"%.*s\"", len, buf);
			return len + 2;
		} else {
			printf("%.*s", len, buf);
			return len;
		}
	} else {
		printf("0x");
		for (i = 0; i < len; i++)
			printf("%02x", buf[i]);
		return (len * 2) + 2;
	}
}

static void
print_tunnel(const struct if_laddrreq *req)
{
	char psrcaddr[NI_MAXHOST];
	char pdstaddr[NI_MAXHOST];
	const char *ver = "";
	const int niflag = NI_NUMERICHOST;

	if (req == NULL) {
		printf("(unset)");
		return;
	}

	psrcaddr[0] = pdstaddr[0] = '\0';

	if (getnameinfo((struct sockaddr *)&req->addr, req->addr.ss_len,
	    psrcaddr, sizeof(psrcaddr), 0, 0, niflag) != 0)
		strlcpy(psrcaddr, "<error>", sizeof(psrcaddr));
	if (req->addr.ss_family == AF_INET6)
		ver = "6";

	printf("inet%s %s", ver, psrcaddr);

	if (req->dstaddr.ss_family != AF_UNSPEC) {
		in_port_t dstport = 0;
		const struct sockaddr_in *sin;
		const struct sockaddr_in6 *sin6;

		if (getnameinfo((struct sockaddr *)&req->dstaddr,
		    req->dstaddr.ss_len, pdstaddr, sizeof(pdstaddr),
		    0, 0, niflag) != 0)
			strlcpy(pdstaddr, "<error>", sizeof(pdstaddr));

		printf(" -> %s", pdstaddr);

		switch (req->dstaddr.ss_family) {
		case AF_INET:
			sin = (const struct sockaddr_in *)&req->dstaddr;
			dstport = sin->sin_port;
			break;
		case AF_INET6:
			sin6 = (const struct sockaddr_in6 *)&req->dstaddr;
			dstport = sin6->sin6_port;
			break;
		}

		if (dstport)
			printf(":%u", ntohs(dstport));
	}
}

static void
phys_status(int force)
{
	struct if_laddrreq req;
	struct if_laddrreq *r = &req;

	memset(&req, 0, sizeof(req));
	(void) strlcpy(req.iflr_name, ifname, sizeof(req.iflr_name));
	if (ioctl(sock, SIOCGLIFPHYADDR, (caddr_t)&req) == -1) {
		if (errno != EADDRNOTAVAIL)
			return;

		r = NULL;
	}

	printf("\ttunnel: ");
	print_tunnel(r);

	if (ioctl(sock, SIOCGLIFPHYTTL, (caddr_t)&ifr) == 0) {
		if (ifr.ifr_ttl == -1)
			printf(" ttl copy");
		else if (ifr.ifr_ttl > 0)
			printf(" ttl %d", ifr.ifr_ttl);
	}

	if (ioctl(sock, SIOCGLIFPHYDF, (caddr_t)&ifr) == 0)
		printf(" %s", ifr.ifr_df ? "df" : "nodf");

#ifndef SMALL
	if (ioctl(sock, SIOCGLIFPHYECN, (caddr_t)&ifr) == 0)
		printf(" %s", ifr.ifr_metric ? "ecn" : "noecn");

	if (ioctl(sock, SIOCGLIFPHYRTABLE, (caddr_t)&ifr) == 0 &&
	    (rdomainid != 0 || ifr.ifr_rdomainid != 0))
		printf(" rdomain %d", ifr.ifr_rdomainid);
#endif
	printf("\n");
}

#ifndef SMALL
const uint64_t ifm_status_valid_list[] = IFM_STATUS_VALID_LIST;

const struct ifmedia_status_description ifm_status_descriptions[] =
	IFM_STATUS_DESCRIPTIONS;
#endif

const struct if_status_description if_status_descriptions[] =
	LINK_STATE_DESCRIPTIONS;

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

/*
 * Print the status of the interface.  If an address family was
 * specified, show it and it only; otherwise, show them all.
 */
void
status(int link, struct sockaddr_dl *sdl, int ls)
{
	const struct afswtch *p = afp;
	struct ifmediareq ifmr;
#ifndef SMALL
	struct ifreq ifrdesc;
	char ifdescr[IFDESCRSIZE];
#endif
	uint64_t *media_list;
	char sep;


	printf("%s: ", ifname);
	printb("flags", flags | (xflags << 16), IFFBITS);
	if (rdomainid)
		printf(" rdomain %d", rdomainid);
	if (metric)
		printf(" metric %lu", metric);
	if (mtu)
		printf(" mtu %lu", mtu);
	putchar('\n');
	if (sdl != NULL && sdl->sdl_alen &&
	    (sdl->sdl_type == IFT_ETHER || sdl->sdl_type == IFT_CARP))
		(void)printf("\tlladdr %s\n", ether_ntoa(
		    (struct ether_addr *)LLADDR(sdl)));

	sep = '\t';
#ifndef SMALL
	(void) memset(&ifrdesc, 0, sizeof(ifrdesc));
	(void) strlcpy(ifrdesc.ifr_name, ifname, sizeof(ifrdesc.ifr_name));
	ifrdesc.ifr_data = (caddr_t)&ifdescr;
	if (ioctl(sock, SIOCGIFDESCR, &ifrdesc) == 0 &&
	    strlen(ifrdesc.ifr_data))
		printf("\tdescription: %s\n", ifrdesc.ifr_data);

	if (sdl != NULL) {
		printf("%cindex %u", sep, sdl->sdl_index);
		sep = ' ';
	}
	if (ioctl(sock, SIOCGIFPRIORITY, &ifrdesc) == 0) {
		printf("%cpriority %d", sep, ifrdesc.ifr_metric);
		sep = ' ';
	}
#endif
	printf("%cllprio %d\n", sep, llprio);

	(void) memset(&ifmr, 0, sizeof(ifmr));
	(void) strlcpy(ifmr.ifm_name, ifname, sizeof(ifmr.ifm_name));

	if (ioctl(sock, SIOCGIFMEDIA, (caddr_t)&ifmr) == -1) {
		/*
		 * Interface doesn't support SIOC{G,S}IFMEDIA.
		 */
		if (ls != LINK_STATE_UNKNOWN)
			printf("\tstatus: %s\n",
			    get_linkstate(sdl->sdl_type, ls));
		goto proto_status;
	}

	if (ifmr.ifm_count == 0) {
		warnx("%s: no media types?", ifname);
		goto proto_status;
	}

	media_list = calloc(ifmr.ifm_count, sizeof(*media_list));
	if (media_list == NULL)
		err(1, "calloc");
	ifmr.ifm_ulist = media_list;

	if (ioctl(sock, SIOCGIFMEDIA, (caddr_t)&ifmr) == -1)
		err(1, "SIOCGIFMEDIA");

#ifdef SMALL
	printf("\tstatus: %s\n", get_linkstate(sdl->sdl_type, ls));
#else
	if (ifmr.ifm_status & IFM_AVALID) {
		const struct ifmedia_status_description *ifms;
		int bitno, found = 0;

		printf("\tstatus: ");
		for (bitno = 0; ifm_status_valid_list[bitno] != 0; bitno++) {
			for (ifms = ifm_status_descriptions;
			    ifms->ifms_valid != 0; ifms++) {
				if (ifms->ifms_type !=
				    IFM_TYPE(ifmr.ifm_current) ||
				    ifms->ifms_valid !=
				    ifm_status_valid_list[bitno])
					continue;
				printf("%s%s", found ? ", " : "",
				    IFM_STATUS_DESC(ifms, ifmr.ifm_status));
				found = 1;

				/*
				 * For each valid indicator bit, there's
				 * only one entry for each media type, so
				 * terminate the inner loop now.
				 */
				break;
			}
		}

		if (found == 0)
			printf("unknown");
		putchar('\n');
	}

#endif
	free(media_list);

 proto_status:
	if (link == 0) {
		if ((p = afp) != NULL) {
			p->af_status(1);
		} else for (p = afs; p->af_name; p++) {
			ifr.ifr_addr.sa_family = p->af_af;
			p->af_status(0);
		}
	}

	phys_status(0);
}

void
in_status(int force)
{
	struct sockaddr_in *sin, sin2;

	getsock(AF_INET);
	if (sock == -1) {
		if (errno == EPROTONOSUPPORT)
			return;
		err(1, "socket");
	}
	(void) strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	sin = (struct sockaddr_in *)&ifr.ifr_addr;

	/*
	 * We keep the interface address and reset it before each
	 * ioctl() so we can get ifaliases information (as opposed
	 * to the primary interface netmask/dstaddr/broadaddr, if
	 * the ifr_addr field is zero).
	 */
	memcpy(&sin2, &ifr.ifr_addr, sizeof(sin2));

	(void) strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	if (ioctl(sock, SIOCGIFADDR, (caddr_t)&ifr) == -1) {
		warn("SIOCGIFADDR");
		memset(&ifr.ifr_addr, 0, sizeof(ifr.ifr_addr));
	}
	(void) strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	sin = (struct sockaddr_in *)&ifr.ifr_addr;
	printf("\tinet %s", inet_ntoa(sin->sin_addr));
	memcpy(&ifr.ifr_addr, &sin2, sizeof(sin2));
	if (ioctl(sock, SIOCGIFNETMASK, (caddr_t)&ifr) == -1) {
		if (errno != EADDRNOTAVAIL)
			warn("SIOCGIFNETMASK");
		memset(&ifr.ifr_addr, 0, sizeof(ifr.ifr_addr));
	} else
		netmask.sin_addr =
		    ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr;
	if (flags & IFF_POINTOPOINT) {
		memcpy(&ifr.ifr_addr, &sin2, sizeof(sin2));
		if (ioctl(sock, SIOCGIFDSTADDR, (caddr_t)&ifr) == -1) {
			if (errno == EADDRNOTAVAIL)
			    memset(&ifr.ifr_addr, 0, sizeof(ifr.ifr_addr));
			else
			    warn("SIOCGIFDSTADDR");
		}
		(void) strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
		sin = (struct sockaddr_in *)&ifr.ifr_dstaddr;
		printf(" --> %s", inet_ntoa(sin->sin_addr));
	}
	printf(" netmask 0x%x", ntohl(netmask.sin_addr.s_addr));
	if (flags & IFF_BROADCAST) {
		memcpy(&ifr.ifr_addr, &sin2, sizeof(sin2));
		if (ioctl(sock, SIOCGIFBRDADDR, (caddr_t)&ifr) == -1) {
			if (errno == EADDRNOTAVAIL)
			    memset(&ifr.ifr_addr, 0, sizeof(ifr.ifr_addr));
			else
			    warn("SIOCGIFBRDADDR");
		}
		(void) strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
		sin = (struct sockaddr_in *)&ifr.ifr_addr;
		if (sin->sin_addr.s_addr != 0)
			printf(" broadcast %s", inet_ntoa(sin->sin_addr));
	}
	putchar('\n');
}

void
setifprefixlen(const char *addr, int d)
{
	setmask++;
	if (afp->af_getprefix)
		afp->af_getprefix(addr, MASK);
	explicit_prefix = 1;
}

void
in6_fillscopeid(struct sockaddr_in6 *sin6)
{
#ifdef __KAME__
	if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr)) {
		sin6->sin6_scope_id =
			ntohs(*(u_int16_t *)&sin6->sin6_addr.s6_addr[2]);
		sin6->sin6_addr.s6_addr[2] = sin6->sin6_addr.s6_addr[3] = 0;
	}
#endif /* __KAME__ */
}

/* XXX not really an alias */
void
in6_alias(struct in6_ifreq *creq)
{
	struct sockaddr_in6 *sin6;
	struct	in6_ifreq ifr6;		/* shadows file static variable */
	u_int32_t scopeid;
	char hbuf[NI_MAXHOST];
	const int niflag = NI_NUMERICHOST;

	/* Get the non-alias address for this interface. */
	getsock(AF_INET6);
	if (sock == -1) {
		if (errno == EPROTONOSUPPORT)
			return;
		err(1, "socket");
	}

	sin6 = (struct sockaddr_in6 *)&creq->ifr_addr;

	in6_fillscopeid(sin6);
	scopeid = sin6->sin6_scope_id;
	if (getnameinfo((struct sockaddr *)sin6, sin6->sin6_len,
	    hbuf, sizeof(hbuf), NULL, 0, niflag) != 0)
		strlcpy(hbuf, "", sizeof hbuf);
	printf("\tinet6 %s", hbuf);

	if (flags & IFF_POINTOPOINT) {
		(void) memset(&ifr6, 0, sizeof(ifr6));
		(void) strlcpy(ifr6.ifr_name, ifname, sizeof(ifr6.ifr_name));
		ifr6.ifr_addr = creq->ifr_addr;
		if (ioctl(sock, SIOCGIFDSTADDR_IN6, (caddr_t)&ifr6) == -1) {
			if (errno != EADDRNOTAVAIL)
				warn("SIOCGIFDSTADDR_IN6");
			(void) memset(&ifr6.ifr_addr, 0, sizeof(ifr6.ifr_addr));
			ifr6.ifr_addr.sin6_family = AF_INET6;
			ifr6.ifr_addr.sin6_len = sizeof(struct sockaddr_in6);
		}
		sin6 = (struct sockaddr_in6 *)&ifr6.ifr_addr;
		in6_fillscopeid(sin6);
		if (getnameinfo((struct sockaddr *)sin6, sin6->sin6_len,
		    hbuf, sizeof(hbuf), NULL, 0, niflag) != 0)
			strlcpy(hbuf, "", sizeof hbuf);
		printf(" -> %s", hbuf);
	}

	(void) memset(&ifr6, 0, sizeof(ifr6));
	(void) strlcpy(ifr6.ifr_name, ifname, sizeof(ifr6.ifr_name));
	ifr6.ifr_addr = creq->ifr_addr;
	if (ioctl(sock, SIOCGIFNETMASK_IN6, (caddr_t)&ifr6) == -1) {
		if (errno != EADDRNOTAVAIL)
			warn("SIOCGIFNETMASK_IN6");
	} else {
		sin6 = (struct sockaddr_in6 *)&ifr6.ifr_addr;
		printf(" prefixlen %d", prefix(&sin6->sin6_addr,
		    sizeof(struct in6_addr)));
	}

	(void) memset(&ifr6, 0, sizeof(ifr6));
	(void) strlcpy(ifr6.ifr_name, ifname, sizeof(ifr6.ifr_name));
	ifr6.ifr_addr = creq->ifr_addr;
	if (ioctl(sock, SIOCGIFAFLAG_IN6, (caddr_t)&ifr6) == -1) {
		if (errno != EADDRNOTAVAIL)
			warn("SIOCGIFAFLAG_IN6");
	} else {
		if (ifr6.ifr_ifru.ifru_flags6 & IN6_IFF_ANYCAST)
			printf(" anycast");
		if (ifr6.ifr_ifru.ifru_flags6 & IN6_IFF_TENTATIVE)
			printf(" tentative");
		if (ifr6.ifr_ifru.ifru_flags6 & IN6_IFF_DUPLICATED)
			printf(" duplicated");
		if (ifr6.ifr_ifru.ifru_flags6 & IN6_IFF_DETACHED)
			printf(" detached");
		if (ifr6.ifr_ifru.ifru_flags6 & IN6_IFF_DEPRECATED)
			printf(" deprecated");
		if (ifr6.ifr_ifru.ifru_flags6 & IN6_IFF_AUTOCONF)
			printf(" autoconf");
		if (ifr6.ifr_ifru.ifru_flags6 & IN6_IFF_TEMPORARY)
			printf(" autoconfprivacy");
	}

	if (scopeid)
		printf(" scopeid 0x%x", scopeid);

	if (Lflag) {
		struct in6_addrlifetime *lifetime;

		(void) memset(&ifr6, 0, sizeof(ifr6));
		(void) strlcpy(ifr6.ifr_name, ifname, sizeof(ifr6.ifr_name));
		ifr6.ifr_addr = creq->ifr_addr;
		lifetime = &ifr6.ifr_ifru.ifru_lifetime;
		if (ioctl(sock, SIOCGIFALIFETIME_IN6, (caddr_t)&ifr6) == -1) {
			if (errno != EADDRNOTAVAIL)
				warn("SIOCGIFALIFETIME_IN6");
		} else if (lifetime->ia6t_preferred || lifetime->ia6t_expire) {
			time_t t = time(NULL);

			printf(" pltime ");
			if (lifetime->ia6t_preferred) {
				printf("%s", lifetime->ia6t_preferred < t
				    ? "0" :
				    sec2str(lifetime->ia6t_preferred - t));
			} else
				printf("infty");

			printf(" vltime ");
			if (lifetime->ia6t_expire) {
				printf("%s", lifetime->ia6t_expire < t
				    ? "0"
				    : sec2str(lifetime->ia6t_expire - t));
			} else
				printf("infty");
		}
	}

	printf("\n");
}

void
in6_status(int force)
{
	in6_alias((struct in6_ifreq *)&ifr6);
}

#ifndef SMALL
void
settunnel(const char *src, const char *dst)
{
	char buf[HOST_NAME_MAX+1 + sizeof (":65535")], *dstport;
	const char *dstip;
	struct addrinfo *srcres, *dstres;
	int ecode;
	struct if_laddrreq req;

	if (strchr(dst, ':') == NULL || strchr(dst, ':') != strrchr(dst, ':')) {
		/* no port or IPv6 */
		dstip = dst;
		dstport = NULL;
	} else {
		if (strlcpy(buf, dst, sizeof(buf)) >= sizeof(buf))
			errx(1, "%s bad value", dst);
		dstport = strchr(buf, ':');
		*dstport++ = '\0';
		dstip = buf;
	}

	if ((ecode = getaddrinfo(src, NULL, NULL, &srcres)) != 0)
		errx(1, "error in parsing address string: %s",
		    gai_strerror(ecode));

	if ((ecode = getaddrinfo(dstip, dstport, NULL, &dstres)) != 0)
		errx(1, "error in parsing address string: %s",
		    gai_strerror(ecode));

	if (srcres->ai_addr->sa_family != dstres->ai_addr->sa_family)
		errx(1,
		    "source and destination address families do not match");

	memset(&req, 0, sizeof(req));
	(void) strlcpy(req.iflr_name, ifname, sizeof(req.iflr_name));
	memcpy(&req.addr, srcres->ai_addr, srcres->ai_addrlen);
	memcpy(&req.dstaddr, dstres->ai_addr, dstres->ai_addrlen);
	if (ioctl(sock, SIOCSLIFPHYADDR, &req) == -1)
		warn("SIOCSLIFPHYADDR");

	freeaddrinfo(srcres);
	freeaddrinfo(dstres);
}

void
settunneladdr(const char *addr, int ignored)
{
	struct addrinfo hints, *res;
	struct if_laddrreq req;
	ssize_t len;
	int rv;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = 0;
	hints.ai_flags = AI_PASSIVE;

	rv = getaddrinfo(addr, NULL, &hints, &res);
	if (rv != 0)
		errx(1, "tunneladdr %s: %s", addr, gai_strerror(rv));

	memset(&req, 0, sizeof(req));
	len = strlcpy(req.iflr_name, ifname, sizeof(req.iflr_name));
	if (len >= sizeof(req.iflr_name))
		errx(1, "%s: Interface name too long", ifname);

	memcpy(&req.addr, res->ai_addr, res->ai_addrlen);

	req.dstaddr.ss_len = 2;
	req.dstaddr.ss_family = AF_UNSPEC;

	if (ioctl(sock, SIOCSLIFPHYADDR, &req) == -1)
		warn("tunneladdr %s", addr);

	freeaddrinfo(res);
}

void
deletetunnel(const char *ignored, int alsoignored)
{
	if (ioctl(sock, SIOCDIFPHYADDR, &ifr) == -1)
		warn("SIOCDIFPHYADDR");
}

void
settunnelinst(const char *id, int param)
{
	const char *errmsg = NULL;
	int rdomainid;

	rdomainid = strtonum(id, 0, RT_TABLEID_MAX, &errmsg);
	if (errmsg)
		errx(1, "rdomain %s: %s", id, errmsg);

	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	ifr.ifr_rdomainid = rdomainid;
	if (ioctl(sock, SIOCSLIFPHYRTABLE, (caddr_t)&ifr) == -1)
		warn("SIOCSLIFPHYRTABLE");
}

void
unsettunnelinst(const char *ignored, int alsoignored)
{
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	ifr.ifr_rdomainid = 0;
	if (ioctl(sock, SIOCSLIFPHYRTABLE, (caddr_t)&ifr) == -1)
		warn("SIOCSLIFPHYRTABLE");
}

void
settunnelttl(const char *id, int param)
{
	const char *errmsg = NULL;
	int ttl;

	if (strcmp(id, "copy") == 0)
		ttl = -1;
	else {
		ttl = strtonum(id, 0, 0xff, &errmsg);
		if (errmsg)
			errx(1, "tunnelttl %s: %s", id, errmsg);
	}

	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	ifr.ifr_ttl = ttl;
	if (ioctl(sock, SIOCSLIFPHYTTL, (caddr_t)&ifr) == -1)
		warn("SIOCSLIFPHYTTL");
}

void
utf16_to_char(uint16_t *in, int inlen, char *out, size_t outlen)
{
	uint16_t c;

	while (outlen > 0) {
		c = inlen > 0 ? letoh16(*in) : 0;
		if (c == 0 || --outlen == 0) {
			/* always NUL terminate result */
			*out = '\0';
			break;
		}
		*out++ = isascii(c) ? (char)c : '?';
		in++;
		inlen--;
	}
}

int
char_to_utf16(const char *in, uint16_t *out, size_t outlen)
{
	int	 n = 0;
	uint16_t c;

	for (;;) {
		c = *in++;

		if (c == '\0') {
			/*
			 * NUL termination is not required, but zero out the
			 * residual buffer
			 */
			memset(out, 0, outlen);
			return n;
		}
		if (outlen < sizeof (*out))
			return -1;

		*out++ = htole16(c);
		n += sizeof (*out);
		outlen -= sizeof (*out);
	}
}

#endif

#define SIN(x) ((struct sockaddr_in *) &(x))
struct sockaddr_in *sintab[] = {
SIN(ridreq.ifr_addr), SIN(in_addreq.ifra_addr),
SIN(in_addreq.ifra_mask), SIN(in_addreq.ifra_broadaddr)};

void
in_getaddr(const char *s, int which)
{
	struct sockaddr_in *sin = sintab[which], tsin;
	struct hostent *hp;
	int bits, l;
	char p[3];

	bzero(&tsin, sizeof(tsin));
	sin->sin_len = sizeof(*sin);
	if (which != MASK)
		sin->sin_family = AF_INET;

	if (which == ADDR && strrchr(s, '/') != NULL &&
	    (bits = inet_net_pton(AF_INET, s, &tsin.sin_addr,
	    sizeof(tsin.sin_addr))) != -1) {
		l = snprintf(p, sizeof(p), "%d", bits);
		if (l < 0 || l >= sizeof(p))
			errx(1, "%d: bad prefixlen", bits);
		setmask++;
		in_getprefix(p, MASK);
		memcpy(&sin->sin_addr, &tsin.sin_addr, sizeof(sin->sin_addr));
	} else if (inet_aton(s, &sin->sin_addr) == 0) {
		if ((hp = gethostbyname(s)))
			memcpy(&sin->sin_addr, hp->h_addr, hp->h_length);
		else
			errx(1, "%s: bad value", s);
	}
}

void
in_getprefix(const char *plen, int which)
{
	struct sockaddr_in *sin = sintab[which];
	const char *errmsg = NULL;
	u_char *cp;
	int len;

	len = strtonum(plen, 0, 32, &errmsg);
	if (errmsg)
		errx(1, "prefix %s: %s", plen, errmsg);

	sin->sin_len = sizeof(*sin);
	if (which != MASK)
		sin->sin_family = AF_INET;
	if ((len == 0) || (len == 32)) {
		memset(&sin->sin_addr, 0xff, sizeof(struct in_addr));
		return;
	}
	memset((void *)&sin->sin_addr, 0x00, sizeof(sin->sin_addr));
	for (cp = (u_char *)&sin->sin_addr; len > 7; len -= 8)
		*cp++ = 0xff;
	if (len)
		*cp = 0xff << (8 - len);
}

/*
 * Print a value a la the %b format of the kernel's printf
 */
void
printb(char *s, unsigned int v, unsigned char *bits)
{
	int i, any = 0;
	unsigned char c;

	if (bits && *bits == 8)
		printf("%s=%o", s, v);
	else
		printf("%s=%x", s, v);

	if (bits) {
		bits++;
		putchar('<');
		while ((i = *bits++)) {
			if (v & (1 << (i-1))) {
				if (any)
					putchar(',');
				any = 1;
				for (; (c = *bits) > 32; bits++)
					putchar(c);
			} else
				for (; *bits > 32; bits++)
					;
		}
		putchar('>');
	}
}

/*
 * A simple version of printb for status output
 */
void
printb_status(unsigned short v, unsigned char *bits)
{
	int i, any = 0;
	unsigned char c;

	if (bits) {
		bits++;
		while ((i = *bits++)) {
			if (v & (1 << (i-1))) {
				if (any)
					putchar(',');
				any = 1;
				for (; (c = *bits) > 32; bits++)
					putchar(tolower(c));
			} else
				for (; *bits > 32; bits++)
					;
		}
	}
}

#define SIN6(x) ((struct sockaddr_in6 *) &(x))
struct sockaddr_in6 *sin6tab[] = {
SIN6(in6_ridreq.ifr_addr), SIN6(in6_addreq.ifra_addr),
SIN6(in6_addreq.ifra_prefixmask), SIN6(in6_addreq.ifra_dstaddr)};

void
in6_getaddr(const char *s, int which)
{
	struct sockaddr_in6 *sin6 = sin6tab[which];
	struct addrinfo hints, *res;
	char buf[HOST_NAME_MAX+1 + sizeof("/128")], *pfxlen;
	int error;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_DGRAM;	/*dummy*/

	if (which == ADDR && strchr(s, '/') != NULL) {
		if (strlcpy(buf, s, sizeof(buf)) >= sizeof(buf))
			errx(1, "%s: bad value", s);
		pfxlen = strchr(buf, '/');
		*pfxlen++ = '\0';
		s = buf;
		setmask++;
		in6_getprefix(pfxlen, MASK);
		explicit_prefix = 1;
	}

	error = getaddrinfo(s, "0", &hints, &res);
	if (error)
		errx(1, "%s: %s", s, gai_strerror(error));
	memcpy(sin6, res->ai_addr, res->ai_addrlen);
#ifdef __KAME__
	if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr) &&
	    *(u_int16_t *)&sin6->sin6_addr.s6_addr[2] == 0 &&
	    sin6->sin6_scope_id) {
		*(u_int16_t *)&sin6->sin6_addr.s6_addr[2] =
		    htons(sin6->sin6_scope_id & 0xffff);
		sin6->sin6_scope_id = 0;
	}
#endif /* __KAME__ */
	freeaddrinfo(res);
}

void
in6_getprefix(const char *plen, int which)
{
	struct sockaddr_in6 *sin6 = sin6tab[which];
	const char *errmsg = NULL;
	u_char *cp;
	int len;

	len = strtonum(plen, 0, 128, &errmsg);
	if (errmsg)
		errx(1, "prefix %s: %s", plen, errmsg);

	sin6->sin6_len = sizeof(*sin6);
	if (which != MASK)
		sin6->sin6_family = AF_INET6;
	if ((len == 0) || (len == 128)) {
		memset(&sin6->sin6_addr, 0xff, sizeof(struct in6_addr));
		return;
	}
	memset((void *)&sin6->sin6_addr, 0x00, sizeof(sin6->sin6_addr));
	for (cp = (u_char *)&sin6->sin6_addr; len > 7; len -= 8)
		*cp++ = 0xff;
	if (len)
		*cp = 0xff << (8 - len);
}

int
prefix(void *val, int size)
{
	u_char *nam = (u_char *)val;
	int byte, bit, plen = 0;

	for (byte = 0; byte < size; byte++, plen += 8)
		if (nam[byte] != 0xff)
			break;
	if (byte == size)
		return (plen);
	for (bit = 7; bit != 0; bit--, plen++)
		if (!(nam[byte] & (1 << bit)))
			break;
	for (; bit != 0; bit--)
		if (nam[byte] & (1 << bit))
			return (0);
	byte++;
	for (; byte < size; byte++)
		if (nam[byte])
			return (0);
	return (plen);
}

/* Print usage and exit  */
__dead void
usage(void)
{
	fprintf(stderr,
	    "usage: ifaddr interface [address_family] "
	    "[address [dest_address]]\n"
	    "\t\t[parameters]\n");
	exit(1);
}

#ifndef SMALL
void
printifhwfeatures(const char *unused, int show)
{
	struct if_data ifrdat;

	if (!show) {
		if (showcapsflag)
			usage();
		showcapsflag = 1;
		return;
	}
	bzero(&ifrdat, sizeof(ifrdat));
	ifr.ifr_data = (caddr_t)&ifrdat;
	if (ioctl(sock, SIOCGIFDATA, (caddr_t)&ifr) == -1)
		err(1, "SIOCGIFDATA");
	printb("\thwfeatures", (u_int)ifrdat.ifi_capabilities, HWFEATURESBITS);

	if (ioctl(sock, SIOCGIFHARDMTU, (caddr_t)&ifr) != -1) {
		if (ifr.ifr_hardmtu)
			printf(" hardmtu %u", ifr.ifr_hardmtu);
	}
	putchar('\n');
}
#endif

char *
sec2str(time_t total)
{
	static char result[256];
	char *p = result;
	char *end = &result[sizeof(result)];

	snprintf(p, end - p, "%lld", (long long)total);
	return (result);
}

#ifndef SMALL
void
setrdomain(const char *id, int param)
{
	const char *errmsg = NULL;
	int rdomainid;

	rdomainid = strtonum(id, 0, RT_TABLEID_MAX, &errmsg);
	if (errmsg)
		errx(1, "rdomain %s: %s", id, errmsg);

	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	ifr.ifr_rdomainid = rdomainid;
	if (ioctl(sock, SIOCSIFRDOMAIN, (caddr_t)&ifr) == -1)
		warn("SIOCSIFRDOMAIN");
}

void
unsetrdomain(const char *ignored, int alsoignored)
{
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	ifr.ifr_rdomainid = 0;
	if (ioctl(sock, SIOCSIFRDOMAIN, (caddr_t)&ifr) == -1)
		warn("SIOCSIFRDOMAIN");
}
#endif

#ifdef SMALL
void
setignore(const char *id, int param)
{
	/* just digest the command */
}
#endif
