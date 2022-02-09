// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 ARM Limited

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>
#include <sys/mman.h>

#include "kselftest.h"
#include "mte_common_util.h"
#include "mte_def.h"

static size_t page_sz;

static int check_usermem_access_fault(int mem_type, int mode, int mapping,
                                      int tag_offset, int tag_len)
{
	int fd, i, err;
	char val = 'A';
	size_t len, read_len;
	void *ptr, *ptr_next;

	err = KSFT_PASS;
	len = 2 * page_sz;
	mte_switch_mode(mode, MTE_ALLOW_NON_ZERO_TAG);
	fd = create_temp_file();
	if (fd == -1)
		return KSFT_FAIL;
	for (i = 0; i < len; i++)
		if (write(fd, &val, sizeof(val)) != sizeof(val))
			return KSFT_FAIL;
	lseek(fd, 0, 0);
	ptr = mte_allocate_memory(len, mem_type, mapping, true);
	if (check_allocated_memory(ptr, len, mem_type, true) != KSFT_PASS) {
		close(fd);
		return KSFT_FAIL;
	}
	mte_initialize_current_context(mode, (uintptr_t)ptr, len);
	/* Copy from file into buffer with valid tag */
	read_len = read(fd, ptr, len);
	mte_wait_after_trig();
	if (cur_mte_cxt.fault_valid || read_len < len)
		goto usermem_acc_err;
	/* Verify same pattern is read */
	for (i = 0; i < len; i++)
		if (*(char *)(ptr + i) != val)
			break;
	if (i < len)
		goto usermem_acc_err;

	if (!tag_len)
		tag_len = len - tag_offset;
	/* Tag a part of memory with different value */
	ptr_next = (void *)((unsigned long)ptr + tag_offset);
	ptr_next = mte_insert_new_tag(ptr_next);
	mte_set_tag_address_range(ptr_next, tag_len);

	lseek(fd, 0, 0);
	/* Copy from file into buffer with invalid tag */
	read_len = read(fd, ptr, len);
	mte_wait_after_trig();
	/*
	 * Accessing user memory in kernel with invalid tag should fail in sync
	 * mode without fault but may not fail in async mode as per the
	 * implemented MTE userspace support in Arm64 kernel.
	 */
	if (cur_mte_cxt.fault_valid)
		goto usermem_acc_err;

	if (mode == MTE_SYNC_ERR && read_len < len) {
		/* test passed */
	} else if (mode == MTE_ASYNC_ERR && read_len == len) {
		/* test passed */
	} else {
		goto usermem_acc_err;
	}

	goto exit;

usermem_acc_err:
	err = KSFT_FAIL;
exit:
	mte_free_memory((void *)ptr, len, mem_type, true);
	close(fd);
	return err;
}

int main(int argc, char *argv[])
{
	int err;

	page_sz = getpagesize();
	if (!page_sz) {
		ksft_print_msg("ERR: Unable to get page size\n");
		return KSFT_FAIL;
	}
	err = mte_default_setup();
	if (err)
		return err;

	/* Register signal handlers */
	mte_register_signal(SIGSEGV, mte_default_handler);

	/* Set test plan */
	ksft_set_plan(4);

	evaluate_test(check_usermem_access_fault(USE_MMAP, MTE_SYNC_ERR, MAP_PRIVATE, page_sz, 0),
		"Check memory access from kernel in sync mode, private mapping and mmap memory\n");
	evaluate_test(check_usermem_access_fault(USE_MMAP, MTE_SYNC_ERR, MAP_SHARED, page_sz, 0),
		"Check memory access from kernel in sync mode, shared mapping and mmap memory\n");

	evaluate_test(check_usermem_access_fault(USE_MMAP, MTE_ASYNC_ERR, MAP_PRIVATE, page_sz, 0),
		"Check memory access from kernel in async mode, private mapping and mmap memory\n");
	evaluate_test(check_usermem_access_fault(USE_MMAP, MTE_ASYNC_ERR, MAP_SHARED, page_sz, 0),
		"Check memory access from kernel in async mode, shared mapping and mmap memory\n");

	mte_restore_setup();
	ksft_print_cnts();
	return ksft_get_fail_cnt() == 0 ? KSFT_PASS : KSFT_FAIL;
}
