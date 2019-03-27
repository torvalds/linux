/*-
 * Copyright (c) 2017 Ngie Cooper <ngie@freebsd.org>
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
#include <kvm.h>
#include <limits.h>
#include <paths.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atf-c.h>

#include "kvm_test_common.h"

ATF_TC_WITHOUT_HEAD(kvm_open_negative_test_nonexistent_corefile);
ATF_TC_BODY(kvm_open_negative_test_nonexistent_corefile, tc)
{

	ATF_CHECK(kvm_open(NULL, "/nonexistent", NULL, O_RDONLY, NULL) == NULL);
	ATF_CHECK(kvm_open(NULL, "/nonexistent", NULL, O_RDONLY,
	    getprogname()) == NULL);
}

ATF_TC_WITHOUT_HEAD(kvm_open_negative_test_nonexistent_execfile);
ATF_TC_BODY(kvm_open_negative_test_nonexistent_execfile, tc)
{

	ATF_CHECK(kvm_open("/nonexistent", _PATH_DEVZERO, NULL, O_RDONLY,
	    NULL) == NULL);
	ATF_CHECK(kvm_open("/nonexistent", _PATH_DEVZERO, NULL, O_RDONLY,
	    getprogname()) == NULL);
}

ATF_TC(kvm_open_negative_test_invalid_corefile);
ATF_TC_HEAD(kvm_open_negative_test_invalid_corefile, tc)
{

	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(kvm_open_negative_test_invalid_corefile, tc)
{
	kvm_t *kd;

	atf_utils_create_file("some-file", "this is a text file");
	kd = kvm_open(NULL, "some-file", NULL, O_RDONLY, getprogname());
	ATF_REQUIRE_MSG(kd == NULL, "kvm_open didn't return NULL on failure");
}

ATF_TC(kvm_open_negative_test_invalid_execfile);
ATF_TC_HEAD(kvm_open_negative_test_invalid_execfile, tc)
{

	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(kvm_open_negative_test_invalid_execfile, tc)
{
	kvm_t *kd;

	atf_utils_create_file("some-file", "this is a text file");
	kd = kvm_open("some-file", "/bin/sh", NULL, O_RDONLY, getprogname());
	ATF_REQUIRE_MSG(kd == NULL, "kvm_open succeeded unexpectedly");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, kvm_open_negative_test_invalid_corefile);
	ATF_TP_ADD_TC(tp, kvm_open_negative_test_invalid_execfile);
	ATF_TP_ADD_TC(tp, kvm_open_negative_test_nonexistent_corefile);
	ATF_TP_ADD_TC(tp, kvm_open_negative_test_nonexistent_execfile);

	return (atf_no_error());
}
