// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 Google LLC
 */
#define _GNU_SOURCE

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <stdbool.h>

#include "../kselftest.h"

#define EXPECT_SUCCESS 0
#define EXPECT_FAILURE 1
#define NON_OVERLAPPING 0
#define OVERLAPPING 1
#define NS_PER_SEC 1000000000ULL
#define VALIDATION_DEFAULT_THRESHOLD 4	/* 4MB */
#define VALIDATION_NO_THRESHOLD 0	/* Verify the entire region */

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))

struct config {
	unsigned long long src_alignment;
	unsigned long long dest_alignment;
	unsigned long long region_size;
	int overlapping;
};

struct test {
	const char *name;
	struct config config;
	int expect_failure;
};

enum {
	_1KB = 1ULL << 10,	/* 1KB -> not page aligned */
	_4KB = 4ULL << 10,
	_8KB = 8ULL << 10,
	_1MB = 1ULL << 20,
	_2MB = 2ULL << 20,
	_4MB = 4ULL << 20,
	_1GB = 1ULL << 30,
	_2GB = 2ULL << 30,
	PMD = _2MB,
	PUD = _1GB,
};

#define PTE page_size

#define MAKE_TEST(source_align, destination_align, size,	\
		  overlaps, should_fail, test_name)		\
(struct test){							\
	.name = test_name,					\
	.config = {						\
		.src_alignment = source_align,			\
		.dest_alignment = destination_align,		\
		.region_size = size,				\
		.overlapping = overlaps,			\
	},							\
	.expect_failure = should_fail				\
}

/* Returns mmap_min_addr sysctl tunable from procfs */
static unsigned long long get_mmap_min_addr(void)
{
	FILE *fp;
	int n_matched;
	static unsigned long long addr;

	if (addr)
		return addr;

	fp = fopen("/proc/sys/vm/mmap_min_addr", "r");
	if (fp == NULL) {
		ksft_print_msg("Failed to open /proc/sys/vm/mmap_min_addr: %s\n",
			strerror(errno));
		exit(KSFT_SKIP);
	}

	n_matched = fscanf(fp, "%llu", &addr);
	if (n_matched != 1) {
		ksft_print_msg("Failed to read /proc/sys/vm/mmap_min_addr: %s\n",
			strerror(errno));
		fclose(fp);
		exit(KSFT_SKIP);
	}

	fclose(fp);
	return addr;
}

/*
 * Returns false if the requested remap region overlaps with an
 * existing mapping (e.g text, stack) else returns true.
 */
static bool is_remap_region_valid(void *addr, unsigned long long size)
{
	void *remap_addr = NULL;
	bool ret = true;

	/* Use MAP_FIXED_NOREPLACE flag to ensure region is not mapped */
	remap_addr = mmap(addr, size, PROT_READ | PROT_WRITE,
					 MAP_FIXED_NOREPLACE | MAP_ANONYMOUS | MAP_SHARED,
					 -1, 0);

	if (remap_addr == MAP_FAILED) {
		if (errno == EEXIST)
			ret = false;
	} else {
		munmap(remap_addr, size);
	}

	return ret;
}

/* Returns mmap_min_addr sysctl tunable from procfs */
static unsigned long long get_mmap_min_addr(void)
{
	FILE *fp;
	int n_matched;
	static unsigned long long addr;

	if (addr)
		return addr;

	fp = fopen("/proc/sys/vm/mmap_min_addr", "r");
	if (fp == NULL) {
		ksft_print_msg("Failed to open /proc/sys/vm/mmap_min_addr: %s\n",
			strerror(errno));
		exit(KSFT_SKIP);
	}

	n_matched = fscanf(fp, "%llu", &addr);
	if (n_matched != 1) {
		ksft_print_msg("Failed to read /proc/sys/vm/mmap_min_addr: %s\n",
			strerror(errno));
		fclose(fp);
		exit(KSFT_SKIP);
	}

	fclose(fp);
	return addr;
}

/*
 * Returns the start address of the mapping on success, else returns
 * NULL on failure.
 */
