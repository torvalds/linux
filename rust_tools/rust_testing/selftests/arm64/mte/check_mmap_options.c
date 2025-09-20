// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 ARM Limited

#define _GNU_SOURCE

#include <assert.h>
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
#define ATAG_CHECK_ON		1
#define ATAG_CHECK_OFF		0

#define TEST_NAME_MAX		256

enum mte_mem_check_type {
	CHECK_ANON_MEM = 0,
	CHECK_FILE_MEM = 1,
	CHECK_CLEAR_PROT_MTE = 2,
};

enum mte_tag_op_type {
	TAG_OP_ALL = 0,
	TAG_OP_STONLY = 1,
};

struct check_mmap_testcase {
	int check_type;
	int mem_type;
	int mte_sync;
	int mapping;
	int tag_check;
	int atag_check;
	int tag_op;
	bool enable_tco;
};

#define TAG_OP_ALL		0
#define TAG_OP_STONLY		1

static size_t page_size;
static int sizes[] = {
	1, 537, 989, 1269, MT_GRANULE_SIZE - 1, MT_GRANULE_SIZE,
	/* page size - 1*/ 0, /* page_size */ 0, /* page size + 1 */ 0
};

static int check_mte_memory(char *ptr, int size, int mode,
		int tag_check,int atag_check, int tag_op)
{
	char buf[MT_GRANULE_SIZE];

	if (!mtefar_support && atag_check == ATAG_CHECK_ON)
		return KSFT_SKIP;

	if (atag_check == ATAG_CHECK_ON)
		ptr = mte_insert_atag(ptr);

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

	if (tag_op == TAG_OP_STONLY) {
		mte_initialize_current_context(mode, (uintptr_t)ptr, -UNDERFLOW);
		memcpy(buf, ptr - UNDERFLOW, MT_GRANULE_SIZE);
		mte_wait_after_trig();
		if (cur_mte_cxt.fault_valid == true)
			return KSFT_FAIL;

		mte_initialize_current_context(mode, (uintptr_t)ptr, size + OVERFLOW);
		memcpy(buf, ptr + size, MT_GRANULE_SIZE);
		mte_wait_after_trig();
		if (cur_mte_cxt.fault_valid == true)
			return KSFT_FAIL;
	}

	return KSFT_PASS;
}

static int check_anonymous_memory_mapping(int mem_type, int mode, int mapping,
		int tag_check, int atag_check, int tag_op)
{
	char *ptr, *map_ptr;
	int run, result, map_size;
	int item = ARRAY_SIZE(sizes);

	if (tag_op == TAG_OP_STONLY && !mtestonly_support)
		return KSFT_SKIP;

	mte_switch_mode(mode, MTE_ALLOW_NON_ZERO_TAG, tag_op);
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
		result = check_mte_memory(ptr, sizes[run], mode, tag_check, atag_check, tag_op);
		mte_clear_tags((void *)ptr, sizes[run]);
		mte_free_memory((void *)map_ptr, map_size, mem_type, false);
		if (result != KSFT_PASS)
			return result;
	}
	return KSFT_PASS;
}

static int check_file_memory_mapping(int mem_type, int mode, int mapping,
		int tag_check, int atag_check, int tag_op)
{
	char *ptr, *map_ptr;
	int run, fd, map_size;
	int total = ARRAY_SIZE(sizes);
	int result = KSFT_PASS;

	if (tag_op == TAG_OP_STONLY && !mtestonly_support)
		return KSFT_SKIP;

	mte_switch_mode(mode, MTE_ALLOW_NON_ZERO_TAG, tag_op);
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
		result = check_mte_memory(ptr, sizes[run], mode, tag_check, atag_check, tag_op);
		mte_clear_tags((void *)ptr, sizes[run]);
		munmap((void *)map_ptr, map_size);
		close(fd);
		if (result != KSFT_PASS)
			return result;
	}
	return KSFT_PASS;
}

static int check_clear_prot_mte_flag(int mem_type, int mode, int mapping, int atag_check)
{
	char *ptr, *map_ptr;
	int run, prot_flag, result, fd, map_size;
	int total = ARRAY_SIZE(sizes);

	prot_flag = PROT_READ | PROT_WRITE;
	mte_switch_mode(mode, MTE_ALLOW_NON_ZERO_TAG, false);
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
		result = check_mte_memory(ptr, sizes[run], mode, TAG_CHECK_ON, atag_check, TAG_OP_ALL);
		mte_free_memory_tag_range((void *)ptr, sizes[run], mem_type, UNDERFLOW, OVERFLOW);
		if (result != KSFT_PASS)
			return result;

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
		result = check_mte_memory(ptr, sizes[run], mode, TAG_CHECK_ON, atag_check, TAG_OP_ALL);
		mte_free_memory_tag_range((void *)ptr, sizes[run], mem_type, UNDERFLOW, OVERFLOW);
		close(fd);
		if (result != KSFT_PASS)
			return result;
	}
	return KSFT_PASS;
}

