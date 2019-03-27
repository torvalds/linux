/*-
 * Copyright (c) 2007 Bruce M. Simpson
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
 * SUCH DAMAGE.
 */

/*
 * Regression test utility for RFC 3678 Advanced Multicast API in FreeBSD.
 *
 * TODO: Test the SSM paths.
 * TODO: Support INET6. The code has been written to facilitate this later.
 * TODO: Merge multicast socket option tests from ipsockopt.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <pwd.h>
#include <setjmp.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h>

#ifndef __SOCKUNION_DECLARED
union sockunion {
	struct sockaddr_storage	ss;
	struct sockaddr		sa;
	struct sockaddr_dl	sdl;
	struct sockaddr_in	sin;
#ifdef INET6
	struct sockaddr_in6	sin6;
#endif
};
typedef union sockunion sockunion_t;
#define __SOCKUNION_DECLARED
#endif /* __SOCKUNION_DECLARED */

#define ADDRBUF_LEN		16
#define DEFAULT_GROUP_STR	"238.1.1.0"
#define DEFAULT_IFNAME		"lo0"
#define DEFAULT_IFADDR_STR	"127.0.0.1"
#define DEFAULT_PORT		6698
#define DEFAULT_TIMEOUT		0		/* don't wait for traffic */
#define RXBUFSIZE		2048

static sockunion_t	 basegroup;
static const char	*basegroup_str = NULL;
static int		 dobindaddr = 0;
static int		 dodebug = 1;
static int		 doipv4 = 0;
static int		 domiscopts = 0;
static int		 dorandom = 0;
static int		 doreuseport = 0;
static int		 dossm = 0;
static int		 dossf = 0;
static int		 doverbose = 0;
static sockunion_t	 ifaddr;
static const char	*ifaddr_str = NULL;
static uint32_t		 ifindex = 0;
static const char	*ifname = NULL;
struct in_addr		*ipv4_sources = NULL;
static jmp_buf		 jmpbuf;
static size_t		 nmcastgroups = IP_MAX_MEMBERSHIPS;
static size_t		 nmcastsources = 0;
static uint16_t		 portno = DEFAULT_PORT;
static char		*progname = NULL;
struct sockaddr_storage	*ss_sources = NULL;
static uint32_t		 timeout = 0;

static int	do_asm_ipv4(void);
static int	do_asm_pim(void);
#ifdef notyet
static int	do_misc_opts(void);
#endif
static int	do_ssf_ipv4(void);
static int	do_ssf_pim(void);
static int	do_ssm_ipv4(void);
static int	do_ssm_pim(void);
static int	open_and_bind_socket(sockunion_t *);
static int	recv_loop_with_match(int, sockunion_t *, sockunion_t *);
static void	signal_handler(int);
static void	usage(void);

/*
 * Test the IPv4 set/getipv4sourcefilter() libc API functions.
 * Build a single socket.
 * Join a source group.
 * Repeatedly change the source filters via setipv4sourcefilter.
 * Read it back with getipv4sourcefilter up to IP_MAX_SOURCES
 * and check for inconsistency.
 */
static int
do_ssf_ipv4(void)
{

	fprintf(stderr, "not yet implemented\n");
	return (0);
}

/*
 * Test the protocol-independent set/getsourcefilter() functions.
 */
static int
do_ssf_pim(void)
{

	fprintf(stderr, "not yet implemented\n");
	return (0);
}

/*
 * Test the IPv4 ASM API.
 * Repeatedly join, block sources, unblock and leave groups.
 */
