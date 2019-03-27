/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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

#include <sys/types.h>
#include <sys/capsicum.h>
#include <sys/sysctl.h>
#include <sys/nv.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libcasper.h>

#include <casper/cap_sysctl.h>

/*
 * We need some sysctls to perform the tests on.
 * We remember their values and restore them afer the test is done.
 */
#define	SYSCTL0_PARENT	"kern"
#define	SYSCTL0_NAME	"kern.sync_on_panic"
#define	SYSCTL1_PARENT	"debug"
#define	SYSCTL1_NAME	"debug.minidump"

static int ntest = 1;

#define CHECK(expr)     do {						\
	if ((expr))							\
		printf("ok %d # %s:%u\n", ntest, __FILE__, __LINE__);	\
	else								\
		printf("not ok %d # %s:%u\n", ntest, __FILE__, __LINE__); \
	fflush(stdout);							\
	ntest++;							\
} while (0)
#define CHECKX(expr)     do {						\
	if ((expr)) {							\
		printf("ok %d # %s:%u\n", ntest, __FILE__, __LINE__);	\
	} else {							\
		printf("not ok %d # %s:%u\n", ntest, __FILE__, __LINE__); \
		exit(1);						\
	}								\
	fflush(stdout);							\
	ntest++;							\
} while (0)

#define	SYSCTL0_READ0		0x0001
#define	SYSCTL0_READ1		0x0002
#define	SYSCTL0_READ2		0x0004
#define	SYSCTL0_WRITE		0x0008
#define	SYSCTL0_READ_WRITE	0x0010
#define	SYSCTL1_READ0		0x0020
#define	SYSCTL1_READ1		0x0040
#define	SYSCTL1_READ2		0x0080
#define	SYSCTL1_WRITE		0x0100
#define	SYSCTL1_READ_WRITE	0x0200

static unsigned int
runtest(cap_channel_t *capsysctl)
{
	unsigned int result;
	int oldvalue, newvalue;
	size_t oldsize;

	result = 0;

	oldsize = sizeof(oldvalue);
	if (cap_sysctlbyname(capsysctl, SYSCTL0_NAME, &oldvalue, &oldsize,
	    NULL, 0) == 0) {
		if (oldsize == sizeof(oldvalue))
			result |= SYSCTL0_READ0;
	}

	newvalue = 123;
	if (cap_sysctlbyname(capsysctl, SYSCTL0_NAME, NULL, NULL, &newvalue,
	    sizeof(newvalue)) == 0) {
		result |= SYSCTL0_WRITE;
	}

	if ((result & SYSCTL0_WRITE) != 0) {
		oldsize = sizeof(oldvalue);
		if (cap_sysctlbyname(capsysctl, SYSCTL0_NAME, &oldvalue,
		    &oldsize, NULL, 0) == 0) {
			if (oldsize == sizeof(oldvalue) && oldvalue == 123)
				result |= SYSCTL0_READ1;
		}
	}

	oldsize = sizeof(oldvalue);
	newvalue = 4567;
	if (cap_sysctlbyname(capsysctl, SYSCTL0_NAME, &oldvalue, &oldsize,
	    &newvalue, sizeof(newvalue)) == 0) {
		if (oldsize == sizeof(oldvalue) && oldvalue == 123)
			result |= SYSCTL0_READ_WRITE;
	}

	if ((result & SYSCTL0_READ_WRITE) != 0) {
		oldsize = sizeof(oldvalue);
		if (cap_sysctlbyname(capsysctl, SYSCTL0_NAME, &oldvalue,
		    &oldsize, NULL, 0) == 0) {
			if (oldsize == sizeof(oldvalue) && oldvalue == 4567)
				result |= SYSCTL0_READ2;
		}
	}

	oldsize = sizeof(oldvalue);
	if (cap_sysctlbyname(capsysctl, SYSCTL1_NAME, &oldvalue, &oldsize,
	    NULL, 0) == 0) {
		if (oldsize == sizeof(oldvalue))
			result |= SYSCTL1_READ0;
	}

	newvalue = 506;
	if (cap_sysctlbyname(capsysctl, SYSCTL1_NAME, NULL, NULL, &newvalue,
	    sizeof(newvalue)) == 0) {
		result |= SYSCTL1_WRITE;
	}

	if ((result & SYSCTL1_WRITE) != 0) {
		oldsize = sizeof(oldvalue);
		if (cap_sysctlbyname(capsysctl, SYSCTL1_NAME, &oldvalue,
		    &oldsize, NULL, 0) == 0) {
			if (oldsize == sizeof(oldvalue) && oldvalue == 506)
				result |= SYSCTL1_READ1;
		}
	}

	oldsize = sizeof(oldvalue);
	newvalue = 7008;
	if (cap_sysctlbyname(capsysctl, SYSCTL1_NAME, &oldvalue, &oldsize,
	    &newvalue, sizeof(newvalue)) == 0) {
		if (oldsize == sizeof(oldvalue) && oldvalue == 506)
			result |= SYSCTL1_READ_WRITE;
	}

	if ((result & SYSCTL1_READ_WRITE) != 0) {
		oldsize = sizeof(oldvalue);
		if (cap_sysctlbyname(capsysctl, SYSCTL1_NAME, &oldvalue,
		    &oldsize, NULL, 0) == 0) {
			if (oldsize == sizeof(oldvalue) && oldvalue == 7008)
				result |= SYSCTL1_READ2;
		}
	}

	return (result);
}

