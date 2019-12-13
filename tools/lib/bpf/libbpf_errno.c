// SPDX-License-Identifier: LGPL-2.1

/*
 * Copyright (C) 2013-2015 Alexei Starovoitov <ast@kernel.org>
 * Copyright (C) 2015 Wang Nan <wangnan0@huawei.com>
 * Copyright (C) 2015 Huawei Inc.
 * Copyright (C) 2017 Nicira, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License (not later!)
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not,  see <http://www.gnu.org/licenses>
 */

#undef _GNU_SOURCE
#include <stdio.h>
#include <string.h>

#include "libbpf.h"

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
};

int libbpf_strerror(int err, char *buf, size_t size)
{
	if (!buf || !size)
		return -1;

	err = err > 0 ? err : -err;

	if (err < __LIBBPF_ERRNO__START) {
		int ret;

		ret = strerror_r(err, buf, size);
		buf[size - 1] = '\0';
		return ret;
	}

	if (err < __LIBBPF_ERRNO__END) {
		const char *msg;

		msg = libbpf_strerror_table[ERRNO_OFFSET(err)];
		snprintf(buf, size, "%s", msg);
		buf[size - 1] = '\0';
		return 0;
	}

	snprintf(buf, size, "Unknown libbpf error %d", err);
	buf[size - 1] = '\0';
	return -1;
}
