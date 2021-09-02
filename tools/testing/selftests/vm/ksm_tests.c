// SPDX-License-Identifier: GPL-2.0

#include <sys/mman.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>

#include "../kselftest.h"

#define KSM_SYSFS_PATH "/sys/kernel/mm/ksm/"
#define KSM_FP(s) (KSM_SYSFS_PATH s)
#define KSM_SCAN_LIMIT_SEC_DEFAULT 120
#define KSM_PAGE_COUNT_DEFAULT 10l
#define KSM_PROT_STR_DEFAULT "rw"
#define KSM_USE_ZERO_PAGES_DEFAULT false

struct ksm_sysfs {
	unsigned long max_page_sharing;
	unsigned long merge_across_nodes;
	unsigned long pages_to_scan;
	unsigned long run;
	unsigned long sleep_millisecs;
	unsigned long stable_node_chains_prune_millisecs;
	unsigned long use_zero_pages;
};

enum ksm_test_name {
	CHECK_KSM_MERGE,
	CHECK_KSM_UNMERGE,
	CHECK_KSM_ZERO_PAGE_MERGE
};

static int ksm_write_sysfs(const char *file_path, unsigned long val)
{
	FILE *f = fopen(file_path, "w");

	if (!f) {
		fprintf(stderr, "f %s\n", file_path);
		perror("fopen");
		return 1;
	}
	if (fprintf(f, "%lu", val) < 0) {
		perror("fprintf");
		return 1;
	}
	fclose(f);

	return 0;
}

static int ksm_read_sysfs(const char *file_path, unsigned long *val)
{
	FILE *f = fopen(file_path, "r");

	if (!f) {
		fprintf(stderr, "f %s\n", file_path);
		perror("fopen");
		return 1;
	}
	if (fscanf(f, "%lu", val) != 1) {
		perror("fscanf");
		return 1;
	}
	fclose(f);

	return 0;
}

static int str_to_prot(char *prot_str)
{
	int prot = 0;

	if ((strchr(prot_str, 'r')) != NULL)
		prot |= PROT_READ;
	if ((strchr(prot_str, 'w')) != NULL)
		prot |= PROT_WRITE;
	if ((strchr(prot_str, 'x')) != NULL)
		prot |= PROT_EXEC;

	return prot;
}

static void print_help(void)
{
	printf("usage: ksm_tests [-h] <test type> [-a prot] [-p page_count] [-l timeout]\n"
	       "[-z use_zero_pages]\n");

	printf("Supported <test type>:\n"
	       " -M (page merging)\n"
	       " -Z (zero pages merging)\n"
	       " -U (page unmerging)\n\n");

	printf(" -a: specify the access protections of pages.\n"
	       "     <prot> must be of the form [rwx].\n"
	       "     Default: %s\n", KSM_PROT_STR_DEFAULT);
	printf(" -p: specify the number of pages to test.\n"
	       "     Default: %ld\n", KSM_PAGE_COUNT_DEFAULT);
	printf(" -l: limit the maximum running time (in seconds) for a test.\n"
	       "     Default: %d seconds\n", KSM_SCAN_LIMIT_SEC_DEFAULT);
	printf(" -z: change use_zero_pages tunable\n"
	       "     Default: %d\n", KSM_USE_ZERO_PAGES_DEFAULT);

	exit(0);
}

static void  *allocate_memory(void *ptr, int prot, int mapping, char data, size_t map_size)
{
	void *map_ptr = mmap(ptr, map_size, PROT_WRITE, mapping, -1, 0);

	if (!map_ptr) {
		perror("mmap");
		return NULL;
	}
	memset(map_ptr, data, map_size);
	if (mprotect(map_ptr, map_size, prot)) {
		perror("mprotect");
		munmap(map_ptr, map_size);
		return NULL;
	}

	return map_ptr;
}

