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
#define RUNS			(MT_TAG_COUNT * 2)
#define MTE_LAST_TAG_MASK	(0x7FFF)

static int verify_mte_pointer_validity(char *ptr, int mode)
{
	mte_initialize_current_context(mode, (uintptr_t)ptr, BUFFER_SIZE);
	/* Check the validity of the tagged pointer */
	memset(ptr, '1', BUFFER_SIZE);
	mte_wait_after_trig();
	if (cur_mte_cxt.fault_valid) {
		ksft_print_msg("Unexpected fault recorded for %p-%p in mode %x\n",
			       ptr, ptr + BUFFER_SIZE, mode);
		return KSFT_FAIL;
	}
	/* Proceed further for nonzero tags */
	if (!MT_FETCH_TAG((uintptr_t)ptr))
		return KSFT_PASS;
	mte_initialize_current_context(mode, (uintptr_t)ptr, BUFFER_SIZE + 1);
	/* Check the validity outside the range */
	ptr[BUFFER_SIZE] = '2';
	mte_wait_after_trig();
	if (!cur_mte_cxt.fault_valid) {
		ksft_print_msg("No valid fault recorded for %p in mode %x\n",
			       ptr, mode);
		return KSFT_FAIL;
	} else {
		return KSFT_PASS;
	}
}

static int check_single_included_tags(int mem_type, int mode)
{
	char *ptr;
	int tag, run, ret, result = KSFT_PASS;

	ptr = mte_allocate_memory(BUFFER_SIZE + MT_GRANULE_SIZE, mem_type, 0, false);
	if (check_allocated_memory(ptr, BUFFER_SIZE + MT_GRANULE_SIZE,
				   mem_type, false) != KSFT_PASS)
		return KSFT_FAIL;

	for (tag = 0; (tag < MT_TAG_COUNT) && (result == KSFT_PASS); tag++) {
		ret = mte_switch_mode(mode, MT_INCLUDE_VALID_TAG(tag));
		if (ret != 0)
			result = KSFT_FAIL;
		/* Try to catch a excluded tag by a number of tries. */
		for (run = 0; (run < RUNS) && (result == KSFT_PASS); run++) {
			ptr = mte_insert_tags(ptr, BUFFER_SIZE);
			/* Check tag value */
			if (MT_FETCH_TAG((uintptr_t)ptr) == tag) {
				ksft_print_msg("FAIL: wrong tag = 0x%x with include mask=0x%x\n",
					       MT_FETCH_TAG((uintptr_t)ptr),
					       MT_INCLUDE_VALID_TAG(tag));
				result = KSFT_FAIL;
				break;
			}
			result = verify_mte_pointer_validity(ptr, mode);
		}
	}
	mte_free_memory_tag_range(ptr, BUFFER_SIZE, mem_type, 0, MT_GRANULE_SIZE);
	return result;
}

static int check_multiple_included_tags(int mem_type, int mode)
{
	char *ptr;
	int tag, run, result = KSFT_PASS;
	unsigned long excl_mask = 0;

	ptr = mte_allocate_memory(BUFFER_SIZE + MT_GRANULE_SIZE, mem_type, 0, false);
	if (check_allocated_memory(ptr, BUFFER_SIZE + MT_GRANULE_SIZE,
				   mem_type, false) != KSFT_PASS)
		return KSFT_FAIL;

	for (tag = 0; (tag < MT_TAG_COUNT - 1) && (result == KSFT_PASS); tag++) {
		excl_mask |= 1 << tag;
		mte_switch_mode(mode, MT_INCLUDE_VALID_TAGS(excl_mask));
		/* Try to catch a excluded tag by a number of tries. */
		for (run = 0; (run < RUNS) && (result == KSFT_PASS); run++) {
			ptr = mte_insert_tags(ptr, BUFFER_SIZE);
			/* Check tag value */
			if (MT_FETCH_TAG((uintptr_t)ptr) < tag) {
				ksft_print_msg("FAIL: wrong tag = 0x%x with include mask=0x%x\n",
					       MT_FETCH_TAG((uintptr_t)ptr),
					       MT_INCLUDE_VALID_TAGS(excl_mask));
				result = KSFT_FAIL;
				break;
			}
			result = verify_mte_pointer_validity(ptr, mode);
		}
	}
	mte_free_memory_tag_range(ptr, BUFFER_SIZE, mem_type, 0, MT_GRANULE_SIZE);
	return result;
}

