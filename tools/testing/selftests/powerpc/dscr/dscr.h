/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * POWER Data Stream Control Register (DSCR)
 *
 * This header file contains helper functions and macros
 * required for all the DSCR related test cases.
 *
 * Copyright 2012, Anton Blanchard, IBM Corporation.
 * Copyright 2015, Anshuman Khandual, IBM Corporation.
 */
#ifndef _SELFTESTS_POWERPC_DSCR_DSCR_H
#define _SELFTESTS_POWERPC_DSCR_DSCR_H

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <pthread.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "reg.h"
#include "utils.h"

#define THREADS		100	/* Max threads */
#define COUNT		100	/* Max iterations */
#define DSCR_MAX	16	/* Max DSCR value */
#define LEN_MAX		100	/* Max name length */

#define DSCR_DEFAULT	"/sys/devices/system/cpu/dscr_default"
#define CPU_PATH	"/sys/devices/system/cpu/"

#define rmb()  asm volatile("lwsync":::"memory")
#define wmb()  asm volatile("lwsync":::"memory")

#define READ_ONCE(x) (*(volatile typeof(x) *)&(x))

/* Prilvilege state DSCR access */
inline unsigned long get_dscr(void)
{
	return mfspr(SPRN_DSCR_PRIV);
}

inline void set_dscr(unsigned long val)
{
	mtspr(SPRN_DSCR_PRIV, val);
}

/* Problem state DSCR access */
inline unsigned long get_dscr_usr(void)
{
	return mfspr(SPRN_DSCR);
}

inline void set_dscr_usr(unsigned long val)
{
	mtspr(SPRN_DSCR, val);
}

/* Default DSCR access */
unsigned long get_default_dscr(void)
{
	int err;
	unsigned long val;

	err = read_ulong(DSCR_DEFAULT, &val, 16);
	if (err) {
		perror("read() failed");
		exit(1);
	}
	return val;
}

void set_default_dscr(unsigned long val)
{
	int err;

	err = write_ulong(DSCR_DEFAULT, val, 16);
	if (err) {
		perror("write() failed");
		exit(1);
	}
}

double uniform_deviate(int seed)
{
	return seed * (1.0 / (RAND_MAX + 1.0));
}
#endif	/* _SELFTESTS_POWERPC_DSCR_DSCR_H */
