// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2024 Ampere Computing LLC

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "kselftest.h"
#include "mte_common_util.h"
#include "mte_def.h"

#define TAG_CHECK_ON		0
#define TAG_CHECK_OFF		1

static unsigned long default_huge_page_size(void)
{
	unsigned long hps = 0;
	char *line = NULL;
	size_t linelen = 0;
	FILE *f = fopen("/proc/meminfo", "r");

	if (!f)
		return 0;
	while (getline(&line, &linelen, f) > 0) {
		if (sscanf(line, "Hugepagesize:       %lu kB", &hps) == 1) {
			hps <<= 10;
			break;
		}
	}

	free(line);
	fclose(f);
	return hps;
}

static bool is_hugetlb_allocated(void)
{
	unsigned long hps = 0;
	char *line = NULL;
	size_t linelen = 0;
	FILE *f = fopen("/proc/meminfo", "r");

	if (!f)
		return false;
	while (getline(&line, &linelen, f) > 0) {
		if (sscanf(line, "Hugetlb:       %lu kB", &hps) == 1) {
			hps <<= 10;
			break;
		}
	}

	free(line);
	fclose(f);

	if (hps > 0)
		return true;

	return false;
}

static void write_sysfs(char *str, unsigned long val)
{
	FILE *f;

	f = fopen(str, "w");
	if (!f) {
		ksft_print_msg("ERR: missing %s\n", str);
		return;
	}
	fprintf(f, "%lu", val);
	fclose(f);
}

static void allocate_hugetlb()
{
	write_sysfs("/proc/sys/vm/nr_hugepages", 2);
}

static void free_hugetlb()
{
	write_sysfs("/proc/sys/vm/nr_hugepages", 0);
}

static int check_child_tag_inheritance(char *ptr, int size, int mode)
{
	int i, parent_tag, child_tag, fault, child_status;
	pid_t child;

	parent_tag = MT_FETCH_TAG((uintptr_t)ptr);
	fault = 0;

	child = fork();
	if (child == -1) {
		ksft_print_msg("FAIL: child process creation\n");
		return KSFT_FAIL;
	} else if (child == 0) {
		mte_initialize_current_context(mode, (uintptr_t)ptr, size);
		/* Do copy on write */
		memset(ptr, '1', size);
		mte_wait_after_trig();
		if (cur_mte_cxt.fault_valid == true) {
			fault = 1;
			goto check_child_tag_inheritance_err;
		}
		for (i = 0; i < size; i += MT_GRANULE_SIZE) {
			child_tag = MT_FETCH_TAG((uintptr_t)(mte_get_tag_address(ptr + i)));
			if (parent_tag != child_tag) {
				ksft_print_msg("FAIL: child mte tag (%d) mismatch\n", i);
				fault = 1;
				goto check_child_tag_inheritance_err;
			}
		}
check_child_tag_inheritance_err:
		_exit(fault);
	}
	/* Wait for child process to terminate */
	wait(&child_status);
	if (WIFEXITED(child_status))
		fault = WEXITSTATUS(child_status);
	else
		fault = 1;
	return (fault) ? KSFT_FAIL : KSFT_PASS;
}

static int check_mte_memory(char *ptr, int size, int mode, int tag_check)
{
	mte_initialize_current_context(mode, (uintptr_t)ptr, size);
	memset(ptr, '1', size);
	mte_wait_after_trig();
	if (cur_mte_cxt.fault_valid == true)
		return KSFT_FAIL;

	return KSFT_PASS;
}

static int check_hugetlb_memory_mapping(int mem_type, int mode, int mapping, int tag_check)
{
	char *ptr, *map_ptr;
	int result;
	unsigned long map_size;

	map_size = default_huge_page_size();

	mte_switch_mode(mode, MTE_ALLOW_NON_ZERO_TAG, false);
	map_ptr = (char *)mte_allocate_memory(map_size, mem_type, mapping, false);
	if (check_allocated_memory(map_ptr, map_size, mem_type, false) != KSFT_PASS)
		return KSFT_FAIL;

	mte_initialize_current_context(mode, (uintptr_t)map_ptr, map_size);
	/* Only mte enabled memory will allow tag insertion */
	ptr = mte_insert_tags((void *)map_ptr, map_size);
	if (!ptr || cur_mte_cxt.fault_valid == true) {
		ksft_print_msg("FAIL: Insert tags on anonymous mmap memory\n");
		munmap((void *)map_ptr, map_size);
		return KSFT_FAIL;
	}
	result = check_mte_memory(ptr, map_size, mode, tag_check);
	mte_clear_tags((void *)ptr, map_size);
	mte_free_memory((void *)map_ptr, map_size, mem_type, false);
	if (result == KSFT_FAIL)
		return KSFT_FAIL;

	return KSFT_PASS;
}

static int check_clear_prot_mte_flag(int mem_type, int mode, int mapping)
{
	char *map_ptr;
	int prot_flag, result;
	unsigned long map_size;

	prot_flag = PROT_READ | PROT_WRITE;
	mte_switch_mode(mode, MTE_ALLOW_NON_ZERO_TAG, false);
	map_size = default_huge_page_size();
	map_ptr = (char *)mte_allocate_memory_tag_range(map_size, mem_type, mapping,
							0, 0);
	if (check_allocated_memory_range(map_ptr, map_size, mem_type,
					 0, 0) != KSFT_PASS)
		return KSFT_FAIL;
	/* Try to clear PROT_MTE property and verify it by tag checking */
	if (mprotect(map_ptr, map_size, prot_flag)) {
		mte_free_memory_tag_range((void *)map_ptr, map_size, mem_type,
					  0, 0);
		ksft_print_msg("FAIL: mprotect not ignoring clear PROT_MTE property\n");
		return KSFT_FAIL;
	}
	result = check_mte_memory(map_ptr, map_size, mode, TAG_CHECK_ON);
	mte_free_memory_tag_range((void *)map_ptr, map_size, mem_type, 0, 0);
	if (result != KSFT_PASS)
		return KSFT_FAIL;

	return KSFT_PASS;
}

