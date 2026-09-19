// SPDX-License-Identifier: GPL-2.0
/*
 * Race collapse against faults, GUP pins, fork, mremap and MADV_DONTNEED
 * over the same ranges.  A racing page must read as its pattern or as
 * zero, never anything else; the kernel's own assertions in dmesg are the
 * other half of the check.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include "kselftest.h"
#include "vm_util.h"
#include <mm/hugepage_settings.h>
#include "../../../../mm/gup_test.h"

#ifndef FOLL_WRITE
#define FOLL_WRITE 0x01
#endif

#define BASE_ADDR	((void *)(1UL << 30))
#define PASS_TIMEOUT_S	30

/*
 * PMD-sized areas the racing threads share, plus one for the mremap
 * thread.  -a shrinks it where a PMD is 512M.
 */
#define DEFAULT_SHARED_AREAS	3
static int nr_shared_areas;
static int nr_areas;

static unsigned long hpage_pmd_size;
static unsigned long page_size;
/* nr_areas PMD-sized areas; the last one belongs to the mremap thread */
static char *region;
static char *mremap_area;
static char *mremap_scratch;
static char *pageout_area;
static size_t pageout_size;
static int gup_fd = -1;
static volatile int stop;
static volatile int corrupted;

static unsigned int pattern(unsigned long page_idx)
{
	unsigned int val = (unsigned int)page_idx * 2654435761U;

	return val ? val : 1;	/* never collides with the zero-fill */
}

/* Zero means never written; anything else must be this page's pattern */
static bool page_is_corrupt(unsigned long page_idx, unsigned int *val)
{
	*val = *(unsigned int *)(region + page_idx * page_size);

	return *val && *val != pattern(page_idx);
}

static void check_page(unsigned long page_idx)
{
	unsigned int val;

	if (page_is_corrupt(page_idx, &val)) {
		corrupted = 1;
		ksft_print_msg("Corruption at page %lu: %#x != %#x\n",
			       page_idx, val, pattern(page_idx));
	}
}

static unsigned long shared_pages(void)
{
	return nr_shared_areas * hpage_pmd_size / page_size;
}

static unsigned long rand_page(unsigned int *seed)
{
	return (unsigned long)rand_r(seed) % shared_pages();
}

/* Clamp so a range never reaches the mremap thread's area */
static unsigned long room_from(unsigned long page_idx, unsigned long want)
{
	unsigned long left = shared_pages() - page_idx;

	return want < left ? want : left;
}

static void *faulter_fn(void *arg)
{
	unsigned int seed = (unsigned long)arg;

	while (!stop) {
		unsigned long page_idx = rand_page(&seed);

		if (rand_r(&seed) & 1)
			*(unsigned int *)(region + page_idx * page_size) =
				pattern(page_idx);
		else
			check_page(page_idx);
	}
	return NULL;
}

static void *dontneed_fn(void *arg)
{
	unsigned int seed = (unsigned long)arg;

	while (!stop) {
		unsigned long page_idx = rand_page(&seed);
		unsigned long nr = 1UL << (rand_r(&seed) % 6);	/* 1..32 pages */

		madvise(region + page_idx * page_size,
			room_from(page_idx, nr) * page_size, MADV_DONTNEED);
		usleep(rand_r(&seed) % 500);
	}
	return NULL;
}

static void *pinner_fn(void *arg)
{
	unsigned int seed = (unsigned long)arg;

	while (!stop) {
		struct gup_test gup = {};
		unsigned long page_idx = rand_page(&seed);
		unsigned long nr = room_from(page_idx, 16);

		gup.addr = (unsigned long)(region + page_idx * page_size);
		gup.size = nr * page_size;
		gup.nr_pages_per_call = nr;
		gup.gup_flags = FOLL_WRITE;
		/* Racing MADV_DONTNEED makes transient failures expected */
		ioctl(gup_fd, PIN_FAST_BENCHMARK, &gup);
		usleep(rand_r(&seed) % 200);
	}
	return NULL;
}

static void *forker_fn(void *arg)
{
	unsigned int seed = (unsigned long)arg;

	while (!stop) {
		pid_t pid = fork();

		if (pid == 0) {
			unsigned int val;
			int bad = 0;

			/*
			 * No stdio in the child: a thread may hold stdout's
			 * lock across the fork, and printing under it hangs.
			 */
			for (int i = 0; i < 16; i++)
				bad |= page_is_corrupt(rand_page(&seed), &val);
			_exit(bad);
		}
		if (pid > 0) {
			int wstatus;

			if (waitpid(pid, &wstatus, 0) < 0)
				ksft_exit_fail_perror("waitpid()");
			/* A child killed on the read counts too */
			if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus))
				corrupted = 1;
		}
		usleep(rand_r(&seed) % 2000);
	}
	return NULL;
}