static int check_all_included_tags(int mem_type, int mode)
{
	char *ptr;
	int run, ret, result = KSFT_PASS;

	ptr = mte_allocate_memory(BUFFER_SIZE + MT_GRANULE_SIZE, mem_type, 0, false);
	if (check_allocated_memory(ptr, BUFFER_SIZE + MT_GRANULE_SIZE,
				   mem_type, false) != KSFT_PASS)
		return KSFT_FAIL;

	ret = mte_switch_mode(mode, MT_INCLUDE_TAG_MASK);
	if (ret != 0)
		return KSFT_FAIL;
	/* Try to catch a excluded tag by a number of tries. */
	for (run = 0; (run < RUNS) && (result == KSFT_PASS); run++) {
		ptr = (char *)mte_insert_tags(ptr, BUFFER_SIZE);
		/*
		 * Here tag byte can be between 0x0 to 0xF (full allowed range)
		 * so no need to match so just verify if it is writable.
		 */
		result = verify_mte_pointer_validity(ptr, mode);
	}
	mte_free_memory_tag_range(ptr, BUFFER_SIZE, mem_type, 0, MT_GRANULE_SIZE);
	return result;
}

static int check_none_included_tags(int mem_type, int mode)
{
	char *ptr;
	int run, ret;

	ptr = mte_allocate_memory(BUFFER_SIZE, mem_type, 0, false);
	if (check_allocated_memory(ptr, BUFFER_SIZE, mem_type, false) != KSFT_PASS)
		return KSFT_FAIL;

	ret = mte_switch_mode(mode, MT_EXCLUDE_TAG_MASK);
	if (ret != 0)
		return KSFT_FAIL;
	/* Try to catch a excluded tag by a number of tries. */
	for (run = 0; run < RUNS; run++) {
		ptr = (char *)mte_insert_tags(ptr, BUFFER_SIZE);
		/* Here all tags exluded so tag value generated should be 0 */
		if (MT_FETCH_TAG((uintptr_t)ptr)) {
			ksft_print_msg("FAIL: included tag value found\n");
			mte_free_memory((void *)ptr, BUFFER_SIZE, mem_type, true);
			return KSFT_FAIL;
		}
		mte_initialize_current_context(mode, (uintptr_t)ptr, BUFFER_SIZE);
		/* Check the write validity of the untagged pointer */
		memset(ptr, '1', BUFFER_SIZE);
		mte_wait_after_trig();
		if (cur_mte_cxt.fault_valid)
			break;
	}
	mte_free_memory(ptr, BUFFER_SIZE, mem_type, false);
	if (cur_mte_cxt.fault_valid)
		return KSFT_FAIL;
	else
		return KSFT_PASS;
}

int main(int argc, char *argv[])
{
	int err;

	err = mte_default_setup();
	if (err)
		return err;

	/* Register SIGSEGV handler */
	mte_register_signal(SIGSEGV, mte_default_handler);

	/* Set test plan */
	ksft_set_plan(4);

	evaluate_test(check_single_included_tags(USE_MMAP, MTE_SYNC_ERR),
		      "Check an included tag value with sync mode\n");
	evaluate_test(check_multiple_included_tags(USE_MMAP, MTE_SYNC_ERR),
		      "Check different included tags value with sync mode\n");
	evaluate_test(check_none_included_tags(USE_MMAP, MTE_SYNC_ERR),
		      "Check none included tags value with sync mode\n");
	evaluate_test(check_all_included_tags(USE_MMAP, MTE_SYNC_ERR),
		      "Check all included tags value with sync mode\n");

	mte_restore_setup();
	ksft_print_cnts();
	return ksft_get_fail_cnt() == 0 ? KSFT_PASS : KSFT_FAIL;
}