static int ksm_do_scan(int scan_count, struct timespec start_time, int timeout)
{
	struct timespec cur_time;
	unsigned long cur_scan, init_scan;

	if (ksm_read_sysfs(KSM_FP("full_scans"), &init_scan))
		return 1;
	cur_scan = init_scan;

	while (cur_scan < init_scan + scan_count) {
		if (ksm_read_sysfs(KSM_FP("full_scans"), &cur_scan))
			return 1;
		if (clock_gettime(CLOCK_MONOTONIC_RAW, &cur_time)) {
			perror("clock_gettime");
			return 1;
		}
		if ((cur_time.tv_sec - start_time.tv_sec) > timeout) {
			printf("Scan time limit exceeded\n");
			return 1;
		}
	}

	return 0;
}

static int ksm_merge_pages(void *addr, size_t size, struct timespec start_time, int timeout)
{
	if (madvise(addr, size, MADV_MERGEABLE)) {
		perror("madvise");
		return 1;
	}
	if (ksm_write_sysfs(KSM_FP("run"), 1))
		return 1;

	/* Since merging occurs only after 2 scans, make sure to get at least 2 full scans */
	if (ksm_do_scan(2, start_time, timeout))
		return 1;

	return 0;
}

static bool assert_ksm_pages_count(long dupl_page_count)
{
	unsigned long max_page_sharing, pages_sharing, pages_shared;

	if (ksm_read_sysfs(KSM_FP("pages_shared"), &pages_shared) ||
	    ksm_read_sysfs(KSM_FP("pages_sharing"), &pages_sharing) ||
	    ksm_read_sysfs(KSM_FP("max_page_sharing"), &max_page_sharing))
		return false;

	/*
	 * Since there must be at least 2 pages for merging and 1 page can be
	 * shared with the limited number of pages (max_page_sharing), sometimes
	 * there are 'leftover' pages that cannot be merged. For example, if there
	 * are 11 pages and max_page_sharing = 10, then only 10 pages will be
	 * merged and the 11th page won't be affected. As a result, when the number
	 * of duplicate pages is divided by max_page_sharing and the remainder is 1,
	 * pages_shared and pages_sharing values will be equal between dupl_page_count
	 * and dupl_page_count - 1.
	 */
	if (dupl_page_count % max_page_sharing == 1 || dupl_page_count % max_page_sharing == 0) {
		if (pages_shared == dupl_page_count / max_page_sharing &&
		    pages_sharing == pages_shared * (max_page_sharing - 1))
			return true;
	} else {
		if (pages_shared == (dupl_page_count / max_page_sharing + 1) &&
		    pages_sharing == dupl_page_count - pages_shared)
			return true;
	}

	return false;
}

static int ksm_save_def(struct ksm_sysfs *ksm_sysfs)
{
	if (ksm_read_sysfs(KSM_FP("max_page_sharing"), &ksm_sysfs->max_page_sharing) ||
	    ksm_read_sysfs(KSM_FP("merge_across_nodes"), &ksm_sysfs->merge_across_nodes) ||
	    ksm_read_sysfs(KSM_FP("sleep_millisecs"), &ksm_sysfs->sleep_millisecs) ||
	    ksm_read_sysfs(KSM_FP("pages_to_scan"), &ksm_sysfs->pages_to_scan) ||
	    ksm_read_sysfs(KSM_FP("run"), &ksm_sysfs->run) ||
	    ksm_read_sysfs(KSM_FP("stable_node_chains_prune_millisecs"),
			   &ksm_sysfs->stable_node_chains_prune_millisecs) ||
	    ksm_read_sysfs(KSM_FP("use_zero_pages"), &ksm_sysfs->use_zero_pages))
		return 1;

	return 0;
}

