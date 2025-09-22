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
#include <sys/stat.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <netgroup.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

static int __ivaliduser_sa(FILE *, struct sockaddr *, socklen_t,
	    const char *, const char *);
static int __icheckhost(struct sockaddr *, socklen_t, const char *);
static char *__gethostloop(struct sockaddr *, socklen_t);
static int iruserok_sa(const void *, int, int, const char *, const char *);

int
ruserok(const char *rhost, int superuser, const char *ruser, const char *luser)
{
	struct addrinfo hints, *res, *r;
	int error;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;	/*dummy*/
	error = getaddrinfo(rhost, "0", &hints, &res);
	if (error)
		return (-1);

	for (r = res; r; r = r->ai_next) {
		if (iruserok_sa(r->ai_addr, r->ai_addrlen, superuser, ruser,
		    luser) == 0) {
			freeaddrinfo(res);
			return (0);
		}
	}
	freeaddrinfo(res);
	return (-1);
}

int
iruserok_sa(const void *raddr, int rlen, int superuser, const char *ruser,
    const char *luser)
{
	struct sockaddr *sa;
	char *cp;
	struct stat sbuf;
	struct passwd pwstore, *pwd;
	FILE *hostf;
	uid_t uid;
	int first;
	char pbuf[PATH_MAX], pwbuf[_PW_BUF_LEN];

	sa = (struct sockaddr *)raddr;
	first = 1;
	hostf = superuser ? NULL : fopen(_PATH_HEQUIV, "re");
again:
	if (hostf) {
		if (__ivaliduser_sa(hostf, sa, rlen, luser, ruser) == 0) {
			(void)fclose(hostf);
			return (0);
		}
		(void)fclose(hostf);
	}
	if (first == 1) {
		int len;

		first = 0;
		pwd = NULL;
		getpwnam_r(luser, &pwstore, pwbuf, sizeof(pwbuf), &pwd);
		if (pwd == NULL)
			return (-1);
		len = snprintf(pbuf, sizeof pbuf, "%s/.rhosts", pwd->pw_dir);
		if (len < 0 || len >= sizeof pbuf)
			return (-1);

		/*
		 * Change effective uid while opening .rhosts.  If root and
		 * reading an NFS mounted file system, can't read files that
		 * are protected read/write owner only.
		 */
		uid = geteuid();
		(void)seteuid(pwd->pw_uid);
		hostf = fopen(pbuf, "re");
		(void)seteuid(uid);

		if (hostf == NULL)
			return (-1);
		/*
		 * If not a regular file, or is owned by someone other than
		 * user or root or if writeable by anyone but the owner, quit.
		 */
		cp = NULL;
		if (lstat(pbuf, &sbuf) == -1)
			cp = ".rhosts lstat failed";
		else if (!S_ISREG(sbuf.st_mode))
			cp = ".rhosts not regular file";
		else if (fstat(fileno(hostf), &sbuf) == -1)
			cp = ".rhosts fstat failed";
		else if (sbuf.st_uid && sbuf.st_uid != pwd->pw_uid)
			cp = "bad .rhosts owner";
		else if (sbuf.st_mode & (S_IWGRP|S_IWOTH))
			cp = ".rhosts writable by other than owner";
		/* If there were any problems, quit. */
		if (cp) {
			(void)fclose(hostf);
			return (-1);
		}
		goto again;
	}
	return (-1);
}

