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

#include <sys/types.h>
#include <sys/stat.h>

#include <atf-c.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "utils.h"

static struct pollfd fds[1];
static mode_t mode = 0777;
static int filedesc;
static dev_t dev =  0;
static const char *auclass = "fc";
static const char *path = "fileforaudit";
static const char *successreg = "fileforaudit.*return,success";
static const char *failurereg = "fileforaudit.*return,failure";


ATF_TC_WITH_CLEANUP(mkdir_success);
ATF_TC_HEAD(mkdir_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"mkdir(2) call");
}

ATF_TC_BODY(mkdir_success, tc)
{
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, mkdir(path, mode));
	check_audit(fds, successreg, pipefd);
}

ATF_TC_CLEANUP(mkdir_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(mkdir_failure);
ATF_TC_HEAD(mkdir_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"mkdir(2) call");
}

ATF_TC_BODY(mkdir_failure, tc)
{
	ATF_REQUIRE_EQ(0, mkdir(path, mode));
	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: directory already exists */
	ATF_REQUIRE_EQ(-1, mkdir(path, mode));
	check_audit(fds, failurereg, pipefd);
}

ATF_TC_CLEANUP(mkdir_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(mkdirat_success);
ATF_TC_HEAD(mkdirat_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"mkdirat(2) call");
}

ATF_TC_BODY(mkdirat_success, tc)
{
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, mkdirat(AT_FDCWD, path, mode));
	check_audit(fds, successreg, pipefd);
}

ATF_TC_CLEANUP(mkdirat_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(mkdirat_failure);
ATF_TC_HEAD(mkdirat_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"mkdirat(2) call");
}

ATF_TC_BODY(mkdirat_failure, tc)
{
	ATF_REQUIRE_EQ(0, mkdirat(AT_FDCWD, path, mode));
	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: directory already exists */
	ATF_REQUIRE_EQ(-1, mkdirat(AT_FDCWD, path, mode));
	check_audit(fds, failurereg, pipefd);
}

ATF_TC_CLEANUP(mkdirat_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(mkfifo_success);
ATF_TC_HEAD(mkfifo_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"mkfifo(2) call");
}

ATF_TC_BODY(mkfifo_success, tc)
{
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, mkfifo(path, mode));
	check_audit(fds, successreg, pipefd);
}

ATF_TC_CLEANUP(mkfifo_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(mkfifo_failure);
ATF_TC_HEAD(mkfifo_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"mkfifo(2) call");
}

ATF_TC_BODY(mkfifo_failure, tc)
{
	ATF_REQUIRE_EQ(0, mkfifo(path, mode));
	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: FIFO already exists */
	ATF_REQUIRE_EQ(-1, mkfifo(path, mode));
	check_audit(fds, failurereg, pipefd);
}

ATF_TC_CLEANUP(mkfifo_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(mkfifoat_success);
ATF_TC_HEAD(mkfifoat_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"mkfifoat(2) call");
}

ATF_TC_BODY(mkfifoat_success, tc)
{
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, mkfifoat(AT_FDCWD, path, mode));
	check_audit(fds, successreg, pipefd);
}

ATF_TC_CLEANUP(mkfifoat_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(mkfifoat_failure);
ATF_TC_HEAD(mkfifoat_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"mkfifoat(2) call");
}

ATF_TC_BODY(mkfifoat_failure, tc)
{
	ATF_REQUIRE_EQ(0, mkfifoat(AT_FDCWD, path, mode));
	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: FIFO already exists */
	ATF_REQUIRE_EQ(-1, mkfifoat(AT_FDCWD, path, mode));
	check_audit(fds, failurereg, pipefd);
}

ATF_TC_CLEANUP(mkfifoat_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(mknod_success);
ATF_TC_HEAD(mknod_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"mknod(2) call");
}

ATF_TC_BODY(mknod_success, tc)
{
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, mknod(path, S_IFIFO | S_IRWXO, dev));
	check_audit(fds, successreg, pipefd);
}

ATF_TC_CLEANUP(mknod_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(mknod_failure);
ATF_TC_HEAD(mknod_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"mknod(2) call");
}

ATF_TC_BODY(mknod_failure, tc)
{
	ATF_REQUIRE_EQ(0, mknod(path, S_IFIFO | S_IRWXO, dev));
	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: FIFO node already exists */
	ATF_REQUIRE_EQ(-1, mknod(path, S_IFIFO | S_IRWXO, dev));
	check_audit(fds, failurereg, pipefd);
}

ATF_TC_CLEANUP(mknod_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(mknodat_success);
ATF_TC_HEAD(mknodat_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"mknodat(2) call");
}

ATF_TC_BODY(mknodat_success, tc)
{
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, mknodat(AT_FDCWD, path, S_IFIFO | S_IRWXO, dev));
	check_audit(fds, successreg, pipefd);
}

ATF_TC_CLEANUP(mknodat_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(mknodat_failure);
ATF_TC_HEAD(mknodat_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"mknodat(2) call");
}

ATF_TC_BODY(mknodat_failure, tc)
{
	ATF_REQUIRE_EQ(0, mknodat(AT_FDCWD, path, S_IFIFO | S_IRWXO, dev));
	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: FIFO node already exists */
	ATF_REQUIRE_EQ(-1, mknodat(AT_FDCWD, path, S_IFIFO | S_IRWXO, dev));
	check_audit(fds, failurereg, pipefd);
}

