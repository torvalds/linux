/*-
 * Copyright (c) 2017 Conrad Meyer <cem@FreeBSD.org>
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

/*
 * Limited test program for getmntinfo(3), a non-standard BSDism.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/ucred.h>

#include <errno.h>

#include <atf-c.h>

static void
check_mntinfo(struct statfs *mntinfo, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		ATF_REQUIRE_MSG(mntinfo[i].f_version == STATFS_VERSION, "%ju",
		    (uintmax_t)mntinfo[i].f_version);
		ATF_REQUIRE(mntinfo[i].f_namemax <= sizeof(mntinfo[0].f_mntonname));
	}
}

ATF_TC_WITHOUT_HEAD(getmntinfo_test);
ATF_TC_BODY(getmntinfo_test, tc)
{
	int nmnts;
	struct statfs *mntinfo;

	/* Test bogus mode */
	nmnts = getmntinfo(&mntinfo, 199);
	ATF_REQUIRE_MSG(nmnts == 0 && errno == EINVAL,
	    "getmntinfo() succeeded; errno=%d", errno);

	/* Valid modes */
	nmnts = getmntinfo(&mntinfo, MNT_NOWAIT);
	ATF_REQUIRE_MSG(nmnts != 0, "getmntinfo(MNT_NOWAIT) failed; errno=%d",
	    errno);

	check_mntinfo(mntinfo, nmnts);
	memset(mntinfo, 0xdf, sizeof(*mntinfo) * nmnts);

	nmnts = getmntinfo(&mntinfo, MNT_WAIT);
	ATF_REQUIRE_MSG(nmnts != 0, "getmntinfo(MNT_WAIT) failed; errno=%d",
	    errno);

	check_mntinfo(mntinfo, nmnts);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, getmntinfo_test);

	return (atf_no_error());
}
