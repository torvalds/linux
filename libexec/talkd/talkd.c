/*	$OpenBSD: talkd.c,v 1.26 2019/06/28 13:32:53 deraadt Exp $	*/

/*
 * Copyright (c) 1983 Regents of the University of California.
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

/*
 * The top level of the daemon, the format is heavily borrowed
 * from rwhod.c. Basically: find out who and where you are;
 * disconnect all descriptors and ttys, and then endless
 * loop on waiting for and processing requests
 */
#include <sys/socket.h>
#include <protocols/talkd.h>

#include <errno.h>
#include <limits.h>
#include <paths.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "talkd.h"

int	debug = 0;
void	timeout(int);
long	lastmsgtime;

char	hostname[HOST_NAME_MAX+1];

#define TIMEOUT 30
#define MAXIDLE 120

int
main(int argc, char *argv[])
{
	if (getuid() != 0) {
		fprintf(stderr, "%s: getuid: not super-user\n", argv[0]);
		exit(1);
	}
	openlog("talkd", LOG_PID, LOG_DAEMON);
	if (gethostname(hostname, sizeof(hostname)) == -1) {
		syslog(LOG_ERR, "gethostname: %m");
		_exit(1);
	}
	if (chdir(_PATH_DEV) == -1) {
		syslog(LOG_ERR, "chdir: %s: %m", _PATH_DEV);
		_exit(1);
	}
	if (argc > 1 && strcmp(argv[1], "-d") == 0)
		debug = 1;
	init_table();
	signal(SIGALRM, timeout);
	alarm(TIMEOUT);

	if (pledge("stdio rpath wpath cpath inet dns", NULL) == -1) {
		syslog(LOG_ERR, "pledge: %m");
		_exit(1);
	}

	for (;;) {
		CTL_RESPONSE response;
		socklen_t len = sizeof(response.addr);
		CTL_MSG	request;
		int cc;
		struct sockaddr ctl_addr;

		memset(&response, 0, sizeof(response));
		cc = recvfrom(STDIN_FILENO, (char *)&request,
		    sizeof(request), 0, (struct sockaddr *)&response.addr,
		    &len);
		if (cc != sizeof(request)) {
			if (cc == -1 && errno != EINTR)
				syslog(LOG_WARNING, "recvfrom: %m");
			continue;
		}

		/* Force NUL termination */
		request.l_name[sizeof(request.l_name) - 1] = '\0';
		request.r_name[sizeof(request.r_name) - 1] = '\0';
		request.r_tty[sizeof(request.r_tty) - 1] = '\0';

		memcpy(&ctl_addr, &request.ctl_addr, sizeof(ctl_addr));
		ctl_addr.sa_family = ntohs(request.ctl_addr.sa_family);
		ctl_addr.sa_len = sizeof(ctl_addr);
		if (ctl_addr.sa_family != AF_INET)
			continue;

		lastmsgtime = time(0);
		process_request(&request, &response);
		/* can block here, is this what I want? */
		cc = sendto(STDOUT_FILENO, (char *)&response,
		    sizeof(response), 0, &ctl_addr, sizeof(ctl_addr));
		if (cc != sizeof(response))
			syslog(LOG_WARNING, "sendto: %m");
	}
}

void
timeout(int signo)
{
	int save_errno = errno;

	if (time(0) - lastmsgtime >= MAXIDLE)
		_exit(0);
	alarm(TIMEOUT);
	errno = save_errno;
}