static void *mremapper_fn(void *arg)
{
	unsigned int seed = (unsigned long)arg;

	while (!stop) {
		void *p;

		p = mremap(mremap_area, hpage_pmd_size, hpage_pmd_size,
			   MREMAP_MAYMOVE | MREMAP_FIXED, mremap_scratch);
		if (p == MAP_FAILED)
			ksft_exit_fail_perror("mremap() away");
		for (int i = 0; i < 8; i++)
			mremap_scratch[(rand_r(&seed) %
				(hpage_pmd_size / page_size)) * page_size] = 1;
		p = mremap(mremap_scratch, hpage_pmd_size, hpage_pmd_size,
			   MREMAP_MAYMOVE | MREMAP_FIXED, mremap_area);
		if (p == MAP_FAILED)
			ksft_exit_fail_perror("mremap() back");
		/* The move back unmapped the scratch address: claim it again */
		if (mmap(mremap_scratch, hpage_pmd_size, PROT_NONE,
			 MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED_NOREPLACE,
			 -1, 0) != (void *)mremap_scratch)
			ksft_exit_fail_perror("mmap() mremap scratch");
		usleep(rand_r(&seed) % 2000);
	}
	return NULL;
}

/*
 * Swap traffic and LRU churn on a region nothing else writes, so a page's
 * pattern must survive the trip through swap exactly.
 */
static void *pageout_fn(void *arg)
{
	unsigned int seed = (unsigned long)arg;
	unsigned long nr = pageout_size / page_size;
	unsigned long i;

	for (i = 0; i < nr; i++)
		*(unsigned int *)(pageout_area + i * page_size) = pattern(i);

	while (!stop) {
		madvise(pageout_area, pageout_size, MADV_PAGEOUT);
		for (i = 0; i < nr && !stop; i++) {
			unsigned int val = *(unsigned int *)(pageout_area +
							     i * page_size);

			if (val != pattern(i)) {
				corrupted = 1;
				ksft_print_msg("Pageout corruption at page %lu: %#x != %#x\n",
					       i, val, pattern(i));
			}
		}
		usleep(rand_r(&seed) % 2000);
	}
	return NULL;
}

/* Compaction migrates the collapse sources while they are being gathered */
static void *compactor_fn(void *arg)
{
	unsigned int seed = (unsigned long)arg;
	int fd = open("/proc/sys/vm/compact_memory", O_WRONLY);

	if (fd < 0) {
		ksft_print_msg("No compact_memory; compactor idle\n");
		return NULL;
	}
	while (!stop) {
		if (write(fd, "1", 1) < 0)
			break;
		usleep(10000 + rand_r(&seed) % 100000);
	}
	close(fd);
	return NULL;
}

static bool swap_available(void)
{
	char line[256];
	int lines = 0;
	FILE *fp = fopen("/proc/swaps", "r");

	if (!fp)
		return false;
	while (fgets(line, sizeof(line), fp))
		lines++;
	fclose(fp);
	return lines > 1;
}

static unsigned long now_ms(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000UL + tv.tv_usec / 1000;
}

static void usage(void)
{
	fprintf(stderr,
		"Usage: khugepaged_race [-d seconds] [-m stepped|free|madvise] [-a areas] [-t mask]\n"
		"\tWithout -m, every mode runs in turn.\n"
		"\t-d: seconds per mode (default 5)\n"
		"\t-a: number of shared PMD-sized playground areas (default 3)\n"
		"\t-t: bitmask of racing threads to start, for bisecting a failure\n");
	exit(1);
}

