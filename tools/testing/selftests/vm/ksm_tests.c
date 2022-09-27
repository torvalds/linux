// SPDX-License-Identifier: GPL-2.0

#include <sys/mman.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>
#include <numa.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <err.h>

#include "../kselftest.h"
#include "../../../../include/vdso/time64.h"
#include "util.h"

#define KSM_SYSFS_PATH "/sys/kernel/mm/ksm/"
#define KSM_FP(s) (KSM_SYSFS_PATH s)
#define KSM_SCAN_LIMIT_SEC_DEFAULT 120
#define KSM_PAGE_COUNT_DEFAULT 10l
#define KSM_PROT_STR_DEFAULT "rw"
#define KSM_USE_ZERO_PAGES_DEFAULT false
#define KSM_MERGE_ACROSS_NODES_DEFAULT true
#define MB (1ul << 20)

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
	CHECK_KSM_ZERO_PAGE_MERGE,
	CHECK_KSM_NUMA_MERGE,
	KSM_MERGE_TIME,
	KSM_MERGE_TIME_HUGE_PAGES,
	KSM_COW_TIME
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
		fclose(f);
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
		fclose(f);
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
	       "[-z use_zero_pages] [-m merge_across_nodes] [-s size]\n");

	printf("Supported <test type>:\n"
	       " -M (page merging)\n"
	       " -Z (zero pages merging)\n"
	       " -N (merging of pages in different NUMA nodes)\n"
	       " -U (page unmerging)\n"
	       " -P evaluate merging time and speed.\n"
	       "    For this test, the size of duplicated memory area (in MiB)\n"
	       "    must be provided using -s option\n"
				 " -H evaluate merging time and speed of area allocated mostly with huge pages\n"
	       "    For this test, the size of duplicated memory area (in MiB)\n"
	       "    must be provided using -s option\n"
	       " -C evaluate the time required to break COW of merged pages.\n\n");

	printf(" -a: specify the access protections of pages.\n"
	       "     <prot> must be of the form [rwx].\n"
	       "     Default: %s\n", KSM_PROT_STR_DEFAULT);
	printf(" -p: specify the number of pages to test.\n"
	       "     Default: %ld\n", KSM_PAGE_COUNT_DEFAULT);
	printf(" -l: limit the maximum running time (in seconds) for a test.\n"
	       "     Default: %d seconds\n", KSM_SCAN_LIMIT_SEC_DEFAULT);
	printf(" -z: change use_zero_pages tunable\n"
	       "     Default: %d\n", KSM_USE_ZERO_PAGES_DEFAULT);
	printf(" -m: change merge_across_nodes tunable\n"
	       "     Default: %d\n", KSM_MERGE_ACROSS_NODES_DEFAULT);
	printf(" -s: the size of duplicated memory area (in MiB)\n");

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
	    numa_available() ? 0 :
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
	    numa_available() ? 0 :
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

static int get_next_mem_node(int node)
{

	long node_size;
	int mem_node = 0;
	int i, max_node = numa_max_node();

	for (i = node + 1; i <= max_node + node; i++) {
		mem_node = i % (max_node + 1);
		node_size = numa_node_size(mem_node, NULL);
		if (node_size > 0)
			break;
	}
	return mem_node;
}

static int get_first_mem_node(void)
{
	return get_next_mem_node(numa_max_node());
}

static int check_ksm_numa_merge(int mapping, int prot, int timeout, bool merge_across_nodes,
				size_t page_size)
{
	void *numa1_map_ptr, *numa2_map_ptr;
	struct timespec start_time;
	int page_count = 2;
	int first_node;

	if (clock_gettime(CLOCK_MONOTONIC_RAW, &start_time)) {
		perror("clock_gettime");
		return KSFT_FAIL;
	}

	if (numa_available() < 0) {
		perror("NUMA support not enabled");
		return KSFT_SKIP;
	}
	if (numa_num_configured_nodes() <= 1) {
		printf("At least 2 NUMA nodes must be available\n");
		return KSFT_SKIP;
	}
	if (ksm_write_sysfs(KSM_FP("merge_across_nodes"), merge_across_nodes))
		return KSFT_FAIL;

	/* allocate 2 pages in 2 different NUMA nodes and fill them with the same data */
	first_node = get_first_mem_node();
	numa1_map_ptr = numa_alloc_onnode(page_size, first_node);
	numa2_map_ptr = numa_alloc_onnode(page_size, get_next_mem_node(first_node));
	if (!numa1_map_ptr || !numa2_map_ptr) {
		perror("numa_alloc_onnode");
		return KSFT_FAIL;
	}

	memset(numa1_map_ptr, '*', page_size);
	memset(numa2_map_ptr, '*', page_size);

	/* try to merge the pages */
	if (ksm_merge_pages(numa1_map_ptr, page_size, start_time, timeout) ||
	    ksm_merge_pages(numa2_map_ptr, page_size, start_time, timeout))
		goto err_out;

       /*
	* verify that the right number of pages are merged:
	* 1) if merge_across_nodes was enabled, 2 duplicate pages will be merged;
	* 2) if merge_across_nodes = 0, there must be 0 merged pages, since there is
	*    only 1 unique page in each node and they can't be shared.
	*/
	if (merge_across_nodes && !assert_ksm_pages_count(page_count))
		goto err_out;
	else if (!merge_across_nodes && !assert_ksm_pages_count(0))
		goto err_out;

	numa_free(numa1_map_ptr, page_size);
	numa_free(numa2_map_ptr, page_size);
	printf("OK\n");
	return KSFT_PASS;

err_out:
	numa_free(numa1_map_ptr, page_size);
	numa_free(numa2_map_ptr, page_size);
	printf("Not OK\n");
	return KSFT_FAIL;
}

