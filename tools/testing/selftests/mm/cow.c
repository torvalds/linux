// SPDX-License-Identifier: GPL-2.0-only
/*
 * COW (Copy On Write) tests.
 *
 * Copyright 2022, Red Hat, Inc.
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
#include <linux/mman.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <linux/memfd.h>

#include "local_config.h"
#ifdef LOCAL_CONFIG_HAVE_LIBURING
#include <liburing.h>
#endif /* LOCAL_CONFIG_HAVE_LIBURING */

#include "../../../../mm/gup_test.h"
#include "../kselftest.h"
#include "vm_util.h"

static size_t pagesize;
static int pagemap_fd;
static size_t thpsize;
static int nr_hugetlbsizes;
static size_t hugetlbsizes[10];
static int gup_fd;
static bool has_huge_zeropage;

static void detect_huge_zeropage(void)
{
	int fd = open("/sys/kernel/mm/transparent_hugepage/use_zero_page",
		      O_RDONLY);
	size_t enabled = 0;
	char buf[15];
	int ret;

	if (fd < 0)
		return;

	ret = pread(fd, buf, sizeof(buf), 0);
	if (ret > 0 && ret < sizeof(buf)) {
		buf[ret] = 0;

		enabled = strtoul(buf, NULL, 10);
		if (enabled == 1) {
			has_huge_zeropage = true;
			ksft_print_msg("[INFO] huge zeropage is enabled\n");
		}
	}

	close(fd);
}

static bool range_is_swapped(void *addr, size_t size)
{
	for (; size; addr += pagesize, size -= pagesize)
		if (!pagemap_is_swapped(pagemap_fd, addr))
			return false;
	return true;
}

struct comm_pipes {
	int child_ready[2];
	int parent_ready[2];
};

static int setup_comm_pipes(struct comm_pipes *comm_pipes)
{
	if (pipe(comm_pipes->child_ready) < 0)
		return -errno;
	if (pipe(comm_pipes->parent_ready) < 0) {
		close(comm_pipes->child_ready[0]);
		close(comm_pipes->child_ready[1]);
		return -errno;
	}

	return 0;
}

static void close_comm_pipes(struct comm_pipes *comm_pipes)
{
	close(comm_pipes->child_ready[0]);
	close(comm_pipes->child_ready[1]);
	close(comm_pipes->parent_ready[0]);
	close(comm_pipes->parent_ready[1]);
}

static int child_memcmp_fn(char *mem, size_t size,
			   struct comm_pipes *comm_pipes)
{
	char *old = malloc(size);
	char buf;

	/* Backup the original content. */
	memcpy(old, mem, size);

	/* Wait until the parent modified the page. */
	write(comm_pipes->child_ready[1], "0", 1);
	while (read(comm_pipes->parent_ready[0], &buf, 1) != 1)
		;

	/* See if we still read the old values. */
	return memcmp(old, mem, size);
}

static int child_vmsplice_memcmp_fn(char *mem, size_t size,
				    struct comm_pipes *comm_pipes)
{
	struct iovec iov = {
		.iov_base = mem,
		.iov_len = size,
	};
	ssize_t cur, total, transferred;
	char *old, *new;
	int fds[2];
	char buf;

	old = malloc(size);
	new = malloc(size);

	/* Backup the original content. */
	memcpy(old, mem, size);

	if (pipe(fds) < 0)
		return -errno;

	/* Trigger a read-only pin. */
	transferred = vmsplice(fds[1], &iov, 1, 0);
	if (transferred < 0)
		return -errno;
	if (transferred == 0)
		return -EINVAL;

	/* Unmap it from our page tables. */
	if (munmap(mem, size) < 0)
		return -errno;

	/* Wait until the parent modified it. */
	write(comm_pipes->child_ready[1], "0", 1);
	while (read(comm_pipes->parent_ready[0], &buf, 1) != 1)
		;

	/* See if we still read the old values via the pipe. */
	for (total = 0; total < transferred; total += cur) {
		cur = read(fds[0], new + total, transferred - total);
		if (cur < 0)
			return -errno;
	}

	return memcmp(old, new, transferred);
}

typedef int (*child_fn)(char *mem, size_t size, struct comm_pipes *comm_pipes);

static void do_test_cow_in_parent(char *mem, size_t size, bool do_mprotect,
				  child_fn fn)
{
	struct comm_pipes comm_pipes;
	char buf;
	int ret;

	ret = setup_comm_pipes(&comm_pipes);
	if (ret) {
		ksft_test_result_fail("pipe() failed\n");
		return;
	}

	ret = fork();
	if (ret < 0) {
		ksft_test_result_fail("fork() failed\n");
		goto close_comm_pipes;
	} else if (!ret) {
		exit(fn(mem, size, &comm_pipes));
	}

	while (read(comm_pipes.child_ready[0], &buf, 1) != 1)
		;

	if (do_mprotect) {
		/*
		 * mprotect() optimizations might try avoiding
		 * write-faults by directly mapping pages writable.
		 */
		ret = mprotect(mem, size, PROT_READ);
		ret |= mprotect(mem, size, PROT_READ|PROT_WRITE);
		if (ret) {
			ksft_test_result_fail("mprotect() failed\n");
			write(comm_pipes.parent_ready[1], "0", 1);
			wait(&ret);
			goto close_comm_pipes;
		}
	}

	/* Modify the page. */
	memset(mem, 0xff, size);
	write(comm_pipes.parent_ready[1], "0", 1);

	wait(&ret);
	if (WIFEXITED(ret))
		ret = WEXITSTATUS(ret);
	else
		ret = -EINVAL;

	ksft_test_result(!ret, "No leak from parent into child\n");
close_comm_pipes:
	close_comm_pipes(&comm_pipes);
}

static void test_cow_in_parent(char *mem, size_t size)
{
	do_test_cow_in_parent(mem, size, false, child_memcmp_fn);
}

static void test_cow_in_parent_mprotect(char *mem, size_t size)
{
	do_test_cow_in_parent(mem, size, true, child_memcmp_fn);
}

static void test_vmsplice_in_child(char *mem, size_t size)
{
	do_test_cow_in_parent(mem, size, false, child_vmsplice_memcmp_fn);
}

static void test_vmsplice_in_child_mprotect(char *mem, size_t size)
{
	do_test_cow_in_parent(mem, size, true, child_vmsplice_memcmp_fn);
}

