// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <linux/magic.h>
#include <sys/mman.h>
#include <sys/statfs.h>
#include <errno.h>
#include <stdbool.h>

#include "../kselftest.h"

#define PREFIX " ... "
#define ERROR_PREFIX " !!! "

#define MAX_WRITE_READ_CHUNK_SIZE (getpagesize() * 16)
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

enum test_status {
	TEST_PASSED = 0,
	TEST_FAILED = 1,
	TEST_SKIPPED = 2,
};

static char *status_to_str(enum test_status status)
{
	switch (status) {
	case TEST_PASSED:
		return "TEST_PASSED";
	case TEST_FAILED:
		return "TEST_FAILED";
	case TEST_SKIPPED:
		return "TEST_SKIPPED";
	default:
		return "TEST_???";
	}
}

static int setup_filemap(char *filemap, size_t len, size_t wr_chunk_size)
{
	char iter = 0;

	for (size_t offset = 0; offset < len;
	     offset += wr_chunk_size) {
		iter++;
		memset(filemap + offset, iter, wr_chunk_size);
	}

	return 0;
}

static bool verify_chunk(char *buf, size_t len, char val)
{
	size_t i;

	for (i = 0; i < len; ++i) {
		if (buf[i] != val) {
			printf(PREFIX ERROR_PREFIX "check fail: buf[%lu] = %u != %u\n",
				i, buf[i], val);
			return false;
		}
	}

	return true;
}

static bool seek_read_hugepage_filemap(int fd, size_t len, size_t wr_chunk_size,
				       off_t offset, size_t expected)
{
	char buf[MAX_WRITE_READ_CHUNK_SIZE];
	ssize_t ret_count = 0;
	ssize_t total_ret_count = 0;
	char val = offset / wr_chunk_size + offset % wr_chunk_size;

	printf(PREFIX PREFIX "init val=%u with offset=0x%lx\n", val, offset);
	printf(PREFIX PREFIX "expect to read 0x%lx bytes of data in total\n",
	       expected);
	if (lseek(fd, offset, SEEK_SET) < 0) {
		perror(PREFIX ERROR_PREFIX "seek failed");
		return false;
	}

	while (offset + total_ret_count < len) {
		ret_count = read(fd, buf, wr_chunk_size);
		if (ret_count == 0) {
			printf(PREFIX PREFIX "read reach end of the file\n");
			break;
		} else if (ret_count < 0) {
			perror(PREFIX ERROR_PREFIX "read failed");
			break;
		}
		++val;
		if (!verify_chunk(buf, ret_count, val))
			return false;

		total_ret_count += ret_count;
	}
	printf(PREFIX PREFIX "actually read 0x%lx bytes of data in total\n",
	       total_ret_count);

	return total_ret_count == expected;
}

static bool read_hugepage_filemap(int fd, size_t len,
				  size_t wr_chunk_size, size_t expected)
{
	char buf[MAX_WRITE_READ_CHUNK_SIZE];
	ssize_t ret_count = 0;
	ssize_t total_ret_count = 0;
	char val = 0;

	printf(PREFIX PREFIX "expect to read 0x%lx bytes of data in total\n",
	       expected);
	while (total_ret_count < len) {
		ret_count = read(fd, buf, wr_chunk_size);
		if (ret_count == 0) {
			printf(PREFIX PREFIX "read reach end of the file\n");
			break;
		} else if (ret_count < 0) {
			perror(PREFIX ERROR_PREFIX "read failed");
			break;
		}
		++val;
		if (!verify_chunk(buf, ret_count, val))
			return false;

		total_ret_count += ret_count;
	}
	printf(PREFIX PREFIX "actually read 0x%lx bytes of data in total\n",
	       total_ret_count);

	return total_ret_count == expected;
}

static enum test_status
test_hugetlb_read(int fd, size_t len, size_t wr_chunk_size)
{
	enum test_status status = TEST_SKIPPED;
	char *filemap = NULL;

	if (ftruncate(fd, len) < 0) {
		perror(PREFIX ERROR_PREFIX "ftruncate failed");
		return status;
	}

	filemap = mmap(NULL, len, PROT_READ | PROT_WRITE,
		       MAP_SHARED | MAP_POPULATE, fd, 0);
	if (filemap == MAP_FAILED) {
		perror(PREFIX ERROR_PREFIX "mmap for primary mapping failed");
		goto done;
	}

	setup_filemap(filemap, len, wr_chunk_size);
	status = TEST_FAILED;

	if (read_hugepage_filemap(fd, len, wr_chunk_size, len))
		status = TEST_PASSED;

	munmap(filemap, len);
done:
	if (ftruncate(fd, 0) < 0) {
		perror(PREFIX ERROR_PREFIX "ftruncate back to 0 failed");
		status = TEST_FAILED;
	}

	return status;
}

