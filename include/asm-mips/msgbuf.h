#ifndef _ASM_MSGBUF_H
#define _ASM_MSGBUF_H

#include <linux/config.h>

/*
 * The msqid64_ds structure for the MIPS architecture.
 * Note extra padding because this structure is passed back and forth
 * between kernel and user space.
 *
 * Pad space is left for:
 * - extension of time_t to 64-bit on 32-bitsystem to solve the y2038 problem
 * - 2 miscellaneous unsigned long values
 */

struct msqid64_ds {
	struct ipc64_perm msg_perm;
#if defined(CONFIG_MIPS32) && !defined(CONFIG_CPU_LITTLE_ENDIAN)
	unsigned long	__unused1;
#endif
	__kernel_time_t msg_stime;	/* last msgsnd time */
#if defined(CONFIG_MIPS32) && defined(CONFIG_CPU_LITTLE_ENDIAN)
	unsigned long	__unused1;
#endif
#if defined(CONFIG_MIPS32) && !defined(CONFIG_CPU_LITTLE_ENDIAN)
	unsigned long	__unused2;
#endif
	__kernel_time_t msg_rtime;	/* last msgrcv time */
#if defined(CONFIG_MIPS32) && defined(CONFIG_CPU_LITTLE_ENDIAN)
	unsigned long	__unused2;
#endif
#if defined(CONFIG_MIPS32) && !defined(CONFIG_CPU_LITTLE_ENDIAN)
	unsigned long	__unused3;
#endif
	__kernel_time_t msg_ctime;	/* last change time */
#if defined(CONFIG_MIPS32) && defined(CONFIG_CPU_LITTLE_ENDIAN)
	unsigned long	__unused3;
#endif
	unsigned long  msg_cbytes;	/* current number of bytes on queue */
	unsigned long  msg_qnum;	/* number of messages in queue */
	unsigned long  msg_qbytes;	/* max number of bytes on queue */
	__kernel_pid_t msg_lspid;	/* pid of last msgsnd */
	__kernel_pid_t msg_lrpid;	/* last receive pid */
	unsigned long  __unused4;
	unsigned long  __unused5;
};

#endif /* _ASM_MSGBUF_H */