static void do_test_vmsplice_in_parent(char *mem, size_t size,
				       bool before_fork)
{
	struct iovec iov = {
		.iov_base = mem,
		.iov_len = size,
	};
	ssize_t cur, total, transferred;
	struct comm_pipes comm_pipes;
	char *old, *new;
	int ret, fds[2];
	char buf;

	old = malloc(size);
	new = malloc(size);

	memcpy(old, mem, size);

	ret = setup_comm_pipes(&comm_pipes);
	if (ret) {
		ksft_test_result_fail("pipe() failed\n");
		goto free;
	}

	if (pipe(fds) < 0) {
		ksft_test_result_fail("pipe() failed\n");
		goto close_comm_pipes;
	}

	if (before_fork) {
		transferred = vmsplice(fds[1], &iov, 1, 0);
		if (transferred <= 0) {
			ksft_test_result_fail("vmsplice() failed\n");
			goto close_pipe;
		}
	}

	ret = fork();
	if (ret < 0) {
		ksft_test_result_fail("fork() failed\n");
		goto close_pipe;
	} else if (!ret) {
		write(comm_pipes.child_ready[1], "0", 1);
		while (read(comm_pipes.parent_ready[0], &buf, 1) != 1)
			;
		/* Modify page content in the child. */
		memset(mem, 0xff, size);
		exit(0);
	}

	if (!before_fork) {
		transferred = vmsplice(fds[1], &iov, 1, 0);
		if (transferred <= 0) {
			ksft_test_result_fail("vmsplice() failed\n");
			wait(&ret);
			goto close_pipe;
		}
	}

	while (read(comm_pipes.child_ready[0], &buf, 1) != 1)
		;
	if (munmap(mem, size) < 0) {
		ksft_test_result_fail("munmap() failed\n");
		goto close_pipe;
	}
	write(comm_pipes.parent_ready[1], "0", 1);

	/* Wait until the child is done writing. */
	wait(&ret);
	if (!WIFEXITED(ret)) {
		ksft_test_result_fail("wait() failed\n");
		goto close_pipe;
	}

	/* See if we still read the old values. */
	for (total = 0; total < transferred; total += cur) {
		cur = read(fds[0], new + total, transferred - total);
		if (cur < 0) {
			ksft_test_result_fail("read() failed\n");
			goto close_pipe;
		}
	}

	ksft_test_result(!memcmp(old, new, transferred),
			 "No leak from child into parent\n");
close_pipe:
	close(fds[0]);
	close(fds[1]);
close_comm_pipes:
	close_comm_pipes(&comm_pipes);
free:
	free(old);
	free(new);
}

static void test_vmsplice_before_fork(char *mem, size_t size)
{
	do_test_vmsplice_in_parent(mem, size, true);
}

static void test_vmsplice_after_fork(char *mem, size_t size)
{
	do_test_vmsplice_in_parent(mem, size, false);
}

#ifdef LOCAL_CONFIG_HAVE_LIBURING
static void do_test_iouring(char *mem, size_t size, bool use_fork)
{
	struct comm_pipes comm_pipes;
	struct io_uring_cqe *cqe;
	struct io_uring_sqe *sqe;
	struct io_uring ring;
	ssize_t cur, total;
	struct iovec iov;
	char *buf, *tmp;
	int ret, fd;
	FILE *file;

	ret = setup_comm_pipes(&comm_pipes);
	if (ret) {
		ksft_test_result_fail("pipe() failed\n");
		return;
	}

	file = tmpfile();
	if (!file) {
		ksft_test_result_fail("tmpfile() failed\n");
		goto close_comm_pipes;
	}
	fd = fileno(file);
	assert(fd);

	tmp = malloc(size);
	if (!tmp) {
		ksft_test_result_fail("malloc() failed\n");
		goto close_file;
	}

	/* Skip on errors, as we might just lack kernel support. */
	ret = io_uring_queue_init(1, &ring, 0);
	if (ret < 0) {
		ksft_test_result_skip("io_uring_queue_init() failed\n");
		goto free_tmp;
	}

	/*
	 * Register the range as a fixed buffer. This will FOLL_WRITE | FOLL_PIN
	 * | FOLL_LONGTERM the range.
	 *
	 * Skip on errors, as we might just lack kernel support or might not
	 * have sufficient MEMLOCK permissions.
	 */
	iov.iov_base = mem;
	iov.iov_len = size;
	ret = io_uring_register_buffers(&ring, &iov, 1);
	if (ret) {
		ksft_test_result_skip("io_uring_register_buffers() failed\n");
		goto queue_exit;
	}

	if (use_fork) {
		/*
		 * fork() and keep the child alive until we're done. Note that
		 * we expect the pinned page to not get shared with the child.
		 */
		ret = fork();
		if (ret < 0) {
			ksft_test_result_fail("fork() failed\n");
			goto unregister_buffers;
		} else if (!ret) {
			write(comm_pipes.child_ready[1], "0", 1);
			while (read(comm_pipes.parent_ready[0], &buf, 1) != 1)
				;
			exit(0);
		}

		while (read(comm_pipes.child_ready[0], &buf, 1) != 1)
			;
	} else {
		/*
		 * Map the page R/O into the page table. Enable softdirty
		 * tracking to stop the page from getting mapped R/W immediately
		 * again by mprotect() optimizations. Note that we don't have an
		 * easy way to test if that worked (the pagemap does not export
		 * if the page is mapped R/O vs. R/W).
		 */
		ret = mprotect(mem, size, PROT_READ);
		clear_softdirty();
		ret |= mprotect(mem, size, PROT_READ | PROT_WRITE);
		if (ret) {
			ksft_test_result_fail("mprotect() failed\n");
			goto unregister_buffers;
		}
	}

	/*
	 * Modify the page and write page content as observed by the fixed
	 * buffer pin to the file so we can verify it.
	 */
	memset(mem, 0xff, size);
	sqe = io_uring_get_sqe(&ring);
	if (!sqe) {
		ksft_test_result_fail("io_uring_get_sqe() failed\n");
		goto quit_child;
	}
	io_uring_prep_write_fixed(sqe, fd, mem, size, 0, 0);

	ret = io_uring_submit(&ring);
	if (ret < 0) {
		ksft_test_result_fail("io_uring_submit() failed\n");
		goto quit_child;
	}

	ret = io_uring_wait_cqe(&ring, &cqe);
	if (ret < 0) {
		ksft_test_result_fail("io_uring_wait_cqe() failed\n");
		goto quit_child;
	}

	if (cqe->res != size) {
		ksft_test_result_fail("write_fixed failed\n");
		goto quit_child;
	}
	io_uring_cqe_seen(&ring, cqe);

	/* Read back the file content to the temporary buffer. */
	total = 0;
	while (total < size) {
		cur = pread(fd, tmp + total, size - total, total);
		if (cur < 0) {
			ksft_test_result_fail("pread() failed\n");
			goto quit_child;
		}
		total += cur;
	}

	/* Finally, check if we read what we expected. */
	ksft_test_result(!memcmp(mem, tmp, size),
			 "Longterm R/W pin is reliable\n");

quit_child:
	if (use_fork) {
		write(comm_pipes.parent_ready[1], "0", 1);
		wait(&ret);
	}
unregister_buffers:
	io_uring_unregister_buffers(&ring);
queue_exit:
	io_uring_queue_exit(&ring);
free_tmp:
	free(tmp);
close_file:
	fclose(file);
close_comm_pipes:
	close_comm_pipes(&comm_pipes);
}

