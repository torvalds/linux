// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 ARM Limited

#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/mman.h>

#include "kselftest.h"
#include "mte_common_util.h"
#include "mte_def.h"

static size_t page_sz;

#define TEST_NAME_MAX 100

enum test_type {
	READ_TEST,
	WRITE_TEST,
	READV_TEST,
	WRITEV_TEST,
	LAST_TEST,
};

static int check_usermem_access_fault(int mem_type, int mode, int mapping,
                                      int tag_offset, int tag_len,
                                      enum test_type test_type)
{
	int fd, i, err;
	char val = 'A';
	ssize_t len, syscall_len;
	void *ptr, *ptr_next;
	int fileoff, ptroff, size;
	int sizes[] = {1, 2, 3, 8, 16, 32, 4096, page_sz};

	err = KSFT_PASS;
	len = 2 * page_sz;
	mte_switch_mode(mode, MTE_ALLOW_NON_ZERO_TAG, false);
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
	syscall_len = read(fd, ptr, len);
	mte_wait_after_trig();
	if (cur_mte_cxt.fault_valid || syscall_len < len)
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

	for (fileoff = 0; fileoff < 16; fileoff++) {
		for (ptroff = 0; ptroff < 16; ptroff++) {
			for (i = 0; i < ARRAY_SIZE(sizes); i++) {
				size = sizes[i];
				lseek(fd, 0, 0);

				/* perform file operation on buffer with invalid tag */
				switch (test_type) {
				case READ_TEST:
					syscall_len = read(fd, ptr + ptroff, size);
					break;
				case WRITE_TEST:
					syscall_len = write(fd, ptr + ptroff, size);
					break;
				case READV_TEST: {
					struct iovec iov[1];
					iov[0].iov_base = ptr + ptroff;
					iov[0].iov_len = size;
					syscall_len = readv(fd, iov, 1);
					break;
				}
				case WRITEV_TEST: {
					struct iovec iov[1];
					iov[0].iov_base = ptr + ptroff;
					iov[0].iov_len = size;
					syscall_len = writev(fd, iov, 1);
					break;
				}
				case LAST_TEST:
					goto usermem_acc_err;
				}

				mte_wait_after_trig();
				/*
				 * Accessing user memory in kernel with invalid tag should fail in sync
				 * mode without fault but may not fail in async mode as per the
				 * implemented MTE userspace support in Arm64 kernel.
				 */
				if (cur_mte_cxt.fault_valid) {
					goto usermem_acc_err;
				}
				if (mode == MTE_SYNC_ERR && syscall_len < len) {
					/* test passed */
				} else if (mode == MTE_ASYNC_ERR && syscall_len == size) {
					/* test passed */
				} else {
					goto usermem_acc_err;
				}
			}
		}
	}

	goto exit;

usermem_acc_err:
	err = KSFT_FAIL;
exit:
	mte_free_memory((void *)ptr, len, mem_type, true);
	close(fd);
	return err;
}

void format_test_name(char* name, int name_len, int type, int sync, int map, int len, int offset) {
	const char* test_type;
	const char* mte_type;
	const char* map_type;

	switch (type) {
	case READ_TEST:
		test_type = "read";
		break;
	case WRITE_TEST:
		test_type = "write";
		break;
	case READV_TEST:
		test_type = "readv";
		break;
	case WRITEV_TEST:
		test_type = "writev";
		break;
	default:
		assert(0);
		break;
	}

	switch (sync) {
	case MTE_SYNC_ERR:
		mte_type = "MTE_SYNC_ERR";
		break;
	case MTE_ASYNC_ERR:
		mte_type = "MTE_ASYNC_ERR";
		break;
	default:
		assert(0);
		break;
	}

	switch (map) {
	case MAP_SHARED:
		map_type = "MAP_SHARED";
		break;
	case MAP_PRIVATE:
		map_type = "MAP_PRIVATE";
		break;
	default:
		assert(0);
		break;
	}

	snprintf(name, name_len,
	         "test type: %s, %s, %s, tag len: %d, tag offset: %d\n",
	         test_type, mte_type, map_type, len, offset);
}

int main(int argc, char *argv[])
{
	int err;
	int t, s, m, l, o;
	int mte_sync[] = {MTE_SYNC_ERR, MTE_ASYNC_ERR};
	int maps[] = {MAP_SHARED, MAP_PRIVATE};
	int tag_lens[] = {0, MT_GRANULE_SIZE};
	int tag_offsets[] = {page_sz, MT_GRANULE_SIZE};
	char test_name[TEST_NAME_MAX];

	page_sz = getpagesize();
	if (!page_sz) {
		ksft_print_msg("ERR: Unable to get page size\n");
		return KSFT_FAIL;
	}
	err = mte_default_setup();
	if (err)
		return err;

	/* Register signal handlers */
	mte_register_signal(SIGSEGV, mte_default_handler, false);

	/* Set test plan */
	ksft_set_plan(64);

	for (t = 0; t < LAST_TEST; t++) {
		for (s = 0; s < ARRAY_SIZE(mte_sync); s++) {
			for (m = 0; m < ARRAY_SIZE(maps); m++) {
				for (l = 0; l < ARRAY_SIZE(tag_lens); l++) {
					for (o = 0; o < ARRAY_SIZE(tag_offsets); o++) {
						int sync = mte_sync[s];
						int map = maps[m];
						int offset = tag_offsets[o];
						int tag_len = tag_lens[l];
						int res = check_usermem_access_fault(USE_MMAP, sync,
						                                     map, offset,
						                                     tag_len, t);
						format_test_name(test_name, TEST_NAME_MAX,
						                 t, sync, map, tag_len, offset);
						evaluate_test(res, test_name);
					}
				}
			}
		}
	}

	mte_restore_setup();
	ksft_print_cnts();
	return ksft_get_fail_cnt() == 0 ? KSFT_PASS : KSFT_FAIL;
}
