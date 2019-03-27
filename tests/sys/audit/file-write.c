/*-
 * Copyright 2018 Aniket Pandey
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
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <atf-c.h>
#include <fcntl.h>

#include "utils.h"

static struct pollfd fds[1];
static mode_t mode = 0777;
static int filedesc;
static off_t offlen = 0;
static const char *path = "fileforaudit";
static const char *errpath = "dirdoesnotexist/fileforaudit";
static const char *successreg = "fileforaudit.*return,success";
static const char *failurereg = "fileforaudit.*return,failure";


ATF_TC_WITH_CLEANUP(truncate_success);
ATF_TC_HEAD(truncate_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"truncate(2) call");
}

ATF_TC_BODY(truncate_success, tc)
{
	/* File needs to exist to call truncate(2) */
	ATF_REQUIRE((filedesc = open(path, O_CREAT, mode)) != -1);
	FILE *pipefd = setup(fds, "fw");
	ATF_REQUIRE_EQ(0, truncate(path, offlen));
	check_audit(fds, successreg, pipefd);
	close(filedesc);
}

ATF_TC_CLEANUP(truncate_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(truncate_failure);
ATF_TC_HEAD(truncate_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"truncate(2) call");
}

ATF_TC_BODY(truncate_failure, tc)
{
	FILE *pipefd = setup(fds, "fw");
	/* Failure reason: file does not exist */
	ATF_REQUIRE_EQ(-1, truncate(errpath, offlen));
	check_audit(fds, failurereg, pipefd);
}

ATF_TC_CLEANUP(truncate_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(ftruncate_success);
ATF_TC_HEAD(ftruncate_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"ftruncate(2) call");
}

ATF_TC_BODY(ftruncate_success, tc)
{
	const char *regex = "ftruncate.*return,success";
	/* Valid file descriptor needs to exist to call ftruncate(2) */
	ATF_REQUIRE((filedesc = open(path, O_CREAT | O_RDWR)) != -1);
	FILE *pipefd = setup(fds, "fw");
	ATF_REQUIRE_EQ(0, ftruncate(filedesc, offlen));
	check_audit(fds, regex, pipefd);
	close(filedesc);
}

ATF_TC_CLEANUP(ftruncate_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(ftruncate_failure);
ATF_TC_HEAD(ftruncate_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"ftruncate(2) call");
}

ATF_TC_BODY(ftruncate_failure, tc)
{
	const char *regex = "ftruncate.*return,failure";
	FILE *pipefd = setup(fds, "fw");
	/* Failure reason: bad file descriptor */
	ATF_REQUIRE_EQ(-1, ftruncate(-1, offlen));
	check_audit(fds, regex, pipefd);
}

ATF_TC_CLEANUP(ftruncate_failure, tc)
{
	cleanup();
}


ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, truncate_success);
	ATF_TP_ADD_TC(tp, truncate_failure);
	ATF_TP_ADD_TC(tp, ftruncate_success);
	ATF_TP_ADD_TC(tp, ftruncate_failure);

	return (atf_no_error());
}
