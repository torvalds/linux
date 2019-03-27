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
#include <libutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atf-c.h>

#include "sbuf_test_common.h"

static char	test_string[] = "this is a test string";
static char	test_whitespace_string[] = " \f\n\r\t\v ";
static int	test_buffer[] = { 0, 1, 2, 3, 4, 5, };

static void
check_buffers_equal(const void *sb_buf, const void *test_buf, size_t len)
{

	if (memcmp(sb_buf, test_buf, len) != 0) {
		printf("sbuf:\n");
		hexdump(sb_buf, len, NULL, 0),
		printf("test_buf:\n");
		hexdump(test_buf, len, NULL, 0);
		atf_tc_fail("contents of sbuf didn't match test_buf contents");
	}
}

ATF_TC_WITHOUT_HEAD(sbuf_bcat_test);
ATF_TC_BODY(sbuf_bcat_test, tc)
{
	struct sbuf *sb;
	int *test_buffer_tmp;
	ssize_t test_sbuf_len;

	test_buffer_tmp = malloc(sizeof(test_buffer) * 2);
	ATF_REQUIRE_MSG(test_buffer_tmp != NULL, "malloc failed");

	memcpy(test_buffer_tmp, test_buffer, sizeof(test_buffer));
	memcpy(&test_buffer_tmp[nitems(test_buffer)], test_buffer,
	    sizeof(test_buffer));

	sb = sbuf_new_auto();
	ATF_REQUIRE_MSG(sb != NULL, "sbuf_new_auto failed: %s",
	    strerror(errno));

	ATF_CHECK_MSG(sbuf_bcat(sb, test_buffer, sizeof(test_buffer)) == 0,
	    "sbuf_bcat failed");

	test_sbuf_len = sbuf_len(sb);
	ATF_REQUIRE_MSG(test_sbuf_len == (ssize_t)sizeof(test_buffer),
	    "sbuf_len(..) => %zd (actual) != %zu (expected)",
	    test_sbuf_len, sizeof(test_buffer));

	ATF_CHECK_MSG(sbuf_bcat(sb, test_buffer, sizeof(test_buffer)) == 0,
	    "sbuf_bcat failed");

	test_sbuf_len = sbuf_len(sb);
	ATF_REQUIRE_MSG(test_sbuf_len == (ssize_t)(2 * sizeof(test_buffer)),
	    "sbuf_len(..) => %zd (actual) != %zu (expected)",
	    test_sbuf_len, 2 * sizeof(test_buffer));

	ATF_REQUIRE_MSG(sbuf_finish(sb) == 0, "sbuf_finish failed: %s",
	    strerror(errno));

	check_buffers_equal(sbuf_data(sb), test_buffer_tmp,
	    (size_t)test_sbuf_len);

	sbuf_delete(sb);

	free(test_buffer_tmp);
}

ATF_TC_WITHOUT_HEAD(sbuf_bcpy_test);
ATF_TC_BODY(sbuf_bcpy_test, tc)
{
	struct sbuf *sb;
	ssize_t test_sbuf_len;

	sb = sbuf_new_auto();
	ATF_REQUIRE_MSG(sb != NULL, "sbuf_new_auto failed: %s",
	    strerror(errno));

	ATF_CHECK_MSG(sbuf_bcpy(sb, test_buffer, sizeof(test_buffer)) == 0,
	    "sbuf_bcpy failed");

	test_sbuf_len = sbuf_len(sb);
	ATF_REQUIRE_MSG(test_sbuf_len == (ssize_t)sizeof(test_buffer),
	    "sbuf_len(..) => %zd (actual) != %zu (expected)",
	    test_sbuf_len, sizeof(test_buffer));

	ATF_CHECK_MSG(sbuf_bcpy(sb, test_buffer, sizeof(test_buffer)) == 0,
	    "sbuf_bcpy failed");

	test_sbuf_len = sbuf_len(sb);
	ATF_REQUIRE_MSG(test_sbuf_len == (ssize_t)sizeof(test_buffer),
	    "sbuf_len(..) => %zd (actual) != %zu (expected)",
	    test_sbuf_len, sizeof(test_buffer));

	ATF_REQUIRE_MSG(sbuf_finish(sb) == 0, "sbuf_finish failed: %s",
	    strerror(errno));

	check_buffers_equal(sbuf_data(sb), test_buffer, (size_t)test_sbuf_len);

	sbuf_delete(sb);
}

ATF_TC_WITHOUT_HEAD(sbuf_cat_test);
ATF_TC_BODY(sbuf_cat_test, tc)
{
	struct sbuf *sb;
	char *test_string_tmp;
	ssize_t test_sbuf_len;

	asprintf(&test_string_tmp, "%s%s", test_string, test_string);
	ATF_REQUIRE_MSG(test_string_tmp != NULL, "asprintf failed");

	sb = sbuf_new_auto();
	ATF_REQUIRE_MSG(sb != NULL, "sbuf_new_auto failed: %s",
	    strerror(errno));

	ATF_CHECK_MSG(sbuf_cat(sb, test_string) == 0, "sbuf_cat failed");

	test_sbuf_len = sbuf_len(sb);
	ATF_REQUIRE_MSG(test_sbuf_len == (ssize_t)strlen(test_string),
	    "sbuf_len(..) => %zd (actual) != %zu (expected)",
	    test_sbuf_len, sizeof(test_string));

	ATF_CHECK_MSG(sbuf_cat(sb, test_string) == 0, "sbuf_cat failed");

	test_sbuf_len = sbuf_len(sb);
	ATF_REQUIRE_MSG(test_sbuf_len == (ssize_t)strlen(test_string_tmp),
	    "sbuf_len(..) => %zd (actual) != %zu (expected)",
	    test_sbuf_len, strlen(test_string_tmp));

	ATF_REQUIRE_MSG(sbuf_finish(sb) == 0, "sbuf_finish failed: %s",
	    strerror(errno));

	ATF_REQUIRE_STREQ_MSG(sbuf_data(sb), test_string_tmp,
	    "sbuf (\"%s\") != test string (\"%s\")", sbuf_data(sb),
	    test_string_tmp);

	sbuf_delete(sb);

	free(test_string_tmp);
}

