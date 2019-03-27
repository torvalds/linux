/*-
 * Copyright (c) 2001 Wes Peters <wes@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atf-c.h>

static char buf[64];
static char *sret;
static int iret;

ATF_TC_WITHOUT_HEAD(strerror_unknown_error);
ATF_TC_BODY(strerror_unknown_error, tc)
{

	errno = 0;
	sret = strerror(INT_MAX);
	snprintf(buf, sizeof(buf), "Unknown error: %d", INT_MAX);
	ATF_CHECK(strcmp(sret, buf) == 0);
	ATF_CHECK(errno == EINVAL);
}

ATF_TC_WITHOUT_HEAD(strerror_no_error);
ATF_TC_BODY(strerror_no_error, tc)
{

	errno = 0;
	sret = strerror(0);
	ATF_CHECK(strcmp(sret, "No error: 0") == 0);
	ATF_CHECK(errno == 0);
}

ATF_TC_WITHOUT_HEAD(strerror_EPERM_test);
ATF_TC_BODY(strerror_EPERM_test, tc)
{

	errno = 0;
	sret = strerror(EPERM);
	ATF_CHECK(strcmp(sret, "Operation not permitted") == 0);
	ATF_CHECK(errno == 0);
}

ATF_TC_WITHOUT_HEAD(strerror_EPFNOSUPPORT_test);
ATF_TC_BODY(strerror_EPFNOSUPPORT_test, tc)
{

	errno = 0;
	sret = strerror(EPFNOSUPPORT);
	ATF_CHECK(strcmp(sret, "Protocol family not supported") == 0);
	ATF_CHECK(errno == 0);
}

ATF_TC_WITHOUT_HEAD(strerror_ELAST_test);
ATF_TC_BODY(strerror_ELAST_test, tc)
{

	errno = 0;
	sret = strerror(ELAST);
	ATF_CHECK(errno == 0);
}

ATF_TC_WITHOUT_HEAD(strerror_r__unknown_error);
ATF_TC_BODY(strerror_r__unknown_error, tc)
{

	memset(buf, '*', sizeof(buf));
	iret = strerror_r(-1, buf, sizeof(buf));
	ATF_CHECK(strcmp(buf, "Unknown error: -1") == 0);
	ATF_CHECK(iret == EINVAL);
}

ATF_TC_WITHOUT_HEAD(strerror_r__EPERM_one_byte_short);
ATF_TC_BODY(strerror_r__EPERM_one_byte_short, tc)
{

	memset(buf, '*', sizeof(buf));
	/* One byte too short. */
	iret = strerror_r(EPERM, buf, strlen("Operation not permitted"));
	ATF_CHECK(strcmp(buf, "Operation not permitte") == 0);
	ATF_CHECK(iret == ERANGE);
}

ATF_TC_WITHOUT_HEAD(strerror_r__EPERM_unknown_error_one_byte_short);
ATF_TC_BODY(strerror_r__EPERM_unknown_error_one_byte_short, tc)
{

	memset(buf, '*', sizeof(buf));
	/* One byte too short. */
	iret = strerror_r(-1, buf, strlen("Unknown error: -1"));
	ATF_CHECK(strcmp(buf, "Unknown error: -") == 0);
	ATF_CHECK(iret == EINVAL);
}

ATF_TC_WITHOUT_HEAD(strerror_r__EPERM_unknown_error_two_bytes_short);
ATF_TC_BODY(strerror_r__EPERM_unknown_error_two_bytes_short, tc)
{

	memset(buf, '*', sizeof(buf));
	/* Two bytes too short. */
	iret = strerror_r(-2, buf, strlen("Unknown error: -2") - 1);
	ATF_CHECK(strcmp(buf, "Unknown error: ") == 0);
	ATF_CHECK(iret == EINVAL);
}

ATF_TC_WITHOUT_HEAD(strerror_r__EPERM_unknown_error_three_bytes_short);
ATF_TC_BODY(strerror_r__EPERM_unknown_error_three_bytes_short, tc)
{

	memset(buf, '*', sizeof(buf));
	/* Three bytes too short. */
	iret = strerror_r(-2, buf, strlen("Unknown error: -2") - 2);
	ATF_CHECK(strcmp(buf, "Unknown error:") == 0);
	ATF_CHECK(iret == EINVAL);
}

ATF_TC_WITHOUT_HEAD(strerror_r__EPERM_unknown_error_12345_one_byte_short);
ATF_TC_BODY(strerror_r__EPERM_unknown_error_12345_one_byte_short, tc)
{

	memset(buf, '*', sizeof(buf));
	/* One byte too short. */
	iret = strerror_r(12345, buf, strlen("Unknown error: 12345"));
	ATF_CHECK(strcmp(buf, "Unknown error: 1234") == 0);
	ATF_CHECK(iret == EINVAL);
}

ATF_TC_WITHOUT_HEAD(strerror_r__no_error);
ATF_TC_BODY(strerror_r__no_error, tc)
{

	memset(buf, '*', sizeof(buf));
	iret = strerror_r(0, buf, sizeof(buf));
	ATF_CHECK(strcmp(buf, "No error: 0") == 0);
	ATF_CHECK(iret == 0);
}

ATF_TC_WITHOUT_HEAD(strerror_r__EDEADLK);
ATF_TC_BODY(strerror_r__EDEADLK, tc)
{

	memset(buf, '*', sizeof(buf));
	iret = strerror_r(EDEADLK, buf, sizeof(buf));
	ATF_CHECK(strcmp(buf, "Resource deadlock avoided") == 0);
	ATF_CHECK(iret == 0);
}

ATF_TC_WITHOUT_HEAD(strerror_r__EPROCLIM);
ATF_TC_BODY(strerror_r__EPROCLIM, tc)
{

	memset(buf, '*', sizeof(buf));
	iret = strerror_r(EPROCLIM, buf, sizeof(buf));
	ATF_CHECK(strcmp(buf, "Too many processes") == 0);
	ATF_CHECK(iret == 0);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, strerror_unknown_error);
	ATF_TP_ADD_TC(tp, strerror_no_error);
	ATF_TP_ADD_TC(tp, strerror_EPERM_test);
	ATF_TP_ADD_TC(tp, strerror_EPFNOSUPPORT_test);
	ATF_TP_ADD_TC(tp, strerror_ELAST_test);
	ATF_TP_ADD_TC(tp, strerror_r__unknown_error);
	ATF_TP_ADD_TC(tp, strerror_r__EPERM_one_byte_short);
	ATF_TP_ADD_TC(tp, strerror_r__EPERM_unknown_error_one_byte_short);
	ATF_TP_ADD_TC(tp, strerror_r__EPERM_unknown_error_two_bytes_short);
	ATF_TP_ADD_TC(tp, strerror_r__EPERM_unknown_error_three_bytes_short);
	ATF_TP_ADD_TC(tp, strerror_r__EPERM_unknown_error_12345_one_byte_short);
	ATF_TP_ADD_TC(tp, strerror_r__no_error);
	ATF_TP_ADD_TC(tp, strerror_r__EDEADLK);
	ATF_TP_ADD_TC(tp, strerror_r__EPROCLIM);

	return (atf_no_error());
}
