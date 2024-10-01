/* SPDX-License-Identifier: GPL-2.0+ */

/*
 * Copyright 2018 IBM Corporation.
 */

#ifndef _SELFTESTS_POWERPC_SECURITY_FLUSH_UTILS_H
#define _SELFTESTS_POWERPC_SECURITY_FLUSH_UTILS_H

#define CACHELINE_SIZE 128

#define PERF_L1D_READ_MISS_CONFIG	((PERF_COUNT_HW_CACHE_L1D) | 		\
					(PERF_COUNT_HW_CACHE_OP_READ << 8) |	\
					(PERF_COUNT_HW_CACHE_RESULT_MISS << 16))

void syscall_loop(char *p, unsigned long iterations,
		  unsigned long zero_size);

void syscall_loop_uaccess(char *p, unsigned long iterations,
			  unsigned long zero_size);

void set_dscr(unsigned long val);

#endif /* _SELFTESTS_POWERPC_SECURITY_FLUSH_UTILS_H */
