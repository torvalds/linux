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
 * This is a joint test of both the read and write privileges with respect to
 * discretionary file system access control (permissions).  Only permissions,
 * not ACL semantics, and only privilege-related checks are performed.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "main.h"

static char fpath_none[1024];
static char fpath_read[1024];
static char fpath_write[1024];
static char fpath_readwrite[1024];

static int fpath_none_initialized;
static int fpath_read_initialized;
static int fpath_write_initialized;
static int fpath_readwrite_initialized;

static void
try_io(const char *label, const char *fpathp, int asroot, int injail, 
    int flags, int expected_error, int expected_errno)
{
	int fd;

	fd = open(fpathp, flags);
	if (fd < 0) {
		if (expected_error != -1)
			warnx("%s(%s, %s): expected (%d, %d) got (-1, %d)",
			    label, asroot ? "root" : "!root", injail ? "jail"
			    : "!jail", expected_error, expected_errno, errno);
	} else {
		if (expected_error == -1)
			warnx("%s(%s, %s): expected (%d, %d) got 0", label,
			    asroot ? "root" : "!root", injail ? "jail" :
			    "!jail", expected_error, expected_errno);
		(void)close(fd);
	}
}

int
priv_vfs_readwrite_fowner_setup(int asroot, int injail, struct test *test)
{

	setup_file("priv_vfs_readwrite_fowner_setup: fpath_none", fpath_none,
	    asroot ? UID_ROOT : UID_OWNER, GID_OTHER, 0000);	
	fpath_none_initialized = 1;
	setup_file("priv_vfs_readwrite_fowner_setup: fpath_read", fpath_read,
	    asroot ? UID_ROOT : UID_OWNER, GID_OTHER, 0400);
	fpath_read_initialized = 1;
	setup_file("priv_vfs_readwrite_fowner_setup: fpath_write",
	    fpath_write, asroot ? UID_ROOT : UID_OWNER, GID_OTHER, 0200);
	fpath_write_initialized = 1;
	setup_file("priv_vfs_readwrite_fowner_setup: fpath_readwrite",
	    fpath_readwrite, asroot ? UID_ROOT : UID_OWNER, GID_OTHER, 0600);
	fpath_readwrite_initialized = 1;
	return (0);
}

int
priv_vfs_readwrite_fgroup_setup(int asroot, int injail, struct test *test)
{

	setup_file("priv_vfs_readwrite_fgroup_setup: fpath_none", fpath_none,
	    UID_OTHER, asroot ? GID_WHEEL : GID_OWNER, 0000);
	fpath_none_initialized = 1;
	setup_file("priv_vfs_readwrite_fgroup_setup: fpath_read", fpath_read,
	    UID_OTHER, asroot ? GID_WHEEL : GID_OWNER, 0040);
	fpath_read_initialized = 1;
	setup_file("priv_vfs_readwrite_fgroup_setup: fpath_write",
	    fpath_write, UID_OTHER, asroot ? GID_WHEEL : GID_OWNER, 0020);
	fpath_write_initialized = 1;
	setup_file("priv_vfs_readwrite_fgroup_setup: fpath_readwrite",
	    fpath_readwrite, UID_OTHER, asroot ? GID_WHEEL : GID_OWNER,
	    0060);
	fpath_readwrite_initialized = 1;
	return (0);
}

int
priv_vfs_readwrite_fother_setup(int asroot, int injail, struct test *test)
{

	setup_file("priv_vfs_readwrite_fother_setup: fpath_none", fpath_none,
	    UID_OTHER, GID_OTHER, 0000);
	fpath_none_initialized = 1;
	setup_file("priv_vfs_readwrite_fother_setup: fpath_read", fpath_read,
	    UID_OTHER, GID_OTHER, 0004);
	fpath_read_initialized = 1;
	setup_file("priv_vfs_readwrite_fother_setup: fpath_write",
	    fpath_write, UID_OTHER, GID_OTHER, 0002);
	fpath_write_initialized = 1;
	setup_file("priv_vfs_readwrite_fother_setup: fpath_readwrite",
	    fpath_readwrite, UID_OTHER, GID_OTHER, 0006);
	fpath_readwrite_initialized = 1;
	return (0);
}

void
priv_vfs_readwrite_fowner(int asroot, int injail, struct test *test)
{

	try_io("priv_vfs_readwrite_fowner(none, O_RDONLY)", fpath_none,
	    asroot, injail, O_RDONLY, asroot ? 0 : -1, EACCES);
	try_io("priv_vfs_readwrite_fowner(none, O_WRONLY)", fpath_none,
	    asroot, injail, O_WRONLY, asroot ? 0 : -1, EACCES);
	try_io("priv_vfs_readwrite_fowner(none, O_RDWR)", fpath_none,
	    asroot, injail, O_RDWR, asroot ? 0 : -1, EACCES);

	try_io("priv_vfs_readwrite_fowner(read, O_RDONLY)", fpath_read,
	    asroot, injail, O_RDONLY, 0, 0);
	try_io("priv_vfs_readwrite_fowner(read, O_WRONLY)", fpath_read,
	    asroot, injail, O_WRONLY, asroot ? 0 : -1, EACCES);
	try_io("priv_vfs_readwrite_fowner(read, O_RDWR)", fpath_read,
	    asroot, injail, O_RDWR, asroot ? 0 : -1, EACCES);

	try_io("priv_vfs_readwrite_fowner(write, O_RDONLY)", fpath_write,
	    asroot, injail, O_RDONLY, asroot ? 0 : -1, EACCES);
	try_io("priv_vfs_readwrite_fowner(write, O_WRONLY)", fpath_write,
	    asroot, injail, O_WRONLY, 0, 0);
	try_io("priv_vfs_readwrite_fowner(write, O_RDWR)", fpath_write,
	    asroot, injail, O_RDWR, asroot ? 0 : -1, EACCES);

	try_io("priv_vfs_readwrite_fowner(write, O_RDONLY)", fpath_readwrite,
	    asroot, injail, O_RDONLY, 0, 0);
	try_io("priv_vfs_readwrite_fowner(write, O_WRONLY)", fpath_readwrite,
	    asroot, injail, O_WRONLY, 0, 0);
	try_io("priv_vfs_readwrite_fowner(write, O_RDWR)", fpath_readwrite,
	    asroot, injail, O_RDWR, 0, 0);
}