static void test_iouring_ro(char *mem, size_t size)
{
	do_test_iouring(mem, size, false);
}

static void test_iouring_fork(char *mem, size_t size)
{
	do_test_iouring(mem, size, true);
}

#endif /* LOCAL_CONFIG_HAVE_LIBURING */

enum ro_pin_test {
	RO_PIN_TEST,
	RO_PIN_TEST_SHARED,
	RO_PIN_TEST_PREVIOUSLY_SHARED,
	RO_PIN_TEST_RO_EXCLUSIVE,
};

static void do_test_ro_pin(char *mem, size_t size, enum ro_pin_test test,
			   bool fast)
{
	struct pin_longterm_test args;
	struct comm_pipes comm_pipes;
	char *tmp, buf;
	__u64 tmp_val;
	int ret;

	if (gup_fd < 0) {
		ksft_test_result_skip("gup_test not available\n");
		return;
	}

	tmp = malloc(size);
	if (!tmp) {
		ksft_test_result_fail("malloc() failed\n");
		return;
	}

	ret = setup_comm_pipes(&comm_pipes);
	if (ret) {
		ksft_test_result_fail("pipe() failed\n");
		goto free_tmp;
	}

	switch (test) {
	case RO_PIN_TEST:
		break;
	case RO_PIN_TEST_SHARED:
	case RO_PIN_TEST_PREVIOUSLY_SHARED:
		/*
		 * Share the pages with our child. As the pages are not pinned,
		 * this should just work.
		 */
		ret = fork();
		if (ret < 0) {
			ksft_test_result_fail("fork() failed\n");
			goto close_comm_pipes;
		} else if (!ret) {
			write(comm_pipes.child_ready[1], "0", 1);
			while (read(comm_pipes.parent_ready[0], &buf, 1) != 1)
				;
			exit(0);
		}

		/* Wait until our child is ready. */
		while (read(comm_pipes.child_ready[0], &buf, 1) != 1)
			;

		if (test == RO_PIN_TEST_PREVIOUSLY_SHARED) {
			/*
			 * Tell the child to quit now and wait until it quit.
			 * The pages should now be mapped R/O into our page
			 * tables, but they are no longer shared.
			 */
			write(comm_pipes.parent_ready[1], "0", 1);
			wait(&ret);
			if (!WIFEXITED(ret))
				ksft_print_msg("[INFO] wait() failed\n");
		}
		break;
	case RO_PIN_TEST_RO_EXCLUSIVE:
		/*
		 * Map the page R/O into the page table. Enable softdirty
		 * tracking to stop the page from getting mapped R/W immediately
		 * again by mprotect() optimizations. Note that we don't have an
		 * easy way to test if that worked (the pagemap does not export
		 * if the page is mapped R/O vs. R/W).
		 */
		ret = mprotect(mem, size, PROT_READ);
		clear_softdirty();
		ret |= mprotect(mem, size, PROT_READ | PROT_WRITE);
		if (ret) {
			ksft_test_result_fail("mprotect() failed\n");
			goto close_comm_pipes;
		}
		break;
	default:
		assert(false);
	}

	/* Take a R/O pin. This should trigger unsharing. */
	args.addr = (__u64)(uintptr_t)mem;
	args.size = size;
	args.flags = fast ? PIN_LONGTERM_TEST_FLAG_USE_FAST : 0;
	ret = ioctl(gup_fd, PIN_LONGTERM_TEST_START, &args);
	if (ret) {
		if (errno == EINVAL)
			ksft_test_result_skip("PIN_LONGTERM_TEST_START failed\n");
		else
			ksft_test_result_fail("PIN_LONGTERM_TEST_START failed\n");
		goto wait;
	}

	/* Modify the page. */
	memset(mem, 0xff, size);

	/*
	 * Read back the content via the pin to the temporary buffer and
	 * test if we observed the modification.
	 */
	tmp_val = (__u64)(uintptr_t)tmp;
	ret = ioctl(gup_fd, PIN_LONGTERM_TEST_READ, &tmp_val);
	if (ret)
		ksft_test_result_fail("PIN_LONGTERM_TEST_READ failed\n");
	else
		ksft_test_result(!memcmp(mem, tmp, size),
				 "Longterm R/O pin is reliable\n");

	ret = ioctl(gup_fd, PIN_LONGTERM_TEST_STOP);
	if (ret)
		ksft_print_msg("[INFO] PIN_LONGTERM_TEST_STOP failed\n");
wait:
	switch (test) {
	case RO_PIN_TEST_SHARED:
		write(comm_pipes.parent_ready[1], "0", 1);
		wait(&ret);
		if (!WIFEXITED(ret))
			ksft_print_msg("[INFO] wait() failed\n");
		break;
	default:
		break;
	}
close_comm_pipes:
	close_comm_pipes(&comm_pipes);
free_tmp:
	free(tmp);
}

static void test_ro_pin_on_shared(char *mem, size_t size)
{
	do_test_ro_pin(mem, size, RO_PIN_TEST_SHARED, false);
}

static void test_ro_fast_pin_on_shared(char *mem, size_t size)
{
	do_test_ro_pin(mem, size, RO_PIN_TEST_SHARED, true);
}

static void test_ro_pin_on_ro_previously_shared(char *mem, size_t size)
{
	do_test_ro_pin(mem, size, RO_PIN_TEST_PREVIOUSLY_SHARED, false);
}

static void test_ro_fast_pin_on_ro_previously_shared(char *mem, size_t size)
{
	do_test_ro_pin(mem, size, RO_PIN_TEST_PREVIOUSLY_SHARED, true);
}

static void test_ro_pin_on_ro_exclusive(char *mem, size_t size)
{
	do_test_ro_pin(mem, size, RO_PIN_TEST_RO_EXCLUSIVE, false);
}

static void test_ro_fast_pin_on_ro_exclusive(char *mem, size_t size)
{
	do_test_ro_pin(mem, size, RO_PIN_TEST_RO_EXCLUSIVE, true);
}

typedef void (*test_fn)(char *mem, size_t size);

