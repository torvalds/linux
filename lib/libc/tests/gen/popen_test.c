/*-
 * Copyright (c) 2013 Jilles Tjoelker
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
 * Limited test program for popen() as specified by IEEE Std. 1003.1-2008,
 * with BSD extensions.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atf-c.h>

static volatile sig_atomic_t got_sigpipe;

static void
sigpipe_handler(int sig __unused)
{
	got_sigpipe = 1;
}

static void
check_cloexec(FILE *fp, const char *mode)
{
	int exp_flags, flags;

	flags = fcntl(fileno(fp), F_GETFD);
	ATF_CHECK_MSG(flags != -1, "fcntl(F_GETFD) failed; errno=%d", errno);
	if (flags == -1)
		return;
	if (strchr(mode, 'e') != NULL)
		exp_flags = FD_CLOEXEC;
	else
		exp_flags = 0;
	ATF_CHECK_MSG((flags & FD_CLOEXEC) == exp_flags,
	    "bad cloexec flag; %d != %d", flags, exp_flags);
}

ATF_TC_WITHOUT_HEAD(popen_all_modes_test);
ATF_TC_BODY(popen_all_modes_test, tc)
{
	FILE *fp;
	int i, status;
	const char *mode;
	const char *allmodes[] = { "r", "w", "r+", "re", "we", "r+e", "re+" };

	for (i = 0; i < nitems(allmodes); i++) {
		mode = allmodes[i];
		fp = popen("exit 7", mode);
		ATF_CHECK_MSG(fp != NULL, "popen(, \"%s\") failed", mode);
		if (fp == NULL)
			continue;
		check_cloexec(fp, mode);
		status = pclose(fp);
		ATF_CHECK_MSG(WIFEXITED(status) && WEXITSTATUS(status) == 7,
		    "bad exit status (no I/O)");
	}
}

ATF_TC_WITHOUT_HEAD(popen_rmodes_test);
ATF_TC_BODY(popen_rmodes_test, tc)
{
	FILE *fp;
	const char *rmodes[] = { "r", "r+", "re", "r+e", "re+" };
	const char *mode;
	char buf[80];
	int i, status;

	for (i = 0; i < nitems(rmodes); i++) {
		mode = rmodes[i];
		fp = popen("exit 9", mode);
		ATF_CHECK_MSG(fp != NULL, "popen(, \"%s\") failed", mode);
		if (fp == NULL)
			continue;
		check_cloexec(fp, mode);
		bool input_error_1 = !(fgetc(fp) != EOF || !feof(fp) || !ferror(fp));
		ATF_CHECK_MSG(!input_error_1, "input error 1");
		if (input_error_1)
			continue;
		status = pclose(fp);
		ATF_CHECK_MSG(WIFEXITED(status) && WEXITSTATUS(status) == 9,
		    "bad exit status (input)");
	}

	for (i = 0; i < nitems(rmodes); i++) {
		char *sres;
		mode = rmodes[i];
		fp = popen("echo hi there", mode);
		ATF_CHECK_MSG(fp != NULL, "popen(, \"%s\") failed", mode);
		if (fp == NULL)
			continue;
		check_cloexec(fp, mode);
		ATF_CHECK_MSG((sres = fgets(buf, sizeof(buf), fp)) != NULL,
		    "Input error 2");
		if (sres != NULL)
			ATF_CHECK_MSG(strcmp(buf, "hi there\n") == 0,
			    "Bad input 1");
		status = pclose(fp);
		ATF_CHECK_MSG(WIFEXITED(status) && WEXITSTATUS(status) == 0,
		    "Bad exit status (input)");
	}
}

ATF_TC_WITHOUT_HEAD(popen_wmodes_test);
ATF_TC_BODY(popen_wmodes_test, tc)
{
	FILE *fp, *fp2;
	const char *wmodes[] = { "w", "r+", "we", "r+e", "re+" };
	const char *mode;
	struct sigaction act, oact;
	int i, j, status;

	for (i = 0; i < nitems(wmodes); i++) {
		mode = wmodes[i];
		fp = popen("read x && [ \"$x\" = abcd ]", mode);
		ATF_CHECK_MSG(fp != NULL, "popen(, \"%s\") failed", mode);
		if (fp == NULL)
			continue;
		check_cloexec(fp, mode);
		ATF_CHECK_MSG(fputs("abcd\n", fp) != EOF,
		    "Output error 1");
		status = pclose(fp);
		ATF_CHECK_MSG(WIFEXITED(status) && WEXITSTATUS(status) == 0,
		    "Bad exit status (output)");
	}

	act.sa_handler = sigpipe_handler;
	act.sa_flags = SA_RESTART;
	sigemptyset(&act.sa_mask);
	ATF_CHECK_MSG(sigaction(SIGPIPE, &act, &oact) != -1,
	    "sigaction() failed");
	for (i = 0; i < nitems(wmodes); i++) {
		mode = wmodes[i];
		fp = popen("exit 88", mode);
		ATF_CHECK_MSG(fp != NULL, "popen(, \"%s\") failed", mode);
		if (fp == NULL)
			continue;
		check_cloexec(fp, mode);
		got_sigpipe = 0;
		while (fputs("abcd\n", fp) != EOF)
			;
		ATF_CHECK_MSG(ferror(fp) && errno == EPIPE, "Expected EPIPE");
		ATF_CHECK_MSG(got_sigpipe, "Expected SIGPIPE");
		status = pclose(fp);
		ATF_CHECK_MSG(WIFEXITED(status) && WEXITSTATUS(status) == 88,
		    "Bad exit status (EPIPE)");
	}
	ATF_CHECK_MSG(sigaction(SIGPIPE, &oact, NULL) != -1,
	    "sigaction() failed");

	for (i = 0; i < nitems(wmodes); i++) {
		for (j = 0; j < nitems(wmodes); j++) {
			mode = wmodes[i];
			fp = popen("read x", mode);
			ATF_CHECK_MSG(fp != NULL,
			    "popen(, \"%s\") failed", mode);
			if (fp == NULL)
				continue;
			mode = wmodes[j];
			fp2 = popen("read x", mode);
			ATF_CHECK_MSG(fp2 != NULL,
			    "popen(, \"%s\") failed", mode);
			if (fp2 == NULL) {
				pclose(fp);
				continue;
			}
			/* If fp2 inherits fp's pipe, we will deadlock here. */
			status = pclose(fp);
			ATF_CHECK_MSG(WIFEXITED(status) && WEXITSTATUS(status) == 1,
			    "bad exit status (2 pipes)");
			status = pclose(fp2);
			ATF_CHECK_MSG(WIFEXITED(status) && WEXITSTATUS(status) == 1,
			    "bad exit status (2 pipes)");
		}
	}
}

