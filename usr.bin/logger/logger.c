/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1983, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#if 0
#ifndef lint
static char sccsid[] = "@(#)logger.c	8.1 (Berkeley) 6/6/93";
#endif /* not lint */
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <ctype.h>
#include <err.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define	SYSLOG_NAMES
#include <syslog.h>

#define	sstosa(ss)	((struct sockaddr *)(void *)ss)

struct socks {
	int sk_sock;
	int sk_addrlen;
	struct sockaddr_storage sk_addr;
};

static int	decode(char *, const CODE *);
static int	pencode(char *);
static ssize_t	socksetup(const char *, const char *, const char *,
		    struct socks **);
static void	logmessage(int, const char *, const char *, const char *,
		    struct socks *, ssize_t, const char *);
static void	usage(void);

#ifdef INET6
static int family = PF_UNSPEC;	/* protocol family (IPv4, IPv6 or both) */
#else
static int family = PF_INET;	/* protocol family (IPv4 only) */
#endif
static int send_to_all = 0;	/* send message to all IPv4/IPv6 addresses */

/*
 * logger -- read and log utility
 *
 *	Reads from an input and arranges to write the result on the system
 *	log.
 */
int
main(int argc, char *argv[])
{
	struct socks *socks;
	ssize_t nsock;
	time_t now;
	int ch, logflags, pri;
	char *tag, *host, buf[1024], *timestamp, tbuf[26],
	    *hostname, hbuf[MAXHOSTNAMELEN];
	const char *svcname, *src;

	tag = NULL;
	host = NULL;
	hostname = NULL;
	svcname = "syslog";
	src = NULL;
	socks = NULL;
	pri = LOG_USER | LOG_NOTICE;
	logflags = 0;
	unsetenv("TZ");
	while ((ch = getopt(argc, argv, "46Af:H:h:iP:p:S:st:")) != -1)
		switch((char)ch) {
		case '4':
			family = PF_INET;
			break;
#ifdef INET6
		case '6':
			family = PF_INET6;
			break;
#endif
		case 'A':
			send_to_all++;
			break;
		case 'f':		/* file to log */
			if (freopen(optarg, "r", stdin) == NULL)
				err(1, "%s", optarg);
			setvbuf(stdin, 0, _IONBF, 0);
			break;
		case 'H':		/* hostname to set in message header */
			hostname = optarg;
			break;
		case 'h':		/* hostname to deliver to */
			host = optarg;
			break;
		case 'i':		/* log process id also */
			logflags |= LOG_PID;
			break;
		case 'P':		/* service name or port number */
			svcname = optarg;
			break;
		case 'p':		/* priority */
			pri = pencode(optarg);
			break;
		case 's':		/* log to standard error */
			logflags |= LOG_PERROR;
			break;
		case 'S':		/* source address */
			src = optarg;
			break;
		case 't':		/* tag */
			tag = optarg;
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (host) {
		nsock = socksetup(src, host, svcname, &socks);
		if (nsock <= 0)
			errx(1, "socket");
	} else {
		if (src)
			errx(1, "-h option is missing.");
		nsock = 0;
	}

	if (tag == NULL)
		tag = getlogin();
	/* setup for logging */
	if (host == NULL)
		openlog(tag, logflags, 0);
	(void) fclose(stdout);

	(void )time(&now);
	(void )ctime_r(&now, tbuf);
	tbuf[19] = '\0';
	timestamp = tbuf + 4;

	if (hostname == NULL) {
		hostname = hbuf;
		(void )gethostname(hbuf, MAXHOSTNAMELEN);
		*strchrnul(hostname, '.') = '\0';
	}

	/* log input line if appropriate */
	if (argc > 0) {
		char *p, *endp;
		size_t len;

		for (p = buf, endp = buf + sizeof(buf) - 2; *argv;) {
			len = strlen(*argv);
			if (p + len > endp && p > buf) {
				logmessage(pri, timestamp, hostname, tag,
				    socks, nsock, buf);
				p = buf;
			}
			if (len > sizeof(buf) - 1)
				logmessage(pri, timestamp, hostname, tag,
				    socks, nsock, *argv++);
			else {
				if (p != buf)
					*p++ = ' ';
				bcopy(*argv++, p, len);
				*(p += len) = '\0';
			}
		}
		if (p != buf)
			logmessage(pri, timestamp, hostname, tag, socks, nsock,
			    buf);
	} else
		while (fgets(buf, sizeof(buf), stdin) != NULL)
			logmessage(pri, timestamp, hostname, tag, socks, nsock,
			    buf);
	exit(0);
}

static ssize_t
socksetup(const char *src, const char *dst, const char *svcname,
	struct socks **socks)
{
	struct addrinfo hints, *res, *res0;
	struct sockaddr_storage *ss_src[AF_MAX];
	struct socks *sk;
	ssize_t nsock = 0;
	int error, maxs;

	memset(&ss_src[0], 0, sizeof(ss_src));
	if (src) {
		char *p, *p0, *hs, *hbuf, *sbuf;

		hbuf = sbuf = NULL;
		p0 = p = strdup(src);
		if (p0 == NULL)
			err(1, "strdup failed");
		hs = p0;	/* point to search ":" */ 
#ifdef INET6
		/* -S option supports IPv6 addr in "[2001:db8::1]:service". */
		if (*p0 == '[') {
			p = strchr(p0, ']');
			if (p == NULL)
				errx(1, "\"]\" not found in src addr");
			*p = '\0';
			/* hs points just after ']' (':' or '\0'). */
			hs = p + 1;
			/*
			 * p points just after '[' while it points hs
			 * in the case of [].
			 */
			p = ((p0 + 1) == (hs - 1)) ? hs : p0 + 1;
		}
#endif
		if (*p != '\0') {
			/* (p == hs) means ":514" or "[]:514". */
			hbuf = (p == hs && *p == ':') ? NULL : p;
			p = strchr(hs, ':');
			if (p != NULL) {
				*p = '\0';
				sbuf = (*(p + 1) != '\0') ? p + 1 : NULL;
			}
		}
		hints = (struct addrinfo){
			.ai_family = family,
			.ai_socktype = SOCK_DGRAM,
			.ai_flags = AI_PASSIVE
		};
		error = getaddrinfo(hbuf, sbuf, &hints, &res0);
		if (error)
			errx(1, "%s: %s", gai_strerror(error), src);
		for (res = res0; res; res = res->ai_next) {
			switch (res->ai_family) {
			case AF_INET:
#ifdef INET6
			case AF_INET6:
#endif
				if (ss_src[res->ai_family] != NULL)
					continue;
				ss_src[res->ai_family] =
				    malloc(sizeof(struct sockaddr_storage));
				if (ss_src[res->ai_family] == NULL)
					err(1, "malloc failed");
				memcpy(ss_src[res->ai_family], res->ai_addr,
				    res->ai_addrlen);
			}
		}
		freeaddrinfo(res0);
		free(p0);
	}

	/* resolve hostname */
	hints = (struct addrinfo){
		.ai_family = family,
		.ai_socktype = SOCK_DGRAM
	};
	error = getaddrinfo(dst, svcname, &hints, &res0);
	if (error == EAI_SERVICE) {
		warnx("%s/udp: unknown service", svcname);
		error = getaddrinfo(dst, "514", &hints, &res0);
	}	
	if (error)
		errx(1, "%s: %s", gai_strerror(error), dst);
	/* count max number of sockets we may open */
	maxs = 0;
	for (res = res0; res; res = res->ai_next)
		maxs++;
	sk = calloc(maxs, sizeof(*sk));
	if (sk == NULL)
		errx(1, "couldn't allocate memory for sockets");
	for (res = res0; res; res = res->ai_next) {
		int s;

		s = socket(res->ai_family, res->ai_socktype,
		    res->ai_protocol);
		if (s < 0)
			continue;
		if (src && ss_src[res->ai_family] == NULL)
			errx(1, "address family mismatch");
			
		if (ss_src[res->ai_family]) {
			error = bind(s, sstosa(ss_src[res->ai_family]),
				    ss_src[res->ai_family]->ss_len);
			if (error < 0)
				err(1, "bind");
		}
		sk[nsock] = (struct socks){
			.sk_addrlen = res->ai_addrlen,
			.sk_sock = s
		};
		memcpy(&sk[nsock].sk_addr, res->ai_addr, res->ai_addrlen);
		nsock++;
	}
	freeaddrinfo(res0);

	*socks = sk;
	return (nsock);
}

/*
 *  Send the message to syslog, either on the local host, or on a remote host
 */
static void
logmessage(int pri, const char *timestamp, const char *hostname,
    const char *tag, struct socks *sk, ssize_t nsock, const char *buf)
{
	char *line;
	int len, i, lsent;

	if (nsock == 0) {
		syslog(pri, "%s", buf);
		return;
	}
	if ((len = asprintf(&line, "<%d>%s %s %s: %s", pri, timestamp,
	    hostname, tag, buf)) == -1)
		errx(1, "asprintf");

	lsent = -1;
	for (i = 0; i < nsock; i++) {
		lsent = sendto(sk[i].sk_sock, line, len, 0,
			       sstosa(&sk[i].sk_addr), sk[i].sk_addrlen);
		if (lsent == len && !send_to_all)
			break;
	}
	if (lsent != len) {
		if (lsent == -1)
			warn("sendto");
		else
			warnx("sendto: short send - %d bytes", lsent);
	}

	free(line);
}

/*
 *  Decode a symbolic name to a numeric value
 */
static int
pencode(char *s)
{
	char *save;
	int fac, lev;

	for (save = s; *s && *s != '.'; ++s);
	if (*s) {
		*s = '\0';
		fac = decode(save, facilitynames);
		if (fac < 0)
			errx(1, "unknown facility name: %s", save);
		*s++ = '.';
	}
	else {
		fac = 0;
		s = save;
	}
	lev = decode(s, prioritynames);
	if (lev < 0)
		errx(1, "unknown priority name: %s", save);
	return ((lev & LOG_PRIMASK) | (fac & LOG_FACMASK));
}

static int
decode(char *name, const CODE *codetab)
{
	const CODE *c;

	if (isdigit(*name))
		return (atoi(name));

	for (c = codetab; c->c_name; c++)
		if (!strcasecmp(name, c->c_name))
			return (c->c_val);

	return (-1);
}

static void
usage(void)
{
	(void)fprintf(stderr, "usage: %s\n",
	    "logger [-46Ais] [-f file] [-h host] [-P port] [-p pri] [-t tag]\n"
	    "              [-S addr:port] [message ...]"
	    );
	exit(1);
}