static int ksm_merge_hugepages_time(int mapping, int prot, int timeout, size_t map_size)
{
	void *map_ptr, *map_ptr_orig;
	struct timespec start_time, end_time;
	unsigned long scan_time_ns;
	int pagemap_fd, n_normal_pages, n_huge_pages;

	map_size *= MB;
	size_t len = map_size;

	len -= len % HPAGE_SIZE;
	map_ptr_orig = mmap(NULL, len + HPAGE_SIZE, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_NORESERVE | MAP_PRIVATE, -1, 0);
	map_ptr = map_ptr_orig + HPAGE_SIZE - (uintptr_t)map_ptr_orig % HPAGE_SIZE;

	if (map_ptr_orig == MAP_FAILED)
		err(2, "initial mmap");

	if (madvise(map_ptr, len + HPAGE_SIZE, MADV_HUGEPAGE))
		err(2, "MADV_HUGEPAGE");

	pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
	if (pagemap_fd < 0)
		err(2, "open pagemap");

	n_normal_pages = 0;
	n_huge_pages = 0;
	for (void *p = map_ptr; p < map_ptr + len; p += HPAGE_SIZE) {
		if (allocate_transhuge(p, pagemap_fd) < 0)
			n_normal_pages++;
		else
			n_huge_pages++;
	}
	printf("Number of normal pages:    %d\n", n_normal_pages);
	printf("Number of huge pages:    %d\n", n_huge_pages);

	memset(map_ptr, '*', len);

	if (clock_gettime(CLOCK_MONOTONIC_RAW, &start_time)) {
		perror("clock_gettime");
		goto err_out;
	}
	if (ksm_merge_pages(map_ptr, map_size, start_time, timeout))
		goto err_out;
	if (clock_gettime(CLOCK_MONOTONIC_RAW, &end_time)) {
		perror("clock_gettime");
		goto err_out;
	}

	scan_time_ns = (end_time.tv_sec - start_time.tv_sec) * NSEC_PER_SEC +
		       (end_time.tv_nsec - start_time.tv_nsec);

	printf("Total size:    %lu MiB\n", map_size / MB);
	printf("Total time:    %ld.%09ld s\n", scan_time_ns / NSEC_PER_SEC,
	       scan_time_ns % NSEC_PER_SEC);
	printf("Average speed:  %.3f MiB/s\n", (map_size / MB) /
					       ((double)scan_time_ns / NSEC_PER_SEC));

	munmap(map_ptr_orig, len + HPAGE_SIZE);
	return KSFT_PASS;

err_out:
	printf("Not OK\n");
	munmap(map_ptr_orig, len + HPAGE_SIZE);
	return KSFT_FAIL;
}

static int ksm_merge_time(int mapping, int prot, int timeout, size_t map_size)
{
	void *map_ptr;
	struct timespec start_time, end_time;
	unsigned long scan_time_ns;

	map_size *= MB;

	map_ptr = allocate_memory(NULL, prot, mapping, '*', map_size);
	if (!map_ptr)
		return KSFT_FAIL;

	if (clock_gettime(CLOCK_MONOTONIC_RAW, &start_time)) {
		perror("clock_gettime");
		goto err_out;
	}
	if (ksm_merge_pages(map_ptr, map_size, start_time, timeout))
		goto err_out;
	if (clock_gettime(CLOCK_MONOTONIC_RAW, &end_time)) {
		perror("clock_gettime");
		goto err_out;
	}

	scan_time_ns = (end_time.tv_sec - start_time.tv_sec) * NSEC_PER_SEC +
		       (end_time.tv_nsec - start_time.tv_nsec);

	printf("Total size:    %lu MiB\n", map_size / MB);
	printf("Total time:    %ld.%09ld s\n", scan_time_ns / NSEC_PER_SEC,
	       scan_time_ns % NSEC_PER_SEC);
	printf("Average speed:  %.3f MiB/s\n", (map_size / MB) /
					       ((double)scan_time_ns / NSEC_PER_SEC));

	munmap(map_ptr, map_size);
	return KSFT_PASS;

err_out:
	printf("Not OK\n");
	munmap(map_ptr, map_size);
	return KSFT_FAIL;
}

