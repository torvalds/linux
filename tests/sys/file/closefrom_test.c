/*-
 * Copyright (c) 2009 Hudson River Trading LLC
 * Written by: John H. Baldwin <jhb@FreeBSD.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Regression tests for the closefrom(2) system call.
 */

#include <sys/param.h>
#include <sys/mman.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <libutil.h>
#include <paths.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct shared_info {
	int	failed;
	char	tag[64];
	char	message[0];
};

static int test = 1;

static void
ok(const char *descr)
{

	printf("ok %d - %s\n", test, descr);
	test++;
}

static void
fail(const char *descr, const char *fmt, ...)
{
	va_list ap;

	printf("not ok %d - %s", test, descr);
	test++;
	if (fmt) {
		va_start(ap, fmt);
		printf(" # ");
		vprintf(fmt, ap);
		va_end(ap);
	}
	printf("\n");
	exit(1);
}

#define	fail_err(descr)		fail((descr), "%s", strerror(errno))

static void
cok(struct shared_info *info, const char *descr)
{

	info->failed = 0;
	strlcpy(info->tag, descr, sizeof(info->tag));
	exit(0);
}

static void
cfail(struct shared_info *info, const char *descr, const char *fmt, ...)
{
	va_list ap;

	info->failed = 1;
	strlcpy(info->tag, descr, sizeof(info->tag));
	if (fmt) {
		va_start(ap, fmt);
		vsprintf(info->message, fmt, ap);
		va_end(ap);
	}
	exit(0);
}

#define	cfail_err(info, descr)	cfail((info), (descr), "%s", strerror(errno))

/*
 * Use kinfo_getfile() to fetch the list of file descriptors and figure out
 * the highest open file descriptor.
 */
static int
highest_fd(void)
{
	struct kinfo_file *kif;
	int cnt, i, highest;

	kif = kinfo_getfile(getpid(), &cnt);
	if (kif == NULL)
		fail_err("kinfo_getfile");
	highest = INT_MIN;
	for (i = 0; i < cnt; i++)
		if (kif[i].kf_fd > highest)
			highest = kif[i].kf_fd;
	free(kif);
	return (highest);
}

static int
devnull(void)
{
	int fd;

	fd = open(_PATH_DEVNULL, O_RDONLY);
	if (fd < 0)
		fail_err("open(\" "_PATH_DEVNULL" \")");
	return (fd);
}

int
main(void)
{
	struct shared_info *info;
	pid_t pid;
	int fd, i, start;

	printf("1..15\n");

	/* We better start up with fd's 0, 1, and 2 open. */
	start = devnull();
	if (start == -1)
		fail("open", "bad descriptor %d", start);
	ok("open");

	/* Make sure highest_fd() works. */
	fd = highest_fd();
	if (start != fd)
		fail("highest_fd", "bad descriptor %d != %d", start, fd);
	ok("highest_fd");

	/* Try to use closefrom() for just closing fd 3. */
	closefrom(start + 1);
	fd = highest_fd();
	if (fd != start)
		fail("closefrom", "highest fd %d", fd);
	ok("closefrom");

	/* Eat up 16 descriptors. */
	for (i = 0; i < 16; i++)
		(void)devnull();
	fd = highest_fd();
	if (fd != start + 16)
		fail("open 16", "highest fd %d", fd);
	ok("open 16");

	/* Close half of them. */
	closefrom(11);
	fd = highest_fd();
	if (fd != 10)
		fail("closefrom", "highest fd %d", fd);
	ok("closefrom");

	/* Explicitly close descriptors 6 and 8 to create holes. */
	if (close(6) < 0 || close(8) < 0)
		fail_err("close2 ");
	ok("close 2");

	/* Verify that close on 6 and 8 fails with EBADF. */
	if (close(6) == 0)
		fail("close(6)", "did not fail");
	if (errno != EBADF)
		fail_err("close(6)");
	ok("close(6)");
	if (close(8) == 0)
		fail("close(8)", "did not fail");
	if (errno != EBADF)
		fail_err("close(8)");
	ok("close(8)");

	/* Close from 4 on. */
	closefrom(4);
	fd = highest_fd();
	if (fd != 3)
		fail("closefrom", "highest fd %d", fd);
	ok("closefrom");

	/* Allocate a small SHM region for IPC with our child. */
	info = mmap(NULL, getpagesize(), PROT_READ | PROT_WRITE, MAP_ANON |
	    MAP_SHARED, -1, 0);
	if (info == MAP_FAILED)
		fail_err("mmap");
	ok("mmap");

	/* Fork a child process to test closefrom(0). */
	pid = fork();
	if (pid < 0)
		fail_err("fork");
	if (pid == 0) {
		/* Child. */
		closefrom(0);
		fd = highest_fd();
		if (fd >= 0)
			cfail(info, "closefrom(0)", "highest fd %d", fd);
		cok(info, "closefrom(0)");
	}
	if (wait(NULL) < 0)
		fail_err("wait");
	if (info->failed)
		fail(info->tag, "%s", info->message);
	ok(info->tag);

	/* Fork a child process to test closefrom(-1). */
	pid = fork();
	if (pid < 0)
		fail_err("fork");
	if (pid == 0) {
		/* Child. */
		closefrom(-1);
		fd = highest_fd();
		if (fd >= 0)
			cfail(info, "closefrom(-1)", "highest fd %d", fd);
		cok(info, "closefrom(-1)");
	}
	if (wait(NULL) < 0)
		fail_err("wait");
	if (info->failed)
		fail(info->tag, "%s", info->message);
	ok(info->tag);

	/* Dup stdout to 6. */
	if (dup2(1, 6) < 0)
		fail_err("dup2");
	fd = highest_fd();
	if (fd != 6)
		fail("dup2", "highest fd %d", fd);
	ok("dup2");

	/* Do a closefrom() starting in a hole. */
	closefrom(4);
	fd = highest_fd();
	if (fd != 3)
		fail("closefrom", "highest fd %d", fd);
	ok("closefrom");

	/* Do a closefrom() beyond our highest open fd. */
	closefrom(32);
	fd = highest_fd();
	if (fd != 3)
		fail("closefrom", "highest fd %d", fd);
	ok("closefrom");

	return (0);
}
