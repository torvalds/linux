// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 ARM Limited

#define _GNU_SOURCE

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "kselftest.h"
#include "mte_common_util.h"
#include "mte_def.h"

#define OVERFLOW_RANGE MT_GRANULE_SIZE

static int sizes[] = {
	1, 555, 1033, MT_GRANULE_SIZE - 1, MT_GRANULE_SIZE,
	/* page size - 1*/ 0, /* page_size */ 0, /* page size + 1 */ 0
};

enum mte_block_test_alloc {
	UNTAGGED_TAGGED,
	TAGGED_UNTAGGED,
	TAGGED_TAGGED,
	BLOCK_ALLOC_MAX,
};

static int check_buffer_by_byte(int mem_type, int mode)
{
	char *ptr;
	int i, j, item;
	bool err;

	mte_switch_mode(mode, MTE_ALLOW_NON_ZERO_TAG);
	item = ARRAY_SIZE(sizes);

	for (i = 0; i < item; i++) {
		ptr = (char *)mte_allocate_memory(sizes[i], mem_type, 0, true);
		if (check_allocated_memory(ptr, sizes[i], mem_type, true) != KSFT_PASS)
			return KSFT_FAIL;
		mte_initialize_current_context(mode, (uintptr_t)ptr, sizes[i]);
		/* Set some value in tagged memory */
		for (j = 0; j < sizes[i]; j++)
			ptr[j] = '1';
		mte_wait_after_trig();
		err = cur_mte_cxt.fault_valid;
		/* Check the buffer whether it is filled. */
		for (j = 0; j < sizes[i] && !err; j++) {
			if (ptr[j] != '1')
				err = true;
		}
		mte_free_memory((void *)ptr, sizes[i], mem_type, true);

		if (err)
			break;
	}
	if (!err)
		return KSFT_PASS;
	else
		return KSFT_FAIL;
}

static int check_buffer_underflow_by_byte(int mem_type, int mode,
					  int underflow_range)
{
	char *ptr;
	int i, j, item, last_index;
	bool err;
	char *und_ptr = NULL;

	mte_switch_mode(mode, MTE_ALLOW_NON_ZERO_TAG);
	item = ARRAY_SIZE(sizes);
	for (i = 0; i < item; i++) {
		ptr = (char *)mte_allocate_memory_tag_range(sizes[i], mem_type, 0,
							    underflow_range, 0);
		if (check_allocated_memory_range(ptr, sizes[i], mem_type,
					       underflow_range, 0) != KSFT_PASS)
			return KSFT_FAIL;

		mte_initialize_current_context(mode, (uintptr_t)ptr, -underflow_range);
		last_index = 0;
		/* Set some value in tagged memory and make the buffer underflow */
		for (j = sizes[i] - 1; (j >= -underflow_range) &&
				       (!cur_mte_cxt.fault_valid); j--) {
			ptr[j] = '1';
			last_index = j;
		}
		mte_wait_after_trig();
		err = false;
		/* Check whether the buffer is filled */
		for (j = 0; j < sizes[i]; j++) {
			if (ptr[j] != '1') {
				err = true;
				ksft_print_msg("Buffer is not filled at index:%d of ptr:0x%p\n",
						j, ptr);
				break;
			}
		}
		if (err)
			goto check_buffer_underflow_by_byte_err;

		switch (mode) {
		case MTE_NONE_ERR:
			if (cur_mte_cxt.fault_valid == true || last_index != -underflow_range) {
				err = true;
				break;
			}
			/* There were no fault so the underflow area should be filled */
			und_ptr = (char *) MT_CLEAR_TAG((size_t) ptr - underflow_range);
			for (j = 0 ; j < underflow_range; j++) {
				if (und_ptr[j] != '1') {
					err = true;
					break;
				}
			}
			break;
		case MTE_ASYNC_ERR:
			/* Imprecise fault should occur otherwise return error */
			if (cur_mte_cxt.fault_valid == false) {
				err = true;
				break;
			}
			/*
			 * The imprecise fault is checked after the write to the buffer,
			 * so the underflow area before the fault should be filled.
			 */
			und_ptr = (char *) MT_CLEAR_TAG((size_t) ptr);
			for (j = last_index ; j < 0 ; j++) {
				if (und_ptr[j] != '1') {
					err = true;
					break;
				}
			}
			break;
		case MTE_SYNC_ERR:
			/* Precise fault should occur otherwise return error */
			if (!cur_mte_cxt.fault_valid || (last_index != (-1))) {
				err = true;
				break;
			}
			/* Underflow area should not be filled */
			und_ptr = (char *) MT_CLEAR_TAG((size_t) ptr);
			if (und_ptr[-1] == '1')
				err = true;
			break;
		default:
			err = true;
		break;
		}
check_buffer_underflow_by_byte_err:
		mte_free_memory_tag_range((void *)ptr, sizes[i], mem_type, underflow_range, 0);
		if (err)
			break;
	}
	return (err ? KSFT_FAIL : KSFT_PASS);
}

