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

int get_compat_ipc64_perm(struct ipc64_perm *to,
			  struct compat_ipc64_perm __user *from)
{
	struct compat_ipc64_perm v;
	if (copy_from_user(&v, from, sizeof(v)))
		return -EFAULT;
	to->uid = v.uid;
	to->gid = v.gid;
	to->mode = v.mode;
	return 0;
}

int get_compat_ipc_perm(struct ipc64_perm *to,
			struct compat_ipc_perm __user *from)
{
	struct compat_ipc_perm v;
	if (copy_from_user(&v, from, sizeof(v)))
		return -EFAULT;
	to->uid = v.uid;
	to->gid = v.gid;
	to->mode = v.mode;
	return 0;
}

void to_compat_ipc64_perm(struct compat_ipc64_perm *to, struct ipc64_perm *from)
{
	to->key = from->key;
	to->uid = from->uid;
	to->gid = from->gid;
	to->cuid = from->cuid;
	to->cgid = from->cgid;
	to->mode = from->mode;
	to->seq = from->seq;
}

void to_compat_ipc_perm(struct compat_ipc_perm *to, struct ipc64_perm *from)
{
	to->key = from->key;
	SET_UID(to->uid, from->uid);
	SET_GID(to->gid, from->gid);
	SET_UID(to->cuid, from->cuid);
	SET_GID(to->cgid, from->cgid);
	to->mode = from->mode;
	to->seq = from->seq;
}
