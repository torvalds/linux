/*	$OpenBSD: t_stat.c,v 1.5 2023/10/31 07:57:59 claudio Exp $	*/
/* $NetBSD: t_stat.c,v 1.6 2019/07/16 17:29:18 martin Exp $ */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jukka Ruohonen.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "macros.h"

#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include "atf-c.h"
#include <errno.h>
#include <fcntl.h>
#include <fts.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

#include <stdio.h>

static const char *path = "stat";

ATF_TC_WITH_CLEANUP(stat_chflags);
ATF_TC_HEAD(stat_chflags, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test chflags(2) with stat(2)");
}

ATF_TC_BODY(stat_chflags, tc)
{
	struct stat sa, sb;
	int fd;

	(void)memset(&sa, 0, sizeof(struct stat));
	(void)memset(&sb, 0, sizeof(struct stat));

	fd = open(path, O_RDONLY | O_CREAT, 0600);

	ATF_REQUIRE(fd != -1);
	ATF_REQUIRE(stat(path, &sa) == 0);
	ATF_REQUIRE(chflags(path, UF_NODUMP) == 0);
	ATF_REQUIRE(stat(path, &sb) == 0);

	if (sa.st_flags == sb.st_flags)
		atf_tc_fail("stat(2) did not detect chflags(2)");

	ATF_REQUIRE(close(fd) == 0);
	ATF_REQUIRE(unlink(path) == 0);
}

ATF_TC_CLEANUP(stat_chflags, tc)
{
	(void)unlink(path);
}

ATF_TC(stat_dir);
ATF_TC_HEAD(stat_dir, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test stat(2) with directories");
}

ATF_TC_BODY(stat_dir, tc)
{
	const short depth = 2;
	struct stat sa, sb;
	char *argv[2];
	FTSENT *ftse;
	FTS *fts;
	int ops;

	argv[1] = NULL;
	argv[0] = __UNCONST("/");

	ops = FTS_NOCHDIR;
	ops |= FTS_PHYSICAL;

	fts = fts_open(argv, ops, NULL);
	ATF_REQUIRE(fts != NULL);

	while ((ftse = fts_read(fts)) != NULL) {

		if (ftse->fts_level < 1)
			continue;

		if (ftse->fts_level > depth) {
			(void)fts_set(fts, ftse, FTS_SKIP);
			continue;
		}

		switch(ftse->fts_info) {

		case FTS_DP:

			(void)memset(&sa, 0, sizeof(struct stat));
			(void)memset(&sb, 0, sizeof(struct stat));

			ATF_REQUIRE(stat(ftse->fts_parent->fts_path,&sa) == 0);
			ATF_REQUIRE(chdir(ftse->fts_path) == 0);
			ATF_REQUIRE(stat(".", &sb) == 0);

			/*
			 * The previous two stat(2) calls
			 * should be for the same directory.
			 */
			if (sa.st_dev != sb.st_dev || sa.st_ino != sb.st_ino)
				atf_tc_fail("inconsistent stat(2)");

			/*
			 * Check that fts(3)'s stat(2)
			 * call equals the manual one.
			 */
			if (sb.st_ino != ftse->fts_statp->st_ino)
				atf_tc_fail("stat(2) and fts(3) differ");

			break;

		default:
			break;
		}
	}

	(void)fts_close(fts);
}

ATF_TC(stat_err);
ATF_TC_HEAD(stat_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test errors from the stat(2) family");
}