static int check_buffer_overflow_by_byte(int mem_type, int mode,
					  int overflow_range)
{
	char *ptr;
	int i, j, item, last_index;
	bool err;
	size_t tagged_size, overflow_size;
	char *over_ptr = NULL;

	mte_switch_mode(mode, MTE_ALLOW_NON_ZERO_TAG);
	item = ARRAY_SIZE(sizes);
	for (i = 0; i < item; i++) {
		ptr = (char *)mte_allocate_memory_tag_range(sizes[i], mem_type, 0,
							    0, overflow_range);
		if (check_allocated_memory_range(ptr, sizes[i], mem_type,
						 0, overflow_range) != KSFT_PASS)
			return KSFT_FAIL;

		tagged_size = MT_ALIGN_UP(sizes[i]);

		mte_initialize_current_context(mode, (uintptr_t)ptr, sizes[i] + overflow_range);

		/* Set some value in tagged memory and make the buffer underflow */
		for (j = 0, last_index = 0 ; (j < (sizes[i] + overflow_range)) &&
					     (cur_mte_cxt.fault_valid == false); j++) {
			ptr[j] = '1';
			last_index = j;
		}
		mte_wait_after_trig();
		err = false;
		/* Check whether the buffer is filled */
		for (j = 0; j < sizes[i]; j++) {
			if (ptr[j] != '1') {
				err = true;
				ksft_print_msg("Buffer is not filled at index:%d of ptr:0x%p\n",
						j, ptr);
				break;
			}
		}
		if (err)
			goto check_buffer_overflow_by_byte_err;

		overflow_size = overflow_range - (tagged_size - sizes[i]);

		switch (mode) {
		case MTE_NONE_ERR:
			if ((cur_mte_cxt.fault_valid == true) ||
			    (last_index != (sizes[i] + overflow_range - 1))) {
				err = true;
				break;
			}
			/* There were no fault so the overflow area should be filled */
			over_ptr = (char *) MT_CLEAR_TAG((size_t) ptr + tagged_size);
			for (j = 0 ; j < overflow_size; j++) {
				if (over_ptr[j] != '1') {
					err = true;
					break;
				}
			}
			break;
		case MTE_ASYNC_ERR:
			/* Imprecise fault should occur otherwise return error */
			if (cur_mte_cxt.fault_valid == false) {
				err = true;
				break;
			}
			/*
			 * The imprecise fault is checked after the write to the buffer,
			 * so the overflow area should be filled before the fault.
			 */
			over_ptr = (char *) MT_CLEAR_TAG((size_t) ptr);
			for (j = tagged_size ; j < last_index; j++) {
				if (over_ptr[j] != '1') {
					err = true;
					break;
				}
			}
			break;
		case MTE_SYNC_ERR:
			/* Precise fault should occur otherwise return error */
			if (!cur_mte_cxt.fault_valid || (last_index != tagged_size)) {
				err = true;
				break;
			}
			/* Underflow area should not be filled */
			over_ptr = (char *) MT_CLEAR_TAG((size_t) ptr + tagged_size);
			for (j = 0 ; j < overflow_size; j++) {
				if (over_ptr[j] == '1')
					err = true;
			}
			break;
		default:
			err = true;
		break;
		}
check_buffer_overflow_by_byte_err:
		mte_free_memory_tag_range((void *)ptr, sizes[i], mem_type, 0, overflow_range);
		if (err)
			break;
	}
	return (err ? KSFT_FAIL : KSFT_PASS);
}

