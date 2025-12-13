// SPDX-License-Identifier: GPL-2.0-only

#define _GNU_SOURCE
#include <stdbool.h>
#include <stdint.h>
#include <fcntl.h>
#include <assert.h>
#include <linux/mman.h>
#include <sys/mman.h>
#include "../kselftest.h"
#include "thp_settings.h"
#include "uffd-common.h"

static int pagemap_fd;
static size_t pagesize;
static int nr_pagesizes = 1;
static int nr_thpsizes;
static size_t thpsizes[20];
static int nr_hugetlbsizes;
static size_t hugetlbsizes[10];

static int detect_thp_sizes(size_t sizes[], int max)
{
	int count = 0;
	unsigned long orders;
	size_t kb;
	int i;

	/* thp not supported at all. */
	if (!read_pmd_pagesize())
		return 0;

	orders = thp_supported_orders();

	for (i = 0; orders && count < max; i++) {
		if (!(orders & (1UL << i)))
			continue;
		orders &= ~(1UL << i);
		kb = (pagesize >> 10) << i;
		sizes[count++] = kb * 1024;
		ksft_print_msg("[INFO] detected THP size: %zu KiB\n", kb);
	}

	return count;
}

static void *mmap_aligned(size_t size, int prot, int flags)
{
	size_t mmap_size = size * 2;
	char *mmap_mem, *mem;

	mmap_mem = mmap(NULL, mmap_size, prot, flags, -1, 0);
	if (mmap_mem == MAP_FAILED)
		return mmap_mem;

	mem = (char *)(((uintptr_t)mmap_mem + size - 1) & ~(size - 1));
	munmap(mmap_mem, mem - mmap_mem);
	munmap(mem + size, mmap_mem + mmap_size - mem - size);

	return mem;
}

static void *alloc_one_folio(size_t size, bool private, bool hugetlb)
{
	bool thp = !hugetlb && size > pagesize;
	int flags = MAP_ANONYMOUS;
	int prot = PROT_READ | PROT_WRITE;
	char *mem, *addr;

	assert((size & (size - 1)) == 0);

	if (private)
		flags |= MAP_PRIVATE;
	else
		flags |= MAP_SHARED;

	/*
	 * For THP, we must explicitly enable the THP size, allocate twice the
	 * required space then manually align.
	 */
	if (thp) {
		struct thp_settings settings = *thp_current_settings();

		if (private)
			settings.hugepages[sz2ord(size, pagesize)].enabled = THP_ALWAYS;
		else
			settings.shmem_hugepages[sz2ord(size, pagesize)].enabled = SHMEM_ALWAYS;

		thp_push_settings(&settings);

		mem = mmap_aligned(size, prot, flags);
	} else {
		if (hugetlb) {
			flags |= MAP_HUGETLB;
			flags |= __builtin_ctzll(size) << MAP_HUGE_SHIFT;
		}

		mem = mmap(NULL, size, prot, flags, -1, 0);
	}

	if (mem == MAP_FAILED) {
		mem = NULL;
		goto out;
	}

	assert(((uintptr_t)mem & (size - 1)) == 0);

	/*
	 * Populate the folio by writing the first byte and check that all pages
	 * are populated. Finally set the whole thing to non-zero data to avoid
	 * kernel from mapping it back to the zero page.
	 */
	mem[0] = 1;
	for (addr = mem; addr < mem + size; addr += pagesize) {
		if (!pagemap_is_populated(pagemap_fd, addr)) {
			munmap(mem, size);
			mem = NULL;
			goto out;
		}
	}
	memset(mem, 1, size);
out:
	if (thp)
		thp_pop_settings();

	return mem;
}

static bool check_uffd_wp_state(void *mem, size_t size, bool expect)
{
	uint64_t pte;
	void *addr;

	for (addr = mem; addr < mem + size; addr += pagesize) {
		pte = pagemap_get_entry(pagemap_fd, addr);
		if (!!(pte & PM_UFFD_WP) != expect) {
			ksft_test_result_fail("uffd-wp not %s for pte %lu!\n",
					      expect ? "set" : "clear",
					      (addr - mem) / pagesize);
			return false;
		}
	}

	return true;
}

static bool range_is_swapped(void *addr, size_t size)
{
	for (; size; addr += pagesize, size -= pagesize)
		if (!pagemap_is_swapped(pagemap_fd, addr))
			return false;
	return true;
}

