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

#include <signal.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include <limits.h>
#include <pwd.h>
#include <errno.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <stdlib.h>
#include <poll.h>

int
rcmd(char **ahost, int rport, const char *locuser, const char *remuser,
    const char *cmd, int *fd2p)
{
	return rcmd_af(ahost, rport, locuser, remuser, cmd, fd2p, AF_INET);
}

int
rcmd_af(char **ahost, int porta, const char *locuser, const char *remuser,
    const char *cmd, int *fd2p, int af)
{
	static char hbuf[HOST_NAME_MAX+1];
	char pbuf[NI_MAXSERV];
	struct addrinfo hints, *res, *r;
	int error;
	struct sockaddr_storage from;
	sigset_t oldmask, mask;
	pid_t pid;
	int s, lport;
	struct timespec timo;
	char c, *p;
	int refused;
	in_port_t rport = porta;
	int numread;

	/* call rcmdsh() with specified remote shell if appropriate. */
	if (!issetugid() && (p = getenv("RSH")) && *p) {
		struct servent *sp = getservbyname("shell", "tcp");

		if (sp && sp->s_port == rport)
			return (rcmdsh(ahost, rport, locuser, remuser,
			    cmd, p));
	}

	/* use rsh(1) if non-root and remote port is shell. */
	if (geteuid()) {
		struct servent *sp = getservbyname("shell", "tcp");

		if (sp && sp->s_port == rport)
			return (rcmdsh(ahost, rport, locuser, remuser,
			    cmd, NULL));
	}

	pid = getpid();
	snprintf(pbuf, sizeof(pbuf), "%u", ntohs(rport));
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = af;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_CANONNAME;
	error = getaddrinfo(*ahost, pbuf, &hints, &res);
	if (error) {
		(void)fprintf(stderr, "rcmd: %s: %s\n", *ahost,
		    gai_strerror(error));
		return (-1);
	}
	if (res->ai_canonname) {
		strlcpy(hbuf, res->ai_canonname, sizeof(hbuf));
		*ahost = hbuf;
	} else
		; /*XXX*/

	r = res;
	refused = 0;
	timespecclear(&timo);
	sigemptyset(&mask);
	sigaddset(&mask, SIGURG);
	sigprocmask(SIG_BLOCK, &mask, &oldmask);
	for (timo.tv_sec = 1, lport = IPPORT_RESERVED - 1;;) {
		s = rresvport_af(&lport, r->ai_family);
		if (s < 0) {
			if (errno == EAGAIN)
				(void)fprintf(stderr,
				    "rcmd: socket: All ports in use\n");
			else
				(void)fprintf(stderr, "rcmd: socket: %s\n",
				    strerror(errno));
			if (r->ai_next) {
				r = r->ai_next;
				continue;
			} else {
				sigprocmask(SIG_SETMASK, &oldmask, NULL);
				freeaddrinfo(res);
				return (-1);
			}
		}
		fcntl(s, F_SETOWN, pid);
		if (connect(s, r->ai_addr, r->ai_addrlen) >= 0)
			break;
		(void)close(s);
		if (errno == EADDRINUSE) {
			lport--;
			continue;
		}
		if (errno == ECONNREFUSED)
			refused++;
		if (r->ai_next) {
			int oerrno = errno;
			char hbuf[NI_MAXHOST];
			const int niflags = NI_NUMERICHOST;

			hbuf[0] = '\0';
			if (getnameinfo(r->ai_addr, r->ai_addrlen,
			    hbuf, sizeof(hbuf), NULL, 0, niflags) != 0)
				strlcpy(hbuf, "(invalid)", sizeof hbuf);
			(void)fprintf(stderr, "connect to address %s: ", hbuf);
			errno = oerrno;
			perror(0);
			r = r->ai_next;
			hbuf[0] = '\0';
			if (getnameinfo(r->ai_addr, r->ai_addrlen,
			    hbuf, sizeof(hbuf), NULL, 0, niflags) != 0)
				strlcpy(hbuf, "(invalid)", sizeof hbuf);
			(void)fprintf(stderr, "Trying %s...\n", hbuf);
			continue;
		}
		if (refused && timo.tv_sec <= 16) {
			(void)nanosleep(&timo, NULL);
			timo.tv_sec *= 2;
			r = res;
			refused = 0;
			continue;
		}
		(void)fprintf(stderr, "%s: %s\n", res->ai_canonname,
		    strerror(errno));
		sigprocmask(SIG_SETMASK, &oldmask, NULL);
		freeaddrinfo(res);
		return (-1);
	}
	/* given "af" can be PF_UNSPEC, we need the real af for "s" */
	af = r->ai_family;
	freeaddrinfo(res);
	if (fd2p == 0) {
		write(s, "", 1);
		lport = 0;
	} else {
		struct pollfd pfd[2];
		char num[8];
		int s2 = rresvport_af(&lport, af), s3;
		socklen_t len = sizeof(from);

		if (s2 < 0)
			goto bad;

		listen(s2, 1);
		(void)snprintf(num, sizeof(num), "%d", lport);
		if (write(s, num, strlen(num)+1) != strlen(num)+1) {
			(void)fprintf(stderr,
			    "rcmd: write (setting up stderr): %s\n",
			    strerror(errno));
			(void)close(s2);
			goto bad;
		}
again:
		pfd[0].fd = s;
		pfd[0].events = POLLIN;
		pfd[1].fd = s2;
		pfd[1].events = POLLIN;

		errno = 0;
		if (poll(pfd, 2, INFTIM) < 1 ||
		    (pfd[1].revents & (POLLIN|POLLHUP)) == 0) {
			if (errno != 0)
				(void)fprintf(stderr,
				    "rcmd: poll (setting up stderr): %s\n",
				    strerror(errno));
			else
				(void)fprintf(stderr,
				"poll: protocol failure in circuit setup\n");
			(void)close(s2);
			goto bad;
		}
		s3 = accept(s2, (struct sockaddr *)&from, &len);
		if (s3 < 0) {
			(void)fprintf(stderr,
			    "rcmd: accept: %s\n", strerror(errno));
			lport = 0;
			close(s2);
			goto bad;
		}

		/*
		 * XXX careful for ftp bounce attacks. If discovered, shut them
		 * down and check for the real auxiliary channel to connect.
		 */
		switch (from.ss_family) {
		case AF_INET:
		case AF_INET6:
			if (getnameinfo((struct sockaddr *)&from, len,
			    NULL, 0, num, sizeof(num), NI_NUMERICSERV) == 0 &&
			    atoi(num) != 20) {
				break;
			}
			close(s3);
			goto again;
		default:
			break;
		}
		(void)close(s2);

		*fd2p = s3;
		switch (from.ss_family) {
		case AF_INET:
		case AF_INET6:
			if (getnameinfo((struct sockaddr *)&from, len,
			    NULL, 0, num, sizeof(num), NI_NUMERICSERV) != 0 ||
			    (atoi(num) >= IPPORT_RESERVED ||
			     atoi(num) < IPPORT_RESERVED / 2)) {
				(void)fprintf(stderr,
				    "socket: protocol failure in circuit setup.\n");
				goto bad2;
			}
			break;
		default:
			break;
		}
	}
	(void)write(s, locuser, strlen(locuser)+1);
	(void)write(s, remuser, strlen(remuser)+1);
	(void)write(s, cmd, strlen(cmd)+1);
	if ((numread = read(s, &c, 1)) != 1) {
		(void)fprintf(stderr,
		    "rcmd: %s: %s\n", *ahost,
		    numread == -1 ? strerror(errno) : "Short read");
		goto bad2;
	}
	if (c != 0) {
		while (read(s, &c, 1) == 1) {
			(void)write(STDERR_FILENO, &c, 1);
			if (c == '\n')
				break;
		}
		goto bad2;
	}
	sigprocmask(SIG_SETMASK, &oldmask, NULL);
	return (s);
bad2:
	if (lport)
		(void)close(*fd2p);
bad:
	(void)close(s);
	sigprocmask(SIG_SETMASK, &oldmask, NULL);
	return (-1);
}
DEF_WEAK(rcmd_af);

