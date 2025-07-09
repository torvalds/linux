// SPDX-License-Identifier: GPL-2.0-only
/*
 * GUP long-term page pinning tests.
 *
 * Copyright 2023, Red Hat, Inc.
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
#include <assert.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/vfs.h>
#include <linux/magic.h>
#include <linux/memfd.h>

#include "local_config.h"
#ifdef LOCAL_CONFIG_HAVE_LIBURING
#include <liburing.h>
#endif /* LOCAL_CONFIG_HAVE_LIBURING */

#include "../../../../mm/gup_test.h"
#include "../kselftest.h"
#include "vm_util.h"

static size_t pagesize;
static int nr_hugetlbsizes;
static size_t hugetlbsizes[10];
static int gup_fd;

static __fsword_t get_fs_type(int fd)
{
	struct statfs fs;
	int ret;

	do {
		ret = fstatfs(fd, &fs);
	} while (ret && errno == EINTR);

	return ret ? 0 : fs.f_type;
}

static bool fs_is_unknown(__fsword_t fs_type)
{
	/*
	 * We only support some filesystems in our tests when dealing with
	 * R/W long-term pinning. For these filesystems, we can be fairly sure
	 * whether they support it or not.
	 */
	switch (fs_type) {
	case TMPFS_MAGIC:
	case HUGETLBFS_MAGIC:
	case BTRFS_SUPER_MAGIC:
	case EXT4_SUPER_MAGIC:
	case XFS_SUPER_MAGIC:
		return false;
	default:
		return true;
	}
}

static bool fs_supports_writable_longterm_pinning(__fsword_t fs_type)
{
	assert(!fs_is_unknown(fs_type));
	switch (fs_type) {
	case TMPFS_MAGIC:
	case HUGETLBFS_MAGIC:
		return true;
	default:
		return false;
	}
}

enum test_type {
	TEST_TYPE_RO,
	TEST_TYPE_RO_FAST,
	TEST_TYPE_RW,
	TEST_TYPE_RW_FAST,
#ifdef LOCAL_CONFIG_HAVE_LIBURING
	TEST_TYPE_IOURING,
#endif /* LOCAL_CONFIG_HAVE_LIBURING */
};

