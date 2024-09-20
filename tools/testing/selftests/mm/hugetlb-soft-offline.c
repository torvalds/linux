// SPDX-License-Identifier: GPL-2.0
/*
 * Test soft offline behavior for HugeTLB pages:
 * - if enable_soft_offline = 0, hugepages should stay intact and soft
 *   offlining failed with EOPNOTSUPP.
 * - if enable_soft_offline = 1, a hugepage should be dissolved and
 *   nr_hugepages/free_hugepages should be reduced by 1.
 *
 * Before running, make sure more than 2 hugepages of default_hugepagesz
 * are allocated. For example, if /proc/meminfo/Hugepagesize is 2048kB:
 *   echo 8 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <linux/magic.h>
#include <linux/memfd.h>
#include <sys/mman.h>
#include <sys/statfs.h>
#include <sys/types.h>

#include "../kselftest.h"

#ifndef MADV_SOFT_OFFLINE
#define MADV_SOFT_OFFLINE 101
#endif

#define EPREFIX " !!! "

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

static int set_enable_soft_offline(int value)
{
	char cmd[256] = {0};
	FILE *cmdfile = NULL;

	if (value != 0 && value != 1)
		return -EINVAL;

	sprintf(cmd, "echo %d > /proc/sys/vm/enable_soft_offline", value);
	cmdfile = popen(cmd, "r");

	if (cmdfile)
		ksft_print_msg("enable_soft_offline => %d\n", value);
	else {
		ksft_perror(EPREFIX "failed to set enable_soft_offline");
		return errno;
	}

	pclose(cmdfile);
	return 0;
}

static int read_nr_hugepages(unsigned long hugepage_size,
			     unsigned long *nr_hugepages)
{
	char buffer[256] = {0};
	char cmd[256] = {0};

	sprintf(cmd, "cat /sys/kernel/mm/hugepages/hugepages-%ldkB/nr_hugepages",
		hugepage_size);
	FILE *cmdfile = popen(cmd, "r");

	if (cmdfile == NULL) {
		ksft_perror(EPREFIX "failed to popen nr_hugepages");
		return -1;
	}

	if (!fgets(buffer, sizeof(buffer), cmdfile)) {
		ksft_perror(EPREFIX "failed to read nr_hugepages");
		pclose(cmdfile);
		return -1;
	}

	*nr_hugepages = atoll(buffer);
	pclose(cmdfile);
	return 0;
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

	if (set_enable_soft_offline(enable_soft_offline) != 0) {
		close(fd);
		ksft_exit_fail_msg("Failed to set enable_soft_offline\n");
	}

	if (read_nr_hugepages(hugepagesize_kb, &nr_hugepages_before) != 0) {
		close(fd);
		ksft_exit_fail_msg("Failed to read nr_hugepages\n");
	}

	ksft_print_msg("Before MADV_SOFT_OFFLINE nr_hugepages=%ld\n",
		       nr_hugepages_before);

	ret = do_soft_offline(fd, 2 * file_stat.f_bsize, expect_errno);

	if (read_nr_hugepages(hugepagesize_kb, &nr_hugepages_after) != 0) {
		close(fd);
		ksft_exit_fail_msg("Failed to read nr_hugepages\n");
	}

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
	ksft_print_header();
	ksft_set_plan(2);

	test_soft_offline_common(1);
	test_soft_offline_common(0);

	ksft_finished();
}
