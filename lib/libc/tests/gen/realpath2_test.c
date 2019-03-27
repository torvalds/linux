/*
 * Copyright (c) 2017 Jan Kokemüller
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
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <atf-c.h>

ATF_TC(realpath_buffer_overflow);
ATF_TC_HEAD(realpath_buffer_overflow, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test for out of bounds read from 'left' array "
	    "(compile realpath.c with '-fsanitize=address')");
}

ATF_TC_BODY(realpath_buffer_overflow, tc)
{
	char path[MAXPATHLEN] = { 0 };
	char resb[MAXPATHLEN] = { 0 };
	size_t i;

	path[0] = 'a';
	path[1] = '/';
	for (i = 2; i < sizeof(path) - 1; ++i) {
		path[i] = 'a';
	}

	ATF_REQUIRE(realpath(path, resb) == NULL);
}

ATF_TC(realpath_empty_symlink);
ATF_TC_HEAD(realpath_empty_symlink, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test for correct behavior when encountering empty symlinks");
}

ATF_TC_BODY(realpath_empty_symlink, tc)
{
	char path[MAXPATHLEN] = { 0 };
	char slnk[MAXPATHLEN] = { 0 };
	char resb[MAXPATHLEN] = { 0 };
	int fd;

	(void)strlcat(slnk, "empty_symlink", sizeof(slnk));

	ATF_REQUIRE(symlink("", slnk) == 0);

	fd = open("aaa", O_RDONLY | O_CREAT, 0600);

	ATF_REQUIRE(fd >= 0);
	ATF_REQUIRE(close(fd) == 0);

	(void)strlcat(path, "empty_symlink", sizeof(path));
	(void)strlcat(path, "/aaa", sizeof(path));

	ATF_REQUIRE_ERRNO(ENOENT, realpath(path, resb) == NULL);

	ATF_REQUIRE(unlink("aaa") == 0);
	ATF_REQUIRE(unlink(slnk) == 0);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, realpath_buffer_overflow);
	ATF_TP_ADD_TC(tp, realpath_empty_symlink);

	return atf_no_error();
}
