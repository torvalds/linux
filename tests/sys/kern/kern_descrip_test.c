/*-
 * Copyright (c) 2014 EMC Corp.
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
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/limits.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <atf-c.h>

static volatile sig_atomic_t done;

#define AFILE "afile"
#define EXPANDBY 1000
#define PARALLEL 4
#define RENDEZVOUS "rendezvous"
#define VALUE "value"

ATF_TC_WITHOUT_HEAD(dup2__simple);
ATF_TC_BODY(dup2__simple, tc)
{
	int fd1, fd2;
	struct stat sb1, sb2;

	ATF_REQUIRE((fd1 = open(AFILE, O_CREAT, 0644)) != -1);
	fd2 = 27;
	ATF_REQUIRE(dup2(fd1, fd2) != -1);
	ATF_REQUIRE(fstat(fd1, &sb1) != -1);
	ATF_REQUIRE(fstat(fd2, &sb2) != -1);
	ATF_REQUIRE(bcmp(&sb1, &sb2, sizeof(sb1)) == 0);
}

ATF_TC(dup2__ebadf_when_2nd_arg_out_of_range);
ATF_TC_HEAD(dup2__ebadf_when_2nd_arg_out_of_range, tc)
{
	atf_tc_set_md_var(tc, "descr", "Regression test for r234131");
}

ATF_TC_BODY(dup2__ebadf_when_2nd_arg_out_of_range, tc)
{
	int fd1, fd2, ret;

	ATF_REQUIRE((fd1 = open(AFILE, O_CREAT, 0644)) != -1);
	fd2 = INT_MAX;
	ret = dup2(fd1, fd2);
	ATF_CHECK_EQ(-1, ret);
	ATF_CHECK_EQ(EBADF, errno);
}

static void
handler(int s __unused)
{
	done++;
}

static void
openfiles2(size_t n)
{
	size_t i;
	int r;

	errno = 0;
	for (i = 0; i < n; i++)
		ATF_REQUIRE((r = open(AFILE, O_RDONLY)) != -1);
	kill(getppid(), SIGUSR1);

	for (;;) {
		if (access(RENDEZVOUS, R_OK) != 0)
			break;
		usleep(1000);
	}
	_exit(0);
}

static void
openfiles(size_t n)
{
	int i, fd;

	signal(SIGUSR1, handler);
	ATF_REQUIRE((fd = open(AFILE, O_CREAT, 0644)) != -1);
	close(fd);
	ATF_REQUIRE((fd = open(RENDEZVOUS, O_CREAT, 0644)) != -1);
	close(fd);
	done = 0;
	for (i = 0; i < PARALLEL; i++)
		if (fork() == 0)
			openfiles2(n / PARALLEL);
	while (done != PARALLEL)
		usleep(1000);
	unlink(RENDEZVOUS);
	usleep(40000);
}

ATF_TC_WITH_CLEANUP(kern_maxfiles__increase);
ATF_TC_HEAD(kern_maxfiles__increase, tc)
{
	atf_tc_set_md_var(tc, "require.user", "root");
	atf_tc_set_md_var(tc, "require.config", "allow_sysctl_side_effects");
	atf_tc_set_md_var(tc, "descr",
	    "Check kern.maxfiles expansion");
}

ATF_TC_BODY(kern_maxfiles__increase, tc)
{
	size_t oldlen;
	int maxfiles, oldmaxfiles, current;
	char buf[80];

	oldlen = sizeof(maxfiles);
	if (sysctlbyname("kern.maxfiles", &maxfiles, &oldlen, NULL, 0) == -1)
		atf_tc_fail("getsysctlbyname(%s): %s", "kern.maxfiles",
		    strerror(errno));
	if (sysctlbyname("kern.openfiles", &current, &oldlen, NULL, 0) == -1)
		atf_tc_fail("getsysctlbyname(%s): %s", "kern.openfiles",
		    strerror(errno));

	oldmaxfiles = maxfiles;

	/* Store old kern.maxfiles in a symlink for cleanup */
	snprintf(buf, sizeof(buf), "%d", oldmaxfiles);
	if (symlink(buf, VALUE) == 1)
		atf_tc_fail("symlink(%s, %s): %s", buf, VALUE,
		    strerror(errno));

	maxfiles += EXPANDBY;
	if (sysctlbyname("kern.maxfiles", NULL, 0, &maxfiles, oldlen) == -1)
		atf_tc_fail("getsysctlbyname(%s): %s", "kern.maxfiles",
		    strerror(errno));

	openfiles(oldmaxfiles - current + 1);
	(void)unlink(VALUE);
}

ATF_TC_CLEANUP(kern_maxfiles__increase, tc)
{
	size_t oldlen;
	int n, oldmaxfiles;
	char buf[80];

	if ((n = readlink(VALUE, buf, sizeof(buf))) > 0) {
		buf[MIN((size_t)n, sizeof(buf) - 1)] = '\0';
		if (sscanf(buf, "%d", &oldmaxfiles) == 1) {
			oldlen = sizeof(oldmaxfiles);
			(void) sysctlbyname("kern.maxfiles", NULL, 0,
			    &oldmaxfiles, oldlen);
		}
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, dup2__simple);
	ATF_TP_ADD_TC(tp, dup2__ebadf_when_2nd_arg_out_of_range);
	ATF_TP_ADD_TC(tp, kern_maxfiles__increase);

	return (atf_no_error());
}