static void *get_source_mapping(struct config c)
{
	unsigned long long addr = 0ULL;
	void *src_addr = NULL;
	unsigned long long mmap_min_addr;

	mmap_min_addr = get_mmap_min_addr();

retry:
	addr += c.src_alignment;
	if (addr < mmap_min_addr)
		goto retry;

	src_addr = mmap((void *) addr, c.region_size, PROT_READ | PROT_WRITE,
					MAP_FIXED_NOREPLACE | MAP_ANONYMOUS | MAP_SHARED,
					-1, 0);
	if (src_addr == MAP_FAILED) {
		if (errno == EPERM || errno == EEXIST)
			goto retry;
		goto error;
	}
	/*
	 * Check that the address is aligned to the specified alignment.
	 * Addresses which have alignments that are multiples of that
	 * specified are not considered valid. For instance, 1GB address is
	 * 2MB-aligned, however it will not be considered valid for a
	 * requested alignment of 2MB. This is done to reduce coincidental
	 * alignment in the tests.
	 */
	if (((unsigned long long) src_addr & (c.src_alignment - 1)) ||
			!((unsigned long long) src_addr & c.src_alignment)) {
		munmap(src_addr, c.region_size);
		goto retry;
	}

	if (!src_addr)
		goto error;

	return src_addr;
error:
	ksft_print_msg("Failed to map source region: %s\n",
			strerror(errno));
	return NULL;
}

/* Returns the time taken for the remap on success else returns -1. */
static long long remap_region(struct config c, unsigned int threshold_mb,
			      char pattern_seed)
{
	void *addr, *src_addr, *dest_addr;
	unsigned long long i;
	struct timespec t_start = {0, 0}, t_end = {0, 0};
	long long  start_ns, end_ns, align_mask, ret, offset;
	unsigned long long threshold;

	if (threshold_mb == VALIDATION_NO_THRESHOLD)
		threshold = c.region_size;
	else
		threshold = MIN(threshold_mb * _1MB, c.region_size);

	src_addr = get_source_mapping(c);
	if (!src_addr) {
		ret = -1;
		goto out;
	}

	/* Set byte pattern */
	srand(pattern_seed);
	for (i = 0; i < threshold; i++)
		memset((char *) src_addr + i, (char) rand(), 1);

	/* Mask to zero out lower bits of address for alignment */
	align_mask = ~(c.dest_alignment - 1);
	/* Offset of destination address from the end of the source region */
	offset = (c.overlapping) ? -c.dest_alignment : c.dest_alignment;
	addr = (void *) (((unsigned long long) src_addr + c.region_size
			  + offset) & align_mask);

	/* See comment in get_source_mapping() */
	if (!((unsigned long long) addr & c.dest_alignment))
		addr = (void *) ((unsigned long long) addr | c.dest_alignment);

	/* Don't destroy existing mappings unless expected to overlap */
	while (!is_remap_region_valid(addr, c.region_size) && !c.overlapping) {
		/* Check for unsigned overflow */
		if (addr + c.dest_alignment < addr) {
			ksft_print_msg("Couldn't find a valid region to remap to\n");
			ret = -1;
			goto out;
		}
		addr += c.dest_alignment;
	}

	clock_gettime(CLOCK_MONOTONIC, &t_start);
	dest_addr = mremap(src_addr, c.region_size, c.region_size,
					  MREMAP_MAYMOVE|MREMAP_FIXED, (char *) addr);
	clock_gettime(CLOCK_MONOTONIC, &t_end);

	if (dest_addr == MAP_FAILED) {
		ksft_print_msg("mremap failed: %s\n", strerror(errno));
		ret = -1;
		goto clean_up_src;
	}

	/* Verify byte pattern after remapping */
	srand(pattern_seed);
	for (i = 0; i < threshold; i++) {
		char c = (char) rand();

		if (((char *) dest_addr)[i] != c) {
			ksft_print_msg("Data after remap doesn't match at offset %d\n",
				       i);
			ksft_print_msg("Expected: %#x\t Got: %#x\n", c & 0xff,
					((char *) dest_addr)[i] & 0xff);
			ret = -1;
			goto clean_up_dest;
		}
	}

	start_ns = t_start.tv_sec * NS_PER_SEC + t_start.tv_nsec;
	end_ns = t_end.tv_sec * NS_PER_SEC + t_end.tv_nsec;
	ret = end_ns - start_ns;

/*
 * Since the destination address is specified using MREMAP_FIXED, subsequent
 * mremap will unmap any previous mapping at the address range specified by
 * dest_addr and region_size. This significantly affects the remap time of
 * subsequent tests. So we clean up mappings after each test.
 */
clean_up_dest:
	munmap(dest_addr, c.region_size);
clean_up_src:
	munmap(src_addr, c.region_size);
out:
	return ret;
}