static void do_run_with_base_page(test_fn fn, bool swapout)
{
	char *mem;
	int ret;

	mem = mmap(NULL, pagesize, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mem == MAP_FAILED) {
		ksft_test_result_fail("mmap() failed\n");
		return;
	}

	ret = madvise(mem, pagesize, MADV_NOHUGEPAGE);
	/* Ignore if not around on a kernel. */
	if (ret && errno != EINVAL) {
		ksft_test_result_fail("MADV_NOHUGEPAGE failed\n");
		goto munmap;
	}

	/* Populate a base page. */
	memset(mem, 0, pagesize);

	if (swapout) {
		madvise(mem, pagesize, MADV_PAGEOUT);
		if (!pagemap_is_swapped(pagemap_fd, mem)) {
			ksft_test_result_skip("MADV_PAGEOUT did not work, is swap enabled?\n");
			goto munmap;
		}
	}

	fn(mem, pagesize);
munmap:
	munmap(mem, pagesize);
}

static void run_with_base_page(test_fn fn, const char *desc)
{
	ksft_print_msg("[RUN] %s ... with base page\n", desc);
	do_run_with_base_page(fn, false);
}

static void run_with_base_page_swap(test_fn fn, const char *desc)
{
	ksft_print_msg("[RUN] %s ... with swapped out base page\n", desc);
	do_run_with_base_page(fn, true);
}

enum thp_run {
	THP_RUN_PMD,
	THP_RUN_PMD_SWAPOUT,
	THP_RUN_PTE,
	THP_RUN_PTE_SWAPOUT,
	THP_RUN_SINGLE_PTE,
	THP_RUN_SINGLE_PTE_SWAPOUT,
	THP_RUN_PARTIAL_MREMAP,
	THP_RUN_PARTIAL_SHARED,
};

static void do_run_with_thp(test_fn fn, enum thp_run thp_run)
{
	char *mem, *mmap_mem, *tmp, *mremap_mem = MAP_FAILED;
	size_t size, mmap_size, mremap_size;
	int ret;

	/* For alignment purposes, we need twice the thp size. */
	mmap_size = 2 * thpsize;
	mmap_mem = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mmap_mem == MAP_FAILED) {
		ksft_test_result_fail("mmap() failed\n");
		return;
	}

	/* We need a THP-aligned memory area. */
	mem = (char *)(((uintptr_t)mmap_mem + thpsize) & ~(thpsize - 1));

	ret = madvise(mem, thpsize, MADV_HUGEPAGE);
	if (ret) {
		ksft_test_result_fail("MADV_HUGEPAGE failed\n");
		goto munmap;
	}

	/*
	 * Try to populate a THP. Touch the first sub-page and test if we get
	 * another sub-page populated automatically.
	 */
	mem[0] = 0;
	if (!pagemap_is_populated(pagemap_fd, mem + pagesize)) {
		ksft_test_result_skip("Did not get a THP populated\n");
		goto munmap;
	}
	memset(mem, 0, thpsize);

	size = thpsize;
	switch (thp_run) {
	case THP_RUN_PMD:
	case THP_RUN_PMD_SWAPOUT:
		break;
	case THP_RUN_PTE:
	case THP_RUN_PTE_SWAPOUT:
		/*
		 * Trigger PTE-mapping the THP by temporarily mapping a single
		 * subpage R/O.
		 */
		ret = mprotect(mem + pagesize, pagesize, PROT_READ);
		if (ret) {
			ksft_test_result_fail("mprotect() failed\n");
			goto munmap;
		}
		ret = mprotect(mem + pagesize, pagesize, PROT_READ | PROT_WRITE);
		if (ret) {
			ksft_test_result_fail("mprotect() failed\n");
			goto munmap;
		}
		break;
	case THP_RUN_SINGLE_PTE:
	case THP_RUN_SINGLE_PTE_SWAPOUT:
		/*
		 * Discard all but a single subpage of that PTE-mapped THP. What
		 * remains is a single PTE mapping a single subpage.
		 */
		ret = madvise(mem + pagesize, thpsize - pagesize, MADV_DONTNEED);
		if (ret) {
			ksft_test_result_fail("MADV_DONTNEED failed\n");
			goto munmap;
		}
		size = pagesize;
		break;
	case THP_RUN_PARTIAL_MREMAP:
		/*
		 * Remap half of the THP. We need some new memory location
		 * for that.
		 */
		mremap_size = thpsize / 2;
		mremap_mem = mmap(NULL, mremap_size, PROT_NONE,
				  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (mem == MAP_FAILED) {
			ksft_test_result_fail("mmap() failed\n");
			goto munmap;
		}
		tmp = mremap(mem + mremap_size, mremap_size, mremap_size,
			     MREMAP_MAYMOVE | MREMAP_FIXED, mremap_mem);
		if (tmp != mremap_mem) {
			ksft_test_result_fail("mremap() failed\n");
			goto munmap;
		}
		size = mremap_size;
		break;
	case THP_RUN_PARTIAL_SHARED:
		/*
		 * Share the first page of the THP with a child and quit the
		 * child. This will result in some parts of the THP never
		 * have been shared.
		 */
		ret = madvise(mem + pagesize, thpsize - pagesize, MADV_DONTFORK);
		if (ret) {
			ksft_test_result_fail("MADV_DONTFORK failed\n");
			goto munmap;
		}
		ret = fork();
		if (ret < 0) {
			ksft_test_result_fail("fork() failed\n");
			goto munmap;
		} else if (!ret) {
			exit(0);
		}
		wait(&ret);
		/* Allow for sharing all pages again. */
		ret = madvise(mem + pagesize, thpsize - pagesize, MADV_DOFORK);
		if (ret) {
			ksft_test_result_fail("MADV_DOFORK failed\n");
			goto munmap;
		}
		break;
	default:
		assert(false);
	}

	switch (thp_run) {
	case THP_RUN_PMD_SWAPOUT:
	case THP_RUN_PTE_SWAPOUT:
	case THP_RUN_SINGLE_PTE_SWAPOUT:
		madvise(mem, size, MADV_PAGEOUT);
		if (!range_is_swapped(mem, size)) {
			ksft_test_result_skip("MADV_PAGEOUT did not work, is swap enabled?\n");
			goto munmap;
		}
		break;
	default:
		break;
	}

	fn(mem, size);
munmap:
	munmap(mmap_mem, mmap_size);
	if (mremap_mem != MAP_FAILED)
		munmap(mremap_mem, mremap_size);
}

static void run_with_thp(test_fn fn, const char *desc)
{
	ksft_print_msg("[RUN] %s ... with THP\n", desc);
	do_run_with_thp(fn, THP_RUN_PMD);
}

static void run_with_thp_swap(test_fn fn, const char *desc)
{
	ksft_print_msg("[RUN] %s ... with swapped-out THP\n", desc);
	do_run_with_thp(fn, THP_RUN_PMD_SWAPOUT);
}

static void run_with_pte_mapped_thp(test_fn fn, const char *desc)
{
	ksft_print_msg("[RUN] %s ... with PTE-mapped THP\n", desc);
	do_run_with_thp(fn, THP_RUN_PTE);
}

