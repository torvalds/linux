/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Conrad Meyer <cem@FreeBSD.org>
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
#include <signal.h>
#include <unistd.h>

#include <atf-c.h>

ATF_TC_WITHOUT_HEAD(getentropy_count);
ATF_TC_BODY(getentropy_count, tc)
{
	char buf[2];
	int ret;

	/* getentropy(2) does not modify buf past the requested length */
	buf[1] = 0x7C;
	ret = getentropy(buf, 1);
	ATF_REQUIRE_EQ(ret, 0);
	ATF_REQUIRE_EQ(buf[1], 0x7C);
}

ATF_TC_WITHOUT_HEAD(getentropy_fault);
ATF_TC_BODY(getentropy_fault, tc)
{
	int ret;

	ret = getentropy(NULL, 1);
	ATF_REQUIRE_EQ(ret, -1);
	ATF_REQUIRE_EQ(errno, EFAULT);
}

ATF_TC_WITHOUT_HEAD(getentropy_sizes);
ATF_TC_BODY(getentropy_sizes, tc)
{
	char buf[512];

	ATF_REQUIRE_EQ(getentropy(buf, sizeof(buf)), -1);
	ATF_REQUIRE_EQ(errno, EIO);
	ATF_REQUIRE_EQ(getentropy(buf, 257), -1);
	ATF_REQUIRE_EQ(errno, EIO);

	/* Smaller sizes always succeed: */
	ATF_REQUIRE_EQ(getentropy(buf, 256), 0);
	ATF_REQUIRE_EQ(getentropy(buf, 128), 0);
	ATF_REQUIRE_EQ(getentropy(buf, 0), 0);
}

ATF_TP_ADD_TCS(tp)
{

	signal(SIGSYS, SIG_IGN);

	ATF_TP_ADD_TC(tp, getentropy_count);
	ATF_TP_ADD_TC(tp, getentropy_fault);
	ATF_TP_ADD_TC(tp, getentropy_sizes);
	return (atf_no_error());
}
