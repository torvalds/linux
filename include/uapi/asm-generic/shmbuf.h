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
 * - 64-bit time_t to solve y2038 problem
 * - 2 miscellaneous 32-bit values
 */

struct shmid64_ds {
	struct ipc64_perm	shm_perm;	/* operation perms */
	size_t			shm_segsz;	/* size of segment (bytes) */
	__kernel_time_t		shm_atime;	/* last attach time */
#if __BITS_PER_LONG != 64
	unsigned long		__unused1;
#endif
	__kernel_time_t		shm_dtime;	/* last detach time */
#if __BITS_PER_LONG != 64
	unsigned long		__unused2;
#endif
	__kernel_time_t		shm_ctime;	/* last change time */
#if __BITS_PER_LONG != 64
	unsigned long		__unused3;
#endif
	__kernel_pid_t		shm_cpid;	/* pid of creator */
	__kernel_pid_t		shm_lpid;	/* pid of last operator */
	__kernel_ulong_t	shm_nattch;	/* no. of current attaches */
	__kernel_ulong_t	__unused4;
	__kernel_ulong_t	__unused5;
};

struct shminfo64 {
	__kernel_ulong_t	shmmax;
	__kernel_ulong_t	shmmin;
	__kernel_ulong_t	shmmni;
	__kernel_ulong_t	shmseg;
	__kernel_ulong_t	shmall;
	__kernel_ulong_t	__unused1;
	__kernel_ulong_t	__unused2;
	__kernel_ulong_t	__unused3;
	__kernel_ulong_t	__unused4;
};

#endif /* __ASM_GENERIC_SHMBUF_H */
