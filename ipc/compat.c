/*
 * 32 bit compatibility code for System V IPC
 *
 * Copyright (C) 1997,1998	Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997		David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1999		Arun Sharma <arun.sharma@intel.com>
 * Copyright (C) 2000		VA Linux Co
 * Copyright (C) 2000		Don Dugger <n0ano@valinux.com>
 * Copyright (C) 2000           Hewlett-Packard Co.
 * Copyright (C) 2000           David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 2000           Gerhard Tonn (ton@de.ibm.com)
 * Copyright (C) 2000-2002      Andi Kleen, SuSE Labs (x86-64 port)
 * Copyright (C) 2000		Silicon Graphics, Inc.
 * Copyright (C) 2001		IBM
 * Copyright (C) 2004		IBM Deutschland Entwicklung GmbH, IBM Corporation
 * Copyright (C) 2004		Arnd Bergmann (arnd@arndb.de)
 *
 * This code is collected from the versions for sparc64, mips64, s390x, ia64,
 * ppc64 and x86_64, all of which are based on the original sparc64 version
 * by Jakub Jelinek.
 *
 */
#include <linux/compat.h>
#include <linux/errno.h>
#include <linux/highuid.h>
#include <linux/init.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/syscalls.h>
#include <linux/ptrace.h>

#include <linux/mutex.h>
#include <linux/uaccess.h>

#include "util.h"

struct compat_msgbuf {
	compat_long_t mtype;
	char mtext[1];
};

struct compat_semid_ds {
	struct compat_ipc_perm sem_perm;
	compat_time_t sem_otime;
	compat_time_t sem_ctime;
	compat_uptr_t sem_base;
	compat_uptr_t sem_pending;
	compat_uptr_t sem_pending_last;
	compat_uptr_t undo;
	unsigned short sem_nsems;
};

struct compat_ipc_kludge {
	compat_uptr_t msgp;
	compat_long_t msgtyp;
};

static inline int __compat_ipc_parse_version(int *cmd)
{
#ifdef	CONFIG_ARCH_WANT_COMPAT_IPC_PARSE_VERSION
	int version = *cmd & IPC_64;

	/* this is tricky: architectures that have support for the old
	 * ipc structures in 64 bit binaries need to have IPC_64 set
	 * in cmd, the others need to have it cleared */
#ifndef ipc_parse_version
	*cmd |= IPC_64;
#else
	*cmd &= ~IPC_64;
#endif
	return version;
#else
	/* With the asm-generic APIs, we always use the 64-bit versions. */
	return IPC_64;
#endif
}

static inline int __get_compat_ipc64_perm(struct ipc64_perm *p64,
					  struct compat_ipc64_perm __user *up64)
{
	int err;

	err  = __get_user(p64->uid, &up64->uid);
	err |= __get_user(p64->gid, &up64->gid);
	err |= __get_user(p64->mode, &up64->mode);
	return err;
}

static inline int __get_compat_ipc_perm(struct ipc64_perm *p,
					struct compat_ipc_perm __user *up)
{
	int err;

	err  = __get_user(p->uid, &up->uid);
	err |= __get_user(p->gid, &up->gid);
	err |= __get_user(p->mode, &up->mode);
	return err;
}

static inline int __put_compat_ipc64_perm(struct ipc64_perm *p64,
					  struct compat_ipc64_perm __user *up64)
{
	int err;

	err  = __put_user(p64->key, &up64->key);
	err |= __put_user(p64->uid, &up64->uid);
	err |= __put_user(p64->gid, &up64->gid);
	err |= __put_user(p64->cuid, &up64->cuid);
	err |= __put_user(p64->cgid, &up64->cgid);
	err |= __put_user(p64->mode, &up64->mode);
	err |= __put_user(p64->seq, &up64->seq);
	return err;
}

static inline int __put_compat_ipc_perm(struct ipc64_perm *p,
					struct compat_ipc_perm __user *uip)
{
	int err;
	__compat_uid_t u;
	__compat_gid_t g;

	err  = __put_user(p->key, &uip->key);
	SET_UID(u, p->uid);
	err |= __put_user(u, &uip->uid);
	SET_GID(g, p->gid);
	err |= __put_user(g, &uip->gid);
	SET_UID(u, p->cuid);
	err |= __put_user(u, &uip->cuid);
	SET_GID(g, p->cgid);
	err |= __put_user(g, &uip->cgid);
	err |= __put_user(p->mode, &uip->mode);
	err |= __put_user(p->seq, &uip->seq);
	return err;
}

