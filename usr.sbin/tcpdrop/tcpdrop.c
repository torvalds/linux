/* $OpenBSD: tcpdrop.c,v 1.4 2004/05/22 23:55:22 deraadt Exp $ */

/*-
 * Copyright (c) 2009 Juli Mallett <jmallett@FreeBSD.org>
 * Copyright (c) 2004 Markus Friedl <markus@openbsd.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>

#include <netinet/in.h>
#include <netinet/in_pcb.h>
#define TCPSTATES
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_var.h>

#include <err.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	TCPDROP_FOREIGN		0
#define	TCPDROP_LOCAL		1

struct host_service {
	char hs_host[NI_MAXHOST];
	char hs_service[NI_MAXSERV];
};

static bool tcpdrop_list_commands = false;

static char *findport(const char *);
static struct xinpgen *getxpcblist(const char *);
static void sockinfo(const struct sockaddr *, struct host_service *);
static bool tcpdrop(const struct sockaddr *, const struct sockaddr *);
static bool tcpdropall(const char *, int);
static bool tcpdropbyname(const char *, const char *, const char *,
    const char *);
static bool tcpdropconn(const struct in_conninfo *);
static void usage(void);

/*
 * Drop a tcp connection.
 */
int
main(int argc, char *argv[])
{
	char stack[TCP_FUNCTION_NAME_LEN_MAX];
	char *lport, *fport;
	bool dropall, dropallstack;
	int ch, state;

	dropall = false;
	dropallstack = false;
	stack[0] = '\0';
	state = -1;

	while ((ch = getopt(argc, argv, "alS:s:")) != -1) {
		switch (ch) {
		case 'a':
			dropall = true;
			break;
		case 'l':
			tcpdrop_list_commands = true;
			break;
		case 'S':
			dropallstack = true;
			strlcpy(stack, optarg, sizeof(stack));
			break;
		case 's':
			dropallstack = true;
			for (state = 0; state < TCP_NSTATES; state++) {
				if (strcmp(tcpstates[state], optarg) == 0)
					break;
			}
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (state == TCP_NSTATES ||
	    state == TCPS_CLOSED ||
	    state == TCPS_LISTEN)
		usage();
	if (dropall && dropallstack)
		usage();
	if (dropall || dropallstack) {
		if (argc != 0)
			usage();
		if (!tcpdropall(stack, state))
			exit(1);
		exit(0);
	}

	if ((argc != 2 && argc != 4) || tcpdrop_list_commands)
		usage();

	if (argc == 2) {
		lport = findport(argv[0]);
		fport = findport(argv[1]);
		if (lport == NULL || lport[1] == '\0' || fport == NULL ||
		    fport[1] == '\0')
			usage();
		*lport++ = '\0';
		*fport++ = '\0';
		if (!tcpdropbyname(argv[0], lport, argv[1], fport))
			exit(1);
	} else if (!tcpdropbyname(argv[0], argv[1], argv[2], argv[3]))
		exit(1);

	exit(0);
}

static char *
findport(const char *arg)
{
	char *dot, *colon;

	/* A strrspn() or strrpbrk() would be nice. */
	dot = strrchr(arg, '.');
	colon = strrchr(arg, ':');
	if (dot == NULL)
		return (colon);
	if (colon == NULL)
		return (dot);
	if (dot < colon)
		return (colon);
	else
		return (dot);
}

static struct xinpgen *
getxpcblist(const char *name)
{
	struct xinpgen *xinp;
	size_t len;
	int rv;

	len = 0;
	rv = sysctlbyname(name, NULL, &len, NULL, 0);
	if (rv == -1)
		err(1, "sysctlbyname %s", name);

	if (len == 0)
		errx(1, "%s is empty", name);

	xinp = malloc(len);
	if (xinp == NULL)
		errx(1, "malloc failed");

	rv = sysctlbyname(name, xinp, &len, NULL, 0);
	if (rv == -1)
		err(1, "sysctlbyname %s", name);

	return (xinp);
}

static void
sockinfo(const struct sockaddr *sa, struct host_service *hs)
{
	static const int flags = NI_NUMERICHOST | NI_NUMERICSERV;
	int rv;

	rv = getnameinfo(sa, sa->sa_len, hs->hs_host, sizeof hs->hs_host,
	    hs->hs_service, sizeof hs->hs_service, flags);
	if (rv == -1)
		err(1, "getnameinfo");
}

static bool
tcpdrop(const struct sockaddr *lsa, const struct sockaddr *fsa)
{
	struct host_service local, foreign;
	struct sockaddr_storage addrs[2];
	int rv;

	memcpy(&addrs[TCPDROP_FOREIGN], fsa, fsa->sa_len);
	memcpy(&addrs[TCPDROP_LOCAL], lsa, lsa->sa_len);

	sockinfo(lsa, &local);
	sockinfo(fsa, &foreign);

	if (tcpdrop_list_commands) {
		printf("tcpdrop %s %s %s %s\n", local.hs_host, local.hs_service,
		    foreign.hs_host, foreign.hs_service);
		return (true);
	}

	rv = sysctlbyname("net.inet.tcp.drop", NULL, NULL, &addrs,
	    sizeof addrs);
	if (rv == -1) {
		warn("%s %s %s %s", local.hs_host, local.hs_service,
		    foreign.hs_host, foreign.hs_service);
		return (false);
	}
	printf("%s %s %s %s: dropped\n", local.hs_host, local.hs_service,
	    foreign.hs_host, foreign.hs_service);
	return (true);
}

static bool
tcpdropall(const char *stack, int state)
{
	struct xinpgen *head, *xinp;
	struct xtcpcb *xtp;
	struct xinpcb *xip;
	bool ok;

	ok = true;

	head = getxpcblist("net.inet.tcp.pcblist");

#define	XINP_NEXT(xinp)							\
	((struct xinpgen *)(uintptr_t)((uintptr_t)(xinp) + (xinp)->xig_len))

	for (xinp = XINP_NEXT(head); xinp->xig_len > sizeof *xinp;
	    xinp = XINP_NEXT(xinp)) {
		xtp = (struct xtcpcb *)xinp;
		xip = &xtp->xt_inp;

		/*
		 * XXX
		 * Check protocol, support just v4 or v6, etc.
		 */

		/* Ignore PCBs which were freed during copyout.  */
		if (xip->inp_gencnt > head->xig_gen)
			continue;

		/* Skip listening sockets.  */
		if (xtp->t_state == TCPS_LISTEN)
			continue;

		/* If requested, skip sockets not having the requested state. */
		if ((state != -1) && (xtp->t_state != state))
			continue;

		/* If requested, skip sockets not having the requested stack. */
		if (stack[0] != '\0' &&
		    strncmp(xtp->xt_stack, stack, TCP_FUNCTION_NAME_LEN_MAX))
			continue;

		if (!tcpdropconn(&xip->inp_inc))
			ok = false;
	}
	free(head);

	return (ok);
}

static bool
tcpdropbyname(const char *lhost, const char *lport, const char *fhost,
    const char *fport)
{
	static const struct addrinfo hints = {
		/*
		 * Look for streams in all domains.
		 */
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM,
	};
	struct addrinfo *ail, *local, *aif, *foreign;
	int error;
	bool ok, infamily;

	error = getaddrinfo(lhost, lport, &hints, &local);
	if (error != 0)
		errx(1, "getaddrinfo: %s port %s: %s", lhost, lport,
		    gai_strerror(error));

	error = getaddrinfo(fhost, fport, &hints, &foreign);
	if (error != 0) {
		freeaddrinfo(local); /* XXX gratuitous */
		errx(1, "getaddrinfo: %s port %s: %s", fhost, fport,
		    gai_strerror(error));
	}

	ok = true;
	infamily = false;

	/*
	 * Try every combination of local and foreign address pairs.
	 */
	for (ail = local; ail != NULL; ail = ail->ai_next) {
		for (aif = foreign; aif != NULL; aif = aif->ai_next) {
			if (ail->ai_family != aif->ai_family)
				continue;
			infamily = true;
			if (!tcpdrop(ail->ai_addr, aif->ai_addr))
				ok = false;
		}
	}

	if (!infamily) {
		warnx("%s %s %s %s: different address families", lhost, lport,
		    fhost, fport);
		ok = false;
	}

	freeaddrinfo(local);
	freeaddrinfo(foreign);

	return (ok);
}

static bool
tcpdropconn(const struct in_conninfo *inc)
{
	struct sockaddr *local, *foreign;
	struct sockaddr_in6 sin6[2];
	struct sockaddr_in sin4[2];

	if ((inc->inc_flags & INC_ISIPV6) != 0) {
		memset(sin6, 0, sizeof sin6);

		sin6[TCPDROP_LOCAL].sin6_len = sizeof sin6[TCPDROP_LOCAL];
		sin6[TCPDROP_LOCAL].sin6_family = AF_INET6;
		sin6[TCPDROP_LOCAL].sin6_port = inc->inc_lport;
		memcpy(&sin6[TCPDROP_LOCAL].sin6_addr, &inc->inc6_laddr,
		    sizeof inc->inc6_laddr);
		local = (struct sockaddr *)&sin6[TCPDROP_LOCAL];

		sin6[TCPDROP_FOREIGN].sin6_len = sizeof sin6[TCPDROP_FOREIGN];
		sin6[TCPDROP_FOREIGN].sin6_family = AF_INET6;
		sin6[TCPDROP_FOREIGN].sin6_port = inc->inc_fport;
		memcpy(&sin6[TCPDROP_FOREIGN].sin6_addr, &inc->inc6_faddr,
		    sizeof inc->inc6_faddr);
		foreign = (struct sockaddr *)&sin6[TCPDROP_FOREIGN];
	} else {
		memset(sin4, 0, sizeof sin4);

		sin4[TCPDROP_LOCAL].sin_len = sizeof sin4[TCPDROP_LOCAL];
		sin4[TCPDROP_LOCAL].sin_family = AF_INET;
		sin4[TCPDROP_LOCAL].sin_port = inc->inc_lport;
		memcpy(&sin4[TCPDROP_LOCAL].sin_addr, &inc->inc_laddr,
		    sizeof inc->inc_laddr);
		local = (struct sockaddr *)&sin4[TCPDROP_LOCAL];

		sin4[TCPDROP_FOREIGN].sin_len = sizeof sin4[TCPDROP_FOREIGN];
		sin4[TCPDROP_FOREIGN].sin_family = AF_INET;
		sin4[TCPDROP_FOREIGN].sin_port = inc->inc_fport;
		memcpy(&sin4[TCPDROP_FOREIGN].sin_addr, &inc->inc_faddr,
		    sizeof inc->inc_faddr);
		foreign = (struct sockaddr *)&sin4[TCPDROP_FOREIGN];
	}

	return (tcpdrop(local, foreign));
}

static void
usage(void)
{
	fprintf(stderr,
"usage: tcpdrop local-address local-port foreign-address foreign-port\n"
"       tcpdrop local-address:local-port foreign-address:foreign-port\n"
"       tcpdrop local-address.local-port foreign-address.foreign-port\n"
"       tcpdrop [-l] -a\n"
"       tcpdrop [-l] -S stack\n"
"       tcpdrop [-l] -s state\n"
"       tcpdrop [-l] -S stack -s state\n");
	exit(1);
}
