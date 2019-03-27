/*-
 * Copyright (c) 2009-2011 Robert N. M. Watson
 * Copyright (c) 2011 Jonathan Anderson
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
 * Test that fcntl works in capability mode.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/capsicum.h>
#include <sys/errno.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/wait.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "cap_test.h"

/* A filename->descriptor mapping. */
struct fd {
	char	*f_name;
	int	 f_fd;
};

/*
 * Ensure that fcntl() works consistently for both regular file descriptors and
 * capability-wrapped ones.
 */
int
test_fcntl(void)
{
	int success = PASSED;
	cap_rights_t rights = CAP_READ | CAP_FCNTL;

	/*
	 * Open some files of different types, and wrap them in capabilities.
	 */
	struct fd files[] = {
		{ "file",         open("/etc/passwd", O_RDONLY) },
		{ "socket",       socket(PF_LOCAL, SOCK_STREAM, 0) },
		{ "SHM",          shm_open(SHM_ANON, O_RDWR, 0600) },
	};
	REQUIRE(files[0].f_fd);
	REQUIRE(files[1].f_fd);
	REQUIRE(files[2].f_fd);

	struct fd caps[] = {
		{ "file cap",     cap_new(files[0].f_fd, rights) },
		{ "socket cap",   cap_new(files[1].f_fd, rights) },
		{ "SHM cap",      cap_new(files[2].f_fd, rights) },
	};
	REQUIRE(caps[0].f_fd);
	REQUIRE(caps[1].f_fd);
	REQUIRE(caps[2].f_fd);

	struct fd all[] = {
		files[0], caps[0],
		files[1], caps[1],
		files[2], caps[2],
	};
	const size_t len = sizeof(all) / sizeof(struct fd);

	REQUIRE(cap_enter());

	/*
	 * Ensure that we can fcntl() all the files that we opened above.
	 */
	for (size_t i = 0; i < len; i++)
	{
		struct fd f = all[i];
		int cap;

		CHECK_SYSCALL_SUCCEEDS(fcntl, f.f_fd, F_GETFL, 0);
		REQUIRE(cap = cap_new(f.f_fd, CAP_READ));
		if (fcntl(f.f_fd, F_GETFL, 0) == -1)
			FAIL("Error calling fcntl('%s', F_GETFL)", f.f_name);
		else
			CHECK_NOTCAPABLE(fcntl, cap, F_GETFL, 0);
	}

	return (success);
}

