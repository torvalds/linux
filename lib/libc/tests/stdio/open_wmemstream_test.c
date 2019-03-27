/*-
 * Copyright (c) 2013 Hudson River Trading LLC
 * Written by: John H. Baldwin <jhb@FreeBSD.org>
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

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <atf-c.h>

static wchar_t *buf;
static size_t len;

static void
assert_stream(const wchar_t *contents)
{
	if (wcslen(contents) != len)
		printf("bad length %zd for \"%ls\"\n", len, contents);
	else if (wcsncmp(buf, contents, wcslen(contents)) != 0)
		printf("bad buffer \"%ls\" for \"%ls\"\n", buf, contents);
}

ATF_TC_WITHOUT_HEAD(open_group_test);
ATF_TC_BODY(open_group_test, tc)
{
	FILE *fp;
	off_t eob;

	fp = open_wmemstream(&buf, &len);
	ATF_REQUIRE_MSG(fp != NULL, "open_wmemstream failed");

	fwprintf(fp, L"hello my world");
	fflush(fp);
	assert_stream(L"hello my world");
	eob = ftello(fp);
	rewind(fp);
	fwprintf(fp, L"good-bye");
	fseeko(fp, eob, SEEK_SET);
	fclose(fp);
	assert_stream(L"good-bye world");
	free(buf);
}

ATF_TC_WITHOUT_HEAD(simple_tests);
ATF_TC_BODY(simple_tests, tc)
{
	static const wchar_t zerobuf[] =
	    { L'f', L'o', L'o', 0, 0, 0, 0, L'b', L'a', L'r', 0 };
	wchar_t c;
	FILE *fp;

	fp = open_wmemstream(&buf, NULL);
	ATF_REQUIRE_MSG(fp == NULL, "open_wmemstream did not fail");
	ATF_REQUIRE_MSG(errno == EINVAL,
	    "open_wmemstream didn't fail with EINVAL");
	fp = open_wmemstream(NULL, &len);
	ATF_REQUIRE_MSG(fp == NULL, "open_wmemstream did not fail");
	ATF_REQUIRE_MSG(errno == EINVAL,
	    "open_wmemstream didn't fail with EINVAL");
	fp = open_wmemstream(&buf, &len);
	ATF_REQUIRE_MSG(fp != NULL, "open_memstream failed; errno=%d", errno);
	fflush(fp);
	assert_stream(L"");
	if (fwide(fp, 0) <= 0)
		printf("stream is not wide-oriented\n");

	fwprintf(fp, L"fo");
	fflush(fp);
	assert_stream(L"fo");
	fputwc(L'o', fp);
	fflush(fp);
	assert_stream(L"foo");
	rewind(fp);
	fflush(fp);
	assert_stream(L"");
	fseek(fp, 0, SEEK_END);
	fflush(fp);
	assert_stream(L"foo");

	/*
	 * Test seeking out past the current end.  Should zero-fill the
	 * intermediate area.
	 */
	fseek(fp, 4, SEEK_END);
	fwprintf(fp, L"bar");
	fflush(fp);

	/*
	 * Can't use assert_stream() here since this should contain
	 * embedded null characters.
	 */
	if (len != 10)
		printf("bad length %zd for zero-fill test\n", len);
	else if (memcmp(buf, zerobuf, sizeof(zerobuf)) != 0)
		printf("bad buffer for zero-fill test\n");

	fseek(fp, 3, SEEK_SET);
	fwprintf(fp, L" in ");
	fflush(fp);
	assert_stream(L"foo in ");
	fseek(fp, 0, SEEK_END);
	fflush(fp);
	assert_stream(L"foo in bar");

	rewind(fp);
	if (fread(&c, sizeof(c), 1, fp) != 0)
		printf("fread did not fail\n");
	else if (!ferror(fp))
		printf("error indicator not set after fread\n");
	else
		clearerr(fp);

	fseek(fp, 4, SEEK_SET);
	fwprintf(fp, L"bar baz");
	fclose(fp);
	assert_stream(L"foo bar baz");
	free(buf);
}

ATF_TC_WITHOUT_HEAD(seek_tests);
ATF_TC_BODY(seek_tests, tc)
{
	FILE *fp;

	fp = open_wmemstream(&buf, &len);
	ATF_REQUIRE_MSG(fp != NULL, "open_wmemstream failed; errno=%d", errno);

#define SEEK_FAIL(offset, whence, error) do {			\
	errno = 0;						\
	ATF_REQUIRE_MSG(fseeko(fp, (offset), (whence)) != 0,	\
	    "fseeko(%s, %s) did not fail, set pos to %jd",	\
	    __STRING(offset), __STRING(whence),			\
	    (intmax_t)ftello(fp));				\
	ATF_REQUIRE_MSG(errno == (error),			\
	    "fseeko(%s, %s) failed with %d rather than %s",	\
	    __STRING(offset), __STRING(whence),	errno,		\
	    __STRING(error));					\
} while (0)

#define SEEK_OK(offset, whence, result) do {			\
	ATF_REQUIRE_MSG(fseeko(fp, (offset), (whence)) == 0,	\
	    "fseeko(%s, %s) failed: %s",			\
	    __STRING(offset), __STRING(whence), strerror(errno)); \
	ATF_REQUIRE_MSG(ftello(fp) == (result),			\
	    "fseeko(%s, %s) seeked to %jd rather than %s",	\
	    __STRING(offset), __STRING(whence),			\
	    (intmax_t)ftello(fp), __STRING(result));		\
} while (0)

	SEEK_FAIL(-1, SEEK_SET, EINVAL);
	SEEK_FAIL(-1, SEEK_CUR, EINVAL);
	SEEK_FAIL(-1, SEEK_END, EINVAL);
	fwprintf(fp, L"foo");
	SEEK_OK(-1, SEEK_CUR, 2);
	SEEK_OK(0, SEEK_SET, 0);
	SEEK_OK(-1, SEEK_END, 2);
	SEEK_OK(OFF_MAX - 1, SEEK_SET, OFF_MAX - 1);
	SEEK_FAIL(2, SEEK_CUR, EOVERFLOW);
	fclose(fp);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, open_group_test);
	ATF_TP_ADD_TC(tp, simple_tests);
	ATF_TP_ADD_TC(tp, seek_tests);

	return (atf_no_error());
}