static int check_buffer_by_block_iterate(int mem_type, int mode, size_t size)
{
	char *src, *dst;
	int j, result = KSFT_PASS;
	enum mte_block_test_alloc alloc_type = UNTAGGED_TAGGED;

	for (alloc_type = UNTAGGED_TAGGED; alloc_type < (int) BLOCK_ALLOC_MAX; alloc_type++) {
		switch (alloc_type) {
		case UNTAGGED_TAGGED:
			src = (char *)mte_allocate_memory(size, mem_type, 0, false);
			if (check_allocated_memory(src, size, mem_type, false) != KSFT_PASS)
				return KSFT_FAIL;

			dst = (char *)mte_allocate_memory(size, mem_type, 0, true);
			if (check_allocated_memory(dst, size, mem_type, true) != KSFT_PASS) {
				mte_free_memory((void *)src, size, mem_type, false);
				return KSFT_FAIL;
			}

			break;
		case TAGGED_UNTAGGED:
			dst = (char *)mte_allocate_memory(size, mem_type, 0, false);
			if (check_allocated_memory(dst, size, mem_type, false) != KSFT_PASS)
				return KSFT_FAIL;

			src = (char *)mte_allocate_memory(size, mem_type, 0, true);
			if (check_allocated_memory(src, size, mem_type, true) != KSFT_PASS) {
				mte_free_memory((void *)dst, size, mem_type, false);
				return KSFT_FAIL;
			}
			break;
		case TAGGED_TAGGED:
			src = (char *)mte_allocate_memory(size, mem_type, 0, true);
			if (check_allocated_memory(src, size, mem_type, true) != KSFT_PASS)
				return KSFT_FAIL;

			dst = (char *)mte_allocate_memory(size, mem_type, 0, true);
			if (check_allocated_memory(dst, size, mem_type, true) != KSFT_PASS) {
				mte_free_memory((void *)src, size, mem_type, true);
				return KSFT_FAIL;
			}
			break;
		default:
			return KSFT_FAIL;
		}

		cur_mte_cxt.fault_valid = false;
		result = KSFT_PASS;
		mte_initialize_current_context(mode, (uintptr_t)dst, size);
		/* Set some value in memory and copy*/
		memset((void *)src, (int)'1', size);
		memcpy((void *)dst, (void *)src, size);
		mte_wait_after_trig();
		if (cur_mte_cxt.fault_valid) {
			result = KSFT_FAIL;
			goto check_buffer_by_block_err;
		}
		/* Check the buffer whether it is filled. */
		for (j = 0; j < size; j++) {
			if (src[j] != dst[j] || src[j] != '1') {
				result = KSFT_FAIL;
				break;
			}
		}
check_buffer_by_block_err:
		mte_free_memory((void *)src, size, mem_type,
				MT_FETCH_TAG((uintptr_t)src) ? true : false);
		mte_free_memory((void *)dst, size, mem_type,
				MT_FETCH_TAG((uintptr_t)dst) ? true : false);
		if (result != KSFT_PASS)
			return result;
	}
	return result;
}

static int check_buffer_by_block(int mem_type, int mode)
{
	int i, item, result = KSFT_PASS;

	mte_switch_mode(mode, MTE_ALLOW_NON_ZERO_TAG);
	item = ARRAY_SIZE(sizes);
	cur_mte_cxt.fault_valid = false;
	for (i = 0; i < item; i++) {
		result = check_buffer_by_block_iterate(mem_type, mode, sizes[i]);
		if (result != KSFT_PASS)
			break;
	}
	return result;
}

static int compare_memory_tags(char *ptr, size_t size, int tag)
{
	int i, new_tag;

	for (i = 0 ; i < size ; i += MT_GRANULE_SIZE) {
		new_tag = MT_FETCH_TAG((uintptr_t)(mte_get_tag_address(ptr + i)));
		if (tag != new_tag) {
			ksft_print_msg("FAIL: child mte tag mismatch\n");
			return KSFT_FAIL;
		}
	}
	return KSFT_PASS;
}

static int check_memory_initial_tags(int mem_type, int mode, int mapping)
{
	char *ptr;
	int run, fd;
	int total = ARRAY_SIZE(sizes);

	mte_switch_mode(mode, MTE_ALLOW_NON_ZERO_TAG);
	for (run = 0; run < total; run++) {
		/* check initial tags for anonymous mmap */
		ptr = (char *)mte_allocate_memory(sizes[run], mem_type, mapping, false);
		if (check_allocated_memory(ptr, sizes[run], mem_type, false) != KSFT_PASS)
			return KSFT_FAIL;
		if (compare_memory_tags(ptr, sizes[run], 0) != KSFT_PASS) {
			mte_free_memory((void *)ptr, sizes[run], mem_type, false);
			return KSFT_FAIL;
		}
		mte_free_memory((void *)ptr, sizes[run], mem_type, false);

		/* check initial tags for file mmap */
		fd = create_temp_file();
		if (fd == -1)
			return KSFT_FAIL;
		ptr = (char *)mte_allocate_file_memory(sizes[run], mem_type, mapping, false, fd);
		if (check_allocated_memory(ptr, sizes[run], mem_type, false) != KSFT_PASS) {
			close(fd);
			return KSFT_FAIL;
		}
		if (compare_memory_tags(ptr, sizes[run], 0) != KSFT_PASS) {
			mte_free_memory((void *)ptr, sizes[run], mem_type, false);
			close(fd);
			return KSFT_FAIL;
		}
		mte_free_memory((void *)ptr, sizes[run], mem_type, false);
		close(fd);
	}
	return KSFT_PASS;
}