static void run_with_pte_mapped_thp_swap(test_fn fn, const char *desc)
{
	ksft_print_msg("[RUN] %s ... with swapped-out, PTE-mapped THP\n", desc);
	do_run_with_thp(fn, THP_RUN_PTE_SWAPOUT);
}

static void run_with_single_pte_of_thp(test_fn fn, const char *desc)
{
	ksft_print_msg("[RUN] %s ... with single PTE of THP\n", desc);
	do_run_with_thp(fn, THP_RUN_SINGLE_PTE);
}

static void run_with_single_pte_of_thp_swap(test_fn fn, const char *desc)
{
	ksft_print_msg("[RUN] %s ... with single PTE of swapped-out THP\n", desc);
	do_run_with_thp(fn, THP_RUN_SINGLE_PTE_SWAPOUT);
}

static void run_with_partial_mremap_thp(test_fn fn, const char *desc)
{
	ksft_print_msg("[RUN] %s ... with partially mremap()'ed THP\n", desc);
	do_run_with_thp(fn, THP_RUN_PARTIAL_MREMAP);
}

static void run_with_partial_shared_thp(test_fn fn, const char *desc)
{
	ksft_print_msg("[RUN] %s ... with partially shared THP\n", desc);
	do_run_with_thp(fn, THP_RUN_PARTIAL_SHARED);
}

static void run_with_hugetlb(test_fn fn, const char *desc, size_t hugetlbsize)
{
	int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB;
	char *mem, *dummy;

	ksft_print_msg("[RUN] %s ... with hugetlb (%zu kB)\n", desc,
		       hugetlbsize / 1024);

	flags |= __builtin_ctzll(hugetlbsize) << MAP_HUGE_SHIFT;

	mem = mmap(NULL, hugetlbsize, PROT_READ | PROT_WRITE, flags, -1, 0);
	if (mem == MAP_FAILED) {
		ksft_test_result_skip("need more free huge pages\n");
		return;
	}

	/* Populate an huge page. */
	memset(mem, 0, hugetlbsize);

	/*
	 * We need a total of two hugetlb pages to handle COW/unsharing
	 * properly, otherwise we might get zapped by a SIGBUS.
	 */
	dummy = mmap(NULL, hugetlbsize, PROT_READ | PROT_WRITE, flags, -1, 0);
	if (dummy == MAP_FAILED) {
		ksft_test_result_skip("need more free huge pages\n");
		goto munmap;
	}
	munmap(dummy, hugetlbsize);

	fn(mem, hugetlbsize);
munmap:
	munmap(mem, hugetlbsize);
}

struct test_case {
	const char *desc;
	test_fn fn;
};

/*
 * Test cases that are specific to anonymous pages: pages in private mappings
 * that may get shared via COW during fork().
 */
static const struct test_case anon_test_cases[] = {
	/*
	 * Basic COW tests for fork() without any GUP. If we miss to break COW,
	 * either the child can observe modifications by the parent or the
	 * other way around.
	 */
	{
		"Basic COW after fork()",
		test_cow_in_parent,
	},
	/*
	 * Basic test, but do an additional mprotect(PROT_READ)+
	 * mprotect(PROT_READ|PROT_WRITE) in the parent before write access.
	 */
	{
		"Basic COW after fork() with mprotect() optimization",
		test_cow_in_parent_mprotect,
	},
	/*
	 * vmsplice() [R/O GUP] + unmap in the child; modify in the parent. If
	 * we miss to break COW, the child observes modifications by the parent.
	 * This is CVE-2020-29374 reported by Jann Horn.
	 */
	{
		"vmsplice() + unmap in child",
		test_vmsplice_in_child
	},
	/*
	 * vmsplice() test, but do an additional mprotect(PROT_READ)+
	 * mprotect(PROT_READ|PROT_WRITE) in the parent before write access.
	 */
	{
		"vmsplice() + unmap in child with mprotect() optimization",
		test_vmsplice_in_child_mprotect
	},
	/*
	 * vmsplice() [R/O GUP] in parent before fork(), unmap in parent after
	 * fork(); modify in the child. If we miss to break COW, the parent
	 * observes modifications by the child.
	 */
	{
		"vmsplice() before fork(), unmap in parent after fork()",
		test_vmsplice_before_fork,
	},
	/*
	 * vmsplice() [R/O GUP] + unmap in parent after fork(); modify in the
	 * child. If we miss to break COW, the parent observes modifications by
	 * the child.
	 */
	{
		"vmsplice() + unmap in parent after fork()",
		test_vmsplice_after_fork,
	},
#ifdef LOCAL_CONFIG_HAVE_LIBURING
	/*
	 * Take a R/W longterm pin and then map the page R/O into the page
	 * table to trigger a write fault on next access. When modifying the
	 * page, the page content must be visible via the pin.
	 */
	{
		"R/O-mapping a page registered as iouring fixed buffer",
		test_iouring_ro,
	},
	/*
	 * Take a R/W longterm pin and then fork() a child. When modifying the
	 * page, the page content must be visible via the pin. We expect the
	 * pinned page to not get shared with the child.
	 */
	{
		"fork() with an iouring fixed buffer",
		test_iouring_fork,
	},

#endif /* LOCAL_CONFIG_HAVE_LIBURING */
	/*
	 * Take a R/O longterm pin on a R/O-mapped shared anonymous page.
	 * When modifying the page via the page table, the page content change
	 * must be visible via the pin.
	 */
	{
		"R/O GUP pin on R/O-mapped shared page",
		test_ro_pin_on_shared,
	},
	/* Same as above, but using GUP-fast. */
	{
		"R/O GUP-fast pin on R/O-mapped shared page",
		test_ro_fast_pin_on_shared,
	},
	/*
	 * Take a R/O longterm pin on a R/O-mapped exclusive anonymous page that
	 * was previously shared. When modifying the page via the page table,
	 * the page content change must be visible via the pin.
	 */
	{
		"R/O GUP pin on R/O-mapped previously-shared page",
		test_ro_pin_on_ro_previously_shared,
	},
	/* Same as above, but using GUP-fast. */
	{
		"R/O GUP-fast pin on R/O-mapped previously-shared page",
		test_ro_fast_pin_on_ro_previously_shared,
	},
	/*
	 * Take a R/O longterm pin on a R/O-mapped exclusive anonymous page.
	 * When modifying the page via the page table, the page content change
	 * must be visible via the pin.
	 */
	{
		"R/O GUP pin on R/O-mapped exclusive page",
		test_ro_pin_on_ro_exclusive,
	},
	/* Same as above, but using GUP-fast. */
	{
		"R/O GUP-fast pin on R/O-mapped exclusive page",
		test_ro_fast_pin_on_ro_exclusive,
	},
};

