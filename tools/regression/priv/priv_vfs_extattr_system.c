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
 * Test that privilege is required to write to the system extended attribute
 * namespace.
 */

#include <sys/types.h>
#include <sys/extattr.h>

#include <err.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "main.h"

#define	EA_NAMESPACE	EXTATTR_NAMESPACE_SYSTEM
#define	EA_NAME		"test"
#define	EA_DATA		"test"
#define	EA_SIZE		strlen(EA_DATA)

static char fpath[1024];
static int fpath_initialized;

int
priv_vfs_extattr_system_setup(int asroot, int injail, struct test *test)
{

	/*
	 * Set file perms so that discretionary access control would grant
	 * write rights on non-system EAs on the file.
	 */
	setup_file("priv_vfs_extattr_system_setup: fpath", fpath, UID_ROOT,
	    GID_WHEEL, 0666);
	fpath_initialized = 1;
	return (0);
}

void
priv_vfs_extattr_system(int asroot, int injail, struct test *test)
{
	ssize_t ret;
	int error;

	ret = extattr_set_file(fpath, EA_NAMESPACE, EA_NAME, EA_DATA,
	    EA_SIZE);
	if (ret < 0)
		error = -1;
	else if (ret == EA_SIZE)
		error = 0;
	else
		err(-1, "priv_vfs_extattr_system: set returned %zd", ret);
	if (asroot && injail)
		expect("priv_vfs_extattr_system(asroot, injail)", error, -1,
		    EPERM);
	if (asroot && !injail)
		expect("priv_vfs_extattr_system(asroot, !injail)", error, 0,
		    0);
	if (!asroot && injail)
		expect("priv_vfs_extattr_system(!asroot, injail)", error, -1,
		    EPERM);
	if (!asroot && !injail)
		expect("priv_vfs_extattr_system(!asroot, !injail)", error,
		    -1, EPERM);
}

void
priv_vfs_extattr_system_cleanup(int asroot, int injail, struct test *test)
{

	if (fpath_initialized) {
		(void)unlink(fpath);
		fpath_initialized = 0;
	}
}
