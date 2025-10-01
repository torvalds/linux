// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
#undef _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "str_error.h"

#ifndef ENOTSUPP
#define ENOTSUPP	524
#endif

/* make sure libbpf doesn't use kernel-only integer typedefs */
#pragma GCC poison u8 u16 u32 u64 s8 s16 s32 s64

const char *libbpf_errstr(int err)
{
	static __thread char buf[12];

	if (err > 0)
		err = -err;

	switch (err) {
	case -E2BIG:		return "-E2BIG";
	case -EACCES:		return "-EACCES";
	case -EADDRINUSE:	return "-EADDRINUSE";
	case -EADDRNOTAVAIL:	return "-EADDRNOTAVAIL";
	case -EAGAIN:		return "-EAGAIN";
	case -EALREADY:		return "-EALREADY";
	case -EBADF:		return "-EBADF";
	case -EBADFD:		return "-EBADFD";
	case -EBUSY:		return "-EBUSY";
	case -ECANCELED:	return "-ECANCELED";
	case -ECHILD:		return "-ECHILD";
	case -EDEADLK:		return "-EDEADLK";
	case -EDOM:		return "-EDOM";
	case -EEXIST:		return "-EEXIST";
	case -EFAULT:		return "-EFAULT";
	case -EFBIG:		return "-EFBIG";
	case -EILSEQ:		return "-EILSEQ";
	case -EINPROGRESS:	return "-EINPROGRESS";
	case -EINTR:		return "-EINTR";
	case -EINVAL:		return "-EINVAL";
	case -EIO:		return "-EIO";
	case -EISDIR:		return "-EISDIR";
	case -ELOOP:		return "-ELOOP";
	case -EMFILE:		return "-EMFILE";
	case -EMLINK:		return "-EMLINK";
	case -EMSGSIZE:		return "-EMSGSIZE";
	case -ENAMETOOLONG:	return "-ENAMETOOLONG";
	case -ENFILE:		return "-ENFILE";
	case -ENODATA:		return "-ENODATA";
	case -ENODEV:		return "-ENODEV";
	case -ENOENT:		return "-ENOENT";
	case -ENOEXEC:		return "-ENOEXEC";
	case -ENOLINK:		return "-ENOLINK";
	case -ENOMEM:		return "-ENOMEM";
	case -ENOSPC:		return "-ENOSPC";
	case -ENOTBLK:		return "-ENOTBLK";
	case -ENOTDIR:		return "-ENOTDIR";
	case -ENOTSUPP:		return "-ENOTSUPP";
	case -ENOTTY:		return "-ENOTTY";
	case -ENXIO:		return "-ENXIO";
	case -EOPNOTSUPP:	return "-EOPNOTSUPP";
	case -EOVERFLOW:	return "-EOVERFLOW";
	case -EPERM:		return "-EPERM";
	case -EPIPE:		return "-EPIPE";
	case -EPROTO:		return "-EPROTO";
	case -EPROTONOSUPPORT:	return "-EPROTONOSUPPORT";
	case -ERANGE:		return "-ERANGE";
	case -EROFS:		return "-EROFS";
	case -ESPIPE:		return "-ESPIPE";
	case -ESRCH:		return "-ESRCH";
	case -ETXTBSY:		return "-ETXTBSY";
	case -EUCLEAN:		return "-EUCLEAN";
	case -EXDEV:		return "-EXDEV";
	default:
		snprintf(buf, sizeof(buf), "%d", err);
		return buf;
	}
}