static void
test_operation(cap_channel_t *origcapsysctl)
{
	cap_channel_t *capsysctl;
	nvlist_t *limits;

	/*
	 * Allow:
	 * SYSCTL0_PARENT/RDWR/RECURSIVE
	 * SYSCTL1_PARENT/RDWR/RECURSIVE
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == 0);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, "foo.bar",
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, "foo.bar",
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);

	CHECK(runtest(capsysctl) == (SYSCTL0_READ0 | SYSCTL0_READ1 |
	    SYSCTL0_READ2 | SYSCTL0_WRITE | SYSCTL0_READ_WRITE |
	    SYSCTL1_READ0 | SYSCTL1_READ1 | SYSCTL1_READ2 | SYSCTL1_WRITE |
	    SYSCTL1_READ_WRITE));

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_NAME,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == 0);

	CHECK(runtest(capsysctl) == (SYSCTL0_READ0 | SYSCTL0_READ1 |
	    SYSCTL0_READ2 | SYSCTL0_WRITE | SYSCTL0_READ_WRITE |
	    SYSCTL1_READ0 | SYSCTL1_READ1 | SYSCTL1_READ2 | SYSCTL1_WRITE |
	    SYSCTL1_READ_WRITE));

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_NAME,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == 0);

	CHECK(runtest(capsysctl) == (SYSCTL0_READ0 | SYSCTL1_WRITE));

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME, CAP_SYSCTL_READ);
	nvlist_add_number(limits, SYSCTL1_NAME, CAP_SYSCTL_WRITE);
	CHECK(cap_limit_set(capsysctl, limits) == 0);

	CHECK(runtest(capsysctl) == (SYSCTL0_READ0 | SYSCTL1_WRITE));

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME, CAP_SYSCTL_READ);
	CHECK(cap_limit_set(capsysctl, limits) == 0);

	CHECK(runtest(capsysctl) == SYSCTL0_READ0);

	cap_close(capsysctl);

	/*
	 * Allow:
	 * SYSCTL0_NAME/RDWR/RECURSIVE
	 * SYSCTL1_NAME/RDWR/RECURSIVE
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_NAME,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == 0);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT, CAP_SYSCTL_RDWR);
	nvlist_add_number(limits, SYSCTL1_PARENT, CAP_SYSCTL_RDWR);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT, CAP_SYSCTL_READ);
	nvlist_add_number(limits, SYSCTL1_PARENT, CAP_SYSCTL_WRITE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT, CAP_SYSCTL_READ);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);

	CHECK(runtest(capsysctl) == (SYSCTL0_READ0 | SYSCTL0_READ1 |
	    SYSCTL0_READ2 | SYSCTL0_WRITE | SYSCTL0_READ_WRITE |
	    SYSCTL1_READ0 | SYSCTL1_READ1 | SYSCTL1_READ2 | SYSCTL1_WRITE |
	    SYSCTL1_READ_WRITE));

	cap_close(capsysctl);

	/*
	 * Allow:
	 * SYSCTL0_PARENT/RDWR
	 * SYSCTL1_PARENT/RDWR
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT, CAP_SYSCTL_RDWR);
	nvlist_add_number(limits, SYSCTL1_PARENT, CAP_SYSCTL_RDWR);
	CHECK(cap_limit_set(capsysctl, limits) == 0);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);

	CHECK(runtest(capsysctl) == 0);

	cap_close(capsysctl);

	/*
	 * Allow:
	 * SYSCTL0_NAME/RDWR
	 * SYSCTL1_NAME/RDWR
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME, CAP_SYSCTL_RDWR);
	nvlist_add_number(limits, SYSCTL1_NAME, CAP_SYSCTL_RDWR);
	CHECK(cap_limit_set(capsysctl, limits) == 0);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);

	CHECK(runtest(capsysctl) == (SYSCTL0_READ0 | SYSCTL0_READ1 |
	    SYSCTL0_READ2 | SYSCTL0_WRITE | SYSCTL0_READ_WRITE |
	    SYSCTL1_READ0 | SYSCTL1_READ1 | SYSCTL1_READ2 | SYSCTL1_WRITE |
	    SYSCTL1_READ_WRITE));

	cap_close(capsysctl);

	/*
	 * Allow:
	 * SYSCTL0_PARENT/RDWR
	 * SYSCTL1_PARENT/RDWR/RECURSIVE
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT, CAP_SYSCTL_RDWR);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == 0);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);

	CHECK(runtest(capsysctl) == (SYSCTL1_READ0 | SYSCTL1_READ1 |
	    SYSCTL1_READ2 | SYSCTL1_WRITE | SYSCTL1_READ_WRITE));

	cap_close(capsysctl);

	/*
	 * Allow:
	 * SYSCTL0_NAME/RDWR
	 * SYSCTL1_NAME/RDWR/RECURSIVE
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME, CAP_SYSCTL_RDWR);
	nvlist_add_number(limits, SYSCTL1_NAME,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == 0);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_NAME,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);

	CHECK(runtest(capsysctl) == (SYSCTL0_READ0 | SYSCTL0_READ1 |
	    SYSCTL0_READ2 | SYSCTL0_WRITE | SYSCTL0_READ_WRITE |
	    SYSCTL1_READ0 | SYSCTL1_READ1 | SYSCTL1_READ2 | SYSCTL1_WRITE |
	    SYSCTL1_READ_WRITE));

	cap_close(capsysctl);

	/*
	 * Allow:
	 * SYSCTL0_PARENT/READ/RECURSIVE
	 * SYSCTL1_PARENT/READ/RECURSIVE
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == 0);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT, CAP_SYSCTL_RDWR);
	nvlist_add_number(limits, SYSCTL1_PARENT, CAP_SYSCTL_RDWR);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT, CAP_SYSCTL_WRITE);
	nvlist_add_number(limits, SYSCTL1_PARENT, CAP_SYSCTL_WRITE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL1_PARENT, CAP_SYSCTL_RDWR);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT, CAP_SYSCTL_WRITE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);

	CHECK(runtest(capsysctl) == (SYSCTL0_READ0 | SYSCTL1_READ0));

	cap_close(capsysctl);

	/*
	 * Allow:
	 * SYSCTL0_NAME/READ/RECURSIVE
	 * SYSCTL1_NAME/READ/RECURSIVE
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_NAME,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == 0);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_NAME,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_NAME,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME, CAP_SYSCTL_RDWR);
	nvlist_add_number(limits, SYSCTL1_NAME, CAP_SYSCTL_RDWR);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME, CAP_SYSCTL_WRITE);
	nvlist_add_number(limits, SYSCTL1_NAME, CAP_SYSCTL_WRITE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL1_NAME,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL1_NAME, CAP_SYSCTL_RDWR);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME, CAP_SYSCTL_WRITE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);

	CHECK(runtest(capsysctl) == (SYSCTL0_READ0 | SYSCTL1_READ0));

	cap_close(capsysctl);

	/*
	 * Allow:
	 * SYSCTL0_PARENT/READ
	 * SYSCTL1_PARENT/READ
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT, CAP_SYSCTL_READ);
	nvlist_add_number(limits, SYSCTL1_PARENT, CAP_SYSCTL_READ);
	CHECK(cap_limit_set(capsysctl, limits) == 0);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT, CAP_SYSCTL_RDWR);
	nvlist_add_number(limits, SYSCTL1_PARENT, CAP_SYSCTL_RDWR);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT, CAP_SYSCTL_WRITE);
	nvlist_add_number(limits, SYSCTL1_PARENT, CAP_SYSCTL_WRITE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL1_PARENT, CAP_SYSCTL_RDWR);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT, CAP_SYSCTL_WRITE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);

	CHECK(runtest(capsysctl) == 0);

	cap_close(capsysctl);

	/*
	 * Allow:
	 * SYSCTL0_NAME/READ
	 * SYSCTL1_NAME/READ
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME, CAP_SYSCTL_READ);
	nvlist_add_number(limits, SYSCTL1_NAME, CAP_SYSCTL_READ);
	CHECK(cap_limit_set(capsysctl, limits) == 0);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_NAME,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_NAME,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_NAME,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME, CAP_SYSCTL_RDWR);
	nvlist_add_number(limits, SYSCTL1_NAME, CAP_SYSCTL_RDWR);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME, CAP_SYSCTL_WRITE);
	nvlist_add_number(limits, SYSCTL1_NAME, CAP_SYSCTL_WRITE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL1_NAME,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL1_NAME, CAP_SYSCTL_RDWR);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME, CAP_SYSCTL_WRITE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);

	CHECK(runtest(capsysctl) == (SYSCTL0_READ0 | SYSCTL1_READ0));

	cap_close(capsysctl);

	/*
	 * Allow:
	 * SYSCTL0_PARENT/READ
	 * SYSCTL1_PARENT/READ/RECURSIVE
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT, CAP_SYSCTL_READ);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == 0);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_PARENT, CAP_SYSCTL_READ);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);

	CHECK(runtest(capsysctl) == SYSCTL1_READ0);

	cap_close(capsysctl);

	/*
	 * Allow:
	 * SYSCTL0_NAME/READ
	 * SYSCTL1_NAME/READ/RECURSIVE
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME, CAP_SYSCTL_READ);
	nvlist_add_number(limits, SYSCTL1_NAME,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == 0);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_NAME, CAP_SYSCTL_READ);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);

	CHECK(runtest(capsysctl) == (SYSCTL0_READ0 | SYSCTL1_READ0));

	cap_close(capsysctl);

	/*
	 * Allow:
	 * SYSCTL0_PARENT/WRITE/RECURSIVE
	 * SYSCTL1_PARENT/WRITE/RECURSIVE
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == 0);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT, CAP_SYSCTL_RDWR);
	nvlist_add_number(limits, SYSCTL1_PARENT, CAP_SYSCTL_RDWR);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT, CAP_SYSCTL_READ);
	nvlist_add_number(limits, SYSCTL1_PARENT, CAP_SYSCTL_READ);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL1_PARENT, CAP_SYSCTL_RDWR);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT, CAP_SYSCTL_READ);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);

	CHECK(runtest(capsysctl) == (SYSCTL0_WRITE | SYSCTL1_WRITE));

	cap_close(capsysctl);

	/*
	 * Allow:
	 * SYSCTL0_NAME/WRITE/RECURSIVE
	 * SYSCTL1_NAME/WRITE/RECURSIVE
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_NAME,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == 0);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_NAME,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_NAME,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME, CAP_SYSCTL_RDWR);
	nvlist_add_number(limits, SYSCTL1_NAME, CAP_SYSCTL_RDWR);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME, CAP_SYSCTL_READ);
	nvlist_add_number(limits, SYSCTL1_NAME, CAP_SYSCTL_READ);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL1_NAME,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL1_NAME, CAP_SYSCTL_RDWR);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME, CAP_SYSCTL_READ);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);

	CHECK(runtest(capsysctl) == (SYSCTL0_WRITE | SYSCTL1_WRITE));

	cap_close(capsysctl);

	/*
	 * Allow:
	 * SYSCTL0_PARENT/WRITE
	 * SYSCTL1_PARENT/WRITE
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT, CAP_SYSCTL_WRITE);
	nvlist_add_number(limits, SYSCTL1_PARENT, CAP_SYSCTL_WRITE);
	CHECK(cap_limit_set(capsysctl, limits) == 0);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT, CAP_SYSCTL_RDWR);
	nvlist_add_number(limits, SYSCTL1_PARENT, CAP_SYSCTL_RDWR);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT, CAP_SYSCTL_READ);
	nvlist_add_number(limits, SYSCTL1_PARENT, CAP_SYSCTL_READ);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL1_PARENT, CAP_SYSCTL_RDWR);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT, CAP_SYSCTL_READ);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);

	CHECK(runtest(capsysctl) == 0);

	cap_close(capsysctl);

	/*
	 * Allow:
	 * SYSCTL0_NAME/WRITE
	 * SYSCTL1_NAME/WRITE
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME, CAP_SYSCTL_WRITE);
	nvlist_add_number(limits, SYSCTL1_NAME, CAP_SYSCTL_WRITE);
	CHECK(cap_limit_set(capsysctl, limits) == 0);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_NAME,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_NAME,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_NAME,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME, CAP_SYSCTL_RDWR);
	nvlist_add_number(limits, SYSCTL1_NAME, CAP_SYSCTL_RDWR);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME, CAP_SYSCTL_READ);
	nvlist_add_number(limits, SYSCTL1_NAME, CAP_SYSCTL_READ);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL1_NAME,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL1_NAME, CAP_SYSCTL_RDWR);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME, CAP_SYSCTL_READ);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);

	CHECK(runtest(capsysctl) == (SYSCTL0_WRITE | SYSCTL1_WRITE));

	cap_close(capsysctl);

	/*
	 * Allow:
	 * SYSCTL0_PARENT/WRITE
	 * SYSCTL1_PARENT/WRITE/RECURSIVE
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT, CAP_SYSCTL_WRITE);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == 0);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_PARENT, CAP_SYSCTL_WRITE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);

	CHECK(runtest(capsysctl) == SYSCTL1_WRITE);

	cap_close(capsysctl);

	/*
	 * Allow:
	 * SYSCTL0_NAME/WRITE
	 * SYSCTL1_NAME/WRITE/RECURSIVE
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME, CAP_SYSCTL_WRITE);
	nvlist_add_number(limits, SYSCTL1_NAME,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == 0);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_NAME, CAP_SYSCTL_WRITE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);

	CHECK(runtest(capsysctl) == (SYSCTL0_WRITE | SYSCTL1_WRITE));

	cap_close(capsysctl);

	/*
	 * Allow:
	 * SYSCTL0_PARENT/READ/RECURSIVE
	 * SYSCTL1_PARENT/WRITE/RECURSIVE
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == 0);

	CHECK(runtest(capsysctl) == (SYSCTL0_READ0 | SYSCTL1_WRITE));

	cap_close(capsysctl);

	/*
	 * Allow:
	 * SYSCTL0_NAME/READ/RECURSIVE
	 * SYSCTL1_NAME/WRITE/RECURSIVE
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_NAME,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == 0);

	CHECK(runtest(capsysctl) == (SYSCTL0_READ0 | SYSCTL1_WRITE));

	cap_close(capsysctl);

	/*
	 * Allow:
	 * SYSCTL0_PARENT/READ
	 * SYSCTL1_PARENT/WRITE
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT, CAP_SYSCTL_READ);
	nvlist_add_number(limits, SYSCTL1_PARENT, CAP_SYSCTL_WRITE);
	CHECK(cap_limit_set(capsysctl, limits) == 0);

	CHECK(runtest(capsysctl) == 0);

	cap_close(capsysctl);

	/*
	 * Allow:
	 * SYSCTL0_NAME/READ
	 * SYSCTL1_NAME/WRITE
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME, CAP_SYSCTL_READ);
	nvlist_add_number(limits, SYSCTL1_NAME, CAP_SYSCTL_WRITE);
	CHECK(cap_limit_set(capsysctl, limits) == 0);

	CHECK(runtest(capsysctl) == (SYSCTL0_READ0 | SYSCTL1_WRITE));

	cap_close(capsysctl);

	/*
	 * Allow:
	 * SYSCTL0_PARENT/READ
	 * SYSCTL1_PARENT/WRITE/RECURSIVE
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT, CAP_SYSCTL_READ);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == 0);

	CHECK(runtest(capsysctl) == SYSCTL1_WRITE);

	cap_close(capsysctl);

	/*
	 * Allow:
	 * SYSCTL0_NAME/READ
	 * SYSCTL1_NAME/WRITE/RECURSIVE
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME, CAP_SYSCTL_READ);
	nvlist_add_number(limits, SYSCTL1_NAME,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == 0);

	CHECK(runtest(capsysctl) == (SYSCTL0_READ0 | SYSCTL1_WRITE));

	cap_close(capsysctl);
}

static void
test_names(cap_channel_t *origcapsysctl)
{
	cap_channel_t *capsysctl;
	nvlist_t *limits;

	/*
	 * Allow:
	 * SYSCTL0_PARENT/READ/RECURSIVE
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == 0);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT, CAP_SYSCTL_READ);
	nvlist_add_number(limits, SYSCTL1_PARENT, CAP_SYSCTL_READ);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL1_PARENT, CAP_SYSCTL_READ);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);

	CHECK(runtest(capsysctl) == SYSCTL0_READ0);

	cap_close(capsysctl);

	/*
	 * Allow:
	 * SYSCTL1_NAME/READ/RECURSIVE
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL1_NAME,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == 0);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_NAME,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME,
	    CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME, CAP_SYSCTL_READ);
	nvlist_add_number(limits, SYSCTL1_NAME, CAP_SYSCTL_READ);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME, CAP_SYSCTL_READ);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);

	CHECK(runtest(capsysctl) == SYSCTL1_READ0);

	cap_close(capsysctl);

	/*
	 * Allow:
	 * SYSCTL0_PARENT/WRITE/RECURSIVE
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == 0);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT, CAP_SYSCTL_WRITE);
	nvlist_add_number(limits, SYSCTL1_PARENT, CAP_SYSCTL_WRITE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL1_PARENT, CAP_SYSCTL_WRITE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);

	CHECK(runtest(capsysctl) == SYSCTL0_WRITE);

	cap_close(capsysctl);

	/*
	 * Allow:
	 * SYSCTL1_NAME/WRITE/RECURSIVE
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL1_NAME,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == 0);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_NAME,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME,
	    CAP_SYSCTL_WRITE | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME, CAP_SYSCTL_WRITE);
	nvlist_add_number(limits, SYSCTL1_NAME, CAP_SYSCTL_WRITE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME, CAP_SYSCTL_WRITE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);

	CHECK(runtest(capsysctl) == SYSCTL1_WRITE);

	cap_close(capsysctl);

	/*
	 * Allow:
	 * SYSCTL0_PARENT/RDWR/RECURSIVE
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == 0);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL1_PARENT,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT, CAP_SYSCTL_READ);
	nvlist_add_number(limits, SYSCTL1_PARENT, CAP_SYSCTL_READ);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL1_PARENT, CAP_SYSCTL_READ);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);

	CHECK(runtest(capsysctl) == (SYSCTL0_READ0 | SYSCTL0_READ1 |
	    SYSCTL0_READ2 | SYSCTL0_WRITE | SYSCTL0_READ_WRITE));

	cap_close(capsysctl);

	/*
	 * Allow:
	 * SYSCTL1_NAME/RDWR/RECURSIVE
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL1_NAME,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == 0);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	nvlist_add_number(limits, SYSCTL1_NAME,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME,
	    CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME, CAP_SYSCTL_READ);
	nvlist_add_number(limits, SYSCTL1_NAME, CAP_SYSCTL_READ);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME, CAP_SYSCTL_READ);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);

	CHECK(runtest(capsysctl) == (SYSCTL1_READ0 | SYSCTL1_READ1 |
	    SYSCTL1_READ2 | SYSCTL1_WRITE | SYSCTL1_READ_WRITE));

	cap_close(capsysctl);

	/*
	 * Allow:
	 * SYSCTL0_PARENT/READ
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT, CAP_SYSCTL_READ);
	CHECK(cap_limit_set(capsysctl, limits) == 0);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT, CAP_SYSCTL_READ);
	nvlist_add_number(limits, SYSCTL1_PARENT, CAP_SYSCTL_READ);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL1_PARENT, CAP_SYSCTL_READ);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);

	CHECK(runtest(capsysctl) == 0);

	cap_close(capsysctl);

	/*
	 * Allow:
	 * SYSCTL1_NAME/READ
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL1_NAME, CAP_SYSCTL_READ);
	CHECK(cap_limit_set(capsysctl, limits) == 0);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME, CAP_SYSCTL_READ);
	nvlist_add_number(limits, SYSCTL1_NAME, CAP_SYSCTL_READ);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME, CAP_SYSCTL_READ);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);

	CHECK(runtest(capsysctl) == SYSCTL1_READ0);

	cap_close(capsysctl);

	/*
	 * Allow:
	 * SYSCTL0_PARENT/WRITE
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT, CAP_SYSCTL_WRITE);
	CHECK(cap_limit_set(capsysctl, limits) == 0);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT, CAP_SYSCTL_WRITE);
	nvlist_add_number(limits, SYSCTL1_PARENT, CAP_SYSCTL_WRITE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL1_PARENT, CAP_SYSCTL_WRITE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);

	CHECK(runtest(capsysctl) == 0);

	cap_close(capsysctl);

	/*
	 * Allow:
	 * SYSCTL1_NAME/WRITE
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL1_NAME, CAP_SYSCTL_WRITE);
	CHECK(cap_limit_set(capsysctl, limits) == 0);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME, CAP_SYSCTL_WRITE);
	nvlist_add_number(limits, SYSCTL1_NAME, CAP_SYSCTL_WRITE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME, CAP_SYSCTL_WRITE);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);

	CHECK(runtest(capsysctl) == SYSCTL1_WRITE);

	cap_close(capsysctl);

	/*
	 * Allow:
	 * SYSCTL0_PARENT/RDWR
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT, CAP_SYSCTL_RDWR);
	CHECK(cap_limit_set(capsysctl, limits) == 0);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_PARENT, CAP_SYSCTL_RDWR);
	nvlist_add_number(limits, SYSCTL1_PARENT, CAP_SYSCTL_RDWR);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL1_PARENT, CAP_SYSCTL_RDWR);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);

	CHECK(runtest(capsysctl) == 0);

	cap_close(capsysctl);

	/*
	 * Allow:
	 * SYSCTL1_NAME/RDWR
	 */

	capsysctl = cap_clone(origcapsysctl);
	CHECK(capsysctl != NULL);

	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL1_NAME, CAP_SYSCTL_RDWR);
	CHECK(cap_limit_set(capsysctl, limits) == 0);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME, CAP_SYSCTL_RDWR);
	nvlist_add_number(limits, SYSCTL1_NAME, CAP_SYSCTL_RDWR);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);
	limits = nvlist_create(0);
	nvlist_add_number(limits, SYSCTL0_NAME, CAP_SYSCTL_RDWR);
	CHECK(cap_limit_set(capsysctl, limits) == -1 && errno == ENOTCAPABLE);

	CHECK(runtest(capsysctl) == (SYSCTL1_READ0 | SYSCTL1_READ1 |
	    SYSCTL1_READ2 | SYSCTL1_WRITE | SYSCTL1_READ_WRITE));

	cap_close(capsysctl);
}