static void do_test(int fd, size_t size, enum test_type type, bool shared)
{
	__fsword_t fs_type = get_fs_type(fd);
	bool should_work;
	char *mem;
	int result = KSFT_PASS;
	int ret;

	if (fd < 0) {
		result = KSFT_FAIL;
		goto report;
	}

	if (ftruncate(fd, size)) {
		if (errno == ENOENT) {
			skip_test_dodgy_fs("ftruncate()");
		} else {
			ksft_print_msg("ftruncate() failed (%s)\n",
				       strerror(errno));
			result = KSFT_FAIL;
			goto report;
		}
		return;
	}

	if (fallocate(fd, 0, 0, size)) {
		if (size == pagesize) {
			ksft_print_msg("fallocate() failed (%s)\n", strerror(errno));
			result = KSFT_FAIL;
		} else {
			ksft_print_msg("need more free huge pages\n");
			result = KSFT_SKIP;
		}
		goto report;
	}

	mem = mmap(NULL, size, PROT_READ | PROT_WRITE,
		   shared ? MAP_SHARED : MAP_PRIVATE, fd, 0);
	if (mem == MAP_FAILED) {
		if (size == pagesize || shared) {
			ksft_print_msg("mmap() failed (%s)\n", strerror(errno));
			result = KSFT_FAIL;
		} else {
			ksft_print_msg("need more free huge pages\n");
			result = KSFT_SKIP;
		}
		goto report;
	}

	/* Fault in the page such that GUP-fast can pin it directly. */
	memset(mem, 0, size);

	switch (type) {
	case TEST_TYPE_RO:
	case TEST_TYPE_RO_FAST:
		/*
		 * Cover more cases regarding unsharing decisions when
		 * long-term R/O pinning by mapping the page R/O.
		 */
		ret = mprotect(mem, size, PROT_READ);
		if (ret) {
			ksft_print_msg("mprotect() failed (%s)\n", strerror(errno));
			result = KSFT_FAIL;
			goto munmap;
		}
		/* FALLTHROUGH */
	case TEST_TYPE_RW:
	case TEST_TYPE_RW_FAST: {
		struct pin_longterm_test args;
		const bool fast = type == TEST_TYPE_RO_FAST ||
				  type == TEST_TYPE_RW_FAST;
		const bool rw = type == TEST_TYPE_RW ||
				type == TEST_TYPE_RW_FAST;

		if (gup_fd < 0) {
			ksft_print_msg("gup_test not available\n");
			result = KSFT_SKIP;
			break;
		}

		if (rw && shared && fs_is_unknown(fs_type)) {
			ksft_print_msg("Unknown filesystem\n");
			result = KSFT_SKIP;
			return;
		}
		/*
		 * R/O pinning or pinning in a private mapping is always
		 * expected to work. Otherwise, we expect long-term R/W pinning
		 * to only succeed for special filesystems.
		 */
		should_work = !shared || !rw ||
			      fs_supports_writable_longterm_pinning(fs_type);

		args.addr = (__u64)(uintptr_t)mem;
		args.size = size;
		args.flags = fast ? PIN_LONGTERM_TEST_FLAG_USE_FAST : 0;
		args.flags |= rw ? PIN_LONGTERM_TEST_FLAG_USE_WRITE : 0;
		ret = ioctl(gup_fd, PIN_LONGTERM_TEST_START, &args);
		if (ret && errno == EINVAL) {
			ksft_print_msg("PIN_LONGTERM_TEST_START failed (EINVAL)n");
			result = KSFT_SKIP;
			break;
		} else if (ret && errno == EFAULT) {
			if (should_work)
				result = KSFT_FAIL;
			else
				result = KSFT_PASS;
			break;
		} else if (ret) {
			ksft_print_msg("PIN_LONGTERM_TEST_START failed (%s)\n",
				       strerror(errno));
			result = KSFT_FAIL;
			break;
		}

		if (ioctl(gup_fd, PIN_LONGTERM_TEST_STOP))
			ksft_print_msg("[INFO] PIN_LONGTERM_TEST_STOP failed (%s)\n",
				       strerror(errno));

		/*
		 * TODO: if the kernel ever supports long-term R/W pinning on
		 * some previously unsupported filesystems, we might want to
		 * perform some additional tests for possible data corruptions.
		 */
		if (should_work)
			result = KSFT_PASS;
		else
			result = KSFT_FAIL;
		break;
	}
#ifdef LOCAL_CONFIG_HAVE_LIBURING
	case TEST_TYPE_IOURING: {
		struct io_uring ring;
		struct iovec iov;

		/* io_uring always pins pages writable. */
		if (shared && fs_is_unknown(fs_type)) {
			ksft_print_msg("Unknown filesystem\n");
			result = KSFT_SKIP;
			goto report;
		}
		should_work = !shared ||
			      fs_supports_writable_longterm_pinning(fs_type);

		/* Skip on errors, as we might just lack kernel support. */
		ret = io_uring_queue_init(1, &ring, 0);
		if (ret < 0) {
			ksft_print_msg("io_uring_queue_init() failed (%s)\n",
				       strerror(-ret));
			result = KSFT_SKIP;
			break;
		}
		/*
		 * Register the range as a fixed buffer. This will FOLL_WRITE |
		 * FOLL_PIN | FOLL_LONGTERM the range.
		 */
		iov.iov_base = mem;
		iov.iov_len = size;
		ret = io_uring_register_buffers(&ring, &iov, 1);
		/* Only new kernels return EFAULT. */
		if (ret && (errno == ENOSPC || errno == EOPNOTSUPP ||
			    errno == EFAULT)) {
			if (should_work) {
				ksft_print_msg("Should have failed (%s)\n",
					       strerror(errno));
				result = KSFT_FAIL;
			} else {
				result = KSFT_PASS;
			}
		} else if (ret) {
			/*
			 * We might just lack support or have insufficient
			 * MEMLOCK limits.
			 */
			ksft_print_msg("io_uring_register_buffers() failed (%s)\n",
				       strerror(-ret));
			result = KSFT_SKIP;
		} else {
			if (should_work) {
				result = KSFT_PASS;
			} else {
				ksft_print_msg("Should have worked\n");
				result = KSFT_FAIL;
			}
			io_uring_unregister_buffers(&ring);
		}

		io_uring_queue_exit(&ring);
		break;
	}
#endif /* LOCAL_CONFIG_HAVE_LIBURING */
	default:
		assert(false);
	}

munmap:
	munmap(mem, size);
report:
	log_test_result(result);
}

typedef void (*test_fn)(int fd, size_t size);

static void run_with_memfd(test_fn fn, const char *desc)
{
	int fd;

	log_test_start("%s ... with memfd", desc);

	fd = memfd_create("test", 0);
	if (fd < 0) {
		ksft_print_msg("memfd_create() failed (%s)\n", strerror(errno));
		log_test_result(KSFT_SKIP);
		return;
	}

	fn(fd, pagesize);
	close(fd);
}

static void run_with_tmpfile(test_fn fn, const char *desc)
{
	FILE *file;
	int fd;

	log_test_start("%s ... with tmpfile", desc);

	file = tmpfile();
	if (!file) {
		ksft_print_msg("tmpfile() failed (%s)\n", strerror(errno));
		fd = -1;
	} else {
		fd = fileno(file);
		if (fd < 0) {
			ksft_print_msg("fileno() failed (%s)\n", strerror(errno));
		}
	}

	fn(fd, pagesize);

	if (file)
		fclose(file);
}

static void run_with_local_tmpfile(test_fn fn, const char *desc)
{
	char filename[] = __FILE__"_tmpfile_XXXXXX";
	int fd;

	log_test_start("%s ... with local tmpfile", desc);

	fd = mkstemp(filename);
	if (fd < 0)
		ksft_print_msg("mkstemp() failed (%s)\n", strerror(errno));

	if (unlink(filename)) {
		ksft_print_msg("unlink() failed (%s)\n", strerror(errno));
		close(fd);
		fd = -1;
	}

	fn(fd, pagesize);

	if (fd >= 0)
		close(fd);
}

