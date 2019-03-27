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
 * Test privileges associated with setting file flags on files; whether or
 * not it requires privilege depends on the flag, and some flags cannot be
 * set in jail at all.
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

/*
 * For chflags, we consider three dimmensions: process owner, file owner, and
 * flag type.  The calling framework handles variations in process owner; the
 * rest are handled via multiple tests.  One cleanup function is used.
 */
static u_long
getflags(char *fpathp)
{
	struct stat sb;

	if (stat(fpathp, &sb) < 0)
		err(-1, "stat(%s)", fpathp);

	return (sb.st_flags);
}

int
priv_vfs_chflags_froot_setup(int asroot, int injail, struct test *test)
{

	setup_file("priv_vfs_chflags_froot_setup: fpath", fpath, UID_ROOT,
	    GID_WHEEL, 0600);
	fpath_initialized = 1;
	return (0);
}

int
priv_vfs_chflags_fowner_setup(int asroot, int injail,
    struct test *test)
{

	setup_file("priv_vfs_chflags_fowner_setup: fpath", fpath, UID_OWNER,
	    GID_OWNER, 0600);
	fpath_initialized = 1;
	return (0);
}

int
priv_vfs_chflags_fother_setup(int asroot, int injail,
    struct test *test)
{

	setup_file("priv_vfs_chflags_fowner_setup: fpath", fpath, UID_OTHER,
	    GID_OTHER, 0600);
	fpath_initialized = 1;
	return (0);
}

void
priv_vfs_chflags_froot_uflags(int asroot, int injail,
    struct test *test)
{
	u_long flags;
	int error;

	flags = getflags(fpath);
	flags |= UF_NODUMP;
	error = chflags(fpath, flags);
	if (asroot && injail)
		expect("priv_vfs_chflags_froot_uflags(asroot, injail)",
		    error, 0, 0);
	if (asroot && !injail)
		expect("priv_vfs_chflags_froot_uflags(asroot, !injail)",
		    error, 0, 0);
	if (!asroot && injail)
		expect("priv_vfs_chflags_froot_uflags(!asroot, injail)",
		    error, -1, EPERM);
	if (!asroot && !injail)
		expect("priv_vfs_chflags_froot_uflags(!asroot, !injail)",
		    error, -1, EPERM);
}

void
priv_vfs_chflags_fowner_uflags(int asroot, int injail,
    struct test *test)
{
	u_long flags;
	int error;

	flags = getflags(fpath);
	flags |= UF_NODUMP;
	error = chflags(fpath, flags);
	if (asroot && injail)
		expect("priv_vfs_chflags_fowner_uflags(asroot, injail)",
		    error, 0, 0);
	if (asroot && !injail)
		expect("priv_vfs_chflags_fowner_uflags(asroot, !injail)",
		    error, 0, 0);
	if (!asroot && injail)
		expect("priv_vfs_chflags_fowner_uflags(!asroot, injail)",
		    error, 0, 0);
	if (!asroot && !injail)
		expect("priv_vfs_chflags_fowner_uflags(!asroot, !injail)",
		    error, 0, 0);
}

void
priv_vfs_chflags_fother_uflags(int asroot, int injail,
    struct test *test)
{
	u_long flags;
	int error;

	flags = getflags(fpath);
	flags |= UF_NODUMP;
	error = chflags(fpath, flags);
	if (asroot && injail)
		expect("priv_vfs_chflags_fother_uflags(asroot, injail)",
		    error, 0, 0);
	if (asroot && !injail)
		expect("priv_vfs_chflags_fother_uflags(asroot, !injail)",
		    error, 0, 0);
	if (!asroot && injail)
		expect("priv_vfs_chflags_fother_uflags(!asroot, injail)",
		    error, -1, EPERM);
	if (!asroot && !injail)
		expect("priv_vfs_chflags_fother_uflags(!asroot, !injail)",
		    error, -1, EPERM);
}

void
priv_vfs_chflags_froot_sflags(int asroot, int injail,
    struct test *test)
{
	u_long flags;
	int error;

	flags = getflags(fpath);
	flags |= SF_ARCHIVED;
	error = chflags(fpath, flags);
	if (asroot && injail)
		expect("priv_vfs_chflags_froot_sflags(asroot, injail)",
		    error, -1, EPERM);
	if (asroot && !injail)
		expect("priv_vfs_chflags_froot_sflags(asroot, !injail)",
		    error, 0, 0);
	if (!asroot && injail)
		expect("priv_vfs_chflags_froot_sflags(!asroot, injail)",
		    error, -1, EPERM);
	if (!asroot && !injail)
		expect("priv_vfs_chflags_froot_sflags(!asroot, !injail)",
		    error, -1, EPERM);
}

void
priv_vfs_chflags_fowner_sflags(int asroot, int injail,
    struct test *test)
{
	u_long flags;
	int error;

	flags = getflags(fpath);
	flags |= SF_ARCHIVED;
	error = chflags(fpath, flags);
	if (asroot && injail)
		expect("priv_vfs_chflags_fowner_sflags(asroot, injail)",
		    error, -1, EPERM);
	if (asroot && !injail)
		expect("priv_vfs_chflags_fowner_sflags(asroot, !injail)",
		    error, 0, 0);
	if (!asroot && injail)
		expect("priv_vfs_chflags_fowner_sflags(!asroot, injail)",
		    error, -1, EPERM);
	if (!asroot && !injail)
		expect("priv_vfs_chflags_fowner_sflags(!asroot, !injail)",
		    error, -1, EPERM);
}

void
priv_vfs_chflags_fother_sflags(int asroot, int injail,
    struct test *test)
{
	u_long flags;
	int error;

	flags = getflags(fpath);
	flags |= SF_ARCHIVED;
	error = chflags(fpath, flags);
	if (asroot && injail)
		expect("priv_vfs_chflags_fother_sflags(asroot, injail)",
		    error, -1, EPERM);
	if (asroot && !injail)
		expect("priv_vfs_chflags_fother_sflags(asroot, !injail)",
		    error, 0, 0);
	if (!asroot && injail)
		expect("priv_vfs_chflags_fother_sflags(!asroot, injail)",
		    error, -1, EPERM);
	if (!asroot && !injail)
		expect("priv_vfs_chflags_fother_sflags(!asroot, !injail)",
		    error, -1, EPERM);
}

void
priv_vfs_chflags_cleanup(int asroot, int injail, struct test *test)
{

	if (fpath_initialized) {
		(void)chflags(fpath, 0);
		(void)unlink(fpath);
		fpath_initialized = 0;
	}
}