ATF_TC_WITHOUT_HEAD(popen_rwmodes_test);
ATF_TC_BODY(popen_rwmodes_test, tc)
{
	const char *rwmodes[] = { "r+", "r+e", "re+" };
	FILE *fp;
	const char *mode;
	char *sres;
	char buf[80];
	int i, ires, status;

	for (i = 0; i < nitems(rwmodes); i++) {
		mode = rwmodes[i];
		fp = popen("read x && printf '%s\\n' \"Q${x#a}\"", mode);
		ATF_CHECK_MSG(fp != NULL, "popen(, \"%s\") failed", mode);
		if (fp == NULL)
			continue;
		check_cloexec(fp, mode);
		ATF_CHECK_MSG((ires = fputs("abcd\n", fp)) != EOF,
		    "Output error 2");
		if (ires != EOF) {
			sres = fgets(buf, sizeof(buf), fp);
			ATF_CHECK_MSG(sres != NULL, "Input error 3");
			if (sres != NULL)
				ATF_CHECK_MSG(strcmp(buf, "Qbcd\n") == 0,
				    "Bad input 2");
		}
		status = pclose(fp);
		ATF_CHECK_MSG(WIFEXITED(status) && WEXITSTATUS(status) == 0,
		    "bad exit status (I/O)");
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, popen_all_modes_test);
	ATF_TP_ADD_TC(tp, popen_rmodes_test);
	ATF_TP_ADD_TC(tp, popen_wmodes_test);
	ATF_TP_ADD_TC(tp, popen_rwmodes_test);

	return (atf_no_error());
}
