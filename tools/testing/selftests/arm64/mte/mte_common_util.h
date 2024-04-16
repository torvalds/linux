/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2020 ARM Limited */

#ifndef _MTE_COMMON_UTIL_H
#define _MTE_COMMON_UTIL_H

#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include "mte_def.h"
#include "kselftest.h"

enum mte_mem_type {
	USE_MALLOC,
	USE_MMAP,
	USE_MPROTECT,
};

enum mte_mode {
	MTE_NONE_ERR,
	MTE_SYNC_ERR,
	MTE_ASYNC_ERR,
};

struct mte_fault_cxt {
	/* Address start which triggers mte tag fault */
	unsigned long trig_addr;
	/* Address range for mte tag fault and negative value means underflow */
	ssize_t trig_range;
	/* siginfo si code */
	unsigned long trig_si_code;
	/* Flag to denote if correct fault caught */
	bool fault_valid;
};

extern struct mte_fault_cxt cur_mte_cxt;

/* MTE utility functions */
void mte_default_handler(int signum, siginfo_t *si, void *uc);
void mte_register_signal(int signal, void (*handler)(int, siginfo_t *, void *));
void mte_wait_after_trig(void);
void *mte_allocate_memory(size_t size, int mem_type, int mapping, bool tags);
void *mte_allocate_memory_tag_range(size_t size, int mem_type, int mapping,
				    size_t range_before, size_t range_after);
void *mte_allocate_file_memory(size_t size, int mem_type, int mapping,
			       bool tags, int fd);
void *mte_allocate_file_memory_tag_range(size_t size, int mem_type, int mapping,
					 size_t range_before, size_t range_after, int fd);
void mte_free_memory(void *ptr, size_t size, int mem_type, bool tags);
void mte_free_memory_tag_range(void *ptr, size_t size, int mem_type,
			       size_t range_before, size_t range_after);
void *mte_insert_tags(void *ptr, size_t size);
void mte_clear_tags(void *ptr, size_t size);
int mte_default_setup(void);
void mte_restore_setup(void);
int mte_switch_mode(int mte_option, unsigned long incl_mask);
void mte_initialize_current_context(int mode, uintptr_t ptr, ssize_t range);

/* Common utility functions */
int create_temp_file(void);

/* Assembly MTE utility functions */
void *mte_insert_random_tag(void *ptr);
void *mte_insert_new_tag(void *ptr);
void *mte_get_tag_address(void *ptr);
void mte_set_tag_address_range(void *ptr, int range);
void mte_clear_tag_address_range(void *ptr, int range);
void mte_disable_pstate_tco(void);
void mte_enable_pstate_tco(void);
unsigned int mte_get_pstate_tco(void);

/* Test framework static inline functions/macros */
static inline void evaluate_test(int err, const char *msg)
{
	switch (err) {
	case KSFT_PASS:
		ksft_test_result_pass(msg);
		break;
	case KSFT_FAIL:
		ksft_test_result_fail(msg);
		break;
	case KSFT_SKIP:
		ksft_test_result_skip(msg);
		break;
	default:
		ksft_test_result_error("Unknown return code %d from %s",
				       err, msg);
		break;
	}
}

static inline int check_allocated_memory(void *ptr, size_t size,
					 int mem_type, bool tags)
{
	if (ptr == NULL) {
		ksft_print_msg("FAIL: memory allocation\n");
		return KSFT_FAIL;
	}

	if (tags && !MT_FETCH_TAG((uintptr_t)ptr)) {
		ksft_print_msg("FAIL: tag not found at addr(%p)\n", ptr);
		mte_free_memory((void *)ptr, size, mem_type, false);
		return KSFT_FAIL;
	}

	return KSFT_PASS;
}

static inline int check_allocated_memory_range(void *ptr, size_t size, int mem_type,
					       size_t range_before, size_t range_after)
{
	if (ptr == NULL) {
		ksft_print_msg("FAIL: memory allocation\n");
		return KSFT_FAIL;
	}

	if (!MT_FETCH_TAG((uintptr_t)ptr)) {
		ksft_print_msg("FAIL: tag not found at addr(%p)\n", ptr);
		mte_free_memory_tag_range((void *)ptr, size, mem_type, range_before,
					  range_after);
		return KSFT_FAIL;
	}
	return KSFT_PASS;
}

#endif /* _MTE_COMMON_UTIL_H */
