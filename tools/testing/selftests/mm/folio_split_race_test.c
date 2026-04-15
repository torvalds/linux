// SPDX-License-Identifier: GPL-2.0
/*
 * The test creates shmem PMD huge pages, fills all pages with known patterns,
 * then continuously verifies non-punched pages with 16 threads. Meanwhile, the
 * main thread punches holes via MADV_REMOVE on the shmem.
 *
 * It tests the race condition between folio_split() and filemap_get_entry(),
 * where the hole punches on shmem lead to folio_split() and reading the shmem
 * lead to filemap_get_entry().
 */

#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <linux/mman.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <signal.h>
#include <unistd.h>
#include "vm_util.h"
#include "kselftest.h"
#include "thp_settings.h"

uint64_t page_size;
uint64_t pmd_pagesize;
#define NR_PMD_PAGE 5
#define FILE_SIZE (pmd_pagesize * NR_PMD_PAGE)
#define TOTAL_PAGES (FILE_SIZE / page_size)

/* Every N-th to N+M-th pages are punched; not aligned with huge page boundaries. */
#define PUNCH_INTERVAL 50 /* N */
#define PUNCH_SIZE_FACTOR 3 /* M */

#define NUM_READER_THREADS 16
#define FILL_BYTE 0xAF
#define NUM_ITERATIONS 100

/* Shared control block: control reading threads and record stats */
struct shared_ctl {
	atomic_uint_fast32_t stop;
	atomic_uint_fast64_t reader_failures;
	atomic_uint_fast64_t reader_verified;
	pthread_barrier_t barrier;
};

static void fill_page(unsigned char *base, size_t page_idx)
{
	unsigned char *page_ptr = base + page_idx * page_size;
	uint64_t idx = (uint64_t)page_idx;

	memset(page_ptr, FILL_BYTE, page_size);
	memcpy(page_ptr, &idx, sizeof(idx));
}

/* Returns true if valid, false if corrupted. */
static bool check_page(unsigned char *base, uint64_t page_idx)
{
	unsigned char *page_ptr = base + page_idx * page_size;
	uint64_t expected_idx = (uint64_t)page_idx;
	uint64_t got_idx;

	memcpy(&got_idx, page_ptr, 8);

	if (got_idx != expected_idx) {
		uint64_t off;
		int all_zero = 1;

		for (off = 0; off < page_size; off++) {
			if (page_ptr[off] != 0) {
				all_zero = 0;
				break;
			}
		}
		if (all_zero) {
			ksft_print_msg("CORRUPTED: page %" PRIu64
				       " (huge page %" PRIu64
				       ") is ALL ZEROS\n",
				       page_idx,
				       (page_idx * page_size) / pmd_pagesize);
		} else {
			ksft_print_msg("CORRUPTED: page %" PRIu64
				       " (huge page %" PRIu64
				       "): expected idx %" PRIu64
				       ", got %" PRIu64 "\n",
				       page_idx,
				       (page_idx * page_size) / pmd_pagesize,
				       page_idx, got_idx);
		}
		return false;
	}
	return true;
}

struct reader_arg {
	unsigned char *base;
	struct shared_ctl *ctl;
	int tid;
	atomic_uint_fast64_t *failures;
	atomic_uint_fast64_t *verified;
};

static void *reader_thread(void *arg)
{
	struct reader_arg *ra = (struct reader_arg *)arg;
	unsigned char *base = ra->base;
	struct shared_ctl *ctl = ra->ctl;
	int tid = ra->tid;
	atomic_uint_fast64_t *failures = ra->failures;
	atomic_uint_fast64_t *verified = ra->verified;
	uint64_t page_idx;

	pthread_barrier_wait(&ctl->barrier);

	while (atomic_load_explicit(&ctl->stop, memory_order_acquire) == 0) {
		for (page_idx = (size_t)tid; page_idx < TOTAL_PAGES;
		     page_idx += NUM_READER_THREADS) {
			/*
			 * page_idx % PUNCH_INTERVAL is in [0, PUNCH_INTERVAL),
			 * skip [0, PUNCH_SIZE_FACTOR)
			 */
			if (page_idx % PUNCH_INTERVAL < PUNCH_SIZE_FACTOR)
				continue;
			if (check_page(base, page_idx))
				atomic_fetch_add_explicit(verified, 1,
							  memory_order_relaxed);
			else
				atomic_fetch_add_explicit(failures, 1,
							  memory_order_relaxed);
		}
		if (atomic_load_explicit(failures, memory_order_relaxed) > 0)
			break;
	}

	return NULL;
}

static void create_readers(pthread_t *threads, struct reader_arg *args,
			   unsigned char *base, struct shared_ctl *ctl)
{
	int i;

	for (i = 0; i < NUM_READER_THREADS; i++) {
		args[i].base = base;
		args[i].ctl = ctl;
		args[i].tid = i;
		args[i].failures = &ctl->reader_failures;
		args[i].verified = &ctl->reader_verified;
		if (pthread_create(&threads[i], NULL, reader_thread,
				   &args[i]) != 0)
			ksft_exit_fail_msg("pthread_create failed\n");
	}
}