int main(int argc, char *argv[])
{
	int err;
	size_t page_size = getpagesize();
	int item = ARRAY_SIZE(sizes);

	sizes[item - 3] = page_size - 1;
	sizes[item - 2] = page_size;
	sizes[item - 1] = page_size + 1;

	err = mte_default_setup();
	if (err)
		return err;

	/* Register SIGSEGV handler */
	mte_register_signal(SIGSEGV, mte_default_handler);

	/* Set test plan */
	ksft_set_plan(20);

	/* Buffer by byte tests */
	evaluate_test(check_buffer_by_byte(USE_MMAP, MTE_SYNC_ERR),
	"Check buffer correctness by byte with sync err mode and mmap memory\n");
	evaluate_test(check_buffer_by_byte(USE_MMAP, MTE_ASYNC_ERR),
	"Check buffer correctness by byte with async err mode and mmap memory\n");
	evaluate_test(check_buffer_by_byte(USE_MPROTECT, MTE_SYNC_ERR),
	"Check buffer correctness by byte with sync err mode and mmap/mprotect memory\n");
	evaluate_test(check_buffer_by_byte(USE_MPROTECT, MTE_ASYNC_ERR),
	"Check buffer correctness by byte with async err mode and mmap/mprotect memory\n");

	/* Check buffer underflow with underflow size as 16 */
	evaluate_test(check_buffer_underflow_by_byte(USE_MMAP, MTE_SYNC_ERR, MT_GRANULE_SIZE),
	"Check buffer write underflow by byte with sync mode and mmap memory\n");
	evaluate_test(check_buffer_underflow_by_byte(USE_MMAP, MTE_ASYNC_ERR, MT_GRANULE_SIZE),
	"Check buffer write underflow by byte with async mode and mmap memory\n");
	evaluate_test(check_buffer_underflow_by_byte(USE_MMAP, MTE_NONE_ERR, MT_GRANULE_SIZE),
	"Check buffer write underflow by byte with tag check fault ignore and mmap memory\n");

	/* Check buffer underflow with underflow size as page size */
	evaluate_test(check_buffer_underflow_by_byte(USE_MMAP, MTE_SYNC_ERR, page_size),
	"Check buffer write underflow by byte with sync mode and mmap memory\n");
	evaluate_test(check_buffer_underflow_by_byte(USE_MMAP, MTE_ASYNC_ERR, page_size),
	"Check buffer write underflow by byte with async mode and mmap memory\n");
	evaluate_test(check_buffer_underflow_by_byte(USE_MMAP, MTE_NONE_ERR, page_size),
	"Check buffer write underflow by byte with tag check fault ignore and mmap memory\n");

	/* Check buffer overflow with overflow size as 16 */
	evaluate_test(check_buffer_overflow_by_byte(USE_MMAP, MTE_SYNC_ERR, MT_GRANULE_SIZE),
	"Check buffer write overflow by byte with sync mode and mmap memory\n");
	evaluate_test(check_buffer_overflow_by_byte(USE_MMAP, MTE_ASYNC_ERR, MT_GRANULE_SIZE),
	"Check buffer write overflow by byte with async mode and mmap memory\n");
	evaluate_test(check_buffer_overflow_by_byte(USE_MMAP, MTE_NONE_ERR, MT_GRANULE_SIZE),
	"Check buffer write overflow by byte with tag fault ignore mode and mmap memory\n");

	/* Buffer by block tests */
	evaluate_test(check_buffer_by_block(USE_MMAP, MTE_SYNC_ERR),
	"Check buffer write correctness by block with sync mode and mmap memory\n");
	evaluate_test(check_buffer_by_block(USE_MMAP, MTE_ASYNC_ERR),
	"Check buffer write correctness by block with async mode and mmap memory\n");
	evaluate_test(check_buffer_by_block(USE_MMAP, MTE_NONE_ERR),
	"Check buffer write correctness by block with tag fault ignore and mmap memory\n");

	/* Initial tags are supposed to be 0 */
	evaluate_test(check_memory_initial_tags(USE_MMAP, MTE_SYNC_ERR, MAP_PRIVATE),
	"Check initial tags with private mapping, sync error mode and mmap memory\n");
	evaluate_test(check_memory_initial_tags(USE_MPROTECT, MTE_SYNC_ERR, MAP_PRIVATE),
	"Check initial tags with private mapping, sync error mode and mmap/mprotect memory\n");
	evaluate_test(check_memory_initial_tags(USE_MMAP, MTE_SYNC_ERR, MAP_SHARED),
	"Check initial tags with shared mapping, sync error mode and mmap memory\n");
	evaluate_test(check_memory_initial_tags(USE_MPROTECT, MTE_SYNC_ERR, MAP_SHARED),
	"Check initial tags with shared mapping, sync error mode and mmap/mprotect memory\n");

	mte_restore_setup();
	ksft_print_cnts();
	return ksft_get_fail_cnt() == 0 ? KSFT_PASS : KSFT_FAIL;
}