static inline int get_compat_semid64_ds(struct semid64_ds *sem64,
					struct compat_semid64_ds __user *up64)
{
	if (!access_ok(VERIFY_READ, up64, sizeof(*up64)))
		return -EFAULT;
	return __get_compat_ipc64_perm(&sem64->sem_perm, &up64->sem_perm);
}

static inline int get_compat_semid_ds(struct semid64_ds *s,
				      struct compat_semid_ds __user *up)
{
	if (!access_ok(VERIFY_READ, up, sizeof(*up)))
		return -EFAULT;
	return __get_compat_ipc_perm(&s->sem_perm, &up->sem_perm);
}

static inline int put_compat_semid64_ds(struct semid64_ds *sem64,
					struct compat_semid64_ds __user *up64)
{
	int err;

	if (!access_ok(VERIFY_WRITE, up64, sizeof(*up64)))
		return -EFAULT;
	err  = __put_compat_ipc64_perm(&sem64->sem_perm, &up64->sem_perm);
	err |= __put_user(sem64->sem_otime, &up64->sem_otime);
	err |= __put_user(sem64->sem_ctime, &up64->sem_ctime);
	err |= __put_user(sem64->sem_nsems, &up64->sem_nsems);
	return err;
}

static inline int put_compat_semid_ds(struct semid64_ds *s,
				      struct compat_semid_ds __user *up)
{
	int err;

	if (!access_ok(VERIFY_WRITE, up, sizeof(*up)))
		return -EFAULT;
	err  = __put_compat_ipc_perm(&s->sem_perm, &up->sem_perm);
	err |= __put_user(s->sem_otime, &up->sem_otime);
	err |= __put_user(s->sem_ctime, &up->sem_ctime);
	err |= __put_user(s->sem_nsems, &up->sem_nsems);
	return err;
}

static long do_compat_semctl(int first, int second, int third, u32 pad)
{
	unsigned long fourth;
	int err, err2;
	struct semid64_ds sem64;
	struct semid64_ds __user *up64;
	int version = __compat_ipc_parse_version(&third);

	memset(&sem64, 0, sizeof(sem64));

	if ((third & (~IPC_64)) == SETVAL)
#ifdef __BIG_ENDIAN
		fourth = (unsigned long)pad << 32;
#else
		fourth = pad;
#endif
	else
		fourth = (unsigned long)compat_ptr(pad);
	switch (third & (~IPC_64)) {
	case IPC_INFO:
	case IPC_RMID:
	case SEM_INFO:
	case GETVAL:
	case GETPID:
	case GETNCNT:
	case GETZCNT:
	case GETALL:
	case SETVAL:
	case SETALL:
		err = sys_semctl(first, second, third, fourth);
		break;

	case IPC_STAT:
	case SEM_STAT:
		up64 = compat_alloc_user_space(sizeof(sem64));
		fourth = (unsigned long)up64;
		err = sys_semctl(first, second, third, fourth);
		if (err < 0)
			break;
		if (copy_from_user(&sem64, up64, sizeof(sem64)))
			err2 = -EFAULT;
		else if (version == IPC_64)
			err2 = put_compat_semid64_ds(&sem64, compat_ptr(pad));
		else
			err2 = put_compat_semid_ds(&sem64, compat_ptr(pad));
		if (err2)
			err = -EFAULT;
		break;

	case IPC_SET:
		if (version == IPC_64)
			err = get_compat_semid64_ds(&sem64, compat_ptr(pad));
		else
			err = get_compat_semid_ds(&sem64, compat_ptr(pad));

		up64 = compat_alloc_user_space(sizeof(sem64));
		if (copy_to_user(up64, &sem64, sizeof(sem64)))
			err = -EFAULT;
		if (err)
			break;

		fourth = (unsigned long)up64;
		err = sys_semctl(first, second, third, fourth);
		break;

	default:
		err = -EINVAL;
		break;
	}
	return err;
}

static long compat_do_msg_fill(void __user *dest, struct msg_msg *msg, size_t bufsz)
{
	struct compat_msgbuf __user *msgp = dest;
	size_t msgsz;

	if (put_user(msg->m_type, &msgp->mtype))
		return -EFAULT;

	msgsz = (bufsz > msg->m_ts) ? msg->m_ts : bufsz;
	if (store_msg(msgp->mtext, msg, msgsz))
		return -EFAULT;
	return msgsz;
}

#ifndef COMPAT_SHMLBA
#define COMPAT_SHMLBA	SHMLBA
#endif

