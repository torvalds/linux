/*-
 * Copyright (c) 2017 Cyril S. E. Schubert
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
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
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

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
 
#include <atf-c.h>

static errno_t error_code;
static const char * message;

void
h(const char * msg, void * ptr __unused, errno_t error)
{
	error_code = error;
	message = msg;
}

/* null ptr */
ATF_TC_WITHOUT_HEAD(null_ptr);
ATF_TC_BODY(null_ptr, tc)
{
	ATF_CHECK_MSG(gets_s(NULL, 1) == NULL,
		"gets_s() failed to handle NULL pointer");
}

/* normal */
ATF_TC_WITHOUT_HEAD(normal);
ATF_TC_BODY(normal, tc)
{
	pid_t	kidpid;
	int	fd[2];
	int	nfd;

	// close(STDIN_FILENO);
	// close(STDOUT_FILENO);
	pipe(fd);

	if ((kidpid = fork()) == 0) {
		char	b[10];

		close(fd[1]);
		nfd = dup2(fd[0], 0);
		close(fd[0]);
		stdin = fdopen(nfd, "r");
		ATF_CHECK_MSG(gets_s(b, sizeof(b)) == 0, "gets_s() normal failed");
		fclose(stdin);
	} else {
		int stat;

		close(fd[0]);
		stdout = fdopen(fd[1], "w");
		puts("a sting");
		fclose(stdout);
		(void) waitpid(kidpid, &stat, WEXITED);
	}
}

/* n > rmax */
ATF_TC_WITHOUT_HEAD(n_gt_rmax);
ATF_TC_BODY(n_gt_rmax, tc)
{
	char b;

	ATF_CHECK_MSG(gets_s(&b, RSIZE_MAX + 1) == NULL,
		"gets_s() n > RSIZE_MAX");
}

/* n == 0 */
ATF_TC_WITHOUT_HEAD(n_eq_zero);
ATF_TC_BODY(n_eq_zero, tc)
{
	char b;

	ATF_CHECK_MSG(gets_s(&b, 0) == NULL, "gets_s() n is zero");
}

/* n > rmax, handler */
ATF_TC_WITHOUT_HEAD(n_gt_rmax_handler);
ATF_TC_BODY(n_gt_rmax_handler, tc)
{
	char b;

	error_code = 0;
	message = NULL;
	set_constraint_handler_s(h);
	ATF_CHECK_MSG(gets_s(&b, RSIZE_MAX + 1) == NULL, "gets_s() n > RSIZE_MAX");
	ATF_CHECK_MSG(error_code > 0, "gets_s() error code is %d", error_code);
	ATF_CHECK_MSG(strcmp(message, "gets_s : n > RSIZE_MAX") == 0, "gets_s(): incorrect error message");
}

/* n == 0, handler */
ATF_TC_WITHOUT_HEAD(n_eq_zero_handler);
ATF_TC_BODY(n_eq_zero_handler, tc)
{
	char b;

	error_code = 0;
	message = NULL;
	set_constraint_handler_s(h);
	ATF_CHECK(gets_s(&b, 0) == NULL);
	ATF_CHECK_MSG(error_code > 0, "gets_s() error code is %d", error_code);
	ATF_CHECK_MSG(strcmp(message, "gets_s : n == 0") == 0, "gets_s(): incorrect error message");
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, null_ptr);
	ATF_TP_ADD_TC(tp, normal);
	ATF_TP_ADD_TC(tp, n_gt_rmax);
	ATF_TP_ADD_TC(tp, n_eq_zero);
	ATF_TP_ADD_TC(tp, n_gt_rmax_handler);
	ATF_TP_ADD_TC(tp, n_eq_zero_handler);
	return (atf_no_error());
}
