/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017 Kyle J. Kneitinger <kyle@kneit.in>
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
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "be.h"
#include "be_impl.h"

/*
 * Usage
 */
int
libbe_errno(libbe_handle_t *lbh)
{

	return (lbh->error);
}


const char *
libbe_error_description(libbe_handle_t *lbh)
{

	switch (lbh->error) {
	case BE_ERR_INVALIDNAME:
		return ("invalid boot environment name");

	case BE_ERR_EXISTS:
		return ("boot environment name already taken");

	case BE_ERR_NOENT:
		return ("specified boot environment does not exist");

	case BE_ERR_PERMS:
		return ("insufficient permissions");

	case BE_ERR_DESTROYACT:
		return ("cannot destroy active boot environment");

	case BE_ERR_DESTROYMNT:
		return ("cannot destroy mounted boot env unless forced");

	case BE_ERR_BADPATH:
		return ("path not suitable for operation");

	case BE_ERR_PATHBUSY:
		return ("specified path is busy");

	case BE_ERR_PATHLEN:
		return ("provided path name exceeds maximum length limit");

	case BE_ERR_BADMOUNT:
		return ("mountpoint is not \"/\"");

	case BE_ERR_NOORIGIN:
		return ("could not open snapshot's origin");

	case BE_ERR_MOUNTED:
		return ("boot environment is already mounted");

	case BE_ERR_NOMOUNT:
		return ("boot environment is not mounted");

	case BE_ERR_ZFSOPEN:
		return ("calling zfs_open() failed");

	case BE_ERR_ZFSCLONE:
		return ("error when calling zfs_clone() to create boot env");

	case BE_ERR_IO:
		return ("input/output error");

	case BE_ERR_NOPOOL:
		return ("operation not supported on this pool");

	case BE_ERR_NOMEM:
		return ("insufficient memory");

	case BE_ERR_UNKNOWN:
		return ("unknown error");

	case BE_ERR_INVORIGIN:
		return ("invalid origin");

	default:
		assert(lbh->error == BE_ERR_SUCCESS);
		return ("no error");
	}
}


void
libbe_print_on_error(libbe_handle_t *lbh, bool val)
{

	lbh->print_on_err = val;
	libzfs_print_on_error(lbh->lzh, val);
}


int
set_error(libbe_handle_t *lbh, be_error_t err)
{

	lbh->error = err;
	if (lbh->print_on_err && (err != BE_ERR_SUCCESS))
		fprintf(stderr, "%s\n", libbe_error_description(lbh));

	return (err);
}
