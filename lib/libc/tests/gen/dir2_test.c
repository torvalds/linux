/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * 
 * Copyright (c) 2017 Spectra Logic Corporation
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
 * Test cases for operations on DIR objects:
 * opendir, readdir, seekdir, telldir, closedir, etc
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#include <atf-c.h>

ATF_TC(telldir_after_seekdir);
ATF_TC_HEAD(telldir_after_seekdir, tc)
{

	atf_tc_set_md_var(tc, "descr", "Calling telldir(3) after seekdir(3) "
	    "should return the argument passed to seekdir.");
}
ATF_TC_BODY(telldir_after_seekdir, tc)
{
	const int NUMFILES = 1000;
	char template[] = "dXXXXXX";
	char *tmpdir;
	int i, dirfd;
	DIR *dirp;
	struct dirent *de;
	long beginning, middle, end, td;
	
	/* Create a temporary directory */
	tmpdir = mkdtemp(template);
	ATF_REQUIRE_MSG(tmpdir != NULL, "mkdtemp failed");
	dirfd = open(tmpdir, O_RDONLY | O_DIRECTORY);
	ATF_REQUIRE(dirfd > 0);

	/* 
	 * Fill it with files.  Must be > 128 to ensure that the directory
	 * can't fit within a single page
	 */
	for (i = 0; i < NUMFILES; i = i+1) {
		int fd;
		char filename[16];

		snprintf(filename, sizeof(filename), "%d", i);
		fd = openat(dirfd, filename, O_WRONLY | O_CREAT);
		ATF_REQUIRE(fd > 0);
		close(fd);
	}

	/* Get some directory bookmarks in various locations */
	dirp = fdopendir(dirfd);
	ATF_REQUIRE_MSG(dirfd >= 0, "fdopendir failed");
	beginning = telldir(dirp);
	for (i = 0; i < NUMFILES / 2; i = i+1) {
		de = readdir(dirp);
		ATF_REQUIRE_MSG(de != NULL, "readdir failed");
	}
	middle = telldir(dirp);
	for (; i < NUMFILES - 1; i = i+1) {
		de = readdir(dirp);
		ATF_REQUIRE_MSG(de != NULL, "readdir failed");
	}
	end = telldir(dirp);

	/*
	 * Seekdir to each bookmark, check the telldir after seekdir condition,
	 * and check that the bookmark is valid by reading another directory
	 * entry.
	 */

	seekdir(dirp, beginning);
	td = telldir(dirp);
	ATF_CHECK_EQ(beginning, td);
	ATF_REQUIRE_MSG(NULL != readdir(dirp), "invalid directory index");

	seekdir(dirp, middle);
	td = telldir(dirp);
	ATF_CHECK_EQ(middle, td);
	ATF_REQUIRE_MSG(NULL != readdir(dirp), "invalid directory index");

	seekdir(dirp, end);
	td = telldir(dirp);
	ATF_CHECK_EQ(end, td);
	ATF_REQUIRE_MSG(NULL != readdir(dirp), "invalid directory index");

	closedir(dirp);
}	

ATF_TC(telldir_at_end_of_block);
ATF_TC_HEAD(telldir_at_end_of_block, tc)
{

	atf_tc_set_md_var(tc, "descr", "Calling telldir(3) after readdir(3) read the last entry in the block should return a valid location");
}
ATF_TC_BODY(telldir_at_end_of_block, tc)
{
	/* For UFS and ZFS, blocks roll over at 128 directory entries.  */
	const int NUMFILES = 129;
	char template[] = "dXXXXXX";
	char *tmpdir;
	int i, dirfd;
	DIR *dirp;
	struct dirent *de;
	long td;
	char last_filename[16];

	/* Create a temporary directory */
	tmpdir = mkdtemp(template);
	ATF_REQUIRE_MSG(tmpdir != NULL, "mkdtemp failed");
	dirfd = open(tmpdir, O_RDONLY | O_DIRECTORY);
	ATF_REQUIRE(dirfd > 0);

	/* 
	 * Fill it with files.  Must be > 128 to ensure that the directory
	 * can't fit within a single page.  The "-2" accounts for "." and ".."
	 */
	for (i = 0; i < NUMFILES - 2; i = i+1) {
		int fd;
		char filename[16];

		snprintf(filename, sizeof(filename), "%d", i);
		fd = openat(dirfd, filename, O_WRONLY | O_CREAT);
		ATF_REQUIRE(fd > 0);
		close(fd);
	}

	/* Read all entries within the first page */
	dirp = fdopendir(dirfd);
	ATF_REQUIRE_MSG(dirfd >= 0, "fdopendir failed");
	for (i = 0; i < NUMFILES - 1; i = i + 1)
		ATF_REQUIRE_MSG(readdir(dirp) != NULL, "readdir failed");

	/* Call telldir at the end of a page */
	td = telldir(dirp);

	/* Read the last entry */
	de = readdir(dirp);
	ATF_REQUIRE_MSG(de != NULL, "readdir failed");
	strlcpy(last_filename, de->d_name, sizeof(last_filename));

	/* Seek back to the bookmark. readdir() should return the last entry */
	seekdir(dirp, td);
	de = readdir(dirp);
	ATF_REQUIRE_STREQ_MSG(last_filename, de->d_name,
			"seekdir went to the wrong directory position");

	closedir(dirp);
}
	

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, telldir_after_seekdir);
	ATF_TP_ADD_TC(tp, telldir_at_end_of_block);

	return atf_no_error();
}