ATF_TC_CLEANUP(mknodat_failure, tc)
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
	FILE *pipefd = setup(fds, auclass);
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
	FILE *pipefd = setup(fds, auclass);
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
	FILE *pipefd = setup(fds, auclass);
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
	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: file does not exist */
	ATF_REQUIRE_EQ(-1, renameat(AT_FDCWD, path, AT_FDCWD, "renamed"));
	check_audit(fds, failurereg, pipefd);
}

ATF_TC_CLEANUP(renameat_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(link_success);
ATF_TC_HEAD(link_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"link(2) call");
}

ATF_TC_BODY(link_success, tc)
{
	ATF_REQUIRE((filedesc = open(path, O_CREAT, mode)) != -1);
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, link(path, "hardlink"));
	check_audit(fds, successreg, pipefd);
	close(filedesc);
}

ATF_TC_CLEANUP(link_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(link_failure);
ATF_TC_HEAD(link_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"link(2) call");
}

ATF_TC_BODY(link_failure, tc)
{
	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: file does not exist */
	ATF_REQUIRE_EQ(-1, link(path, "hardlink"));
	check_audit(fds, failurereg, pipefd);
}

ATF_TC_CLEANUP(link_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(linkat_success);
ATF_TC_HEAD(linkat_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"linkat(2) call");
}

ATF_TC_BODY(linkat_success, tc)
{
	ATF_REQUIRE((filedesc = open(path, O_CREAT, mode)) != -1);
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, linkat(AT_FDCWD, path, AT_FDCWD, "hardlink", 0));
	check_audit(fds, successreg, pipefd);
	close(filedesc);
}

ATF_TC_CLEANUP(linkat_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(linkat_failure);
ATF_TC_HEAD(linkat_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"linkat(2) call");
}

ATF_TC_BODY(linkat_failure, tc)
{
	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: file does not exist */
	ATF_REQUIRE_EQ(-1, linkat(AT_FDCWD, path, AT_FDCWD, "hardlink", 0));
	check_audit(fds, failurereg, pipefd);
}

ATF_TC_CLEANUP(linkat_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(symlink_success);
ATF_TC_HEAD(symlink_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"symlink(2) call");
}

ATF_TC_BODY(symlink_success, tc)
{
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, symlink(path, "symlink"));
	check_audit(fds, successreg, pipefd);
}

ATF_TC_CLEANUP(symlink_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(symlink_failure);
ATF_TC_HEAD(symlink_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"symlink(2) call");
}

ATF_TC_BODY(symlink_failure, tc)
{
	ATF_REQUIRE_EQ(0, symlink(path, "symlink"));
	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: symbolic link already exists */
	ATF_REQUIRE_EQ(-1, symlink(path, "symlink"));
	check_audit(fds, failurereg, pipefd);
}

ATF_TC_CLEANUP(symlink_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(symlinkat_success);
ATF_TC_HEAD(symlinkat_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"symlinkat(2) call");
}

ATF_TC_BODY(symlinkat_success, tc)
{
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, symlinkat(path, AT_FDCWD, "symlink"));
	check_audit(fds, successreg, pipefd);
}

ATF_TC_CLEANUP(symlinkat_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(symlinkat_failure);
ATF_TC_HEAD(symlinkat_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"symlinkat(2) call");
}

ATF_TC_BODY(symlinkat_failure, tc)
{
	ATF_REQUIRE_EQ(0, symlinkat(path, AT_FDCWD, "symlink"));
	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: symbolic link already exists */
	ATF_REQUIRE_EQ(-1, symlinkat(path, AT_FDCWD, "symlink"));
	check_audit(fds, failurereg, pipefd);
}

ATF_TC_CLEANUP(symlinkat_failure, tc)
{
	cleanup();
}


ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, mkdir_success);
	ATF_TP_ADD_TC(tp, mkdir_failure);
	ATF_TP_ADD_TC(tp, mkdirat_success);
	ATF_TP_ADD_TC(tp, mkdirat_failure);

	ATF_TP_ADD_TC(tp, mkfifo_success);
	ATF_TP_ADD_TC(tp, mkfifo_failure);
	ATF_TP_ADD_TC(tp, mkfifoat_success);
	ATF_TP_ADD_TC(tp, mkfifoat_failure);

	ATF_TP_ADD_TC(tp, mknod_success);
	ATF_TP_ADD_TC(tp, mknod_failure);
	ATF_TP_ADD_TC(tp, mknodat_success);
	ATF_TP_ADD_TC(tp, mknodat_failure);

	ATF_TP_ADD_TC(tp, rename_success);
	ATF_TP_ADD_TC(tp, rename_failure);
	ATF_TP_ADD_TC(tp, renameat_success);
	ATF_TP_ADD_TC(tp, renameat_failure);

	ATF_TP_ADD_TC(tp, link_success);
	ATF_TP_ADD_TC(tp, link_failure);
	ATF_TP_ADD_TC(tp, linkat_success);
	ATF_TP_ADD_TC(tp, linkat_failure);

	ATF_TP_ADD_TC(tp, symlink_success);
	ATF_TP_ADD_TC(tp, symlink_failure);
	ATF_TP_ADD_TC(tp, symlinkat_success);
	ATF_TP_ADD_TC(tp, symlinkat_failure);

	return (atf_no_error());
}
