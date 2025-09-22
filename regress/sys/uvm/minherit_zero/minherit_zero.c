/*	$OpenBSD: minherit_zero.c,v 1.1 2014/06/13 07:17:54 matthew Exp $	*/
/*
 * Copyright (c) 2014 Google Inc.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/mman.h>
#include <sys/wait.h>
#include <assert.h>
#include <fcntl.h>
#include <paths.h>
#include <string.h>
#include <unistd.h>

#define CHECK(x) assert((x))
#define CHECK_EQ(a, b) assert((a) == (b))
#define CHECK_NE(a, b) assert((a) != (b))
#define CHECK_GE(a, b) assert((a) >= (b))

static int
ismemset(const void *s, int c, size_t n)
{
	const unsigned char *p = s;
	size_t i;

	for (i = 0; i < n; i++)
		if (p[i] != c)
			return (0);
	return (1);
}

static void
wait_for_clean_exit(pid_t pid)
{
	int status;
	CHECK_EQ(pid, waitpid(pid, &status, 0));
	CHECK(WIFEXITED(status));
	CHECK_EQ(0, WEXITSTATUS(status));
}

enum {
	NPAGES = 4,

	PARENT_BYTE = 42,
	CHILD_BYTE = 53,
	GRANDCHILD_BYTE = 65
};

/*
 * We map some memory, configure it's inheritance for MAP_INHERIT_ZERO,
 * then check that when we fork child or grandchild processes, that they
 * receive new zero'd out memory mappings.  Additionally, we sanity check
 * that after the child (or grandchild) process exits, that the parent's
 * memory is still in tact.
 */
static void
dotest(int fd, size_t len, int flags)
{
	void *p;
	pid_t pid;

	p = mmap(NULL, len, PROT_READ|PROT_WRITE, flags, fd, 0);
	CHECK_NE(MAP_FAILED, p);
	
	CHECK_EQ(0, minherit(p, len, MAP_INHERIT_ZERO));

	memset(p, PARENT_BYTE, len);

	pid = fork();
	CHECK_GE(pid, 0);
	if (pid == 0) {
		CHECK(ismemset(p, 0, len));
		memset(p, CHILD_BYTE, len);

		pid = fork();
		CHECK_GE(pid, 0);
		if (pid == 0) {
			CHECK(ismemset(p, 0, len));
			memset(p, GRANDCHILD_BYTE, len);
			_exit(0);
		}

		wait_for_clean_exit(pid);
		CHECK(ismemset(p, CHILD_BYTE, len));
		memset(p, 0, len);
		_exit(0);
	}

	wait_for_clean_exit(pid);
	CHECK(ismemset(p, PARENT_BYTE, len));
	memset(p, 0, len);

	CHECK_EQ(0, munmap(p, len));
}

int
main()
{
	long pagesize;
	size_t len;

	pagesize = sysconf(_SC_PAGESIZE);
	CHECK_GE(pagesize, 1);
	len = NPAGES * pagesize;

	/* First run test with private anonymous memory. */
	dotest(-1, len, MAP_ANON|MAP_PRIVATE);

	/* Test again with shared anonymous memory. */
	dotest(-1, len, MAP_ANON|MAP_SHARED);

	/* Finally, test with private file mapping. */
	int fd = open(_PATH_BSHELL, O_RDONLY);
	CHECK_GE(fd, 0);
	dotest(fd, len, MAP_FILE|MAP_PRIVATE);
	CHECK_EQ(0, close(fd));

	return (0);
}