/* Run a single iteration. Returns total number of corrupted pages. */
static uint64_t run_iteration(void)
{
	uint64_t reader_failures, reader_verified;
	struct reader_arg args[NUM_READER_THREADS];
	pthread_t threads[NUM_READER_THREADS];
	unsigned char *mmap_base;
	struct shared_ctl ctl;
	uint64_t i;

	memset(&ctl, 0, sizeof(struct shared_ctl));

	mmap_base = mmap(NULL, FILE_SIZE, PROT_READ | PROT_WRITE,
			 MAP_SHARED | MAP_ANONYMOUS, -1, 0);

	if (mmap_base == MAP_FAILED)
		ksft_exit_fail_msg("mmap failed: %d\n", errno);

	if (madvise(mmap_base, FILE_SIZE, MADV_HUGEPAGE) != 0)
		ksft_exit_fail_msg("madvise(MADV_HUGEPAGE) failed: %d\n",
				   errno);

	for (i = 0; i < TOTAL_PAGES; i++)
		fill_page(mmap_base, i);

	if (!check_huge_shmem(mmap_base, NR_PMD_PAGE, pmd_pagesize))
		ksft_exit_fail_msg("No shmem THP is allocated\n");

	if (pthread_barrier_init(&ctl.barrier, NULL, NUM_READER_THREADS + 1) != 0)
		ksft_exit_fail_msg("pthread_barrier_init failed\n");

	create_readers(threads, args, mmap_base, &ctl);

	/* Wait for all reader threads to be ready before punching holes. */
	pthread_barrier_wait(&ctl.barrier);

	for (i = 0; i < TOTAL_PAGES; i++) {
		if (i % PUNCH_INTERVAL != 0)
			continue;
		if (madvise(mmap_base + i * page_size,
			    PUNCH_SIZE_FACTOR * page_size, MADV_REMOVE) != 0) {
			ksft_exit_fail_msg(
				"madvise(MADV_REMOVE) failed on page %" PRIu64 ": %d\n",
				i, errno);
		}

		i += PUNCH_SIZE_FACTOR - 1;
	}

	atomic_store_explicit(&ctl.stop, 1, memory_order_release);

	for (i = 0; i < NUM_READER_THREADS; i++)
		pthread_join(threads[i], NULL);

	pthread_barrier_destroy(&ctl.barrier);

	reader_failures = atomic_load_explicit(&ctl.reader_failures,
					       memory_order_acquire);
	reader_verified = atomic_load_explicit(&ctl.reader_verified,
					       memory_order_acquire);
	if (reader_failures)
		ksft_print_msg("Child: %" PRIu64 " pages verified, %" PRIu64 " failures\n",
			       reader_verified, reader_failures);

	munmap(mmap_base, FILE_SIZE);

	return reader_failures;
}

static void thp_cleanup_handler(int signum)
{
	thp_restore_settings();
	/*
	 * Restore default handler and re-raise the signal to exit.
	 * This is to ensure the test process exits with the correct
	 * status code corresponding to the signal.
	 */
	signal(signum, SIG_DFL);
	raise(signum);
}

static void thp_settings_cleanup(void)
{
	thp_restore_settings();
}

int main(void)
{
	struct thp_settings current_settings;
	uint64_t corrupted_pages;
	uint64_t iter;

	ksft_print_header();

	page_size = getpagesize();
	pmd_pagesize = read_pmd_pagesize();

	if (!thp_available() || !pmd_pagesize)
		ksft_exit_skip("Transparent Hugepages not available\n");

	if (geteuid() != 0)
		ksft_exit_skip("Please run the test as root\n");

	thp_save_settings();
	/* make sure thp settings are restored */
	if (atexit(thp_settings_cleanup) != 0)
		ksft_exit_fail_msg("atexit failed\n");

	signal(SIGINT, thp_cleanup_handler);
	signal(SIGTERM, thp_cleanup_handler);

	thp_read_settings(&current_settings);
	current_settings.shmem_enabled = SHMEM_ADVISE;
	thp_write_settings(&current_settings);

	ksft_set_plan(1);

	ksft_print_msg("folio split race test\n");

	for (iter = 0; iter < NUM_ITERATIONS; iter++) {
		corrupted_pages = run_iteration();
		if (corrupted_pages > 0)
			break;
	}

	if (iter < NUM_ITERATIONS)
		ksft_test_result_fail("FAILED on iteration %" PRIu64
				      ": %" PRIu64
				      " pages corrupted by MADV_REMOVE!\n",
				      iter, corrupted_pages);
	else
		ksft_test_result_pass("All %d iterations passed\n",
				      NUM_ITERATIONS);

	ksft_exit(iter == NUM_ITERATIONS);

	return 0;
}
