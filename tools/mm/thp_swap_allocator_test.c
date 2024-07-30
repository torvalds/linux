// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * thp_swap_allocator_test
 *
 * The purpose of this test program is helping check if THP swpout
 * can correctly get swap slots to swap out as a whole instead of
 * being split. It randomly releases swap entries through madvise
 * DONTNEED and swapin/out on two memory areas: a memory area for
 * 64KB THP and the other area for small folios. The second memory
 * can be enabled by "-s".
 * Before running the program, we need to setup a zRAM or similar
 * swap device by:
 *  echo lzo > /sys/block/zram0/comp_algorithm
 *  echo 64M > /sys/block/zram0/disksize
 *  echo never > /sys/kernel/mm/transparent_hugepage/hugepages-2048kB/enabled
 *  echo always > /sys/kernel/mm/transparent_hugepage/hugepages-64kB/enabled
 *  mkswap /dev/zram0
 *  swapon /dev/zram0
 * The expected result should be 0% anon swpout fallback ratio w/ or
 * w/o "-s".
 *
 * Author(s): Barry Song <v-songbaohua@oppo.com>
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <linux/mman.h>
#include <sys/mman.h>
#include <errno.h>
#include <time.h>

#define MEMSIZE_MTHP (60 * 1024 * 1024)
#define MEMSIZE_SMALLFOLIO (4 * 1024 * 1024)
#define ALIGNMENT_MTHP (64 * 1024)
#define ALIGNMENT_SMALLFOLIO (4 * 1024)
#define TOTAL_DONTNEED_MTHP (16 * 1024 * 1024)
#define TOTAL_DONTNEED_SMALLFOLIO (1 * 1024 * 1024)
#define MTHP_FOLIO_SIZE (64 * 1024)

#define SWPOUT_PATH \
	"/sys/kernel/mm/transparent_hugepage/hugepages-64kB/stats/swpout"
#define SWPOUT_FALLBACK_PATH \
	"/sys/kernel/mm/transparent_hugepage/hugepages-64kB/stats/swpout_fallback"

static void *aligned_alloc_mem(size_t size, size_t alignment)
{
	void *mem = NULL;

	if (posix_memalign(&mem, alignment, size) != 0) {
		perror("posix_memalign");
		return NULL;
	}
	return mem;
}

/*
 * This emulates the behavior of native libc and Java heap,
 * as well as process exit and munmap. It helps generate mTHP
 * and ensures that iterations can proceed with mTHP, as we
 * currently don't support large folios swap-in.
 */
static void random_madvise_dontneed(void *mem, size_t mem_size,
		size_t align_size, size_t total_dontneed_size)
{
	size_t num_pages = total_dontneed_size / align_size;
	size_t i;
	size_t offset;
	void *addr;

	for (i = 0; i < num_pages; ++i) {
		offset = (rand() % (mem_size / align_size)) * align_size;
		addr = (char *)mem + offset;
		if (madvise(addr, align_size, MADV_DONTNEED) != 0)
			perror("madvise dontneed");

		memset(addr, 0x11, align_size);
	}
}

static void random_swapin(void *mem, size_t mem_size,
		size_t align_size, size_t total_swapin_size)
{
	size_t num_pages = total_swapin_size / align_size;
	size_t i;
	size_t offset;
	void *addr;

	for (i = 0; i < num_pages; ++i) {
		offset = (rand() % (mem_size / align_size)) * align_size;
		addr = (char *)mem + offset;
		memset(addr, 0x11, align_size);
	}
}

static unsigned long read_stat(const char *path)
{
	FILE *file;
	unsigned long value;

	file = fopen(path, "r");
	if (!file) {
		perror("fopen");
		return 0;
	}

	if (fscanf(file, "%lu", &value) != 1) {
		perror("fscanf");
		fclose(file);
		return 0;
	}

	fclose(file);
	return value;
}