static int ksm_restore(struct ksm_sysfs *ksm_sysfs)
{
	if (ksm_write_sysfs(KSM_FP("max_page_sharing"), ksm_sysfs->max_page_sharing) ||
	    ksm_write_sysfs(KSM_FP("merge_across_nodes"), ksm_sysfs->merge_across_nodes) ||
	    ksm_write_sysfs(KSM_FP("pages_to_scan"), ksm_sysfs->pages_to_scan) ||
	    ksm_write_sysfs(KSM_FP("run"), ksm_sysfs->run) ||
	    ksm_write_sysfs(KSM_FP("sleep_millisecs"), ksm_sysfs->sleep_millisecs) ||
	    ksm_write_sysfs(KSM_FP("stable_node_chains_prune_millisecs"),
			    ksm_sysfs->stable_node_chains_prune_millisecs) ||
	    ksm_write_sysfs(KSM_FP("use_zero_pages"), ksm_sysfs->use_zero_pages))
		return 1;

	return 0;
}

static int check_ksm_merge(int mapping, int prot, long page_count, int timeout, size_t page_size)
{
	void *map_ptr;
	struct timespec start_time;

	if (clock_gettime(CLOCK_MONOTONIC_RAW, &start_time)) {
		perror("clock_gettime");
		return KSFT_FAIL;
	}

	/* fill pages with the same data and merge them */
	map_ptr = allocate_memory(NULL, prot, mapping, '*', page_size * page_count);
	if (!map_ptr)
		return KSFT_FAIL;

	if (ksm_merge_pages(map_ptr, page_size * page_count, start_time, timeout))
		goto err_out;

	/* verify that the right number of pages are merged */
	if (assert_ksm_pages_count(page_count)) {
		printf("OK\n");
		munmap(map_ptr, page_size * page_count);
		return KSFT_PASS;
	}

err_out:
	printf("Not OK\n");
	munmap(map_ptr, page_size * page_count);
	return KSFT_FAIL;
}

static int check_ksm_unmerge(int mapping, int prot, int timeout, size_t page_size)
{
	void *map_ptr;
	struct timespec start_time;
	int page_count = 2;

	if (clock_gettime(CLOCK_MONOTONIC_RAW, &start_time)) {
		perror("clock_gettime");
		return KSFT_FAIL;
	}

	/* fill pages with the same data and merge them */
	map_ptr = allocate_memory(NULL, prot, mapping, '*', page_size * page_count);
	if (!map_ptr)
		return KSFT_FAIL;

	if (ksm_merge_pages(map_ptr, page_size * page_count, start_time, timeout))
		goto err_out;

	/* change 1 byte in each of the 2 pages -- KSM must automatically unmerge them */
	memset(map_ptr, '-', 1);
	memset(map_ptr + page_size, '+', 1);

	/* get at least 1 scan, so KSM can detect that the pages were modified */
	if (ksm_do_scan(1, start_time, timeout))
		goto err_out;

	/* check that unmerging was successful and 0 pages are currently merged */
	if (assert_ksm_pages_count(0)) {
		printf("OK\n");
		munmap(map_ptr, page_size * page_count);
		return KSFT_PASS;
	}

err_out:
	printf("Not OK\n");
	munmap(map_ptr, page_size * page_count);
	return KSFT_FAIL;
}

static int check_ksm_zero_page_merge(int mapping, int prot, long page_count, int timeout,
				     bool use_zero_pages, size_t page_size)
{
	void *map_ptr;
	struct timespec start_time;

	if (clock_gettime(CLOCK_MONOTONIC_RAW, &start_time)) {
		perror("clock_gettime");
		return KSFT_FAIL;
	}

	if (ksm_write_sysfs(KSM_FP("use_zero_pages"), use_zero_pages))
		return KSFT_FAIL;

	/* fill pages with zero and try to merge them */
	map_ptr = allocate_memory(NULL, prot, mapping, 0, page_size * page_count);
	if (!map_ptr)
		return KSFT_FAIL;

	if (ksm_merge_pages(map_ptr, page_size * page_count, start_time, timeout))
		goto err_out;

       /*
	* verify that the right number of pages are merged:
	* 1) if use_zero_pages is set to 1, empty pages are merged
	*    with the kernel zero page instead of with each other;
	* 2) if use_zero_pages is set to 0, empty pages are not treated specially
	*    and merged as usual.
	*/
	if (use_zero_pages && !assert_ksm_pages_count(0))
		goto err_out;
	else if (!use_zero_pages && !assert_ksm_pages_count(page_count))
		goto err_out;

	printf("OK\n");
	munmap(map_ptr, page_size * page_count);
	return KSFT_PASS;

err_out:
	printf("Not OK\n");
	munmap(map_ptr, page_size * page_count);
	return KSFT_FAIL;
}

