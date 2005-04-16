#ifndef _PPC_MSGBUF_H
#define _PPC_MSGBUF_H

/*
 * The msqid64_ds structure for the PPC architecture.
 */

struct msqid64_ds {
	struct ipc64_perm msg_perm;
	unsigned int	__unused1;
	__kernel_time_t msg_stime;	/* last msgsnd time */
	unsigned int	__unused2;
	__kernel_time_t msg_rtime;	/* last msgrcv time */
	unsigned int	__unused3;
	__kernel_time_t msg_ctime;	/* last change time */
	unsigned long  msg_cbytes;	/* current number of bytes on queue */
	unsigned long  msg_qnum;	/* number of messages in queue */
	unsigned long  msg_qbytes;	/* max number of bytes on queue */
	__kernel_pid_t msg_lspid;	/* pid of last msgsnd */
	__kernel_pid_t msg_lrpid;	/* last receive pid */
	unsigned long  __unused4;
	unsigned long  __unused5;
};

#endif /* _PPC_MSGBUF_H */