static int
do_asm_ipv4(void)
{
	int			 error;
	char			 gaddrbuf[ADDRBUF_LEN];
	int			 i;
	sockunion_t		 laddr;
	struct ip_mreq		 mreq;
	struct ip_mreq_source	 mreqs;
	in_addr_t		 ngroupbase;
	char			 saddrbuf[ADDRBUF_LEN];
	int			 sock;
	sockunion_t		 tmpgroup;
	sockunion_t		 tmpsource;

	memset(&mreq, 0, sizeof(struct ip_mreq));
	memset(&mreqs, 0, sizeof(struct ip_mreq_source));
	memset(&laddr, 0, sizeof(sockunion_t));

	if (dobindaddr) {
		laddr = ifaddr;
	} else {
		laddr.sin.sin_family = AF_INET;
		laddr.sin.sin_len = sizeof(struct sockaddr_in);
		laddr.sin.sin_addr.s_addr = INADDR_ANY;
	}
	laddr.sin.sin_port = htons(portno);

	tmpgroup = basegroup;
	ngroupbase = ntohl(basegroup.sin.sin_addr.s_addr) + 1;	/* XXX */
	tmpgroup.sin.sin_addr.s_addr = htonl(ngroupbase);

	sock = open_and_bind_socket(&laddr);
	if (sock == -1)
		return (EX_OSERR);

	for (i = 0; i < (signed)nmcastgroups; i++) {
		mreq.imr_multiaddr.s_addr = htonl((ngroupbase + i));
		mreq.imr_interface = ifaddr.sin.sin_addr;
		if (doverbose) {
			inet_ntop(AF_INET, &mreq.imr_multiaddr, gaddrbuf,
			    sizeof(gaddrbuf));
			fprintf(stderr, "IP_ADD_MEMBERSHIP %s %s\n",
			    gaddrbuf, inet_ntoa(mreq.imr_interface));
		}
		error = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
		    &mreq, sizeof(struct ip_mreq));
		if (error < 0) {
			warn("setsockopt IP_ADD_MEMBERSHIP");
			close(sock);
			return (EX_OSERR);
		}
	}

	/*
	 * If no test sources auto-generated or specified on command line,
	 * skip source filter portion of ASM test.
	*/
	if (nmcastsources == 0)
		goto skipsources;

	/*
	 * Begin blocking sources on the first group chosen.
	 */
	for (i = 0; i < (signed)nmcastsources; i++) {
		mreqs.imr_multiaddr = tmpgroup.sin.sin_addr;
		mreqs.imr_interface = ifaddr.sin.sin_addr;
		mreqs.imr_sourceaddr = ipv4_sources[i];
		if (doverbose) {
			inet_ntop(AF_INET, &mreqs.imr_multiaddr, gaddrbuf,
			    sizeof(gaddrbuf));
			inet_ntop(AF_INET, &mreqs.imr_sourceaddr, saddrbuf,
			    sizeof(saddrbuf));
			fprintf(stderr, "IP_BLOCK_SOURCE %s %s %s\n",
			    gaddrbuf, inet_ntoa(mreqs.imr_interface),
			    saddrbuf);
		}
		error = setsockopt(sock, IPPROTO_IP, IP_BLOCK_SOURCE, &mreqs,
		    sizeof(struct ip_mreq_source));
		if (error < 0) {
			warn("setsockopt IP_BLOCK_SOURCE");
			close(sock);
			return (EX_OSERR);
		}
	}

	/*
	 * Choose the first group and source for a match.
	 * Enter the I/O loop.
	 */
	memset(&tmpsource, 0, sizeof(sockunion_t));
	tmpsource.sin.sin_family = AF_INET;
	tmpsource.sin.sin_len = sizeof(struct sockaddr_in);
	tmpsource.sin.sin_addr = ipv4_sources[0];

	error = recv_loop_with_match(sock, &tmpgroup, &tmpsource);

	/*
	 * Unblock sources.
	 */
	for (i = nmcastsources-1; i >= 0; i--) {
		mreqs.imr_multiaddr = tmpgroup.sin.sin_addr;
		mreqs.imr_interface = ifaddr.sin.sin_addr;
		mreqs.imr_sourceaddr = ipv4_sources[i];
		if (doverbose) {
			inet_ntop(AF_INET, &mreqs.imr_multiaddr, gaddrbuf,
			    sizeof(gaddrbuf));
			inet_ntop(AF_INET, &mreqs.imr_sourceaddr, saddrbuf,
			    sizeof(saddrbuf));
			fprintf(stderr, "IP_UNBLOCK_SOURCE %s %s %s\n",
			    gaddrbuf, inet_ntoa(mreqs.imr_interface),
			    saddrbuf);
		}
		error = setsockopt(sock, IPPROTO_IP, IP_UNBLOCK_SOURCE, &mreqs,
		    sizeof(struct ip_mreq_source));
		if (error < 0) {
			warn("setsockopt IP_UNBLOCK_SOURCE");
			close(sock);
			return (EX_OSERR);
		}
	}

