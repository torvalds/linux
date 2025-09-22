/*	$OpenBSD: t_msync.c,v 1.2 2021/12/13 16:56:48 deraadt Exp $	*/
/* $NetBSD: t_msync.c,v 1.3 2017/01/14 20:52:42 christos Exp $ */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
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

#include "atf-c.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static long		page = 0;
static const off_t	off = 512;
static const char	path[] = "msync";

static const char	*msync_sync(const char *, int);

static const char *
msync_sync(const char *garbage, int flags)
{
	char *buf, *map = MAP_FAILED;
	const char *str = NULL;
	size_t len;
	int fd, rv;

	/*
	 * Create a temporary file, write
	 * one page to it, and map the file.
	 */
	buf = malloc(page);

	if (buf == NULL)
		return NULL;

	memset(buf, 'x', page);

	fd = open(path, O_RDWR | O_CREAT, 0700);

	if (fd < 0) {
		free(buf);
		return "failed to open";
	}

	ATF_REQUIRE_MSG(write(fd, buf, page) != -1, "write(2) failed: %s",
	    strerror(errno));

	map = mmap(NULL, page, PROT_READ | PROT_WRITE, MAP_FILE|MAP_PRIVATE,
	     fd, 0);

	if (map == MAP_FAILED) {
		str = "failed to map";
		goto out;
	}

	/*
	 * Seek to an arbitrary offset and
	 * write garbage to this position.
	 */
	if (lseek(fd, off, SEEK_SET) != off) {
		str = "failed to seek";
		goto out;
	}

	len = strlen(garbage);
	rv = write(fd, garbage, len);

	if (rv != (ssize_t)len) {
		str = "failed to write garbage";
		goto out;
	}

	/*
	 * Synchronize the mapping and verify
	 * that garbage is at the given offset.
	 */
	if (msync(map, page, flags) != 0) {
		str = "failed to msync";
		goto out;
	}

	if (memcmp(map + off, garbage, len) != 0) {
		str = "msync did not synchronize";
		goto out;
	}

out:
	free(buf);

	(void)close(fd);
	(void)unlink(path);

	if (map != MAP_FAILED)
		(void)munmap(map, page);

	return str;
}

ATF_TC(msync_async);
ATF_TC_HEAD(msync_async, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test of msync(2), MS_ASYNC");
}

ATF_TC_BODY(msync_async, tc)
{
	const char *str;

	str = msync_sync("garbage", MS_ASYNC);

	if (str != NULL)
		atf_tc_fail("%s", str);
}

ATF_TC(msync_err);
ATF_TC_HEAD(msync_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test error conditions in msync(2)");
}

ATF_TC_BODY(msync_err, tc)
{

	char *map = MAP_FAILED;

	/*
	 * Test that invalid flags error out.
	 */
	ATF_REQUIRE(msync_sync("error", -1) != NULL);
	ATF_REQUIRE(msync_sync("error", INT_MAX) != NULL);

	errno = 0;

	/*
	 * Map a page and then unmap to get an unmapped address.
	 */
	map = mmap(NULL, page, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE,
	    -1, 0);
	ATF_REQUIRE(map != MAP_FAILED);

	(void)munmap(map, page);

	ATF_REQUIRE(msync(map, page, MS_SYNC) != 0);
	ATF_REQUIRE(errno == EFAULT);
}

ATF_TC(msync_invalidate);
ATF_TC_HEAD(msync_invalidate, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test of msync(2), MS_INVALIDATE");
}

ATF_TC_BODY(msync_invalidate, tc)
{
	const char *str;

	str = msync_sync("garbage", MS_INVALIDATE);

	if (str != NULL)
		atf_tc_fail("%s", str);
}

ATF_TC(msync_sync);
ATF_TC_HEAD(msync_sync, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test of msync(2), MS_SYNC");
}

ATF_TC_BODY(msync_sync, tc)
{
	const char *str;

	str = msync_sync("garbage", MS_SYNC);

	if (str != NULL)
		atf_tc_fail("%s", str);
}

ATF_TP_ADD_TCS(tp)
{

	page = sysconf(_SC_PAGESIZE);

	ATF_REQUIRE(page >= 0);
	ATF_REQUIRE(page > off);

	ATF_TP_ADD_TC(tp, msync_async);
	ATF_TP_ADD_TC(tp, msync_err);
	ATF_TP_ADD_TC(tp, msync_invalidate);
	ATF_TP_ADD_TC(tp, msync_sync);

	return atf_no_error();
}
