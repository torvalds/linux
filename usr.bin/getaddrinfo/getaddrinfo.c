/*	$NetBSD: getaddrinfo.c,v 1.4 2014/04/22 02:23:03 ginsbach Exp $	*/

/*-
 * Copyright (c) 2013 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Taylor R. Campbell.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/socket.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <util.h>

#include "tables.h"

static void	usage(void) __dead;
static void	printaddrinfo(struct addrinfo *);
static bool	parse_af(const char *, int *);
static bool	parse_protocol(const char *, int *);
static bool	parse_socktype(const char *, int *);
static bool	parse_numeric_tabular(const char *, int *, const char *const *,
		    size_t);

int
main(int argc, char **argv)
{
	static const struct addrinfo zero_addrinfo;
	struct addrinfo hints = zero_addrinfo;
	struct addrinfo *addrinfo;
	const char *hostname = NULL, *service = NULL;
	int ch;
	int error;

	setprogname(argv[0]);

	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = 0;
	hints.ai_protocol = 0;
	hints.ai_flags = 0;

	while ((ch = getopt(argc, argv, "cf:nNp:Ps:t:")) != -1) {
		switch (ch) {
		case 'c':
			hints.ai_flags |= AI_CANONNAME;
			break;

		case 'f':
			if (!parse_af(optarg, &hints.ai_family)) {
				warnx("invalid address family: %s", optarg);
				usage();
			}
			break;

		case 'n':
			hints.ai_flags |= AI_NUMERICHOST;
			break;

		case 'N':
			hints.ai_flags |= AI_NUMERICSERV;
			break;

		case 's':
			service = optarg;
			break;

		case 'p':
			if (!parse_protocol(optarg, &hints.ai_protocol)) {
				warnx("invalid protocol: %s", optarg);
				usage();
			}
			break;

		case 'P':
			hints.ai_flags |= AI_PASSIVE;
			break;

		case 't':
			if (!parse_socktype(optarg, &hints.ai_socktype)) {
				warnx("invalid socket type: %s", optarg);
				usage();
			}
			break;

		case '?':
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (!((argc == 1) || ((argc == 0) && (hints.ai_flags & AI_PASSIVE))))
		usage();
	if (argc == 1)
		hostname = argv[0];

	if (service != NULL) {
		char *p;

		if ((p = strchr(service, '/')) != NULL) {
			if (hints.ai_protocol != 0) {
				warnx("protocol already specified");
				usage();
			}
			*p = '\0';
			p++;

			if (!parse_protocol(p, &hints.ai_protocol)) {
				warnx("invalid protocol: %s", p);
				usage();
			}
		}
	}

	error = getaddrinfo(hostname, service, &hints, &addrinfo);
	if (error)
		errx(1, "%s", gai_strerror(error));

	if ((hints.ai_flags & AI_CANONNAME) && (addrinfo != NULL)) {
		if (printf("canonname %s\n", addrinfo->ai_canonname) < 0)
			err(1, "printf");
	}

	printaddrinfo(addrinfo);

	freeaddrinfo(addrinfo);

	return 0;
}

static void __dead
usage(void)
{

	(void)fprintf(stderr, "Usage: %s", getprogname());
	(void)fprintf(stderr,
	    " [-f <family>] [-p <protocol>] [-t <socktype>] [-s <service>]\n");
	(void)fprintf(stderr, "   [-cnNP] [<hostname>]\n");
	exit(1);
}

static bool
parse_af(const char *string, int *afp)
{

	return parse_numeric_tabular(string, afp, address_families,
	    __arraycount(address_families));
}

static bool
parse_protocol(const char *string, int *protop)
{
	struct protoent *protoent;
	char *end;
	long value;

	errno = 0;
	value = strtol(string, &end, 0);
	if ((string[0] == '\0') || (*end != '\0'))
		goto numeric_failed;
	if ((errno == ERANGE) && ((value == LONG_MAX) || (value == LONG_MIN)))
		goto numeric_failed;
	if ((value > INT_MAX) || (value < INT_MIN))
		goto numeric_failed;

	*protop = value;
	return true;

numeric_failed:
	protoent = getprotobyname(string);
	if (protoent == NULL)
		goto protoent_failed;

	*protop = protoent->p_proto;
	return true;

protoent_failed:
	return false;
}

static bool
parse_socktype(const char *string, int *typep)
{

	return parse_numeric_tabular(string, typep, socket_types,
	    __arraycount(socket_types));
}

static bool
parse_numeric_tabular(const char *string, int *valuep,
    const char *const *table, size_t n)
{
	char *end;
	long value;
	size_t i;

	assert((uintmax_t)n <= (uintmax_t)INT_MAX);

	errno = 0;
	value = strtol(string, &end, 0);
	if ((string[0] == '\0') || (*end != '\0'))
		goto numeric_failed;
	if ((errno == ERANGE) && ((value == LONG_MAX) || (value == LONG_MIN)))
		goto numeric_failed;
	if ((value > INT_MAX) || (value < INT_MIN))
		goto numeric_failed;

	*valuep = value;
	return true;

numeric_failed:
	for (i = 0; i < n; i++)
		if ((table[i] != NULL) && (strcmp(string, table[i]) == 0))
			break;
	if (i == n)
		goto table_failed;
	*valuep = i;
	return true;

table_failed:
	return false;
}

static void
printaddrinfo(struct addrinfo *addrinfo)
{
	struct addrinfo *ai;
	char buf[1024];
	int n;
	struct protoent *protoent;

	for (ai = addrinfo; ai != NULL; ai = ai->ai_next) {
		/* Print the socket type.  */
		if ((ai->ai_socktype >= 0) &&
		    ((size_t)ai->ai_socktype < __arraycount(socket_types)) &&
		    (socket_types[ai->ai_socktype] != NULL))
			n = printf("%s", socket_types[ai->ai_socktype]);
		else
			n = printf("%d", ai->ai_socktype);
		if (n < 0)
			err(1, "printf");

		/* Print the address family.  */
		if ((ai->ai_family >= 0) &&
		    ((size_t)ai->ai_family < __arraycount(address_families)) &&
		    (address_families[ai->ai_family] != NULL))
			n = printf(" %s", address_families[ai->ai_family]);
		else
			n = printf(" %d", ai->ai_family);
		if (n < 0)
			err(1, "printf");

		/* Print the protocol number.  */
		protoent = getprotobynumber(ai->ai_protocol);
		if (protoent == NULL)
			n = printf(" %d", ai->ai_protocol);
		else
			n = printf(" %s", protoent->p_name);
		if (n < 0)
			err(1, "printf");

		/* Format the sockaddr.  */
		switch (ai->ai_family) {
		case AF_INET:
		case AF_INET6:
			n = sockaddr_snprintf(buf, sizeof(buf), " %a %p",
			    ai->ai_addr);
			break;

		default:
			n = sockaddr_snprintf(buf, sizeof(buf),
			    "%a %p %I %F %R %S", ai->ai_addr);
		}

		/*
		 * Check for sockaddr_snprintf failure.
		 *
		 * XXX sockaddr_snprintf's error reporting is botched
		 * -- man page says it sets errno, but if getnameinfo
		 * fails, errno is not where it reports the error...
		 */
		if (n < 0) {
			warnx("sockaddr_snprintf failed");
			continue;
		}
		if (sizeof(buf) <= (size_t)n)
			warnx("truncated sockaddr_snprintf output");

		/* Print the formatted sockaddr.  */
		if (printf("%s\n", buf) < 0)
			err(1, "printf");
	}
}
