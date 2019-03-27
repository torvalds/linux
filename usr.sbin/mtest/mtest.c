/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2009 Bruce Simpson.
 * Copyright (c) 2000 Wilbert De Graaf.
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Diagnostic and test utility for multicast sockets.
 * XXX: This file currently assumes INET support in the base system.
 * TODO: Support embedded KAME Scope ID in IPv6 group addresses.
 * TODO: Use IPv4 link-local address when source address selection
 * is implemented; use MCAST_JOIN_SOURCE for IPv4.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/ethernet.h>
#ifdef INET
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#endif
#ifdef INET6
#include <netinet/in.h>
#include <netinet/ip6.h>
#endif

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <err.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <ifaddrs.h>

union sockunion {
	struct sockaddr_storage	ss;
	struct sockaddr		sa;
	struct sockaddr_dl	sdl;
#ifdef INET
	struct sockaddr_in	sin;
#endif
#ifdef INET6
	struct sockaddr_in6	sin6;
#endif
};
typedef union sockunion sockunion_t;

union mrequnion {
#ifdef INET
	struct ip_mreq	 	 mr;
	struct ip_mreq_source	 mrs;
#endif
#ifdef INET6
	struct ipv6_mreq	 mr6;
	struct group_source_req	 gr;
#endif
};
typedef union mrequnion mrequnion_t;

#define	MAX_ADDRS	20
#define	STR_SIZE	20
#define	LINE_LENGTH	80

#ifdef INET
static int	__ifindex_to_primary_ip(const uint32_t, struct in_addr *);
#endif
static uint32_t	parse_cmd_args(sockunion_t *, sockunion_t *,
		    const char *, const char *, const char *);
static void	process_file(char *, int, int);
static void	process_cmd(char*, int, int, FILE *);
static int	su_cmp(const void *, const void *);
static void	usage(void);

/*
 * Ordering predicate for qsort().
 */
static int
su_cmp(const void *a, const void *b)
{
	const sockunion_t	*sua = (const sockunion_t *)a;
	const sockunion_t	*sub = (const sockunion_t *)b;

	assert(sua->sa.sa_family == sub->sa.sa_family);

	switch (sua->sa.sa_family) {
#ifdef INET
	case AF_INET:
		return ((int)(sua->sin.sin_addr.s_addr -
		    sub->sin.sin_addr.s_addr));
		break;
#endif
#ifdef INET6
	case AF_INET6:
		return (memcmp(&sua->sin6.sin6_addr, &sub->sin6.sin6_addr,
		    sizeof(struct in6_addr)));
		break;
#endif
	default:
		break;
	}

	assert(sua->sa.sa_len == sub->sa.sa_len);
	return (memcmp(sua, sub, sua->sa.sa_len));
}

#ifdef INET
/*
 * Internal: Map an interface index to primary IPv4 address.
 * This is somewhat inefficient. This is a useful enough operation
 * that it probably belongs in the C library.
 * Return zero if found, -1 on error, 1 on not found.
 */
static int
__ifindex_to_primary_ip(const uint32_t ifindex, struct in_addr *pina)
{
	char		 ifname[IFNAMSIZ];
	struct ifaddrs	*ifa;
	struct ifaddrs	*ifaddrs;
	sockunion_t	*psu;
	int		 retval;

	assert(ifindex != 0);

	retval = -1;
	if (if_indextoname(ifindex, ifname) == NULL)
		return (retval);
	if (getifaddrs(&ifaddrs) < 0)
		return (retval);

	/*
	 * Find the ifaddr entry corresponding to the interface name,
	 * and return the first matching IPv4 address.
	 */
	retval = 1;
	for (ifa = ifaddrs; ifa != NULL; ifa = ifa->ifa_next) {
		if (strcmp(ifa->ifa_name, ifname) != 0)
			continue;
		psu = (sockunion_t *)ifa->ifa_addr;
		if (psu && psu->sa.sa_family == AF_INET) {
			retval = 0;
			memcpy(pina, &psu->sin.sin_addr,
			    sizeof(struct in_addr));
			break;
		}
	}

	if (retval != 0)
		errno = EADDRNOTAVAIL;	/* XXX */

	freeifaddrs(ifaddrs);
	return (retval);
}
#endif /* INET */