static int ksm_cow_time(int mapping, int prot, int timeout, size_t page_size)
{
	void *map_ptr;
	struct timespec start_time, end_time;
	unsigned long cow_time_ns;

	/* page_count must be less than 2*page_size */
	size_t page_count = 4000;

	map_ptr = allocate_memory(NULL, prot, mapping, '*', page_size * page_count);
	if (!map_ptr)
		return KSFT_FAIL;

	if (clock_gettime(CLOCK_MONOTONIC_RAW, &start_time)) {
		perror("clock_gettime");
		return KSFT_FAIL;
	}
	for (size_t i = 0; i < page_count - 1; i = i + 2)
		memset(map_ptr + page_size * i, '-', 1);
	if (clock_gettime(CLOCK_MONOTONIC_RAW, &end_time)) {
		perror("clock_gettime");
		return KSFT_FAIL;
	}

	cow_time_ns = (end_time.tv_sec - start_time.tv_sec) * NSEC_PER_SEC +
		       (end_time.tv_nsec - start_time.tv_nsec);

	printf("Total size:    %lu MiB\n\n", (page_size * page_count) / MB);
	printf("Not merged pages:\n");
	printf("Total time:     %ld.%09ld s\n", cow_time_ns / NSEC_PER_SEC,
	       cow_time_ns % NSEC_PER_SEC);
	printf("Average speed:  %.3f MiB/s\n\n", ((page_size * (page_count / 2)) / MB) /
					       ((double)cow_time_ns / NSEC_PER_SEC));

	/* Create 2000 pairs of duplicate pages */
	for (size_t i = 0; i < page_count - 1; i = i + 2) {
		memset(map_ptr + page_size * i, '+', i / 2 + 1);
		memset(map_ptr + page_size * (i + 1), '+', i / 2 + 1);
	}
	if (ksm_merge_pages(map_ptr, page_size * page_count, start_time, timeout))
		goto err_out;

	if (clock_gettime(CLOCK_MONOTONIC_RAW, &start_time)) {
		perror("clock_gettime");
		goto err_out;
	}
	for (size_t i = 0; i < page_count - 1; i = i + 2)
		memset(map_ptr + page_size * i, '-', 1);
	if (clock_gettime(CLOCK_MONOTONIC_RAW, &end_time)) {
		perror("clock_gettime");
		goto err_out;
	}

	cow_time_ns = (end_time.tv_sec - start_time.tv_sec) * NSEC_PER_SEC +
		       (end_time.tv_nsec - start_time.tv_nsec);

	printf("Merged pages:\n");
	printf("Total time:     %ld.%09ld s\n", cow_time_ns / NSEC_PER_SEC,
	       cow_time_ns % NSEC_PER_SEC);
	printf("Average speed:  %.3f MiB/s\n", ((page_size * (page_count / 2)) / MB) /
					       ((double)cow_time_ns / NSEC_PER_SEC));

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
	bool merge_across_nodes = KSM_MERGE_ACROSS_NODES_DEFAULT;
	long size_MB = 0;

	while ((opt = getopt(argc, argv, "ha:p:l:z:m:s:MUZNPCH")) != -1) {
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
		case 'm':
			if (strcmp(optarg, "0") == 0)
				merge_across_nodes = 0;
			else
				merge_across_nodes = 1;
			break;
		case 's':
			size_MB = atoi(optarg);
			if (size_MB <= 0) {
				printf("Size must be greater than 0\n");
				return KSFT_FAIL;
			}
		case 'M':
			break;
		case 'U':
			test_name = CHECK_KSM_UNMERGE;
			break;
		case 'Z':
			test_name = CHECK_KSM_ZERO_PAGE_MERGE;
			break;
		case 'N':
			test_name = CHECK_KSM_NUMA_MERGE;
			break;
		case 'P':
			test_name = KSM_MERGE_TIME;
			break;
		case 'H':
			test_name = KSM_MERGE_TIME_HUGE_PAGES;
			break;
		case 'C':
			test_name = KSM_COW_TIME;
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
	    numa_available() ? 0 :
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
	case CHECK_KSM_NUMA_MERGE:
		ret = check_ksm_numa_merge(MAP_PRIVATE | MAP_ANONYMOUS, prot, ksm_scan_limit_sec,
					   merge_across_nodes, page_size);
		break;
	case KSM_MERGE_TIME:
		if (size_MB == 0) {
			printf("Option '-s' is required.\n");
			return KSFT_FAIL;
		}
		ret = ksm_merge_time(MAP_PRIVATE | MAP_ANONYMOUS, prot, ksm_scan_limit_sec,
				     size_MB);
		break;
	case KSM_MERGE_TIME_HUGE_PAGES:
		if (size_MB == 0) {
			printf("Option '-s' is required.\n");
			return KSFT_FAIL;
		}
		ret = ksm_merge_hugepages_time(MAP_PRIVATE | MAP_ANONYMOUS, prot,
				ksm_scan_limit_sec, size_MB);
		break;
	case KSM_COW_TIME:
		ret = ksm_cow_time(MAP_PRIVATE | MAP_ANONYMOUS, prot, ksm_scan_limit_sec,
				   page_size);
		break;
	}

	if (ksm_restore(&ksm_sysfs_old)) {
		printf("Cannot restore default tunables\n");
		return KSFT_FAIL;
	}

	return ret;
}
