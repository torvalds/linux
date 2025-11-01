// SPDX-License-Identifier: GPL-2.0
/*
 * A test of splitting PMD THPs and PTE-mapped THPs from a specified virtual
 * address range in a process via <debugfs>/split_huge_pages interface.
 */

#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <inttypes.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <malloc.h>
#include <stdbool.h>
#include <time.h>
#include "vm_util.h"
#include "../kselftest.h"

uint64_t pagesize;
unsigned int pageshift;
uint64_t pmd_pagesize;
unsigned int pmd_order;
int *expected_orders;

#define SPLIT_DEBUGFS "/sys/kernel/debug/split_huge_pages"
#define SMAP_PATH "/proc/self/smaps"
#define INPUT_MAX 80

#define PID_FMT "%d,0x%lx,0x%lx,%d"
#define PID_FMT_OFFSET "%d,0x%lx,0x%lx,%d,%d"
#define PATH_FMT "%s,0x%lx,0x%lx,%d"

const char *pagemap_proc = "/proc/self/pagemap";
const char *kpageflags_proc = "/proc/kpageflags";
int pagemap_fd;
int kpageflags_fd;

static bool is_backed_by_folio(char *vaddr, int order, int pagemap_fd,
		int kpageflags_fd)
{
	const uint64_t folio_head_flags = KPF_THP | KPF_COMPOUND_HEAD;
	const uint64_t folio_tail_flags = KPF_THP | KPF_COMPOUND_TAIL;
	const unsigned long nr_pages = 1UL << order;
	unsigned long pfn_head;
	uint64_t pfn_flags;
	unsigned long pfn;
	unsigned long i;

	pfn = pagemap_get_pfn(pagemap_fd, vaddr);

	/* non present page */
	if (pfn == -1UL)
		return false;

	if (pageflags_get(pfn, kpageflags_fd, &pfn_flags))
		goto fail;

	/* check for order-0 pages */
	if (!order) {
		if (pfn_flags & (folio_head_flags | folio_tail_flags))
			return false;
		return true;
	}

	/* non THP folio */
	if (!(pfn_flags & KPF_THP))
		return false;

	pfn_head = pfn & ~(nr_pages - 1);

	if (pageflags_get(pfn_head, kpageflags_fd, &pfn_flags))
		goto fail;

	/* head PFN has no compound_head flag set */
	if ((pfn_flags & folio_head_flags) != folio_head_flags)
		return false;

	/* check all tail PFN flags */
	for (i = 1; i < nr_pages; i++) {
		if (pageflags_get(pfn_head + i, kpageflags_fd, &pfn_flags))
			goto fail;
		if ((pfn_flags & folio_tail_flags) != folio_tail_flags)
			return false;
	}

	/*
	 * check the PFN after this folio, but if its flags cannot be obtained,
	 * assume this folio has the expected order
	 */
	if (pageflags_get(pfn_head + nr_pages, kpageflags_fd, &pfn_flags))
		return true;

	/* If we find another tail page, then the folio is larger. */
	return (pfn_flags & folio_tail_flags) != folio_tail_flags;
fail:
	ksft_exit_fail_msg("Failed to get folio info\n");
	return false;
}

static int vaddr_pageflags_get(char *vaddr, int pagemap_fd, int kpageflags_fd,
		uint64_t *flags)
{
	unsigned long pfn;

	pfn = pagemap_get_pfn(pagemap_fd, vaddr);

	/* non-present PFN */
	if (pfn == -1UL)
		return 1;

	if (pageflags_get(pfn, kpageflags_fd, flags))
		return -1;

	return 0;
}

