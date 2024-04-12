// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <stdint.h>
#include <malloc.h>
#include <sys/mman.h>
#include "../kselftest.h"
#include "vm_util.h"

#define PAGEMAP_FILE_PATH "/proc/self/pagemap"
#define TEST_ITERATIONS 10000

static void test_simple(int pagemap_fd, int pagesize)
{
	int i;
	char *map;

	map = aligned_alloc(pagesize, pagesize);
	if (!map)
		ksft_exit_fail_msg("mmap failed\n");

	clear_softdirty();

	for (i = 0 ; i < TEST_ITERATIONS; i++) {
		if (pagemap_is_softdirty(pagemap_fd, map) == 1) {
			ksft_print_msg("dirty bit was 1, but should be 0 (i=%d)\n", i);
			break;
		}

		clear_softdirty();
		// Write something to the page to get the dirty bit enabled on the page
		map[0]++;

		if (pagemap_is_softdirty(pagemap_fd, map) == 0) {
			ksft_print_msg("dirty bit was 0, but should be 1 (i=%d)\n", i);
			break;
		}

		clear_softdirty();
	}
	free(map);

	ksft_test_result(i == TEST_ITERATIONS, "Test %s\n", __func__);
}

static void test_vma_reuse(int pagemap_fd, int pagesize)
{
	char *map, *map2;

	map = mmap(NULL, pagesize, (PROT_READ | PROT_WRITE), (MAP_PRIVATE | MAP_ANON), -1, 0);
	if (map == MAP_FAILED)
		ksft_exit_fail_msg("mmap failed");

	// The kernel always marks new regions as soft dirty
	ksft_test_result(pagemap_is_softdirty(pagemap_fd, map) == 1,
			 "Test %s dirty bit of allocated page\n", __func__);

	clear_softdirty();
	munmap(map, pagesize);

	map2 = mmap(NULL, pagesize, (PROT_READ | PROT_WRITE), (MAP_PRIVATE | MAP_ANON), -1, 0);
	if (map2 == MAP_FAILED)
		ksft_exit_fail_msg("mmap failed");

	// Dirty bit is set for new regions even if they are reused
	if (map == map2)
		ksft_test_result(pagemap_is_softdirty(pagemap_fd, map2) == 1,
				 "Test %s dirty bit of reused address page\n", __func__);
	else
		ksft_test_result_skip("Test %s dirty bit of reused address page\n", __func__);

	munmap(map2, pagesize);
}

static void test_hugepage(int pagemap_fd, int pagesize)
{
	char *map;
	int i, ret;
	size_t hpage_len = read_pmd_pagesize();

	if (!hpage_len)
		ksft_exit_fail_msg("Reading PMD pagesize failed");

	map = memalign(hpage_len, hpage_len);
	if (!map)
		ksft_exit_fail_msg("memalign failed\n");

	ret = madvise(map, hpage_len, MADV_HUGEPAGE);
	if (ret)
		ksft_exit_fail_msg("madvise failed %d\n", ret);

	for (i = 0; i < hpage_len; i++)
		map[i] = (char)i;

	if (check_huge_anon(map, 1, hpage_len)) {
		ksft_test_result_pass("Test %s huge page allocation\n", __func__);

		clear_softdirty();
		for (i = 0 ; i < TEST_ITERATIONS ; i++) {
			if (pagemap_is_softdirty(pagemap_fd, map) == 1) {
				ksft_print_msg("dirty bit was 1, but should be 0 (i=%d)\n", i);
				break;
			}

			clear_softdirty();
			// Write something to the page to get the dirty bit enabled on the page
			map[0]++;

			if (pagemap_is_softdirty(pagemap_fd, map) == 0) {
				ksft_print_msg("dirty bit was 0, but should be 1 (i=%d)\n", i);
				break;
			}
			clear_softdirty();
		}

		ksft_test_result(i == TEST_ITERATIONS, "Test %s huge page dirty bit\n", __func__);
	} else {
		// hugepage allocation failed. skip these tests
		ksft_test_result_skip("Test %s huge page allocation\n", __func__);
		ksft_test_result_skip("Test %s huge page dirty bit\n", __func__);
	}
	free(map);
}

static void test_mprotect(int pagemap_fd, int pagesize, bool anon)
{
	const char *type[] = {"file", "anon"};
	const char *fname = "./soft-dirty-test-file";
	int test_fd;
	char *map;

	if (anon) {
		map = mmap(NULL, pagesize, PROT_READ|PROT_WRITE,
			   MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
		if (!map)
			ksft_exit_fail_msg("anon mmap failed\n");
	} else {
		test_fd = open(fname, O_RDWR | O_CREAT, 0664);
		if (test_fd < 0) {
			ksft_test_result_skip("Test %s open() file failed\n", __func__);
			return;
		}
		unlink(fname);
		ftruncate(test_fd, pagesize);
		map = mmap(NULL, pagesize, PROT_READ|PROT_WRITE,
			   MAP_SHARED, test_fd, 0);
		if (!map)
			ksft_exit_fail_msg("file mmap failed\n");
	}

	*map = 1;
	ksft_test_result(pagemap_is_softdirty(pagemap_fd, map) == 1,
			 "Test %s-%s dirty bit of new written page\n",
			 __func__, type[anon]);
	clear_softdirty();
	ksft_test_result(pagemap_is_softdirty(pagemap_fd, map) == 0,
			 "Test %s-%s soft-dirty clear after clear_refs\n",
			 __func__, type[anon]);
	mprotect(map, pagesize, PROT_READ);
	ksft_test_result(pagemap_is_softdirty(pagemap_fd, map) == 0,
			 "Test %s-%s soft-dirty clear after marking RO\n",
			 __func__, type[anon]);
	mprotect(map, pagesize, PROT_READ|PROT_WRITE);
	ksft_test_result(pagemap_is_softdirty(pagemap_fd, map) == 0,
			 "Test %s-%s soft-dirty clear after marking RW\n",
			 __func__, type[anon]);
	*map = 2;
	ksft_test_result(pagemap_is_softdirty(pagemap_fd, map) == 1,
			 "Test %s-%s soft-dirty after rewritten\n",
			 __func__, type[anon]);

	munmap(map, pagesize);

	if (!anon)
		close(test_fd);
}

static void test_mprotect_anon(int pagemap_fd, int pagesize)
{
	test_mprotect(pagemap_fd, pagesize, true);
}

static void test_mprotect_file(int pagemap_fd, int pagesize)
{
	test_mprotect(pagemap_fd, pagesize, false);
}

int main(int argc, char **argv)
{
	int pagemap_fd;
	int pagesize;

	ksft_print_header();
	ksft_set_plan(15);

	pagemap_fd = open(PAGEMAP_FILE_PATH, O_RDONLY);
	if (pagemap_fd < 0)
		ksft_exit_fail_msg("Failed to open %s\n", PAGEMAP_FILE_PATH);

	pagesize = getpagesize();

	test_simple(pagemap_fd, pagesize);
	test_vma_reuse(pagemap_fd, pagesize);
	test_hugepage(pagemap_fd, pagesize);
	test_mprotect_anon(pagemap_fd, pagesize);
	test_mprotect_file(pagemap_fd, pagesize);

	close(pagemap_fd);

	return ksft_exit_pass();
}
