/*-
 * Copyright (c) 2009	Simon L. Nielsen <simon@FreeBSD.org>,
 * 			Bjoern A. Zeeb <bz@FreeBSD.org>
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

#include <sys/param.h>
#include <sys/mman.h>
#include <sys/sysctl.h>

#include <atf-c.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static const struct {
	void	*addr;
	int	ok[2];	/* Depending on security.bsd.map_at_zero {0, !=0}. */
} map_at_zero_tests[] = {
	{ (void *)0,			{ 0, 1 } }, /* Test sysctl. */
	{ (void *)1,			{ 0, 0 } },
	{ (void *)(PAGE_SIZE - 1),	{ 0, 0 } },
	{ (void *)PAGE_SIZE,		{ 1, 1 } },
	{ (void *)-1,			{ 0, 0 } },
	{ (void *)(-PAGE_SIZE),		{ 0, 0 } },
	{ (void *)(-1 - PAGE_SIZE),	{ 0, 0 } },
	{ (void *)(-1 - PAGE_SIZE - 1),	{ 0, 0 } },
	{ (void *)(0x1000 * PAGE_SIZE),	{ 1, 1 } },
};

#define	MAP_AT_ZERO	"security.bsd.map_at_zero"

ATF_TC_WITHOUT_HEAD(mmap__map_at_zero);
ATF_TC_BODY(mmap__map_at_zero, tc)
{
	void *p;
	size_t len;
	unsigned int i;
	int map_at_zero;

	len = sizeof(map_at_zero);
	if (sysctlbyname(MAP_AT_ZERO, &map_at_zero, &len, NULL, 0) == -1) {
		atf_tc_skip("sysctl for %s failed: %s\n", MAP_AT_ZERO,
		    strerror(errno));
		return;
	}

	/* Normalize to 0 or 1 for array access. */
	map_at_zero = !!map_at_zero;

	for (i = 0; i < nitems(map_at_zero_tests); i++) {
		p = mmap((void *)map_at_zero_tests[i].addr, PAGE_SIZE,
		    PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_FIXED,
		    -1, 0);
		if (p == MAP_FAILED) {
			ATF_CHECK_MSG(map_at_zero_tests[i].ok[map_at_zero] == 0,
			    "mmap(%p, ...) failed", map_at_zero_tests[i].addr);
		} else {
			ATF_CHECK_MSG(map_at_zero_tests[i].ok[map_at_zero] == 1,
			    "mmap(%p, ...) succeeded: p=%p\n",
			    map_at_zero_tests[i].addr, p);
		}
	}
}

static void
checked_mmap(int prot, int flags, int fd, int error, const char *msg)
{
	void *p;
	int pagesize;

	ATF_REQUIRE((pagesize = getpagesize()) > 0);
	p = mmap(NULL, pagesize, prot, flags, fd, 0);
	if (p == MAP_FAILED) {
		if (error == 0)
			ATF_CHECK_MSG(0, "%s failed with errno %d", msg,
			    errno);
		else
			ATF_CHECK_EQ_MSG(error, errno,
			    "%s failed with wrong errno %d (expected %d)", msg,
			    errno, error);
	} else {
		ATF_CHECK_MSG(error == 0, "%s succeeded", msg);
		munmap(p, pagesize);
	}
}

