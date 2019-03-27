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
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/errno.h>

#include <err.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cap_test.h"

/*
 * Test openat(2) in a variety of sitations to ensure that it obeys Capsicum
 * "strict relative" rules:
 *
 * 1. Use strict relative lookups in capability mode or when operating
 *    relative to a capability.
 * 2. When performing strict relative lookups, absolute paths (including
 *    symlinks to absolute paths) are not allowed, nor are paths containing
 *    '..' components.
 */
int
test_relative(void)
{
	int success = PASSED;
	int fd, etc, etc_cap, etc_cap_ro, etc_cap_base, etc_cap_all;
	cap_rights_t baserights = CAP_READ | CAP_WRITE | CAP_SEEK | CAP_LOOKUP;
	cap_rights_t rights;

	REQUIRE(etc = open("/etc/", O_RDONLY));
	CHECK_SYSCALL_SUCCEEDS(cap_getrights, etc, &rights);
	CHECK_RIGHTS(rights, CAP_ALL);

	MAKE_CAPABILITY(etc_cap, etc, CAP_READ);
	MAKE_CAPABILITY(etc_cap_ro, etc, CAP_READ | CAP_LOOKUP);
	MAKE_CAPABILITY(etc_cap_base, etc, baserights);
	MAKE_CAPABILITY(etc_cap_all, etc, CAP_MASK_VALID);

	/*
	 * openat(2) with regular file descriptors in non-capability mode
	 * should Just Work (tm).
	 */
	CHECK_SYSCALL_SUCCEEDS(openat, etc, "/etc/passwd", O_RDONLY);
	CHECK_SYSCALL_SUCCEEDS(openat, AT_FDCWD, "/etc/passwd", O_RDONLY);
	CHECK_SYSCALL_SUCCEEDS(openat, etc, "passwd", O_RDONLY);
	CHECK_SYSCALL_SUCCEEDS(openat, etc, "../etc/passwd", O_RDONLY);

	/*
	 * Lookups relative to capabilities should be strictly relative.
	 *
	 * When not in capability mode, we don't actually require CAP_LOOKUP.
	 */
	CHECK_SYSCALL_SUCCEEDS(openat, etc_cap_ro, "passwd", O_RDONLY);
	CHECK_SYSCALL_SUCCEEDS(openat, etc_cap_base, "passwd", O_RDONLY);
	CHECK_SYSCALL_SUCCEEDS(openat, etc_cap_all, "passwd", O_RDONLY);

	CHECK_NOTCAPABLE(openat, etc_cap_ro, "../etc/passwd", O_RDONLY);
	CHECK_NOTCAPABLE(openat, etc_cap_base, "../etc/passwd", O_RDONLY);

	/*
	 * This requires discussion: do we treat a capability with
	 * CAP_MASK_VALID *exactly* like a non-capability file descriptor
	 * (currently, the implementation says yes)?
	 */
	CHECK_SYSCALL_SUCCEEDS(openat, etc_cap_all, "../etc/passwd", O_RDONLY);

	/*
	 * A file opened relative to a capability should itself be a capability.
	 */
	CHECK_SYSCALL_SUCCEEDS(cap_getrights, etc_cap_base, &rights);

	REQUIRE(fd = openat(etc_cap_base, "passwd", O_RDONLY));
	CHECK_SYSCALL_SUCCEEDS(cap_getrights, fd, &rights);
	CHECK_RIGHTS(rights, baserights);

	/*
	 * Enter capability mode; now ALL lookups are strictly relative.
	 */
	REQUIRE(cap_enter());

	/*
	 * Relative lookups on regular files or capabilities with CAP_LOOKUP
	 * ought to succeed.
	 */
	CHECK_SYSCALL_SUCCEEDS(openat, etc, "passwd", O_RDONLY);
	CHECK_SYSCALL_SUCCEEDS(openat, etc_cap_ro, "passwd", O_RDONLY);
	CHECK_SYSCALL_SUCCEEDS(openat, etc_cap_base, "passwd", O_RDONLY);
	CHECK_SYSCALL_SUCCEEDS(openat, etc_cap_all, "passwd", O_RDONLY);

	/*
	 * Lookup relative to capabilities without CAP_LOOKUP should fail.
	 */
	CHECK_NOTCAPABLE(openat, etc_cap, "passwd", O_RDONLY);

	/*
	 * Absolute lookups should fail.
	 */
	CHECK_CAPMODE(openat, AT_FDCWD, "/etc/passwd", O_RDONLY);
	CHECK_NOTCAPABLE(openat, etc, "/etc/passwd", O_RDONLY);

	/*
	 * Lookups containing '..' should fail in capability mode.
	 */
	CHECK_NOTCAPABLE(openat, etc, "../etc/passwd", O_RDONLY);
	CHECK_NOTCAPABLE(openat, etc_cap_ro, "../etc/passwd", O_RDONLY);
	CHECK_NOTCAPABLE(openat, etc_cap_base, "../etc/passwd", O_RDONLY);

	REQUIRE(fd = openat(etc, "passwd", O_RDONLY));
	CHECK_SYSCALL_SUCCEEDS(cap_getrights, fd, &rights);

	/*
	 * A file opened relative to a capability should itself be a capability.
	 */
	REQUIRE(fd = openat(etc_cap_base, "passwd", O_RDONLY));
	CHECK_SYSCALL_SUCCEEDS(cap_getrights, fd, &rights);
	CHECK_RIGHTS(rights, baserights);

	return success;
}
