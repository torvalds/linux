#ifndef __ASM_GENERIC_MSGBUF_H
#define __ASM_GENERIC_MSGBUF_H

#include <asm/bitsperlong.h>
/*
 * generic msqid64_ds structure.
 *
 * Note extra padding because this structure is passed back and forth
 * between kernel and user space.
 *
 * msqid64_ds was originally meant to be architecture specific, but
 * everyone just ended up making identical copies without specific
 * optimizations, so we may just as well all use the same one.
 *
 * 64 bit architectures typically define a 64 bit __kernel_time_t,
 * so they do not need the first three padding words.
 * On big-endian systems, the padding is in the wrong place.
 *
 * Pad space is left for:
 * - 64-bit time_t to solve y2038 problem
 * - 2 miscellaneous 32-bit values
 */

struct msqid64_ds {
	struct ipc64_perm msg_perm;
	__kernel_time_t msg_stime;	/* last msgsnd time */
#if __BITS_PER_LONG != 64
	unsigned long	__unused1;
#endif
	__kernel_time_t msg_rtime;	/* last msgrcv time */
#if __BITS_PER_LONG != 64
	unsigned long	__unused2;
#endif
	__kernel_time_t msg_ctime;	/* last change time */
#if __BITS_PER_LONG != 64
	unsigned long	__unused3;
#endif
	__kernel_ulong_t msg_cbytes;	/* current number of bytes on queue */
	__kernel_ulong_t msg_qnum;	/* number of messages in queue */
	__kernel_ulong_t msg_qbytes;	/* max number of bytes on queue */
	__kernel_pid_t msg_lspid;	/* pid of last msgsnd */
	__kernel_pid_t msg_lrpid;	/* last receive pid */
	__kernel_ulong_t __unused4;
	__kernel_ulong_t __unused5;
};

#endif /* __ASM_GENERIC_MSGBUF_H */