const char *format_test_name(struct check_mmap_testcase *tc)
{
	static char test_name[TEST_NAME_MAX];
	const char *check_type_str;
	const char *mem_type_str;
	const char *sync_str;
	const char *mapping_str;
	const char *tag_check_str;
	const char *atag_check_str;
	const char *tag_op_str;

	switch (tc->check_type) {
	case CHECK_ANON_MEM:
		check_type_str = "anonymous memory";
		break;
	case CHECK_FILE_MEM:
		check_type_str = "file memory";
		break;
	case CHECK_CLEAR_PROT_MTE:
		check_type_str = "clear PROT_MTE flags";
		break;
	default:
		assert(0);
		break;
	}

	switch (tc->mem_type) {
	case USE_MMAP:
		mem_type_str = "mmap";
		break;
	case USE_MPROTECT:
		mem_type_str = "mmap/mprotect";
		break;
	default:
		assert(0);
		break;
	}

	switch (tc->mte_sync) {
	case MTE_NONE_ERR:
		sync_str = "no error";
		break;
	case MTE_SYNC_ERR:
		sync_str = "sync error";
		break;
	case MTE_ASYNC_ERR:
		sync_str = "async error";
		break;
	default:
		assert(0);
		break;
	}

	switch (tc->mapping) {
	case MAP_SHARED:
		mapping_str = "shared";
		break;
	case MAP_PRIVATE:
		mapping_str = "private";
		break;
	default:
		assert(0);
		break;
	}

	switch (tc->tag_check) {
	case TAG_CHECK_ON:
		tag_check_str = "tag check on";
		break;
	case TAG_CHECK_OFF:
		tag_check_str = "tag check off";
		break;
	default:
		assert(0);
		break;
	}

	switch (tc->atag_check) {
	case ATAG_CHECK_ON:
		atag_check_str = "with address tag [63:60]";
		break;
	case ATAG_CHECK_OFF:
		atag_check_str = "without address tag [63:60]";
		break;
	default:
		assert(0);
		break;
	}

	snprintf(test_name, sizeof(test_name),
	         "Check %s with %s mapping, %s mode, %s memory and %s (%s)\n",
	         check_type_str, mapping_str, sync_str, mem_type_str,
	         tag_check_str, atag_check_str);

	switch (tc->tag_op) {
	case TAG_OP_ALL:
		tag_op_str = "";
		break;
	case TAG_OP_STONLY:
		tag_op_str = " / store-only";
		break;
	default:
		assert(0);
		break;
	}

	snprintf(test_name, TEST_NAME_MAX,
	         "Check %s with %s mapping, %s mode, %s memory and %s (%s%s)\n",
	         check_type_str, mapping_str, sync_str, mem_type_str,
	         tag_check_str, atag_check_str, tag_op_str);

	return test_name;
}

