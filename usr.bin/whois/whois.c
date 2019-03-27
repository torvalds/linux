/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1993
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

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1980, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#if 0
#ifndef lint
static char sccsid[] = "@(#)whois.c	8.1 (Berkeley) 6/6/93";
#endif /* not lint */
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <err.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define	ABUSEHOST	"whois.abuse.net"
#define	ANICHOST	"whois.arin.net"
#define	DENICHOST	"whois.denic.de"
#define	DKNICHOST	"whois.dk-hostmaster.dk"
#define	FNICHOST	"whois.afrinic.net"
#define	GNICHOST	"whois.nic.gov"
#define	IANAHOST	"whois.iana.org"
#define	INICHOST	"whois.internic.net"
#define	KNICHOST	"whois.krnic.net"
#define	LNICHOST	"whois.lacnic.net"
#define	MNICHOST	"whois.ra.net"
#define	PDBHOST		"whois.peeringdb.com"
#define	PNICHOST	"whois.apnic.net"
#define	QNICHOST_TAIL	".whois-servers.net"
#define	RNICHOST	"whois.ripe.net"
#define	VNICHOST	"whois.verisign-grs.com"

#define	DEFAULT_PORT	"whois"

#define WHOIS_RECURSE	0x01
#define WHOIS_QUICK	0x02
#define WHOIS_SPAM_ME	0x04

#define CHOPSPAM	">>> Last update of WHOIS database:"

#define ishost(h) (isalnum((unsigned char)h) || h == '.' || h == '-')

#define SCAN(p, end, check)					\
	while ((p) < (end))					\
		if (check) ++(p);				\
		else break

static struct {
	const char *suffix, *server;
} whoiswhere[] = {
	/* Various handles */
	{ "-ARIN", ANICHOST },
	{ "-NICAT", "at" QNICHOST_TAIL },
	{ "-NORID", "no" QNICHOST_TAIL },
	{ "-RIPE", RNICHOST },
	/* Nominet's whois server doesn't return referrals to JANET */
	{ ".ac.uk", "ac.uk" QNICHOST_TAIL },
	{ ".gov.uk", "ac.uk" QNICHOST_TAIL },
	{ "", IANAHOST }, /* default */
	{ NULL, NULL } /* safety belt */
};

#define WHOIS_REFERRAL(s) { s, sizeof(s) - 1 }
static struct {
	const char *prefix;
	size_t len;
} whois_referral[] = {
	WHOIS_REFERRAL("whois:"), /* IANA */
	WHOIS_REFERRAL("Whois Server:"),
	WHOIS_REFERRAL("Registrar WHOIS Server:"), /* corporatedomains.com */
	WHOIS_REFERRAL("ReferralServer:  whois://"), /* ARIN */
	WHOIS_REFERRAL("descr:          region. Please query"), /* AfriNIC */
	{ NULL, 0 }
};

/*
 * We have a list of patterns for RIRs that assert ignorance rather than
 * providing referrals. If that happens, we guess that ARIN will be more
 * helpful. But, before following a referral to an RIR, we check if we have
 * asked that RIR already, and if so we make another guess.
 */
static const char *actually_arin[] = {
	"netname:        ERX-NETBLOCK\n", /* APNIC */
	"netname:        NON-RIPE-NCC-MANAGED-ADDRESS-BLOCK\n",
	NULL
};

static struct {
	int loop;
	const char *host;
} try_rir[] = {
	{ 0, ANICHOST },
	{ 0, RNICHOST },
	{ 0, PNICHOST },
	{ 0, FNICHOST },
	{ 0, LNICHOST },
	{ 0, NULL }
};

static void
reset_rir(void) {
	int i;

	for (i = 0; try_rir[i].host != NULL; i++)
		try_rir[i].loop = 0;
}

static const char *port = DEFAULT_PORT;

static const char *choose_server(char *);
static struct addrinfo *gethostinfo(char const *host, int exitnoname);
static void s_asprintf(char **ret, const char *format, ...) __printflike(2, 3);
static void usage(void);
static void whois(const char *, const char *, int);

