/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __ASM_GENERIC_SHMBUF_H
#define __ASM_GENERIC_SHMBUF_H

#include <asm/bitsperlong.h>

/*
 * The shmid64_ds structure for x86 architecture.
 * Note extra padding because this structure is passed back and forth
 * between kernel and user space.
 *
 * shmid64_ds was originally meant to be architecture specific, but
 * everyone just ended up making identical copies without specific
 * optimizations, so we may just as well all use the same one.
 *
 * 64 bit architectures typically define a 64 bit __kernel_time_t,
 * so they do not need the first two padding words.
 * On big-endian systems, the padding is in the wrong place.
 *
 *
 * Pad space is left for:
 * - 2 miscellaneous 32-bit values
 */

struct shmid64_ds {
	struct ipc64_perm	shm_perm;	/* operation perms */
	size_t			shm_segsz;	/* size of segment (bytes) */
#if __BITS_PER_LONG == 64
	__kernel_time_t		shm_atime;	/* last attach time */
	__kernel_time_t		shm_dtime;	/* last detach time */
	__kernel_time_t		shm_ctime;	/* last change time */
#else
	unsigned long		shm_atime;	/* last attach time */
	unsigned long		shm_atime_high;
	unsigned long		shm_dtime;	/* last detach time */
	unsigned long		shm_dtime_high;
	unsigned long		shm_ctime;	/* last change time */
	unsigned long		shm_ctime_high;
#endif
	__kernel_pid_t		shm_cpid;	/* pid of creator */
	__kernel_pid_t		shm_lpid;	/* pid of last operator */
	unsigned long		shm_nattch;	/* no. of current attaches */
	unsigned long		__unused4;
	unsigned long		__unused5;
};

struct shminfo64 {
	unsigned long		shmmax;
	unsigned long		shmmin;
	unsigned long		shmmni;
	unsigned long		shmseg;
	unsigned long		shmall;
	unsigned long		__unused1;
	unsigned long		__unused2;
	unsigned long		__unused3;
	unsigned long		__unused4;
};

#endif /* __ASM_GENERIC_SHMBUF_H */