/*
 * gather_after_split_folio_orders - scan through [vaddr_start, len) and record
 * folio orders
 *
 * @vaddr_start: start vaddr
 * @len: range length
 * @pagemap_fd: file descriptor to /proc/<pid>/pagemap
 * @kpageflags_fd: file descriptor to /proc/kpageflags
 * @orders: output folio order array
 * @nr_orders: folio order array size
 *
 * gather_after_split_folio_orders() scan through [vaddr_start, len) and check
 * all folios within the range and record their orders. All order-0 pages will
 * be recorded. Non-present vaddr is skipped.
 *
 * NOTE: the function is used to check folio orders after a split is performed,
 * so it assumes [vaddr_start, len) fully maps to after-split folios within that
 * range.
 *
 * Return: 0 - no error, -1 - unhandled cases
 */
static int gather_after_split_folio_orders(char *vaddr_start, size_t len,
		int pagemap_fd, int kpageflags_fd, int orders[], int nr_orders)
{
	uint64_t page_flags = 0;
	int cur_order = -1;
	char *vaddr;

	if (pagemap_fd == -1 || kpageflags_fd == -1)
		return -1;
	if (!orders)
		return -1;
	if (nr_orders <= 0)
		return -1;

	for (vaddr = vaddr_start; vaddr < vaddr_start + len;) {
		char *next_folio_vaddr;
		int status;

		status = vaddr_pageflags_get(vaddr, pagemap_fd, kpageflags_fd,
					&page_flags);
		if (status < 0)
			return -1;

		/* skip non present vaddr */
		if (status == 1) {
			vaddr += psize();
			continue;
		}

		/* all order-0 pages with possible false postive (non folio) */
		if (!(page_flags & (KPF_COMPOUND_HEAD | KPF_COMPOUND_TAIL))) {
			orders[0]++;
			vaddr += psize();
			continue;
		}

		/* skip non thp compound pages */
		if (!(page_flags & KPF_THP)) {
			vaddr += psize();
			continue;
		}

		/* vpn points to part of a THP at this point */
		if (page_flags & KPF_COMPOUND_HEAD)
			cur_order = 1;
		else {
			vaddr += psize();
			continue;
		}

		next_folio_vaddr = vaddr + (1UL << (cur_order + pshift()));

		if (next_folio_vaddr >= vaddr_start + len)
			break;

		while ((status = vaddr_pageflags_get(next_folio_vaddr,
						     pagemap_fd, kpageflags_fd,
						     &page_flags)) >= 0) {
			/*
			 * non present vaddr, next compound head page, or
			 * order-0 page
			 */
			if (status == 1 ||
			    (page_flags & KPF_COMPOUND_HEAD) ||
			    !(page_flags & (KPF_COMPOUND_HEAD | KPF_COMPOUND_TAIL))) {
				if (cur_order < nr_orders) {
					orders[cur_order]++;
					cur_order = -1;
					vaddr = next_folio_vaddr;
				}
				break;
			}

			cur_order++;
			next_folio_vaddr = vaddr + (1UL << (cur_order + pshift()));
		}

		if (status < 0)
			return status;
	}
	if (cur_order > 0 && cur_order < nr_orders)
		orders[cur_order]++;
	return 0;
}

static int check_after_split_folio_orders(char *vaddr_start, size_t len,
		int pagemap_fd, int kpageflags_fd, int orders[], int nr_orders)
{
	int *vaddr_orders;
	int status;
	int i;

	vaddr_orders = (int *)malloc(sizeof(int) * nr_orders);

	if (!vaddr_orders)
		ksft_exit_fail_msg("Cannot allocate memory for vaddr_orders");

	memset(vaddr_orders, 0, sizeof(int) * nr_orders);
	status = gather_after_split_folio_orders(vaddr_start, len, pagemap_fd,
				     kpageflags_fd, vaddr_orders, nr_orders);
	if (status)
		ksft_exit_fail_msg("gather folio info failed\n");

	for (i = 0; i < nr_orders; i++)
		if (vaddr_orders[i] != orders[i]) {
			ksft_print_msg("order %d: expected: %d got %d\n", i,
				       orders[i], vaddr_orders[i]);
			status = -1;
		}

	free(vaddr_orders);
	return status;
}

