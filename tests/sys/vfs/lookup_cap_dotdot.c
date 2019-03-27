/*-
 * Copyright (c) 2016 Ed Maste <emaste@FreeBSD.org>
 * Copyright (c) 2016 Conrad Meyer <cem@FreeBSD.org>
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
#include <sys/sysctl.h>
#include <sys/stat.h>

#include <atf-c.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "freebsd_test_suite/macros.h"

static int dirfd = -1;
static char *abspath;

static void
touchat(int _dirfd, const char *name)
{
	int fd;

	ATF_REQUIRE((fd = openat(_dirfd, name, O_CREAT | O_TRUNC | O_WRONLY,
	    0777)) >= 0);
	ATF_REQUIRE(close(fd) == 0);
}

static void
prepare_dotdot_tests(void)
{
	char cwd[MAXPATHLEN];

	ATF_REQUIRE(getcwd(cwd, sizeof(cwd)) != NULL);
	asprintf(&abspath, "%s/testdir/d1/f1", cwd);

	ATF_REQUIRE(mkdir("testdir", 0777) == 0);
	ATF_REQUIRE((dirfd = open("testdir", O_RDONLY)) >= 0);

	ATF_REQUIRE(mkdirat(dirfd, "d1", 0777) == 0);
	ATF_REQUIRE(mkdirat(dirfd, "d1/d2", 0777) == 0);
	ATF_REQUIRE(mkdirat(dirfd, "d1/d2/d3", 0777) == 0);
	touchat(dirfd, "d1/f1");
	touchat(dirfd, "d1/d2/f2");
	touchat(dirfd, "d1/d2/d3/f3");
	ATF_REQUIRE(symlinkat("d1/d2/d3", dirfd, "l3") == 0);
	ATF_REQUIRE(symlinkat("../testdir/d1", dirfd, "lup") == 0);
	ATF_REQUIRE(symlinkat("../..", dirfd, "d1/d2/d3/ld1") == 0);
	ATF_REQUIRE(symlinkat("../../f1", dirfd, "d1/d2/d3/lf1") == 0);
}

static void
check_capsicum(void)
{
	ATF_REQUIRE_FEATURE("security_capabilities");
	ATF_REQUIRE_FEATURE("security_capability_mode");
}

/*
 * Positive tests
 */
ATF_TC(openat__basic_positive);
ATF_TC_HEAD(openat__basic_positive, tc)
{
	atf_tc_set_md_var(tc, "descr", "Basic positive openat testcases");
}

ATF_TC_BODY(openat__basic_positive, tc)
{
	prepare_dotdot_tests();

	ATF_REQUIRE(openat(dirfd, "d1/d2/d3/f3", O_RDONLY) >= 0);
	ATF_REQUIRE(openat(dirfd, "d1/d2/d3/../../f1", O_RDONLY) >= 0);
	ATF_REQUIRE(openat(dirfd, "l3/f3", O_RDONLY) >= 0);
	ATF_REQUIRE(openat(dirfd, "l3/../../f1", O_RDONLY) >= 0);
	ATF_REQUIRE(openat(dirfd, "../testdir/d1/f1", O_RDONLY) >= 0);
	ATF_REQUIRE(openat(dirfd, "lup/f1", O_RDONLY) >= 0);
	ATF_REQUIRE(openat(dirfd, "l3/ld1", O_RDONLY) >= 0);
	ATF_REQUIRE(openat(dirfd, "l3/lf1", O_RDONLY) >= 0);
	ATF_REQUIRE(open(abspath, O_RDONLY) >= 0);
	ATF_REQUIRE(openat(dirfd, abspath, O_RDONLY) >= 0);
}

ATF_TC(lookup_cap_dotdot__basic);
ATF_TC_HEAD(lookup_cap_dotdot__basic, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Validate cap-mode (testdir)/d1/.. lookup");
}

ATF_TC_BODY(lookup_cap_dotdot__basic, tc)
{
	cap_rights_t rights;

	check_capsicum();
	prepare_dotdot_tests();

	cap_rights_init(&rights, CAP_LOOKUP, CAP_READ);
	ATF_REQUIRE(cap_rights_limit(dirfd, &rights) >= 0);

	atf_tc_expect_signal(SIGABRT, "needs change done upstream in atf/kyua according to cem: bug 215690");

	ATF_REQUIRE(cap_enter() >= 0);

	ATF_REQUIRE_MSG(openat(dirfd, "d1/..", O_RDONLY) >= 0, "%s",
	    strerror(errno));
}

ATF_TC(lookup_cap_dotdot__advanced);
ATF_TC_HEAD(lookup_cap_dotdot__advanced, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Validate cap-mode (testdir)/d1/.. lookup");
}