skipsources:
	/*
	 * Leave groups.
	 */
	for (i = nmcastgroups-1; i >= 0; i--) {
		mreq.imr_multiaddr.s_addr = htonl((ngroupbase + i));
		mreq.imr_interface = ifaddr.sin.sin_addr;
		if (doverbose) {
			inet_ntop(AF_INET, &mreq.imr_multiaddr, gaddrbuf,
			    sizeof(gaddrbuf));
			fprintf(stderr, "IP_DROP_MEMBERSHIP %s %s\n",
			    gaddrbuf, inet_ntoa(mreq.imr_interface));
		}
		error = setsockopt(sock, IPPROTO_IP, IP_DROP_MEMBERSHIP,
		    &mreq, sizeof(struct ip_mreq));
		if (error < 0) {
			warn("setsockopt IP_DROP_MEMBERSHIP");
			close(sock);
			return (EX_OSERR);
		}
	}

	return (0);
}

static int
do_asm_pim(void)
{

	fprintf(stderr, "not yet implemented\n");
	return (0);
}

#ifdef notyet
/*
 * Test misceallaneous IPv4 options.
 */
static int
do_misc_opts(void)
{
	int sock;

	sock = open_and_bind_socket(NULL);
	if (sock == -1)
		return (EX_OSERR);
	test_ip_uchar(sock, socktypename, IP_MULTICAST_TTL,
	    "IP_MULTICAST_TTL", 1);
	close(sock);

	sock = open_and_bind_socket(NULL);
	if (sock == -1)
		return (EX_OSERR);
	test_ip_boolean(sock, socktypename, IP_MULTICAST_LOOP,
	    "IP_MULTICAST_LOOP", 1, BOOLEAN_ANYONE);
	close(sock);

	return (0);
}
#endif

/*
 * Test the IPv4 SSM API.
 */
static int
do_ssm_ipv4(void)
{

	fprintf(stderr, "not yet implemented\n");
	return (0);
}

/*
 * Test the protocol-independent SSM API with IPv4 addresses.
 */
static int
do_ssm_pim(void)
{

	fprintf(stderr, "not yet implemented\n");
	return (0);
}

