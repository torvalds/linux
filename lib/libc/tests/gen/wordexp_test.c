/*-
 * Copyright (c) 2003 Tim J. Robbins
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

/*
 * Test program for wordexp() and wordfree() as specified by
 * IEEE Std. 1003.1-2001.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wordexp.h>

#include <atf-c.h>

static void
chld_handler(int x)
{
	int status, serrno;

	(void)x;
	serrno = errno;
	while (waitpid(-1, &status, WNOHANG) > 0)
		;
	errno = serrno;
}

ATF_TC_WITHOUT_HEAD(simple_test);
ATF_TC_BODY(simple_test, tc)
{
	wordexp_t we;
	int r;

	/* Test that the macros are there. */
	(void)(WRDE_APPEND + WRDE_DOOFFS + WRDE_NOCMD + WRDE_REUSE +
	    WRDE_SHOWERR + WRDE_UNDEF);
	(void)(WRDE_BADCHAR + WRDE_BADVAL + WRDE_CMDSUB + WRDE_NOSPACE +
	    WRDE_SYNTAX);

	/* Simple test. */
	r = wordexp("hello world", &we, 0);
	ATF_REQUIRE(r == 0);
	ATF_REQUIRE(we.we_wordc == 2);
	ATF_REQUIRE(strcmp(we.we_wordv[0], "hello") == 0);
	ATF_REQUIRE(strcmp(we.we_wordv[1], "world") == 0);
	ATF_REQUIRE(we.we_wordv[2] == NULL);
	wordfree(&we);
}

ATF_TC_WITHOUT_HEAD(long_output_test);
ATF_TC_BODY(long_output_test, tc)
{
	char longdata[6 * 10000 + 1];
	wordexp_t we;
	int i, r;

	/* Long output. */
	for (i = 0; i < 10000; i++)
		snprintf(longdata + 6 * i, 7, "%05d ", i);
	r = wordexp(longdata, &we, 0);
	ATF_REQUIRE(r == 0);
	ATF_REQUIRE(we.we_wordc == 10000);
	ATF_REQUIRE(we.we_wordv[10000] == NULL);
	wordfree(&we);
}

ATF_TC_WITHOUT_HEAD(WRDE_DOOFFS_test);
ATF_TC_BODY(WRDE_DOOFFS_test, tc)
{
	wordexp_t we;
	int r;

	we.we_offs = 3;
	r = wordexp("hello world", &we, WRDE_DOOFFS);
	ATF_REQUIRE(r == 0);
	ATF_REQUIRE(we.we_wordc == 2);
	ATF_REQUIRE(we.we_wordv[0] == NULL);
	ATF_REQUIRE(we.we_wordv[1] == NULL);
	ATF_REQUIRE(we.we_wordv[2] == NULL);
	ATF_REQUIRE(strcmp(we.we_wordv[3], "hello") == 0);
	ATF_REQUIRE(strcmp(we.we_wordv[4], "world") == 0);
	ATF_REQUIRE(we.we_wordv[5] == NULL);
	wordfree(&we);
}

ATF_TC_WITHOUT_HEAD(WRDE_REUSE_test);
ATF_TC_BODY(WRDE_REUSE_test, tc)
{
	wordexp_t we;
	int r;

	r = wordexp("hello world", &we, 0);
	r = wordexp("hello world", &we, WRDE_REUSE);
	ATF_REQUIRE(r == 0);
	ATF_REQUIRE(we.we_wordc == 2);
	ATF_REQUIRE(strcmp(we.we_wordv[0], "hello") == 0);
	ATF_REQUIRE(strcmp(we.we_wordv[1], "world") == 0);
	ATF_REQUIRE(we.we_wordv[2] == NULL);
	wordfree(&we);
}

ATF_TC_WITHOUT_HEAD(WRDE_APPEND_test);
ATF_TC_BODY(WRDE_APPEND_test, tc)
{
	wordexp_t we;
	int r;

	r = wordexp("this is", &we, 0);
	ATF_REQUIRE(r == 0);
	r = wordexp("a test", &we, WRDE_APPEND);
	ATF_REQUIRE(r == 0);
	ATF_REQUIRE(we.we_wordc == 4);
	ATF_REQUIRE(strcmp(we.we_wordv[0], "this") == 0);
	ATF_REQUIRE(strcmp(we.we_wordv[1], "is") == 0);
	ATF_REQUIRE(strcmp(we.we_wordv[2], "a") == 0);
	ATF_REQUIRE(strcmp(we.we_wordv[3], "test") == 0);
	ATF_REQUIRE(we.we_wordv[4] == NULL);
	wordfree(&we);
}