int
main(int argc, char **argv)
{
	char	 line[LINE_LENGTH];
	char	*p;
	int	 i, s, s6;

	s = -1;
	s6 = -1;
#ifdef INET
	s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == -1 && errno != EAFNOSUPPORT)
		err(1, "can't open IPv4 socket");
#endif
#ifdef INET6
	s6 = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	if (s6 == -1 && errno != EAFNOSUPPORT)
		err(1, "can't open IPv6 socket");
#endif
	if (s == -1 && s6 == -1)
		errc(1, EPROTONOSUPPORT, "can't open socket");

	if (argc < 2) {
		if (isatty(STDIN_FILENO)) {
			printf("multicast membership test program; "
			    "enter ? for list of commands\n");
		}
		do {
			if (fgets(line, sizeof(line), stdin) != NULL) {
				if (line[0] != 'f')
					process_cmd(line, s, s6, stdin);
				else {
					/* Get the filename */
					for (i = 1; isblank(line[i]); i++);
					if ((p = (char*)strchr(line, '\n'))
					    != NULL)
						*p = '\0';
					process_file(&line[i], s, s6);
				}
			}
		} while (!feof(stdin));
	} else {
		for (i = 1; i < argc; i++) {
			process_file(argv[i], s, s6);
		}
	}

	if (s != -1)
		close(s);
	if (s6 != -1)
		close(s6);

	exit (0);
}

static void
process_file(char *fname, int s, int s6)
{
	char line[80];
	FILE *fp;
	char *lineptr;

	fp = fopen(fname, "r");
	if (fp == NULL) {
		warn("fopen");
		return;
	}

	/* Skip comments and empty lines. */
	while (fgets(line, sizeof(line), fp) != NULL) {
		lineptr = line;
		while (isblank(*lineptr))
			lineptr++;
		if (*lineptr != '#' && *lineptr != '\n')
			process_cmd(lineptr, s, s6, fp);
	}

	fclose(fp);
}

/*
 * Parse join/leave/allow/block arguments, given:
 *  str1: group (as AF_INET or AF_INET6 printable)
 *  str2: ifname
 *  str3: optional source address (may be NULL).
 *   This argument must have the same parsed address family as str1.
 * Return the ifindex of ifname, or 0 if any parse element failed.
 */
static uint32_t
parse_cmd_args(sockunion_t *psu, sockunion_t *psu2,
    const char *str1, const char *str2, const char *str3)
{
	struct addrinfo		 hints;
	struct addrinfo		*res;
	uint32_t		 ifindex;
	int			 af, error;

	assert(psu != NULL);
	assert(str1 != NULL);
	assert(str2 != NULL);

	af = AF_UNSPEC;

	ifindex = if_nametoindex(str2);
	if (ifindex == 0)
		return (0);

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_flags = AI_NUMERICHOST;
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;

	memset(psu, 0, sizeof(sockunion_t));
	psu->sa.sa_family = AF_UNSPEC;

	error = getaddrinfo(str1, "0", &hints, &res);
	if (error) {
		warnx("getaddrinfo: %s", gai_strerror(error));
		return (0);
	}
	assert(res != NULL);
	af = res->ai_family;
	memcpy(psu, res->ai_addr, res->ai_addrlen);
	freeaddrinfo(res);

	/* sscanf() may pass the empty string. */
	if (psu2 != NULL && str3 != NULL && *str3 != '\0') {
		memset(psu2, 0, sizeof(sockunion_t));
		psu2->sa.sa_family = AF_UNSPEC;

		/* look for following address family; str3 is *optional*. */
		hints.ai_family = af;
		error = getaddrinfo(str3, "0", &hints, &res);
		if (error) {
			warnx("getaddrinfo: %s", gai_strerror(error));
			ifindex = 0;
		} else {
			if (af != res->ai_family) {
				errno = EINVAL; /* XXX */
				ifindex = 0;
			}
			memcpy(psu2, res->ai_addr, res->ai_addrlen);
			freeaddrinfo(res);
		}
	}

	return (ifindex);
}

static __inline int
af2sock(const int af, int s, int s6)
{

#ifdef INET
	if (af == AF_INET)
		return (s);
#endif
#ifdef INET6
	if (af == AF_INET6)
		return (s6);
#endif
	return (-1);
}

