// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 ARM Limited

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

#include "kselftest.h"
#include "mte_common_util.h"
#include "mte_def.h"

#define RUNS			(MT_TAG_COUNT)
#define UNDERFLOW		MT_GRANULE_SIZE
#define OVERFLOW		MT_GRANULE_SIZE
#define TAG_CHECK_ON		0
#define TAG_CHECK_OFF		1

static size_t page_size;
static int sizes[] = {
	1, 537, 989, 1269, MT_GRANULE_SIZE - 1, MT_GRANULE_SIZE,
	/* page size - 1*/ 0, /* page_size */ 0, /* page size + 1 */ 0
};

static int check_mte_memory(char *ptr, int size, int mode, int tag_check)
{
	mte_initialize_current_context(mode, (uintptr_t)ptr, size);
	memset(ptr, '1', size);
	mte_wait_after_trig();
	if (cur_mte_cxt.fault_valid == true)
		return KSFT_FAIL;

	mte_initialize_current_context(mode, (uintptr_t)ptr, -UNDERFLOW);
	memset(ptr - UNDERFLOW, '2', UNDERFLOW);
	mte_wait_after_trig();
	if (cur_mte_cxt.fault_valid == false && tag_check == TAG_CHECK_ON)
		return KSFT_FAIL;
	if (cur_mte_cxt.fault_valid == true && tag_check == TAG_CHECK_OFF)
		return KSFT_FAIL;

	mte_initialize_current_context(mode, (uintptr_t)ptr, size + OVERFLOW);
	memset(ptr + size, '3', OVERFLOW);
	mte_wait_after_trig();
	if (cur_mte_cxt.fault_valid == false && tag_check == TAG_CHECK_ON)
		return KSFT_FAIL;
	if (cur_mte_cxt.fault_valid == true && tag_check == TAG_CHECK_OFF)
		return KSFT_FAIL;

	return KSFT_PASS;
}

static int check_anonymous_memory_mapping(int mem_type, int mode, int mapping, int tag_check)
{
	char *ptr, *map_ptr;
	int run, result, map_size;
	int item = sizeof(sizes)/sizeof(int);

	item = sizeof(sizes)/sizeof(int);
	mte_switch_mode(mode, MTE_ALLOW_NON_ZERO_TAG);
	for (run = 0; run < item; run++) {
		map_size = sizes[run] + OVERFLOW + UNDERFLOW;
		map_ptr = (char *)mte_allocate_memory(map_size, mem_type, mapping, false);
		if (check_allocated_memory(map_ptr, map_size, mem_type, false) != KSFT_PASS)
			return KSFT_FAIL;

		ptr = map_ptr + UNDERFLOW;
		mte_initialize_current_context(mode, (uintptr_t)ptr, sizes[run]);
		/* Only mte enabled memory will allow tag insertion */
		ptr = mte_insert_tags((void *)ptr, sizes[run]);
		if (!ptr || cur_mte_cxt.fault_valid == true) {
			ksft_print_msg("FAIL: Insert tags on anonymous mmap memory\n");
			munmap((void *)map_ptr, map_size);
			return KSFT_FAIL;
		}
		result = check_mte_memory(ptr, sizes[run], mode, tag_check);
		mte_clear_tags((void *)ptr, sizes[run]);
		mte_free_memory((void *)map_ptr, map_size, mem_type, false);
		if (result == KSFT_FAIL)
			return KSFT_FAIL;
	}
	return KSFT_PASS;
}

static int check_file_memory_mapping(int mem_type, int mode, int mapping, int tag_check)
{
	char *ptr, *map_ptr;
	int run, fd, map_size;
	int total = sizeof(sizes)/sizeof(int);
	int result = KSFT_PASS;

	mte_switch_mode(mode, MTE_ALLOW_NON_ZERO_TAG);
	for (run = 0; run < total; run++) {
		fd = create_temp_file();
		if (fd == -1)
			return KSFT_FAIL;

		map_size = sizes[run] + UNDERFLOW + OVERFLOW;
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
		result = check_mte_memory(ptr, sizes[run], mode, tag_check);
		mte_clear_tags((void *)ptr, sizes[run]);
		munmap((void *)map_ptr, map_size);
		close(fd);
		if (result == KSFT_FAIL)
			break;
	}
	return result;
}