static void run_mremap_test_case(struct test test_case, int *failures,
				 unsigned int threshold_mb,
				 unsigned int pattern_seed)
{
	long long remap_time = remap_region(test_case.config, threshold_mb,
					    pattern_seed);

	if (remap_time < 0) {
		if (test_case.expect_failure)
			ksft_test_result_pass("%s\n\tExpected mremap failure\n",
					      test_case.name);
		else {
			ksft_test_result_fail("%s\n", test_case.name);
			*failures += 1;
		}
	} else {
		/*
		 * Comparing mremap time is only applicable if entire region
		 * was faulted in.
		 */
		if (threshold_mb == VALIDATION_NO_THRESHOLD ||
		    test_case.config.region_size <= threshold_mb * _1MB)
			ksft_test_result_pass("%s\n\tmremap time: %12lldns\n",
					      test_case.name, remap_time);
		else
			ksft_test_result_pass("%s\n", test_case.name);
	}
}

static void usage(const char *cmd)
{
	fprintf(stderr,
		"Usage: %s [[-t <threshold_mb>] [-p <pattern_seed>]]\n"
		"-t\t only validate threshold_mb of the remapped region\n"
		"  \t if 0 is supplied no threshold is used; all tests\n"
		"  \t are run and remapped regions validated fully.\n"
		"  \t The default threshold used is 4MB.\n"
		"-p\t provide a seed to generate the random pattern for\n"
		"  \t validating the remapped region.\n", cmd);
}

static int parse_args(int argc, char **argv, unsigned int *threshold_mb,
		      unsigned int *pattern_seed)
{
	const char *optstr = "t:p:";
	int opt;

	while ((opt = getopt(argc, argv, optstr)) != -1) {
		switch (opt) {
		case 't':
			*threshold_mb = atoi(optarg);
			break;
		case 'p':
			*pattern_seed = atoi(optarg);
			break;
		default:
			usage(argv[0]);
			return -1;
		}
	}

	if (optind < argc) {
		usage(argv[0]);
		return -1;
	}

	return 0;
}