static int check_child_hugetlb_memory_mapping(int mem_type, int mode, int mapping)
{
	char *ptr;
	int result;
	unsigned long map_size;

	map_size = default_huge_page_size();

	mte_switch_mode(mode, MTE_ALLOW_NON_ZERO_TAG, false);
	ptr = (char *)mte_allocate_memory_tag_range(map_size, mem_type, mapping,
						    0, 0);
	if (check_allocated_memory_range(ptr, map_size, mem_type,
					 0, 0) != KSFT_PASS)
		return KSFT_FAIL;
	result = check_child_tag_inheritance(ptr, map_size, mode);
	mte_free_memory_tag_range((void *)ptr, map_size, mem_type, 0, 0);
	if (result == KSFT_FAIL)
		return result;

	return KSFT_PASS;
}

int main(int argc, char *argv[])
{
	int err;
	void *map_ptr;
	unsigned long map_size;

	err = mte_default_setup();
	if (err)
		return err;

	/* Register signal handlers */
	mte_register_signal(SIGBUS, mte_default_handler, false);
	mte_register_signal(SIGSEGV, mte_default_handler, false);

	allocate_hugetlb();

	if (!is_hugetlb_allocated()) {
		ksft_print_msg("ERR: Unable allocate hugetlb pages\n");
		return KSFT_FAIL;
	}

	/* Check if MTE supports hugetlb mappings */
	map_size = default_huge_page_size();
	map_ptr = mmap(NULL, map_size, PROT_READ | PROT_MTE,
		       MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
	if (map_ptr == MAP_FAILED)
		ksft_exit_skip("PROT_MTE not supported with MAP_HUGETLB mappings\n");
	else
		munmap(map_ptr, map_size);

	/* Set test plan */
	ksft_set_plan(12);

	mte_enable_pstate_tco();

	evaluate_test(check_hugetlb_memory_mapping(USE_MMAP, MTE_SYNC_ERR, MAP_PRIVATE | MAP_HUGETLB, TAG_CHECK_OFF),
	"Check hugetlb memory with private mapping, sync error mode, mmap memory and tag check off\n");

	mte_disable_pstate_tco();
	evaluate_test(check_hugetlb_memory_mapping(USE_MMAP, MTE_NONE_ERR, MAP_PRIVATE | MAP_HUGETLB, TAG_CHECK_OFF),
	"Check hugetlb memory with private mapping, no error mode, mmap memory and tag check off\n");

	evaluate_test(check_hugetlb_memory_mapping(USE_MMAP, MTE_SYNC_ERR, MAP_PRIVATE | MAP_HUGETLB, TAG_CHECK_ON),
	"Check hugetlb memory with private mapping, sync error mode, mmap memory and tag check on\n");
	evaluate_test(check_hugetlb_memory_mapping(USE_MPROTECT, MTE_SYNC_ERR, MAP_PRIVATE | MAP_HUGETLB, TAG_CHECK_ON),
	"Check hugetlb memory with private mapping, sync error mode, mmap/mprotect memory and tag check on\n");
	evaluate_test(check_hugetlb_memory_mapping(USE_MMAP, MTE_ASYNC_ERR, MAP_PRIVATE | MAP_HUGETLB, TAG_CHECK_ON),
	"Check hugetlb memory with private mapping, async error mode, mmap memory and tag check on\n");
	evaluate_test(check_hugetlb_memory_mapping(USE_MPROTECT, MTE_ASYNC_ERR, MAP_PRIVATE | MAP_HUGETLB, TAG_CHECK_ON),
	"Check hugetlb memory with private mapping, async error mode, mmap/mprotect memory and tag check on\n");

	evaluate_test(check_clear_prot_mte_flag(USE_MMAP, MTE_SYNC_ERR, MAP_PRIVATE | MAP_HUGETLB),
	"Check clear PROT_MTE flags with private mapping, sync error mode and mmap memory\n");
	evaluate_test(check_clear_prot_mte_flag(USE_MPROTECT, MTE_SYNC_ERR, MAP_PRIVATE | MAP_HUGETLB),
	"Check clear PROT_MTE flags with private mapping and sync error mode and mmap/mprotect memory\n");

	evaluate_test(check_child_hugetlb_memory_mapping(USE_MMAP, MTE_SYNC_ERR, MAP_PRIVATE | MAP_HUGETLB),
		"Check child hugetlb memory with private mapping, sync error mode and mmap memory\n");
	evaluate_test(check_child_hugetlb_memory_mapping(USE_MMAP, MTE_ASYNC_ERR, MAP_PRIVATE | MAP_HUGETLB),
		"Check child hugetlb memory with private mapping, async error mode and mmap memory\n");
	evaluate_test(check_child_hugetlb_memory_mapping(USE_MPROTECT, MTE_SYNC_ERR, MAP_PRIVATE | MAP_HUGETLB),
		"Check child hugetlb memory with private mapping, sync error mode and mmap/mprotect memory\n");
	evaluate_test(check_child_hugetlb_memory_mapping(USE_MPROTECT, MTE_ASYNC_ERR, MAP_PRIVATE | MAP_HUGETLB),
		"Check child hugetlb memory with private mapping, async error mode and mmap/mprotect memory\n");

	mte_restore_setup();
	free_hugetlb();
	ksft_print_cnts();
	return ksft_get_fail_cnt() == 0 ? KSFT_PASS : KSFT_FAIL;
}
