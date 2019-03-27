/*-
 * Copyright (c) 2016 Chelsio Communications, Inc.
 * All rights reserved.
 * Written by: John Baldwin <jhb@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

#ifndef _AIO_TEST_LOCAL_H_
#define	_AIO_TEST_LOCAL_H_

#include <sys/types.h>
#include <sys/sysctl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <atf-c.h>

static const char	*sysctl_oid_name = "vfs.aio.enable_unsafe";

static int
is_unsafe_aio_enabled(void)
{
	size_t len;
	int unsafe;

	len = sizeof(unsafe);
	if (sysctlbyname(sysctl_oid_name, &unsafe, &len, NULL, 0) < 0) {
		if (errno == ENOENT)
			return (-1);
		return (0);
	}
	return (unsafe == 0 ? 0 : 1);
}

#define	ATF_REQUIRE_UNSAFE_AIO() do {						\
	switch (is_unsafe_aio_enabled()) {					\
	case -1:								\
		atf_libc_error(errno, "Failed to read %s", sysctl_oid_name);	\
		break;								\
	case 0:									\
		atf_tc_skip("Unsafe AIO is disabled");				\
		break;								\
	default:								\
		printf("Unsafe AIO is enabled\n");				\
		break;								\
	}									\
} while (0)

#define	PLAIN_REQUIRE_UNSAFE_AIO(_exit_code) do {				\
	switch (is_unsafe_aio_enabled()) {					\
	case -1:								\
		printf("Failed to read %s", sysctl_oid_name);			\
		_exit(_exit_code);						\
		break;								\
	case 0:									\
		printf("Unsafe AIO is disabled\n");				\
		_exit(_exit_code);						\
		break;								\
	default:								\
		printf("Unsafe AIO is enabled\n");				\
		break;								\
	}									\
} while (0)

#endif /* !_AIO_TEST_LOCAL_H_ */
