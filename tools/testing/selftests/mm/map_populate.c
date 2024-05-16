// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Dmitry Safonov, Arista Networks
 *
 * MAP_POPULATE | MAP_PRIVATE should COW VMA pages.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../kselftest.h"

#define MMAP_SZ		4096

#define BUG_ON(condition, description)						\
	do {									\
		if (condition)							\
			ksft_exit_fail_msg("[FAIL]\t%s:%d\t%s:%s\n",		\
					   __func__, __LINE__, (description),	\
					   strerror(errno));			\
	} while (0)

#define TESTS_IN_CHILD 2

static void parent_f(int sock, unsigned long *smap, int child)
{
	int status, ret;

	ret = read(sock, &status, sizeof(int));
	BUG_ON(ret <= 0, "read(sock)");

	*smap = 0x22222BAD;
	ret = msync(smap, MMAP_SZ, MS_SYNC);
	BUG_ON(ret, "msync()");

	ret = write(sock, &status, sizeof(int));
	BUG_ON(ret <= 0, "write(sock)");

	waitpid(child, &status, 0);

	/* The ksft macros don't keep counters between processes */
	ksft_cnt.ksft_pass = WEXITSTATUS(status);
	ksft_cnt.ksft_fail = TESTS_IN_CHILD - WEXITSTATUS(status);
}

static int child_f(int sock, unsigned long *smap, int fd)
{
	int ret, buf = 0;

	smap = mmap(0, MMAP_SZ, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_POPULATE, fd, 0);
	BUG_ON(smap == MAP_FAILED, "mmap()");

	BUG_ON(*smap != 0xdeadbabe, "MAP_PRIVATE | MAP_POPULATE changed file");

	ret = write(sock, &buf, sizeof(int));
	BUG_ON(ret <= 0, "write(sock)");

	ret = read(sock, &buf, sizeof(int));
	BUG_ON(ret <= 0, "read(sock)");

	ksft_test_result(*smap != 0x22222BAD, "MAP_POPULATE COW private page\n");
	ksft_test_result(*smap == 0xdeadbabe, "The mapping state\n");

	/* The ksft macros don't keep counters between processes */
	return ksft_cnt.ksft_pass;
}

int main(int argc, char **argv)
{
	int sock[2], child, ret;
	FILE *ftmp;
	unsigned long *smap;

	ksft_print_header();
	ksft_set_plan(TESTS_IN_CHILD);

	ftmp = tmpfile();
	BUG_ON(!ftmp, "tmpfile()");

	ret = ftruncate(fileno(ftmp), MMAP_SZ);
	BUG_ON(ret, "ftruncate()");

	smap = mmap(0, MMAP_SZ, PROT_READ | PROT_WRITE,
			MAP_SHARED, fileno(ftmp), 0);
	BUG_ON(smap == MAP_FAILED, "mmap()");

	*smap = 0xdeadbabe;
	/* Probably unnecessary, but let it be. */
	ret = msync(smap, MMAP_SZ, MS_SYNC);
	BUG_ON(ret, "msync()");

	ret = socketpair(PF_LOCAL, SOCK_SEQPACKET, 0, sock);
	BUG_ON(ret, "socketpair()");

	child = fork();
	BUG_ON(child == -1, "fork()");

	if (child) {
		ret = close(sock[0]);
		BUG_ON(ret, "close()");

		parent_f(sock[1], smap, child);

		ksft_finished();
	}

	ret = close(sock[1]);
	BUG_ON(ret, "close()");

	return child_f(sock[0], smap, fileno(ftmp));
}
