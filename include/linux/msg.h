#ifndef _LINUX_MSG_H
#define _LINUX_MSG_H

#include <linux/ipc.h>

/* ipcs ctl commands */
#define MSG_STAT 11
#define MSG_INFO 12

/* msgrcv options */
#define MSG_NOERROR     010000  /* no error if message is too big */
#define MSG_EXCEPT      020000  /* recv any msg except of specified type.*/

/* Obsolete, used only for backwards compatibility and libc5 compiles */
struct msqid_ds {
	struct ipc_perm msg_perm;
	struct msg *msg_first;		/* first message on queue,unused  */
	struct msg *msg_last;		/* last message in queue,unused */
	__kernel_time_t msg_stime;	/* last msgsnd time */
	__kernel_time_t msg_rtime;	/* last msgrcv time */
	__kernel_time_t msg_ctime;	/* last change time */
	unsigned long  msg_lcbytes;	/* Reuse junk fields for 32 bit */
	unsigned long  msg_lqbytes;	/* ditto */
	unsigned short msg_cbytes;	/* current number of bytes on queue */
	unsigned short msg_qnum;	/* number of messages in queue */
	unsigned short msg_qbytes;	/* max number of bytes on queue */
	__kernel_ipc_pid_t msg_lspid;	/* pid of last msgsnd */
	__kernel_ipc_pid_t msg_lrpid;	/* last receive pid */
};

/* Include the definition of msqid64_ds */
#include <asm/msgbuf.h>

/* message buffer for msgsnd and msgrcv calls */
struct msgbuf {
	long mtype;         /* type of message */
	char mtext[1];      /* message text */
};

/* buffer for msgctl calls IPC_INFO, MSG_INFO */
struct msginfo {
	int msgpool;
	int msgmap; 
	int msgmax; 
	int msgmnb; 
	int msgmni; 
	int msgssz; 
	int msgtql; 
	unsigned short  msgseg; 
};

/*
 * Scaling factor to compute msgmni:
 * the memory dedicated to msg queues (msgmni * msgmnb) should occupy
 * at most 1/MSG_MEM_SCALE of the lowmem (see the formula in ipc/msg.c):
 * up to 8MB       : msgmni = 16 (MSGMNI)
 * 4 GB            : msgmni = 8K
 * more than 16 GB : msgmni = 32K (IPCMNI)
 */
#define MSG_MEM_SCALE 32

#define MSGMNI    16   /* <= IPCMNI */     /* max # of msg queue identifiers */
#define MSGMAX  8192   /* <= INT_MAX */   /* max size of message (bytes) */
#define MSGMNB 16384   /* <= INT_MAX */   /* default max size of a message queue */

/* unused */
#define MSGPOOL (MSGMNI * MSGMNB) /* size in bytes of message pool */
#define MSGTQL  MSGMNB            /* number of system message headers */
#define MSGMAP  MSGMNB            /* number of entries in message map */
#define MSGSSZ  16                /* message segment size */
#define __MSGSEG (MSGPOOL / MSGSSZ) /* max no. of segments */
#define MSGSEG (__MSGSEG <= 0xffff ? __MSGSEG : 0xffff)

#ifdef __KERNEL__
#include <linux/list.h>

/* one msg_msg structure for each message */
struct msg_msg {
	struct list_head m_list; 
	long  m_type;          
	int m_ts;           /* message text size */
	struct msg_msgseg* next;
	void *security;
	/* the actual message follows immediately */
};

/* one msq_queue structure for each present queue on the system */
struct msg_queue {
	struct kern_ipc_perm q_perm;
	time_t q_stime;			/* last msgsnd time */
	time_t q_rtime;			/* last msgrcv time */
	time_t q_ctime;			/* last change time */
	unsigned long q_cbytes;		/* current number of bytes on queue */
	unsigned long q_qnum;		/* number of messages in queue */
	unsigned long q_qbytes;		/* max number of bytes on queue */
	pid_t q_lspid;			/* pid of last msgsnd */
	pid_t q_lrpid;			/* last receive pid */

	struct list_head q_messages;
	struct list_head q_receivers;
	struct list_head q_senders;
};

/* Helper routines for sys_msgsnd and sys_msgrcv */
extern long do_msgsnd(int msqid, long mtype, void __user *mtext,
			size_t msgsz, int msgflg);
extern long do_msgrcv(int msqid, long *pmtype, void __user *mtext,
			size_t msgsz, long msgtyp, int msgflg);

#endif /* __KERNEL__ */

#endif /* _LINUX_MSG_H */