static __inline int
af2socklen(const int af)
{

#ifdef INET
	if (af == AF_INET)
		return (sizeof(struct sockaddr_in));
#endif
#ifdef INET6
	if (af == AF_INET6)
		return (sizeof(struct sockaddr_in6));
#endif
	return (-1);
}

static void
process_cmd(char *cmd, int s, int s6, FILE *fp __unused)
{
	char			 str1[STR_SIZE];
	char			 str2[STR_SIZE];
	char			 str3[STR_SIZE];
	mrequnion_t		 mr;
	sockunion_t		 su, su2;
	struct ifreq		 ifr;
	char			*line;
	char			*toptname;
	void			*optval;
	uint32_t		 fmode, ifindex;
	socklen_t		 optlen;
	size_t			 j;
	int			 af, error, f, flags, i, level, n, optname;

	af = AF_UNSPEC;
	su.sa.sa_family = AF_UNSPEC;
	su2.sa.sa_family = AF_UNSPEC;

	line = cmd;
	while (isblank(*++line))
		;	/* Skip whitespace. */

	n = 0;
	switch (*cmd) {
	case '?':
		usage();
		break;

	case 'q':
		close(s);
		exit(0);

	case 's':
		if ((sscanf(line, "%d", &n) != 1) || (n < 1)) {
			printf("-1\n");
			break;
		}
		sleep(n);
		printf("ok\n");
		break;

	case 'j':
	case 'l':
		str3[0] = '\0';
		toptname = "";
		sscanf(line, "%s %s %s", str1, str2, str3);
		ifindex = parse_cmd_args(&su, &su2, str1, str2, str3);
		if (ifindex == 0) {
			printf("-1\n");
			break;
		}
		af = su.sa.sa_family;
#ifdef INET
		if (af == AF_INET) {
			struct in_addr ina;

			error = __ifindex_to_primary_ip(ifindex, &ina);
			if (error != 0) {
				warn("primary_ip_lookup %s", str2);
				printf("-1\n");
				break;
			}
			level = IPPROTO_IP;

			if (su2.sa.sa_family != AF_UNSPEC) {
				mr.mrs.imr_multiaddr = su.sin.sin_addr;
				mr.mrs.imr_sourceaddr = su2.sin.sin_addr;
				mr.mrs.imr_interface = ina;
				optname = (*cmd == 'j') ?
				    IP_ADD_SOURCE_MEMBERSHIP :
				    IP_DROP_SOURCE_MEMBERSHIP;
				toptname = (*cmd == 'j') ?
				    "IP_ADD_SOURCE_MEMBERSHIP" :
				    "IP_DROP_SOURCE_MEMBERSHIP";
				optval = (void *)&mr.mrs;
				optlen = sizeof(mr.mrs);
			} else {
				mr.mr.imr_multiaddr = su.sin.sin_addr;
				mr.mr.imr_interface = ina;
				optname = (*cmd == 'j') ?
				    IP_ADD_MEMBERSHIP : IP_DROP_MEMBERSHIP;
				toptname = (*cmd == 'j') ?
				    "IP_ADD_MEMBERSHIP" : "IP_DROP_MEMBERSHIP";
				optval = (void *)&mr.mr;
				optlen = sizeof(mr.mr);
			}
			if (s < 0) {
				warnc(EPROTONOSUPPORT, "setsockopt %s",
				    toptname);
			} else if (setsockopt(s, level, optname, optval,
			    optlen) == 0) {
				printf("ok\n");
				break;
			} else {
				warn("setsockopt %s", toptname);
			}
		}
#ifdef INET6
		else
#endif /* INET with INET6 */
#endif /* INET */
#ifdef INET6
		if (af == AF_INET6) {
			level = IPPROTO_IPV6;
			if (su2.sa.sa_family != AF_UNSPEC) {
				mr.gr.gsr_interface = ifindex;
				mr.gr.gsr_group = su.ss;
				mr.gr.gsr_source = su2.ss;
				optname = (*cmd == 'j') ?
				    MCAST_JOIN_SOURCE_GROUP:
				    MCAST_LEAVE_SOURCE_GROUP;
				toptname = (*cmd == 'j') ?
				    "MCAST_JOIN_SOURCE_GROUP":
				    "MCAST_LEAVE_SOURCE_GROUP";
				optval = (void *)&mr.gr;
				optlen = sizeof(mr.gr);
			} else {
				mr.mr6.ipv6mr_multiaddr = su.sin6.sin6_addr;
				mr.mr6.ipv6mr_interface = ifindex;
				optname = (*cmd == 'j') ?
				    IPV6_JOIN_GROUP :
				    IPV6_LEAVE_GROUP;
				toptname = (*cmd == 'j') ?
				    "IPV6_JOIN_GROUP" :
				    "IPV6_LEAVE_GROUP";
				optval = (void *)&mr.mr6;
				optlen = sizeof(mr.mr6);
			}
			if (s6 < 0) {
				warnc(EPROTONOSUPPORT, "setsockopt %s",
				    toptname);
			} else if (setsockopt(s6, level, optname, optval,
			    optlen) == 0) {
				printf("ok\n");
				break;
			} else {
				warn("setsockopt %s", toptname);
			}
		}
#endif /* INET6 */
		/* FALLTHROUGH */
		printf("-1\n");
		break;

	/*
	 * Set the socket to include or exclude filter mode, and
	 * add some sources to the filterlist, using the full-state API.
	 */
	case 'i':
	case 'e': {
		sockunion_t	 sources[MAX_ADDRS];
		struct addrinfo	 hints;
		struct addrinfo	*res;
		char		*cp;
		int		 af1;

		n = 0;
		fmode = (*cmd == 'i') ? MCAST_INCLUDE : MCAST_EXCLUDE;
		if ((sscanf(line, "%s %s %d", str1, str2, &n)) != 3) {
			printf("-1\n");
			break;
		}

		ifindex = parse_cmd_args(&su, NULL, str1, str2, NULL);
		if (ifindex == 0 || n < 0 || n > MAX_ADDRS) {
			printf("-1\n");
			break;
		}
		af = su.sa.sa_family;
		if (af2sock(af, s, s6) == -1) {
			warnc(EPROTONOSUPPORT, "setsourcefilter");
			break;
		}

		memset(&hints, 0, sizeof(struct addrinfo));
		hints.ai_flags = AI_NUMERICHOST;
		hints.ai_family = af;
		hints.ai_socktype = SOCK_DGRAM;

		for (i = 0; i < n; i++) {
			sockunion_t *psu = (sockunion_t *)&sources[i];
			/*
			 * Trim trailing whitespace, as getaddrinfo()
			 * can't cope with it.
			 */
			fgets(str1, sizeof(str1), fp);
			cp = strchr(str1, '\n');
			if (cp != NULL)
				*cp = '\0';

			res = NULL;
			error = getaddrinfo(str1, "0", &hints, &res);
			if (error)
				break;
			assert(res != NULL);

			memset(psu, 0, sizeof(sockunion_t));
			af1 = res->ai_family;
			if (af1 == af)
				memcpy(psu, res->ai_addr, res->ai_addrlen);
			freeaddrinfo(res);
			if (af1 != af)
				break;
		}
		if (i < n) {
			if (error)
				warnx("getaddrinfo: %s", gai_strerror(error));
			printf("-1\n");
			break;
		}
		if (setsourcefilter(af2sock(af, s, s6), ifindex,
		    &su.sa, su.sa.sa_len, fmode, n, &sources[0].ss) != 0)
			warn("setsourcefilter");
		else
			printf("ok\n");
	} break;

	/*
	 * Allow or block traffic from a source, using the
	 * delta based api.
	 */
	case 't':
	case 'b': {
		str3[0] = '\0';
		toptname = "";
		sscanf(line, "%s %s %s", str1, str2, str3);
		ifindex = parse_cmd_args(&su, &su2, str1, str2, str3);
		if (ifindex == 0 || su2.sa.sa_family == AF_UNSPEC) {
			printf("-1\n");
			break;
		}
		af = su.sa.sa_family;
		if (af2sock(af, s, s6) == -1) {
			warnc(EPROTONOSUPPORT, "getsourcefilter");
			break;
		}

		/* First determine our current filter mode. */
		if (getsourcefilter(af2sock(af, s, s6), ifindex,
		    &su.sa, su.sa.sa_len, &fmode, &n, NULL) != 0) {
			warn("getsourcefilter");
			break;
		}
#ifdef INET
		if (af == AF_INET) {
			struct in_addr ina;

			error = __ifindex_to_primary_ip(ifindex, &ina);
			if (error != 0) {
				warn("primary_ip_lookup %s", str2);
				printf("-1\n");
				break;
			}
			level = IPPROTO_IP;
			optval = (void *)&mr.mrs;
			optlen = sizeof(mr.mrs);
			mr.mrs.imr_multiaddr = su.sin.sin_addr;
			mr.mrs.imr_sourceaddr = su2.sin.sin_addr;
			mr.mrs.imr_interface = ina;
			if (fmode == MCAST_EXCLUDE) {
				/* Any-source mode socket membership. */
				optname = (*cmd == 't') ?
				    IP_UNBLOCK_SOURCE :
				    IP_BLOCK_SOURCE;
				toptname = (*cmd == 't') ?
				    "IP_UNBLOCK_SOURCE" :
				    "IP_BLOCK_SOURCE";
			} else {
				/* Source-specific mode socket membership. */
				optname = (*cmd == 't') ?
				    IP_ADD_SOURCE_MEMBERSHIP :
				    IP_DROP_SOURCE_MEMBERSHIP;
				toptname = (*cmd == 't') ?
				    "IP_ADD_SOURCE_MEMBERSHIP" :
				    "IP_DROP_SOURCE_MEMBERSHIP";
			}
			if (setsockopt(s, level, optname, optval,
			    optlen) == 0) {
				printf("ok\n");
				break;
			} else {
				warn("setsockopt %s", toptname);
			}
		}
#ifdef INET6
		else
#endif /* INET with INET6 */
#endif /* INET */
#ifdef INET6
		if (af == AF_INET6) {
			level = IPPROTO_IPV6;
			mr.gr.gsr_interface = ifindex;
			mr.gr.gsr_group = su.ss;
			mr.gr.gsr_source = su2.ss;
			if (fmode == MCAST_EXCLUDE) {
				/* Any-source mode socket membership. */
				optname = (*cmd == 't') ?
				    MCAST_UNBLOCK_SOURCE :
				    MCAST_BLOCK_SOURCE;
				toptname = (*cmd == 't') ?
				    "MCAST_UNBLOCK_SOURCE" :
				    "MCAST_BLOCK_SOURCE";
			} else {
				/* Source-specific mode socket membership. */
				optname = (*cmd == 't') ?
				    MCAST_JOIN_SOURCE_GROUP :
				    MCAST_LEAVE_SOURCE_GROUP;
				toptname = (*cmd == 't') ?
				    "MCAST_JOIN_SOURCE_GROUP":
				    "MCAST_LEAVE_SOURCE_GROUP";
			}
			optval = (void *)&mr.gr;
			optlen = sizeof(mr.gr);
			if (setsockopt(s6, level, optname, optval,
			    optlen) == 0) {
				printf("ok\n");
				break;
			} else {
				warn("setsockopt %s", toptname);
			}
		}
#endif /* INET6 */
		/* FALLTHROUGH */
		printf("-1\n");
	} break;

	case 'g': {
		sockunion_t	 sources[MAX_ADDRS];
		char		 addrbuf[NI_MAXHOST];
		int		 nreqsrc, nsrc;

		if ((sscanf(line, "%s %s %d", str1, str2, &nreqsrc)) != 3) {
			printf("-1\n");
			break;
		}
		ifindex = parse_cmd_args(&su, NULL, str1, str2, NULL);
		if (ifindex == 0 || (n < 0 || n > MAX_ADDRS)) {
			printf("-1\n");
			break;
		}

		af = su.sa.sa_family;
		if (af2sock(af, s, s6) == -1) {
			warnc(EPROTONOSUPPORT, "getsourcefilter");
			break;
		}
		nsrc = nreqsrc;
		if (getsourcefilter(af2sock(af, s, s6), ifindex, &su.sa,
		    su.sa.sa_len, &fmode, &nsrc, &sources[0].ss) != 0) {
			warn("getsourcefilter");
			printf("-1\n");
			break;
		}
		printf("%s\n", (fmode == MCAST_INCLUDE) ? "include" :
		    "exclude");
		printf("%d\n", nsrc);

		nsrc = MIN(nreqsrc, nsrc);
		fprintf(stderr, "hexdump of sources:\n");
		uint8_t *bp = (uint8_t *)&sources[0];
		for (j = 0; j < (nsrc * sizeof(sources[0])); j++) {
			fprintf(stderr, "%02x", bp[j]);
		}
		fprintf(stderr, "\nend hexdump\n");

		qsort(sources, nsrc, af2socklen(af), su_cmp);
		for (i = 0; i < nsrc; i++) {
			sockunion_t *psu = (sockunion_t *)&sources[i];
			addrbuf[0] = '\0';
			error = getnameinfo(&psu->sa, psu->sa.sa_len,
			    addrbuf, sizeof(addrbuf), NULL, 0,
			    NI_NUMERICHOST);
			if (error)
				warnx("getnameinfo: %s", gai_strerror(error));
			else
				printf("%s\n", addrbuf);
		}
		printf("ok\n");
	} break;

	/* link-layer stuff follows. */

	case 'a':
	case 'd': {
		struct sockaddr_dl	*dlp;
		struct ether_addr	*ep;

		memset(&ifr, 0, sizeof(struct ifreq));
		dlp = (struct sockaddr_dl *)&ifr.ifr_addr;
		dlp->sdl_len = sizeof(struct sockaddr_dl);
		dlp->sdl_family = AF_LINK;
		dlp->sdl_index = 0;
		dlp->sdl_nlen = 0;
		dlp->sdl_alen = ETHER_ADDR_LEN;
		dlp->sdl_slen = 0;
		if (sscanf(line, "%s %s", str1, str2) != 2) {
			warnc(EINVAL, "sscanf");
			break;
		}
		ep = ether_aton(str2);
		if (ep == NULL) {
			warnc(EINVAL, "ether_aton");
			break;
		}
		strlcpy(ifr.ifr_name, str1, IF_NAMESIZE);
		memcpy(LLADDR(dlp), ep, ETHER_ADDR_LEN);
		if (ioctl(s, (*cmd == 'a') ? SIOCADDMULTI : SIOCDELMULTI,
		    &ifr) == -1) {
			warn("ioctl SIOCADDMULTI/SIOCDELMULTI");
			printf("-1\n");
		} else
			printf("ok\n");
		break;
	}

	case 'm':
		fprintf(stderr,
		    "warning: IFF_ALLMULTI cannot be set from userland "
		    "in FreeBSD; command ignored.\n");
		printf("-1\n");
		break;

	case 'p':
		if (sscanf(line, "%s %u", ifr.ifr_name, &f) != 2) {
			printf("-1\n");
			break;
		}
		if (ioctl(s, SIOCGIFFLAGS, &ifr) == -1) {
			warn("ioctl SIOCGIFFLAGS");
			break;
		}
		flags = (ifr.ifr_flags & 0xffff) | (ifr.ifr_flagshigh << 16);
		if (f == 0) {
			flags &= ~IFF_PPROMISC;
		} else {
			flags |= IFF_PPROMISC;
		}
		ifr.ifr_flags = flags & 0xffff;
		ifr.ifr_flagshigh = flags >> 16;
		if (ioctl(s, SIOCSIFFLAGS, &ifr) == -1)
			warn("ioctl SIOCGIFFLAGS");
		else
			printf( "changed to 0x%08x\n", flags );
		break;

	case '\n':
		break;
	default:
		printf("invalid command\n");
		break;
	}
}

static void
usage(void)
{

	printf("j mcast-addr ifname [src-addr] - join IP multicast group\n");
	printf("l mcast-addr ifname [src-addr] - leave IP multicast group\n");
	printf(
"i mcast-addr ifname n          - set n include mode src filter\n");
	printf(
"e mcast-addr ifname n          - set n exclude mode src filter\n");
	printf("t mcast-addr ifname src-addr  - allow traffic from src\n");
	printf("b mcast-addr ifname src-addr  - block traffic from src\n");
	printf("g mcast-addr ifname n        - get and show n src filters\n");
	printf("a ifname mac-addr          - add link multicast filter\n");
	printf("d ifname mac-addr          - delete link multicast filter\n");
	printf("m ifname 1/0               - set/clear ether allmulti flag\n");
	printf("p ifname 1/0               - set/clear ether promisc flag\n");
	printf("f filename                 - read command(s) from file\n");
	printf("s seconds                  - sleep for some time\n");
	printf("q                          - quit\n");
}

