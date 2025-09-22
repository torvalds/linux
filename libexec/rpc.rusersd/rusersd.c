/*	$OpenBSD: rusersd.c,v 1.24 2023/03/08 04:43:06 guenther Exp $	*/

/*-
 *  Copyright (c) 1993 John Brezak
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR `AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <pwd.h>
#include <syslog.h>
#include <rpc/rpc.h>
#include <rpcsvc/rusers.h>	/* New version */
#include <rpcsvc/rnusers.h>	/* Old version */
#include <rpc/pmap_clnt.h>
#include <utmp.h>

extern void rusers_service(struct svc_req *, SVCXPRT *);

int from_inetd = 1;
int utmp_fd;

static void
cleanup(int signo)
{
	(void) pmap_unset(RUSERSPROG, RUSERSVERS_3);	/* XXX signal races */
	(void) pmap_unset(RUSERSPROG, RUSERSVERS_IDLE);
	(void) pmap_unset(RUSERSPROG, RUSERSVERS_ORIG);
	_exit(0);
}

int
main(int argc, char *argv[])
{
	int sock = 0, proto = 0;
	socklen_t fromlen;
	struct sockaddr_storage from;
	struct passwd *pw;
	SVCXPRT *transp;

	if ((utmp_fd = open(_PATH_UTMP, O_RDONLY)) == -1) {
		syslog(LOG_ERR, "cannot open %s", _PATH_UTMP);
		exit(1);
	}

	openlog("rpc.rusersd", LOG_NDELAY|LOG_CONS|LOG_PID, LOG_DAEMON);

	pw = getpwnam("_rusersd");
	if (!pw) {
		syslog(LOG_ERR, "no such user _rusersd");
		exit(1);
	}

	if (unveil("/dev", "r") == -1) {
		syslog(LOG_ERR, "unveil /dev");
		exit(1);
	}
	if (unveil(NULL, NULL) == -1) {
		syslog(LOG_ERR, "unveil");
		exit(1);
	}

	setgroups(1, &pw->pw_gid);
	setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid);
	setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid);

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

		(void) pmap_unset(RUSERSPROG, RUSERSVERS_3);
		(void) pmap_unset(RUSERSPROG, RUSERSVERS_IDLE);
		(void) pmap_unset(RUSERSPROG, RUSERSVERS_ORIG);

		(void) signal(SIGINT, cleanup);
		(void) signal(SIGTERM, cleanup);
		(void) signal(SIGHUP, cleanup);
	}

	transp = svcudp_create(sock);
	if (transp == NULL) {
		syslog(LOG_ERR, "cannot create udp service.");
		exit(1);
	}
	if (!svc_register(transp, RUSERSPROG, RUSERSVERS_3, rusers_service, proto)) {
		syslog(LOG_ERR,
		    "unable to register (RUSERSPROG, RUSERSVERS_3, %s).",
		    proto ? "udp" : "(inetd)");
		exit(1);
	}
	if (!svc_register(transp, RUSERSPROG, RUSERSVERS_IDLE, rusers_service, proto)) {
		syslog(LOG_ERR,
		    "unable to register (RUSERSPROG, RUSERSVERS_IDLE, %s).",
		    proto ? "udp" : "(inetd)");
		exit(1);
	}
	if (!svc_register(transp, RUSERSPROG, RUSERSVERS_ORIG, rusers_service, proto)) {
		syslog(LOG_ERR,
		    "unable to register (RUSERSPROG, RUSERSVERS_ORIG, %s).",
		    proto ? "udp" : "(inetd)");
		exit(1);
	}

	svc_run();
	syslog(LOG_ERR, "svc_run returned");
	exit(1);
}
