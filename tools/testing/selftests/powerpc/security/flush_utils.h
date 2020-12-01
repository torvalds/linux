/* SPDX-License-Identifier: GPL-2.0+ */

/*
 * Copyright 2018 IBM Corporation.
 */

#ifndef _SELFTESTS_POWERPC_SECURITY_FLUSH_UTILS_H
#define _SELFTESTS_POWERPC_SECURITY_FLUSH_UTILS_H

#define CACHELINE_SIZE 128

void syscall_loop(char *p, unsigned long iterations,
		  unsigned long zero_size);

void set_dscr(unsigned long val);

#endif /* _SELFTESTS_POWERPC_SECURITY_FLUSH_UTILS_H */