ATF_TC_BODY(lookup_cap_dotdot__advanced, tc)
{
	cap_rights_t rights;

	check_capsicum();
	prepare_dotdot_tests();

	atf_tc_expect_signal(SIGABRT, "needs change done upstream in atf/kyua according to cem: bug 215690");

	cap_rights_init(&rights, CAP_LOOKUP, CAP_READ);
	ATF_REQUIRE(cap_rights_limit(dirfd, &rights) >= 0);

	ATF_REQUIRE(cap_enter() >= 0);

	ATF_REQUIRE(openat(dirfd, "d1/d2/d3/../../f1", O_RDONLY) >= 0);
	ATF_REQUIRE(openat(dirfd, "l3/../../f1", O_RDONLY) >= 0);
	ATF_REQUIRE(openat(dirfd, "l3/ld1", O_RDONLY) >= 0);
	ATF_REQUIRE(openat(dirfd, "l3/lf1", O_RDONLY) >= 0);
}

/*
 * Negative tests
 */
ATF_TC(openat__basic_negative);
ATF_TC_HEAD(openat__basic_negative, tc)
{
	atf_tc_set_md_var(tc, "descr", "Basic negative openat testcases");
}

ATF_TC_BODY(openat__basic_negative, tc)
{
	prepare_dotdot_tests();

	ATF_REQUIRE_ERRNO(ENOENT,
	    openat(dirfd, "does-not-exist", O_RDONLY) < 0);
	ATF_REQUIRE_ERRNO(ENOENT,
	    openat(dirfd, "l3/does-not-exist", O_RDONLY) < 0);
}

ATF_TC(capmode__negative);
ATF_TC_HEAD(capmode__negative, tc)
{
	atf_tc_set_md_var(tc, "descr", "Negative Capability mode testcases");
}

ATF_TC_BODY(capmode__negative, tc)
{
	int subdirfd;

	check_capsicum();
	prepare_dotdot_tests();

	atf_tc_expect_signal(SIGABRT, "needs change done upstream in atf/kyua according to cem: bug 215690");

	ATF_REQUIRE(cap_enter() == 0);

	/* open() not permitted in capability mode */
	ATF_REQUIRE_ERRNO(ECAPMODE, open("testdir", O_RDONLY) < 0);

	/* AT_FDCWD not permitted in capability mode */
	ATF_REQUIRE_ERRNO(ECAPMODE, openat(AT_FDCWD, "d1/f1", O_RDONLY) < 0);

	/* Relative path above dirfd not capable */
	ATF_REQUIRE_ERRNO(ENOTCAPABLE, openat(dirfd, "..", O_RDONLY) < 0);
	ATF_REQUIRE((subdirfd = openat(dirfd, "l3", O_RDONLY)) >= 0);
	ATF_REQUIRE_ERRNO(ENOTCAPABLE,
	    openat(subdirfd, "../../f1", O_RDONLY) < 0);

	/* Absolute paths not capable */
	ATF_REQUIRE_ERRNO(ENOTCAPABLE, openat(dirfd, abspath, O_RDONLY) < 0);

	/* Symlink above dirfd */
	ATF_REQUIRE_ERRNO(ENOTCAPABLE, openat(dirfd, "lup/f1", O_RDONLY) < 0);
}

ATF_TC(lookup_cap_dotdot__negative);
ATF_TC_HEAD(lookup_cap_dotdot__negative, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Validate cap-mode (testdir)/.. lookup fails");
}

ATF_TC_BODY(lookup_cap_dotdot__negative, tc)
{
	cap_rights_t rights;

	check_capsicum();
	prepare_dotdot_tests();

	cap_rights_init(&rights, CAP_LOOKUP, CAP_READ);
	ATF_REQUIRE(cap_rights_limit(dirfd, &rights) >= 0);

	atf_tc_expect_signal(SIGABRT, "needs change done upstream in atf/kyua according to cem: bug 215690");

	ATF_REQUIRE(cap_enter() >= 0);

	ATF_REQUIRE_ERRNO(ENOTCAPABLE, openat(dirfd, "..", O_RDONLY) < 0);
	ATF_REQUIRE_ERRNO(ENOTCAPABLE, openat(dirfd, "d1/../..", O_RDONLY) < 0);
	ATF_REQUIRE_ERRNO(ENOTCAPABLE, openat(dirfd, "../testdir/d1/f1", O_RDONLY) < 0);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, openat__basic_positive);
	ATF_TP_ADD_TC(tp, openat__basic_negative);

	ATF_TP_ADD_TC(tp, capmode__negative);

	ATF_TP_ADD_TC(tp, lookup_cap_dotdot__basic);
	ATF_TP_ADD_TC(tp, lookup_cap_dotdot__advanced);
	ATF_TP_ADD_TC(tp, lookup_cap_dotdot__negative);

	return (atf_no_error());
}