static void run_with_memfd_hugetlb(test_fn fn, const char *desc,
				   size_t hugetlbsize)
{
	int flags = MFD_HUGETLB;
	int fd;

	log_test_start("%s ... with memfd hugetlb (%zu kB)", desc,
		       hugetlbsize / 1024);

	flags |= __builtin_ctzll(hugetlbsize) << MFD_HUGE_SHIFT;

	fd = memfd_create("test", flags);
	if (fd < 0) {
		ksft_print_msg("memfd_create() failed (%s)\n", strerror(errno));
		log_test_result(KSFT_SKIP);
		return;
	}

	fn(fd, hugetlbsize);
	close(fd);
}

struct test_case {
	const char *desc;
	test_fn fn;
};

static void test_shared_rw_pin(int fd, size_t size)
{
	do_test(fd, size, TEST_TYPE_RW, true);
}

static void test_shared_rw_fast_pin(int fd, size_t size)
{
	do_test(fd, size, TEST_TYPE_RW_FAST, true);
}

static void test_shared_ro_pin(int fd, size_t size)
{
	do_test(fd, size, TEST_TYPE_RO, true);
}

static void test_shared_ro_fast_pin(int fd, size_t size)
{
	do_test(fd, size, TEST_TYPE_RO_FAST, true);
}

static void test_private_rw_pin(int fd, size_t size)
{
	do_test(fd, size, TEST_TYPE_RW, false);
}

static void test_private_rw_fast_pin(int fd, size_t size)
{
	do_test(fd, size, TEST_TYPE_RW_FAST, false);
}

static void test_private_ro_pin(int fd, size_t size)
{
	do_test(fd, size, TEST_TYPE_RO, false);
}

static void test_private_ro_fast_pin(int fd, size_t size)
{
	do_test(fd, size, TEST_TYPE_RO_FAST, false);
}

#ifdef LOCAL_CONFIG_HAVE_LIBURING
static void test_shared_iouring(int fd, size_t size)
{
	do_test(fd, size, TEST_TYPE_IOURING, true);
}

static void test_private_iouring(int fd, size_t size)
{
	do_test(fd, size, TEST_TYPE_IOURING, false);
}
#endif /* LOCAL_CONFIG_HAVE_LIBURING */

static const struct test_case test_cases[] = {
	{
		"R/W longterm GUP pin in MAP_SHARED file mapping",
		test_shared_rw_pin,
	},
	{
		"R/W longterm GUP-fast pin in MAP_SHARED file mapping",
		test_shared_rw_fast_pin,
	},
	{
		"R/O longterm GUP pin in MAP_SHARED file mapping",
		test_shared_ro_pin,
	},
	{
		"R/O longterm GUP-fast pin in MAP_SHARED file mapping",
		test_shared_ro_fast_pin,
	},
	{
		"R/W longterm GUP pin in MAP_PRIVATE file mapping",
		test_private_rw_pin,
	},
	{
		"R/W longterm GUP-fast pin in MAP_PRIVATE file mapping",
		test_private_rw_fast_pin,
	},
	{
		"R/O longterm GUP pin in MAP_PRIVATE file mapping",
		test_private_ro_pin,
	},
	{
		"R/O longterm GUP-fast pin in MAP_PRIVATE file mapping",
		test_private_ro_fast_pin,
	},
#ifdef LOCAL_CONFIG_HAVE_LIBURING
	{
		"io_uring fixed buffer with MAP_SHARED file mapping",
		test_shared_iouring,
	},
	{
		"io_uring fixed buffer with MAP_PRIVATE file mapping",
		test_private_iouring,
	},
#endif /* LOCAL_CONFIG_HAVE_LIBURING */
};

static void run_test_case(struct test_case const *test_case)
{
	int i;

	run_with_memfd(test_case->fn, test_case->desc);
	run_with_tmpfile(test_case->fn, test_case->desc);
	run_with_local_tmpfile(test_case->fn, test_case->desc);
	for (i = 0; i < nr_hugetlbsizes; i++)
		run_with_memfd_hugetlb(test_case->fn, test_case->desc,
				       hugetlbsizes[i]);
}

static int tests_per_test_case(void)
{
	return 3 + nr_hugetlbsizes;
}

int main(int argc, char **argv)
{
	int i;

	pagesize = getpagesize();
	nr_hugetlbsizes = detect_hugetlb_page_sizes(hugetlbsizes,
						    ARRAY_SIZE(hugetlbsizes));

	ksft_print_header();
	ksft_set_plan(ARRAY_SIZE(test_cases) * tests_per_test_case());

	gup_fd = open("/sys/kernel/debug/gup_test", O_RDWR);

	for (i = 0; i < ARRAY_SIZE(test_cases); i++)
		run_test_case(&test_cases[i]);

	ksft_finished();
}
