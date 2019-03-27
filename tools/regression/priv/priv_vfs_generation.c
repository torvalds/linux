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
 * Confirm that a generation number isn't returned by stat() when not running
 * with privilege.  In order to differentiate between a generation of 0 and
 * a generation not being returned, we have to create a temporary file known
 * to have a non-0 generation.  We try up to MAX_TRIES times, and then give
 * up, which is non-ideal, but better than not testing for a problem.
 */

#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "main.h"

static char fpath[1024];
static int fpath_initialized;

#define	MAX_TRIES	100

int
priv_vfs_generation_setup(int asroot, int injail, struct test *test)
{
	struct stat sb;
	int i;

	/*
	 * The kernel zeros the generation number field when an unprivileged
	 * user stats a file.  In order to distinguish the two cases, we
	 * therefore require a file that we know has a non-zero generation
	 * number.  We try up to MAX_TRIES times and otherwise fail.
	 */
	for (i = 0; i < MAX_TRIES; i++) {
		setup_file("priv_vfs_generation_setup: fpath", fpath,
		    UID_ROOT, GID_WHEEL, 0644);
		if (stat(fpath, &sb) < 0) {
			warn("priv_vfs_generation_setup: fstat(%s)", fpath);
			(void)unlink(fpath);
			return (-1);
		}
		if (sb.st_gen != 0) {
			fpath_initialized = 1;
			return (0);
		}
	}
	warnx("priv_vfs_generation_setup: unable to create gen file");
	return (-1);
}

void
priv_vfs_generation(int asroot, int injail, struct test *test)
{
	struct stat sb;
	int error;

	error = stat(fpath, &sb);
	if (error < 0)
		warn("priv_vfs_generation(asroot, injail) stat");

	if (sb.st_gen == 0) {
		error = -1;
		errno = EPERM;
	} else
		error = 0;
	if (asroot && injail)
		expect("priv_vfs_generation(asroot, injail)", error, -1,
		    EPERM);
	if (asroot && !injail)
		expect("priv_vfs_generation(asroot, !injail)", error, 0, 0);
	if (!asroot && injail)
		expect("priv_vfs_generation(!asroot, injail)", error, -1,
		    EPERM);
	if (!asroot && !injail)
		expect("priv_vfs_generation(!asroot, !injail)", error, -1,
		    EPERM);
}

void
priv_vfs_generation_cleanup(int asroot, int injail, struct test *test)
{

	if (fpath_initialized) {
		(void)unlink(fpath);
		fpath_initialized = 0;
	}
}
