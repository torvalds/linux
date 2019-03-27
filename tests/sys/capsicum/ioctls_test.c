/*-
 * Copyright (c) 2018 John Baldwin <jhb@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/capsicum.h>
#include <sys/filio.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <atf-c.h>

/*
 * A variant of ATF_REQUIRE that is suitable for use in child
 * processes.  This only works if the parent process is tripped up by
 * the early exit and fails some requirement itself.
 */
#define	CHILD_REQUIRE(exp) do {						\
		if (!(exp))						\
			child_fail_require(__FILE__, __LINE__,		\
			    #exp " not met");				\
	} while (0)

static __dead2 void
child_fail_require(const char *file, int line, const char *str)
{
	char buf[128];

	snprintf(buf, sizeof(buf), "%s:%d: %s\n", file, line, str);
	write(2, buf, strlen(buf));
	_exit(32);
}

/*
 * Exercise the edge case of a custom ioctl list being copied from a
 * listen socket to an accepted socket.
 */
ATF_TC_WITHOUT_HEAD(cap_ioctls__listen_copy);
ATF_TC_BODY(cap_ioctls__listen_copy, tc)
{
	struct sockaddr_in sin;
	cap_rights_t rights;
	u_long cmds[] = { FIONREAD };
	socklen_t len;
	pid_t pid;
	char dummy;
	int s[2], status;

	s[0] = socket(AF_INET, SOCK_STREAM, 0);
	ATF_REQUIRE(s[0] > 0);

	/* Bind to an arbitrary unused port. */
	memset(&sin, 0, sizeof(sin));
	sin.sin_len = sizeof(sin);
	sin.sin_family = AF_INET;
	sin.sin_port = 0;
	sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	ATF_REQUIRE(bind(s[0], (struct sockaddr *)&sin, sizeof(sin)) == 0);

	CHILD_REQUIRE(listen(s[0], 1) == 0);

	len = sizeof(sin);
	ATF_REQUIRE(getsockname(s[0], (struct sockaddr *)&sin, &len) == 0);
	ATF_REQUIRE(len == sizeof(sin));

	cap_rights_init(&rights, CAP_ACCEPT, CAP_IOCTL);
	ATF_REQUIRE(cap_rights_limit(s[0], &rights) == 0);
	ATF_REQUIRE(cap_ioctls_limit(s[0], cmds, nitems(cmds)) == 0);

	pid = fork();
	if (pid == 0) {
		s[1] = accept(s[0], NULL, NULL);
		CHILD_REQUIRE(s[1] > 0);

		/* Close both sockets during exit(). */
		exit(0);
	}

	ATF_REQUIRE(pid > 0);

	ATF_REQUIRE(close(s[0]) == 0);
	s[1] = socket(AF_INET, SOCK_STREAM, 0);
	ATF_REQUIRE(s[1] > 0);
	ATF_REQUIRE(connect(s[1], (struct sockaddr *)&sin, sizeof(sin)) == 0);
	ATF_REQUIRE(read(s[1], &dummy, sizeof(dummy)) == 0);
	ATF_REQUIRE(close(s[1]) == 0);

	ATF_REQUIRE(wait(&status) == pid);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE(WEXITSTATUS(status) == 0);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, cap_ioctls__listen_copy);

	return (atf_no_error());
}