ATF_TC_WITHOUT_HEAD(WRDE_DOOFFS__WRDE_APPEND_test);
ATF_TC_BODY(WRDE_DOOFFS__WRDE_APPEND_test, tc)
{
	wordexp_t we;
	int r;

	we.we_offs = 2;
	r = wordexp("this is", &we, WRDE_DOOFFS);
	ATF_REQUIRE(r == 0);
	r = wordexp("a test", &we, WRDE_APPEND|WRDE_DOOFFS);
	ATF_REQUIRE(r == 0);
	r = wordexp("of wordexp", &we, WRDE_APPEND|WRDE_DOOFFS);
	ATF_REQUIRE(r == 0);
	ATF_REQUIRE(we.we_wordc == 6);
	ATF_REQUIRE(we.we_wordv[0] == NULL);
	ATF_REQUIRE(we.we_wordv[1] == NULL);
	ATF_REQUIRE(strcmp(we.we_wordv[2], "this") == 0);
	ATF_REQUIRE(strcmp(we.we_wordv[3], "is") == 0);
	ATF_REQUIRE(strcmp(we.we_wordv[4], "a") == 0);
	ATF_REQUIRE(strcmp(we.we_wordv[5], "test") == 0);
	ATF_REQUIRE(strcmp(we.we_wordv[6], "of") == 0);
	ATF_REQUIRE(strcmp(we.we_wordv[7], "wordexp") == 0);
	ATF_REQUIRE(we.we_wordv[8] == NULL);
	wordfree(&we);
}

ATF_TC_WITHOUT_HEAD(WRDE_UNDEF_test);
ATF_TC_BODY(WRDE_UNDEF_test, tc)
{
	wordexp_t we;
	int r;

	r = wordexp("${dont_set_me}", &we, WRDE_UNDEF);
	ATF_REQUIRE(r == WRDE_BADVAL);
}

ATF_TC_WITHOUT_HEAD(WRDE_NOCMD_test);
ATF_TC_BODY(WRDE_NOCMD_test, tc)
{
	wordexp_t we;
	int r;

	r = wordexp("`date`", &we, WRDE_NOCMD);
	ATF_REQUIRE(r == WRDE_CMDSUB);
	r = wordexp("\"`date`\"", &we, WRDE_NOCMD);
	ATF_REQUIRE(r == WRDE_CMDSUB);
	r = wordexp("$(date)", &we, WRDE_NOCMD);
	ATF_REQUIRE(r == WRDE_CMDSUB);
	r = wordexp("\"$(date)\"", &we, WRDE_NOCMD);
	ATF_REQUIRE(r == WRDE_CMDSUB);
	r = wordexp("$((3+5))", &we, WRDE_NOCMD);
	ATF_REQUIRE(r == 0);
	r = wordexp("\\$\\(date\\)", &we, WRDE_NOCMD|WRDE_REUSE);
	ATF_REQUIRE(r == 0);
	r = wordexp("'`date`'", &we, WRDE_NOCMD|WRDE_REUSE);
	ATF_REQUIRE(r == 0);
	r = wordexp("'$(date)'", &we, WRDE_NOCMD|WRDE_REUSE);
	ATF_REQUIRE(r == 0);
	wordfree(&we);
}

ATF_TC_WITHOUT_HEAD(WRDE_BADCHAR_test);
ATF_TC_BODY(WRDE_BADCHAR_test, tc)
{
	wordexp_t we;
	int r;

	r = wordexp("'\n|&;<>(){}'", &we, 0);
	ATF_REQUIRE(r == 0);
	r = wordexp("\"\n|&;<>(){}\"", &we, WRDE_REUSE);
	ATF_REQUIRE(r == 0);
	r = wordexp("\\\n\\|\\&\\;\\<\\>\\(\\)\\{\\}", &we, WRDE_REUSE);
	ATF_REQUIRE(r == 0);
	wordfree(&we);
	r = wordexp("test \n test", &we, 0);
	ATF_REQUIRE(r == WRDE_BADCHAR);
	r = wordexp("test | test", &we, 0);
	ATF_REQUIRE(r == WRDE_BADCHAR);
	r = wordexp("test & test", &we, 0);
	ATF_REQUIRE(r == WRDE_BADCHAR);
	r = wordexp("test ; test", &we, 0);
	ATF_REQUIRE(r == WRDE_BADCHAR);
	r = wordexp("test > test", &we, 0);
	ATF_REQUIRE(r == WRDE_BADCHAR);
	r = wordexp("test < test", &we, 0);
	ATF_REQUIRE(r == WRDE_BADCHAR);
	r = wordexp("test ( test", &we, 0);
	ATF_REQUIRE(r == WRDE_BADCHAR);
	r = wordexp("test ) test", &we, 0);
	ATF_REQUIRE(r == WRDE_BADCHAR);
	r = wordexp("test { test", &we, 0);
	ATF_REQUIRE(r == WRDE_BADCHAR);
	r = wordexp("test } test", &we, 0);
	ATF_REQUIRE(r == WRDE_BADCHAR);
}