int
main(int argc, char *argv[])
{
	const char *country, *host;
	int ch, flags;

#ifdef	SOCKS
	SOCKSinit(argv[0]);
#endif

	country = host = NULL;
	flags = 0;
	while ((ch = getopt(argc, argv, "aAbc:fgh:iIklmp:PQrRS")) != -1) {
		switch (ch) {
		case 'a':
			host = ANICHOST;
			break;
		case 'A':
			host = PNICHOST;
			break;
		case 'b':
			host = ABUSEHOST;
			break;
		case 'c':
			country = optarg;
			break;
		case 'f':
			host = FNICHOST;
			break;
		case 'g':
			host = GNICHOST;
			break;
		case 'h':
			host = optarg;
			break;
		case 'i':
			host = INICHOST;
			break;
		case 'I':
			host = IANAHOST;
			break;
		case 'k':
			host = KNICHOST;
			break;
		case 'l':
			host = LNICHOST;
			break;
		case 'm':
			host = MNICHOST;
			break;
		case 'p':
			port = optarg;
			break;
		case 'P':
			host = PDBHOST;
			break;
		case 'Q':
			flags |= WHOIS_QUICK;
			break;
		case 'r':
			host = RNICHOST;
			break;
		case 'R':
			flags |= WHOIS_RECURSE;
			break;
		case 'S':
			flags |= WHOIS_SPAM_ME;
			break;
		case '?':
		default:
			usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	if (!argc || (country != NULL && host != NULL))
		usage();

	/*
	 * If no host or country is specified, rely on referrals from IANA.
	 */
	if (host == NULL && country == NULL) {
		if ((host = getenv("WHOIS_SERVER")) == NULL &&
		    (host = getenv("RA_SERVER")) == NULL) {
			if (!(flags & WHOIS_QUICK))
				flags |= WHOIS_RECURSE;
		}
	}
	while (argc-- > 0) {
		if (country != NULL) {
			char *qnichost;
			s_asprintf(&qnichost, "%s%s", country, QNICHOST_TAIL);
			whois(*argv, qnichost, flags);
			free(qnichost);
		} else
			whois(*argv, host != NULL ? host :
			      choose_server(*argv), flags);
		reset_rir();
		argv++;
	}
	exit(0);
}

static const char *
choose_server(char *domain)
{
	size_t len = strlen(domain);
	int i;

	for (i = 0; whoiswhere[i].suffix != NULL; i++) {
		size_t suffix_len = strlen(whoiswhere[i].suffix);
		if (len > suffix_len &&
		    strcasecmp(domain + len - suffix_len,
			       whoiswhere[i].suffix) == 0)
			return (whoiswhere[i].server);
	}
	errx(EX_SOFTWARE, "no default whois server");
}

static struct addrinfo *
gethostinfo(char const *host, int exit_on_noname)
{
	struct addrinfo hints, *res;
	int error;

	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_CANONNAME;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	res = NULL;
	error = getaddrinfo(host, port, &hints, &res);
	if (error && (exit_on_noname || error != EAI_NONAME))
		err(EX_NOHOST, "%s: %s", host, gai_strerror(error));
	return (res);
}

/*
 * Wrapper for asprintf(3) that exits on error.
 */
static void
s_asprintf(char **ret, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	if (vasprintf(ret, format, ap) == -1) {
		va_end(ap);
		err(EX_OSERR, "vasprintf()");
	}
	va_end(ap);
}

static int
connect_to_any_host(struct addrinfo *hostres)
{
	struct addrinfo *res;
	nfds_t i, j;
	size_t count;
	struct pollfd *fds;
	int timeout = 180, s = -1;

	for (res = hostres, count = 0; res; res = res->ai_next)
		count++;
	fds = calloc(count, sizeof(*fds));
	if (fds == NULL)
		err(EX_OSERR, "calloc()");

	/*
	 * Traverse the result list elements and make non-block
	 * connection attempts.
	 */
	count = i = 0;
	for (res = hostres; res != NULL; res = res->ai_next) {
		s = socket(res->ai_family, res->ai_socktype | SOCK_NONBLOCK,
		    res->ai_protocol);
		if (s < 0)
			continue;
		if (connect(s, res->ai_addr, res->ai_addrlen) < 0) {
			if (errno == EINPROGRESS) {
				/* Add the socket to poll list */
				fds[i].fd = s;
				fds[i].events = POLLERR | POLLHUP |
						POLLIN | POLLOUT;
				/*
				 * From here until a socket connects, the
				 * socket fd is owned by the fds[] poll array.
				 */
				s = -1;
				count++;
				i++;
			} else {
				close(s);
				s = -1;

				/*
				 * Poll only if we have something to poll,
				 * otherwise just go ahead and try next
				 * address
				 */
				if (count == 0)
					continue;
			}
		} else
			goto done;

		/*
		 * If we are at the last address, poll until a connection is
		 * established or we failed all connection attempts.
		 */
		if (res->ai_next == NULL)
			timeout = INFTIM;

		/*
		 * Poll the watched descriptors for successful connections:
		 * if we still have more untried resolved addresses, poll only
		 * once; otherwise, poll until all descriptors have errors,
		 * which will be considered as ETIMEDOUT later.
		 */
		do {
			int n;

			n = poll(fds, i, timeout);
			if (n == 0) {
				/*
				 * No event reported in time.  Try with a
				 * smaller timeout (but cap at 2-3ms)
				 * after a new host have been added.
				 */
				if (timeout >= 3)
					timeout >>= 1;

				break;
			} else if (n < 0) {
				/*
				 * errno here can only be EINTR which we would
				 * want to clean up and bail out.
				 */
				s = -1;
				goto done;
			}

			/*
			 * Check for the event(s) we have seen.
			 */
			for (j = 0; j < i; j++) {
				if (fds[j].fd == -1 || fds[j].events == 0 ||
				    fds[j].revents == 0)
					continue;
				if (fds[j].revents & ~(POLLIN | POLLOUT)) {
					close(fds[j].fd);
					fds[j].fd = -1;
					fds[j].events = 0;
					count--;
					continue;
				} else if (fds[j].revents & (POLLIN | POLLOUT)) {
					/* Connect succeeded. */
					s = fds[j].fd;
					fds[j].fd = -1;

					goto done;
				}

			}
		} while (timeout == INFTIM && count != 0);
	}

	/* All attempts were failed */
	s = -1;
	if (count == 0)
		errno = ETIMEDOUT;

done:
	/* Close all watched fds except the succeeded one */
	for (j = 0; j < i; j++)
		if (fds[j].fd != -1)
			close(fds[j].fd);
	free(fds);
	return (s);
}

static void
whois(const char *query, const char *hostname, int flags)
{
	FILE *fp;
	struct addrinfo *hostres;
	char *buf, *host, *nhost, *p;
	int comment, s, f;
	size_t len, i;

	hostres = gethostinfo(hostname, 1);
	s = connect_to_any_host(hostres);
	if (s == -1)
		err(EX_OSERR, "connect()");

	/* Restore default blocking behavior.  */
	if ((f = fcntl(s, F_GETFL)) == -1)
		err(EX_OSERR, "fcntl()");
	f &= ~O_NONBLOCK;
	if (fcntl(s, F_SETFL, f) == -1)
		err(EX_OSERR, "fcntl()");

	fp = fdopen(s, "r+");
	if (fp == NULL)
		err(EX_OSERR, "fdopen()");

	if (!(flags & WHOIS_SPAM_ME) &&
	    (strcasecmp(hostname, DENICHOST) == 0 ||
	     strcasecmp(hostname, "de" QNICHOST_TAIL) == 0)) {
		const char *q;
		int idn = 0;
		for (q = query; *q != '\0'; q++)
			if (!isascii(*q))
				idn = 1;
		fprintf(fp, "-T dn%s %s\r\n", idn ? "" : ",ace", query);
	} else if (!(flags & WHOIS_SPAM_ME) &&
		   (strcasecmp(hostname, DKNICHOST) == 0 ||
		    strcasecmp(hostname, "dk" QNICHOST_TAIL) == 0))
		fprintf(fp, "--show-handles %s\r\n", query);
	else if ((flags & WHOIS_SPAM_ME) ||
		 strchr(query, ' ') != NULL)
		fprintf(fp, "%s\r\n", query);
	else if (strcasecmp(hostname, ANICHOST) == 0) {
		if (strncasecmp(query, "AS", 2) == 0 &&
		    strspn(query+2, "0123456789") == strlen(query+2))
			fprintf(fp, "+ a %s\r\n", query+2);
		else
			fprintf(fp, "+ %s\r\n", query);
	} else if (strcasecmp(hostres->ai_canonname, VNICHOST) == 0)
		fprintf(fp, "domain %s\r\n", query);
	else
		fprintf(fp, "%s\r\n", query);
	fflush(fp);

	comment = 0;
	if (!(flags & WHOIS_SPAM_ME) &&
	    (strcasecmp(hostname, ANICHOST) == 0 ||
	     strcasecmp(hostname, RNICHOST) == 0)) {
		comment = 2;
	}

	nhost = NULL;
	while ((buf = fgetln(fp, &len)) != NULL) {
		/* Nominet */
		if (!(flags & WHOIS_SPAM_ME) &&
		    len == 5 && strncmp(buf, "-- \r\n", 5) == 0)
			break;
		/* RIRs */
		if (comment == 1 && buf[0] == '#')
			break;
		else if (comment == 2) {
			if (strchr("#%\r\n", buf[0]) != NULL)
				continue;
			else
				comment = 1;
		}

		printf("%.*s", (int)len, buf);

		if ((flags & WHOIS_RECURSE) && nhost == NULL) {
			for (i = 0; whois_referral[i].prefix != NULL; i++) {
				p = buf;
				SCAN(p, buf+len, *p == ' ');
				if (strncasecmp(p, whois_referral[i].prefix,
					           whois_referral[i].len) != 0)
					continue;
				p += whois_referral[i].len;
				SCAN(p, buf+len, *p == ' ');
				host = p;
				SCAN(p, buf+len, ishost(*p));
				if (p > host)
					s_asprintf(&nhost, "%.*s",
						   (int)(p - host), host);
				break;
			}
			for (i = 0; actually_arin[i] != NULL; i++) {
				if (strncmp(buf, actually_arin[i], len) == 0) {
					s_asprintf(&nhost, "%s", ANICHOST);
					break;
				}
			}
		}
		/* Verisign etc. */
		if (!(flags & WHOIS_SPAM_ME) &&
		    len >= sizeof(CHOPSPAM)-1 &&
		    (strncasecmp(buf, CHOPSPAM, sizeof(CHOPSPAM)-1) == 0 ||
		     strncasecmp(buf, CHOPSPAM+4, sizeof(CHOPSPAM)-5) == 0)) {
			printf("\n");
			break;
		}
	}
	fclose(fp);
	freeaddrinfo(hostres);

	f = 0;
	for (i = 0; try_rir[i].host != NULL; i++) {
		/* Remember visits to RIRs */
		if (try_rir[i].loop == 0 &&
		    strcasecmp(try_rir[i].host, hostname) == 0)
			try_rir[i].loop = 1;
		/* Do we need to find an alternative RIR? */
		if (try_rir[i].loop != 0 && nhost != NULL &&
		    strcasecmp(try_rir[i].host, nhost) == 0) {
			    free(nhost);
			    nhost = NULL;
			    f = 1;
		}
	}
	if (f) {
		/* Find a replacement RIR */
		for (i = 0; try_rir[i].host != NULL; i++) {
			if (try_rir[i].loop == 0) {
				s_asprintf(&nhost, "%s",
					try_rir[i].host);
				break;
			}
		}
	}
	if (nhost != NULL) {
		/* Ignore self-referrals */
		if (strcasecmp(hostname, nhost) != 0) {
			printf("# %s\n\n", nhost);
			whois(query, nhost, flags);
		}
		free(nhost);
	}
}

static void
usage(void)
{
	fprintf(stderr,
	    "usage: whois [-aAbfgiIklmPQrRS] [-c country-code | -h hostname] "
	    "[-p port] name ...\n");
	exit(EX_USAGE);
}
