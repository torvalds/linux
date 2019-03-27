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

#define	MESSAGE_FORMAT	"message: %s\n"
#define	MESSAGE_SEPARATOR	';'

static int
sbuf_vprintf_helper(struct sbuf *sb, const char * restrict format, ...)
{
	va_list ap;
	int rc;

	va_start(ap, format);

	rc = sbuf_vprintf(sb, format, ap);

	va_end(ap);

	return (rc);
}

ATF_TC_WITHOUT_HEAD(sbuf_printf_test);
ATF_TC_BODY(sbuf_printf_test, tc)
{
	struct sbuf *sb;
	char *test_string_tmp;

	asprintf(&test_string_tmp, "%s%c" MESSAGE_FORMAT,
	    test_string, MESSAGE_SEPARATOR, test_string);
	ATF_REQUIRE_MSG(test_string_tmp != NULL, "asprintf failed");

	sb = sbuf_new_auto();
	ATF_REQUIRE_MSG(sb != NULL, "sbuf_new_auto failed: %s",
	    strerror(errno));

	ATF_REQUIRE_MSG(sbuf_cat(sb, test_string) == 0, "sbuf_cat failed");
	ATF_REQUIRE_MSG(sbuf_putc(sb, MESSAGE_SEPARATOR) == 0,
	    "sbuf_putc failed");

	ATF_REQUIRE_MSG(sbuf_printf(sb, MESSAGE_FORMAT, test_string) == 0,
	    "sbuf_printf failed");

	ATF_REQUIRE_MSG(sbuf_finish(sb) == 0, "sbuf_finish failed: %s",
	    strerror(errno));

	ATF_REQUIRE_STREQ_MSG(sbuf_data(sb), test_string_tmp,
	    "sbuf (\"%s\") != test string (\"%s\")", sbuf_data(sb),
	    test_string_tmp);

	sbuf_delete(sb);

	free(test_string_tmp);
}

ATF_TC_WITHOUT_HEAD(sbuf_putbuf_test);
ATF_TC_BODY(sbuf_putbuf_test, tc)
{
	struct sbuf *sb;
	pid_t child_proc;

	sb = sbuf_new_auto();
	ATF_REQUIRE_MSG(sb != NULL, "sbuf_new_auto failed: %s",
	    strerror(errno));

	ATF_REQUIRE_MSG(sbuf_cat(sb, test_string) == 0, "sbuf_cat failed");

	child_proc = atf_utils_fork();
	if (child_proc == 0) {
		sbuf_putbuf(sb);
		exit(0);
	}
	atf_utils_wait(child_proc, 0, test_string, "");

	ATF_REQUIRE_MSG(sbuf_finish(sb) == 0, "sbuf_finish failed: %s",
	    strerror(errno));

	sbuf_delete(sb);
}

ATF_TC_WITHOUT_HEAD(sbuf_vprintf_test);
ATF_TC_BODY(sbuf_vprintf_test, tc)
{
	struct sbuf *sb;
	char *test_string_tmp;
	int rc;

	asprintf(&test_string_tmp, "%s%c" MESSAGE_FORMAT,
	    test_string, MESSAGE_SEPARATOR, test_string);
	ATF_REQUIRE_MSG(test_string_tmp != NULL, "asprintf failed");

	sb = sbuf_new_auto();
	ATF_REQUIRE_MSG(sb != NULL, "sbuf_new_auto failed: %s",
	    strerror(errno));

	ATF_REQUIRE_MSG(sbuf_cat(sb, test_string) == 0, "sbuf_cat failed");
	ATF_REQUIRE_MSG(sbuf_putc(sb, MESSAGE_SEPARATOR) == 0,
	    "sbuf_putc failed");

	rc = sbuf_vprintf_helper(sb, MESSAGE_FORMAT, test_string);
	ATF_REQUIRE_MSG(rc == 0, "sbuf_vprintf failed");

	ATF_REQUIRE_MSG(sbuf_finish(sb) == 0, "sbuf_finish failed: %s",
	    strerror(errno));

	ATF_REQUIRE_STREQ_MSG(sbuf_data(sb), test_string_tmp,
	    "sbuf (\"%s\") != test string (\"%s\")", sbuf_data(sb),
	    test_string_tmp);

	sbuf_delete(sb);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, sbuf_printf_test);
	ATF_TP_ADD_TC(tp, sbuf_putbuf_test);
	ATF_TP_ADD_TC(tp, sbuf_vprintf_test);

	return (atf_no_error());
}