ATF_TC_BODY(stat_err, tc)
{
	char buf[NAME_MAX + 1];
	struct stat st;

	(void)memset(buf, 'x', sizeof(buf));

	errno = 0;
	ATF_REQUIRE_ERRNO(EBADF, fstat(-1, &st) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(ENAMETOOLONG, stat(buf, &st) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(ENAMETOOLONG, lstat(buf, &st) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(EFAULT, stat((void *)-1, &st) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(EFAULT, lstat((void *)-1, &st) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(EFAULT, stat("/etc/passwd", (void *)-1) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(EFAULT, lstat("/etc/passwd", (void *)-1) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(ENOENT, stat("/a/b/c/d/e/f/g/h/i/j/k", &st) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(ENOENT, lstat("/a/b/c/d/e/f/g/h/i/j/k", &st) == -1);
}

ATF_TC_WITH_CLEANUP(stat_mtime);
ATF_TC_HEAD(stat_mtime, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test modification times with stat(2)");
}

ATF_TC_BODY(stat_mtime, tc)
{
	struct stat sa, sb;
	int fd[3];
	size_t i;

	for (i = 0; i < __arraycount(fd); i++) {

		(void)memset(&sa, 0, sizeof(struct stat));
		(void)memset(&sb, 0, sizeof(struct stat));

		fd[i] = open(path, O_WRONLY | O_CREAT, 0600);

		ATF_REQUIRE(fd[i] != -1);
		ATF_REQUIRE(write(fd[i], "X", 1) == 1);
		ATF_REQUIRE(stat(path, &sa) == 0);

		(void)sleep(1);

		ATF_REQUIRE(write(fd[i], "X", 1) == 1);
		ATF_REQUIRE(stat(path, &sb) == 0);

		ATF_REQUIRE(close(fd[i]) == 0);
		ATF_REQUIRE(unlink(path) == 0);

		if (sa.st_mtime == sb.st_mtime)
			atf_tc_fail("mtimes did not change");
	}
}

ATF_TC_CLEANUP(stat_mtime, tc)
{
	(void)unlink(path);
}

ATF_TC_WITH_CLEANUP(stat_perm);
ATF_TC_HEAD(stat_perm, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test permissions with stat(2)");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(stat_perm, tc)
{
	struct stat sa, sb;
	gid_t gid;
	uid_t uid;
	int fd;

	(void)memset(&sa, 0, sizeof(struct stat));
	(void)memset(&sb, 0, sizeof(struct stat));

	uid = getuid();
	gid = getgid();

	fd = open(path, O_RDONLY | O_CREAT, 0600);

	ATF_REQUIRE(fd != -1);
	ATF_REQUIRE(fstat(fd, &sa) == 0);
	ATF_REQUIRE(stat(path, &sb) == 0);

	if (sa.st_gid != sb.st_gid)
		atf_tc_fail("invalid GID");

	if (uid != sa.st_uid || sa.st_uid != sb.st_uid)
		atf_tc_fail("invalid UID");

	ATF_REQUIRE(close(fd) == 0);
	ATF_REQUIRE(unlink(path) == 0);
}

ATF_TC_CLEANUP(stat_perm, tc)
{
	(void)unlink(path);
}

ATF_TC_WITH_CLEANUP(stat_size);
ATF_TC_HEAD(stat_size, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test file sizes with stat(2)");
}

ATF_TC_BODY(stat_size, tc)
{
	struct stat sa, sb, sc;
	const size_t n = 10;
	size_t i;
	int fd;

	fd = open(path, O_WRONLY | O_CREAT, 0600);
	ATF_REQUIRE(fd >= 0);

	for (i = 0; i < n; i++) {

		(void)memset(&sa, 0, sizeof(struct stat));
		(void)memset(&sb, 0, sizeof(struct stat));
		(void)memset(&sc, 0, sizeof(struct stat));

		ATF_REQUIRE(fstat(fd, &sa) == 0);
		ATF_REQUIRE(write(fd, "X", 1) == 1);
		ATF_REQUIRE(fstat(fd, &sb) == 0);
		ATF_REQUIRE(stat(path, &sc) == 0);

		if (sa.st_size + 1 != sb.st_size)
			atf_tc_fail("invalid file size");

		if (sb.st_size != sc.st_size)
			atf_tc_fail("stat(2) and fstat(2) mismatch");
	}

	ATF_REQUIRE(close(fd) == 0);
	ATF_REQUIRE(unlink(path) == 0);
}

ATF_TC_CLEANUP(stat_size, tc)
{
	(void)unlink(path);
}

ATF_TC(stat_socket);
ATF_TC_HEAD(stat_socket, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test fstat(2) with "
	    "a socket (PR kern/46077)");
}

ATF_TC_BODY(stat_socket, tc)
{
	struct sockaddr_in addr;
	struct stat st;
	uint32_t iaddr;
	int fd, flags;

	(void)memset(&st, 0, sizeof(struct stat));
	(void)memset(&addr, 0, sizeof(struct sockaddr_in));

	fd = socket(AF_INET, SOCK_STREAM, 0);
	ATF_REQUIRE(fd >= 0);

	flags = fcntl(fd, F_GETFL);

	ATF_REQUIRE(flags != -1);
	ATF_REQUIRE(fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1);
	ATF_REQUIRE(inet_pton(AF_INET, "127.0.0.1", &iaddr) == 1);

	addr.sin_port = htons(42);
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = iaddr;

	errno = 0;

	ATF_REQUIRE_ERRNO(EINPROGRESS,
	    connect(fd, (struct sockaddr *)&addr,
		sizeof(struct sockaddr_in)) == -1);

	errno = 0;

	if (fstat(fd, &st) != 0 || errno != 0)
		atf_tc_fail("fstat(2) failed for a EINPROGRESS socket");

	(void)close(fd);
}

ATF_TC_WITH_CLEANUP(stat_symlink);
ATF_TC_HEAD(stat_symlink, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test symbolic links with stat(2)");
}

ATF_TC_BODY(stat_symlink, tc)
{
	const char *pathlink = "pathlink";
	struct stat sa, sb;
	int fd;

	(void)memset(&sa, 0, sizeof(struct stat));
	(void)memset(&sb, 0, sizeof(struct stat));

	fd = open(path, O_WRONLY | O_CREAT, 0600);

	ATF_REQUIRE(fd >= 0);
	ATF_REQUIRE(symlink(path, pathlink) == 0);
	ATF_REQUIRE(stat(pathlink, &sa) == 0);
	ATF_REQUIRE(lstat(pathlink, &sb) == 0);

	if (S_ISLNK(sa.st_mode) != 0)
		atf_tc_fail("stat(2) detected symbolic link");

	if (S_ISLNK(sb.st_mode) == 0)
		atf_tc_fail("lstat(2) did not detect symbolic link");

	if (sa.st_mode == sb.st_mode)
		atf_tc_fail("inconsistencies between stat(2) and lstat(2)");

	(void)close(fd);
	ATF_REQUIRE(unlink(path) == 0);
	ATF_REQUIRE(unlink(pathlink) == 0);
}

ATF_TC_CLEANUP(stat_symlink, tc)
{
	(void)unlink(path);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, stat_chflags);
	ATF_TP_ADD_TC(tp, stat_dir);
	ATF_TP_ADD_TC(tp, stat_err);
	ATF_TP_ADD_TC(tp, stat_mtime);
	ATF_TP_ADD_TC(tp, stat_perm);
	ATF_TP_ADD_TC(tp, stat_size);
	ATF_TP_ADD_TC(tp, stat_socket);
	ATF_TP_ADD_TC(tp, stat_symlink);

	return atf_no_error();
}
