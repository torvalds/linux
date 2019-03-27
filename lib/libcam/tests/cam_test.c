/*-
 * Copyright (c) 2017 Spectra Logic Corporation
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

/* Tests functions in sys/cam/cam.c */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <camlib.h>

#include <atf-c.h>

#define ATF_CHECK_NE(x, y) ATF_CHECK((x) != (y))

ATF_TC_WITHOUT_HEAD(cam_strmatch);
ATF_TC_BODY(cam_strmatch, tc)
{
	/* Basic fixed patterns */
	ATF_CHECK_EQ(0, cam_strmatch("foo", "foo", 3));
	ATF_CHECK_NE(0, cam_strmatch("foo", "bar", 3));
	ATF_CHECK_NE(0, cam_strmatch("foo", "foobar", 3));

	/* The str is not necessarily null-terminated */
	ATF_CHECK_EQ(0, cam_strmatch("fooxuehfxeuf", "foo", 3));
	ATF_CHECK_NE(0, cam_strmatch("foo\0bar", "foo", 7));

	/* Eat trailing spaces, which get added by SAT */
	ATF_CHECK_EQ(0, cam_strmatch("foo             ", "foo", 16));

	/* '*' matches everything, like shell globbing */
	ATF_CHECK_EQ(0, cam_strmatch("foobar", "foo*", 6));
	ATF_CHECK_EQ(0, cam_strmatch("foobar", "*bar", 6));
	ATF_CHECK_NE(0, cam_strmatch("foobar", "foo*x", 6));
	ATF_CHECK_EQ(0, cam_strmatch("foobarbaz", "*bar*", 9));
	/* Even NUL */
	ATF_CHECK_EQ(0, cam_strmatch("foo\0bar", "foo*", 7));
	/* Or nothing */
	ATF_CHECK_EQ(0, cam_strmatch("foo", "foo*", 3));
	/* But stuff after the * still must match */
	ATF_CHECK_NE(0, cam_strmatch("foo", "foo*x", 3));

	/* '?' matches exactly one single character */
	ATF_CHECK_EQ(0, cam_strmatch("foobar", "foo?ar", 6));
	ATF_CHECK_NE(0, cam_strmatch("foo", "foo?", 3));
	/* Even NUL */
	ATF_CHECK_EQ(0, cam_strmatch("foo\0bar", "foo?bar", 7));

	/* '[]' contains a set of characters */
	ATF_CHECK_EQ(0, cam_strmatch("foobar", "foo[abc]ar", 6));
	ATF_CHECK_EQ(0, cam_strmatch("foobar", "foo[b]ar", 6));
	ATF_CHECK_NE(0, cam_strmatch("foobar", "foo[ac]ar", 6));

	/* '[]' can contain a range of characters, too */
	ATF_CHECK_EQ(0, cam_strmatch("foobar", "foo[a-c]ar", 6));
	ATF_CHECK_EQ(0, cam_strmatch("fooxar", "foo[a-cx]ar", 6));
	ATF_CHECK_NE(0, cam_strmatch("foodar", "foo[a-c]ar", 6));
	
	/* Back-to-back '[]' character sets */
	ATF_CHECK_EQ(0, cam_strmatch("foobar", "fo[a-z][abc]ar", 6));
	ATF_CHECK_NE(0, cam_strmatch("foAbar", "fo[a-z][abc]ar", 6));
	ATF_CHECK_NE(0, cam_strmatch("foodar", "fo[a-z][abc]ar", 6));

	/* A '^' negates a set of characters */
	ATF_CHECK_NE(0, cam_strmatch("foobar", "foo[^abc]ar", 6));
	ATF_CHECK_NE(0, cam_strmatch("foobar", "foo[^b]ar", 6));
	ATF_CHECK_EQ(0, cam_strmatch("foobar", "foo[^ac]ar", 6));
	ATF_CHECK_NE(0, cam_strmatch("foobar", "foo[^a-c]ar", 6));
	ATF_CHECK_NE(0, cam_strmatch("fooxar", "foo[^a-cx]ar", 6));
	ATF_CHECK_EQ(0, cam_strmatch("foodar", "foo[^a-c]ar", 6));

	/* Outside of '[]' a ']' is just an ordinary character */
	ATF_CHECK_EQ(0, cam_strmatch("f]o", "f]o", 3));
	ATF_CHECK_NE(0, cam_strmatch("foo", "f]o", 3));

	/* Matching a literal '[' requires specifying a range */
	ATF_CHECK_EQ(0, cam_strmatch("f[o", "f[[]o", 3));
	ATF_CHECK_NE(0, cam_strmatch("foo", "f[[]o", 3));
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, cam_strmatch);

	return (atf_no_error());
}