#ifdef CONFIG_ARCH_WANT_OLD_COMPAT_IPC
COMPAT_SYSCALL_DEFINE6(ipc, u32, call, int, first, int, second,
	u32, third, compat_uptr_t, ptr, u32, fifth)
{
	int version;
	u32 pad;

	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	switch (call) {
	case SEMOP:
		/* struct sembuf is the same on 32 and 64bit :)) */
		return sys_semtimedop(first, compat_ptr(ptr), second, NULL);
	case SEMTIMEDOP:
		return compat_sys_semtimedop(first, compat_ptr(ptr), second,
						compat_ptr(fifth));
	case SEMGET:
		return sys_semget(first, second, third);
	case SEMCTL:
		if (!ptr)
			return -EINVAL;
		if (get_user(pad, (u32 __user *) compat_ptr(ptr)))
			return -EFAULT;
		return do_compat_semctl(first, second, third, pad);

	case MSGSND: {
		struct compat_msgbuf __user *up = compat_ptr(ptr);
		compat_long_t type;

		if (first < 0 || second < 0)
			return -EINVAL;

		if (get_user(type, &up->mtype))
			return -EFAULT;

		return do_msgsnd(first, type, up->mtext, second, third);
	}
	case MSGRCV: {
		void __user *uptr = compat_ptr(ptr);

		if (first < 0 || second < 0)
			return -EINVAL;

		if (!version) {
			struct compat_ipc_kludge ipck;
			if (!uptr)
				return -EINVAL;
			if (copy_from_user(&ipck, uptr, sizeof(ipck)))
				return -EFAULT;
			uptr = compat_ptr(ipck.msgp);
			fifth = ipck.msgtyp;
		}
		return do_msgrcv(first, uptr, second, (s32)fifth, third,
				 compat_do_msg_fill);
	}
	case MSGGET:
		return sys_msgget(first, second);
	case MSGCTL:
		return compat_sys_msgctl(first, second, compat_ptr(ptr));

	case SHMAT: {
		int err;
		unsigned long raddr;

		if (version == 1)
			return -EINVAL;
		err = do_shmat(first, compat_ptr(ptr), second, &raddr,
			       COMPAT_SHMLBA);
		if (err < 0)
			return err;
		return put_user(raddr, (compat_ulong_t *)compat_ptr(third));
	}
	case SHMDT:
		return sys_shmdt(compat_ptr(ptr));
	case SHMGET:
		return sys_shmget(first, (unsigned)second, third);
	case SHMCTL:
		return compat_sys_shmctl(first, second, compat_ptr(ptr));
	}

	return -ENOSYS;
}
#endif

COMPAT_SYSCALL_DEFINE4(semctl, int, semid, int, semnum, int, cmd, int, arg)
{
	return do_compat_semctl(semid, semnum, cmd, arg);
}

COMPAT_SYSCALL_DEFINE4(msgsnd, int, msqid, compat_uptr_t, msgp,
		       compat_ssize_t, msgsz, int, msgflg)
{
	struct compat_msgbuf __user *up = compat_ptr(msgp);
	compat_long_t mtype;

	if (get_user(mtype, &up->mtype))
		return -EFAULT;
	return do_msgsnd(msqid, mtype, up->mtext, (ssize_t)msgsz, msgflg);
}

COMPAT_SYSCALL_DEFINE5(msgrcv, int, msqid, compat_uptr_t, msgp,
		       compat_ssize_t, msgsz, compat_long_t, msgtyp, int, msgflg)
{
	return do_msgrcv(msqid, compat_ptr(msgp), (ssize_t)msgsz, (long)msgtyp,
			 msgflg, compat_do_msg_fill);
}

COMPAT_SYSCALL_DEFINE3(shmat, int, shmid, compat_uptr_t, shmaddr, int, shmflg)
{
	unsigned long ret;
	long err;

	err = do_shmat(shmid, compat_ptr(shmaddr), shmflg, &ret, COMPAT_SHMLBA);
	if (err)
		return err;
	force_successful_syscall_return();
	return (long)ret;
}

COMPAT_SYSCALL_DEFINE4(semtimedop, int, semid, struct sembuf __user *, tsems,
		       unsigned, nsops,
		       const struct compat_timespec __user *, timeout)
{
	struct timespec __user *ts64;
	if (compat_convert_timespec(&ts64, timeout))
		return -EFAULT;
	return sys_semtimedop(semid, tsems, nsops, ts64);
}