void
priv_vfs_readwrite_fgroup(int asroot, int injail, struct test *test)
{

	try_io("priv_vfs_readwrite_fgroup(none, O_RDONLY)", fpath_none,
	    asroot, injail, O_RDONLY, asroot ? 0 : -1, EACCES);
	try_io("priv_vfs_readwrite_fgroup(none, O_WRONLY)", fpath_none,
	    asroot, injail, O_WRONLY, asroot ? 0 : -1, EACCES);
	try_io("priv_vfs_readwrite_fgroup(none, O_RDWR)", fpath_none,
	    asroot, injail, O_RDWR, asroot ? 0 : -1, EACCES);

	try_io("priv_vfs_readwrite_fgroup(read, O_RDONLY)", fpath_read,
	    asroot, injail, O_RDONLY, 0, 0);
	try_io("priv_vfs_readwrite_fgroup(read, O_WRONLY)", fpath_read,
	    asroot, injail, O_WRONLY, asroot ? 0 : -1, EACCES);
	try_io("priv_vfs_readwrite_fgroup(read, O_RDWR)", fpath_read,
	    asroot, injail, O_RDWR, asroot ? 0 : -1, EACCES);

	try_io("priv_vfs_readwrite_fgroup(write, O_RDONLY)", fpath_write,
	    asroot, injail, O_RDONLY, asroot ? 0 : -1, EACCES);
	try_io("priv_vfs_readwrite_fgroup(write, O_WRONLY)", fpath_write,
	    asroot, injail, O_WRONLY, 0, 0);
	try_io("priv_vfs_readwrite_fgroup(write, O_RDWR)", fpath_write,
	    asroot, injail, O_RDWR, asroot ? 0 : -1, EACCES);

	try_io("priv_vfs_readwrite_fgroup(write, O_RDONLY)", fpath_readwrite,
	    asroot, injail, O_RDONLY, 0, 0);
	try_io("priv_vfs_readwrite_fgroup(write, O_WRONLY)", fpath_readwrite,
	    asroot, injail, O_WRONLY, 0, 0);
	try_io("priv_vfs_readwrite_fgroup(write, O_RDWR)", fpath_readwrite,
	    asroot, injail, O_RDWR, 0, 0);
}

void
priv_vfs_readwrite_fother(int asroot, int injail, struct test *test)
{

	try_io("priv_vfs_readwrite_fother(none, O_RDONLY)", fpath_none,
	    asroot, injail, O_RDONLY, asroot ? 0 : -1, EACCES);
	try_io("priv_vfs_readwrite_fother(none, O_WRONLY)", fpath_none,
	    asroot, injail, O_WRONLY, asroot ? 0 : -1, EACCES);
	try_io("priv_vfs_readwrite_fother(none, O_RDWR)", fpath_none,
	    asroot, injail, O_RDWR, asroot ? 0 : -1, EACCES);

	try_io("priv_vfs_readwrite_fother(read, O_RDONLY)", fpath_read,
	    asroot, injail, O_RDONLY, 0, 0);
	try_io("priv_vfs_readwrite_fother(read, O_WRONLY)", fpath_read,
	    asroot, injail, O_WRONLY, asroot ? 0 : -1, EACCES);
	try_io("priv_vfs_readwrite_fother(read, O_RDWR)", fpath_read,
	    asroot, injail, O_RDWR, asroot ? 0 : -1, EACCES);

	try_io("priv_vfs_readwrite_fother(write, O_RDONLY)", fpath_write,
	    asroot, injail, O_RDONLY, asroot ? 0 : -1, EACCES);
	try_io("priv_vfs_readwrite_fother(write, O_WRONLY)", fpath_write,
	    asroot, injail, O_WRONLY, 0, 0);
	try_io("priv_vfs_readwrite_fother(write, O_RDWR)", fpath_write,
	    asroot, injail, O_RDWR, asroot ? 0 : -1, EACCES);

	try_io("priv_vfs_readwrite_fother(write, O_RDONLY)", fpath_readwrite,
	    asroot, injail, O_RDONLY, 0, 0);
	try_io("priv_vfs_readwrite_fother(write, O_WRONLY)", fpath_readwrite,
	    asroot, injail, O_WRONLY, 0, 0);
	try_io("priv_vfs_readwrite_fother(write, O_RDWR)", fpath_readwrite,
	    asroot, injail, O_RDWR, 0, 0);
}

void
priv_vfs_readwrite_cleanup(int asroot, int injail, struct test *test)
{

	if (fpath_none_initialized) {
		(void)unlink(fpath_none);
		fpath_none_initialized = 0;
	}
	if (fpath_read_initialized) {
		(void)unlink(fpath_read);
		fpath_read_initialized = 0;
	}
	if (fpath_write_initialized) {
		(void)unlink(fpath_write);
		fpath_write_initialized = 0;
	}
	if (fpath_readwrite_initialized) {
		(void)unlink(fpath_readwrite);
		fpath_readwrite_initialized = 0;
	}
}