static void write_file(const char *path, const char *buf, size_t buflen)
{
	int fd;
	ssize_t numwritten;

	fd = open(path, O_WRONLY);
	if (fd == -1)
		ksft_exit_fail_msg("%s open failed: %s\n", path, strerror(errno));

	numwritten = write(fd, buf, buflen - 1);
	close(fd);
	if (numwritten < 1)
		ksft_exit_fail_msg("Write failed\n");
}

static void write_debugfs(const char *fmt, ...)
{
	char input[INPUT_MAX];
	int ret;
	va_list argp;

	va_start(argp, fmt);
	ret = vsnprintf(input, INPUT_MAX, fmt, argp);
	va_end(argp);

	if (ret >= INPUT_MAX)
		ksft_exit_fail_msg("%s: Debugfs input is too long\n", __func__);

	write_file(SPLIT_DEBUGFS, input, ret + 1);
}

static char *allocate_zero_filled_hugepage(size_t len)
{
	char *result;
	size_t i;

	result = memalign(pmd_pagesize, len);
	if (!result) {
		printf("Fail to allocate memory\n");
		exit(EXIT_FAILURE);
	}

	madvise(result, len, MADV_HUGEPAGE);

	for (i = 0; i < len; i++)
		result[i] = (char)0;

	return result;
}

static void verify_rss_anon_split_huge_page_all_zeroes(char *one_page, int nr_hpages, size_t len)
{
	unsigned long rss_anon_before, rss_anon_after;
	size_t i;

	if (!check_huge_anon(one_page, nr_hpages, pmd_pagesize))
		ksft_exit_fail_msg("No THP is allocated\n");

	rss_anon_before = rss_anon();
	if (!rss_anon_before)
		ksft_exit_fail_msg("No RssAnon is allocated before split\n");

	/* split all THPs */
	write_debugfs(PID_FMT, getpid(), (uint64_t)one_page,
		      (uint64_t)one_page + len, 0);

	for (i = 0; i < len; i++)
		if (one_page[i] != (char)0)
			ksft_exit_fail_msg("%ld byte corrupted\n", i);

	if (!check_huge_anon(one_page, 0, pmd_pagesize))
		ksft_exit_fail_msg("Still AnonHugePages not split\n");

	rss_anon_after = rss_anon();
	if (rss_anon_after >= rss_anon_before)
		ksft_exit_fail_msg("Incorrect RssAnon value. Before: %ld After: %ld\n",
		       rss_anon_before, rss_anon_after);
}

static void split_pmd_zero_pages(void)
{
	char *one_page;
	int nr_hpages = 4;
	size_t len = nr_hpages * pmd_pagesize;

	one_page = allocate_zero_filled_hugepage(len);
	verify_rss_anon_split_huge_page_all_zeroes(one_page, nr_hpages, len);
	ksft_test_result_pass("Split zero filled huge pages successful\n");
	free(one_page);
}

static void split_pmd_thp_to_order(int order)
{
	char *one_page;
	size_t len = 4 * pmd_pagesize;
	size_t i;

	one_page = memalign(pmd_pagesize, len);
	if (!one_page)
		ksft_exit_fail_msg("Fail to allocate memory: %s\n", strerror(errno));

	madvise(one_page, len, MADV_HUGEPAGE);

	for (i = 0; i < len; i++)
		one_page[i] = (char)i;

	if (!check_huge_anon(one_page, 4, pmd_pagesize))
		ksft_exit_fail_msg("No THP is allocated\n");

	/* split all THPs */
	write_debugfs(PID_FMT, getpid(), (uint64_t)one_page,
		(uint64_t)one_page + len, order);

	for (i = 0; i < len; i++)
		if (one_page[i] != (char)i)
			ksft_exit_fail_msg("%ld byte corrupted\n", i);

	memset(expected_orders, 0, sizeof(int) * (pmd_order + 1));
	expected_orders[order] = 4 << (pmd_order - order);

	if (check_after_split_folio_orders(one_page, len, pagemap_fd,
					   kpageflags_fd, expected_orders,
					   (pmd_order + 1)))
		ksft_exit_fail_msg("Unexpected THP split\n");

	if (!check_huge_anon(one_page, 0, pmd_pagesize))
		ksft_exit_fail_msg("Still AnonHugePages not split\n");

	ksft_test_result_pass("Split huge pages to order %d successful\n", order);
	free(one_page);
}

