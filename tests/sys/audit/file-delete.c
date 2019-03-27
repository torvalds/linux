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

#include <sys/stat.h>

#include <atf-c.h>
#include <fcntl.h>
#include <unistd.h>

#include "utils.h"

static struct pollfd fds[1];
static mode_t mode = 0777;
static int filedesc;
static const char *path = "fileforaudit";
static const char *errpath = "dirdoesnotexist/fileforaudit";
static const char *successreg = "fileforaudit.*return,success";
static const char *failurereg = "fileforaudit.*return,failure";


ATF_TC_WITH_CLEANUP(rmdir_success);
ATF_TC_HEAD(rmdir_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"rmdir(2) call");
}

ATF_TC_BODY(rmdir_success, tc)
{
	ATF_REQUIRE_EQ(0, mkdir(path, mode));
	FILE *pipefd = setup(fds, "fd");
	ATF_REQUIRE_EQ(0, rmdir(path));
	check_audit(fds, successreg, pipefd);
}

ATF_TC_CLEANUP(rmdir_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(rmdir_failure);
ATF_TC_HEAD(rmdir_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"rmdir(2) call");
}

ATF_TC_BODY(rmdir_failure, tc)
{
	FILE *pipefd = setup(fds, "fd");
	/* Failure reason: directory does not exist */
	ATF_REQUIRE_EQ(-1, rmdir(errpath));
	check_audit(fds, failurereg, pipefd);
}

ATF_TC_CLEANUP(rmdir_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(rename_success);
ATF_TC_HEAD(rename_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"rename(2) call");
}

ATF_TC_BODY(rename_success, tc)
{
	ATF_REQUIRE((filedesc = open(path, O_CREAT, mode)) != -1);
	FILE *pipefd = setup(fds, "fd");
	ATF_REQUIRE_EQ(0, rename(path, "renamed"));
	check_audit(fds, successreg, pipefd);
	close(filedesc);
}

ATF_TC_CLEANUP(rename_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(rename_failure);
ATF_TC_HEAD(rename_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"rename(2) call");
}

ATF_TC_BODY(rename_failure, tc)
{
	FILE *pipefd = setup(fds, "fd");
	/* Failure reason: file does not exist */
	ATF_REQUIRE_EQ(-1, rename(path, "renamed"));
	check_audit(fds, failurereg, pipefd);
}

ATF_TC_CLEANUP(rename_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(renameat_success);
ATF_TC_HEAD(renameat_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"renameat(2) call");
}

ATF_TC_BODY(renameat_success, tc)
{
	ATF_REQUIRE((filedesc = open(path, O_CREAT, mode)) != -1);
	FILE *pipefd = setup(fds, "fd");
	ATF_REQUIRE_EQ(0, renameat(AT_FDCWD, path, AT_FDCWD, "renamed"));
	check_audit(fds, successreg, pipefd);
	close(filedesc);
}

ATF_TC_CLEANUP(renameat_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(renameat_failure);
ATF_TC_HEAD(renameat_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"renameat(2) call");
}

ATF_TC_BODY(renameat_failure, tc)
{
	FILE *pipefd = setup(fds, "fd");
	/* Failure reason: file does not exist */
	ATF_REQUIRE_EQ(-1, renameat(AT_FDCWD, path, AT_FDCWD, "renamed"));
	check_audit(fds, failurereg, pipefd);
}

ATF_TC_CLEANUP(renameat_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(unlink_success);
ATF_TC_HEAD(unlink_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"unlink(2) call");
}

ATF_TC_BODY(unlink_success, tc)
{
	ATF_REQUIRE((filedesc = open(path, O_CREAT, mode)) != -1);
	FILE *pipefd = setup(fds, "fd");
	ATF_REQUIRE_EQ(0, unlink(path));
	check_audit(fds, successreg, pipefd);
	close(filedesc);
}

ATF_TC_CLEANUP(unlink_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(unlink_failure);
ATF_TC_HEAD(unlink_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"unlink(2) call");
}

ATF_TC_BODY(unlink_failure, tc)
{
	FILE *pipefd = setup(fds, "fd");
	/* Failure reason: file does not exist */
	ATF_REQUIRE_EQ(-1, unlink(errpath));
	check_audit(fds, failurereg, pipefd);
}

ATF_TC_CLEANUP(unlink_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(unlinkat_success);
ATF_TC_HEAD(unlinkat_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"unlinkat(2) call");
}

ATF_TC_BODY(unlinkat_success, tc)
{
	ATF_REQUIRE_EQ(0, mkdir(path, mode));
	FILE *pipefd = setup(fds, "fd");
	ATF_REQUIRE_EQ(0, unlinkat(AT_FDCWD, path, AT_REMOVEDIR));
	check_audit(fds, successreg, pipefd);
}

ATF_TC_CLEANUP(unlinkat_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(unlinkat_failure);
ATF_TC_HEAD(unlinkat_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"unlinkat(2) call");
}

ATF_TC_BODY(unlinkat_failure, tc)
{
	FILE *pipefd = setup(fds, "fd");
	/* Failure reason: directory does not exist */
	ATF_REQUIRE_EQ(-1, unlinkat(AT_FDCWD, errpath, AT_REMOVEDIR));
	check_audit(fds, failurereg, pipefd);
}

ATF_TC_CLEANUP(unlinkat_failure, tc)
{
	cleanup();
}


ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, rmdir_success);
	ATF_TP_ADD_TC(tp, rmdir_failure);

	ATF_TP_ADD_TC(tp, rename_success);
	ATF_TP_ADD_TC(tp, rename_failure);
	ATF_TP_ADD_TC(tp, renameat_success);
	ATF_TP_ADD_TC(tp, renameat_failure);

	ATF_TP_ADD_TC(tp, unlink_success);
	ATF_TP_ADD_TC(tp, unlink_failure);
	ATF_TP_ADD_TC(tp, unlinkat_success);
	ATF_TP_ADD_TC(tp, unlinkat_failure);

	return (atf_no_error());
}
