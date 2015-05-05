/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
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
 * - 2 miscellaneous 32-bit values
 */

struct msqid64_ds {
	struct ipc64_perm msg_perm;
#if __BITS_PER_LONG == 64
	__kernel_time_t msg_stime;	/* last msgsnd time */
	__kernel_time_t msg_rtime;	/* last msgrcv time */
	__kernel_time_t msg_ctime;	/* last change time */
#else
	unsigned long	msg_stime;	/* last msgsnd time */
	unsigned long	msg_stime_high;
	unsigned long	msg_rtime;	/* last msgrcv time */
	unsigned long	msg_rtime_high;
	unsigned long	msg_ctime;	/* last change time */
	unsigned long	msg_ctime_high;
#endif
	unsigned long	msg_cbytes;	/* current number of bytes on queue */
	unsigned long	msg_qnum;	/* number of messages in queue */
	unsigned long	 msg_qbytes;	/* max number of bytes on queue */
	__kernel_pid_t msg_lspid;	/* pid of last msgsnd */
	__kernel_pid_t msg_lrpid;	/* last receive pid */
	unsigned long	 __unused4;
	unsigned long	 __unused5;
};

#endif /* __ASM_GENERIC_MSGBUF_H */
