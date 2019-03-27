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

static char *buf;
static size_t len;

static void
assert_stream(const char *contents)
{
	if (strlen(contents) != len)
		printf("bad length %zd for \"%s\"\n", len, contents);
	else if (strncmp(buf, contents, strlen(contents)) != 0)
		printf("bad buffer \"%s\" for \"%s\"\n", buf, contents);
}

ATF_TC_WITHOUT_HEAD(open_group_test);
ATF_TC_BODY(open_group_test, tc)
{
	FILE *fp;
	off_t eob;

	fp = open_memstream(&buf, &len);
	ATF_REQUIRE_MSG(fp != NULL, "open_memstream failed");

	fprintf(fp, "hello my world");
	fflush(fp);
	assert_stream("hello my world");
	eob = ftello(fp);
	rewind(fp);
	fprintf(fp, "good-bye");
	fseeko(fp, eob, SEEK_SET);
	fclose(fp);
	assert_stream("good-bye world");
	free(buf);
}

ATF_TC_WITHOUT_HEAD(simple_tests);
ATF_TC_BODY(simple_tests, tc)
{
	static const char zerobuf[] =
	    { 'f', 'o', 'o', 0, 0, 0, 0, 'b', 'a', 'r', 0 };
	char c;
	FILE *fp;

	fp = open_memstream(&buf, NULL);
	ATF_REQUIRE_MSG(fp == NULL, "open_memstream did not fail");
	ATF_REQUIRE_MSG(errno == EINVAL,
	    "open_memstream didn't fail with EINVAL");
	fp = open_memstream(NULL, &len);
	ATF_REQUIRE_MSG(fp == NULL, "open_memstream did not fail");
	ATF_REQUIRE_MSG(errno == EINVAL,
	    "open_memstream didn't fail with EINVAL");
	fp = open_memstream(&buf, &len);
	ATF_REQUIRE_MSG(fp != NULL, "open_memstream failed; errno=%d", errno);
	fflush(fp);
	assert_stream("");
	if (fwide(fp, 0) >= 0)
		printf("stream is not byte-oriented\n");

	fprintf(fp, "fo");
	fflush(fp);
	assert_stream("fo");
	fputc('o', fp);
	fflush(fp);
	assert_stream("foo");
	rewind(fp);
	fflush(fp);
	assert_stream("");
	fseek(fp, 0, SEEK_END);
	fflush(fp);
	assert_stream("foo");

	/*
	 * Test seeking out past the current end.  Should zero-fill the
	 * intermediate area.
	 */
	fseek(fp, 4, SEEK_END);
	fprintf(fp, "bar");
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
	fprintf(fp, " in ");
	fflush(fp);
	assert_stream("foo in ");
	fseek(fp, 0, SEEK_END);
	fflush(fp);
	assert_stream("foo in bar");

	rewind(fp);
	if (fread(&c, sizeof(c), 1, fp) != 0)
		printf("fread did not fail\n");
	else if (!ferror(fp))
		printf("error indicator not set after fread\n");
	else
		clearerr(fp);

	fseek(fp, 4, SEEK_SET);
	fprintf(fp, "bar baz");
	fclose(fp);
	assert_stream("foo bar baz");
	free(buf);
}

ATF_TC_WITHOUT_HEAD(seek_tests);
ATF_TC_BODY(seek_tests, tc)
{
	FILE *fp;

	fp = open_memstream(&buf, &len);
	ATF_REQUIRE_MSG(fp != NULL, "open_memstream failed: %d", errno);

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
	fprintf(fp, "foo");
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
