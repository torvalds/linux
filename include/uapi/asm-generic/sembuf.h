#ifndef __ASM_GENERIC_SEMBUF_H
#define __ASM_GENERIC_SEMBUF_H

#include <asm/bitsperlong.h>

/*
 * The semid64_ds structure for x86 architecture.
 * Note extra padding because this structure is passed back and forth
 * between kernel and user space.
 *
 * semid64_ds was originally meant to be architecture specific, but
 * everyone just ended up making identical copies without specific
 * optimizations, so we may just as well all use the same one.
 *
 * 64 bit architectures typically define a 64 bit __kernel_time_t,
 * so they do not need the first two padding words.
 * On big-endian systems, the padding is in the wrong place.
 *
 * Pad space is left for:
 * - 64-bit time_t to solve y2038 problem
 * - 2 miscellaneous 32-bit values
 */
struct semid64_ds {
	struct ipc64_perm sem_perm;	/* permissions .. see ipc.h */
	__kernel_time_t	sem_otime;	/* last semop time */
#if __BITS_PER_LONG != 64
	unsigned long	__unused1;
#endif
	__kernel_time_t	sem_ctime;	/* last change time */
#if __BITS_PER_LONG != 64
	unsigned long	__unused2;
#endif
	unsigned long	sem_nsems;	/* no. of semaphores in array */
	unsigned long	__unused3;
	unsigned long	__unused4;
};

#endif /* __ASM_GENERIC_SEMBUF_H */
