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
 * Confirm that calls to fhstat() require non-jailed privilege.  We create a
 * temporary file and grab the file handle using getfh() before starting.
 */

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <unistd.h>

#include "main.h"

static char fpath[1024];
static int fpath_initialized;
static fhandle_t fh;

int
priv_vfs_fhstat_setup(int asroot, int injail, struct test *test)
{

	setup_file("priv_vfs_fhstat_setup: fpath", fpath, UID_ROOT,
	    GID_WHEEL, 0644);
	fpath_initialized = 1;
	if (getfh(fpath, &fh) < 0) {
		warn("priv_vfs_fhstat_setup: getfh(%s)", fpath);
		return (-1);
	}
	return (0);
}

void
priv_vfs_fhstat(int asroot, int injail, struct test *test)
{
	struct stat sb;
	int error;

	error = fhstat(&fh, &sb);
	if (asroot && injail)
		expect("priv_vfs_fhstat(asroot, injail)", error, -1, EPERM);
	if (asroot && !injail)
		expect("priv_vfs_fhstat(asroot, !injail)", error, 0, 0);
	if (!asroot && injail)
		expect("priv_vfs_fhstat(!asroot, injail)", error, -1, EPERM);
	if (!asroot && !injail)
		expect("priv_vfs_fhstat(!asroot, !injail)", error, -1, EPERM);
}

void
priv_vfs_fhstat_cleanup(int asroot, int injail, struct test *test)
{

	if (fpath_initialized) {
		(void)unlink(fpath);
		fpath_initialized = 0;
	}
}
