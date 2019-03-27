/*-
 * Copyright (c) 2012 Jilles Tjoelker
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
#include <sys/wait.h>
#include <err.h>
#include <errno.h>
#include <fmtmsg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <atf-c.h>

static char *run_test(long classification, const char *label, int severity,
    const char *text, const char *action, const char *tag);

struct testcase {
	long classification;
	const char *label;
	int severity;
	const char *text;
	const char *action;
	const char *tag;
	const char *msgverb;
	const char *result;
} testcases[] = {
	{
		MM_UTIL | MM_PRINT, "BSD:ls", MM_ERROR,
		"illegal option -- z", "refer to manual", "BSD:ls:001",
		NULL,
		"BSD:ls: ERROR: illegal option -- z\n"
		    "TO FIX: refer to manual BSD:ls:001\n"
	},
	{
		MM_UTIL | MM_PRINT, "BSD:ls", MM_ERROR,
		"illegal option -- z", "refer to manual", "BSD:ls:001",
		"text:severity:action:tag",
		"illegal option -- z: ERROR\n"
		    "TO FIX: refer to manual BSD:ls:001\n"
	},
	{
		MM_UTIL | MM_PRINT, "BSD:ls", MM_ERROR,
		"illegal option -- z", "refer to manual", "BSD:ls:001",
		"text",
		"illegal option -- z\n"
	},
	{
		MM_UTIL | MM_PRINT, "BSD:ls", MM_ERROR,
		"illegal option -- z", "refer to manual", "BSD:ls:001",
		"severity:text",
		"ERROR: illegal option -- z\n"
	},
	{
		MM_UTIL | MM_PRINT, "BSD:ls", MM_ERROR,
		"illegal option -- z", "refer to manual", "BSD:ls:001",
		"ignore me",
		"BSD:ls: ERROR: illegal option -- z\n"
		    "TO FIX: refer to manual BSD:ls:001\n"
	},
	{
		MM_UTIL | MM_PRINT, "BSD:ls", MM_ERROR,
		"illegal option -- z", "refer to manual", "BSD:ls:001",
		"tag:severity:text:nothing:action",
		"BSD:ls: ERROR: illegal option -- z\n"
		    "TO FIX: refer to manual BSD:ls:001\n"
	},
	{
		MM_UTIL | MM_PRINT, "BSD:ls", MM_ERROR,
		"illegal option -- z", "refer to manual", "BSD:ls:001",
		"",
		"BSD:ls: ERROR: illegal option -- z\n"
		    "TO FIX: refer to manual BSD:ls:001\n"
	},
	{
		MM_UTIL | MM_PRINT, MM_NULLLBL, MM_ERROR,
		"illegal option -- z", "refer to manual", "BSD:ls:001",
		NULL,
		"ERROR: illegal option -- z\n"
		    "TO FIX: refer to manual BSD:ls:001\n"
	},
	{
		MM_UTIL | MM_PRINT, "BSD:ls", MM_ERROR,
		"illegal option -- z", MM_NULLACT, MM_NULLTAG,
		NULL,
		"BSD:ls: ERROR: illegal option -- z\n"
	},
	{
		MM_UTIL | MM_NULLMC, "BSD:ls", MM_ERROR,
		"illegal option -- z", "refer to manual", "BSD:ls:001",
		NULL,
		""
	},
	{
		MM_APPL | MM_PRINT, "ABCDEFGHIJ:abcdefghijklmn", MM_INFO,
		"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
		    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
		    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
		    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
		"refer to manual", "ABCDEFGHIJ:abcdefghijklmn:001",
		NULL,
		"ABCDEFGHIJ:abcdefghijklmn: INFO: "
		    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
		    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
		    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
		    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n"
		    "TO FIX: refer to manual ABCDEFGHIJ:abcdefghijklmn:001\n"
	},
	{
		MM_OPSYS | MM_PRINT, "TEST:test", MM_HALT,
		"failed", "nothing can help me", "NOTHING",
		NULL,
		"TEST:test: HALT: failed\n"
		    "TO FIX: nothing can help me NOTHING\n"
	},
	{
		MM_OPSYS | MM_PRINT, "TEST:test", MM_WARNING,
		"failed", "nothing can help me", "NOTHING",
		NULL,
		"TEST:test: WARNING: failed\n"
		    "TO FIX: nothing can help me NOTHING\n"
	},
	{
		MM_OPSYS | MM_PRINT, "TEST:test", MM_NOSEV,
		"failed", "nothing can help me", "NOTHING",
		NULL,
		"TEST:test: failed\n"
		    "TO FIX: nothing can help me NOTHING\n"
	}
};

static char *
run_test(long classification, const char *label, int severity,
    const char *text, const char *action, const char *tag)
{
	int pip[2];
	pid_t pid, wpid;
	char *result, *p;
	size_t resultsize;
	ssize_t n;
	int status;

	if (pipe(pip) == -1)
		err(2, "pipe");
	pid = fork();
	if (pid == -1)
		err(2, "fork");
	if (pid == 0) {
		close(pip[0]);
		if (pip[1] != STDERR_FILENO &&
		    dup2(pip[1], STDERR_FILENO) == -1)
			_exit(2);
		if (fmtmsg(classification, label, severity, text, action, tag)
		    != MM_OK)
			_exit(1);
		else
			_exit(0);
	}
	close(pip[1]);
	resultsize = 1024;
	result = malloc(resultsize);
	p = result;
	while ((n = read(pip[0], p, result + resultsize - p - 1)) != 0) {
		if (n == -1) {
			if (errno == EINTR)
				continue;
			else
				err(2, "read");
		}
		p += n;
		if (result + resultsize == p - 1) {
			resultsize *= 2;
			result = realloc(result, resultsize);
			if (result == NULL)
				err(2, "realloc");
		}
	}
	if (memchr(result, '\0', p - result) != NULL) {
		free(result);
		return (NULL);
	}
	*p = '\0';
	close(pip[0]);
	while ((wpid = waitpid(pid, &status, 0)) == -1 && errno == EINTR)
		;
	if (wpid == -1)
		err(2, "waitpid");
	if (status != 0) {
		free(result);
		return (NULL);
	}
	return (result);
}

ATF_TC_WITHOUT_HEAD(fmtmsg_test);
ATF_TC_BODY(fmtmsg_test, tc)
{
	char *result;
	struct testcase *t;
	int i;

	for (i = 0; i < nitems(testcases); i++) {
		t = &testcases[i];
		if (t->msgverb != NULL)
			setenv("MSGVERB", t->msgverb, 1);
		else
			unsetenv("MSGVERB");
		result = run_test(t->classification, t->label, t->severity,
		    t->text, t->action, t->tag);
		ATF_CHECK_MSG(result != NULL, "testcase %d failed", i + 1);
		if (result != NULL)
			ATF_CHECK_MSG(strcmp(result, t->result) == 0,
			    "results for testcase %d didn't match; "
			    "`%s` != `%s`", i + 1, result, t->result);
		free(result);
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, fmtmsg_test);

	return (atf_no_error());
}
