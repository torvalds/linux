#ifndef __ASM_SH64_MSGBUF_H
#define __ASM_SH64_MSGBUF_H

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * include/asm-sh64/msgbuf.h
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 *
 */

/*
 * The msqid64_ds structure for i386 architecture.
 * Note extra padding because this structure is passed back and forth
 * between kernel and user space.
 *
 * Pad space is left for:
 * - 64-bit time_t to solve y2038 problem
 * - 2 miscellaneous 32-bit values
 */

struct msqid64_ds {
	struct ipc64_perm msg_perm;
	__kernel_time_t msg_stime;	/* last msgsnd time */
	unsigned long	__unused1;
	__kernel_time_t msg_rtime;	/* last msgrcv time */
	unsigned long	__unused2;
	__kernel_time_t msg_ctime;	/* last change time */
	unsigned long	__unused3;
	unsigned long  msg_cbytes;	/* current number of bytes on queue */
	unsigned long  msg_qnum;	/* number of messages in queue */
	unsigned long  msg_qbytes;	/* max number of bytes on queue */
	__kernel_pid_t msg_lspid;	/* pid of last msgsnd */
	__kernel_pid_t msg_lrpid;	/* last receive pid */
	unsigned long  __unused4;
	unsigned long  __unused5;
};

#endif /* __ASM_SH64_MSGBUF_H */
