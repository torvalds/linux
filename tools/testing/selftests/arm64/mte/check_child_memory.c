// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 ARM Limited

#define _GNU_SOURCE

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <sys/wait.h>

#include "kselftest.h"
#include "mte_common_util.h"
#include "mte_def.h"

#define BUFFER_SIZE		(5 * MT_GRANULE_SIZE)
#define RUNS			(MT_TAG_COUNT)
#define UNDERFLOW		MT_GRANULE_SIZE
#define OVERFLOW		MT_GRANULE_SIZE

static size_t page_size;
static int sizes[] = {
	1, 537, 989, 1269, MT_GRANULE_SIZE - 1, MT_GRANULE_SIZE,
	/* page size - 1*/ 0, /* page_size */ 0, /* page size + 1 */ 0
};

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
		for (i = 0 ; i < size ; i += MT_GRANULE_SIZE) {
			child_tag = MT_FETCH_TAG((uintptr_t)(mte_get_tag_address(ptr + i)));
			if (parent_tag != child_tag) {
				ksft_print_msg("FAIL: child mte tag mismatch\n");
				fault = 1;
				goto check_child_tag_inheritance_err;
			}
		}
		mte_initialize_current_context(mode, (uintptr_t)ptr, -UNDERFLOW);
		memset(ptr - UNDERFLOW, '2', UNDERFLOW);
		mte_wait_after_trig();
		if (cur_mte_cxt.fault_valid == false) {
			fault = 1;
			goto check_child_tag_inheritance_err;
		}
		mte_initialize_current_context(mode, (uintptr_t)ptr, size + OVERFLOW);
		memset(ptr + size, '3', OVERFLOW);
		mte_wait_after_trig();
		if (cur_mte_cxt.fault_valid == false) {
			fault = 1;
			goto check_child_tag_inheritance_err;
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

static int check_child_memory_mapping(int mem_type, int mode, int mapping)
{
	char *ptr;
	int run, result;
	int item = ARRAY_SIZE(sizes);

	item = ARRAY_SIZE(sizes);
	mte_switch_mode(mode, MTE_ALLOW_NON_ZERO_TAG);
	for (run = 0; run < item; run++) {
		ptr = (char *)mte_allocate_memory_tag_range(sizes[run], mem_type, mapping,
							    UNDERFLOW, OVERFLOW);
		if (check_allocated_memory_range(ptr, sizes[run], mem_type,
						 UNDERFLOW, OVERFLOW) != KSFT_PASS)
			return KSFT_FAIL;
		result = check_child_tag_inheritance(ptr, sizes[run], mode);
		mte_free_memory_tag_range((void *)ptr, sizes[run], mem_type, UNDERFLOW, OVERFLOW);
		if (result == KSFT_FAIL)
			return result;
	}
	return KSFT_PASS;
}

static int check_child_file_mapping(int mem_type, int mode, int mapping)
{
	char *ptr, *map_ptr;
	int run, fd, map_size, result = KSFT_PASS;
	int total = ARRAY_SIZE(sizes);

	mte_switch_mode(mode, MTE_ALLOW_NON_ZERO_TAG);
	for (run = 0; run < total; run++) {
		fd = create_temp_file();
		if (fd == -1)
			return KSFT_FAIL;

		map_size = sizes[run] + OVERFLOW + UNDERFLOW;
		map_ptr = (char *)mte_allocate_file_memory(map_size, mem_type, mapping, false, fd);
		if (check_allocated_memory(map_ptr, map_size, mem_type, false) != KSFT_PASS) {
			close(fd);
			return KSFT_FAIL;
		}
		ptr = map_ptr + UNDERFLOW;
		mte_initialize_current_context(mode, (uintptr_t)ptr, sizes[run]);
		/* Only mte enabled memory will allow tag insertion */
		ptr = mte_insert_tags((void *)ptr, sizes[run]);
		if (!ptr || cur_mte_cxt.fault_valid == true) {
			ksft_print_msg("FAIL: Insert tags on file based memory\n");
			munmap((void *)map_ptr, map_size);
			close(fd);
			return KSFT_FAIL;
		}
		result = check_child_tag_inheritance(ptr, sizes[run], mode);
		mte_clear_tags((void *)ptr, sizes[run]);
		munmap((void *)map_ptr, map_size);
		close(fd);
		if (result != KSFT_PASS)
			return KSFT_FAIL;
	}
	return KSFT_PASS;
}

int main(int argc, char *argv[])
{
	int err;
	int item = ARRAY_SIZE(sizes);

	page_size = getpagesize();
	if (!page_size) {
		ksft_print_msg("ERR: Unable to get page size\n");
		return KSFT_FAIL;
	}
	sizes[item - 3] = page_size - 1;
	sizes[item - 2] = page_size;
	sizes[item - 1] = page_size + 1;

	err = mte_default_setup();
	if (err)
		return err;

	/* Register SIGSEGV handler */
	mte_register_signal(SIGSEGV, mte_default_handler);
	mte_register_signal(SIGBUS, mte_default_handler);

	/* Set test plan */
	ksft_set_plan(12);

	evaluate_test(check_child_memory_mapping(USE_MMAP, MTE_SYNC_ERR, MAP_PRIVATE),
		"Check child anonymous memory with private mapping, precise mode and mmap memory\n");
	evaluate_test(check_child_memory_mapping(USE_MMAP, MTE_SYNC_ERR, MAP_SHARED),
		"Check child anonymous memory with shared mapping, precise mode and mmap memory\n");
	evaluate_test(check_child_memory_mapping(USE_MMAP, MTE_ASYNC_ERR, MAP_PRIVATE),
		"Check child anonymous memory with private mapping, imprecise mode and mmap memory\n");
	evaluate_test(check_child_memory_mapping(USE_MMAP, MTE_ASYNC_ERR, MAP_SHARED),
		"Check child anonymous memory with shared mapping, imprecise mode and mmap memory\n");
	evaluate_test(check_child_memory_mapping(USE_MPROTECT, MTE_SYNC_ERR, MAP_PRIVATE),
		"Check child anonymous memory with private mapping, precise mode and mmap/mprotect memory\n");
	evaluate_test(check_child_memory_mapping(USE_MPROTECT, MTE_SYNC_ERR, MAP_SHARED),
		"Check child anonymous memory with shared mapping, precise mode and mmap/mprotect memory\n");

	evaluate_test(check_child_file_mapping(USE_MMAP, MTE_SYNC_ERR, MAP_PRIVATE),
		"Check child file memory with private mapping, precise mode and mmap memory\n");
	evaluate_test(check_child_file_mapping(USE_MMAP, MTE_SYNC_ERR, MAP_SHARED),
		"Check child file memory with shared mapping, precise mode and mmap memory\n");
	evaluate_test(check_child_memory_mapping(USE_MMAP, MTE_ASYNC_ERR, MAP_PRIVATE),
		"Check child file memory with private mapping, imprecise mode and mmap memory\n");
	evaluate_test(check_child_memory_mapping(USE_MMAP, MTE_ASYNC_ERR, MAP_SHARED),
		"Check child file memory with shared mapping, imprecise mode and mmap memory\n");
	evaluate_test(check_child_memory_mapping(USE_MPROTECT, MTE_SYNC_ERR, MAP_PRIVATE),
		"Check child file memory with private mapping, precise mode and mmap/mprotect memory\n");
	evaluate_test(check_child_memory_mapping(USE_MPROTECT, MTE_SYNC_ERR, MAP_SHARED),
		"Check child file memory with shared mapping, precise mode and mmap/mprotect memory\n");

	mte_restore_setup();
	ksft_print_cnts();
	return ksft_get_fail_cnt() == 0 ? KSFT_PASS : KSFT_FAIL;
}
