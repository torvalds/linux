// SPDX-License-Identifier: GPL-2.0-only
/*
 * MADV_POPULATE_READ and MADV_POPULATE_WRITE tests
 *
 * Copyright 2021, Red Hat, Inc.
 *
 * Author(s): David Hildenbrand <david@redhat.com>
 */
#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/mman.h>
#include <sys/mman.h>

#include "../kselftest.h"
#include "vm_util.h"

#ifndef MADV_POPULATE_READ
#define MADV_POPULATE_READ	22
#endif /* MADV_POPULATE_READ */
#ifndef MADV_POPULATE_WRITE
#define MADV_POPULATE_WRITE	23
#endif /* MADV_POPULATE_WRITE */

/*
 * For now, we're using 2 MiB of private anonymous memory for all tests.
 */
#define SIZE (2 * 1024 * 1024)

static size_t pagesize;

static void sense_support(void)
{
	char *addr;
	int ret;

	addr = mmap(0, pagesize, PROT_READ | PROT_WRITE,
		    MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
	if (!addr)
		ksft_exit_fail_msg("mmap failed\n");

	ret = madvise(addr, pagesize, MADV_POPULATE_READ);
	if (ret)
		ksft_exit_skip("MADV_POPULATE_READ is not available\n");

	ret = madvise(addr, pagesize, MADV_POPULATE_WRITE);
	if (ret)
		ksft_exit_skip("MADV_POPULATE_WRITE is not available\n");

	munmap(addr, pagesize);
}

static void test_prot_read(void)
{
	char *addr;
	int ret;

	ksft_print_msg("[RUN] %s\n", __func__);

	addr = mmap(0, SIZE, PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
	if (addr == MAP_FAILED)
		ksft_exit_fail_msg("mmap failed\n");

	ret = madvise(addr, SIZE, MADV_POPULATE_READ);
	ksft_test_result(!ret, "MADV_POPULATE_READ with PROT_READ\n");

	ret = madvise(addr, SIZE, MADV_POPULATE_WRITE);
	ksft_test_result(ret == -1 && errno == EINVAL,
			 "MADV_POPULATE_WRITE with PROT_READ\n");

	munmap(addr, SIZE);
}

static void test_prot_write(void)
{
	char *addr;
	int ret;

	ksft_print_msg("[RUN] %s\n", __func__);

	addr = mmap(0, SIZE, PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
	if (addr == MAP_FAILED)
		ksft_exit_fail_msg("mmap failed\n");

	ret = madvise(addr, SIZE, MADV_POPULATE_READ);
	ksft_test_result(ret == -1 && errno == EINVAL,
			 "MADV_POPULATE_READ with PROT_WRITE\n");

	ret = madvise(addr, SIZE, MADV_POPULATE_WRITE);
	ksft_test_result(!ret, "MADV_POPULATE_WRITE with PROT_WRITE\n");

	munmap(addr, SIZE);
}

static void test_holes(void)
{
	char *addr;
	int ret;

	ksft_print_msg("[RUN] %s\n", __func__);

	addr = mmap(0, SIZE, PROT_READ | PROT_WRITE,
		    MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
	if (addr == MAP_FAILED)
		ksft_exit_fail_msg("mmap failed\n");
	ret = munmap(addr + pagesize, pagesize);
	if (ret)
		ksft_exit_fail_msg("munmap failed\n");

	/* Hole in the middle */
	ret = madvise(addr, SIZE, MADV_POPULATE_READ);
	ksft_test_result(ret == -1 && errno == ENOMEM,
			 "MADV_POPULATE_READ with holes in the middle\n");
	ret = madvise(addr, SIZE, MADV_POPULATE_WRITE);
	ksft_test_result(ret == -1 && errno == ENOMEM,
			 "MADV_POPULATE_WRITE with holes in the middle\n");

	/* Hole at end */
	ret = madvise(addr, 2 * pagesize, MADV_POPULATE_READ);
	ksft_test_result(ret == -1 && errno == ENOMEM,
			 "MADV_POPULATE_READ with holes at the end\n");
	ret = madvise(addr, 2 * pagesize, MADV_POPULATE_WRITE);
	ksft_test_result(ret == -1 && errno == ENOMEM,
			 "MADV_POPULATE_WRITE with holes at the end\n");

	/* Hole at beginning */
	ret = madvise(addr + pagesize, pagesize, MADV_POPULATE_READ);
	ksft_test_result(ret == -1 && errno == ENOMEM,
			 "MADV_POPULATE_READ with holes at the beginning\n");
	ret = madvise(addr + pagesize, pagesize, MADV_POPULATE_WRITE);
	ksft_test_result(ret == -1 && errno == ENOMEM,
			 "MADV_POPULATE_WRITE with holes at the beginning\n");

	munmap(addr, SIZE);
}

static bool range_is_populated(char *start, ssize_t size)
{
	int fd = open("/proc/self/pagemap", O_RDONLY);
	bool ret = true;

	if (fd < 0)
		ksft_exit_fail_msg("opening pagemap failed\n");
	for (; size > 0 && ret; size -= pagesize, start += pagesize)
		if (!pagemap_is_populated(fd, start))
			ret = false;
	close(fd);
	return ret;
}

static bool range_is_not_populated(char *start, ssize_t size)
{
	int fd = open("/proc/self/pagemap", O_RDONLY);
	bool ret = true;

	if (fd < 0)
		ksft_exit_fail_msg("opening pagemap failed\n");
	for (; size > 0 && ret; size -= pagesize, start += pagesize)
		if (pagemap_is_populated(fd, start))
			ret = false;
	close(fd);
	return ret;
}

static void test_populate_read(void)
{
	char *addr;
	int ret;

	ksft_print_msg("[RUN] %s\n", __func__);

	addr = mmap(0, SIZE, PROT_READ | PROT_WRITE,
		    MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
	if (addr == MAP_FAILED)
		ksft_exit_fail_msg("mmap failed\n");
	ksft_test_result(range_is_not_populated(addr, SIZE),
			 "range initially not populated\n");

	ret = madvise(addr, SIZE, MADV_POPULATE_READ);
	ksft_test_result(!ret, "MADV_POPULATE_READ\n");
	ksft_test_result(range_is_populated(addr, SIZE),
			 "range is populated\n");

	munmap(addr, SIZE);
}

static void test_populate_write(void)
{
	char *addr;
	int ret;

	ksft_print_msg("[RUN] %s\n", __func__);

	addr = mmap(0, SIZE, PROT_READ | PROT_WRITE,
		    MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
	if (addr == MAP_FAILED)
		ksft_exit_fail_msg("mmap failed\n");
	ksft_test_result(range_is_not_populated(addr, SIZE),
			 "range initially not populated\n");

	ret = madvise(addr, SIZE, MADV_POPULATE_WRITE);
	ksft_test_result(!ret, "MADV_POPULATE_WRITE\n");
	ksft_test_result(range_is_populated(addr, SIZE),
			 "range is populated\n");

	munmap(addr, SIZE);
}

static bool range_is_softdirty(char *start, ssize_t size)
{
	int fd = open("/proc/self/pagemap", O_RDONLY);
	bool ret = true;

	if (fd < 0)
		ksft_exit_fail_msg("opening pagemap failed\n");
	for (; size > 0 && ret; size -= pagesize, start += pagesize)
		if (!pagemap_is_softdirty(fd, start))
			ret = false;
	close(fd);
	return ret;
}

static bool range_is_not_softdirty(char *start, ssize_t size)
{
	int fd = open("/proc/self/pagemap", O_RDONLY);
	bool ret = true;

	if (fd < 0)
		ksft_exit_fail_msg("opening pagemap failed\n");
	for (; size > 0 && ret; size -= pagesize, start += pagesize)
		if (pagemap_is_softdirty(fd, start))
			ret = false;
	close(fd);
	return ret;
}

static void test_softdirty(void)
{
	char *addr;
	int ret;

	ksft_print_msg("[RUN] %s\n", __func__);

	addr = mmap(0, SIZE, PROT_READ | PROT_WRITE,
		    MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
	if (addr == MAP_FAILED)
		ksft_exit_fail_msg("mmap failed\n");

	/* Clear any softdirty bits. */
	clear_softdirty();
	ksft_test_result(range_is_not_softdirty(addr, SIZE),
			 "range is not softdirty\n");

	/* Populating READ should set softdirty. */
	ret = madvise(addr, SIZE, MADV_POPULATE_READ);
	ksft_test_result(!ret, "MADV_POPULATE_READ\n");
	ksft_test_result(range_is_not_softdirty(addr, SIZE),
			 "range is not softdirty\n");

	/* Populating WRITE should set softdirty. */
	ret = madvise(addr, SIZE, MADV_POPULATE_WRITE);
	ksft_test_result(!ret, "MADV_POPULATE_WRITE\n");
	ksft_test_result(range_is_softdirty(addr, SIZE),
			 "range is softdirty\n");

	munmap(addr, SIZE);
}

int main(int argc, char **argv)
{
	int err;

	pagesize = getpagesize();

	ksft_print_header();
	ksft_set_plan(21);

	sense_support();
	test_prot_read();
	test_prot_write();
	test_holes();
	test_populate_read();
	test_populate_write();
	test_softdirty();

	err = ksft_get_fail_cnt();
	if (err)
		ksft_exit_fail_msg("%d out of %d tests failed\n",
				   err, ksft_test_num());
	return ksft_exit_pass();
}
