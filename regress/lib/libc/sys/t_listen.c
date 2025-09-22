/*	$OpenBSD: t_listen.c,v 1.1.1.1 2019/11/19 19:57:03 bluhm Exp $	*/
/*	$NetBSD: t_listen.c,v 1.6 2019/07/09 16:24:01 maya Exp $	*/
/*
 * Copyright (c) 2007 The NetBSD Foundation, Inc.
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "macros.h"

#include <sys/socket.h>
#include "atf-c.h"
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>

static const char *path = "listen";

ATF_TC_WITH_CLEANUP(listen_err);
ATF_TC_HEAD(listen_err, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Checks errors from listen(2) (PR standards/46150)");
}

ATF_TC_BODY(listen_err, tc)
{
	static const size_t siz = sizeof(struct sockaddr_in);
	struct sockaddr_in sina, sinb;
	int fda, fdb, fdc;

	(void)memset(&sina, 0, sizeof(struct sockaddr_in));
	(void)memset(&sinb, 0, sizeof(struct sockaddr_in));

	sina.sin_family = AF_INET;
	sina.sin_port = htons(31522);
	sina.sin_addr.s_addr = inet_addr("127.0.0.1");

	sinb.sin_family = AF_INET;
	sinb.sin_port = htons(31522);
	sinb.sin_addr.s_addr = inet_addr("127.0.0.1");

	fda = socket(AF_INET, SOCK_STREAM, 0);
	fdb = socket(AF_INET, SOCK_STREAM, 0);
	fdc = open("listen", O_RDWR | O_CREAT, 0600);

	ATF_REQUIRE(fda >= 0 && fdb >= 0 && fdc >= 0);
	ATF_REQUIRE_ERRNO(ENOTSOCK, listen(fdc, 1) == -1);

	(void)close(fdc);
	(void)unlink(path);

	ATF_REQUIRE(bind(fda, (struct sockaddr *)&sina, siz) == 0);
	ATF_REQUIRE(listen(fda, 1) == 0);

	/*
	 * According to IEEE Std 1003.1-2008: if the socket is
	 * already connected, the call should fail with EINVAL.
	 */
	ATF_REQUIRE(connect(fdb, (struct sockaddr *)&sinb, siz) == 0);
	ATF_REQUIRE_ERRNO(EINVAL, listen(fdb, 1) == -1);

	(void)close(fda);
	(void)close(fdb);

	ATF_REQUIRE_ERRNO(EBADF, connect(fdb,
		(struct sockaddr *)&sinb, siz) == -1);
}

ATF_TC_CLEANUP(listen_err, tc)
{
	(void)unlink(path);
}

ATF_TC(listen_low_port);
ATF_TC_HEAD(listen_low_port, tc)
{
	atf_tc_set_md_var(tc, "descr", "Does low-port allocation work?");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(listen_low_port, tc)
{
	int sd, val;

	sd = socket(AF_INET, SOCK_STREAM, 0);
	ATF_REQUIRE_MSG(sd != -1, "socket failed: %s", strerror(errno));

	val = IP_PORTRANGE_LOW;
	if (setsockopt(sd, IPPROTO_IP, IP_PORTRANGE, &val,
	    sizeof(val)) == -1)
		atf_tc_fail("setsockopt failed: %s", strerror(errno));

	if (listen(sd, 5) == -1) {
		int serrno = errno;
		atf_tc_fail("listen failed: %s%s",
		    strerror(serrno),
		    serrno != EACCES ? "" :
		    " (see http://mail-index.netbsd.org/"
		    "source-changes/2007/12/16/0011.html)");
	}

	close(sd);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, listen_err);
	ATF_TP_ADD_TC(tp, listen_low_port);

	return atf_no_error();
}
