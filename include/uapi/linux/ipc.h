/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_IPC_H
#define _UAPI_LINUX_IPC_H

#include <linux/types.h>

#define IPC_PRIVATE ((__kernel_key_t) 0)  

/* Obsolete, used only for backwards compatibility and libc5 compiles */
struct ipc_perm
{
	__kernel_key_t	key;
	__kernel_uid_t	uid;
	__kernel_gid_t	gid;
	__kernel_uid_t	cuid;
	__kernel_gid_t	cgid;
	__kernel_mode_t	mode; 
	unsigned short	seq;
};

/* Include the definition of ipc64_perm */
#include <asm/ipcbuf.h>

/* resource get request flags */
#define IPC_CREAT  00001000   /* create if key is nonexistent */
#define IPC_EXCL   00002000   /* fail if key exists */
#define IPC_NOWAIT 00004000   /* return error on wait */

/* these fields are used by the DIPC package so the kernel as standard
   should avoid using them if possible */
   
#define IPC_DIPC 00010000  /* make it distributed */
#define IPC_OWN  00020000  /* this machine is the DIPC owner */

/* 
 * Control commands used with semctl, msgctl and shmctl 
 * see also specific commands in sem.h, msg.h and shm.h
 */
#define IPC_RMID 0     /* remove resource */
#define IPC_SET  1     /* set ipc_perm options */
#define IPC_STAT 2     /* get ipc_perm options */
#define IPC_INFO 3     /* see ipcs */

/*
 * Version flags for semctl, msgctl, and shmctl commands
 * These are passed as bitflags or-ed with the actual command
 */
#define IPC_OLD 0	/* Old version (no 32-bit UID support on many
			   architectures) */
#define IPC_64  0x0100  /* New version (support 32-bit UIDs, bigger
			   message sizes, etc. */

/*
 * These are used to wrap system calls.
 *
 * See architecture code for ugly details..
 */
struct ipc_kludge {
	struct msgbuf __user *msgp;
	long msgtyp;
};

#define SEMOP		 1
#define SEMGET		 2
#define SEMCTL		 3
#define SEMTIMEDOP	 4
#define MSGSND		11
#define MSGRCV		12
#define MSGGET		13
#define MSGCTL		14
#define SHMAT		21
#define SHMDT		22
#define SHMGET		23
#define SHMCTL		24

/* Used by the DIPC package, try and avoid reusing it */
#define DIPC            25

#define IPCCALL(version,op)	((version)<<16 | (op))


#endif /* _UAPI_LINUX_IPC_H */