static void run_anon_test_case(struct test_case const *test_case)
{
	int i;

	run_with_base_page(test_case->fn, test_case->desc);
	run_with_base_page_swap(test_case->fn, test_case->desc);
	if (thpsize) {
		run_with_thp(test_case->fn, test_case->desc);
		run_with_thp_swap(test_case->fn, test_case->desc);
		run_with_pte_mapped_thp(test_case->fn, test_case->desc);
		run_with_pte_mapped_thp_swap(test_case->fn, test_case->desc);
		run_with_single_pte_of_thp(test_case->fn, test_case->desc);
		run_with_single_pte_of_thp_swap(test_case->fn, test_case->desc);
		run_with_partial_mremap_thp(test_case->fn, test_case->desc);
		run_with_partial_shared_thp(test_case->fn, test_case->desc);
	}
	for (i = 0; i < nr_hugetlbsizes; i++)
		run_with_hugetlb(test_case->fn, test_case->desc,
				 hugetlbsizes[i]);
}

static void run_anon_test_cases(void)
{
	int i;

	ksft_print_msg("[INFO] Anonymous memory tests in private mappings\n");

	for (i = 0; i < ARRAY_SIZE(anon_test_cases); i++)
		run_anon_test_case(&anon_test_cases[i]);
}

static int tests_per_anon_test_case(void)
{
	int tests = 2 + nr_hugetlbsizes;

	if (thpsize)
		tests += 8;
	return tests;
}

enum anon_thp_collapse_test {
	ANON_THP_COLLAPSE_UNSHARED,
	ANON_THP_COLLAPSE_FULLY_SHARED,
	ANON_THP_COLLAPSE_LOWER_SHARED,
	ANON_THP_COLLAPSE_UPPER_SHARED,
};

static void do_test_anon_thp_collapse(char *mem, size_t size,
				      enum anon_thp_collapse_test test)
{
	struct comm_pipes comm_pipes;
	char buf;
	int ret;

	ret = setup_comm_pipes(&comm_pipes);
	if (ret) {
		ksft_test_result_fail("pipe() failed\n");
		return;
	}

	/*
	 * Trigger PTE-mapping the THP by temporarily mapping a single subpage
	 * R/O, such that we can try collapsing it later.
	 */
	ret = mprotect(mem + pagesize, pagesize, PROT_READ);
	if (ret) {
		ksft_test_result_fail("mprotect() failed\n");
		goto close_comm_pipes;
	}
	ret = mprotect(mem + pagesize, pagesize, PROT_READ | PROT_WRITE);
	if (ret) {
		ksft_test_result_fail("mprotect() failed\n");
		goto close_comm_pipes;
	}

	switch (test) {
	case ANON_THP_COLLAPSE_UNSHARED:
		/* Collapse before actually COW-sharing the page. */
		ret = madvise(mem, size, MADV_COLLAPSE);
		if (ret) {
			ksft_test_result_skip("MADV_COLLAPSE failed: %s\n",
					      strerror(errno));
			goto close_comm_pipes;
		}
		break;
	case ANON_THP_COLLAPSE_FULLY_SHARED:
		/* COW-share the full PTE-mapped THP. */
		break;
	case ANON_THP_COLLAPSE_LOWER_SHARED:
		/* Don't COW-share the upper part of the THP. */
		ret = madvise(mem + size / 2, size / 2, MADV_DONTFORK);
		if (ret) {
			ksft_test_result_fail("MADV_DONTFORK failed\n");
			goto close_comm_pipes;
		}
		break;
	case ANON_THP_COLLAPSE_UPPER_SHARED:
		/* Don't COW-share the lower part of the THP. */
		ret = madvise(mem, size / 2, MADV_DONTFORK);
		if (ret) {
			ksft_test_result_fail("MADV_DONTFORK failed\n");
			goto close_comm_pipes;
		}
		break;
	default:
		assert(false);
	}

	ret = fork();
	if (ret < 0) {
		ksft_test_result_fail("fork() failed\n");
		goto close_comm_pipes;
	} else if (!ret) {
		switch (test) {
		case ANON_THP_COLLAPSE_UNSHARED:
		case ANON_THP_COLLAPSE_FULLY_SHARED:
			exit(child_memcmp_fn(mem, size, &comm_pipes));
			break;
		case ANON_THP_COLLAPSE_LOWER_SHARED:
			exit(child_memcmp_fn(mem, size / 2, &comm_pipes));
			break;
		case ANON_THP_COLLAPSE_UPPER_SHARED:
			exit(child_memcmp_fn(mem + size / 2, size / 2,
					     &comm_pipes));
			break;
		default:
			assert(false);
		}
	}

	while (read(comm_pipes.child_ready[0], &buf, 1) != 1)
		;

	switch (test) {
	case ANON_THP_COLLAPSE_UNSHARED:
		break;
	case ANON_THP_COLLAPSE_UPPER_SHARED:
	case ANON_THP_COLLAPSE_LOWER_SHARED:
		/*
		 * Revert MADV_DONTFORK such that we merge the VMAs and are
		 * able to actually collapse.
		 */
		ret = madvise(mem, size, MADV_DOFORK);
		if (ret) {
			ksft_test_result_fail("MADV_DOFORK failed\n");
			write(comm_pipes.parent_ready[1], "0", 1);
			wait(&ret);
			goto close_comm_pipes;
		}
		/* FALLTHROUGH */
	case ANON_THP_COLLAPSE_FULLY_SHARED:
		/* Collapse before anyone modified the COW-shared page. */
		ret = madvise(mem, size, MADV_COLLAPSE);
		if (ret) {
			ksft_test_result_skip("MADV_COLLAPSE failed: %s\n",
					      strerror(errno));
			write(comm_pipes.parent_ready[1], "0", 1);
			wait(&ret);
			goto close_comm_pipes;
		}
		break;
	default:
		assert(false);
	}

	/* Modify the page. */
	memset(mem, 0xff, size);
	write(comm_pipes.parent_ready[1], "0", 1);

	wait(&ret);
	if (WIFEXITED(ret))
		ret = WEXITSTATUS(ret);
	else
		ret = -EINVAL;

	ksft_test_result(!ret, "No leak from parent into child\n");
close_comm_pipes:
	close_comm_pipes(&comm_pipes);
}

static void test_anon_thp_collapse_unshared(char *mem, size_t size)
{
	do_test_anon_thp_collapse(mem, size, ANON_THP_COLLAPSE_UNSHARED);
}

static void test_anon_thp_collapse_fully_shared(char *mem, size_t size)
{
	do_test_anon_thp_collapse(mem, size, ANON_THP_COLLAPSE_FULLY_SHARED);
}