int
main(int argc, char *argv[])
{
	struct addrinfo		 aih;
	struct addrinfo		*aip;
	int			 ch;
	int			 error;
	int			 exitval;
	size_t			 i;
	struct in_addr		*pina;
	struct sockaddr_storage	*pbss;

	ifname = DEFAULT_IFNAME;
	ifaddr_str = DEFAULT_IFADDR_STR;
	basegroup_str = DEFAULT_GROUP_STR;
	ifname = DEFAULT_IFNAME;
	portno = DEFAULT_PORT;
	basegroup.ss.ss_family = AF_UNSPEC;
	ifaddr.ss.ss_family = AF_UNSPEC;

	progname = basename(argv[0]);
	while ((ch = getopt(argc, argv, "4bg:i:I:mM:p:rsS:tT:v")) != -1) {
		switch (ch) {
		case '4':
			doipv4 = 1;
			break;
		case 'b':
			dobindaddr = 1;
			break;
		case 'g':
			basegroup_str = optarg;
			break;
		case 'i':
			ifname = optarg;
			break;
		case 'I':
			ifaddr_str = optarg;
			break;
		case 'm':
			usage();	/* notyet */
			/*NOTREACHED*/
			domiscopts = 1;
			break;
		case 'M':
			nmcastgroups = atoi(optarg);
			break;
		case 'p':
			portno = atoi(optarg);
			break;
		case 'r':
			doreuseport = 1;
			break;
		case 'S':
			nmcastsources = atoi(optarg);
			break;
		case 's':
			dossm = 1;
			break;
		case 't':
			dossf = 1;
			break;
		case 'T':
			timeout = atoi(optarg);
			break;
		case 'v':
			doverbose = 1;
			break;
		default:
			usage();
			break;
			/*NOTREACHED*/
		}
	}
	argc -= optind;
	argv += optind;

	memset(&aih, 0, sizeof(struct addrinfo));
	aih.ai_flags = AI_NUMERICHOST | AI_PASSIVE;
	aih.ai_family = PF_INET;
	aih.ai_socktype = SOCK_DGRAM;
	aih.ai_protocol = IPPROTO_UDP;

	/*
	 * Fill out base group.
	 */
	aip = NULL;
	error = getaddrinfo(basegroup_str, NULL, &aih, &aip);
	if (error != 0) {
		fprintf(stderr, "%s: getaddrinfo: %s\n", progname,
		    gai_strerror(error));
		exit(EX_USAGE);
	}
	memcpy(&basegroup, aip->ai_addr, aip->ai_addrlen);
	if (dodebug) {
		fprintf(stderr, "debug: gai thinks %s is %s\n",
		    basegroup_str, inet_ntoa(basegroup.sin.sin_addr));
	}
	freeaddrinfo(aip);

	assert(basegroup.ss.ss_family == AF_INET);

	/*
	 * If user specified interface as an address, and protocol
	 * specific APIs were selected, parse it.
	 * Otherwise, parse interface index from name if protocol
	 * independent APIs were selected (the default).
	 */
	if (doipv4) {
		if (ifaddr_str == NULL) {
			warnx("required argument missing: ifaddr");
			usage();
			/* NOTREACHED */
		}
		aip = NULL;
		error = getaddrinfo(ifaddr_str, NULL, &aih, &aip);
		if (error != 0) {
			fprintf(stderr, "%s: getaddrinfo: %s\n", progname,
			    gai_strerror(error));
			exit(EX_USAGE);
		}
		memcpy(&ifaddr, aip->ai_addr, aip->ai_addrlen);
		if (dodebug) {
			fprintf(stderr, "debug: gai thinks %s is %s\n",
			    ifaddr_str, inet_ntoa(ifaddr.sin.sin_addr));
		}
		freeaddrinfo(aip);
	}

	if (!doipv4) {
		if (ifname == NULL) {
			warnx("required argument missing: ifname");
			usage();
			/* NOTREACHED */
		}
		ifindex = if_nametoindex(ifname);
		if (ifindex == 0)
			err(EX_USAGE, "if_nametoindex");
	}

	/*
	 * Introduce randomness into group base if specified.
	 */
	if (dorandom) {
		in_addr_t ngroupbase;

		srandomdev();
		ngroupbase = ntohl(basegroup.sin.sin_addr.s_addr);
		ngroupbase |= ((random() % ((1 << 11) - 1)) << 16);
		basegroup.sin.sin_addr.s_addr = htonl(ngroupbase);
	}

	if (argc > 0) {
		nmcastsources = argc;
		if (doipv4) {
			ipv4_sources = calloc(nmcastsources,
			    sizeof(struct in_addr));
			if (ipv4_sources == NULL) {
				exitval = EX_OSERR;
				goto out;
			}
		} else {
			ss_sources = calloc(nmcastsources,
			    sizeof(struct sockaddr_storage));
			if (ss_sources == NULL) {
				exitval = EX_OSERR;
				goto out;
			}
		}
	}

	/*
	 * Parse source list, if any were specified on the command line.
	 */
	assert(aih.ai_family == PF_INET);
	pbss = ss_sources;
	pina = ipv4_sources;
	for (i = 0; i < (size_t)argc; i++) {
		aip = NULL;
		error = getaddrinfo(argv[i], NULL, &aih, &aip);
		if (error != 0) {
			fprintf(stderr, "getaddrinfo: %s\n",
			    gai_strerror(error));
			exitval = EX_USAGE;
			goto out;
		}
		if (doipv4) {
			struct sockaddr_in *sin =
			    (struct sockaddr_in *)aip->ai_addr;
			*pina++ = sin->sin_addr;
		} else {
			memcpy(pbss++, aip->ai_addr, aip->ai_addrlen);
		}
		freeaddrinfo(aip);
	}

	/*
	 * Perform the regression tests which the user requested.
	 */
#ifdef notyet
	if (domiscopts) {
		exitval = do_misc_opts();
		if (exitval)
			goto out;
	}
#endif
	if (doipv4) {
		/* IPv4 protocol specific API tests */
		if (dossm) {
			/* Source-specific multicast */
			exitval = do_ssm_ipv4();
			if (exitval)
				goto out;
			if (dossf) {
				/* Do setipvsourcefilter() too */
				exitval = do_ssf_ipv4();
			}
		} else {
			/* Any-source multicast */
			exitval = do_asm_ipv4();
		}
	} else {
		/* Protocol independent API tests */
		if (dossm) {
			/* Source-specific multicast */
			exitval = do_ssm_pim();
			if (exitval)
				goto out;
			if (dossf) {
				/* Do setsourcefilter() too */
				exitval = do_ssf_pim();
			}
		} else {
			/* Any-source multicast */
			exitval = do_asm_pim();
		}
	}

out:
	if (ipv4_sources != NULL)
		free(ipv4_sources);

	if (ss_sources != NULL)
		free(ss_sources);

	exit(exitval);
}