int main(int argc, char **argv)
{
	static const char * const thread_names[] = {
		"faulter", "faulter2", "dontneed", "pinner", "forker",
		"mremapper", "pageout", "compactor",
	};
	void *(*const thread_fns[])(void *) = {
		faulter_fn, faulter_fn, dontneed_fn, pinner_fn, forker_fn,
		mremapper_fn, pageout_fn, compactor_fn,
	};
	enum { T_FAULTER, T_FAULTER2, T_DONTNEED, T_PINNER, T_FORKER,
	       T_MREMAPPER, T_PAGEOUT, T_COMPACTOR };
	const unsigned long pageout_bit = 1UL << T_PAGEOUT;
	const unsigned long compactor_bit = 1UL << T_COMPACTOR;
	const int nr_threads = ARRAY_SIZE(thread_names);
	pthread_t threads[ARRAY_SIZE(thread_names)];
	static const char * const all_modes[] = { "stepped", "free", "madvise" };
	static const bool occupancies[] = { false, true };	/* strict, holes */
	static const bool pressures[] = { false, true };	/* quiet, under pressure */
	const int nr_occupancies = ARRAY_SIZE(occupancies);
	const int nr_pressures = ARRAY_SIZE(pressures);
	const char *one_mode[1];
	const char * const *modes = all_modes;
	int nr_modes = ARRAY_SIZE(all_modes);
	const char *mode_arg = NULL;
	struct thp_settings settings;
	unsigned long end_ms;
	int duration_s = 5;
	unsigned long thread_mask = ~0UL;
	unsigned long base_mask;
	bool have_swap;
	char label[64];
	int nr_areas_arg = 0;
	unsigned long i;
	int steps = 0;
	int opt;

	while ((opt = getopt(argc, argv, "a:d:m:t:h")) != -1) {
		switch (opt) {
		case 'a':
			nr_areas_arg = atoi(optarg);
			break;
		case 'd':
			duration_s = atoi(optarg);
			break;
		case 'm':
			mode_arg = optarg;
			break;
		case 't':
			thread_mask = strtoul(optarg, NULL, 0);
			break;
		default:
			usage();
		}
	}

	if (mode_arg) {
		if (strcmp(mode_arg, "stepped") && strcmp(mode_arg, "free") &&
		    strcmp(mode_arg, "madvise"))
			usage();
		one_mode[0] = mode_arg;
		modes = one_mode;
		nr_modes = 1;
	}

	ksft_print_header();
	if (!thp_available())
		ksft_exit_skip("Transparent Hugepages not available\n");

	page_size = getpagesize();
	hpage_pmd_size = read_pmd_pagesize();
	if (!hpage_pmd_size)
		ksft_exit_fail_msg("Reading PMD pagesize failed\n");

	gup_fd = open("/sys/kernel/debug/gup_test", O_RDWR);
	if (gup_fd < 0)
		ksft_exit_skip("/sys/kernel/debug/gup_test requires CONFIG_GUP_TEST and root\n");

	nr_shared_areas = nr_areas_arg > 0 ? nr_areas_arg : DEFAULT_SHARED_AREAS;
	nr_areas = nr_shared_areas + 1;

	/*
	 * MREMAP_FIXED unmaps whatever is in the way without saying so, so
	 * claim the mremap thread's scratch address up front.
	 */
	mremap_scratch = (char *)BASE_ADDR + 2 * nr_areas * hpage_pmd_size;
	if (mmap(mremap_scratch, hpage_pmd_size, PROT_NONE,
		 MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED_NOREPLACE,
		 -1, 0) != (void *)mremap_scratch)
		ksft_exit_fail_perror("mmap() mremap scratch");

	base_mask = thread_mask;
	have_swap = swap_available();
	if (!have_swap)
		/* No swap, no anon reclaim: compaction-only pressure */
		ksft_print_msg("no swap: the pageout thread is not started\n");

	ksft_set_plan(nr_modes * nr_occupancies * nr_pressures);

	thp_save_settings();
	thp_read_settings(&settings);

	/* Base of the settings stack; the bottom entry is never popped */
	thp_push_settings(&settings);

	for (int run = 0; run < nr_modes * nr_occupancies * nr_pressures; run++) {
		int rem = run % (nr_occupancies * nr_pressures);
		const char *mode = modes[run / (nr_occupancies * nr_pressures)];
		bool holes = occupancies[rem / nr_pressures];
		bool pressure = pressures[rem % nr_pressures];

		snprintf(label, sizeof(label), "%s/%s%s", mode,
			 holes ? "holes" : "strict", pressure ? "/pressure" : "");
		if (corrupted) {
			/* Memory is suspect; the rest would prove nothing */
			ksft_test_result_skip("%s: skipped after corruption\n", label);
			continue;
		}

		thread_mask = base_mask;
		if (!pressure)
			thread_mask &= ~(pageout_bit | compactor_bit);
		else if (!have_swap)
			thread_mask &= ~pageout_bit;

		thp_read_settings(&settings);
		settings.thp_enabled = THP_MADVISE;
		settings.thp_defrag = THP_DEFRAG_ALWAYS;
		settings.shmem_enabled = SHMEM_NEVER;
		settings.khugepaged.defrag = 1;
		settings.khugepaged.scan_sleep_millisecs =
			strcmp(mode, "free") ? 1000 : 0;
		settings.khugepaged.alloc_sleep_millisecs = 10;

		/*
		 * mTHP collapse honours only 0 or HPAGE_PMD_NR - 1 here.  The two
		 * ends race different paths: a strict window has every PTE
		 * present, a hole-heavy one is mostly zero-filled.
		 */
		settings.khugepaged.max_ptes_none = holes ?
			(hpage_pmd_size / page_size) - 1 : 0;
		/* One wake, one pass: the playground plus the forked children's copies */
		settings.khugepaged.pages_to_scan =
			nr_areas * (hpage_pmd_size / page_size) * 8;
		for (i = 0; i < NR_ORDERS; i++) {
			if (thp_supported_orders() & (1UL << i))
				settings.hugepages[i].enabled = THP_INHERIT;
		}
		thp_push_settings(&settings);

		region = mmap(BASE_ADDR, nr_areas * hpage_pmd_size,
			      PROT_READ | PROT_WRITE, MAP_ANONYMOUS |
			      MAP_PRIVATE | MAP_FIXED_NOREPLACE, -1, 0);
		if (region != BASE_ADDR)
			ksft_exit_fail_perror("mmap() playground");
		mremap_area = region + nr_shared_areas * hpage_pmd_size;

		if (thread_mask & pageout_bit) {
			/* Enough to drive real reclaim without swamping a small guest */
			pageout_size = 4 * hpage_pmd_size;
			if (pageout_size < 16UL << 20)
				pageout_size = 16UL << 20;
			if (pageout_size > 64UL << 20)
				pageout_size = 64UL << 20;
			pageout_area = mmap(NULL, pageout_size,
					    PROT_READ | PROT_WRITE,
					    MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
			if (pageout_area == MAP_FAILED)
				ksft_exit_fail_perror("mmap() pageout area");
		}

		/* Populate so the first pass has something to collapse */
		for (i = 0; i < nr_shared_areas * hpage_pmd_size / page_size; i++)
			*(unsigned int *)(region + i * page_size) = pattern(i);
		memset(mremap_area, 1, hpage_pmd_size);
		if (madvise(region, nr_areas * hpage_pmd_size, MADV_HUGEPAGE))
			ksft_exit_fail_perror("madvise(MADV_HUGEPAGE)");

		for (i = 0; i < nr_threads; i++) {
			if (!(thread_mask & (1UL << i))) {
				threads[i] = 0;
				continue;
			}
			if (pthread_create(&threads[i], NULL, thread_fns[i],
					   (void *)(i + 1)))
				ksft_exit_fail_perror(thread_names[i]);
		}

		end_ms = now_ms() + duration_s * 1000UL;
		if (!strcmp(mode, "stepped")) {
			while (now_ms() < end_ms && !corrupted) {
				if (!khugepaged_full_pass(PASS_TIMEOUT_S))
					ksft_exit_fail_msg("khugepaged pass timed out\n");
				steps++;
			}
		} else if (!strcmp(mode, "free")) {
			while (now_ms() < end_ms && !corrupted)
				usleep(100 * 1000);
		} else {	/* madvise */
			while (now_ms() < end_ms && !corrupted) {
				for (i = 0; i < nr_shared_areas; i++) {
					madvise(region + i * hpage_pmd_size,
						hpage_pmd_size, MADV_COLLAPSE);
				}
				madvise(region, nr_shared_areas * hpage_pmd_size,
					MADV_DONTNEED);
				steps++;
			}
		}

		stop = 1;
		for (i = 0; i < nr_threads; i++) {
			if (threads[i])
				pthread_join(threads[i], NULL);
		}

		for (i = 0; i < nr_shared_areas * hpage_pmd_size / page_size; i++)
			check_page(i);

		ksft_test_result(!corrupted, "%s: %ds, %d steps, no corruption\n",
				 label, duration_s, steps);

		/* The next mode maps the same fixed address with its own settings */
		munmap(region, nr_areas * hpage_pmd_size);
		if (pageout_area) {
			munmap(pageout_area, pageout_size);
			pageout_area = NULL;
		}
		thp_pop_settings();
		stop = 0;
		steps = 0;
	}

	ksft_finished();
}
