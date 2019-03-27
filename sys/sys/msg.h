/* $FreeBSD$ */
/*	$NetBSD: msg.h,v 1.4 1994/06/29 06:44:43 cgd Exp $	*/

/*-
 * SVID compatible msg.h file
 *
 * Author:  Daniel Boulet
 *
 * Copyright 1993 Daniel Boulet and RTMX Inc.
 *
 * This system call was implemented by Daniel Boulet under contract from RTMX.
 *
 * Redistribution and use in source forms, with and without modification,
 * are permitted provided that this entire comment appears intact.
 *
 * Redistribution in binary form may occur without any restrictions.
 * Obviously, it would be nice if you gave credit where credit is due
 * but requiring it would be too onerous.
 *
 * This software is provided ``AS IS'' without any warranties of any kind.
 */

#ifndef _SYS_MSG_H_
#define _SYS_MSG_H_

#include <sys/cdefs.h>
#include <sys/_types.h>
#ifdef _WANT_SYSVMSG_INTERNALS
#define	_WANT_SYSVIPC_INTERNALS
#endif
#include <sys/ipc.h>

/*
 * The MSG_NOERROR identifier value, the msqid_ds struct and the msg struct
 * are as defined by the SV API Intel 386 Processor Supplement.
 */

#define MSG_NOERROR	010000		/* don't complain about too long msgs */

typedef	unsigned long	msglen_t;
typedef	unsigned long	msgqnum_t;

#ifndef _PID_T_DECLARED
typedef	__pid_t		pid_t;
#define	_PID_T_DECLARED
#endif

#ifndef _SIZE_T_DECLARED
typedef	__size_t	size_t;
#define	_SIZE_T_DECLARED
#endif

#ifndef _SSIZE_T_DECLARED
typedef	__ssize_t	ssize_t;
#define	_SSIZE_T_DECLARED
#endif

#ifndef _TIME_T_DECLARED
typedef	__time_t	time_t;
#define	_TIME_T_DECLARED
#endif

#if defined(COMPAT_FREEBSD4) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD7)
struct msqid_ds_old {
	struct	ipc_perm_old msg_perm;	/* msg queue permission bits */
	struct	msg *__msg_first;	/* first message in the queue */
	struct	msg *__msg_last;	/* last message in the queue */
	msglen_t msg_cbytes;	/* number of bytes in use on the queue */
	msgqnum_t msg_qnum;	/* number of msgs in the queue */
	msglen_t msg_qbytes;	/* max # of bytes on the queue */
	pid_t	msg_lspid;	/* pid of last msgsnd() */
	pid_t	msg_lrpid;	/* pid of last msgrcv() */
	time_t	msg_stime;	/* time of last msgsnd() */
	long	msg_pad1;
	time_t	msg_rtime;	/* time of last msgrcv() */
	long	msg_pad2;
	time_t	msg_ctime;	/* time of last msgctl() */
	long	msg_pad3;
	long	msg_pad4[4];
};
#endif

/*
 * XXX there seems to be no prefix reserved for this header, so the name
 * "msg" in "struct msg" and the names of all of the nonstandard members
 * are namespace pollution.
 */

struct msqid_ds {
	struct	ipc_perm msg_perm;	/* msg queue permission bits */
	struct	msg *__msg_first;	/* first message in the queue */
	struct	msg *__msg_last;	/* last message in the queue */
	msglen_t msg_cbytes;	/* number of bytes in use on the queue */
	msgqnum_t msg_qnum;	/* number of msgs in the queue */
	msglen_t msg_qbytes;	/* max # of bytes on the queue */
	pid_t	msg_lspid;	/* pid of last msgsnd() */
	pid_t	msg_lrpid;	/* pid of last msgrcv() */
	time_t	msg_stime;	/* time of last msgsnd() */
	time_t	msg_rtime;	/* time of last msgrcv() */
	time_t	msg_ctime;	/* time of last msgctl() */
};

#ifdef _KERNEL
struct msg {
	struct	msg *msg_next;  /* next msg in the chain */
	long	msg_type; 	/* type of this message */
				/* >0 -> type of this message */
				/* 0 -> free header */
	u_short	msg_ts;		/* size of this message */
	short	msg_spot;	/* location of start of msg in buffer */
	struct	label *label;	/* MAC Framework label */
};
#endif

#if defined(_KERNEL) || defined(_WANT_SYSVMSG_INTERNALS)
/*
 * Based on the configuration parameters described in an SVR2 (yes, two)
 * config(1m) man page.
 *
 * Each message is broken up and stored in segments that are msgssz bytes
 * long.  For efficiency reasons, this should be a power of two.  Also,
 * it doesn't make sense if it is less than 8 or greater than about 256.
 * Consequently, msginit in kern/sysv_msg.c checks that msgssz is a power of
 * two between 8 and 1024 inclusive (and panic's if it isn't).
 */
struct msginfo {
	int	msgmax;		/* max chars in a message */
	int	msgmni;		/* max message queue identifiers */
	int	msgmnb;		/* max chars in a queue */
	int	msgtql;		/* max messages in system */
	int	msgssz;		/* size of a message segment (see note) */
	int	msgseg;		/* number of message segments */
};

/*
 * Kernel wrapper for the user-level structure.
 */
struct msqid_kernel {
	/*
	 * Data structure exposed to user space.
	 */
	struct	msqid_ds u;

	/*
	 * Kernel-private components of the message queue.
	 */
	struct	label *label;	/* MAC label */
	struct	ucred *cred;	/* creator's credentials */
};
#endif

#ifdef _KERNEL
extern struct msginfo	msginfo;

#else /* _KERNEL */

__BEGIN_DECLS
int msgctl(int, int, struct msqid_ds *);
int msgget(key_t, int);
ssize_t msgrcv(int, void *, size_t, long, int);
int msgsnd(int, const void *, size_t, int);
#if __BSD_VISIBLE
int msgsys(int, ...);
#endif
__END_DECLS
#endif /* !_KERNEL */

#endif /* !_SYS_MSG_H_ */
