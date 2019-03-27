/*-
 * Copyright (c) 2015 EMC / Isilon Storage Division
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

#ifndef	_FREEBSD_TEST_MACROS_H_
#define	_FREEBSD_TEST_MACROS_H_

#include <sys/param.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include <atf-c.h>

#define	ATF_REQUIRE_FEATURE(_feature_name) do {				\
	if (feature_present(_feature_name) == 0) {			\
		atf_tc_skip("kernel feature (%s) not present",		\
		    _feature_name);					\
	}								\
} while(0)

#define	ATF_REQUIRE_KERNEL_MODULE(_mod_name) do {			\
	if (modfind(_mod_name) == -1) {					\
		atf_tc_skip("module %s could not be resolved: %s",	\
		    _mod_name, strerror(errno));			\
	}								\
} while(0)

#define ATF_REQUIRE_SYSCTL_INT(_mib_name, _required_value) do {		\
	int value;							\
	size_t size = sizeof(value);					\
	if (sysctlbyname(_mib_name, &value, &size, NULL, 0) == -1) {	\
		atf_tc_skip("sysctl for %s failed: %s", _mib_name,	\
		    strerror(errno));					\
	}								\
	if (value != _required_value)					\
		atf_tc_skip("requires %s=%d", _mib_name, _required_value); \
} while(0)

#define	PLAIN_REQUIRE_FEATURE(_feature_name, _exit_code) do {		\
	if (feature_present(_feature_name) == 0) {			\
		printf("kernel feature (%s) not present\n",		\
		    _feature_name);					\
		_exit(_exit_code);					\
	}								\
} while(0)

#define	PLAIN_REQUIRE_KERNEL_MODULE(_mod_name, _exit_code) do {		\
	if (modfind(_mod_name) == -1) {					\
		printf("module %s could not be resolved: %s\n",		\
		    _mod_name, strerror(errno));			\
		_exit(_exit_code);					\
	}								\
} while(0)

#endif
