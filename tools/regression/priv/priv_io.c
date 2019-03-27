/*-
 * Copyright (c) 2006 nCircle Network Security, Inc.
 * Copyright (c) 2007 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by Robert N. M. Watson for the TrustedBSD
 * Project under contract to nCircle Network Security, Inc.
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR, NCIRCLE NETWORK SECURITY,
 * INC., OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Test privilege check on /dev/io.  By default, the permissions also protect
 * against non-superuser access, so this program will modify permissions on
 * /dev/io to allow world access, and revert the change on exit.  This is not
 * good for run-time security, but is necessary to test the checks properly.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "main.h"

#define	NEW_PERMS	0666
#define	DEV_IO		"/dev/io"
#define	EXPECTED_PERMS	0600

static int initialized;
static mode_t saved_perms;

int
priv_io_setup(int asroot, int asjail, struct test *test)
{
	struct stat sb;

	if (stat(DEV_IO, &sb) < 0) {
		warn("priv_io_setup: stat(%s)", DEV_IO);
		return (-1);
	}
	saved_perms = sb.st_mode & ALLPERMS;
	if (saved_perms != EXPECTED_PERMS) {
		warnx("priv_io_setup: perms = 0%o; expected 0%o",
		    saved_perms, EXPECTED_PERMS);
		return (-1);
	}
	if (chmod(DEV_IO, NEW_PERMS) < 0) {
		warn("priv_io_setup: chmod(%s, 0%o)", DEV_IO, NEW_PERMS);
		return (-1);
	}
	initialized = 1;
	return (0);
}

void
priv_io(int asroot, int injail, struct test *test)
{
	int error, fd;

	fd = open(DEV_IO, O_RDONLY);
	if (fd < 0)
		error = -1;
	else
		error = 0;
	if (asroot && injail)
		expect("priv_io(asroot, injail)", error, -1, EPERM);
	if (asroot && !injail)
		expect("priv_io(asroot, !injail)", error, 0, 0);
	if (!asroot && injail)
		expect("priv_io(!asroot, injail)", error, -1, EPERM);
	if (!asroot && !injail)
		expect("priv_io(!asroot, !injail)", error, -1, EPERM);
	if (fd != -1)
		close(fd);
}

void
priv_io_cleanup(int asroot, int asjail, struct test *test)
{

	if (!initialized)
		return;
	if (chmod(DEV_IO, saved_perms) < 0)
		err(-1, "priv_io_cleanup: chmod(%s, 0%o)", DEV_IO,
		    saved_perms);
	initialized = 0;
}