ATF_TC_WITHOUT_HEAD(WRDE_SYNTAX_test);
ATF_TC_BODY(WRDE_SYNTAX_test, tc)
{
	wordexp_t we;
	int r;

	r = wordexp("'", &we, 0);
	ATF_REQUIRE(r == WRDE_SYNTAX);
	r = wordexp("'", &we, WRDE_UNDEF);
	ATF_REQUIRE(r == WRDE_SYNTAX);
	r = wordexp("'\\'", &we, 0);
	ATF_REQUIRE(r == 0);
	ATF_REQUIRE(we.we_wordc == 1);
	ATF_REQUIRE(strcmp(we.we_wordv[0], "\\") == 0);
	ATF_REQUIRE(we.we_wordv[1] == NULL);
	wordfree(&we);
	/* Two syntax errors that are not detected by the current we_check(). */
	r = wordexp("${IFS:+'}", &we, 0);
	ATF_REQUIRE(r == WRDE_SYNTAX);
	r = wordexp("${IFS:+'}", &we, WRDE_UNDEF);
	ATF_REQUIRE(r == WRDE_SYNTAX);
	r = wordexp("$(case)", &we, 0);
	ATF_REQUIRE(r == WRDE_SYNTAX);
	r = wordexp("$(case)", &we, WRDE_UNDEF);
	ATF_REQUIRE(r == WRDE_SYNTAX);
}

ATF_TC_WITHOUT_HEAD(with_SIGCHILD_handler_test);
ATF_TC_BODY(with_SIGCHILD_handler_test, tc)
{
	struct sigaction sa;
	wordexp_t we;
	int r;

	/* With a SIGCHLD handler that reaps all zombies. */
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = chld_handler;
	r = sigaction(SIGCHLD, &sa, NULL);
	ATF_REQUIRE(r == 0);
	r = wordexp("hello world", &we, 0);
	ATF_REQUIRE(r == 0);
	ATF_REQUIRE(we.we_wordc == 2);
	ATF_REQUIRE(strcmp(we.we_wordv[0], "hello") == 0);
	ATF_REQUIRE(strcmp(we.we_wordv[1], "world") == 0);
	ATF_REQUIRE(we.we_wordv[2] == NULL);
	wordfree(&we);
	sa.sa_handler = SIG_DFL;
	r = sigaction(SIGCHLD, &sa, NULL);
	ATF_REQUIRE(r == 0);
}

ATF_TC_WITHOUT_HEAD(with_unused_non_default_IFS_test);
ATF_TC_BODY(with_unused_non_default_IFS_test, tc)
{
	wordexp_t we;
	int r;

	/*
	 * With IFS set to a non-default value (without depending on whether
	 * IFS is inherited or not).
	 */
	r = setenv("IFS", ":", 1);
	ATF_REQUIRE(r == 0);
	r = wordexp("hello world", &we, 0);
	ATF_REQUIRE(r == 0);
	ATF_REQUIRE(we.we_wordc == 2);
	ATF_REQUIRE(strcmp(we.we_wordv[0], "hello") == 0);
	ATF_REQUIRE(strcmp(we.we_wordv[1], "world") == 0);
	ATF_REQUIRE(we.we_wordv[2] == NULL);
	wordfree(&we);
	r = unsetenv("IFS");
	ATF_REQUIRE(r == 0);
}

ATF_TC_WITHOUT_HEAD(with_used_non_default_IFS_test);
ATF_TC_BODY(with_used_non_default_IFS_test, tc)
{
	wordexp_t we;
	int r;

	/*
	 * With IFS set to a non-default value, and using it.
	 */
	r = setenv("IFS", ":", 1);
	ATF_REQUIRE(r == 0);
	r = wordexp("${IFS+hello:world}", &we, 0);
	ATF_REQUIRE(r == 0);
	ATF_REQUIRE(we.we_wordc == 2);
	ATF_REQUIRE(strcmp(we.we_wordv[0], "hello") == 0);
	ATF_REQUIRE(strcmp(we.we_wordv[1], "world") == 0);
	ATF_REQUIRE(we.we_wordv[2] == NULL);
	wordfree(&we);
	r = unsetenv("IFS");
	ATF_REQUIRE(r == 0);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, simple_test);
	ATF_TP_ADD_TC(tp, long_output_test);
	ATF_TP_ADD_TC(tp, WRDE_DOOFFS_test);
	ATF_TP_ADD_TC(tp, WRDE_REUSE_test);
	ATF_TP_ADD_TC(tp, WRDE_APPEND_test);
	ATF_TP_ADD_TC(tp, WRDE_DOOFFS__WRDE_APPEND_test);
	ATF_TP_ADD_TC(tp, WRDE_UNDEF_test);
	ATF_TP_ADD_TC(tp, WRDE_NOCMD_test);
	ATF_TP_ADD_TC(tp, WRDE_BADCHAR_test);
	ATF_TP_ADD_TC(tp, WRDE_SYNTAX_test);
	ATF_TP_ADD_TC(tp, with_SIGCHILD_handler_test);
	ATF_TP_ADD_TC(tp, with_unused_non_default_IFS_test);
	ATF_TP_ADD_TC(tp, with_used_non_default_IFS_test);

	return (atf_no_error());
}