int main(int argc, char *argv[])
{
	int err, i;
	int item = ARRAY_SIZE(sizes);
	struct check_mmap_testcase test_cases[]= {
		{
			.check_type = CHECK_ANON_MEM,
			.mem_type = USE_MMAP,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_PRIVATE,
			.tag_check = TAG_CHECK_OFF,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_ALL,
			.enable_tco = true,
		},
		{
			.check_type = CHECK_FILE_MEM,
			.mem_type = USE_MPROTECT,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_PRIVATE,
			.tag_check = TAG_CHECK_OFF,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_ALL,
			.enable_tco = true,
		},
		{
			.check_type = CHECK_ANON_MEM,
			.mem_type = USE_MMAP,
			.mte_sync = MTE_NONE_ERR,
			.mapping = MAP_PRIVATE,
			.tag_check = TAG_CHECK_OFF,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_ALL,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_FILE_MEM,
			.mem_type = USE_MPROTECT,
			.mte_sync = MTE_NONE_ERR,
			.mapping = MAP_PRIVATE,
			.tag_check = TAG_CHECK_OFF,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_ALL,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_ANON_MEM,
			.mem_type = USE_MMAP,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_PRIVATE,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_ALL,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_ANON_MEM,
			.mem_type = USE_MPROTECT,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_PRIVATE,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_ALL,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_ANON_MEM,
			.mem_type = USE_MMAP,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_SHARED,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_ALL,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_ANON_MEM,
			.mem_type = USE_MPROTECT,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_SHARED,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_ALL,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_ANON_MEM,
			.mem_type = USE_MMAP,
			.mte_sync = MTE_ASYNC_ERR,
			.mapping = MAP_PRIVATE,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_ALL,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_ANON_MEM,
			.mem_type = USE_MPROTECT,
			.mte_sync = MTE_ASYNC_ERR,
			.mapping = MAP_PRIVATE,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_ALL,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_ANON_MEM,
			.mem_type = USE_MMAP,
			.mte_sync = MTE_ASYNC_ERR,
			.mapping = MAP_SHARED,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_ALL,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_ANON_MEM,
			.mem_type = USE_MPROTECT,
			.mte_sync = MTE_ASYNC_ERR,
			.mapping = MAP_SHARED,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_ALL,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_FILE_MEM,
			.mem_type = USE_MMAP,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_PRIVATE,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_ALL,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_FILE_MEM,
			.mem_type = USE_MPROTECT,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_PRIVATE,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_ALL,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_FILE_MEM,
			.mem_type = USE_MMAP,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_SHARED,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_ALL,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_FILE_MEM,
			.mem_type = USE_MPROTECT,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_SHARED,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_ALL,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_FILE_MEM,
			.mem_type = USE_MMAP,
			.mte_sync = MTE_ASYNC_ERR,
			.mapping = MAP_PRIVATE,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_ALL,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_FILE_MEM,
			.mem_type = USE_MPROTECT,
			.mte_sync = MTE_ASYNC_ERR,
			.mapping = MAP_PRIVATE,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_ALL,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_FILE_MEM,
			.mem_type = USE_MMAP,
			.mte_sync = MTE_ASYNC_ERR,
			.mapping = MAP_SHARED,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_ALL,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_FILE_MEM,
			.mem_type = USE_MPROTECT,
			.mte_sync = MTE_ASYNC_ERR,
			.mapping = MAP_SHARED,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_ALL,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_CLEAR_PROT_MTE,
			.mem_type = USE_MMAP,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_PRIVATE,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_ALL,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_CLEAR_PROT_MTE,
			.mem_type = USE_MPROTECT,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_PRIVATE,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_ALL,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_ANON_MEM,
			.mem_type = USE_MMAP,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_PRIVATE,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_ON,
			.tag_op = TAG_OP_ALL,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_ANON_MEM,
			.mem_type = USE_MPROTECT,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_PRIVATE,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_ON,
			.tag_op = TAG_OP_ALL,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_ANON_MEM,
			.mem_type = USE_MMAP,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_SHARED,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_ON,
			.tag_op = TAG_OP_ALL,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_ANON_MEM,
			.mem_type = USE_MPROTECT,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_SHARED,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_ON,
			.tag_op = TAG_OP_ALL,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_FILE_MEM,
			.mem_type = USE_MMAP,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_PRIVATE,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_ON,
			.tag_op = TAG_OP_ALL,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_FILE_MEM,
			.mem_type = USE_MPROTECT,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_PRIVATE,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_ON,
			.tag_op = TAG_OP_ALL,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_FILE_MEM,
			.mem_type = USE_MMAP,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_SHARED,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_ON,
			.tag_op = TAG_OP_ALL,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_FILE_MEM,
			.mem_type = USE_MPROTECT,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_SHARED,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_ON,
			.tag_op = TAG_OP_ALL,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_FILE_MEM,
			.mem_type = USE_MMAP,
			.mte_sync = MTE_ASYNC_ERR,
			.mapping = MAP_PRIVATE,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_ON,
			.tag_op = TAG_OP_ALL,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_ANON_MEM,
			.mem_type = USE_MMAP,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_PRIVATE,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_STONLY,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_ANON_MEM,
			.mem_type = USE_MPROTECT,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_PRIVATE,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_STONLY,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_ANON_MEM,
			.mem_type = USE_MMAP,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_SHARED,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_STONLY,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_ANON_MEM,
			.mem_type = USE_MPROTECT,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_SHARED,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_STONLY,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_ANON_MEM,
			.mem_type = USE_MMAP,
			.mte_sync = MTE_ASYNC_ERR,
			.mapping = MAP_PRIVATE,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_STONLY,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_ANON_MEM,
			.mem_type = USE_MPROTECT,
			.mte_sync = MTE_ASYNC_ERR,
			.mapping = MAP_PRIVATE,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_STONLY,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_ANON_MEM,
			.mem_type = USE_MMAP,
			.mte_sync = MTE_ASYNC_ERR,
			.mapping = MAP_SHARED,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_STONLY,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_ANON_MEM,
			.mem_type = USE_MPROTECT,
			.mte_sync = MTE_ASYNC_ERR,
			.mapping = MAP_SHARED,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_STONLY,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_FILE_MEM,
			.mem_type = USE_MMAP,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_PRIVATE,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_STONLY,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_FILE_MEM,
			.mem_type = USE_MPROTECT,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_PRIVATE,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_STONLY,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_FILE_MEM,
			.mem_type = USE_MMAP,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_SHARED,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_STONLY,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_FILE_MEM,
			.mem_type = USE_MPROTECT,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_SHARED,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_STONLY,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_FILE_MEM,
			.mem_type = USE_MMAP,
			.mte_sync = MTE_ASYNC_ERR,
			.mapping = MAP_PRIVATE,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_STONLY,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_FILE_MEM,
			.mem_type = USE_MPROTECT,
			.mte_sync = MTE_ASYNC_ERR,
			.mapping = MAP_PRIVATE,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_STONLY,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_FILE_MEM,
			.mem_type = USE_MMAP,
			.mte_sync = MTE_ASYNC_ERR,
			.mapping = MAP_SHARED,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_STONLY,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_FILE_MEM,
			.mem_type = USE_MPROTECT,
			.mte_sync = MTE_ASYNC_ERR,
			.mapping = MAP_SHARED,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_OFF,
			.tag_op = TAG_OP_STONLY,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_ANON_MEM,
			.mem_type = USE_MMAP,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_PRIVATE,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_ON,
			.tag_op = TAG_OP_STONLY,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_ANON_MEM,
			.mem_type = USE_MPROTECT,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_PRIVATE,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_ON,
			.tag_op = TAG_OP_STONLY,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_ANON_MEM,
			.mem_type = USE_MMAP,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_SHARED,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_ON,
			.tag_op = TAG_OP_STONLY,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_ANON_MEM,
			.mem_type = USE_MPROTECT,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_SHARED,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_ON,
			.tag_op = TAG_OP_STONLY,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_FILE_MEM,
			.mem_type = USE_MMAP,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_PRIVATE,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_ON,
			.tag_op = TAG_OP_STONLY,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_FILE_MEM,
			.mem_type = USE_MPROTECT,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_PRIVATE,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_ON,
			.tag_op = TAG_OP_STONLY,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_FILE_MEM,
			.mem_type = USE_MMAP,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_SHARED,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_ON,
			.tag_op = TAG_OP_STONLY,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_FILE_MEM,
			.mem_type = USE_MPROTECT,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_SHARED,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_ON,
			.tag_op = TAG_OP_STONLY,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_FILE_MEM,
			.mem_type = USE_MMAP,
			.mte_sync = MTE_ASYNC_ERR,
			.mapping = MAP_PRIVATE,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_ON,
			.tag_op = TAG_OP_STONLY,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_CLEAR_PROT_MTE,
			.mem_type = USE_MMAP,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_PRIVATE,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_ON,
			.tag_op = TAG_OP_ALL,
			.enable_tco = false,
		},
		{
			.check_type = CHECK_CLEAR_PROT_MTE,
			.mem_type = USE_MPROTECT,
			.mte_sync = MTE_SYNC_ERR,
			.mapping = MAP_PRIVATE,
			.tag_check = TAG_CHECK_ON,
			.atag_check = ATAG_CHECK_ON,
			.tag_op = TAG_OP_ALL,
			.enable_tco = false,
		},
	};

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

	/* Set test plan */
	ksft_set_plan(ARRAY_SIZE(test_cases));

	for (i = 0 ; i < ARRAY_SIZE(test_cases); i++) {
		/* Register signal handlers */
		mte_register_signal(SIGBUS, mte_default_handler,
				    test_cases[i].atag_check == ATAG_CHECK_ON);
		mte_register_signal(SIGSEGV, mte_default_handler,
				    test_cases[i].atag_check == ATAG_CHECK_ON);

		if (test_cases[i].enable_tco)
			mte_enable_pstate_tco();
		else
			mte_disable_pstate_tco();

		switch (test_cases[i].check_type) {
		case CHECK_ANON_MEM:
			evaluate_test(check_anonymous_memory_mapping(test_cases[i].mem_type,
								     test_cases[i].mte_sync,
								     test_cases[i].mapping,
								     test_cases[i].tag_check,
								     test_cases[i].atag_check,
								     test_cases[i].tag_op),
				      format_test_name(&test_cases[i]));
			break;
		case CHECK_FILE_MEM:
			evaluate_test(check_file_memory_mapping(test_cases[i].mem_type,
							        test_cases[i].mte_sync,
							        test_cases[i].mapping,
							        test_cases[i].tag_check,
							        test_cases[i].atag_check,
								test_cases[i].tag_op),
				      format_test_name(&test_cases[i]));
			break;
		case CHECK_CLEAR_PROT_MTE:
			evaluate_test(check_clear_prot_mte_flag(test_cases[i].mem_type,
							        test_cases[i].mte_sync,
							        test_cases[i].mapping,
							        test_cases[i].atag_check),
				      format_test_name(&test_cases[i]));
			break;
		default:
			exit(KSFT_FAIL);
		}
	}

	mte_restore_setup();
	ksft_print_cnts();
	return ksft_get_fail_cnt() == 0 ? KSFT_PASS : KSFT_FAIL;
}