ATF_TC_WITHOUT_HEAD(sbuf_cpy_test);
ATF_TC_BODY(sbuf_cpy_test, tc)
{
	struct sbuf *sb;
	ssize_t test_sbuf_len;

	sb = sbuf_new_auto();
	ATF_REQUIRE_MSG(sb != NULL, "sbuf_new_auto failed: %s",
	    strerror(errno));

	ATF_CHECK_MSG(sbuf_cpy(sb, test_string) == 0, "sbuf_cpy failed");

	test_sbuf_len = sbuf_len(sb);
	ATF_REQUIRE_MSG(test_sbuf_len == (ssize_t)strlen(test_string),
	    "sbuf_len(..) => %zd (actual) != %zu (expected)",
	    test_sbuf_len, strlen(test_string));

	ATF_CHECK_MSG(sbuf_cpy(sb, test_string) == 0, "sbuf_cpy failed");

	test_sbuf_len = sbuf_len(sb);
	ATF_REQUIRE_MSG(test_sbuf_len == (ssize_t)strlen(test_string),
	    "sbuf_len(..) => %zd (actual) != %zu (expected)",
	    test_sbuf_len, strlen(test_string));

	ATF_REQUIRE_MSG(sbuf_finish(sb) == 0, "sbuf_finish failed: %s",
	    strerror(errno));

	ATF_REQUIRE_STREQ_MSG(sbuf_data(sb), test_string,
	    "sbuf (\"%s\") != test string (\"%s\")", sbuf_data(sb),
	    test_string);

	sbuf_delete(sb);
}

ATF_TC_WITHOUT_HEAD(sbuf_putc_test);
ATF_TC_BODY(sbuf_putc_test, tc)
{
	struct sbuf *sb;
	ssize_t test_sbuf_len;
	size_t i;

	sb = sbuf_new_auto();
	ATF_REQUIRE_MSG(sb != NULL, "sbuf_new_auto failed: %s",
	    strerror(errno));

	for (i = 0; i <= strlen(test_string); i++) {	/* Include the NUL */
		ATF_REQUIRE_MSG(sbuf_putc(sb, test_string[i]) == 0,
		    "sbuf_putc failed");

		/* The best we can do until sbuf_finish(3) is called. */
		test_sbuf_len = sbuf_len(sb);
		ATF_REQUIRE_MSG((ssize_t)(i + 1) == test_sbuf_len,
		    "sbuf_len(..) => %zd (actual) != %zu (expected)",
		    test_sbuf_len, i + 1);
	}

	ATF_REQUIRE_MSG(sbuf_finish(sb) == 0, "sbuf_finish failed: %s",
	    strerror(errno));

	ATF_REQUIRE_STREQ_MSG(sbuf_data(sb), test_string,
	    "sbuf (\"%s\") != test string (\"%s\")", sbuf_data(sb),
	    test_string);

	sbuf_delete(sb);
}

ATF_TC_WITHOUT_HEAD(sbuf_trim_test);
ATF_TC_BODY(sbuf_trim_test, tc)
{
	struct sbuf *sb;
	ssize_t exp_sbuf_len, test_sbuf_len;

	sb = sbuf_new_auto();
	ATF_REQUIRE_MSG(sb != NULL, "sbuf_new_auto failed: %s",
	    strerror(errno));

	ATF_CHECK_MSG(sbuf_cpy(sb, test_string) == 0, "sbuf_cpy failed");
	ATF_CHECK_MSG(sbuf_cat(sb, test_whitespace_string) == 0,
	    "sbuf_cat failed");

	/* The best we can do until sbuf_finish(3) is called. */
	exp_sbuf_len = (ssize_t)(strlen(test_string) +
	    strlen(test_whitespace_string));
	test_sbuf_len = sbuf_len(sb);
	ATF_REQUIRE_MSG(exp_sbuf_len == test_sbuf_len,
	    "sbuf_len(..) => %zd (actual) != %zu (expected)",
	    test_sbuf_len, exp_sbuf_len);

	ATF_REQUIRE_MSG(sbuf_trim(sb) == 0, "sbuf_trim failed");

	ATF_REQUIRE_MSG(sbuf_finish(sb) == 0, "sbuf_finish failed: %s",
	    strerror(errno));

	ATF_REQUIRE_STREQ_MSG(sbuf_data(sb), test_string,
	    "sbuf (\"%s\") != test string (\"%s\") (trimmed)", sbuf_data(sb),
	    test_string);

	sbuf_delete(sb);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, sbuf_bcat_test);
	ATF_TP_ADD_TC(tp, sbuf_bcpy_test);
	ATF_TP_ADD_TC(tp, sbuf_cat_test);
	ATF_TP_ADD_TC(tp, sbuf_cpy_test);
	ATF_TP_ADD_TC(tp, sbuf_putc_test);
	ATF_TP_ADD_TC(tp, sbuf_trim_test);

	return (atf_no_error());
}
