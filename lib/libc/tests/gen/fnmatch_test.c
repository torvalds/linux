/*-
 * Copyright (c) 2010 Jilles Tjoelker
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <atf-c.h>

#include "fnmatch_testcases.h"

static const char *
flags_to_string(int flags)
{
	static const int flagvalues[] = { FNM_NOESCAPE, FNM_PATHNAME,
		FNM_PERIOD, FNM_LEADING_DIR, FNM_CASEFOLD, 0 };
	static const char flagnames[] = "FNM_NOESCAPE\0FNM_PATHNAME\0FNM_PERIOD\0FNM_LEADING_DIR\0FNM_CASEFOLD\0";
	static char result[sizeof(flagnames) + 3 * sizeof(int) + 2];
	char *p;
	size_t i, len;
	const char *fp;

	p = result;
	fp = flagnames;
	for (i = 0; flagvalues[i] != 0; i++) {
		len = strlen(fp);
		if (flags & flagvalues[i]) {
			if (p != result)
				*p++ = '|';
			memcpy(p, fp, len);
			p += len;
			flags &= ~flagvalues[i];
		}
		fp += len + 1;
	}
	if (p == result)
		memcpy(p, "0", 2);
	else if (flags != 0)
		sprintf(p, "%d", flags);
	else
		*p = '\0';
	return result;
}

ATF_TC_WITHOUT_HEAD(fnmatch_test);
ATF_TC_BODY(fnmatch_test, tc)
{
	size_t i;
	int flags, result;
	struct testcase *t;

	for (i = 0; i < nitems(testcases); i++) {
		t = &testcases[i];
		flags = t->flags;
		do {
			result = fnmatch(t->pattern, t->string, flags);
			if (result != t->result)
				break;
			if (strchr(t->pattern, '\\') == NULL &&
			    !(flags & FNM_NOESCAPE)) {
				flags |= FNM_NOESCAPE;
				result = fnmatch(t->pattern, t->string, flags);
				if (result != t->result)
					break;
				flags = t->flags;
			}
			if (strchr(t->pattern, '\\') != NULL &&
			    strchr(t->string, '\\') == NULL &&
			    t->result == FNM_NOMATCH &&
			    !(flags & (FNM_NOESCAPE | FNM_LEADING_DIR))) {
				flags |= FNM_NOESCAPE;
				result = fnmatch(t->pattern, t->string, flags);
				if (result != t->result)
					break;
				flags = t->flags;
			}
			if ((t->string[0] != '.' || t->pattern[0] == '.' ||
			    t->result == FNM_NOMATCH) &&
			    !(flags & (FNM_PATHNAME | FNM_PERIOD))) {
				flags |= FNM_PERIOD;
				result = fnmatch(t->pattern, t->string, flags);
				if (result != t->result)
					break;
				flags = t->flags;
			}
			if ((strchr(t->string, '/') == NULL ||
			    t->result == FNM_NOMATCH) &&
			    !(flags & FNM_PATHNAME)) {
				flags |= FNM_PATHNAME;
				result = fnmatch(t->pattern, t->string, flags);
				if (result != t->result)
					break;
				flags = t->flags;
			}
			if ((((t->string[0] != '.' || t->pattern[0] == '.') &&
			    strstr(t->string, "/.") == NULL) ||
			    t->result == FNM_NOMATCH) &&
			    flags & FNM_PATHNAME && !(flags & FNM_PERIOD)) {
				flags |= FNM_PERIOD;
				result = fnmatch(t->pattern, t->string, flags);
				if (result != t->result)
					break;
				flags = t->flags;
			}
			if ((((t->string[0] != '.' || t->pattern[0] == '.') &&
			    strchr(t->string, '/') == NULL) ||
			    t->result == FNM_NOMATCH) &&
			    !(flags & (FNM_PATHNAME | FNM_PERIOD))) {
				flags |= FNM_PATHNAME | FNM_PERIOD;
				result = fnmatch(t->pattern, t->string, flags);
				if (result != t->result)
					break;
				flags = t->flags;
			}
			if ((strchr(t->string, '/') == NULL || t->result == 0)
			    && !(flags & FNM_LEADING_DIR)) {
				flags |= FNM_LEADING_DIR;
				result = fnmatch(t->pattern, t->string, flags);
				if (result != t->result)
					break;
				flags = t->flags;
			}
			if (t->result == 0 && !(flags & FNM_CASEFOLD)) {
				flags |= FNM_CASEFOLD;
				result = fnmatch(t->pattern, t->string, flags);
				if (result != t->result)
					break;
				flags = t->flags;
			}
			if (strchr(t->pattern, '\\') == NULL &&
			    t->result == 0 &&
			    !(flags & (FNM_NOESCAPE | FNM_CASEFOLD))) {
				flags |= FNM_NOESCAPE | FNM_CASEFOLD;
				result = fnmatch(t->pattern, t->string, flags);
				if (result != t->result)
					break;
				flags = t->flags;
			}
		} while (0);

		ATF_CHECK(result == t->result);
		if (result == t->result)
			printf("fnmatch(\"%s\", \"%s\", %s) == %d\n",
			    t->pattern, t->string, flags_to_string(flags), result);
		else
			printf("fnmatch(\"%s\", \"%s\", %s) != %d (was %d)\n",
			    t->pattern, t->string, flags_to_string(flags),
			    t->result, result);
	}

}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, fnmatch_test);

	return (atf_no_error());
}