static int check_clear_prot_mte_flag(int mem_type, int mode, int mapping)
{
	char *ptr, *map_ptr;
	int run, prot_flag, result, fd, map_size;
	int total = sizeof(sizes)/sizeof(int);

	prot_flag = PROT_READ | PROT_WRITE;
	mte_switch_mode(mode, MTE_ALLOW_NON_ZERO_TAG);
	for (run = 0; run < total; run++) {
		map_size = sizes[run] + OVERFLOW + UNDERFLOW;
		ptr = (char *)mte_allocate_memory_tag_range(sizes[run], mem_type, mapping,
							    UNDERFLOW, OVERFLOW);
		if (check_allocated_memory_range(ptr, sizes[run], mem_type,
						 UNDERFLOW, OVERFLOW) != KSFT_PASS)
			return KSFT_FAIL;
		map_ptr = ptr - UNDERFLOW;
		/* Try to clear PROT_MTE property and verify it by tag checking */
		if (mprotect(map_ptr, map_size, prot_flag)) {
			mte_free_memory_tag_range((void *)ptr, sizes[run], mem_type,
						  UNDERFLOW, OVERFLOW);
			ksft_print_msg("FAIL: mprotect not ignoring clear PROT_MTE property\n");
			return KSFT_FAIL;
		}
		result = check_mte_memory(ptr, sizes[run], mode, TAG_CHECK_ON);
		mte_free_memory_tag_range((void *)ptr, sizes[run], mem_type, UNDERFLOW, OVERFLOW);
		if (result != KSFT_PASS)
			return KSFT_FAIL;

		fd = create_temp_file();
		if (fd == -1)
			return KSFT_FAIL;
		ptr = (char *)mte_allocate_file_memory_tag_range(sizes[run], mem_type, mapping,
								 UNDERFLOW, OVERFLOW, fd);
		if (check_allocated_memory_range(ptr, sizes[run], mem_type,
						 UNDERFLOW, OVERFLOW) != KSFT_PASS) {
			close(fd);
			return KSFT_FAIL;
		}
		map_ptr = ptr - UNDERFLOW;
		/* Try to clear PROT_MTE property and verify it by tag checking */
		if (mprotect(map_ptr, map_size, prot_flag)) {
			ksft_print_msg("FAIL: mprotect not ignoring clear PROT_MTE property\n");
			mte_free_memory_tag_range((void *)ptr, sizes[run], mem_type,
						  UNDERFLOW, OVERFLOW);
			close(fd);
			return KSFT_FAIL;
		}
		result = check_mte_memory(ptr, sizes[run], mode, TAG_CHECK_ON);
		mte_free_memory_tag_range((void *)ptr, sizes[run], mem_type, UNDERFLOW, OVERFLOW);
		close(fd);
		if (result != KSFT_PASS)
			return KSFT_FAIL;
	}
	return KSFT_PASS;
}

