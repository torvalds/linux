/*-
 * Copyright (c) 2014 Roger Pau Monné <royger@FreeBSD.org>.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS AS IS'' AND
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

#ifndef __XEN_ERROR_H__
#define __XEN_ERROR_H__

#include <xen/interface/errno.h>

/* Translation table */
static int xen_errors[] =
{
	[XEN_EPERM]		= EPERM,
	[XEN_ENOENT]		= ENOENT,
	[XEN_ESRCH]		= ESRCH,
	[XEN_EIO]		= EIO,
	[XEN_ENXIO]		= ENXIO,
	[XEN_E2BIG]		= E2BIG,
	[XEN_ENOEXEC]		= ENOEXEC,
	[XEN_EBADF]		= EBADF,
	[XEN_ECHILD]		= ECHILD,
	[XEN_EAGAIN]		= EAGAIN,
	[XEN_ENOMEM]		= ENOMEM,
	[XEN_EACCES]		= EACCES,
	[XEN_EFAULT]		= EFAULT,
	[XEN_EBUSY]		= EBUSY,
	[XEN_EEXIST]		= EEXIST,
	[XEN_EXDEV]		= EXDEV,
	[XEN_ENODEV]		= ENODEV,
	[XEN_EINVAL]		= EINVAL,
	[XEN_ENFILE]		= ENFILE,
	[XEN_EMFILE]		= EMFILE,
	[XEN_ENOSPC]		= ENOSPC,
	[XEN_EMLINK]		= EMLINK,
	[XEN_EDOM]		= EDOM,
	[XEN_ERANGE]		= ERANGE,
	[XEN_EDEADLK]		= EDEADLK,
	[XEN_ENAMETOOLONG]	= ENAMETOOLONG,
	[XEN_ENOLCK]		= ENOLCK,
	[XEN_ENOSYS]		= ENOSYS,
	[XEN_ENODATA]		= ENOENT,
	[XEN_ETIME]		= ETIMEDOUT,
	[XEN_EBADMSG]		= EBADMSG,
	[XEN_EOVERFLOW]		= EOVERFLOW,
	[XEN_EILSEQ]		= EILSEQ,
	[XEN_ENOTSOCK]		= ENOTSOCK,
	[XEN_EOPNOTSUPP]	= EOPNOTSUPP,
	[XEN_EADDRINUSE]	= EADDRINUSE,
	[XEN_EADDRNOTAVAIL]	= EADDRNOTAVAIL,
	[XEN_ENOBUFS]		= ENOBUFS,
	[XEN_EISCONN]		= EISCONN,
	[XEN_ENOTCONN]		= ENOTCONN,
	[XEN_ETIMEDOUT]		= ETIMEDOUT,
};

static inline int
xen_translate_error(int error)
{
	int bsd_error;

	KASSERT((error < 0), ("Value is not a valid Xen error code"));

	if (-error >= nitems(xen_errors)) {
		/*
		 * We received an error value that cannot be translated,
		 * return EINVAL.
		 */
		return (EINVAL);
	}

	bsd_error = xen_errors[-error];
	KASSERT((bsd_error != 0), ("Unknown Xen error code"));

	return (bsd_error);
}

#endif /* !__XEN_ERROR_H__ */
