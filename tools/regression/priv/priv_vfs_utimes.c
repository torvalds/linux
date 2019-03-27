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
 * Test NULL and non-NULL tv arguments to utimes() -- if NULL, then it is
 * allowed without privilege if the owner or if write access is held.  If
 * non-NULL, privilege is required even if writable.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "main.h"

static char fpath[1024];
static int fpath_initialized;

int
priv_vfs_utimes_froot_setup(int asroot, int injail, struct test *test)
{

	setup_file("priv_vfs_utimes_froot_setup: fpath", fpath,
	    UID_ROOT, GID_WHEEL, 0600);
	fpath_initialized = 1;
	return (0);
}

int
priv_vfs_utimes_fowner_setup(int asroot, int injail, struct test *test)
{

	setup_file("priv_vfs_utimes_fowner_setup: fpath", fpath,
	    UID_OWNER, GID_OWNER, 0600);
	fpath_initialized = 1;
	return (0);
}

int
priv_vfs_utimes_fother_setup(int asroot, int injail, struct test *test)
{

	/*
	 * In the 'other' case, we make the file writable by the test user so
	 * we can evaluate the difference between setting the time to NULL,
	 * which is possible as a writer, and non-NULL, which requires
	 * ownership.
	 */
	setup_file("priv_vfs_utimes_fother_setup: fpath", fpath,
	    UID_OTHER, GID_OTHER, 0666);
	fpath_initialized = 1;
	return (0);
}

void
priv_vfs_utimes_froot(int asroot, int injail, struct test *test)
{
	struct timeval tv[2];
	int error;

	tv[0].tv_sec = 0;
	tv[0].tv_usec = 0;
	tv[1].tv_sec = 0;
	tv[1].tv_usec = 0;
	error = utimes(fpath, tv);
	if (asroot && injail)
		expect("priv_vfs_utimes_froot(root, jail)", error, 0, 0);
	if (asroot && !injail)
		expect("priv_vfs_utimes_froot(root, !jail)", error, 0, 0);
	if (!asroot && injail)
		expect("priv_vfs_utimes_froot(!root, jail)", error, -1,
		    EPERM);
	if (!asroot && !injail)
		expect("priv_vfs_utimes_froot(!root, !jail)", error, -1,
		    EPERM);
}

void
priv_vfs_utimes_froot_null(int asroot, int injail, struct test *test)
{
	int error;

	error = utimes(fpath, NULL);
	if (asroot && injail)
		expect("priv_vfs_utimes_froot_null(root, jail)", error, 0,
		    0);
	if (asroot && !injail)
		expect("priv_vfs_utimes_froot_null(root, !jail)", error, 0,
		    0);
	if (!asroot && injail)
		expect("priv_vfs_utimes_froot_null(!root, jail)", error, -1,
		    EACCES);
	if (!asroot && !injail)
		expect("priv_vfs_utimes_froot_null(!root, !jail)", error, -1,
		    EACCES);
}

void
priv_vfs_utimes_fowner(int asroot, int injail, struct test *test)
{
	struct timeval tv[2];
	int error;

	tv[0].tv_sec = 0;
	tv[0].tv_usec = 0;
	tv[1].tv_sec = 0;
	tv[1].tv_usec = 0;
	error = utimes(fpath, tv);
	if (asroot && injail)
		expect("priv_vfs_utimes_fowner(root, jail)", error, 0, 0);
	if (asroot && !injail)
		expect("priv_vfs_utimes_fowner(root, !jail)", error, 0, 0);
	if (!asroot && injail)
		expect("priv_vfs_utimes_fowner(!root, jail)", error, 0, 0);
	if (!asroot && !injail)
		expect("priv_vfs_utimes_fowner(!root, !jail)", error, 0, 0);
}

void
priv_vfs_utimes_fowner_null(int asroot, int injail, struct test *test)
{
	int error;

	error = utimes(fpath, NULL);
	if (asroot && injail)
		expect("priv_vfs_utimes_fowner_null(root, jail)", error, 0,
		    0);
	if (asroot && !injail)
		expect("priv_vfs_utimes_fowner_null(root, !jail)", error, 0,
		    0);
	if (!asroot && injail)
		expect("priv_vfs_utimes_fowner_null(!root, jail)", error, 0,
		    0);
	if (!asroot && !injail)
		expect("priv_vfs_utimes_fowner_null(!root, !jail)", error, 0,
		    0);
}

void
priv_vfs_utimes_fother(int asroot, int injail, struct test *test)
{
	struct timeval tv[2];
	int error;

	tv[0].tv_sec = 0;
	tv[0].tv_usec = 0;
	tv[1].tv_sec = 0;
	tv[1].tv_usec = 0;
	error = utimes(fpath, tv);
	if (asroot && injail)
		expect("priv_vfs_utimes_fother(root, jail)", error, 0, 0);
	if (asroot && !injail)
		expect("priv_vfs_utimes_fother(root, !jail)", error, 0, 0);
	if (!asroot && injail)
		expect("priv_vfs_utimes_fother(!root, jail)", error, -1,
		    EPERM);
	if (!asroot && !injail)
		expect("priv_vfs_utimes_fother(!root, !jail)", error, -1,
		    EPERM);
}

void
priv_vfs_utimes_fother_null(int asroot, int injail, struct test *test)
{
	int error;

	error = utimes(fpath, NULL);
	if (asroot && injail)
		expect("priv_vfs_utimes_fother_null(root, jail)", error, 0,
		    0);
	if (asroot && !injail)
		expect("priv_vfs_utimes_fother_null(root, !jail)", error, 0,
		    0);
	if (!asroot && injail)
		expect("priv_vfs_utimes_fother_null(!root, jail)", error, 0,
		    0);
	if (!asroot && !injail)
		expect("priv_vfs_utimes_fother_null(!root, !jail)", error, 0,
		    0);
}

void
priv_vfs_utimes_cleanup(int asroot, int injail, struct test *test)
{

	if (fpath_initialized) {
		(void)unlink(fpath);
		fpath_initialized = 0;
	}
}
