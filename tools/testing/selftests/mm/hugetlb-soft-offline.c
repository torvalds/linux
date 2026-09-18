// SPDX-License-Identifier: GPL-2.0
/*
 * Test soft offline behavior for HugeTLB pages:
 * - if enable_soft_offline = 0, hugepages should stay intact and soft
 *   offlining failed with EOPNOTSUPP.
 * - if enable_soft_offline = 1, a hugepage should be dissolved and
 *   nr_hugepages/free_hugepages should be reduced by 1.
 *
 * The test allocates 8 default hugepages
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <linux/magic.h>
#include <linux/memfd.h>
#include <sys/mman.h>
#include <sys/statfs.h>
#include <sys/types.h>

#include "kselftest.h"
#include "vm_util.h"
#include "hugepage_settings.h"

#ifndef MADV_SOFT_OFFLINE
#define MADV_SOFT_OFFLINE 101
#endif

#define EPREFIX " !!! "

#define ENABLE_SOFT_OFFLINE_PATH "/proc/sys/vm/enable_soft_offline"

static int do_soft_offline(int fd, size_t len, int expect_errno)
{
	char *filemap = NULL;
	char *hwp_addr = NULL;
	const unsigned long pagesize = getpagesize();
	int ret = 0;

	if (ftruncate(fd, len) < 0) {
		ksft_perror(EPREFIX "ftruncate to len failed");
		return -1;
	}

	filemap = mmap(NULL, len, PROT_READ | PROT_WRITE,
		       MAP_SHARED | MAP_POPULATE, fd, 0);
	if (filemap == MAP_FAILED) {
		ksft_perror(EPREFIX "mmap failed");
		ret = -1;
		goto untruncate;
	}

	memset(filemap, 0xab, len);
	ksft_print_msg("Allocated %#lx bytes of hugetlb pages\n", len);

	hwp_addr = filemap + len / 2;
	ret = madvise(hwp_addr, pagesize, MADV_SOFT_OFFLINE);
	ksft_print_msg("MADV_SOFT_OFFLINE %p ret=%d, errno=%d\n",
		       hwp_addr, ret, errno);
	if (ret != 0)
		ksft_perror(EPREFIX "madvise failed");

	if (errno == expect_errno)
		ret = 0;
	else {
		ksft_print_msg("MADV_SOFT_OFFLINE should ret %d\n",
			       expect_errno);
		ret = -1;
	}

	munmap(filemap, len);
untruncate:
	if (ftruncate(fd, 0) < 0)
		ksft_perror(EPREFIX "ftruncate back to 0 failed");

	return ret;
}

static unsigned long orig_enable_soft_offline = -1UL;

/*
 * Runs from an atexit handler, so it must not call anything that
 * exits on failure.
 */
static void restore_enable_soft_offline(void)
{
	char buf[24];
	int fd, len;

	if (orig_enable_soft_offline == -1UL)
		return;

	len = snprintf(buf, sizeof(buf), "%lu", orig_enable_soft_offline);
	fd = open(ENABLE_SOFT_OFFLINE_PATH, O_WRONLY);
	if (fd < 0)
		return;
	if (write(fd, buf, len) != len)
		ksft_print_msg("failed to restore enable_soft_offline: %s\n",
			       strerror(errno));
	close(fd);
}

static int create_hugetlbfs_file(struct statfs *file_stat)
{
	int fd;

	fd = memfd_create("hugetlb_tmp", MFD_HUGETLB);
	if (fd < 0) {
		ksft_perror(EPREFIX "could not open hugetlbfs file");
		return -1;
	}

	memset(file_stat, 0, sizeof(*file_stat));
	if (fstatfs(fd, file_stat)) {
		ksft_perror(EPREFIX "fstatfs failed");
		goto close;
	}
	if (file_stat->f_type != HUGETLBFS_MAGIC) {
		ksft_print_msg(EPREFIX "not hugetlbfs file\n");
		goto close;
	}

	return fd;
close:
	close(fd);
	return -1;
}

static void test_soft_offline_common(int enable_soft_offline)
{
	int fd;
	int expect_errno = enable_soft_offline ? 0 : EOPNOTSUPP;
	struct statfs file_stat;
	unsigned long hugepagesize_kb = 0;
	unsigned long nr_hugepages_before = 0;
	unsigned long nr_hugepages_after = 0;
	int ret;

	ksft_print_msg("Test soft-offline when enabled_soft_offline=%d\n",
		       enable_soft_offline);

	fd = create_hugetlbfs_file(&file_stat);
	if (fd < 0)
		ksft_exit_fail_msg("Failed to create hugetlbfs file\n");

	hugepagesize_kb = file_stat.f_bsize / 1024;
	ksft_print_msg("Hugepagesize is %ldkB\n", hugepagesize_kb);

	ret = write_num(ENABLE_SOFT_OFFLINE_PATH, enable_soft_offline);
	if (ret)
		ksft_exit_fail_msg("Failed to write to %s: %s\n",
				   ENABLE_SOFT_OFFLINE_PATH, strerror(-ret));

	nr_hugepages_before = hugetlb_nr_default_pages();

	ksft_print_msg("Before MADV_SOFT_OFFLINE nr_hugepages=%ld\n",
		       nr_hugepages_before);

	ret = do_soft_offline(fd, 2 * file_stat.f_bsize, expect_errno);

	nr_hugepages_after = hugetlb_nr_default_pages();

	ksft_print_msg("After MADV_SOFT_OFFLINE nr_hugepages=%ld\n",
		nr_hugepages_after);

	// No need for the hugetlbfs file from now on.
	close(fd);

	if (enable_soft_offline) {
		if (nr_hugepages_before != nr_hugepages_after + 1) {
			ksft_test_result_fail("MADV_SOFT_OFFLINE should reduced 1 hugepage\n");
			return;
		}
	} else {
		if (nr_hugepages_before != nr_hugepages_after) {
			ksft_test_result_fail("MADV_SOFT_OFFLINE reduced %lu hugepages\n",
				nr_hugepages_before - nr_hugepages_after);
			return;
		}
	}

	ksft_test_result(ret == 0,
			 "Test soft-offline when enabled_soft_offline=%d\n",
			 enable_soft_offline);
}

int main(int argc, char **argv)
{
	int ret;

	ksft_print_header();

	if (!hugetlb_setup_default(8))
		ksft_exit_skip("not enough hugetlb pages\n");

	ksft_set_plan(2);

	ret = read_num(ENABLE_SOFT_OFFLINE_PATH, &orig_enable_soft_offline);
	if (ret)
		ksft_exit_fail_msg("Failed to read %s: %s\n",
				   ENABLE_SOFT_OFFLINE_PATH, strerror(-ret));

	atexit(restore_enable_soft_offline);

	test_soft_offline_common(1);
	test_soft_offline_common(0);

	ksft_finished();
}
