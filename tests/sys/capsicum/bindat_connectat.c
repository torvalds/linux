/*
 * Copyright (c) 2017 Jan Kokemüller
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

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/stat.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <atf-c.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "freebsd_test_suite/macros.h"

static int rootfd = -1;

/* circumvent bug 215690 */
int
open(const char *path, int flags, ...)
{
	mode_t mode = 0;

	if (flags & O_CREAT) {
		va_list ap;
		va_start(ap, flags);
		mode = (mode_t) va_arg(ap, int);
		va_end(ap);
	}

	if (path && path[0] == '/' && rootfd >= 0) {
		return (openat(rootfd, path + 1, flags, mode));
	} else {
		return (openat(AT_FDCWD, path, flags, mode));
	}
}

static void
check_capsicum(void)
{
	ATF_REQUIRE_FEATURE("security_capabilities");
	ATF_REQUIRE_FEATURE("security_capability_mode");

	ATF_REQUIRE((rootfd = open("/", O_EXEC | O_CLOEXEC)) >= 0);
}

typedef int (*socket_fun)(int, const struct sockaddr *, socklen_t);

static int
connectat_fdcwd(int s, const struct sockaddr *name, socklen_t namelen)
{

	return (connectat(AT_FDCWD, s, name, namelen));
}

static int
bindat_fdcwd(int s, const struct sockaddr *name, socklen_t namelen)
{

	return (bindat(AT_FDCWD, s, name, namelen));
}


ATF_TC(bindat_connectat_1);
ATF_TC_HEAD(bindat_connectat_1, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Verify that connect/bind work in normal case");
}

static void
check_1(socket_fun f, int s, const struct sockaddr_in *name)
{

	ATF_REQUIRE((s = socket(AF_INET, SOCK_STREAM, 0)) >= 0);
	ATF_REQUIRE_ERRNO(EAFNOSUPPORT,
	    f(s, (const struct sockaddr *)(name),
	        sizeof(struct sockaddr_in)) < 0);
}

ATF_TC_BODY(bindat_connectat_1, tc)
{
	struct sockaddr_in sin;

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(0);
	sin.sin_addr.s_addr = htonl(0xE0000000);

	check_1(bindat_fdcwd, 0, &sin);
	check_1(bind, 0, &sin);
	check_1(connectat_fdcwd, 0, &sin);
	check_1(connect, 0, &sin);
}


ATF_TC(bindat_connectat_2);
ATF_TC_HEAD(bindat_connectat_2, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Verify that connect/bind are disabled in cap-mode");
}

static void
check_2(socket_fun f, int s, const struct sockaddr_in *name)
{

	ATF_REQUIRE_ERRNO(ECAPMODE,
	    f(s, (const struct sockaddr *)name,
	        sizeof(struct sockaddr_in)) < 0);
}

ATF_TC_BODY(bindat_connectat_2, tc)
{
	int sock;
	struct sockaddr_in sin;

	check_capsicum();

	ATF_REQUIRE(cap_enter() >= 0);

	/* note: sock is created _after_ cap_enter() and contains all rights */
	ATF_REQUIRE((sock = socket(AF_INET, SOCK_STREAM, 0)) >= 0);

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	/* dummy port and multicast address (224.0.0.0) to distinguish two
	 * cases:
	 *  - ECAPMODE/ENOTCAPABLE --> call blocked by capsicum
	 *  - EAFNOSUPPORT --> call went through to protocol layer
	 */
	sin.sin_port = htons(0);
	sin.sin_addr.s_addr = htonl(0xE0000000);

	check_2(bindat_fdcwd, sock, &sin);
	check_2(bind, sock, &sin);
	check_2(connectat_fdcwd, sock, &sin);
	check_2(connect, sock, &sin);
}


ATF_TC(bindat_connectat_3);
ATF_TC_HEAD(bindat_connectat_3, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Check that taking away CAP_BIND/CAP_CONNECT "
	    "sabotages bind/connect");
}

static void
check_3(socket_fun f, int s, const struct sockaddr_in *name,
    cap_rights_t *rights, cap_rights_t *sub_rights)
{

	ATF_REQUIRE((s = socket(AF_INET, SOCK_STREAM, 0)) >= 0);
	ATF_REQUIRE(cap_rights_limit(s, rights) >= 0);
	ATF_REQUIRE_ERRNO(EAFNOSUPPORT,
	    f(s, (const struct sockaddr *)name,
	        sizeof(struct sockaddr_in)) < 0);
	ATF_REQUIRE(cap_rights_limit(s,
	                cap_rights_remove(rights, sub_rights)) >= 0);
	ATF_REQUIRE_ERRNO(ENOTCAPABLE,
	    f(s, (const struct sockaddr *)name,
	        sizeof(struct sockaddr_in)) < 0);
}

ATF_TC_BODY(bindat_connectat_3, tc)
{
	struct sockaddr_in sin;
	cap_rights_t rights, sub_rights;

	check_capsicum();

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(0);
	sin.sin_addr.s_addr = htonl(0xE0000000);

	check_3(bindat_fdcwd, 0, &sin,
	    cap_rights_init(&rights, CAP_SOCK_SERVER),
	    cap_rights_init(&sub_rights, CAP_BIND));
	check_3(bind, 0, &sin,
	    cap_rights_init(&rights, CAP_SOCK_SERVER),
	    cap_rights_init(&sub_rights, CAP_BIND));
	check_3(connectat_fdcwd, 0, &sin,
	    cap_rights_init(&rights, CAP_SOCK_CLIENT),
	    cap_rights_init(&sub_rights, CAP_CONNECT));
	check_3(connect, 0, &sin,
	    cap_rights_init(&rights, CAP_SOCK_CLIENT),
	    cap_rights_init(&sub_rights, CAP_CONNECT));
}


ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, bindat_connectat_1);
	ATF_TP_ADD_TC(tp, bindat_connectat_2);
	ATF_TP_ADD_TC(tp, bindat_connectat_3);

	return (atf_no_error());
}
