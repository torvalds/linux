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
#include <sys/sbuf.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <atf-c.h>

#include "sbuf_test_common.h"

static char	test_string[] = "this is a test string";
#define	TEST_STRING_CHOP_COUNT	5
_Static_assert(nitems(test_string) > TEST_STRING_CHOP_COUNT,
    "test_string is too short");

ATF_TC_WITHOUT_HEAD(sbuf_clear_test);
ATF_TC_BODY(sbuf_clear_test, tc)
{
	struct sbuf *sb;
	ssize_t buf_len;
	pid_t child_proc;

	sb = sbuf_new_auto();
	ATF_REQUIRE_MSG(sb != NULL, "sbuf_new_auto failed: %s",
	    strerror(errno));

	ATF_REQUIRE_MSG(sbuf_cat(sb, test_string) == 0, "sbuf_cat failed");

	/*
	 * Cheat so we can get the contents of the buffer before calling
	 * sbuf_finish(3) below, making additional sbuf changes impossible.
	 */
	child_proc = atf_utils_fork();
	if (child_proc == 0) {
		sbuf_putbuf(sb);
		exit(0);
	}
	atf_utils_wait(child_proc, 0, test_string, "");

	sbuf_clear(sb);

	ATF_REQUIRE_MSG(sbuf_finish(sb) == 0, "sbuf_finish failed: %s",
	    strerror(errno));

	buf_len = sbuf_len(sb);
	ATF_REQUIRE_MSG(buf_len == 0, "sbuf_len (%zd) != 0", buf_len);
	ATF_REQUIRE_STREQ_MSG(sbuf_data(sb), "",
	    "sbuf (\"%s\") was not empty", sbuf_data(sb));

	sbuf_delete(sb);
}

ATF_TC_WITHOUT_HEAD(sbuf_done_and_sbuf_finish_test);
ATF_TC_BODY(sbuf_done_and_sbuf_finish_test, tc)
{
	struct sbuf *sb;

	sb = sbuf_new_auto();
	ATF_REQUIRE_MSG(sb != NULL, "sbuf_new_auto failed: %s",
	    strerror(errno));

	ATF_CHECK(sbuf_done(sb) == 0);

	ATF_REQUIRE_MSG(sbuf_finish(sb) == 0, "sbuf_finish failed: %s",
	    strerror(errno));

	ATF_CHECK(sbuf_done(sb) != 0);

	sbuf_delete(sb);
}

ATF_TC_WITHOUT_HEAD(sbuf_len_test);
ATF_TC_BODY(sbuf_len_test, tc)
{
	struct sbuf *sb;
	ssize_t buf_len, test_string_len;
	int i;

	sb = sbuf_new_auto();
	ATF_REQUIRE_MSG(sb != NULL, "sbuf_new_auto failed: %s",
	    strerror(errno));

	test_string_len = strlen(test_string);
	for (i = 0; i < 20; i++) {
		buf_len = sbuf_len(sb);
		ATF_REQUIRE_MSG(buf_len == (ssize_t)(i * test_string_len),
		    "sbuf_len (%zd) != %zu", buf_len, i * test_string_len);
		ATF_REQUIRE_MSG(sbuf_cat(sb, test_string) == 0, "sbuf_cat failed");
	}

#ifdef	HAVE_SBUF_SET_FLAGS
	sbuf_set_flags(sb, SBUF_INCLUDENUL);
	ATF_REQUIRE_MSG((ssize_t)(i * test_string_len + 1) == sbuf_len(sb),
	    "sbuf_len(..) didn't report the NUL char");
#endif

	ATF_REQUIRE_MSG(sbuf_finish(sb) == 0, "sbuf_finish failed: %s",
	    strerror(errno));

	sbuf_delete(sb);
}

ATF_TC_WITHOUT_HEAD(sbuf_setpos_test);
ATF_TC_BODY(sbuf_setpos_test, tc)
{
	struct sbuf *sb;
	size_t test_string_chopped_len, test_string_len;
	ssize_t buf_len;

	sb = sbuf_new_auto();
	ATF_REQUIRE_MSG(sb != NULL, "sbuf_new_auto failed: %s",
	    strerror(errno));

	/*
	 * An obvious sanity check -- if sbuf_len(..) lies, these invariants
	 * are impossible to test.
	 */
	ATF_REQUIRE(sbuf_len(sb) == 0);

	ATF_CHECK(sbuf_setpos(sb, -1) == -1);
	ATF_CHECK(sbuf_setpos(sb, 0) == 0);
	ATF_CHECK(sbuf_setpos(sb, 1) == -1);

	ATF_REQUIRE_MSG(sbuf_cat(sb, test_string) == 0, "sbuf_cat failed");

	buf_len = sbuf_len(sb);
	test_string_len = strlen(test_string);
	test_string_chopped_len = test_string_len - TEST_STRING_CHOP_COUNT;
	ATF_REQUIRE_MSG(buf_len == (ssize_t)test_string_len,
	    "sbuf length (%zd) != test_string length (%zu)", buf_len,
	    test_string_len);

	/* Out of bounds (under length) */
	ATF_CHECK(sbuf_setpos(sb, -1) == -1);
	/*
	 * Out of bounds (over length)
	 *
	 * Note: SBUF_INCLUDENUL not set, so take '\0' into account.
	 */
	ATF_CHECK(sbuf_setpos(sb, test_string_len + 2) == -1);
	/* Within bounds */
	ATF_CHECK(sbuf_setpos(sb, test_string_chopped_len) == 0);

	ATF_REQUIRE_MSG(sbuf_finish(sb) == 0, "sbuf_finish failed: %s",
	    strerror(errno));

	buf_len = sbuf_len(sb);
	ATF_REQUIRE_MSG(buf_len == (ssize_t)test_string_chopped_len,
	    "sbuf_setpos didn't truncate string as expected");
	ATF_REQUIRE_MSG(strncmp(sbuf_data(sb), test_string, buf_len) == 0,
	    "sbuf (\"%s\") != test string (\"%s\") for [0,%zd]", sbuf_data(sb),
	    test_string, buf_len);

	sbuf_delete(sb);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, sbuf_clear_test);
	ATF_TP_ADD_TC(tp, sbuf_done_and_sbuf_finish_test);
	ATF_TP_ADD_TC(tp, sbuf_len_test);
#if 0
	/* TODO */
#ifdef	HAVE_SBUF_CLEAR_FLAGS
	ATF_TP_ADD_TC(tp, sbuf_clear_flags_test);
#endif
#ifdef	HAVE_SBUF_GET_FLAGS
	ATF_TP_ADD_TC(tp, sbuf_get_flags_test);
#endif
	ATF_TP_ADD_TC(tp, sbuf_new_positive_test);
	ATF_TP_ADD_TC(tp, sbuf_new_negative_test);
#ifdef	HAVE_SBUF_SET_FLAGS
	ATF_TP_ADD_TC(tp, sbuf_set_flags_test);
#endif
#endif
	ATF_TP_ADD_TC(tp, sbuf_setpos_test);

	return (atf_no_error());
}
