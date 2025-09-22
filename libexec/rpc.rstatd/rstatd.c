/*	$OpenBSD: rstatd.c,v 1.31 2023/03/08 04:43:05 guenther Exp $	*/

/*-
 * Copyright (c) 1993, John Brezak
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <pwd.h>
#include <syslog.h>
#include <errno.h>
#include <rpc/rpc.h>
#include <rpcsvc/rstat.h>

extern void rstat_service(struct svc_req *, SVCXPRT *);

void my_svc_run(void);

int from_inetd = 1;	/* started from inetd ? */
int closedown = 20;	/* how long to wait before going dormant */

volatile sig_atomic_t gotsig;

static void
getsig(int signo)
{
	gotsig = 1;
}


int
main(int argc, char *argv[])
{
	int sock = 0, proto = 0;
	socklen_t fromlen;
	struct passwd *pw;
	struct sockaddr_storage from;
	SVCXPRT *transp;

	openlog("rpc.rstatd", LOG_NDELAY|LOG_CONS|LOG_PID, LOG_DAEMON);

	if ((pw = getpwnam("_rstatd")) == NULL) {
		syslog(LOG_ERR, "no such user _rstatd");
		exit(1);
	}
	if (chroot("/var/empty") == -1) {
		syslog(LOG_ERR, "cannot chdir to /var/empty.");
		exit(1);
	}
	chdir("/");

	setgroups(1, &pw->pw_gid);
	setegid(pw->pw_gid);
	setgid(pw->pw_gid);
	seteuid(pw->pw_uid);
	setuid(pw->pw_uid);

	if (argc == 2)
		closedown = strtonum(argv[1], 1, INT_MAX, NULL);
	if (closedown == 0)
		closedown = 20;

	/*
	 * See if inetd started us
	 */
	fromlen = sizeof(from);
	if (getsockname(0, (struct sockaddr *)&from, &fromlen) == -1) {
		from_inetd = 0;
		sock = RPC_ANYSOCK;
		proto = IPPROTO_UDP;
	}

	if (!from_inetd) {
		daemon(0, 0);

		(void)pmap_unset(RSTATPROG, RSTATVERS_TIME);
		(void)pmap_unset(RSTATPROG, RSTATVERS_SWTCH);
		(void)pmap_unset(RSTATPROG, RSTATVERS_ORIG);

		(void) signal(SIGINT, getsig);
		(void) signal(SIGTERM, getsig);
		(void) signal(SIGHUP, getsig);
	}

	transp = svcudp_create(sock);
	if (transp == NULL) {
		syslog(LOG_ERR, "cannot create udp service.");
		exit(1);
	}
	if (!svc_register(transp, RSTATPROG, RSTATVERS_TIME, rstat_service, proto)) {
		syslog(LOG_ERR, "unable to register (RSTATPROG, RSTATVERS_TIME, udp).");
		exit(1);
	}
	if (!svc_register(transp, RSTATPROG, RSTATVERS_SWTCH, rstat_service, proto)) {
		syslog(LOG_ERR, "unable to register (RSTATPROG, RSTATVERS_SWTCH, udp).");
		exit(1);
	}
	if (!svc_register(transp, RSTATPROG, RSTATVERS_ORIG, rstat_service, proto)) {
		syslog(LOG_ERR, "unable to register (RSTATPROG, RSTATVERS_ORIG, udp).");
		exit(1);
	}

	my_svc_run();
	syslog(LOG_ERR, "svc_run returned");
	exit(1);
}

void
my_svc_run(void)
{
	extern volatile sig_atomic_t wantupdatestat;
	extern void updatestat(void);
	struct pollfd *pfd = NULL, *newp;
	int nready, saved_max_pollfd = 0;

	for (;;) {
		if (wantupdatestat) {
			wantupdatestat = 0;
			updatestat();
		}
		if (gotsig) {
			(void) pmap_unset(RSTATPROG, RSTATVERS_TIME);
			(void) pmap_unset(RSTATPROG, RSTATVERS_SWTCH);
			(void) pmap_unset(RSTATPROG, RSTATVERS_ORIG);
			exit(0);
		}
		if (svc_max_pollfd > saved_max_pollfd) {
			newp = reallocarray(pfd, svc_max_pollfd, sizeof(*pfd));
			if (newp == NULL) {
				free(pfd);
				perror("svc_run: - realloc failed");
				return;
			}
			pfd = newp;
			saved_max_pollfd = svc_max_pollfd;
		}
		memcpy(pfd, svc_pollfd, svc_max_pollfd * sizeof(*pfd));

		nready = poll(pfd, svc_max_pollfd, INFTIM);
		switch (nready) {
		case -1:
			if (errno == EINTR)
				continue;
			perror("svc_run: - poll failed");
			free(pfd);
			return;
		case 0:
			continue;
		default:
			svc_getreq_poll(pfd, nready);
		}
	}
}