int main(int argc, char *argv[])
{
	int ret, opt;
	int prot = 0;
	int ksm_scan_limit_sec = KSM_SCAN_LIMIT_SEC_DEFAULT;
	long page_count = KSM_PAGE_COUNT_DEFAULT;
	size_t page_size = sysconf(_SC_PAGESIZE);
	struct ksm_sysfs ksm_sysfs_old;
	int test_name = CHECK_KSM_MERGE;
	bool use_zero_pages = KSM_USE_ZERO_PAGES_DEFAULT;

	while ((opt = getopt(argc, argv, "ha:p:l:z:MUZ")) != -1) {
		switch (opt) {
		case 'a':
			prot = str_to_prot(optarg);
			break;
		case 'p':
			page_count = atol(optarg);
			if (page_count <= 0) {
				printf("The number of pages must be greater than 0\n");
				return KSFT_FAIL;
			}
			break;
		case 'l':
			ksm_scan_limit_sec = atoi(optarg);
			if (ksm_scan_limit_sec <= 0) {
				printf("Timeout value must be greater than 0\n");
				return KSFT_FAIL;
			}
			break;
		case 'h':
			print_help();
			break;
		case 'z':
			if (strcmp(optarg, "0") == 0)
				use_zero_pages = 0;
			else
				use_zero_pages = 1;
			break;
		case 'M':
			break;
		case 'U':
			test_name = CHECK_KSM_UNMERGE;
			break;
		case 'Z':
			test_name = CHECK_KSM_ZERO_PAGE_MERGE;
			break;
		default:
			return KSFT_FAIL;
		}
	}

	if (prot == 0)
		prot = str_to_prot(KSM_PROT_STR_DEFAULT);

	if (access(KSM_SYSFS_PATH, F_OK)) {
		printf("Config KSM not enabled\n");
		return KSFT_SKIP;
	}

	if (ksm_save_def(&ksm_sysfs_old)) {
		printf("Cannot save default tunables\n");
		return KSFT_FAIL;
	}

	if (ksm_write_sysfs(KSM_FP("run"), 2) ||
	    ksm_write_sysfs(KSM_FP("sleep_millisecs"), 0) ||
	    ksm_write_sysfs(KSM_FP("merge_across_nodes"), 1) ||
	    ksm_write_sysfs(KSM_FP("pages_to_scan"), page_count))
		return KSFT_FAIL;

	switch (test_name) {
	case CHECK_KSM_MERGE:
		ret = check_ksm_merge(MAP_PRIVATE | MAP_ANONYMOUS, prot, page_count,
				      ksm_scan_limit_sec, page_size);
		break;
	case CHECK_KSM_UNMERGE:
		ret = check_ksm_unmerge(MAP_PRIVATE | MAP_ANONYMOUS, prot, ksm_scan_limit_sec,
					page_size);
		break;
	case CHECK_KSM_ZERO_PAGE_MERGE:
		ret = check_ksm_zero_page_merge(MAP_PRIVATE | MAP_ANONYMOUS, prot, page_count,
						ksm_scan_limit_sec, use_zero_pages, page_size);
		break;
	}

	if (ksm_restore(&ksm_sysfs_old)) {
		printf("Cannot restore default tunables\n");
		return KSFT_FAIL;
	}

	return ret;
}
