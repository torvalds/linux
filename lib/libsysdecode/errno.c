/*-
 * Copyright (c) 2015 John H. Baldwin <jhb@FreeBSD.org>
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include <sys/param.h>
#include <sys/acl.h>
#include <sys/wait.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <sysdecode.h>

#if defined(__aarch64__) || defined(__amd64__) || defined(__i386__)
static
#include <compat/linux/linux_errno.inc>
#endif

#include <contrib/cloudabi/cloudabi_types_common.h>

static const int cloudabi_errno_table[] = {
	[CLOUDABI_E2BIG]		= E2BIG,
	[CLOUDABI_EACCES]		= EACCES,
	[CLOUDABI_EADDRINUSE]		= EADDRINUSE,
	[CLOUDABI_EADDRNOTAVAIL]	= EADDRNOTAVAIL,
	[CLOUDABI_EAFNOSUPPORT]		= EAFNOSUPPORT,
	[CLOUDABI_EAGAIN]		= EAGAIN,
	[CLOUDABI_EALREADY]		= EALREADY,
	[CLOUDABI_EBADF]		= EBADF,
	[CLOUDABI_EBADMSG]		= EBADMSG,
	[CLOUDABI_EBUSY]		= EBUSY,
	[CLOUDABI_ECANCELED]		= ECANCELED,
	[CLOUDABI_ECHILD]		= ECHILD,
	[CLOUDABI_ECONNABORTED]		= ECONNABORTED,
	[CLOUDABI_ECONNREFUSED]		= ECONNREFUSED,
	[CLOUDABI_ECONNRESET]		= ECONNRESET,
	[CLOUDABI_EDEADLK]		= EDEADLK,
	[CLOUDABI_EDESTADDRREQ]		= EDESTADDRREQ,
	[CLOUDABI_EDOM]			= EDOM,
	[CLOUDABI_EDQUOT]		= EDQUOT,
	[CLOUDABI_EEXIST]		= EEXIST,
	[CLOUDABI_EFAULT]		= EFAULT,
	[CLOUDABI_EFBIG]		= EFBIG,
	[CLOUDABI_EHOSTUNREACH]		= EHOSTUNREACH,
	[CLOUDABI_EIDRM]		= EIDRM,
	[CLOUDABI_EILSEQ]		= EILSEQ,
	[CLOUDABI_EINPROGRESS]		= EINPROGRESS,
	[CLOUDABI_EINTR]		= EINTR,
	[CLOUDABI_EINVAL]		= EINVAL,
	[CLOUDABI_EIO]			= EIO,
	[CLOUDABI_EISCONN]		= EISCONN,
	[CLOUDABI_EISDIR]		= EISDIR,
	[CLOUDABI_ELOOP]		= ELOOP,
	[CLOUDABI_EMFILE]		= EMFILE,
	[CLOUDABI_EMLINK]		= EMLINK,
	[CLOUDABI_EMSGSIZE]		= EMSGSIZE,
	[CLOUDABI_EMULTIHOP]		= EMULTIHOP,
	[CLOUDABI_ENAMETOOLONG]		= ENAMETOOLONG,
	[CLOUDABI_ENETDOWN]		= ENETDOWN,
	[CLOUDABI_ENETRESET]		= ENETRESET,
	[CLOUDABI_ENETUNREACH]		= ENETUNREACH,
	[CLOUDABI_ENFILE]		= ENFILE,
	[CLOUDABI_ENOBUFS]		= ENOBUFS,
	[CLOUDABI_ENODEV]		= ENODEV,
	[CLOUDABI_ENOENT]		= ENOENT,
	[CLOUDABI_ENOEXEC]		= ENOEXEC,
	[CLOUDABI_ENOLCK]		= ENOLCK,
	[CLOUDABI_ENOLINK]		= ENOLINK,
	[CLOUDABI_ENOMEM]		= ENOMEM,
	[CLOUDABI_ENOMSG]		= ENOMSG,
	[CLOUDABI_ENOPROTOOPT]		= ENOPROTOOPT,
	[CLOUDABI_ENOSPC]		= ENOSPC,
	[CLOUDABI_ENOSYS]		= ENOSYS,
	[CLOUDABI_ENOTCONN]		= ENOTCONN,
	[CLOUDABI_ENOTDIR]		= ENOTDIR,
	[CLOUDABI_ENOTEMPTY]		= ENOTEMPTY,
	[CLOUDABI_ENOTRECOVERABLE]	= ENOTRECOVERABLE,
	[CLOUDABI_ENOTSOCK]		= ENOTSOCK,
	[CLOUDABI_ENOTSUP]		= ENOTSUP,
	[CLOUDABI_ENOTTY]		= ENOTTY,
	[CLOUDABI_ENXIO]		= ENXIO,
	[CLOUDABI_EOVERFLOW]		= EOVERFLOW,
	[CLOUDABI_EOWNERDEAD]		= EOWNERDEAD,
	[CLOUDABI_EPERM]		= EPERM,
	[CLOUDABI_EPIPE]		= EPIPE,
	[CLOUDABI_EPROTO]		= EPROTO,
	[CLOUDABI_EPROTONOSUPPORT]	= EPROTONOSUPPORT,
	[CLOUDABI_EPROTOTYPE]		= EPROTOTYPE,
	[CLOUDABI_ERANGE]		= ERANGE,
	[CLOUDABI_EROFS]		= EROFS,
	[CLOUDABI_ESPIPE]		= ESPIPE,
	[CLOUDABI_ESRCH]		= ESRCH,
	[CLOUDABI_ESTALE]		= ESTALE,
	[CLOUDABI_ETIMEDOUT]		= ETIMEDOUT,
	[CLOUDABI_ETXTBSY]		= ETXTBSY,
	[CLOUDABI_EXDEV]		= EXDEV,
	[CLOUDABI_ENOTCAPABLE]		= ENOTCAPABLE,
};

int
sysdecode_abi_to_freebsd_errno(enum sysdecode_abi abi, int error)
{

	switch (abi) {
	case SYSDECODE_ABI_FREEBSD:
	case SYSDECODE_ABI_FREEBSD32:
		return (error);
#if defined(__aarch64__) || defined(__amd64__) || defined(__i386__)
	case SYSDECODE_ABI_LINUX:
	case SYSDECODE_ABI_LINUX32: {
		unsigned int i;

		/*
		 * This is imprecise since it returns the first
		 * matching errno.
		 */
		for (i = 0; i < nitems(linux_errtbl); i++) {
			if (error == linux_errtbl[i])
				return (i);
		}
		break;
	}
#endif
	case SYSDECODE_ABI_CLOUDABI32:
	case SYSDECODE_ABI_CLOUDABI64:
		if (error >= 0 &&
		    (unsigned int)error < nitems(cloudabi_errno_table))
			return (cloudabi_errno_table[error]);
		break;
	default:
		break;
	}
	return (INT_MAX);
}

int
sysdecode_freebsd_to_abi_errno(enum sysdecode_abi abi, int error)
{

	switch (abi) {
	case SYSDECODE_ABI_FREEBSD:
	case SYSDECODE_ABI_FREEBSD32:
		return (error);
#if defined(__aarch64__) || defined(__amd64__) || defined(__i386__)
	case SYSDECODE_ABI_LINUX:
	case SYSDECODE_ABI_LINUX32:
		if (error >= 0 && error <= ELAST)
			return (linux_errtbl[error]);
		break;
#endif
	case SYSDECODE_ABI_CLOUDABI32:
	case SYSDECODE_ABI_CLOUDABI64: {
		unsigned int i;

		for (i = 0; i < nitems(cloudabi_errno_table); i++) {
			if (error == cloudabi_errno_table[i])
				return (i);
		}
		break;
	}
	default:
		break;
	}
	return (INT_MAX);
}

