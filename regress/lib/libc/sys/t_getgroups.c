/*	$OpenBSD: t_getgroups.c,v 1.3 2021/12/13 16:56:48 deraadt Exp $	*/
/* $NetBSD: t_getgroups.c,v 1.1 2011/07/07 06:57:53 jruoho Exp $ */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jukka Ruohonen.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "macros.h"

#include <sys/wait.h>

#include "atf-c.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

ATF_TC(getgroups_err);
ATF_TC_HEAD(getgroups_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test errors in getgroups(2)");
}

ATF_TC_BODY(getgroups_err, tc)
{
	gid_t gidset[NGROUPS_MAX];

	errno = 0;

#if __OpenBSD__
	ATF_REQUIRE(getgroups(NGROUPS_MAX, (gid_t *)-1) == -1);
#else
	ATF_REQUIRE(getgroups(10, (gid_t *)-1) == -1);
#endif
	ATF_REQUIRE(errno == EFAULT);

	errno = 0;

	ATF_REQUIRE(getgroups(-1, gidset) == -1);
	ATF_REQUIRE(errno == EINVAL);
}

ATF_TC(getgroups_getgid);
ATF_TC_HEAD(getgroups_getgid, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test getgid(2) from getgroups(2)");
}

ATF_TC_BODY(getgroups_getgid, tc)
{
	gid_t gidset[NGROUPS_MAX];
	gid_t gid = getgid();
	int i, n;

	/*
	 * Check that getgid(2) is found from
	 * the GIDs returned by getgroups(2).
	 */
	n = getgroups(NGROUPS_MAX, gidset);

	for (i = 0; i < n; i++) {

		if (gidset[i] == gid)
			return;
	}

	atf_tc_fail("getgid(2) not found from getgroups(2)");
}

ATF_TC(getgroups_setgid);
ATF_TC_HEAD(getgroups_setgid, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test setgid(2) from getgroups(2)");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(getgroups_setgid, tc)
{
	gid_t gidset[NGROUPS_MAX];
	int i, n, rv, sta;
	pid_t pid;

	/*
	 * Check that we can setgid(2)
	 * to the returned group IDs.
	 */
	n = getgroups(NGROUPS_MAX, gidset);
	ATF_REQUIRE(n >= 0);

	for (i = 0; i < n; i++) {

		pid = fork();
		ATF_REQUIRE(pid >= 0);

		if (pid == 0) {

			rv = setgid(gidset[i]);

			if (rv != 0)
				_exit(EXIT_FAILURE);

			_exit(EXIT_SUCCESS);
		}

		(void)wait(&sta);

		if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != EXIT_SUCCESS)
			atf_tc_fail("getgroups(2) is inconsistent");
	}
}

ATF_TC(getgroups_zero);
ATF_TC_HEAD(getgroups_zero, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test getgroups(2) with zero param");
}

ATF_TC_BODY(getgroups_zero, tc)
{
	const gid_t val = 123456789;
	gid_t gidset[NGROUPS_MAX];
	size_t i;

	/*
	 * If the first parameter is zero, the number
	 * of groups should be returned but the supplied
	 * buffer should remain intact.
	 */
	for (i = 0; i < __arraycount(gidset); i++)
		gidset[i] = val;

	ATF_REQUIRE(getgroups(0, gidset) >= 0);

	for (i = 0; i < __arraycount(gidset); i++) {

		if (gidset[i] != val)
			atf_tc_fail("getgroups(2) modified the buffer");
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, getgroups_err);
	ATF_TP_ADD_TC(tp, getgroups_getgid);
	ATF_TP_ADD_TC(tp, getgroups_setgid);
	ATF_TP_ADD_TC(tp, getgroups_zero);

	return atf_no_error();
}