int
__ivaliduser_sa(FILE *hostf, struct sockaddr *raddr, socklen_t salen,
    const char *luser, const char *ruser)
{
	char *user, *p;
	char *buf;
	const char *auser, *ahost;
	int hostok, userok;
	char *rhost = (char *)-1;
	char domain[HOST_NAME_MAX+1];
	size_t buflen;

	getdomainname(domain, sizeof(domain));

	while ((buf = fgetln(hostf, &buflen))) {
		p = buf;
		if (*p == '#')
			continue;
		while (p < buf + buflen && *p != '\n' && *p != ' ' && *p != '\t') {
			if (!isprint((unsigned char)*p))
				goto bail;
			*p = isupper((unsigned char)*p) ?
			    tolower((unsigned char)*p) : *p;
			p++;
		}
		if (p >= buf + buflen)
			continue;
		if (*p == ' ' || *p == '\t') {
			*p++ = '\0';
			while (p < buf + buflen && (*p == ' ' || *p == '\t'))
				p++;
			if (p >= buf + buflen)
				continue;
			user = p;
			while (p < buf + buflen && *p != '\n' && *p != ' ' &&
			    *p != '\t') {
				if (!isprint((unsigned char)*p))
					goto bail;
				p++;
			}
		} else
			user = p;
		*p = '\0';

		if (p == buf)
			continue;

		auser = *user ? user : luser;
		ahost = buf;

		if (strlen(ahost) > HOST_NAME_MAX)
			continue;

		/*
		 * innetgr() must lookup a hostname (we do not attempt
		 * to change the semantics so that netgroups may have
		 * #.#.#.# addresses in the list.)
		 */
		if (ahost[0] == '+')
			switch (ahost[1]) {
			case '\0':
				hostok = 1;
				break;
			case '@':
				if (rhost == (char *)-1)
					rhost = __gethostloop(raddr, salen);
				hostok = 0;
				if (rhost)
					hostok = innetgr(&ahost[2], rhost,
					    NULL, domain);
				break;
			default:
				hostok = __icheckhost(raddr, salen, &ahost[1]);
				break;
			}
		else if (ahost[0] == '-')
			switch (ahost[1]) {
			case '\0':
				hostok = -1;
				break;
			case '@':
				if (rhost == (char *)-1)
					rhost = __gethostloop(raddr, salen);
				hostok = 0;
				if (rhost)
					hostok = -innetgr(&ahost[2], rhost,
					    NULL, domain);
				break;
			default:
				hostok = -__icheckhost(raddr, salen, &ahost[1]);
				break;
			}
		else
			hostok = __icheckhost(raddr, salen, ahost);


		if (auser[0] == '+')
			switch (auser[1]) {
			case '\0':
				userok = 1;
				break;
			case '@':
				userok = innetgr(&auser[2], NULL, ruser,
				    domain);
				break;
			default:
				userok = strcmp(ruser, &auser[1]) ? 0 : 1;
				break;
			}
		else if (auser[0] == '-')
			switch (auser[1]) {
			case '\0':
				userok = -1;
				break;
			case '@':
				userok = -innetgr(&auser[2], NULL, ruser,
				    domain);
				break;
			default:
				userok = strcmp(ruser, &auser[1]) ? 0 : -1;
				break;
			}
		else
			userok = strcmp(ruser, auser) ? 0 : 1;

		/* Check if one component did not match */
		if (hostok == 0 || userok == 0)
			continue;

		/* Check if we got a forbidden pair */
		if (userok <= -1 || hostok <= -1)
			return (-1);

		/* Check if we got a valid pair */
		if (hostok >= 1 && userok >= 1)
			return (0);
	}
bail:
	return (-1);
}

/*
 * Returns "true" if match, 0 if no match.  If we do not find any
 * semblance of an A->PTR->A loop, allow a simple #.#.#.# match to work.
 */
static int
__icheckhost(struct sockaddr *raddr, socklen_t salen, const char *lhost)
{
	struct addrinfo hints, *res, *r;
	char h1[NI_MAXHOST], h2[NI_MAXHOST];
	int error;
	const int niflags = NI_NUMERICHOST;

	h1[0] = '\0';
	if (getnameinfo(raddr, salen, h1, sizeof(h1), NULL, 0,
	    niflags) != 0)
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
		if (strcmp(h1, h2) == 0) {
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
__gethostloop(struct sockaddr *raddr, socklen_t salen)
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
	if (getnameinfo(raddr, salen, h1, sizeof(h1), NULL, 0,
	    niflags) != 0)
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
		if (strcmp(h1, h2) == 0) {
			freeaddrinfo(res);
			return (remotehost);
		}
	}

	/*
	 * either the DNS administrator has made a configuration
	 * mistake, or someone has attempted to spoof us
	 */
	freeaddrinfo(res);
	return (NULL);
}
