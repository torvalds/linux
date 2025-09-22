/*	$OpenBSD: t_mlock.c,v 1.3 2021/12/13 16:56:48 deraadt Exp $	*/
/* $NetBSD: t_mlock.c,v 1.8 2020/01/24 08:45:16 skrll Exp $ */

/*-
 * Copyright (c) 2012 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jukka Ruohonen.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "macros.h"

#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <sys/wait.h>

#include <errno.h>
#include "atf-c.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static long page = 0;

ATF_TC(mlock_clip);
ATF_TC_HEAD(mlock_clip, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test with mlock(2) that UVM only "
	    "clips if the clip address is within the entry (PR kern/44788)");
}

ATF_TC_BODY(mlock_clip, tc)
{
	void *buf;
	int err1, err2;

	buf = malloc(page);
	ATF_REQUIRE(buf != NULL);
	fprintf(stderr, "mlock_clip: buf = %p (page=%ld)\n", buf, page);

	if (page < 1024)
		atf_tc_skip("page size too small");

	for (size_t i = page; i >= 1; i = i - 1024) {
		err1 = mlock(buf, page - i);
		if (err1 != 0)
			fprintf(stderr, "mlock_clip: page=%ld i=%zu,"
			    " mlock(%p, %ld): %s\n", page, i, buf, page - i,
			    strerror(errno));
		err2 = munlock(buf, page - i);
		if (err2 != 0)
			fprintf(stderr, "mlock_clip: page=%ld i=%zu,"
			    " munlock(%p, %ld): %s (mlock %s)\n", page, i,
			    buf, page - i, strerror(errno), err1?"failed":"ok");
	}

	free(buf);
}

ATF_TC(mlock_err);
ATF_TC_HEAD(mlock_err, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test error conditions in mlock(2) and munlock(2)");
}

ATF_TC_BODY(mlock_err, tc)
{
	void *invalid_ptr;
	void *buf;
	int mlock_err, munlock_err;

	/*
	 * Any bad address must return ENOMEM (for lock & unlock)
	 */
	errno = 0;
	ATF_REQUIRE_ERRNO(ENOMEM, mlock(NULL, page) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(ENOMEM, mlock((char *)0, page) == -1);

	errno = 0;
#ifdef __OpenBSD__
	ATF_REQUIRE_ERRNO(EINVAL, mlock((char *)-1, page) == -1);
#else
	ATF_REQUIRE_ERRNO(ENOMEM, mlock((char *)-1, page) == -1);
#endif

	errno = 0;
	ATF_REQUIRE_ERRNO(ENOMEM, munlock(NULL, page) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(ENOMEM, munlock((char *)0, page) == -1);

	errno = 0;
#ifdef __OpenBSD__
	ATF_REQUIRE_ERRNO(EINVAL, munlock((char *)-1, page) == -1);
#else
	ATF_REQUIRE_ERRNO(ENOMEM, munlock((char *)-1, page) == -1);
#endif

	buf = malloc(page);
	ATF_REQUIRE(buf != NULL);
	fprintf(stderr, "mlock_err: buf = %p (page=%ld)\n", buf, page);

	/*
	 * unlocking memory that is not locked is an error...
	 */

#ifndef __OpenBSD__
	errno = 0;
	ATF_REQUIRE_ERRNO(ENOMEM, munlock(buf, page) == -1);
#endif

	/*
	 * These are permitted to fail (EINVAL) but do not on NetBSD
	 */
	mlock_err = mlock((void *)(((uintptr_t)buf) + page/3), page/5);
	if (mlock_err != 0)
	    fprintf(stderr, "mlock_err: mlock(%p, %ld): %d [%d] %s\n",
		(void *)(((uintptr_t)buf) + page/3), page/5, mlock_err,
		errno, strerror(errno));
	ATF_REQUIRE(mlock_err == 0);
	munlock_err= munlock((void *)(((uintptr_t)buf) + page/3), page/5);
	if (munlock_err != 0)
	    fprintf(stderr, "mlock_err: munlock(%p, %ld): %d [%d] %s\n",
		(void *)(((uintptr_t)buf) + page/3), page/5, munlock_err,
		errno, strerror(errno));
	ATF_REQUIRE(munlock_err == 0);

	(void)free(buf);

	/*
	 * Try to create a pointer to an unmapped page - first after current
	 * brk will likely do.
	 */
	invalid_ptr = (void*)(((uintptr_t)sbrk(0)+page) & ~(page-1));
	printf("testing with (hopefully) invalid pointer %p\n", invalid_ptr);

	errno = 0;
	ATF_REQUIRE_ERRNO(ENOMEM, mlock(invalid_ptr, page) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(ENOMEM, munlock(invalid_ptr, page) == -1);
}

ATF_TC(mlock_limits);
ATF_TC_HEAD(mlock_limits, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test system limits with mlock(2)");
}

ATF_TC_BODY(mlock_limits, tc)
{
	struct rlimit res;
	void *buf;
	pid_t pid;
	int sta;

	buf = malloc(page);
	ATF_REQUIRE(buf != NULL);
	fprintf(stderr, "mlock_limits: buf = %p (page=%ld)\n", buf, page);

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {

		for (ssize_t i = page; i >= 2; i -= 100) {

			res.rlim_cur = i - 1;
			res.rlim_max = i - 1;

			(void)fprintf(stderr, "trying to lock %zu bytes "
			    "with %zu byte limit\n", i, (size_t)res.rlim_cur);

			if (setrlimit(RLIMIT_MEMLOCK, &res) != 0)
				_exit(EXIT_FAILURE);

			errno = 0;

			if ((sta = mlock(buf, i)) != -1 || errno != EAGAIN) {
				fprintf(stderr, "mlock(%p, %zu): %d [%d] %s\n",
				    buf, i, sta, errno, strerror(errno));
				(void)munlock(buf, i);
				_exit(EXIT_FAILURE);
			}
		}

		_exit(EXIT_SUCCESS);
	}

	(void)wait(&sta);

	if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != EXIT_SUCCESS)
		atf_tc_fail("mlock(2) locked beyond system limits");

	free(buf);
}

ATF_TC(mlock_mmap);
ATF_TC_HEAD(mlock_mmap, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test mlock(2)-mmap(2) interaction");
}

ATF_TC_BODY(mlock_mmap, tc)
{
#ifdef __OpenBSD__
	static const int flags = MAP_ANON | MAP_PRIVATE;
#else
	static const int flags = MAP_ANON | MAP_PRIVATE | MAP_WIRED;
#endif
	void *buf;

	/*
	 * Make a wired RW mapping and check that mlock(2)
	 * does not fail for the (already locked) mapping.
	 */
	buf = mmap(NULL, page, PROT_READ | PROT_WRITE, flags, -1, 0);

	if (buf == MAP_FAILED)
		fprintf(stderr,
		    "mlock_mmap: mmap(NULL, %ld, %#x, %#x, -1, 0): MAP_FAILED"
		    " [%d] %s\n", page, PROT_READ | PROT_WRITE, flags, errno,
		    strerror(errno));

	ATF_REQUIRE(buf != MAP_FAILED);

	fprintf(stderr, "mlock_mmap: buf=%p, page=%ld\n", buf, page);

	ATF_REQUIRE(mlock(buf, page) == 0);
	ATF_REQUIRE(munlock(buf, page) == 0);
	ATF_REQUIRE(munmap(buf, page) == 0);
	ATF_REQUIRE(munlock(buf, page) != 0);

	fprintf(stderr, "mlock_mmap: first test succeeded\n");

	/*
	 * But it should be impossible to mlock(2) a PROT_NONE mapping.
	 */
	buf = mmap(NULL, page, PROT_NONE, flags, -1, 0);

	if (buf == MAP_FAILED)
		fprintf(stderr,
		    "mlock_mmap: mmap(NULL, %ld, %#x, %#x, -1, 0): MAP_FAILED"
		    " [%d] %s\n", page, PROT_NONE, flags, errno,
		    strerror(errno));

	ATF_REQUIRE(buf != MAP_FAILED);
	ATF_REQUIRE(mlock(buf, page) != 0);
	ATF_REQUIRE(munmap(buf, page) == 0);

	fprintf(stderr, "mlock_mmap: second test succeeded\n");
}

ATF_TC(mlock_nested);
ATF_TC_HEAD(mlock_nested, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test that consecutive mlock(2) calls succeed");
}

ATF_TC_BODY(mlock_nested, tc)
{
	const size_t maxiter = 100;
	void *buf;
	int err;

	buf = malloc(page);
	ATF_REQUIRE(buf != NULL);
	fprintf(stderr, "mlock_nested: buf = %p (page=%ld)\n", buf, page);

	for (size_t i = 0; i < maxiter; i++) {
		err = mlock(buf, page);
		if (err != 0)
		    fprintf(stderr,
		    "mlock_nested: i=%zu (of %zu) mlock(%p, %ld): %d [%d] %s\n",
			i, maxiter, buf, page, err, errno, strerror(errno));
		ATF_REQUIRE(err == 0);
	}

	err = munlock(buf, page);
	if (err != 0)
		fprintf(stderr, "mlock_nested: munlock(%p, %ld): %d [%d] %s\n",
		    buf, page, err, errno, strerror(errno));
	ATF_REQUIRE(err == 0);
	free(buf);
}

ATF_TP_ADD_TCS(tp)
{

	page = sysconf(_SC_PAGESIZE);
	ATF_REQUIRE(page >= 0);

	ATF_TP_ADD_TC(tp, mlock_clip);
	ATF_TP_ADD_TC(tp, mlock_err);
	ATF_TP_ADD_TC(tp, mlock_limits);
	ATF_TP_ADD_TC(tp, mlock_mmap);
	ATF_TP_ADD_TC(tp, mlock_nested);

	return atf_no_error();
}