int main(int argc, char *argv[])
{
	int use_small_folio = 0, aligned_swapin = 0;
	void *mem1 = NULL, *mem2 = NULL;
	int i;

	for (i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "-s") == 0)
			use_small_folio = 1;
		else if (strcmp(argv[i], "-a") == 0)
			aligned_swapin = 1;
	}

	mem1 = aligned_alloc_mem(MEMSIZE_MTHP, ALIGNMENT_MTHP);
	if (mem1 == NULL) {
		fprintf(stderr, "Failed to allocate large folios memory\n");
		return EXIT_FAILURE;
	}

	if (madvise(mem1, MEMSIZE_MTHP, MADV_HUGEPAGE) != 0) {
		perror("madvise hugepage for mem1");
		free(mem1);
		return EXIT_FAILURE;
	}

	if (use_small_folio) {
		mem2 = aligned_alloc_mem(MEMSIZE_SMALLFOLIO, ALIGNMENT_MTHP);
		if (mem2 == NULL) {
			fprintf(stderr, "Failed to allocate small folios memory\n");
			free(mem1);
			return EXIT_FAILURE;
		}

		if (madvise(mem2, MEMSIZE_SMALLFOLIO, MADV_NOHUGEPAGE) != 0) {
			perror("madvise nohugepage for mem2");
			free(mem1);
			free(mem2);
			return EXIT_FAILURE;
		}
	}

	/* warm-up phase to occupy the swapfile */
	memset(mem1, 0x11, MEMSIZE_MTHP);
	madvise(mem1, MEMSIZE_MTHP, MADV_PAGEOUT);
	if (use_small_folio) {
		memset(mem2, 0x11, MEMSIZE_SMALLFOLIO);
		madvise(mem2, MEMSIZE_SMALLFOLIO, MADV_PAGEOUT);
	}

	/* iterations with newly created mTHP, swap-in, and swap-out */
	for (i = 0; i < 100; ++i) {
		unsigned long initial_swpout;
		unsigned long initial_swpout_fallback;
		unsigned long final_swpout;
		unsigned long final_swpout_fallback;
		unsigned long swpout_inc;
		unsigned long swpout_fallback_inc;
		double fallback_percentage;

		initial_swpout = read_stat(SWPOUT_PATH);
		initial_swpout_fallback = read_stat(SWPOUT_FALLBACK_PATH);

		/*
		 * The following setup creates a 1:1 ratio of mTHP to small folios
		 * since large folio swap-in isn't supported yet. Once we support
		 * mTHP swap-in, we'll likely need to reduce MEMSIZE_MTHP and
		 * increase MEMSIZE_SMALLFOLIO to maintain the ratio.
		 */
		random_swapin(mem1, MEMSIZE_MTHP,
				aligned_swapin ? ALIGNMENT_MTHP : ALIGNMENT_SMALLFOLIO,
				TOTAL_DONTNEED_MTHP);
		random_madvise_dontneed(mem1, MEMSIZE_MTHP, ALIGNMENT_MTHP,
				TOTAL_DONTNEED_MTHP);

		if (use_small_folio) {
			random_swapin(mem2, MEMSIZE_SMALLFOLIO,
					ALIGNMENT_SMALLFOLIO,
					TOTAL_DONTNEED_SMALLFOLIO);
		}

		if (madvise(mem1, MEMSIZE_MTHP, MADV_PAGEOUT) != 0) {
			perror("madvise pageout for mem1");
			free(mem1);
			if (mem2 != NULL)
				free(mem2);
			return EXIT_FAILURE;
		}

		if (use_small_folio) {
			if (madvise(mem2, MEMSIZE_SMALLFOLIO, MADV_PAGEOUT) != 0) {
				perror("madvise pageout for mem2");
				free(mem1);
				free(mem2);
				return EXIT_FAILURE;
			}
		}

		final_swpout = read_stat(SWPOUT_PATH);
		final_swpout_fallback = read_stat(SWPOUT_FALLBACK_PATH);

		swpout_inc = final_swpout - initial_swpout;
		swpout_fallback_inc = final_swpout_fallback - initial_swpout_fallback;

		fallback_percentage = (double)swpout_fallback_inc /
			(swpout_fallback_inc + swpout_inc) * 100;

		printf("Iteration %d: swpout inc: %lu, swpout fallback inc: %lu, Fallback percentage: %.2f%%\n",
				i + 1, swpout_inc, swpout_fallback_inc, fallback_percentage);
	}

	free(mem1);
	if (mem2 != NULL)
		free(mem2);

	return EXIT_SUCCESS;
}