static enum test_status
test_hugetlb_read_hwpoison(int fd, size_t len, size_t wr_chunk_size,
			   bool skip_hwpoison_page)
{
	enum test_status status = TEST_SKIPPED;
	char *filemap = NULL;
	char *hwp_addr = NULL;
	const unsigned long pagesize = getpagesize();

	if (ftruncate(fd, len) < 0) {
		perror(PREFIX ERROR_PREFIX "ftruncate failed");
		return status;
	}

	filemap = mmap(NULL, len, PROT_READ | PROT_WRITE,
		       MAP_SHARED | MAP_POPULATE, fd, 0);
	if (filemap == MAP_FAILED) {
		perror(PREFIX ERROR_PREFIX "mmap for primary mapping failed");
		goto done;
	}

	setup_filemap(filemap, len, wr_chunk_size);
	status = TEST_FAILED;

	/*
	 * Poisoned hugetlb page layout (assume hugepagesize=2MB):
	 * |<---------------------- 1MB ---------------------->|
	 * |<---- healthy page ---->|<---- HWPOISON page ----->|
	 * |<------------------- (1MB - 8KB) ----------------->|
	 */
	hwp_addr = filemap + len / 2 + pagesize;
	if (madvise(hwp_addr, pagesize, MADV_HWPOISON) < 0) {
		perror(PREFIX ERROR_PREFIX "MADV_HWPOISON failed");
		goto unmap;
	}

	if (!skip_hwpoison_page) {
		/*
		 * Userspace should be able to read (1MB + 1 page) from
		 * the beginning of the HWPOISONed hugepage.
		 */
		if (read_hugepage_filemap(fd, len, wr_chunk_size,
					  len / 2 + pagesize))
			status = TEST_PASSED;
	} else {
		/*
		 * Userspace should be able to read (1MB - 2 pages) from
		 * HWPOISONed hugepage.
		 */
		if (seek_read_hugepage_filemap(fd, len, wr_chunk_size,
					       len / 2 + MAX(2 * pagesize, wr_chunk_size),
					       len / 2 - MAX(2 * pagesize, wr_chunk_size)))
			status = TEST_PASSED;
	}

unmap:
	munmap(filemap, len);
done:
	if (ftruncate(fd, 0) < 0) {
		perror(PREFIX ERROR_PREFIX "ftruncate back to 0 failed");
		status = TEST_FAILED;
	}

	return status;
}

static int create_hugetlbfs_file(struct statfs *file_stat)
{
	int fd;

	fd = memfd_create("hugetlb_tmp", MFD_HUGETLB);
	if (fd < 0) {
		perror(PREFIX ERROR_PREFIX "could not open hugetlbfs file");
		return -1;
	}

	memset(file_stat, 0, sizeof(*file_stat));
	if (fstatfs(fd, file_stat)) {
		perror(PREFIX ERROR_PREFIX "fstatfs failed");
		goto close;
	}
	if (file_stat->f_type != HUGETLBFS_MAGIC) {
		printf(PREFIX ERROR_PREFIX "not hugetlbfs file\n");
		goto close;
	}

	return fd;
close:
	close(fd);
	return -1;
}

int main(void)
{
	int fd;
	struct statfs file_stat;
	enum test_status status;
	/* Test read() in different granularity. */
	size_t wr_chunk_sizes[] = {
		getpagesize() / 2, getpagesize(),
		getpagesize() * 2, getpagesize() * 4
	};
	size_t i;

	for (i = 0; i < ARRAY_SIZE(wr_chunk_sizes); ++i) {
		printf("Write/read chunk size=0x%lx\n",
		       wr_chunk_sizes[i]);

		fd = create_hugetlbfs_file(&file_stat);
		if (fd < 0)
			goto create_failure;
		printf(PREFIX "HugeTLB read regression test...\n");
		status = test_hugetlb_read(fd, file_stat.f_bsize,
					   wr_chunk_sizes[i]);
		printf(PREFIX "HugeTLB read regression test...%s\n",
		       status_to_str(status));
		close(fd);
		if (status == TEST_FAILED)
			return -1;

		fd = create_hugetlbfs_file(&file_stat);
		if (fd < 0)
			goto create_failure;
		printf(PREFIX "HugeTLB read HWPOISON test...\n");
		status = test_hugetlb_read_hwpoison(fd, file_stat.f_bsize,
						    wr_chunk_sizes[i], false);
		printf(PREFIX "HugeTLB read HWPOISON test...%s\n",
		       status_to_str(status));
		close(fd);
		if (status == TEST_FAILED)
			return -1;

		fd = create_hugetlbfs_file(&file_stat);
		if (fd < 0)
			goto create_failure;
		printf(PREFIX "HugeTLB seek then read HWPOISON test...\n");
		status = test_hugetlb_read_hwpoison(fd, file_stat.f_bsize,
						    wr_chunk_sizes[i], true);
		printf(PREFIX "HugeTLB seek then read HWPOISON test...%s\n",
		       status_to_str(status));
		close(fd);
		if (status == TEST_FAILED)
			return -1;
	}

	return 0;

create_failure:
	printf(ERROR_PREFIX "Abort test: failed to create hugetlbfs file\n");
	return -1;
}