#define MAX_TEST 13
#define MAX_PERF_TEST 3
int main(int argc, char **argv)
{
	int failures = 0;
	int i, run_perf_tests;
	unsigned int threshold_mb = VALIDATION_DEFAULT_THRESHOLD;
	unsigned int pattern_seed;
	struct test test_cases[MAX_TEST];
	struct test perf_test_cases[MAX_PERF_TEST];
	int page_size;
	time_t t;

	pattern_seed = (unsigned int) time(&t);

	if (parse_args(argc, argv, &threshold_mb, &pattern_seed) < 0)
		exit(EXIT_FAILURE);

	ksft_print_msg("Test configs:\n\tthreshold_mb=%u\n\tpattern_seed=%u\n\n",
		       threshold_mb, pattern_seed);

	page_size = sysconf(_SC_PAGESIZE);

	/* Expected mremap failures */
	test_cases[0] =	MAKE_TEST(page_size, page_size, page_size,
				  OVERLAPPING, EXPECT_FAILURE,
				  "mremap - Source and Destination Regions Overlapping");

	test_cases[1] = MAKE_TEST(page_size, page_size/4, page_size,
				  NON_OVERLAPPING, EXPECT_FAILURE,
				  "mremap - Destination Address Misaligned (1KB-aligned)");
	test_cases[2] = MAKE_TEST(page_size/4, page_size, page_size,
				  NON_OVERLAPPING, EXPECT_FAILURE,
				  "mremap - Source Address Misaligned (1KB-aligned)");

	/* Src addr PTE aligned */
	test_cases[3] = MAKE_TEST(PTE, PTE, PTE * 2,
				  NON_OVERLAPPING, EXPECT_SUCCESS,
				  "8KB mremap - Source PTE-aligned, Destination PTE-aligned");

	/* Src addr 1MB aligned */
	test_cases[4] = MAKE_TEST(_1MB, PTE, _2MB, NON_OVERLAPPING, EXPECT_SUCCESS,
				  "2MB mremap - Source 1MB-aligned, Destination PTE-aligned");
	test_cases[5] = MAKE_TEST(_1MB, _1MB, _2MB, NON_OVERLAPPING, EXPECT_SUCCESS,
				  "2MB mremap - Source 1MB-aligned, Destination 1MB-aligned");

	/* Src addr PMD aligned */
	test_cases[6] = MAKE_TEST(PMD, PTE, _4MB, NON_OVERLAPPING, EXPECT_SUCCESS,
				  "4MB mremap - Source PMD-aligned, Destination PTE-aligned");
	test_cases[7] =	MAKE_TEST(PMD, _1MB, _4MB, NON_OVERLAPPING, EXPECT_SUCCESS,
				  "4MB mremap - Source PMD-aligned, Destination 1MB-aligned");
	test_cases[8] = MAKE_TEST(PMD, PMD, _4MB, NON_OVERLAPPING, EXPECT_SUCCESS,
				  "4MB mremap - Source PMD-aligned, Destination PMD-aligned");

	/* Src addr PUD aligned */
	test_cases[9] = MAKE_TEST(PUD, PTE, _2GB, NON_OVERLAPPING, EXPECT_SUCCESS,
				  "2GB mremap - Source PUD-aligned, Destination PTE-aligned");
	test_cases[10] = MAKE_TEST(PUD, _1MB, _2GB, NON_OVERLAPPING, EXPECT_SUCCESS,
				   "2GB mremap - Source PUD-aligned, Destination 1MB-aligned");
	test_cases[11] = MAKE_TEST(PUD, PMD, _2GB, NON_OVERLAPPING, EXPECT_SUCCESS,
				   "2GB mremap - Source PUD-aligned, Destination PMD-aligned");
	test_cases[12] = MAKE_TEST(PUD, PUD, _2GB, NON_OVERLAPPING, EXPECT_SUCCESS,
				   "2GB mremap - Source PUD-aligned, Destination PUD-aligned");

	perf_test_cases[0] =  MAKE_TEST(page_size, page_size, _1GB, NON_OVERLAPPING, EXPECT_SUCCESS,
					"1GB mremap - Source PTE-aligned, Destination PTE-aligned");
	/*
	 * mremap 1GB region - Page table level aligned time
	 * comparison.
	 */
	perf_test_cases[1] = MAKE_TEST(PMD, PMD, _1GB, NON_OVERLAPPING, EXPECT_SUCCESS,
				       "1GB mremap - Source PMD-aligned, Destination PMD-aligned");
	perf_test_cases[2] = MAKE_TEST(PUD, PUD, _1GB, NON_OVERLAPPING, EXPECT_SUCCESS,
				       "1GB mremap - Source PUD-aligned, Destination PUD-aligned");

	run_perf_tests =  (threshold_mb == VALIDATION_NO_THRESHOLD) ||
				(threshold_mb * _1MB >= _1GB);

	ksft_set_plan(ARRAY_SIZE(test_cases) + (run_perf_tests ?
		      ARRAY_SIZE(perf_test_cases) : 0));

	for (i = 0; i < ARRAY_SIZE(test_cases); i++)
		run_mremap_test_case(test_cases[i], &failures, threshold_mb,
				     pattern_seed);

	if (run_perf_tests) {
		ksft_print_msg("\n%s\n",
		 "mremap HAVE_MOVE_PMD/PUD optimization time comparison for 1GB region:");
		for (i = 0; i < ARRAY_SIZE(perf_test_cases); i++)
			run_mremap_test_case(perf_test_cases[i], &failures,
					     threshold_mb, pattern_seed);
	}

	if (failures > 0)
		ksft_exit_fail();
	else
		ksft_exit_pass();
}
