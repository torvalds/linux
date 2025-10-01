// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)

/*
 * Copyright (C) 2013-2015 Alexei Starovoitov <ast@kernel.org>
 * Copyright (C) 2015 Wang Nan <wangnan0@huawei.com>
 * Copyright (C) 2015 Huawei Inc.
 * Copyright (C) 2017 Nicira, Inc.
 */

#undef _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "libbpf.h"
#include "libbpf_internal.h"

#ifndef ENOTSUPP
#define ENOTSUPP	524
#endif

/* make sure libbpf doesn't use kernel-only integer typedefs */
#pragma GCC poison u8 u16 u32 u64 s8 s16 s32 s64

#define ERRNO_OFFSET(e)		((e) - __LIBBPF_ERRNO__START)
#define ERRCODE_OFFSET(c)	ERRNO_OFFSET(LIBBPF_ERRNO__##c)
#define NR_ERRNO	(__LIBBPF_ERRNO__END - __LIBBPF_ERRNO__START)

static const char *libbpf_strerror_table[NR_ERRNO] = {
	[ERRCODE_OFFSET(LIBELF)]	= "Something wrong in libelf",
	[ERRCODE_OFFSET(FORMAT)]	= "BPF object format invalid",
	[ERRCODE_OFFSET(KVERSION)]	= "'version' section incorrect or lost",
	[ERRCODE_OFFSET(ENDIAN)]	= "Endian mismatch",
	[ERRCODE_OFFSET(INTERNAL)]	= "Internal error in libbpf",
	[ERRCODE_OFFSET(RELOC)]		= "Relocation failed",
	[ERRCODE_OFFSET(VERIFY)]	= "Kernel verifier blocks program loading",
	[ERRCODE_OFFSET(PROG2BIG)]	= "Program too big",
	[ERRCODE_OFFSET(KVER)]		= "Incorrect kernel version",
	[ERRCODE_OFFSET(PROGTYPE)]	= "Kernel doesn't support this program type",
	[ERRCODE_OFFSET(WRNGPID)]	= "Wrong pid in netlink message",
	[ERRCODE_OFFSET(INVSEQ)]	= "Invalid netlink sequence",
	[ERRCODE_OFFSET(NLPARSE)]	= "Incorrect netlink message parsing",
};

int libbpf_strerror(int err, char *buf, size_t size)
{
	int ret;

	if (!buf || !size)
		return libbpf_err(-EINVAL);

	err = err > 0 ? err : -err;

	if (err < __LIBBPF_ERRNO__START) {
		ret = strerror_r(err, buf, size);
		buf[size - 1] = '\0';
		return libbpf_err_errno(ret);
	}

	if (err < __LIBBPF_ERRNO__END) {
		const char *msg;

		msg = libbpf_strerror_table[ERRNO_OFFSET(err)];
		ret = snprintf(buf, size, "%s", msg);
		buf[size - 1] = '\0';
		/* The length of the buf and msg is positive.
		 * A negative number may be returned only when the
		 * size exceeds INT_MAX. Not likely to appear.
		 */
		if (ret >= size)
			return libbpf_err(-ERANGE);
		return 0;
	}

	ret = snprintf(buf, size, "Unknown libbpf error %d", err);
	buf[size - 1] = '\0';
	if (ret >= size)
		return libbpf_err(-ERANGE);
	return libbpf_err(-ENOENT);
}

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
