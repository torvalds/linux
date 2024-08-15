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
	unsigned long ret;

	asm volatile("mfspr %0,%1" : "=r" (ret) : "i" (SPRN_DSCR_PRIV));

	return ret;
}

inline void set_dscr(unsigned long val)
{
	asm volatile("mtspr %1,%0" : : "r" (val), "i" (SPRN_DSCR_PRIV));
}

/* Problem state DSCR access */
inline unsigned long get_dscr_usr(void)
{
	unsigned long ret;

	asm volatile("mfspr %0,%1" : "=r" (ret) : "i" (SPRN_DSCR));

	return ret;
}

inline void set_dscr_usr(unsigned long val)
{
	asm volatile("mtspr %1,%0" : : "r" (val), "i" (SPRN_DSCR));
}

/* Default DSCR access */
unsigned long get_default_dscr(void)
{
	int fd = -1, ret;
	char buf[16];
	unsigned long val;

	if (fd == -1) {
		fd = open(DSCR_DEFAULT, O_RDONLY);
		if (fd == -1) {
			perror("open() failed");
			exit(1);
		}
	}
	memset(buf, 0, sizeof(buf));
	lseek(fd, 0, SEEK_SET);
	ret = read(fd, buf, sizeof(buf));
	if (ret == -1) {
		perror("read() failed");
		exit(1);
	}
	sscanf(buf, "%lx", &val);
	close(fd);
	return val;
}

void set_default_dscr(unsigned long val)
{
	int fd = -1, ret;
	char buf[16];

	if (fd == -1) {
		fd = open(DSCR_DEFAULT, O_RDWR);
		if (fd == -1) {
			perror("open() failed");
			exit(1);
		}
	}
	sprintf(buf, "%lx\n", val);
	ret = write(fd, buf, strlen(buf));
	if (ret == -1) {
		perror("write() failed");
		exit(1);
	}
	close(fd);
}

double uniform_deviate(int seed)
{
	return seed * (1.0 / (RAND_MAX + 1.0));
}
#endif	/* _SELFTESTS_POWERPC_DSCR_DSCR_H */