static void split_pte_mapped_thp(void)
{
	const size_t nr_thps = 4;
	const size_t thp_area_size = nr_thps * pmd_pagesize;
	const size_t page_area_size = nr_thps * pagesize;
	char *thp_area, *tmp, *page_area = MAP_FAILED;
	size_t i;

	thp_area = mmap((void *)(1UL << 30), thp_area_size, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (thp_area == MAP_FAILED) {
		ksft_test_result_fail("Fail to allocate memory: %s\n", strerror(errno));
		return;
	}

	madvise(thp_area, thp_area_size, MADV_HUGEPAGE);

	for (i = 0; i < thp_area_size; i++)
		thp_area[i] = (char)i;

	if (!check_huge_anon(thp_area, nr_thps, pmd_pagesize)) {
		ksft_test_result_skip("Not all THPs allocated\n");
		goto out;
	}

	/*
	 * To challenge spitting code, we will mremap a single page of each
	 * THP (page[i] of thp[i]) in the thp_area into page_area. This will
	 * replace the PMD mappings in the thp_area by PTE mappings first,
	 * but leaving the THP unsplit, to then create a page-sized hole in
	 * the thp_area.
	 * We will then manually trigger splitting of all THPs through the
	 * single mremap'ed pages of each THP in the page_area.
	 */
	page_area = mmap(NULL, page_area_size, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (page_area == MAP_FAILED) {
		ksft_test_result_fail("Fail to allocate memory: %s\n", strerror(errno));
		goto out;
	}

	for (i = 0; i < nr_thps; i++) {
		tmp = mremap(thp_area + pmd_pagesize * i + pagesize * i,
			     pagesize, pagesize, MREMAP_MAYMOVE|MREMAP_FIXED,
			     page_area + pagesize * i);
		if (tmp != MAP_FAILED)
			continue;
		ksft_test_result_fail("mremap failed: %s\n", strerror(errno));
		goto out;
	}

	/*
	 * Verify that our THPs were not split yet. Note that
	 * check_huge_anon() cannot be used as it checks for PMD mappings.
	 */
	for (i = 0; i < nr_thps; i++) {
		if (is_backed_by_folio(page_area + i * pagesize, pmd_order,
				       pagemap_fd, kpageflags_fd))
			continue;
		ksft_test_result_fail("THP %zu missing after mremap\n", i);
		goto out;
	}

	/* Split all THPs through the remapped pages. */
	write_debugfs(PID_FMT, getpid(), (uint64_t)page_area,
		      (uint64_t)page_area + page_area_size, 0);

	/* Corruption during mremap or split? */
	for (i = 0; i < page_area_size; i++) {
		if (page_area[i] == (char)i)
			continue;
		ksft_test_result_fail("%zu byte corrupted\n", i);
		goto out;
	}

	/* Split failed? */
	for (i = 0; i < nr_thps; i++) {
		if (is_backed_by_folio(page_area + i * pagesize, 0,
				       pagemap_fd, kpageflags_fd))
			continue;
		ksft_test_result_fail("THP %zu not split\n", i);
	}

	ksft_test_result_pass("Split PTE-mapped huge pages successful\n");
out:
	munmap(thp_area, thp_area_size);
	if (page_area != MAP_FAILED)
		munmap(page_area, page_area_size);
}

static void split_file_backed_thp(int order)
{
	int status;
	int fd;
	char tmpfs_template[] = "/tmp/thp_split_XXXXXX";
	const char *tmpfs_loc = mkdtemp(tmpfs_template);
	char testfile[INPUT_MAX];
	ssize_t num_written, num_read;
	char *file_buf1, *file_buf2;
	uint64_t pgoff_start = 0, pgoff_end = 1024;
	int i;

	ksft_print_msg("Please enable pr_debug in split_huge_pages_in_file() for more info.\n");

	file_buf1 = (char *)malloc(pmd_pagesize);
	file_buf2 = (char *)malloc(pmd_pagesize);

	if (!file_buf1 || !file_buf2) {
		ksft_print_msg("cannot allocate file buffers\n");
		goto out;
	}

	for (i = 0; i < pmd_pagesize; i++)
		file_buf1[i] = (char)i;
	memset(file_buf2, 0, pmd_pagesize);

	status = mount("tmpfs", tmpfs_loc, "tmpfs", 0, "huge=always,size=4m");

	if (status)
		ksft_exit_fail_msg("Unable to create a tmpfs for testing\n");

	status = snprintf(testfile, INPUT_MAX, "%s/thp_file", tmpfs_loc);
	if (status >= INPUT_MAX) {
		ksft_print_msg("Fail to create file-backed THP split testing file\n");
		goto cleanup;
	}

	fd = open(testfile, O_CREAT|O_RDWR, 0664);
	if (fd == -1) {
		ksft_perror("Cannot open testing file");
		goto cleanup;
	}

	/* write pmd size data to the file, so a file-backed THP can be allocated */
	num_written = write(fd, file_buf1, pmd_pagesize);

	if (num_written == -1 || num_written != pmd_pagesize) {
		ksft_perror("Failed to write data to testing file");
		goto close_file;
	}

	/* split the file-backed THP */
	write_debugfs(PATH_FMT, testfile, pgoff_start, pgoff_end, order);

	/* check file content after split */
	status = lseek(fd, 0, SEEK_SET);
	if (status == -1) {
		ksft_perror("Cannot lseek file");
		goto close_file;
	}

	num_read = read(fd, file_buf2, num_written);
	if (num_read == -1 || num_read != num_written) {
		ksft_perror("Cannot read file content back");
		goto close_file;
	}

	if (strncmp(file_buf1, file_buf2, pmd_pagesize) != 0) {
		ksft_print_msg("File content changed\n");
		goto close_file;
	}

	close(fd);
	status = unlink(testfile);
	if (status) {
		ksft_perror("Cannot remove testing file");
		goto cleanup;
	}

	status = umount(tmpfs_loc);
	if (status) {
		rmdir(tmpfs_loc);
		ksft_exit_fail_msg("Unable to umount %s\n", tmpfs_loc);
	}

	status = rmdir(tmpfs_loc);
	if (status)
		ksft_exit_fail_msg("cannot remove tmp dir: %s\n", strerror(errno));

	ksft_print_msg("Please check dmesg for more information\n");
	ksft_test_result_pass("File-backed THP split to order %d test done\n", order);
	return;

close_file:
	close(fd);
cleanup:
	umount(tmpfs_loc);
	rmdir(tmpfs_loc);
out:
	ksft_exit_fail_msg("Error occurred\n");
}

static bool prepare_thp_fs(const char *xfs_path, char *thp_fs_template,
		const char **thp_fs_loc)
{
	if (xfs_path) {
		*thp_fs_loc = xfs_path;
		return false;
	}

	*thp_fs_loc = mkdtemp(thp_fs_template);

	if (!*thp_fs_loc)
		ksft_exit_fail_msg("cannot create temp folder\n");

	return true;
}

static void cleanup_thp_fs(const char *thp_fs_loc, bool created_tmp)
{
	int status;

	if (!created_tmp)
		return;

	status = rmdir(thp_fs_loc);
	if (status)
		ksft_exit_fail_msg("cannot remove tmp dir: %s\n",
				   strerror(errno));
}

static int create_pagecache_thp_and_fd(const char *testfile, size_t fd_size,
		int *fd, char **addr)
{
	size_t i;
	unsigned char buf[1024];

	srand(time(NULL));

	*fd = open(testfile, O_CREAT | O_RDWR, 0664);
	if (*fd == -1)
		ksft_exit_fail_msg("Failed to create a file at %s\n", testfile);

	assert(fd_size % sizeof(buf) == 0);
	for (i = 0; i < sizeof(buf); i++)
		buf[i] = (unsigned char)i;
	for (i = 0; i < fd_size; i += sizeof(buf))
		write(*fd, buf, sizeof(buf));

	close(*fd);
	sync();
	*fd = open("/proc/sys/vm/drop_caches", O_WRONLY);
	if (*fd == -1) {
		ksft_perror("open drop_caches");
		goto err_out_unlink;
	}
	if (write(*fd, "3", 1) != 1) {
		ksft_perror("write to drop_caches");
		goto err_out_unlink;
	}
	close(*fd);

	*fd = open(testfile, O_RDWR);
	if (*fd == -1) {
		ksft_perror("Failed to open testfile\n");
		goto err_out_unlink;
	}

	*addr = mmap(NULL, fd_size, PROT_READ|PROT_WRITE, MAP_SHARED, *fd, 0);
	if (*addr == (char *)-1) {
		ksft_perror("cannot mmap");
		goto err_out_close;
	}
	madvise(*addr, fd_size, MADV_HUGEPAGE);

	for (size_t i = 0; i < fd_size; i++) {
		char *addr2 = *addr + i;

		FORCE_READ(*addr2);
	}

	if (!check_huge_file(*addr, fd_size / pmd_pagesize, pmd_pagesize)) {
		ksft_print_msg("No large pagecache folio generated, please provide a filesystem supporting large folio\n");
		munmap(*addr, fd_size);
		close(*fd);
		unlink(testfile);
		ksft_test_result_skip("Pagecache folio split skipped\n");
		return -2;
	}
	return 0;
err_out_close:
	close(*fd);
err_out_unlink:
	unlink(testfile);
	ksft_exit_fail_msg("Failed to create large pagecache folios\n");
	return -1;
}

static void split_thp_in_pagecache_to_order_at(size_t fd_size,
		const char *fs_loc, int order, int offset)
{
	int fd;
	char *split_addr;
	char *addr;
	size_t i;
	char testfile[INPUT_MAX];
	int err = 0;

	err = snprintf(testfile, INPUT_MAX, "%s/test", fs_loc);

	if (err < 0)
		ksft_exit_fail_msg("cannot generate right test file name\n");

	err = create_pagecache_thp_and_fd(testfile, fd_size, &fd, &addr);
	if (err)
		return;

	err = 0;

	memset(expected_orders, 0, sizeof(int) * (pmd_order + 1));
	/*
	 * use [split_addr, split_addr + pagesize) range to split THPs, since
	 * the debugfs function always split a range with pagesize step and
	 * providing a full [addr, addr + fd_size) range can trigger multiple
	 * splits, complicating after-split result checking.
	 */
	if (offset == -1) {
		for (split_addr = addr; split_addr < addr + fd_size; split_addr += pmd_pagesize)
			write_debugfs(PID_FMT, getpid(), (uint64_t)split_addr,
				      (uint64_t)split_addr + pagesize, order);

		expected_orders[order] = fd_size / (pagesize << order);
	} else {
		int times = fd_size / pmd_pagesize;

		for (split_addr = addr; split_addr < addr + fd_size; split_addr += pmd_pagesize)
			write_debugfs(PID_FMT_OFFSET, getpid(), (uint64_t)split_addr,
				      (uint64_t)split_addr + pagesize, order, offset);

		for (i = order + 1; i < pmd_order; i++)
			expected_orders[i] = times;
		expected_orders[order] = 2 * times;
	}

	for (i = 0; i < fd_size; i++)
		if (*(addr + i) != (char)i) {
			ksft_print_msg("%lu byte corrupted in the file\n", i);
			err = EXIT_FAILURE;
			goto out;
		}

	if (check_after_split_folio_orders(addr, fd_size, pagemap_fd,
					   kpageflags_fd, expected_orders,
					   (pmd_order + 1))) {
		ksft_print_msg("Unexpected THP split\n");
		err = 1;
		goto out;
	}

	if (!check_huge_file(addr, 0, pmd_pagesize)) {
		ksft_print_msg("Still FilePmdMapped not split\n");
		err = EXIT_FAILURE;
		goto out;
	}

out:
	munmap(addr, fd_size);
	close(fd);
	unlink(testfile);
	if (offset == -1) {
		if (err)
			ksft_exit_fail_msg("Split PMD-mapped pagecache folio to order %d failed\n", order);
		ksft_test_result_pass("Split PMD-mapped pagecache folio to order %d passed\n", order);
	} else {
		if (err)
			ksft_exit_fail_msg("Split PMD-mapped pagecache folio to order %d at in-folio offset %d failed\n", order, offset);
		ksft_test_result_pass("Split PMD-mapped pagecache folio to order %d at in-folio offset %d passed\n", order, offset);
	}
}

int main(int argc, char **argv)
{
	int i;
	size_t fd_size;
	char *optional_xfs_path = NULL;
	char fs_loc_template[] = "/tmp/thp_fs_XXXXXX";
	const char *fs_loc;
	bool created_tmp;
	int offset;
	unsigned int nr_pages;
	unsigned int tests;

	ksft_print_header();

	if (geteuid() != 0) {
		ksft_print_msg("Please run the benchmark as root\n");
		ksft_finished();
	}

	if (argc > 1)
		optional_xfs_path = argv[1];

	pagesize = getpagesize();
	pageshift = ffs(pagesize) - 1;
	pmd_pagesize = read_pmd_pagesize();
	if (!pmd_pagesize)
		ksft_exit_fail_msg("Reading PMD pagesize failed\n");

	nr_pages = pmd_pagesize / pagesize;
	pmd_order = sz2ord(pmd_pagesize, pagesize);

	expected_orders = (int *)malloc(sizeof(int) * (pmd_order + 1));
	if (!expected_orders)
		ksft_exit_fail_msg("Fail to allocate memory: %s\n", strerror(errno));

	tests = 2 + (pmd_order - 1) + (2 * pmd_order) + (pmd_order - 1) * 4 + 2;
	ksft_set_plan(tests);

	pagemap_fd = open(pagemap_proc, O_RDONLY);
	if (pagemap_fd == -1)
		ksft_exit_fail_msg("read pagemap: %s\n", strerror(errno));

	kpageflags_fd = open(kpageflags_proc, O_RDONLY);
	if (kpageflags_fd == -1)
		ksft_exit_fail_msg("read kpageflags: %s\n", strerror(errno));

	fd_size = 2 * pmd_pagesize;

	split_pmd_zero_pages();

	for (i = 0; i < pmd_order; i++)
		if (i != 1)
			split_pmd_thp_to_order(i);

	split_pte_mapped_thp();
	for (i = 0; i < pmd_order; i++)
		split_file_backed_thp(i);

	created_tmp = prepare_thp_fs(optional_xfs_path, fs_loc_template,
			&fs_loc);
	for (i = pmd_order - 1; i >= 0; i--)
		split_thp_in_pagecache_to_order_at(fd_size, fs_loc, i, -1);

	for (i = 0; i < pmd_order; i++)
		for (offset = 0;
		     offset < nr_pages;
		     offset += MAX(nr_pages / 4, 1 << i))
			split_thp_in_pagecache_to_order_at(fd_size, fs_loc, i, offset);
	cleanup_thp_fs(fs_loc, created_tmp);

	close(pagemap_fd);
	close(kpageflags_fd);
	free(expected_orders);

	ksft_finished();

	return 0;
}