static void test_one_folio(uffd_global_test_opts_t *gopts, size_t size, bool private,
			   bool swapout, bool hugetlb)
{
	struct uffdio_writeprotect wp_prms;
	uint64_t features = 0;
	void *addr = NULL;
	void *mem = NULL;

	assert(!(hugetlb && swapout));

	ksft_print_msg("[RUN] %s(size=%zu, private=%s, swapout=%s, hugetlb=%s)\n",
				__func__,
				size,
				private ? "true" : "false",
				swapout ? "true" : "false",
				hugetlb ? "true" : "false");

	/* Allocate a folio of required size and type. */
	mem = alloc_one_folio(size, private, hugetlb);
	if (!mem) {
		ksft_test_result_fail("alloc_one_folio() failed\n");
		goto out;
	}

	/* Register range for uffd-wp. */
	if (userfaultfd_open(gopts, &features)) {
		if (errno == ENOENT)
			ksft_test_result_skip("userfaultfd not available\n");
		else
			ksft_test_result_fail("userfaultfd_open() failed\n");
		goto out;
	}
	if (uffd_register(gopts->uffd, mem, size, false, true, false)) {
		ksft_test_result_fail("uffd_register() failed\n");
		goto out;
	}
	wp_prms.mode = UFFDIO_WRITEPROTECT_MODE_WP;
	wp_prms.range.start = (uintptr_t)mem;
	wp_prms.range.len = size;
	if (ioctl(gopts->uffd, UFFDIO_WRITEPROTECT, &wp_prms)) {
		ksft_test_result_fail("ioctl(UFFDIO_WRITEPROTECT) failed\n");
		goto out;
	}

	if (swapout) {
		madvise(mem, size, MADV_PAGEOUT);
		if (!range_is_swapped(mem, size)) {
			ksft_test_result_skip("MADV_PAGEOUT did not work, is swap enabled?\n");
			goto out;
		}
	}

	/* Check that uffd-wp is set for all PTEs in range. */
	if (!check_uffd_wp_state(mem, size, true))
		goto out;

	/*
	 * Move the mapping to a new, aligned location. Since
	 * UFFD_FEATURE_EVENT_REMAP is not set, we expect the uffd-wp bit for
	 * each PTE to be cleared in the new mapping.
	 */
	addr = mmap_aligned(size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS);
	if (addr == MAP_FAILED) {
		ksft_test_result_fail("mmap_aligned() failed\n");
		goto out;
	}
	if (mremap(mem, size, size, MREMAP_FIXED | MREMAP_MAYMOVE, addr) == MAP_FAILED) {
		ksft_test_result_fail("mremap() failed\n");
		munmap(addr, size);
		goto out;
	}
	mem = addr;

	/* Check that uffd-wp is cleared for all PTEs in range. */
	if (!check_uffd_wp_state(mem, size, false))
		goto out;

	ksft_test_result_pass("%s(size=%zu, private=%s, swapout=%s, hugetlb=%s)\n",
				__func__,
				size,
				private ? "true" : "false",
				swapout ? "true" : "false",
				hugetlb ? "true" : "false");
out:
	if (mem)
		munmap(mem, size);
	if (gopts->uffd >= 0) {
		close(gopts->uffd);
		gopts->uffd = -1;
	}
}

struct testcase {
	size_t *sizes;
	int *nr_sizes;
	bool private;
	bool swapout;
	bool hugetlb;
};

static const struct testcase testcases[] = {
	/* base pages. */
	{
		.sizes = &pagesize,
		.nr_sizes = &nr_pagesizes,
		.private = false,
		.swapout = false,
		.hugetlb = false,
	},
	{
		.sizes = &pagesize,
		.nr_sizes = &nr_pagesizes,
		.private = true,
		.swapout = false,
		.hugetlb = false,
	},
	{
		.sizes = &pagesize,
		.nr_sizes = &nr_pagesizes,
		.private = false,
		.swapout = true,
		.hugetlb = false,
	},
	{
		.sizes = &pagesize,
		.nr_sizes = &nr_pagesizes,
		.private = true,
		.swapout = true,
		.hugetlb = false,
	},

	/* thp. */
	{
		.sizes = thpsizes,
		.nr_sizes = &nr_thpsizes,
		.private = false,
		.swapout = false,
		.hugetlb = false,
	},
	{
		.sizes = thpsizes,
		.nr_sizes = &nr_thpsizes,
		.private = true,
		.swapout = false,
		.hugetlb = false,
	},
	{
		.sizes = thpsizes,
		.nr_sizes = &nr_thpsizes,
		.private = false,
		.swapout = true,
		.hugetlb = false,
	},
	{
		.sizes = thpsizes,
		.nr_sizes = &nr_thpsizes,
		.private = true,
		.swapout = true,
		.hugetlb = false,
	},

	/* hugetlb. */
	{
		.sizes = hugetlbsizes,
		.nr_sizes = &nr_hugetlbsizes,
		.private = false,
		.swapout = false,
		.hugetlb = true,
	},
	{
		.sizes = hugetlbsizes,
		.nr_sizes = &nr_hugetlbsizes,
		.private = true,
		.swapout = false,
		.hugetlb = true,
	},
};

int main(int argc, char **argv)
{
	uffd_global_test_opts_t gopts = { 0 };
	struct thp_settings settings;
	int i, j, plan = 0;

	pagesize = getpagesize();
	nr_thpsizes = detect_thp_sizes(thpsizes, ARRAY_SIZE(thpsizes));
	nr_hugetlbsizes = detect_hugetlb_page_sizes(hugetlbsizes,
						    ARRAY_SIZE(hugetlbsizes));

	/* If THP is supported, save THP settings and initially disable THP. */
	if (nr_thpsizes) {
		thp_save_settings();
		thp_read_settings(&settings);
		for (i = 0; i < NR_ORDERS; i++) {
			settings.hugepages[i].enabled = THP_NEVER;
			settings.shmem_hugepages[i].enabled = SHMEM_NEVER;
		}
		thp_push_settings(&settings);
	}

	for (i = 0; i < ARRAY_SIZE(testcases); i++)
		plan += *testcases[i].nr_sizes;
	ksft_set_plan(plan);

	pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
	if (pagemap_fd < 0)
		ksft_exit_fail_msg("opening pagemap failed\n");

	for (i = 0; i < ARRAY_SIZE(testcases); i++) {
		const struct testcase *tc = &testcases[i];

		for (j = 0; j < *tc->nr_sizes; j++)
			test_one_folio(&gopts, tc->sizes[j], tc->private,
				       tc->swapout, tc->hugetlb);
	}

	/* If THP is supported, restore original THP settings. */
	if (nr_thpsizes)
		thp_restore_settings();

	i = ksft_get_fail_cnt();
	if (i)
		ksft_exit_fail_msg("%d out of %d tests failed\n",
				   i, ksft_test_num());
	ksft_exit_pass();
}