int main(int argc, char *argv[])
{
	int err;
	int item = sizeof(sizes)/sizeof(int);

	err = mte_default_setup();
	if (err)
		return err;
	page_size = getpagesize();
	if (!page_size) {
		ksft_print_msg("ERR: Unable to get page size\n");
		return KSFT_FAIL;
	}
	sizes[item - 3] = page_size - 1;
	sizes[item - 2] = page_size;
	sizes[item - 1] = page_size + 1;

	/* Register signal handlers */
	mte_register_signal(SIGBUS, mte_default_handler);
	mte_register_signal(SIGSEGV, mte_default_handler);

	/* Set test plan */
	ksft_set_plan(22);

	mte_enable_pstate_tco();

	evaluate_test(check_anonymous_memory_mapping(USE_MMAP, MTE_SYNC_ERR, MAP_PRIVATE, TAG_CHECK_OFF),
	"Check anonymous memory with private mapping, sync error mode, mmap memory and tag check off\n");
	evaluate_test(check_file_memory_mapping(USE_MPROTECT, MTE_SYNC_ERR, MAP_PRIVATE, TAG_CHECK_OFF),
	"Check file memory with private mapping, sync error mode, mmap/mprotect memory and tag check off\n");

	mte_disable_pstate_tco();
	evaluate_test(check_anonymous_memory_mapping(USE_MMAP, MTE_NONE_ERR, MAP_PRIVATE, TAG_CHECK_OFF),
	"Check anonymous memory with private mapping, no error mode, mmap memory and tag check off\n");
	evaluate_test(check_file_memory_mapping(USE_MPROTECT, MTE_NONE_ERR, MAP_PRIVATE, TAG_CHECK_OFF),
	"Check file memory with private mapping, no error mode, mmap/mprotect memory and tag check off\n");

	evaluate_test(check_anonymous_memory_mapping(USE_MMAP, MTE_SYNC_ERR, MAP_PRIVATE, TAG_CHECK_ON),
	"Check anonymous memory with private mapping, sync error mode, mmap memory and tag check on\n");
	evaluate_test(check_anonymous_memory_mapping(USE_MPROTECT, MTE_SYNC_ERR, MAP_PRIVATE, TAG_CHECK_ON),
	"Check anonymous memory with private mapping, sync error mode, mmap/mprotect memory and tag check on\n");
	evaluate_test(check_anonymous_memory_mapping(USE_MMAP, MTE_SYNC_ERR, MAP_SHARED, TAG_CHECK_ON),
	"Check anonymous memory with shared mapping, sync error mode, mmap memory and tag check on\n");
	evaluate_test(check_anonymous_memory_mapping(USE_MPROTECT, MTE_SYNC_ERR, MAP_SHARED, TAG_CHECK_ON),
	"Check anonymous memory with shared mapping, sync error mode, mmap/mprotect memory and tag check on\n");
	evaluate_test(check_anonymous_memory_mapping(USE_MMAP, MTE_ASYNC_ERR, MAP_PRIVATE, TAG_CHECK_ON),
	"Check anonymous memory with private mapping, async error mode, mmap memory and tag check on\n");
	evaluate_test(check_anonymous_memory_mapping(USE_MPROTECT, MTE_ASYNC_ERR, MAP_PRIVATE, TAG_CHECK_ON),
	"Check anonymous memory with private mapping, async error mode, mmap/mprotect memory and tag check on\n");
	evaluate_test(check_anonymous_memory_mapping(USE_MMAP, MTE_ASYNC_ERR, MAP_SHARED, TAG_CHECK_ON),
	"Check anonymous memory with shared mapping, async error mode, mmap memory and tag check on\n");
	evaluate_test(check_anonymous_memory_mapping(USE_MPROTECT, MTE_ASYNC_ERR, MAP_SHARED, TAG_CHECK_ON),
	"Check anonymous memory with shared mapping, async error mode, mmap/mprotect memory and tag check on\n");

	evaluate_test(check_file_memory_mapping(USE_MMAP, MTE_SYNC_ERR, MAP_PRIVATE, TAG_CHECK_ON),
	"Check file memory with private mapping, sync error mode, mmap memory and tag check on\n");
	evaluate_test(check_file_memory_mapping(USE_MPROTECT, MTE_SYNC_ERR, MAP_PRIVATE, TAG_CHECK_ON),
	"Check file memory with private mapping, sync error mode, mmap/mprotect memory and tag check on\n");
	evaluate_test(check_file_memory_mapping(USE_MMAP, MTE_SYNC_ERR, MAP_SHARED, TAG_CHECK_ON),
	"Check file memory with shared mapping, sync error mode, mmap memory and tag check on\n");
	evaluate_test(check_file_memory_mapping(USE_MPROTECT, MTE_SYNC_ERR, MAP_SHARED, TAG_CHECK_ON),
	"Check file memory with shared mapping, sync error mode, mmap/mprotect memory and tag check on\n");
	evaluate_test(check_file_memory_mapping(USE_MMAP, MTE_ASYNC_ERR, MAP_PRIVATE, TAG_CHECK_ON),
	"Check file memory with private mapping, async error mode, mmap memory and tag check on\n");
	evaluate_test(check_file_memory_mapping(USE_MPROTECT, MTE_ASYNC_ERR, MAP_PRIVATE, TAG_CHECK_ON),
	"Check file memory with private mapping, async error mode, mmap/mprotect memory and tag check on\n");
	evaluate_test(check_file_memory_mapping(USE_MMAP, MTE_ASYNC_ERR, MAP_SHARED, TAG_CHECK_ON),
	"Check file memory with shared mapping, async error mode, mmap memory and tag check on\n");
	evaluate_test(check_file_memory_mapping(USE_MPROTECT, MTE_ASYNC_ERR, MAP_SHARED, TAG_CHECK_ON),
	"Check file memory with shared mapping, async error mode, mmap/mprotect memory and tag check on\n");

	evaluate_test(check_clear_prot_mte_flag(USE_MMAP, MTE_SYNC_ERR, MAP_PRIVATE),
	"Check clear PROT_MTE flags with private mapping, sync error mode and mmap memory\n");
	evaluate_test(check_clear_prot_mte_flag(USE_MPROTECT, MTE_SYNC_ERR, MAP_PRIVATE),
	"Check clear PROT_MTE flags with private mapping and sync error mode and mmap/mprotect memory\n");

	mte_restore_setup();
	ksft_print_cnts();
	return ksft_get_fail_cnt() == 0 ? KSFT_PASS : KSFT_FAIL;
}
