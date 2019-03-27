/*-
 * Copyright (c) 2006 nCircle Network Security, Inc.
 * Copyright (c) 2007 Robert M. M. Watson
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
 * Confirm that privilege is required in the cases using chown():
 *
 * - If the process euid does not match the file uid.
 *
 * - If the target uid is different than the current uid.
 *
 * - If the target gid changes and we the process is not a member of the new
 *   group.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include "main.h"

static char fpath[1024];
static int fpath_initialized;

/*
 * Check that changing the uid of a file requires privilege.
 */
int
priv_vfs_chown_uid_setup(int asroot, int injail, struct test *test)
{

	setup_file("priv_vfs_chown_uid: fpath", fpath, UID_ROOT, GID_WHEEL,
	    0600);
	fpath_initialized = 1;
	return (0);
}

void
priv_vfs_chown_uid(int asroot, int injail, struct test *test)
{
	int error;

	error = chown(fpath, UID_OWNER, -1);
	if (asroot && injail)
		expect("priv_vfs_chown_uid(root, jail)", error, 0, 0);
	if (asroot && !injail)
		expect("priv_vfs_chown_uid(root, !jail)", error, 0, 0);
	if (!asroot && injail)
		expect("priv_vfs_chown_uid(!root, jail)", error, -1, EPERM);
	if (!asroot && !injail)
		expect("priv_vfs_chown_uid(!root, !jail)", error, -1, EPERM);
}

/*
 * Check that changing the gid of a file owned by the user is allowed without
 * privilege as long as the gid matches the process.
 */
int
priv_vfs_chown_mygid_setup(int asroot, int injail, struct test *test)
{

	/*
	 * Create a file with a matching uid to the test process, but not a
	 * matching gid.
	 */
	setup_file("priv_vfs_chown_mygid: fpath", fpath, asroot ? UID_ROOT :
	    UID_OWNER, GID_OTHER, 0600);
	fpath_initialized = 1;
	return (0);
}

void
priv_vfs_chown_mygid(int asroot, int injail, struct test *test)
{
	int error;

	error = chown(fpath, -1, asroot ? GID_WHEEL : GID_OWNER);
	if (asroot && injail)
		expect("priv_vfs_chown_mygid(root, jail)", error, 0, 0);
	if (asroot && !injail)
		expect("priv_vfs_chown_mygid(root, !jail)", error, 0, 0);
	if (!asroot && injail)
		expect("priv_vfs_chown_mygid(!root, !jail)", error, 0, 0);
	if (!asroot && !injail)
		expect("priv_vfs_chown_mygid(!root, !jail)", error, 0, 0);
}

/*
 * Check that changing the gid of a file owned by the user is not allowed
 * without privilege if the gid doesn't match the process.
 */
int
priv_vfs_chown_othergid_setup(int asroot, int injail, struct test *test)
{

	/*
	 * Create a file with a matching uid to the test process with a
	 * matching gid.
	 */
	setup_file("priv_vfs_chown_othergid: fpath", fpath, asroot ? UID_ROOT
	    : UID_OWNER, asroot ? GID_WHEEL : GID_OWNER, 0600);
	fpath_initialized = 1;
	return (0);
}

void
priv_vfs_chown_othergid(int asroot, int injail, struct test *test)
{
	int error;

	error = chown(fpath, -1, GID_OTHER);
	if (asroot && injail)
		expect("priv_vfs_chown_othergid(root, jail)", error, 0, 0);
	if (asroot && !injail)
		expect("priv_vfs_chown_othergid(root, !jail)", error, 0, 0);
	if (!asroot && injail)
		expect("priv_vfs_chown_othergid(!root, !jail)", error, -1,
		    EPERM);
	if (!asroot && !injail)
		expect("priv_vfs_chown_othergid(!root, !jail)", error, -1,
		    EPERM);
}

void
priv_vfs_chown_cleanup(int asroot, int injail, struct test *test)
{

	if (fpath_initialized) {
		(void)unlink(fpath);
		fpath_initialized = 0;
	}
}
