/*	$OpenBSD: t_umask.c,v 1.2 2021/12/13 16:56:48 deraadt Exp $	*/
/* $NetBSD: t_umask.c,v 1.2 2017/01/13 19:34:19 christos Exp $ */

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
#include <sys/wait.h>

#include "atf-c.h"
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char path[] = "umask";
static const mode_t mask[] = {
	S_IRWXU,
	S_IRUSR,
	S_IWUSR,
	S_IXUSR,
	S_IRWXG,
	S_IRGRP,
	S_IWGRP,
	S_IXGRP,
	S_IRWXO,
	S_IROTH,
	S_IWOTH,
	S_IXOTH
};

ATF_TC_WITH_CLEANUP(umask_fork);
ATF_TC_HEAD(umask_fork, tc)
{
	atf_tc_set_md_var(tc, "descr", "Check that umask(2) is inherited");
}

ATF_TC_BODY(umask_fork, tc)
{
	mode_t mode;
	pid_t pid;
	size_t i;
	int sta;

	for (i = 0; i < __arraycount(mask) - 1; i++) {

		(void)umask(mask[i] | mask[i + 1]);

		pid = fork();

		if (pid < 0)
			continue;

		if (pid == 0) {

			mode = umask(mask[i]);

			if (mode != (mask[i] | mask[i + 1]))
				_exit(EXIT_FAILURE);

			_exit(EXIT_SUCCESS);
		}

		(void)wait(&sta);

		if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != EXIT_SUCCESS)
			goto fail;
	}

	return;

fail:
	(void)umask(S_IWGRP | S_IWOTH);

	atf_tc_fail("umask(2) was not inherited");
}

ATF_TC_CLEANUP(umask_fork, tc)
{
	(void)umask(S_IWGRP | S_IWOTH);
}

ATF_TC_WITH_CLEANUP(umask_open);
ATF_TC_HEAD(umask_open, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of open(2) and umask(2)");
}

ATF_TC_BODY(umask_open, tc)
{
	const char *str = NULL;
	struct stat st;
	size_t i;
	int fd;

	for (i = 0; i < __arraycount(mask); i++) {

		(void)umask(mask[i]);

		fd = open(path, O_RDWR | O_CREAT, 0777);

		if (fd < 0)
			continue;

		(void)close(fd);
		(void)memset(&st, 0, sizeof(struct stat));

		if (stat(path, &st) != 0) {
			str = "failed to stat(2)";
			goto out;
		}

		if ((st.st_mode & mask[i]) != 0) {
			str = "invalid umask(2)";
			goto out;
		}

		if (unlink(path) != 0) {
			str = "failed to unlink(2)";
			goto out;
		}

	}

out:
	(void)umask(S_IWGRP | S_IWOTH);

	if (str != NULL)
		atf_tc_fail("%s", str);
}

ATF_TC_CLEANUP(umask_open, tc)
{
	(void)umask(S_IWGRP | S_IWOTH);
	(void)unlink(path);
}

ATF_TC_WITH_CLEANUP(umask_previous);
ATF_TC_HEAD(umask_previous, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test the return value from umask(2)");
}

ATF_TC_BODY(umask_previous, tc)
{
	mode_t mode;
	size_t i;

	for (i = 0; i < __arraycount(mask); i++) {

		mode = umask(mask[i]);
		mode = umask(mask[i]);

		if (mode != mask[i])
			goto fail;
	}

	return;

fail:
	(void)umask(S_IWGRP | S_IWOTH);

	atf_tc_fail("umask(2) did not return the previous mask");
}

ATF_TC_CLEANUP(umask_previous, tc)
{
	(void)umask(S_IWGRP | S_IWOTH);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, umask_fork);
	ATF_TP_ADD_TC(tp, umask_open);
	ATF_TP_ADD_TC(tp, umask_previous);

	return atf_no_error();
}