static int
open_and_bind_socket(sockunion_t *bsu)
{
	int	 error, optval, sock;

	sock = -1;

	sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == -1) {
		warn("socket");
		return (-1);
	}

	if (doreuseport) {
		optval = 1;
		if (setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &optval,
		    sizeof(optval)) < 0) {
			warn("setsockopt SO_REUSEPORT");
			close(sock);
			return (-1);
		}
	}

	if (bsu != NULL) {
		error = bind(sock, &bsu->sa, bsu->sa.sa_len);
		if (error == -1) {
			warn("bind");
			close(sock);
			return (-1);
		}
	}

	return (sock);
}

/*
 * Protocol-agnostic multicast I/O loop.
 *
 * Wait for 'timeout' seconds looking for traffic on group, so that manual
 * or automated regression tests (possibly running on another host) have an
 * opportunity to transmit within the group to test source filters.
 *
 * If the filter failed, this loop will report if we received traffic
 * from the source we elected to monitor.
 */
static int
recv_loop_with_match(int sock, sockunion_t *group, sockunion_t *source)
{
	int		 error;
	sockunion_t	 from;
	char		 groupname[NI_MAXHOST];
	ssize_t		 len;
	size_t		 npackets;
	int		 jmpretval;
	char		 rxbuf[RXBUFSIZE];
	char		 sourcename[NI_MAXHOST];

	assert(source->sa.sa_family == AF_INET);

	/*
	 * Return immediately if we don't need to wait for traffic.
	 */
	if (timeout == 0)
		return (0);

	error = getnameinfo(&group->sa, group->sa.sa_len, groupname,
	    NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
	if (error) {
		fprintf(stderr, "getnameinfo: %s\n", gai_strerror(error));
		return (error);
	}

	error = getnameinfo(&source->sa, source->sa.sa_len, sourcename,
	    NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
	if (error) {
		fprintf(stderr, "getnameinfo: %s\n", gai_strerror(error));
		return (error);
	}

	fprintf(stdout,
	    "Waiting %d seconds for inbound traffic on group %s\n"
	    "Expecting no traffic from blocked source: %s\n",
	    (int)timeout, groupname, sourcename);

	signal(SIGINT, signal_handler);
	signal(SIGALRM, signal_handler);

	error = 0;
	npackets = 0;
	alarm(timeout);
	while (0 == (jmpretval = setjmp(jmpbuf))) {
		len = recvfrom(sock, rxbuf, RXBUFSIZE, 0, &from.sa,
		    (socklen_t *)&from.sa.sa_len);
		if (dodebug) {
			fprintf(stderr, "debug: packet received from %s\n",
			    inet_ntoa(from.sin.sin_addr));
		}
		if (source &&
		    source->sin.sin_addr.s_addr == from.sin.sin_addr.s_addr)
			break;
		npackets++;
	}

	if (doverbose) {
		fprintf(stderr, "Number of datagrams received from "
		    "non-blocked sources: %d\n", (int)npackets);
	}

	switch (jmpretval) {
	case SIGALRM:	/* ok */
		break;
	case SIGINT:	/* go bye bye */
		fprintf(stderr, "interrupted\n");
		error = 20;
		break;
	case 0:		/* Broke out of loop; saw a bad source. */
		fprintf(stderr, "FAIL: got packet from blocked source\n");
		error = EX_IOERR;
		break;
	default:
		warnx("recvfrom");
		error = EX_OSERR;
		break;
	}

	signal(SIGINT, SIG_DFL);
	signal(SIGALRM, SIG_DFL);

	return (error);
}

static void
signal_handler(int signo)
{

	longjmp(jmpbuf, signo);
}

static void
usage(void)
{

	fprintf(stderr, "\nIP multicast regression test utility\n");
	fprintf(stderr,
"usage: %s [-4] [-b] [-g groupaddr] [-i ifname] [-I ifaddr] [-m]\n"
"       [-M ngroups] [-p portno] [-r] [-R] [-s] [-S nsources] [-t] [-T timeout]\n"
"       [-v] [blockaddr ...]\n\n", progname);
	fprintf(stderr, "-4: Use IPv4 API "
	                "(default: Use protocol-independent API)\n");
	fprintf(stderr, "-b: bind listening socket to ifaddr "
	    "(default: INADDR_ANY)\n");
	fprintf(stderr, "-g: Base IPv4 multicast group to join (default: %s)\n",
	    DEFAULT_GROUP_STR);
	fprintf(stderr, "-i: interface for multicast joins (default: %s)\n",
	    DEFAULT_IFNAME);
	fprintf(stderr, "-I: IPv4 address to join groups on, if using IPv4 "
	    "API\n    (default: %s)\n", DEFAULT_IFADDR_STR);
#ifdef notyet
	fprintf(stderr, "-m: Test misc IPv4 multicast socket options "
	    "(default: off)\n");
#endif
	fprintf(stderr, "-M: Number of multicast groups to join "
	    "(default: %d)\n", (int)nmcastgroups);
	fprintf(stderr, "-p: Set local and remote port (default: %d)\n",
	    DEFAULT_PORT);
	fprintf(stderr, "-r: Set SO_REUSEPORT on (default: off)\n");
	fprintf(stderr, "-R: Randomize groups/sources (default: off)\n");
	fprintf(stderr, "-s: Test source-specific API "
	    "(default: test any-source API)\n");
	fprintf(stderr, "-S: Number of multicast sources to generate if\n"
	    "    none specified on command line (default: %d)\n",
	    (int)nmcastsources);
	fprintf(stderr, "-t: Test get/setNsourcefilter() (default: off)\n");
	fprintf(stderr, "-T: Timeout to wait for blocked traffic on first "
	    "group (default: %d)\n", DEFAULT_TIMEOUT);
	fprintf(stderr, "-v: Be verbose (default: off)\n");
	fprintf(stderr, "\nRemaining arguments are treated as a list of IPv4 "
	    "sources to filter.\n\n");

	exit(EX_USAGE);
}