static void test_anon_thp_collapse_lower_shared(char *mem, size_t size)
{
	do_test_anon_thp_collapse(mem, size, ANON_THP_COLLAPSE_LOWER_SHARED);
}

static void test_anon_thp_collapse_upper_shared(char *mem, size_t size)
{
	do_test_anon_thp_collapse(mem, size, ANON_THP_COLLAPSE_UPPER_SHARED);
}

/*
 * Test cases that are specific to anonymous THP: pages in private mappings
 * that may get shared via COW during fork().
 */
static const struct test_case anon_thp_test_cases[] = {
	/*
	 * Basic COW test for fork() without any GUP when collapsing a THP
	 * before fork().
	 *
	 * Re-mapping a PTE-mapped anon THP using a single PMD ("in-place
	 * collapse") might easily get COW handling wrong when not collapsing
	 * exclusivity information properly.
	 */
	{
		"Basic COW after fork() when collapsing before fork()",
		test_anon_thp_collapse_unshared,
	},
	/* Basic COW test, but collapse after COW-sharing a full THP. */
	{
		"Basic COW after fork() when collapsing after fork() (fully shared)",
		test_anon_thp_collapse_fully_shared,
	},
	/*
	 * Basic COW test, but collapse after COW-sharing the lower half of a
	 * THP.
	 */
	{
		"Basic COW after fork() when collapsing after fork() (lower shared)",
		test_anon_thp_collapse_lower_shared,
	},
	/*
	 * Basic COW test, but collapse after COW-sharing the upper half of a
	 * THP.
	 */
	{
		"Basic COW after fork() when collapsing after fork() (upper shared)",
		test_anon_thp_collapse_upper_shared,
	},
};

static void run_anon_thp_test_cases(void)
{
	int i;

	if (!thpsize)
		return;

	ksft_print_msg("[INFO] Anonymous THP tests\n");

	for (i = 0; i < ARRAY_SIZE(anon_thp_test_cases); i++) {
		struct test_case const *test_case = &anon_thp_test_cases[i];

		ksft_print_msg("[RUN] %s\n", test_case->desc);
		do_run_with_thp(test_case->fn, THP_RUN_PMD);
	}
}

static int tests_per_anon_thp_test_case(void)
{
	return thpsize ? 1 : 0;
}

typedef void (*non_anon_test_fn)(char *mem, const char *smem, size_t size);

static void test_cow(char *mem, const char *smem, size_t size)
{
	char *old = malloc(size);

	/* Backup the original content. */
	memcpy(old, smem, size);

	/* Modify the page. */
	memset(mem, 0xff, size);

	/* See if we still read the old values via the other mapping. */
	ksft_test_result(!memcmp(smem, old, size),
			 "Other mapping not modified\n");
	free(old);
}

static void test_ro_pin(char *mem, const char *smem, size_t size)
{
	do_test_ro_pin(mem, size, RO_PIN_TEST, false);
}

static void test_ro_fast_pin(char *mem, const char *smem, size_t size)
{
	do_test_ro_pin(mem, size, RO_PIN_TEST, true);
}

static void run_with_zeropage(non_anon_test_fn fn, const char *desc)
{
	char *mem, *smem, tmp;

	ksft_print_msg("[RUN] %s ... with shared zeropage\n", desc);

	mem = mmap(NULL, pagesize, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANON, -1, 0);
	if (mem == MAP_FAILED) {
		ksft_test_result_fail("mmap() failed\n");
		return;
	}

	smem = mmap(NULL, pagesize, PROT_READ, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (mem == MAP_FAILED) {
		ksft_test_result_fail("mmap() failed\n");
		goto munmap;
	}

	/* Read from the page to populate the shared zeropage. */
	tmp = *mem + *smem;
	asm volatile("" : "+r" (tmp));

	fn(mem, smem, pagesize);
munmap:
	munmap(mem, pagesize);
	if (smem != MAP_FAILED)
		munmap(smem, pagesize);
}

static void run_with_huge_zeropage(non_anon_test_fn fn, const char *desc)
{
	char *mem, *smem, *mmap_mem, *mmap_smem, tmp;
	size_t mmap_size;
	int ret;

	ksft_print_msg("[RUN] %s ... with huge zeropage\n", desc);

	if (!has_huge_zeropage) {
		ksft_test_result_skip("Huge zeropage not enabled\n");
		return;
	}

	/* For alignment purposes, we need twice the thp size. */
	mmap_size = 2 * thpsize;
	mmap_mem = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mmap_mem == MAP_FAILED) {
		ksft_test_result_fail("mmap() failed\n");
		return;
	}
	mmap_smem = mmap(NULL, mmap_size, PROT_READ,
			 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mmap_smem == MAP_FAILED) {
		ksft_test_result_fail("mmap() failed\n");
		goto munmap;
	}

	/* We need a THP-aligned memory area. */
	mem = (char *)(((uintptr_t)mmap_mem + thpsize) & ~(thpsize - 1));
	smem = (char *)(((uintptr_t)mmap_smem + thpsize) & ~(thpsize - 1));

	ret = madvise(mem, thpsize, MADV_HUGEPAGE);
	ret |= madvise(smem, thpsize, MADV_HUGEPAGE);
	if (ret) {
		ksft_test_result_fail("MADV_HUGEPAGE failed\n");
		goto munmap;
	}

	/*
	 * Read from the memory to populate the huge shared zeropage. Read from
	 * the first sub-page and test if we get another sub-page populated
	 * automatically.
	 */
	tmp = *mem + *smem;
	asm volatile("" : "+r" (tmp));
	if (!pagemap_is_populated(pagemap_fd, mem + pagesize) ||
	    !pagemap_is_populated(pagemap_fd, smem + pagesize)) {
		ksft_test_result_skip("Did not get THPs populated\n");
		goto munmap;
	}

	fn(mem, smem, thpsize);
munmap:
	munmap(mmap_mem, mmap_size);
	if (mmap_smem != MAP_FAILED)
		munmap(mmap_smem, mmap_size);
}