int
main(void)
{
	cap_channel_t *capcas, *capsysctl;
	int scvalue0, scvalue1;
	size_t scsize;

	printf("1..256\n");
	fflush(stdout);

	scsize = sizeof(scvalue0);
	CHECKX(sysctlbyname(SYSCTL0_NAME, &scvalue0, &scsize, NULL, 0) == 0);
	CHECKX(scsize == sizeof(scvalue0));
	scsize = sizeof(scvalue1);
	CHECKX(sysctlbyname(SYSCTL1_NAME, &scvalue1, &scsize, NULL, 0) == 0);
	CHECKX(scsize == sizeof(scvalue1));

	capcas = cap_init();
	CHECKX(capcas != NULL);

	capsysctl = cap_service_open(capcas, "system.sysctl");
	CHECKX(capsysctl != NULL);

	cap_close(capcas);

	/* No limits set. */

	CHECK(runtest(capsysctl) == (SYSCTL0_READ0 | SYSCTL0_READ1 |
	    SYSCTL0_READ2 | SYSCTL0_WRITE | SYSCTL0_READ_WRITE |
	    SYSCTL1_READ0 | SYSCTL1_READ1 | SYSCTL1_READ2 | SYSCTL1_WRITE |
	    SYSCTL1_READ_WRITE));

	test_operation(capsysctl);

	test_names(capsysctl);

	cap_close(capsysctl);

	CHECK(sysctlbyname(SYSCTL0_NAME, NULL, NULL, &scvalue0,
	    sizeof(scvalue0)) == 0);
	CHECK(sysctlbyname(SYSCTL1_NAME, NULL, NULL, &scvalue1,
	    sizeof(scvalue1)) == 0);

	exit(0);
}
