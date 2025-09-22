/*
 * Copyright (c) 1995, 1996, 1998 Theo de Raadt.  All rights reserved.
 * Copyright (c) 1983, 1993, 1994
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
#include <sys/types.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <ctype.h>
#include <limits.h>
#include <netdb.h>
#include <netgroup.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

static int checkhost(struct sockaddr *, socklen_t, const char *);
static char *gethostloop(struct sockaddr *, socklen_t);

/*
 * Check whether the specified addr is listed in hostf.
 * Returns 0 if allowed, else -1.
 */
int
allowedhost(FILE *hostf, struct sockaddr *raddr, socklen_t salen)
{
	char *cp, *ep, *line = NULL;
	char *ahost, *rhost = (char *)-1;
	char domain[HOST_NAME_MAX+1];
	size_t linesize = 0;
	ssize_t linelen;
	int hostok = 0;

	getdomainname(domain, sizeof(domain));

	while ((linelen = getline(&line, &linesize, hostf)) != -1) {
		cp = line;
		ep = line + linelen;
		if (*cp == '#')
			continue;
		while (cp < ep && !isspace((unsigned char)*cp)) {
			if (!isprint((unsigned char)*cp))
				goto bail;
			*cp = isupper((unsigned char)*cp) ?
			    tolower((unsigned char)*cp) : *cp;
			cp++;
		}
		if (cp == line)
			continue;
		*cp = '\0';

		ahost = line;
		if (strlen(ahost) > HOST_NAME_MAX)
			continue;

		/*
		 * innetgr() must lookup a hostname (we do not attempt
		 * to change the semantics so that netgroups may have
		 * #.#.#.# addresses in the list.)
		 */
		switch (ahost[0]) {
		case '+':
		case '-':
			switch (ahost[1]) {
			case '\0':
				hostok = 1;
				break;
			case '@':
				if (rhost == (char *)-1)
					rhost = gethostloop(raddr, salen);
				hostok = 0;
				if (rhost)
					hostok = innetgr(&ahost[2], rhost,
					    NULL, domain);
				break;
			default:
				hostok = checkhost(raddr, salen, &ahost[1]);
				break;
			}
			if (ahost[0] == '-')
				hostok = -hostok;
			break;
		default:
			hostok = checkhost(raddr, salen, ahost);
			break;
		}

		/* Check if we got a match (positive or negative). */
		if (hostok != 0)
			break;
	}
bail:
	free(line);
	return (hostok > 0 ? 0 : -1);
}

/*
 * Returns 1 if match, 0 if no match.  If we do not find any
 * semblance of an A->PTR->A loop, allow a simple #.#.#.# match to work.
 */
static int
checkhost(struct sockaddr *raddr, socklen_t salen, const char *lhost)
{
	struct addrinfo hints, *res, *r;
	char h1[NI_MAXHOST], h2[NI_MAXHOST];
	int error;
	const int niflags = NI_NUMERICHOST;

	h1[0] = '\0';
	if (getnameinfo(raddr, salen, h1, sizeof(h1), NULL, 0, niflags) != 0)
		return (0);

	/* Resolve laddr into sockaddr */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = raddr->sa_family;
	hints.ai_socktype = SOCK_DGRAM;	/*dummy*/
	res = NULL;
	error = getaddrinfo(lhost, "0", &hints, &res);
	if (error)
		return (0);

	/*
	 * Try string comparisons between raddr and laddr.
	 */
	for (r = res; r; r = r->ai_next) {
		h2[0] = '\0';
		if (getnameinfo(r->ai_addr, r->ai_addrlen, h2, sizeof(h2),
		    NULL, 0, niflags) != 0)
			continue;
		if (strcasecmp(h1, h2) == 0) {
			freeaddrinfo(res);
			return (1);
		}
	}

	/* No match. */
	freeaddrinfo(res);
	return (0);
}

/*
 * Return the hostname associated with the supplied address.
 * Do a reverse lookup as well for security. If a loop cannot
 * be found, pack the result of inet_ntoa() into the string.
 */
static char *
gethostloop(struct sockaddr *raddr, socklen_t salen)
{
	static char remotehost[NI_MAXHOST];
	char h1[NI_MAXHOST], h2[NI_MAXHOST];
	struct addrinfo hints, *res, *r;
	int error;
	const int niflags = NI_NUMERICHOST;

	h1[0] = remotehost[0] = '\0';
	if (getnameinfo(raddr, salen, remotehost, sizeof(remotehost),
	    NULL, 0, NI_NAMEREQD) != 0)
		return (NULL);
	if (getnameinfo(raddr, salen, h1, sizeof(h1), NULL, 0, niflags) != 0)
		return (NULL);

	/*
	 * Look up the name and check that the supplied
	 * address is in the list
	 */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = raddr->sa_family;
	hints.ai_socktype = SOCK_DGRAM;	/*dummy*/
	hints.ai_flags = AI_CANONNAME;
	res = NULL;
	error = getaddrinfo(remotehost, "0", &hints, &res);
	if (error)
		return (NULL);

	for (r = res; r; r = r->ai_next) {
		h2[0] = '\0';
		if (getnameinfo(r->ai_addr, r->ai_addrlen, h2, sizeof(h2),
		    NULL, 0, niflags) != 0)
			continue;
		if (strcasecmp(h1, h2) == 0) {
			freeaddrinfo(res);
			return (remotehost);
		}
	}

	/*
	 * Either the DNS administrator has made a configuration
	 * mistake, or someone has attempted to spoof us.
	 */
	syslog(LOG_NOTICE, "lpd: address %s not listed for host %s",
	    h1, res->ai_canonname ? res->ai_canonname : remotehost);
	freeaddrinfo(res);
	return (NULL);
}

#ifdef DEBUG
int
main(int argc, char *argv[])
{
	struct addrinfo hints;
	int i;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	for (i = 1; i < argc; i++) {
		struct addrinfo *res0;
		int error = getaddrinfo(argv[i], NULL, &hints, &res0);
		if (error) {
			printf("%s: %s\n", argv[i], gai_strerror(error));
			continue;
		}
		error = allowedhost(stdin, res0->ai_addr, res0->ai_addrlen);
		printf("%s: %s\n", argv[i], error ? "denied" : "allowed");
	}
	exit(0);
}
#endif /* DEBUG */