static void run_with_memfd(non_anon_test_fn fn, const char *desc)
{
	char *mem, *smem, tmp;
	int fd;

	ksft_print_msg("[RUN] %s ... with memfd\n", desc);

	fd = memfd_create("test", 0);
	if (fd < 0) {
		ksft_test_result_fail("memfd_create() failed\n");
		return;
	}

	/* File consists of a single page filled with zeroes. */
	if (fallocate(fd, 0, 0, pagesize)) {
		ksft_test_result_fail("fallocate() failed\n");
		goto close;
	}

	/* Create a private mapping of the memfd. */
	mem = mmap(NULL, pagesize, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (mem == MAP_FAILED) {
		ksft_test_result_fail("mmap() failed\n");
		goto close;
	}
	smem = mmap(NULL, pagesize, PROT_READ, MAP_SHARED, fd, 0);
	if (mem == MAP_FAILED) {
		ksft_test_result_fail("mmap() failed\n");
		goto munmap;
	}

	/* Fault the page in. */
	tmp = *mem + *smem;
	asm volatile("" : "+r" (tmp));

	fn(mem, smem, pagesize);
munmap:
	munmap(mem, pagesize);
	if (smem != MAP_FAILED)
		munmap(smem, pagesize);
close:
	close(fd);
}

static void run_with_tmpfile(non_anon_test_fn fn, const char *desc)
{
	char *mem, *smem, tmp;
	FILE *file;
	int fd;

	ksft_print_msg("[RUN] %s ... with tmpfile\n", desc);

	file = tmpfile();
	if (!file) {
		ksft_test_result_fail("tmpfile() failed\n");
		return;
	}

	fd = fileno(file);
	if (fd < 0) {
		ksft_test_result_skip("fileno() failed\n");
		return;
	}

	/* File consists of a single page filled with zeroes. */
	if (fallocate(fd, 0, 0, pagesize)) {
		ksft_test_result_fail("fallocate() failed\n");
		goto close;
	}

	/* Create a private mapping of the memfd. */
	mem = mmap(NULL, pagesize, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (mem == MAP_FAILED) {
		ksft_test_result_fail("mmap() failed\n");
		goto close;
	}
	smem = mmap(NULL, pagesize, PROT_READ, MAP_SHARED, fd, 0);
	if (mem == MAP_FAILED) {
		ksft_test_result_fail("mmap() failed\n");
		goto munmap;
	}

	/* Fault the page in. */
	tmp = *mem + *smem;
	asm volatile("" : "+r" (tmp));

	fn(mem, smem, pagesize);
munmap:
	munmap(mem, pagesize);
	if (smem != MAP_FAILED)
		munmap(smem, pagesize);
close:
	fclose(file);
}

static void run_with_memfd_hugetlb(non_anon_test_fn fn, const char *desc,
				   size_t hugetlbsize)
{
	int flags = MFD_HUGETLB;
	char *mem, *smem, tmp;
	int fd;

	ksft_print_msg("[RUN] %s ... with memfd hugetlb (%zu kB)\n", desc,
		       hugetlbsize / 1024);

	flags |= __builtin_ctzll(hugetlbsize) << MFD_HUGE_SHIFT;

	fd = memfd_create("test", flags);
	if (fd < 0) {
		ksft_test_result_skip("memfd_create() failed\n");
		return;
	}

	/* File consists of a single page filled with zeroes. */
	if (fallocate(fd, 0, 0, hugetlbsize)) {
		ksft_test_result_skip("need more free huge pages\n");
		goto close;
	}

	/* Create a private mapping of the memfd. */
	mem = mmap(NULL, hugetlbsize, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd,
		   0);
	if (mem == MAP_FAILED) {
		ksft_test_result_skip("need more free huge pages\n");
		goto close;
	}
	smem = mmap(NULL, hugetlbsize, PROT_READ, MAP_SHARED, fd, 0);
	if (mem == MAP_FAILED) {
		ksft_test_result_fail("mmap() failed\n");
		goto munmap;
	}

	/* Fault the page in. */
	tmp = *mem + *smem;
	asm volatile("" : "+r" (tmp));

	fn(mem, smem, hugetlbsize);
munmap:
	munmap(mem, hugetlbsize);
	if (mem != MAP_FAILED)
		munmap(smem, hugetlbsize);
close:
	close(fd);
}

struct non_anon_test_case {
	const char *desc;
	non_anon_test_fn fn;
};

/*
 * Test cases that target any pages in private mappings that are not anonymous:
 * pages that may get shared via COW ndependent of fork(). This includes
 * the shared zeropage(s), pagecache pages, ...
 */
static const struct non_anon_test_case non_anon_test_cases[] = {
	/*
	 * Basic COW test without any GUP. If we miss to break COW, changes are
	 * visible via other private/shared mappings.
	 */
	{
		"Basic COW",
		test_cow,
	},
	/*
	 * Take a R/O longterm pin. When modifying the page via the page table,
	 * the page content change must be visible via the pin.
	 */
	{
		"R/O longterm GUP pin",
		test_ro_pin,
	},
	/* Same as above, but using GUP-fast. */
	{
		"R/O longterm GUP-fast pin",
		test_ro_fast_pin,
	},
};

static void run_non_anon_test_case(struct non_anon_test_case const *test_case)
{
	int i;

	run_with_zeropage(test_case->fn, test_case->desc);
	run_with_memfd(test_case->fn, test_case->desc);
	run_with_tmpfile(test_case->fn, test_case->desc);
	if (thpsize)
		run_with_huge_zeropage(test_case->fn, test_case->desc);
	for (i = 0; i < nr_hugetlbsizes; i++)
		run_with_memfd_hugetlb(test_case->fn, test_case->desc,
				       hugetlbsizes[i]);
}

static void run_non_anon_test_cases(void)
{
	int i;

	ksft_print_msg("[RUN] Non-anonymous memory tests in private mappings\n");

	for (i = 0; i < ARRAY_SIZE(non_anon_test_cases); i++)
		run_non_anon_test_case(&non_anon_test_cases[i]);
}

static int tests_per_non_anon_test_case(void)
{
	int tests = 3 + nr_hugetlbsizes;

	if (thpsize)
		tests += 1;
	return tests;
}

int main(int argc, char **argv)
{
	int err;

	ksft_print_header();

	pagesize = getpagesize();
	thpsize = read_pmd_pagesize();
	if (thpsize)
		ksft_print_msg("[INFO] detected THP size: %zu KiB\n",
			       thpsize / 1024);
	nr_hugetlbsizes = detect_hugetlb_page_sizes(hugetlbsizes,
						    ARRAY_SIZE(hugetlbsizes));
	detect_huge_zeropage();

	ksft_set_plan(ARRAY_SIZE(anon_test_cases) * tests_per_anon_test_case() +
		      ARRAY_SIZE(anon_thp_test_cases) * tests_per_anon_thp_test_case() +
		      ARRAY_SIZE(non_anon_test_cases) * tests_per_non_anon_test_case());

	gup_fd = open("/sys/kernel/debug/gup_test", O_RDWR);
	pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
	if (pagemap_fd < 0)
		ksft_exit_fail_msg("opening pagemap failed\n");

	run_anon_test_cases();
	run_anon_thp_test_cases();
	run_non_anon_test_cases();

	err = ksft_get_fail_cnt();
	if (err)
		ksft_exit_fail_msg("%d out of %d tests failed\n",
				   err, ksft_test_num());
	return ksft_exit_pass();
}
