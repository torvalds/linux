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
 * 64 bit architectures use a 64-bit long time field here, while
 * 32 bit architectures have a pair of unsigned long values.
 * On big-endian systems, the lower half is in the wrong place.
 *
 * Pad space is left for:
 * - 2 miscellaneous 32-bit values
 */

struct msqid64_ds {
	struct ipc64_perm msg_perm;
#if __BITS_PER_LONG == 64
	long		 msg_stime;	/* last msgsnd time */
	long		 msg_rtime;	/* last msgrcv time */
	long		 msg_ctime;	/* last change time */
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