ATF_TC_WITHOUT_HEAD(mmap__bad_arguments);
ATF_TC_BODY(mmap__bad_arguments, tc)
{
	int devstatfd, pagesize, shmfd, zerofd;

	ATF_REQUIRE((pagesize = getpagesize()) > 0);
	ATF_REQUIRE((devstatfd = open("/dev/devstat", O_RDONLY)) >= 0);
	ATF_REQUIRE((shmfd = shm_open(SHM_ANON, O_RDWR, 0644)) >= 0);
	ATF_REQUIRE(ftruncate(shmfd, pagesize) == 0);
	ATF_REQUIRE((zerofd = open("/dev/zero", O_RDONLY)) >= 0);

	/* These should work. */
	checked_mmap(PROT_READ | PROT_WRITE, MAP_ANON, -1, 0,
	    "simple MAP_ANON");
	checked_mmap(PROT_READ | PROT_WRITE, MAP_SHARED, shmfd, 0,
	    "simple shm fd shared");
	checked_mmap(PROT_READ | PROT_WRITE, MAP_PRIVATE, shmfd, 0,
	    "simple shm fd private");
	checked_mmap(PROT_READ, MAP_SHARED, zerofd, 0,
	    "simple /dev/zero shared");
	checked_mmap(PROT_READ | PROT_WRITE, MAP_PRIVATE, zerofd, 0,
	    "simple /dev/zero private");
	checked_mmap(PROT_READ, MAP_SHARED, devstatfd, 0,
	    "simple /dev/devstat shared");

	/* Extra PROT flags. */
	checked_mmap(PROT_READ | PROT_WRITE | 0x100000, MAP_ANON, -1, EINVAL,
	    "MAP_ANON with extra PROT flags");
	checked_mmap(0xffff, MAP_SHARED, shmfd, EINVAL,
	    "shm fd with garbage PROT");

	/* Undefined flag. */
	checked_mmap(PROT_READ | PROT_WRITE, MAP_ANON | MAP_RESERVED0080, -1,
	    EINVAL, "Undefined flag");

	/* Both MAP_SHARED and MAP_PRIVATE */
	checked_mmap(PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE |
	    MAP_SHARED, -1, EINVAL, "MAP_ANON with both SHARED and PRIVATE");
	checked_mmap(PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_SHARED, shmfd,
	    EINVAL, "shm fd with both SHARED and PRIVATE");

	/* At least one of MAP_SHARED or MAP_PRIVATE without ANON */
	checked_mmap(PROT_READ | PROT_WRITE, 0, shmfd, EINVAL,
	    "shm fd without sharing flag");

	/* MAP_ANON with either sharing flag (impacts fork). */
	checked_mmap(PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0,
	    "shared MAP_ANON");
	checked_mmap(PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0,
	    "private MAP_ANON");

	/* MAP_ANON should require an fd of -1. */
	checked_mmap(PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, 0, EINVAL,
	    "MAP_ANON with fd != -1");

	/* Writable MAP_SHARED should fail on read-only descriptors. */
	checked_mmap(PROT_READ | PROT_WRITE, MAP_SHARED, zerofd, EACCES,
	    "MAP_SHARED of read-only /dev/zero");

	/*
	 * Character devices other than /dev/zero do not support private
	 * mappings.
	 */
	checked_mmap(PROT_READ, MAP_PRIVATE, devstatfd, EINVAL,
	    "MAP_PRIVATE of /dev/devstat");

	close(devstatfd);
	close(shmfd);
	close(zerofd);
}

ATF_TC_WITHOUT_HEAD(mmap__dev_zero_private);
ATF_TC_BODY(mmap__dev_zero_private, tc)
{
	char *p1, *p2, *p3;
	int fd, i, pagesize;

	ATF_REQUIRE((pagesize = getpagesize()) > 0);
	ATF_REQUIRE((fd = open("/dev/zero", O_RDONLY)) >= 0);

	p1 = mmap(NULL, pagesize, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	ATF_REQUIRE(p1 != MAP_FAILED);

	p2 = mmap(NULL, pagesize, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	ATF_REQUIRE(p2 != MAP_FAILED);

	for (i = 0; i < pagesize; i++)
		ATF_REQUIRE_EQ_MSG(0, p1[i], "byte at p1[%d] is %x", i, p1[i]);

	ATF_REQUIRE(memcmp(p1, p2, pagesize) == 0);

	p1[0] = 1;

	ATF_REQUIRE(p2[0] == 0);

	p2[0] = 2;

	ATF_REQUIRE(p1[0] == 1);

	p3 = mmap(NULL, pagesize, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	ATF_REQUIRE(p3 != MAP_FAILED);

	ATF_REQUIRE(p3[0] == 0);

	munmap(p1, pagesize);
	munmap(p2, pagesize);
	munmap(p3, pagesize);
	close(fd);
}

ATF_TC_WITHOUT_HEAD(mmap__dev_zero_shared);
ATF_TC_BODY(mmap__dev_zero_shared, tc)
{
	char *p1, *p2, *p3;
	int fd, i, pagesize;

	ATF_REQUIRE((pagesize = getpagesize()) > 0);
	ATF_REQUIRE((fd = open("/dev/zero", O_RDWR)) >= 0);

	p1 = mmap(NULL, pagesize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	ATF_REQUIRE(p1 != MAP_FAILED);

	p2 = mmap(NULL, pagesize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	ATF_REQUIRE(p2 != MAP_FAILED);

	for (i = 0; i < pagesize; i++)
		ATF_REQUIRE_EQ_MSG(0, p1[i], "byte at p1[%d] is %x", i, p1[i]);

	ATF_REQUIRE(memcmp(p1, p2, pagesize) == 0);

	p1[0] = 1;

	ATF_REQUIRE(p2[0] == 0);

	p2[0] = 2;

	ATF_REQUIRE(p1[0] == 1);

	p3 = mmap(NULL, pagesize, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
	    0);
	ATF_REQUIRE(p3 != MAP_FAILED);

	ATF_REQUIRE(p3[0] == 0);

	munmap(p1, pagesize);
	munmap(p2, pagesize);
	munmap(p3, pagesize);
	close(fd);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, mmap__map_at_zero);
	ATF_TP_ADD_TC(tp, mmap__bad_arguments);
	ATF_TP_ADD_TC(tp, mmap__dev_zero_private);
	ATF_TP_ADD_TC(tp, mmap__dev_zero_shared);

	return (atf_no_error());
}
