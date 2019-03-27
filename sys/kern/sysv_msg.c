/*-
 * Implementation of SVID messages
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
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2005 McAfee, Inc.
 * Copyright (c) 2016-2017 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project in part by McAfee
 * Research, the Security Research Division of McAfee, Inc under DARPA/SPAWAR
 * contract N66001-01-C-8035 ("CBOSS"), as part of the DARPA CHATS research
 * program.
 *
 * Portions of this software were developed by BAE Systems, the University of
 * Cambridge Computer Laboratory, and Memorial University under DARPA/AFRL
 * contract FA8650-15-C-7558 ("CADETS"), as part of the DARPA Transparent
 * Computing (TC) research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_sysvipc.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/kernel.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/module.h>
#include <sys/mount.h>
#include <sys/msg.h>
#include <sys/racct.h>
#include <sys/sx.h>
#include <sys/syscall.h>
#include <sys/syscallsubr.h>
#include <sys/sysent.h>
#include <sys/sysctl.h>
#include <sys/malloc.h>
#include <sys/jail.h>

#include <security/audit/audit.h>
#include <security/mac/mac_framework.h>

FEATURE(sysv_msg, "System V message queues support");

static MALLOC_DEFINE(M_MSG, "msg", "SVID compatible message queues");

static int msginit(void);
static int msgunload(void);
static int sysvmsg_modload(struct module *, int, void *);
static void msq_remove(struct msqid_kernel *);
static struct prison *msg_find_prison(struct ucred *);
static int msq_prison_cansee(struct prison *, struct msqid_kernel *);
static int msg_prison_check(void *, void *);
static int msg_prison_set(void *, void *);
static int msg_prison_get(void *, void *);
static int msg_prison_remove(void *, void *);
static void msg_prison_cleanup(struct prison *);


#ifdef MSG_DEBUG
#define DPRINTF(a)	printf a
#else
#define DPRINTF(a)	(void)0
#endif

static void msg_freehdr(struct msg *msghdr);

#ifndef MSGSSZ
#define MSGSSZ	8		/* Each segment must be 2^N long */
#endif
#ifndef MSGSEG
#define MSGSEG	2048		/* must be less than 32767 */
#endif
#define MSGMAX	(MSGSSZ*MSGSEG)
#ifndef MSGMNB
#define MSGMNB	2048		/* max # of bytes in a queue */
#endif
#ifndef MSGMNI
#define MSGMNI	40
#endif
#ifndef MSGTQL
#define MSGTQL	40
#endif

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
struct msginfo msginfo = {
                MSGMAX,         /* max chars in a message */
                MSGMNI,         /* # of message queue identifiers */
                MSGMNB,         /* max chars in a queue */
                MSGTQL,         /* max messages in system */
                MSGSSZ,         /* size of a message segment */
                		/* (must be small power of 2 greater than 4) */
                MSGSEG          /* number of message segments */
};

/*
 * macros to convert between msqid_ds's and msqid's.
 * (specific to this implementation)
 */
#define MSQID(ix,ds)	((ix) & 0xffff | (((ds).msg_perm.seq << 16) & 0xffff0000))
#define MSQID_IX(id)	((id) & 0xffff)
#define MSQID_SEQ(id)	(((id) >> 16) & 0xffff)

/*
 * The rest of this file is specific to this particular implementation.
 */

struct msgmap {
	short	next;		/* next segment in buffer */
    				/* -1 -> available */
    				/* 0..(MSGSEG-1) -> index of next segment */
};

#define MSG_LOCKED	01000	/* Is this msqid_ds locked? */

static int nfree_msgmaps;	/* # of free map entries */
static short free_msgmaps;	/* head of linked list of free map entries */
static struct msg *free_msghdrs;/* list of free msg headers */
static char *msgpool;		/* MSGMAX byte long msg buffer pool */
static struct msgmap *msgmaps;	/* MSGSEG msgmap structures */
static struct msg *msghdrs;	/* MSGTQL msg headers */
static struct msqid_kernel *msqids;	/* MSGMNI msqid_kernel struct's */
static struct mtx msq_mtx;	/* global mutex for message queues. */
static unsigned msg_prison_slot;/* prison OSD slot */

static struct syscall_helper_data msg_syscalls[] = {
	SYSCALL_INIT_HELPER(msgctl),
	SYSCALL_INIT_HELPER(msgget),
	SYSCALL_INIT_HELPER(msgsnd),
	SYSCALL_INIT_HELPER(msgrcv),
#if defined(COMPAT_FREEBSD4) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD7)
	SYSCALL_INIT_HELPER(msgsys),
	SYSCALL_INIT_HELPER_COMPAT(freebsd7_msgctl),
#endif
	SYSCALL_INIT_LAST
};

#ifdef COMPAT_FREEBSD32
#include <compat/freebsd32/freebsd32.h>
#include <compat/freebsd32/freebsd32_ipc.h>
#include <compat/freebsd32/freebsd32_proto.h>
#include <compat/freebsd32/freebsd32_signal.h>
#include <compat/freebsd32/freebsd32_syscall.h>
#include <compat/freebsd32/freebsd32_util.h>

static struct syscall_helper_data msg32_syscalls[] = {
	SYSCALL32_INIT_HELPER(freebsd32_msgctl),
	SYSCALL32_INIT_HELPER(freebsd32_msgsnd),
	SYSCALL32_INIT_HELPER(freebsd32_msgrcv),
	SYSCALL32_INIT_HELPER_COMPAT(msgget),
	SYSCALL32_INIT_HELPER(freebsd32_msgsys),
#if defined(COMPAT_FREEBSD4) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD7)
	SYSCALL32_INIT_HELPER(freebsd7_freebsd32_msgctl),
#endif
	SYSCALL_INIT_LAST
};
#endif

static int
msginit()
{
	struct prison *pr;
	void **rsv;
	int i, error;
	osd_method_t methods[PR_MAXMETHOD] = {
	    [PR_METHOD_CHECK] =		msg_prison_check,
	    [PR_METHOD_SET] =		msg_prison_set,
	    [PR_METHOD_GET] =		msg_prison_get,
	    [PR_METHOD_REMOVE] =	msg_prison_remove,
	};

	msginfo.msgmax = msginfo.msgseg * msginfo.msgssz;
	msgpool = malloc(msginfo.msgmax, M_MSG, M_WAITOK);
	msgmaps = malloc(sizeof(struct msgmap) * msginfo.msgseg, M_MSG, M_WAITOK);
	msghdrs = malloc(sizeof(struct msg) * msginfo.msgtql, M_MSG, M_WAITOK);
	msqids = malloc(sizeof(struct msqid_kernel) * msginfo.msgmni, M_MSG,
	    M_WAITOK | M_ZERO);

	/*
	 * msginfo.msgssz should be a power of two for efficiency reasons.
	 * It is also pretty silly if msginfo.msgssz is less than 8
	 * or greater than about 256 so ...
	 */

	i = 8;
	while (i < 1024 && i != msginfo.msgssz)
		i <<= 1;
    	if (i != msginfo.msgssz) {
		DPRINTF(("msginfo.msgssz=%d (0x%x)\n", msginfo.msgssz,
		    msginfo.msgssz));
		panic("msginfo.msgssz not a small power of 2");
	}

	if (msginfo.msgseg > 32767) {
		DPRINTF(("msginfo.msgseg=%d\n", msginfo.msgseg));
		panic("msginfo.msgseg > 32767");
	}

	for (i = 0; i < msginfo.msgseg; i++) {
		if (i > 0)
			msgmaps[i-1].next = i;
		msgmaps[i].next = -1;	/* implies entry is available */
	}
	free_msgmaps = 0;
	nfree_msgmaps = msginfo.msgseg;

	for (i = 0; i < msginfo.msgtql; i++) {
		msghdrs[i].msg_type = 0;
		if (i > 0)
			msghdrs[i-1].msg_next = &msghdrs[i];
		msghdrs[i].msg_next = NULL;
#ifdef MAC
		mac_sysvmsg_init(&msghdrs[i]);
#endif
    	}
	free_msghdrs = &msghdrs[0];

	for (i = 0; i < msginfo.msgmni; i++) {
		msqids[i].u.msg_qbytes = 0;	/* implies entry is available */
		msqids[i].u.msg_perm.seq = 0;	/* reset to a known value */
		msqids[i].u.msg_perm.mode = 0;
#ifdef MAC
		mac_sysvmsq_init(&msqids[i]);
#endif
	}
	mtx_init(&msq_mtx, "msq", NULL, MTX_DEF);

	/* Set current prisons according to their allow.sysvipc. */
	msg_prison_slot = osd_jail_register(NULL, methods);
	rsv = osd_reserve(msg_prison_slot);
	prison_lock(&prison0);
	(void)osd_jail_set_reserved(&prison0, msg_prison_slot, rsv, &prison0);
	prison_unlock(&prison0);
	rsv = NULL;
	sx_slock(&allprison_lock);
	TAILQ_FOREACH(pr, &allprison, pr_list) {
		if (rsv == NULL)
			rsv = osd_reserve(msg_prison_slot);
		prison_lock(pr);
		if ((pr->pr_allow & PR_ALLOW_SYSVIPC) && pr->pr_ref > 0) {
			(void)osd_jail_set_reserved(pr, msg_prison_slot, rsv,
			    &prison0);
			rsv = NULL;
		}
		prison_unlock(pr);
	}
	if (rsv != NULL)
		osd_free_reserved(rsv);
	sx_sunlock(&allprison_lock);

	error = syscall_helper_register(msg_syscalls, SY_THR_STATIC_KLD);
	if (error != 0)
		return (error);
#ifdef COMPAT_FREEBSD32
	error = syscall32_helper_register(msg32_syscalls, SY_THR_STATIC_KLD);
	if (error != 0)
		return (error);
#endif
	return (0);
}

static int
msgunload()
{
	struct msqid_kernel *msqkptr;
	int msqid;
#ifdef MAC
	int i;
#endif

	syscall_helper_unregister(msg_syscalls);
#ifdef COMPAT_FREEBSD32
	syscall32_helper_unregister(msg32_syscalls);
#endif

	for (msqid = 0; msqid < msginfo.msgmni; msqid++) {
		msqkptr = &msqids[msqid];
		if (msqkptr->u.msg_qbytes != 0 ||
		    (msqkptr->u.msg_perm.mode & MSG_LOCKED) != 0)
			break;
	}
	if (msqid != msginfo.msgmni)
		return (EBUSY);

	if (msg_prison_slot != 0)
		osd_jail_deregister(msg_prison_slot);
#ifdef MAC
	for (i = 0; i < msginfo.msgtql; i++)
		mac_sysvmsg_destroy(&msghdrs[i]);
	for (msqid = 0; msqid < msginfo.msgmni; msqid++)
		mac_sysvmsq_destroy(&msqids[msqid]);
#endif
	free(msgpool, M_MSG);
	free(msgmaps, M_MSG);
	free(msghdrs, M_MSG);
	free(msqids, M_MSG);
	mtx_destroy(&msq_mtx);
	return (0);
}


static int
sysvmsg_modload(struct module *module, int cmd, void *arg)
{
	int error = 0;

	switch (cmd) {
	case MOD_LOAD:
		error = msginit();
		if (error != 0)
			msgunload();
		break;
	case MOD_UNLOAD:
		error = msgunload();
		break;
	case MOD_SHUTDOWN:
		break;
	default:
		error = EINVAL;
		break;
	}
	return (error);
}

static moduledata_t sysvmsg_mod = {
	"sysvmsg",
	&sysvmsg_modload,
	NULL
};

DECLARE_MODULE(sysvmsg, sysvmsg_mod, SI_SUB_SYSV_MSG, SI_ORDER_FIRST);
MODULE_VERSION(sysvmsg, 1);

static void
msg_freehdr(struct msg *msghdr)
{
	while (msghdr->msg_ts > 0) {
		short next;
		if (msghdr->msg_spot < 0 || msghdr->msg_spot >= msginfo.msgseg)
			panic("msghdr->msg_spot out of range");
		next = msgmaps[msghdr->msg_spot].next;
		msgmaps[msghdr->msg_spot].next = free_msgmaps;
		free_msgmaps = msghdr->msg_spot;
		nfree_msgmaps++;
		msghdr->msg_spot = next;
		if (msghdr->msg_ts >= msginfo.msgssz)
			msghdr->msg_ts -= msginfo.msgssz;
		else
			msghdr->msg_ts = 0;
	}
	if (msghdr->msg_spot != -1)
		panic("msghdr->msg_spot != -1");
	msghdr->msg_next = free_msghdrs;
	free_msghdrs = msghdr;
#ifdef MAC
	mac_sysvmsg_cleanup(msghdr);
#endif
}

static void
msq_remove(struct msqid_kernel *msqkptr)
{
	struct msg *msghdr;

	racct_sub_cred(msqkptr->cred, RACCT_NMSGQ, 1);
	racct_sub_cred(msqkptr->cred, RACCT_MSGQQUEUED, msqkptr->u.msg_qnum);
	racct_sub_cred(msqkptr->cred, RACCT_MSGQSIZE, msqkptr->u.msg_cbytes);
	crfree(msqkptr->cred);
	msqkptr->cred = NULL;

	/* Free the message headers */
	msghdr = msqkptr->u.__msg_first;
	while (msghdr != NULL) {
		struct msg *msghdr_tmp;

		/* Free the segments of each message */
		msqkptr->u.msg_cbytes -= msghdr->msg_ts;
		msqkptr->u.msg_qnum--;
		msghdr_tmp = msghdr;
		msghdr = msghdr->msg_next;
		msg_freehdr(msghdr_tmp);
	}

	if (msqkptr->u.msg_cbytes != 0)
		panic("msg_cbytes is screwed up");
	if (msqkptr->u.msg_qnum != 0)
		panic("msg_qnum is screwed up");

	msqkptr->u.msg_qbytes = 0;	/* Mark it as free */

#ifdef MAC
	mac_sysvmsq_cleanup(msqkptr);
#endif

	wakeup(msqkptr);
}

static struct prison *
msg_find_prison(struct ucred *cred)
{
	struct prison *pr, *rpr;

	pr = cred->cr_prison;
	prison_lock(pr);
	rpr = osd_jail_get(pr, msg_prison_slot);
	prison_unlock(pr);
	return rpr;
}

static int
msq_prison_cansee(struct prison *rpr, struct msqid_kernel *msqkptr)
{

	if (msqkptr->cred == NULL ||
	    !(rpr == msqkptr->cred->cr_prison ||
	      prison_ischild(rpr, msqkptr->cred->cr_prison)))
		return (EINVAL);
	return (0);
}

#ifndef _SYS_SYSPROTO_H_
struct msgctl_args {
	int	msqid;
	int	cmd;
	struct	msqid_ds *buf;
};
#endif
int
sys_msgctl(struct thread *td, struct msgctl_args *uap)
{
	int msqid = uap->msqid;
	int cmd = uap->cmd;
	struct msqid_ds msqbuf;
	int error;

	DPRINTF(("call to msgctl(%d, %d, %p)\n", msqid, cmd, uap->buf));
	if (cmd == IPC_SET &&
	    (error = copyin(uap->buf, &msqbuf, sizeof(msqbuf))) != 0)
		return (error);
	error = kern_msgctl(td, msqid, cmd, &msqbuf);
	if (cmd == IPC_STAT && error == 0)
		error = copyout(&msqbuf, uap->buf, sizeof(struct msqid_ds));
	return (error);
}

int
kern_msgctl(struct thread *td, int msqid, int cmd, struct msqid_ds *msqbuf)
{
	int rval, error, msqix;
	struct msqid_kernel *msqkptr;
	struct prison *rpr;

	rpr = msg_find_prison(td->td_ucred);
	if (rpr == NULL)
		return (ENOSYS);

	AUDIT_ARG_SVIPC_CMD(cmd);
	AUDIT_ARG_SVIPC_ID(msqid);
	msqix = IPCID_TO_IX(msqid);

	if (msqix < 0 || msqix >= msginfo.msgmni) {
		DPRINTF(("msqid (%d) out of range (0<=msqid<%d)\n", msqix,
		    msginfo.msgmni));
		return (EINVAL);
	}

	msqkptr = &msqids[msqix];

	mtx_lock(&msq_mtx);
	if (msqkptr->u.msg_qbytes == 0) {
		DPRINTF(("no such msqid\n"));
		error = EINVAL;
		goto done2;
	}
	if (msqkptr->u.msg_perm.seq != IPCID_TO_SEQ(msqid)) {
		DPRINTF(("wrong sequence number\n"));
		error = EINVAL;
		goto done2;
	}

	error = msq_prison_cansee(rpr, msqkptr);
	if (error != 0) {
		DPRINTF(("requester can't see prison\n"));
		goto done2;
	}

#ifdef MAC
	error = mac_sysvmsq_check_msqctl(td->td_ucred, msqkptr, cmd);
	if (error != 0)
		goto done2;
#endif

	error = 0;
	rval = 0;

	switch (cmd) {

	case IPC_RMID:
	{
#ifdef MAC
		struct msg *msghdr;
#endif
		if ((error = ipcperm(td, &msqkptr->u.msg_perm, IPC_M)))
			goto done2;

#ifdef MAC
		/*
		 * Check that the thread has MAC access permissions to
		 * individual msghdrs.  Note: We need to do this in a
		 * separate loop because the actual loop alters the
		 * msq/msghdr info as it progresses, and there is no going
		 * back if half the way through we discover that the
		 * thread cannot free a certain msghdr.  The msq will get
		 * into an inconsistent state.
		 */
		for (msghdr = msqkptr->u.__msg_first; msghdr != NULL;
		    msghdr = msghdr->msg_next) {
			error = mac_sysvmsq_check_msgrmid(td->td_ucred, msghdr);
			if (error != 0)
				goto done2;
		}
#endif

		msq_remove(msqkptr);
	}

		break;

	case IPC_SET:
		AUDIT_ARG_SVIPC_PERM(&msqbuf->msg_perm);
		if ((error = ipcperm(td, &msqkptr->u.msg_perm, IPC_M)))
			goto done2;
		if (msqbuf->msg_qbytes > msqkptr->u.msg_qbytes) {
			error = priv_check(td, PRIV_IPC_MSGSIZE);
			if (error)
				goto done2;
		}
		if (msqbuf->msg_qbytes > msginfo.msgmnb) {
			DPRINTF(("can't increase msg_qbytes beyond %d"
			    "(truncating)\n", msginfo.msgmnb));
			msqbuf->msg_qbytes = msginfo.msgmnb;	/* silently restrict qbytes to system limit */
		}
		if (msqbuf->msg_qbytes == 0) {
			DPRINTF(("can't reduce msg_qbytes to 0\n"));
			error = EINVAL;		/* non-standard errno! */
			goto done2;
		}
		msqkptr->u.msg_perm.uid = msqbuf->msg_perm.uid;	/* change the owner */
		msqkptr->u.msg_perm.gid = msqbuf->msg_perm.gid;	/* change the owner */
		msqkptr->u.msg_perm.mode = (msqkptr->u.msg_perm.mode & ~0777) |
		    (msqbuf->msg_perm.mode & 0777);
		msqkptr->u.msg_qbytes = msqbuf->msg_qbytes;
		msqkptr->u.msg_ctime = time_second;
		break;

	case IPC_STAT:
		if ((error = ipcperm(td, &msqkptr->u.msg_perm, IPC_R))) {
			DPRINTF(("requester doesn't have read access\n"));
			goto done2;
		}
		*msqbuf = msqkptr->u;
		if (td->td_ucred->cr_prison != msqkptr->cred->cr_prison)
			msqbuf->msg_perm.key = IPC_PRIVATE;
		break;

	default:
		DPRINTF(("invalid command %d\n", cmd));
		error = EINVAL;
		goto done2;
	}

	if (error == 0)
		td->td_retval[0] = rval;
done2:
	mtx_unlock(&msq_mtx);
	return (error);
}

#ifndef _SYS_SYSPROTO_H_
struct msgget_args {
	key_t	key;
	int	msgflg;
};
#endif

int
sys_msgget(struct thread *td, struct msgget_args *uap)
{
	int msqid, error = 0;
	int key = uap->key;
	int msgflg = uap->msgflg;
	struct ucred *cred = td->td_ucred;
	struct msqid_kernel *msqkptr = NULL;

	DPRINTF(("msgget(0x%x, 0%o)\n", key, msgflg));

	if (msg_find_prison(cred) == NULL)
		return (ENOSYS);

	mtx_lock(&msq_mtx);
	if (key != IPC_PRIVATE) {
		for (msqid = 0; msqid < msginfo.msgmni; msqid++) {
			msqkptr = &msqids[msqid];
			if (msqkptr->u.msg_qbytes != 0 &&
			    msqkptr->cred != NULL &&
			    msqkptr->cred->cr_prison == cred->cr_prison &&
			    msqkptr->u.msg_perm.key == key)
				break;
		}
		if (msqid < msginfo.msgmni) {
			DPRINTF(("found public key\n"));
			if ((msgflg & IPC_CREAT) && (msgflg & IPC_EXCL)) {
				DPRINTF(("not exclusive\n"));
				error = EEXIST;
				goto done2;
			}
			AUDIT_ARG_SVIPC_ID(IXSEQ_TO_IPCID(msqid,
			    msqkptr->u.msg_perm));
			if ((error = ipcperm(td, &msqkptr->u.msg_perm,
			    msgflg & 0700))) {
				DPRINTF(("requester doesn't have 0%o access\n",
				    msgflg & 0700));
				goto done2;
			}
#ifdef MAC
			error = mac_sysvmsq_check_msqget(cred, msqkptr);
			if (error != 0)
				goto done2;
#endif
			goto found;
		}
	}

	DPRINTF(("need to allocate the msqid_ds\n"));
	if (key == IPC_PRIVATE || (msgflg & IPC_CREAT)) {
		for (msqid = 0; msqid < msginfo.msgmni; msqid++) {
			/*
			 * Look for an unallocated and unlocked msqid_ds.
			 * msqid_ds's can be locked by msgsnd or msgrcv while
			 * they are copying the message in/out.  We can't
			 * re-use the entry until they release it.
			 */
			msqkptr = &msqids[msqid];
			if (msqkptr->u.msg_qbytes == 0 &&
			    (msqkptr->u.msg_perm.mode & MSG_LOCKED) == 0)
				break;
		}
		if (msqid == msginfo.msgmni) {
			DPRINTF(("no more msqid_ds's available\n"));
			error = ENOSPC;
			goto done2;
		}
#ifdef RACCT
		if (racct_enable) {
			PROC_LOCK(td->td_proc);
			error = racct_add(td->td_proc, RACCT_NMSGQ, 1);
			PROC_UNLOCK(td->td_proc);
			if (error != 0) {
				error = ENOSPC;
				goto done2;
			}
		}
#endif
		DPRINTF(("msqid %d is available\n", msqid));
		msqkptr->u.msg_perm.key = key;
		msqkptr->u.msg_perm.cuid = cred->cr_uid;
		msqkptr->u.msg_perm.uid = cred->cr_uid;
		msqkptr->u.msg_perm.cgid = cred->cr_gid;
		msqkptr->u.msg_perm.gid = cred->cr_gid;
		msqkptr->u.msg_perm.mode = (msgflg & 0777);
		msqkptr->cred = crhold(cred);
		/* Make sure that the returned msqid is unique */
		msqkptr->u.msg_perm.seq = (msqkptr->u.msg_perm.seq + 1) & 0x7fff;
		msqkptr->u.__msg_first = NULL;
		msqkptr->u.__msg_last = NULL;
		msqkptr->u.msg_cbytes = 0;
		msqkptr->u.msg_qnum = 0;
		msqkptr->u.msg_qbytes = msginfo.msgmnb;
		msqkptr->u.msg_lspid = 0;
		msqkptr->u.msg_lrpid = 0;
		msqkptr->u.msg_stime = 0;
		msqkptr->u.msg_rtime = 0;
		msqkptr->u.msg_ctime = time_second;
#ifdef MAC
		mac_sysvmsq_create(cred, msqkptr);
#endif
		AUDIT_ARG_SVIPC_PERM(&msqkptr->u.msg_perm);
	} else {
		DPRINTF(("didn't find it and wasn't asked to create it\n"));
		error = ENOENT;
		goto done2;
	}

found:
	/* Construct the unique msqid */
	td->td_retval[0] = IXSEQ_TO_IPCID(msqid, msqkptr->u.msg_perm);
done2:
	mtx_unlock(&msq_mtx);
	return (error);
}

#ifndef _SYS_SYSPROTO_H_
struct msgsnd_args {
	int	msqid;
	const void	*msgp;	/* XXX msgp is actually mtext. */
	size_t	msgsz;
	int	msgflg;
};
#endif
int
kern_msgsnd(struct thread *td, int msqid, const void *msgp,
    size_t msgsz, int msgflg, long mtype)
{
	int msqix, segs_needed, error = 0;
	struct msqid_kernel *msqkptr;
	struct msg *msghdr;
	struct prison *rpr;
	short next;
#ifdef RACCT
	size_t saved_msgsz = 0;
#endif

	rpr = msg_find_prison(td->td_ucred);
	if (rpr == NULL)
		return (ENOSYS);

	mtx_lock(&msq_mtx);
	AUDIT_ARG_SVIPC_ID(msqid);
	msqix = IPCID_TO_IX(msqid);

	if (msqix < 0 || msqix >= msginfo.msgmni) {
		DPRINTF(("msqid (%d) out of range (0<=msqid<%d)\n", msqix,
		    msginfo.msgmni));
		error = EINVAL;
		goto done2;
	}

	msqkptr = &msqids[msqix];
	AUDIT_ARG_SVIPC_PERM(&msqkptr->u.msg_perm);
	if (msqkptr->u.msg_qbytes == 0) {
		DPRINTF(("no such message queue id\n"));
		error = EINVAL;
		goto done2;
	}
	if (msqkptr->u.msg_perm.seq != IPCID_TO_SEQ(msqid)) {
		DPRINTF(("wrong sequence number\n"));
		error = EINVAL;
		goto done2;
	}

	if ((error = msq_prison_cansee(rpr, msqkptr))) {
		DPRINTF(("requester can't see prison\n"));
		goto done2;
	}

	if ((error = ipcperm(td, &msqkptr->u.msg_perm, IPC_W))) {
		DPRINTF(("requester doesn't have write access\n"));
		goto done2;
	}

#ifdef MAC
	error = mac_sysvmsq_check_msqsnd(td->td_ucred, msqkptr);
	if (error != 0)
		goto done2;
#endif

#ifdef RACCT
	if (racct_enable) {
		PROC_LOCK(td->td_proc);
		if (racct_add(td->td_proc, RACCT_MSGQQUEUED, 1)) {
			PROC_UNLOCK(td->td_proc);
			error = EAGAIN;
			goto done2;
		}
		saved_msgsz = msgsz;
		if (racct_add(td->td_proc, RACCT_MSGQSIZE, msgsz)) {
			racct_sub(td->td_proc, RACCT_MSGQQUEUED, 1);
			PROC_UNLOCK(td->td_proc);
			error = EAGAIN;
			goto done2;
		}
		PROC_UNLOCK(td->td_proc);
	}
#endif

	segs_needed = howmany(msgsz, msginfo.msgssz);
	DPRINTF(("msgsz=%zu, msgssz=%d, segs_needed=%d\n", msgsz,
	    msginfo.msgssz, segs_needed));
	for (;;) {
		int need_more_resources = 0;

		/*
		 * check msgsz
		 * (inside this loop in case msg_qbytes changes while we sleep)
		 */

		if (msgsz > msqkptr->u.msg_qbytes) {
			DPRINTF(("msgsz > msqkptr->u.msg_qbytes\n"));
			error = EINVAL;
			goto done3;
		}

		if (msqkptr->u.msg_perm.mode & MSG_LOCKED) {
			DPRINTF(("msqid is locked\n"));
			need_more_resources = 1;
		}
		if (msgsz + msqkptr->u.msg_cbytes > msqkptr->u.msg_qbytes) {
			DPRINTF(("msgsz + msg_cbytes > msg_qbytes\n"));
			need_more_resources = 1;
		}
		if (segs_needed > nfree_msgmaps) {
			DPRINTF(("segs_needed > nfree_msgmaps\n"));
			need_more_resources = 1;
		}
		if (free_msghdrs == NULL) {
			DPRINTF(("no more msghdrs\n"));
			need_more_resources = 1;
		}

		if (need_more_resources) {
			int we_own_it;

			if ((msgflg & IPC_NOWAIT) != 0) {
				DPRINTF(("need more resources but caller "
				    "doesn't want to wait\n"));
				error = EAGAIN;
				goto done3;
			}

			if ((msqkptr->u.msg_perm.mode & MSG_LOCKED) != 0) {
				DPRINTF(("we don't own the msqid_ds\n"));
				we_own_it = 0;
			} else {
				/* Force later arrivals to wait for our
				   request */
				DPRINTF(("we own the msqid_ds\n"));
				msqkptr->u.msg_perm.mode |= MSG_LOCKED;
				we_own_it = 1;
			}
			DPRINTF(("msgsnd:  goodnight\n"));
			error = msleep(msqkptr, &msq_mtx, (PZERO - 4) | PCATCH,
			    "msgsnd", hz);
			DPRINTF(("msgsnd:  good morning, error=%d\n", error));
			if (we_own_it)
				msqkptr->u.msg_perm.mode &= ~MSG_LOCKED;
			if (error == EWOULDBLOCK) {
				DPRINTF(("msgsnd:  timed out\n"));
				continue;
			}
			if (error != 0) {
				DPRINTF(("msgsnd:  interrupted system call\n"));
				error = EINTR;
				goto done3;
			}

			/*
			 * Make sure that the msq queue still exists
			 */

			if (msqkptr->u.msg_qbytes == 0) {
				DPRINTF(("msqid deleted\n"));
				error = EIDRM;
				goto done3;
			}

		} else {
			DPRINTF(("got all the resources that we need\n"));
			break;
		}
	}

	/*
	 * We have the resources that we need.
	 * Make sure!
	 */

	if (msqkptr->u.msg_perm.mode & MSG_LOCKED)
		panic("msg_perm.mode & MSG_LOCKED");
	if (segs_needed > nfree_msgmaps)
		panic("segs_needed > nfree_msgmaps");
	if (msgsz + msqkptr->u.msg_cbytes > msqkptr->u.msg_qbytes)
		panic("msgsz + msg_cbytes > msg_qbytes");
	if (free_msghdrs == NULL)
		panic("no more msghdrs");

	/*
	 * Re-lock the msqid_ds in case we page-fault when copying in the
	 * message
	 */

	if ((msqkptr->u.msg_perm.mode & MSG_LOCKED) != 0)
		panic("msqid_ds is already locked");
	msqkptr->u.msg_perm.mode |= MSG_LOCKED;

	/*
	 * Allocate a message header
	 */

	msghdr = free_msghdrs;
	free_msghdrs = msghdr->msg_next;
	msghdr->msg_spot = -1;
	msghdr->msg_ts = msgsz;
	msghdr->msg_type = mtype;
#ifdef MAC
	/*
	 * XXXMAC: Should the mac_sysvmsq_check_msgmsq check follow here
	 * immediately?  Or, should it be checked just before the msg is
	 * enqueued in the msgq (as it is done now)?
	 */
	mac_sysvmsg_create(td->td_ucred, msqkptr, msghdr);
#endif

	/*
	 * Allocate space for the message
	 */

	while (segs_needed > 0) {
		if (nfree_msgmaps <= 0)
			panic("not enough msgmaps");
		if (free_msgmaps == -1)
			panic("nil free_msgmaps");
		next = free_msgmaps;
		if (next <= -1)
			panic("next too low #1");
		if (next >= msginfo.msgseg)
			panic("next out of range #1");
		DPRINTF(("allocating segment %d to message\n", next));
		free_msgmaps = msgmaps[next].next;
		nfree_msgmaps--;
		msgmaps[next].next = msghdr->msg_spot;
		msghdr->msg_spot = next;
		segs_needed--;
	}

	/*
	 * Validate the message type
	 */

	if (msghdr->msg_type < 1) {
		msg_freehdr(msghdr);
		msqkptr->u.msg_perm.mode &= ~MSG_LOCKED;
		wakeup(msqkptr);
		DPRINTF(("mtype (%ld) < 1\n", msghdr->msg_type));
		error = EINVAL;
		goto done3;
	}

	/*
	 * Copy in the message body
	 */

	next = msghdr->msg_spot;
	while (msgsz > 0) {
		size_t tlen;
		if (msgsz > msginfo.msgssz)
			tlen = msginfo.msgssz;
		else
			tlen = msgsz;
		if (next <= -1)
			panic("next too low #2");
		if (next >= msginfo.msgseg)
			panic("next out of range #2");
		mtx_unlock(&msq_mtx);
		if ((error = copyin(msgp, &msgpool[next * msginfo.msgssz],
		    tlen)) != 0) {
			mtx_lock(&msq_mtx);
			DPRINTF(("error %d copying in message segment\n",
			    error));
			msg_freehdr(msghdr);
			msqkptr->u.msg_perm.mode &= ~MSG_LOCKED;
			wakeup(msqkptr);
			goto done3;
		}
		mtx_lock(&msq_mtx);
		msgsz -= tlen;
		msgp = (const char *)msgp + tlen;
		next = msgmaps[next].next;
	}
	if (next != -1)
		panic("didn't use all the msg segments");

	/*
	 * We've got the message.  Unlock the msqid_ds.
	 */

	msqkptr->u.msg_perm.mode &= ~MSG_LOCKED;

	/*
	 * Make sure that the msqid_ds is still allocated.
	 */

	if (msqkptr->u.msg_qbytes == 0) {
		msg_freehdr(msghdr);
		wakeup(msqkptr);
		error = EIDRM;
		goto done3;
	}

#ifdef MAC
	/*
	 * Note: Since the task/thread allocates the msghdr and usually
	 * primes it with its own MAC label, for a majority of policies, it
	 * won't be necessary to check whether the msghdr has access
	 * permissions to the msgq.  The mac_sysvmsq_check_msqsnd check would
	 * suffice in that case.  However, this hook may be required where
	 * individual policies derive a non-identical label for the msghdr
	 * from the current thread label and may want to check the msghdr
	 * enqueue permissions, along with read/write permissions to the
	 * msgq.
	 */
	error = mac_sysvmsq_check_msgmsq(td->td_ucred, msghdr, msqkptr);
	if (error != 0) {
		msg_freehdr(msghdr);
		wakeup(msqkptr);
		goto done3;
	}
#endif

	/*
	 * Put the message into the queue
	 */
	if (msqkptr->u.__msg_first == NULL) {
		msqkptr->u.__msg_first = msghdr;
		msqkptr->u.__msg_last = msghdr;
	} else {
		msqkptr->u.__msg_last->msg_next = msghdr;
		msqkptr->u.__msg_last = msghdr;
	}
	msqkptr->u.__msg_last->msg_next = NULL;

	msqkptr->u.msg_cbytes += msghdr->msg_ts;
	msqkptr->u.msg_qnum++;
	msqkptr->u.msg_lspid = td->td_proc->p_pid;
	msqkptr->u.msg_stime = time_second;

	wakeup(msqkptr);
	td->td_retval[0] = 0;
done3:
#ifdef RACCT
	if (racct_enable && error != 0) {
		PROC_LOCK(td->td_proc);
		racct_sub(td->td_proc, RACCT_MSGQQUEUED, 1);
		racct_sub(td->td_proc, RACCT_MSGQSIZE, saved_msgsz);
		PROC_UNLOCK(td->td_proc);
	}
#endif
done2:
	mtx_unlock(&msq_mtx);
	return (error);
}

int
sys_msgsnd(struct thread *td, struct msgsnd_args *uap)
{
	int error;
	long mtype;

	DPRINTF(("call to msgsnd(%d, %p, %zu, %d)\n", uap->msqid, uap->msgp,
	    uap->msgsz, uap->msgflg));

	if ((error = copyin(uap->msgp, &mtype, sizeof(mtype))) != 0) {
		DPRINTF(("error %d copying the message type\n", error));
		return (error);
	}
	return (kern_msgsnd(td, uap->msqid,
	    (const char *)uap->msgp + sizeof(mtype),
	    uap->msgsz, uap->msgflg, mtype));
}

#ifndef _SYS_SYSPROTO_H_
struct msgrcv_args {
	int	msqid;
	void	*msgp;
	size_t	msgsz;
	long	msgtyp;
	int	msgflg;
};
#endif
/* XXX msgp is actually mtext. */
int
kern_msgrcv(struct thread *td, int msqid, void *msgp, size_t msgsz, long msgtyp,
    int msgflg, long *mtype)
{
	size_t len;
	struct msqid_kernel *msqkptr;
	struct msg *msghdr;
	struct prison *rpr;
	int msqix, error = 0;
	short next;

	rpr = msg_find_prison(td->td_ucred);
	if (rpr == NULL)
		return (ENOSYS);

	AUDIT_ARG_SVIPC_ID(msqid);
	msqix = IPCID_TO_IX(msqid);

	if (msqix < 0 || msqix >= msginfo.msgmni) {
		DPRINTF(("msqid (%d) out of range (0<=msqid<%d)\n", msqix,
		    msginfo.msgmni));
		return (EINVAL);
	}

	msqkptr = &msqids[msqix];
	mtx_lock(&msq_mtx);
	AUDIT_ARG_SVIPC_PERM(&msqkptr->u.msg_perm);
	if (msqkptr->u.msg_qbytes == 0) {
		DPRINTF(("no such message queue id\n"));
		error = EINVAL;
		goto done2;
	}
	if (msqkptr->u.msg_perm.seq != IPCID_TO_SEQ(msqid)) {
		DPRINTF(("wrong sequence number\n"));
		error = EINVAL;
		goto done2;
	}

	if ((error = msq_prison_cansee(rpr, msqkptr))) {
		DPRINTF(("requester can't see prison\n"));
		goto done2;
	}

	if ((error = ipcperm(td, &msqkptr->u.msg_perm, IPC_R))) {
		DPRINTF(("requester doesn't have read access\n"));
		goto done2;
	}

#ifdef MAC
	error = mac_sysvmsq_check_msqrcv(td->td_ucred, msqkptr);
	if (error != 0)
		goto done2;
#endif

	msghdr = NULL;
	while (msghdr == NULL) {
		if (msgtyp == 0) {
			msghdr = msqkptr->u.__msg_first;
			if (msghdr != NULL) {
				if (msgsz < msghdr->msg_ts &&
				    (msgflg & MSG_NOERROR) == 0) {
					DPRINTF(("first message on the queue "
					    "is too big (want %zu, got %d)\n",
					    msgsz, msghdr->msg_ts));
					error = E2BIG;
					goto done2;
				}
#ifdef MAC
				error = mac_sysvmsq_check_msgrcv(td->td_ucred,
				    msghdr);
				if (error != 0)
					goto done2;
#endif
				if (msqkptr->u.__msg_first ==
				    msqkptr->u.__msg_last) {
					msqkptr->u.__msg_first = NULL;
					msqkptr->u.__msg_last = NULL;
				} else {
					msqkptr->u.__msg_first = msghdr->msg_next;
					if (msqkptr->u.__msg_first == NULL)
						panic("msg_first/last screwed up #1");
				}
			}
		} else {
			struct msg *previous;
			struct msg **prev;

			previous = NULL;
			prev = &(msqkptr->u.__msg_first);
			while ((msghdr = *prev) != NULL) {
				/*
				 * Is this message's type an exact match or is
				 * this message's type less than or equal to
				 * the absolute value of a negative msgtyp?
				 * Note that the second half of this test can
				 * NEVER be true if msgtyp is positive since
				 * msg_type is always positive!
				 */

				if (msgtyp == msghdr->msg_type ||
				    msghdr->msg_type <= -msgtyp) {
					DPRINTF(("found message type %ld, "
					    "requested %ld\n",
					    msghdr->msg_type, msgtyp));
					if (msgsz < msghdr->msg_ts &&
					    (msgflg & MSG_NOERROR) == 0) {
						DPRINTF(("requested message "
						    "on the queue is too big "
						    "(want %zu, got %hu)\n",
						    msgsz, msghdr->msg_ts));
						error = E2BIG;
						goto done2;
					}
#ifdef MAC
					error = mac_sysvmsq_check_msgrcv(
					    td->td_ucred, msghdr);
					if (error != 0)
						goto done2;
#endif
					*prev = msghdr->msg_next;
					if (msghdr == msqkptr->u.__msg_last) {
						if (previous == NULL) {
							if (prev !=
							    &msqkptr->u.__msg_first)
								panic("__msg_first/last screwed up #2");
							msqkptr->u.__msg_first =
							    NULL;
							msqkptr->u.__msg_last =
							    NULL;
						} else {
							if (prev ==
							    &msqkptr->u.__msg_first)
								panic("__msg_first/last screwed up #3");
							msqkptr->u.__msg_last =
							    previous;
						}
					}
					break;
				}
				previous = msghdr;
				prev = &(msghdr->msg_next);
			}
		}

		/*
		 * We've either extracted the msghdr for the appropriate
		 * message or there isn't one.
		 * If there is one then bail out of this loop.
		 */

		if (msghdr != NULL)
			break;

		/*
		 * Hmph!  No message found.  Does the user want to wait?
		 */

		if ((msgflg & IPC_NOWAIT) != 0) {
			DPRINTF(("no appropriate message found (msgtyp=%ld)\n",
			    msgtyp));
			/* The SVID says to return ENOMSG. */
			error = ENOMSG;
			goto done2;
		}

		/*
		 * Wait for something to happen
		 */

		DPRINTF(("msgrcv:  goodnight\n"));
		error = msleep(msqkptr, &msq_mtx, (PZERO - 4) | PCATCH,
		    "msgrcv", 0);
		DPRINTF(("msgrcv:  good morning (error=%d)\n", error));

		if (error != 0) {
			DPRINTF(("msgrcv:  interrupted system call\n"));
			error = EINTR;
			goto done2;
		}

		/*
		 * Make sure that the msq queue still exists
		 */

		if (msqkptr->u.msg_qbytes == 0 ||
		    msqkptr->u.msg_perm.seq != IPCID_TO_SEQ(msqid)) {
			DPRINTF(("msqid deleted\n"));
			error = EIDRM;
			goto done2;
		}
	}

	/*
	 * Return the message to the user.
	 *
	 * First, do the bookkeeping (before we risk being interrupted).
	 */

	msqkptr->u.msg_cbytes -= msghdr->msg_ts;
	msqkptr->u.msg_qnum--;
	msqkptr->u.msg_lrpid = td->td_proc->p_pid;
	msqkptr->u.msg_rtime = time_second;

	racct_sub_cred(msqkptr->cred, RACCT_MSGQQUEUED, 1);
	racct_sub_cred(msqkptr->cred, RACCT_MSGQSIZE, msghdr->msg_ts);

	/*
	 * Make msgsz the actual amount that we'll be returning.
	 * Note that this effectively truncates the message if it is too long
	 * (since msgsz is never increased).
	 */

	DPRINTF(("found a message, msgsz=%zu, msg_ts=%hu\n", msgsz,
	    msghdr->msg_ts));
	if (msgsz > msghdr->msg_ts)
		msgsz = msghdr->msg_ts;
	*mtype = msghdr->msg_type;

	/*
	 * Return the segments to the user
	 */

	next = msghdr->msg_spot;
	for (len = 0; len < msgsz; len += msginfo.msgssz) {
		size_t tlen;

		if (msgsz - len > msginfo.msgssz)
			tlen = msginfo.msgssz;
		else
			tlen = msgsz - len;
		if (next <= -1)
			panic("next too low #3");
		if (next >= msginfo.msgseg)
			panic("next out of range #3");
		mtx_unlock(&msq_mtx);
		error = copyout(&msgpool[next * msginfo.msgssz], msgp, tlen);
		mtx_lock(&msq_mtx);
		if (error != 0) {
			DPRINTF(("error (%d) copying out message segment\n",
			    error));
			msg_freehdr(msghdr);
			wakeup(msqkptr);
			goto done2;
		}
		msgp = (char *)msgp + tlen;
		next = msgmaps[next].next;
	}

	/*
	 * Done, return the actual number of bytes copied out.
	 */

	msg_freehdr(msghdr);
	wakeup(msqkptr);
	td->td_retval[0] = msgsz;
done2:
	mtx_unlock(&msq_mtx);
	return (error);
}

int
sys_msgrcv(struct thread *td, struct msgrcv_args *uap)
{
	int error;
	long mtype;

	DPRINTF(("call to msgrcv(%d, %p, %zu, %ld, %d)\n", uap->msqid,
	    uap->msgp, uap->msgsz, uap->msgtyp, uap->msgflg));

	if ((error = kern_msgrcv(td, uap->msqid,
	    (char *)uap->msgp + sizeof(mtype), uap->msgsz,
	    uap->msgtyp, uap->msgflg, &mtype)) != 0)
		return (error);
	if ((error = copyout(&mtype, uap->msgp, sizeof(mtype))) != 0)
		DPRINTF(("error %d copying the message type\n", error));
	return (error);
}

static int
sysctl_msqids(SYSCTL_HANDLER_ARGS)
{
	struct msqid_kernel tmsqk;
#ifdef COMPAT_FREEBSD32
	struct msqid_kernel32 tmsqk32;
#endif
	struct prison *pr, *rpr;
	void *outaddr;
	size_t outsize;
	int error, i;

	pr = req->td->td_ucred->cr_prison;
	rpr = msg_find_prison(req->td->td_ucred);
	error = 0;
	for (i = 0; i < msginfo.msgmni; i++) {
		mtx_lock(&msq_mtx);
		if (msqids[i].u.msg_qbytes == 0 || rpr == NULL ||
		    msq_prison_cansee(rpr, &msqids[i]) != 0)
			bzero(&tmsqk, sizeof(tmsqk));
		else {
			tmsqk = msqids[i];
			if (tmsqk.cred->cr_prison != pr)
				tmsqk.u.msg_perm.key = IPC_PRIVATE;
		}
		mtx_unlock(&msq_mtx);
#ifdef COMPAT_FREEBSD32
		if (SV_CURPROC_FLAG(SV_ILP32)) {
			bzero(&tmsqk32, sizeof(tmsqk32));
			freebsd32_ipcperm_out(&tmsqk.u.msg_perm,
			    &tmsqk32.u.msg_perm);
			/* Don't copy u.msg_first or u.msg_last */
			CP(tmsqk, tmsqk32, u.msg_cbytes);
			CP(tmsqk, tmsqk32, u.msg_qnum);
			CP(tmsqk, tmsqk32, u.msg_qbytes);
			CP(tmsqk, tmsqk32, u.msg_lspid);
			CP(tmsqk, tmsqk32, u.msg_lrpid);
			CP(tmsqk, tmsqk32, u.msg_stime);
			CP(tmsqk, tmsqk32, u.msg_rtime);
			CP(tmsqk, tmsqk32, u.msg_ctime);
			/* Don't copy label or cred */
			outaddr = &tmsqk32;
			outsize = sizeof(tmsqk32);
		} else
#endif
		{
			/* Don't leak kernel pointers */
			tmsqk.u.__msg_first = NULL;
			tmsqk.u.__msg_last = NULL;
			tmsqk.label = NULL;
			tmsqk.cred = NULL;
			/*
			 * XXX: some padding also exists, but we take care to
			 * allocate our pool of msqid_kernel structs with
			 * zeroed memory so this should be OK.
			 */
			outaddr = &tmsqk;
			outsize = sizeof(tmsqk);
		}
		error = SYSCTL_OUT(req, outaddr, outsize);
		if (error != 0)
			break;
	}
	return (error);
}

SYSCTL_INT(_kern_ipc, OID_AUTO, msgmax, CTLFLAG_RD, &msginfo.msgmax, 0,
    "Maximum message size");
SYSCTL_INT(_kern_ipc, OID_AUTO, msgmni, CTLFLAG_RDTUN, &msginfo.msgmni, 0,
    "Number of message queue identifiers");
SYSCTL_INT(_kern_ipc, OID_AUTO, msgmnb, CTLFLAG_RDTUN, &msginfo.msgmnb, 0,
    "Maximum number of bytes in a queue");
SYSCTL_INT(_kern_ipc, OID_AUTO, msgtql, CTLFLAG_RDTUN, &msginfo.msgtql, 0,
    "Maximum number of messages in the system");
SYSCTL_INT(_kern_ipc, OID_AUTO, msgssz, CTLFLAG_RDTUN, &msginfo.msgssz, 0,
    "Size of a message segment");
SYSCTL_INT(_kern_ipc, OID_AUTO, msgseg, CTLFLAG_RDTUN, &msginfo.msgseg, 0,
    "Number of message segments");
SYSCTL_PROC(_kern_ipc, OID_AUTO, msqids,
    CTLTYPE_OPAQUE | CTLFLAG_RD | CTLFLAG_MPSAFE,
    NULL, 0, sysctl_msqids, "",
    "Array of struct msqid_kernel for each potential message queue");

static int
msg_prison_check(void *obj, void *data)
{
	struct prison *pr = obj;
	struct prison *prpr;
	struct vfsoptlist *opts = data;
	int error, jsys;

	/*
	 * sysvmsg is a jailsys integer.
	 * It must be "disable" if the parent jail is disabled.
	 */
	error = vfs_copyopt(opts, "sysvmsg", &jsys, sizeof(jsys));
	if (error != ENOENT) {
		if (error != 0)
			return (error);
		switch (jsys) {
		case JAIL_SYS_DISABLE:
			break;
		case JAIL_SYS_NEW:
		case JAIL_SYS_INHERIT:
			prison_lock(pr->pr_parent);
			prpr = osd_jail_get(pr->pr_parent, msg_prison_slot);
			prison_unlock(pr->pr_parent);
			if (prpr == NULL)
				return (EPERM);
			break;
		default:
			return (EINVAL);
		}
	}

	return (0);
}

static int
msg_prison_set(void *obj, void *data)
{
	struct prison *pr = obj;
	struct prison *tpr, *orpr, *nrpr, *trpr;
	struct vfsoptlist *opts = data;
	void *rsv;
	int jsys, descend;

	/*
	 * sysvmsg controls which jail is the root of the associated msgs (this
	 * jail or same as the parent), or if the feature is available at all.
	 */
	if (vfs_copyopt(opts, "sysvmsg", &jsys, sizeof(jsys)) == ENOENT)
		jsys = vfs_flagopt(opts, "allow.sysvipc", NULL, 0)
		    ? JAIL_SYS_INHERIT
		    : vfs_flagopt(opts, "allow.nosysvipc", NULL, 0)
		    ? JAIL_SYS_DISABLE
		    : -1;
	if (jsys == JAIL_SYS_DISABLE) {
		prison_lock(pr);
		orpr = osd_jail_get(pr, msg_prison_slot);
		if (orpr != NULL)
			osd_jail_del(pr, msg_prison_slot);
		prison_unlock(pr);
		if (orpr != NULL) {
			if (orpr == pr)
				msg_prison_cleanup(pr);
			/* Disable all child jails as well. */
			FOREACH_PRISON_DESCENDANT(pr, tpr, descend) {
				prison_lock(tpr);
				trpr = osd_jail_get(tpr, msg_prison_slot);
				if (trpr != NULL) {
					osd_jail_del(tpr, msg_prison_slot);
					prison_unlock(tpr);
					if (trpr == tpr)
						msg_prison_cleanup(tpr);
				} else {
					prison_unlock(tpr);
					descend = 0;
				}
			}
		}
	} else if (jsys != -1) {
		if (jsys == JAIL_SYS_NEW)
			nrpr = pr;
		else {
			prison_lock(pr->pr_parent);
			nrpr = osd_jail_get(pr->pr_parent, msg_prison_slot);
			prison_unlock(pr->pr_parent);
		}
		rsv = osd_reserve(msg_prison_slot);
		prison_lock(pr);
		orpr = osd_jail_get(pr, msg_prison_slot);
		if (orpr != nrpr)
			(void)osd_jail_set_reserved(pr, msg_prison_slot, rsv,
			    nrpr);
		else
			osd_free_reserved(rsv);
		prison_unlock(pr);
		if (orpr != nrpr) {
			if (orpr == pr)
				msg_prison_cleanup(pr);
			if (orpr != NULL) {
				/* Change child jails matching the old root, */
				FOREACH_PRISON_DESCENDANT(pr, tpr, descend) {
					prison_lock(tpr);
					trpr = osd_jail_get(tpr,
					    msg_prison_slot);
					if (trpr == orpr) {
						(void)osd_jail_set(tpr,
						    msg_prison_slot, nrpr);
						prison_unlock(tpr);
						if (trpr == tpr)
							msg_prison_cleanup(tpr);
					} else {
						prison_unlock(tpr);
						descend = 0;
					}
				}
			}
		}
	}

	return (0);
}

static int
msg_prison_get(void *obj, void *data)
{
	struct prison *pr = obj;
	struct prison *rpr;
	struct vfsoptlist *opts = data;
	int error, jsys;

	/* Set sysvmsg based on the jail's root prison. */
	prison_lock(pr);
	rpr = osd_jail_get(pr, msg_prison_slot);
	prison_unlock(pr);
	jsys = rpr == NULL ? JAIL_SYS_DISABLE
	    : rpr == pr ? JAIL_SYS_NEW : JAIL_SYS_INHERIT;
	error = vfs_setopt(opts, "sysvmsg", &jsys, sizeof(jsys));
	if (error == ENOENT)
		error = 0;
	return (error);
}

static int
msg_prison_remove(void *obj, void *data __unused)
{
	struct prison *pr = obj;
	struct prison *rpr;

	prison_lock(pr);
	rpr = osd_jail_get(pr, msg_prison_slot);
	prison_unlock(pr);
	if (rpr == pr)
		msg_prison_cleanup(pr);
	return (0);
}

static void
msg_prison_cleanup(struct prison *pr)
{
	struct msqid_kernel *msqkptr;
	int i;

	/* Remove any msqs that belong to this jail. */
	mtx_lock(&msq_mtx);
	for (i = 0; i < msginfo.msgmni; i++) {
		msqkptr = &msqids[i];
		if (msqkptr->u.msg_qbytes != 0 &&
		    msqkptr->cred != NULL && msqkptr->cred->cr_prison == pr)
			msq_remove(msqkptr);
	}
	mtx_unlock(&msq_mtx);
}

SYSCTL_JAIL_PARAM_SYS_NODE(sysvmsg, CTLFLAG_RW, "SYSV message queues");

#ifdef COMPAT_FREEBSD32
int
freebsd32_msgsys(struct thread *td, struct freebsd32_msgsys_args *uap)
{

#if defined(COMPAT_FREEBSD4) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD7)
	AUDIT_ARG_SVIPC_WHICH(uap->which);
	switch (uap->which) {
	case 0:
		return (freebsd7_freebsd32_msgctl(td,
		    (struct freebsd7_freebsd32_msgctl_args *)&uap->a2));
	case 2:
		return (freebsd32_msgsnd(td,
		    (struct freebsd32_msgsnd_args *)&uap->a2));
	case 3:
		return (freebsd32_msgrcv(td,
		    (struct freebsd32_msgrcv_args *)&uap->a2));
	default:
		return (sys_msgsys(td, (struct msgsys_args *)uap));
	}
#else
	return (nosys(td, NULL));
#endif
}

#if defined(COMPAT_FREEBSD4) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD7)
int
freebsd7_freebsd32_msgctl(struct thread *td,
    struct freebsd7_freebsd32_msgctl_args *uap)
{
	struct msqid_ds msqbuf;
	struct msqid_ds32_old msqbuf32;
	int error;

	if (uap->cmd == IPC_SET) {
		error = copyin(uap->buf, &msqbuf32, sizeof(msqbuf32));
		if (error)
			return (error);
		freebsd32_ipcperm_old_in(&msqbuf32.msg_perm, &msqbuf.msg_perm);
		PTRIN_CP(msqbuf32, msqbuf, __msg_first);
		PTRIN_CP(msqbuf32, msqbuf, __msg_last);
		CP(msqbuf32, msqbuf, msg_cbytes);
		CP(msqbuf32, msqbuf, msg_qnum);
		CP(msqbuf32, msqbuf, msg_qbytes);
		CP(msqbuf32, msqbuf, msg_lspid);
		CP(msqbuf32, msqbuf, msg_lrpid);
		CP(msqbuf32, msqbuf, msg_stime);
		CP(msqbuf32, msqbuf, msg_rtime);
		CP(msqbuf32, msqbuf, msg_ctime);
	}
	error = kern_msgctl(td, uap->msqid, uap->cmd, &msqbuf);
	if (error)
		return (error);
	if (uap->cmd == IPC_STAT) {
		bzero(&msqbuf32, sizeof(msqbuf32));
		freebsd32_ipcperm_old_out(&msqbuf.msg_perm, &msqbuf32.msg_perm);
		PTROUT_CP(msqbuf, msqbuf32, __msg_first);
		PTROUT_CP(msqbuf, msqbuf32, __msg_last);
		CP(msqbuf, msqbuf32, msg_cbytes);
		CP(msqbuf, msqbuf32, msg_qnum);
		CP(msqbuf, msqbuf32, msg_qbytes);
		CP(msqbuf, msqbuf32, msg_lspid);
		CP(msqbuf, msqbuf32, msg_lrpid);
		CP(msqbuf, msqbuf32, msg_stime);
		CP(msqbuf, msqbuf32, msg_rtime);
		CP(msqbuf, msqbuf32, msg_ctime);
		error = copyout(&msqbuf32, uap->buf, sizeof(struct msqid_ds32));
	}
	return (error);
}
#endif

int
freebsd32_msgctl(struct thread *td, struct freebsd32_msgctl_args *uap)
{
	struct msqid_ds msqbuf;
	struct msqid_ds32 msqbuf32;
	int error;

	if (uap->cmd == IPC_SET) {
		error = copyin(uap->buf, &msqbuf32, sizeof(msqbuf32));
		if (error)
			return (error);
		freebsd32_ipcperm_in(&msqbuf32.msg_perm, &msqbuf.msg_perm);
		PTRIN_CP(msqbuf32, msqbuf, __msg_first);
		PTRIN_CP(msqbuf32, msqbuf, __msg_last);
		CP(msqbuf32, msqbuf, msg_cbytes);
		CP(msqbuf32, msqbuf, msg_qnum);
		CP(msqbuf32, msqbuf, msg_qbytes);
		CP(msqbuf32, msqbuf, msg_lspid);
		CP(msqbuf32, msqbuf, msg_lrpid);
		CP(msqbuf32, msqbuf, msg_stime);
		CP(msqbuf32, msqbuf, msg_rtime);
		CP(msqbuf32, msqbuf, msg_ctime);
	}
	error = kern_msgctl(td, uap->msqid, uap->cmd, &msqbuf);
	if (error)
		return (error);
	if (uap->cmd == IPC_STAT) {
		freebsd32_ipcperm_out(&msqbuf.msg_perm, &msqbuf32.msg_perm);
		PTROUT_CP(msqbuf, msqbuf32, __msg_first);
		PTROUT_CP(msqbuf, msqbuf32, __msg_last);
		CP(msqbuf, msqbuf32, msg_cbytes);
		CP(msqbuf, msqbuf32, msg_qnum);
		CP(msqbuf, msqbuf32, msg_qbytes);
		CP(msqbuf, msqbuf32, msg_lspid);
		CP(msqbuf, msqbuf32, msg_lrpid);
		CP(msqbuf, msqbuf32, msg_stime);
		CP(msqbuf, msqbuf32, msg_rtime);
		CP(msqbuf, msqbuf32, msg_ctime);
		error = copyout(&msqbuf32, uap->buf, sizeof(struct msqid_ds32));
	}
	return (error);
}

int
freebsd32_msgsnd(struct thread *td, struct freebsd32_msgsnd_args *uap)
{
	const void *msgp;
	long mtype;
	int32_t mtype32;
	int error;

	msgp = PTRIN(uap->msgp);
	if ((error = copyin(msgp, &mtype32, sizeof(mtype32))) != 0)
		return (error);
	mtype = mtype32;
	return (kern_msgsnd(td, uap->msqid,
	    (const char *)msgp + sizeof(mtype32),
	    uap->msgsz, uap->msgflg, mtype));
}

int
freebsd32_msgrcv(struct thread *td, struct freebsd32_msgrcv_args *uap)
{
	void *msgp;
	long mtype;
	int32_t mtype32;
	int error;

	msgp = PTRIN(uap->msgp);
	if ((error = kern_msgrcv(td, uap->msqid,
	    (char *)msgp + sizeof(mtype32), uap->msgsz,
	    uap->msgtyp, uap->msgflg, &mtype)) != 0)
		return (error);
	mtype32 = (int32_t)mtype;
	return (copyout(&mtype32, msgp, sizeof(mtype32)));
}
#endif

#if defined(COMPAT_FREEBSD4) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD7)

/* XXX casting to (sy_call_t *) is bogus, as usual. */
static sy_call_t *msgcalls[] = {
	(sy_call_t *)freebsd7_msgctl, (sy_call_t *)sys_msgget,
	(sy_call_t *)sys_msgsnd, (sy_call_t *)sys_msgrcv
};

/*
 * Entry point for all MSG calls.
 *
 * XXX actually varargs.
 * struct msgsys_args {
 *		int	which;
 *		int	a2;
 *		int	a3;
 *		int	a4;
 *		int	a5;
 *		int	a6;
 *	} *uap;
 */
int
sys_msgsys(struct thread *td, struct msgsys_args *uap)
{
	int error;

	AUDIT_ARG_SVIPC_WHICH(uap->which);
	if (uap->which < 0 || uap->which >= nitems(msgcalls))
		return (EINVAL);
	error = (*msgcalls[uap->which])(td, &uap->a2);
	return (error);
}

#ifndef CP
#define CP(src, dst, fld)	do { (dst).fld = (src).fld; } while (0)
#endif

#ifndef _SYS_SYSPROTO_H_
struct freebsd7_msgctl_args {
	int	msqid;
	int	cmd;
	struct	msqid_ds_old *buf;
};
#endif
int
freebsd7_msgctl(struct thread *td, struct freebsd7_msgctl_args *uap)
{
	struct msqid_ds_old msqold;
	struct msqid_ds msqbuf;
	int error;

	DPRINTF(("call to freebsd7_msgctl(%d, %d, %p)\n", uap->msqid, uap->cmd,
	    uap->buf));
	if (uap->cmd == IPC_SET) {
		error = copyin(uap->buf, &msqold, sizeof(msqold));
		if (error)
			return (error);
		ipcperm_old2new(&msqold.msg_perm, &msqbuf.msg_perm);
		CP(msqold, msqbuf, __msg_first);
		CP(msqold, msqbuf, __msg_last);
		CP(msqold, msqbuf, msg_cbytes);
		CP(msqold, msqbuf, msg_qnum);
		CP(msqold, msqbuf, msg_qbytes);
		CP(msqold, msqbuf, msg_lspid);
		CP(msqold, msqbuf, msg_lrpid);
		CP(msqold, msqbuf, msg_stime);
		CP(msqold, msqbuf, msg_rtime);
		CP(msqold, msqbuf, msg_ctime);
	}
	error = kern_msgctl(td, uap->msqid, uap->cmd, &msqbuf);
	if (error)
		return (error);
	if (uap->cmd == IPC_STAT) {
		bzero(&msqold, sizeof(msqold));
		ipcperm_new2old(&msqbuf.msg_perm, &msqold.msg_perm);
		CP(msqbuf, msqold, __msg_first);
		CP(msqbuf, msqold, __msg_last);
		CP(msqbuf, msqold, msg_cbytes);
		CP(msqbuf, msqold, msg_qnum);
		CP(msqbuf, msqold, msg_qbytes);
		CP(msqbuf, msqold, msg_lspid);
		CP(msqbuf, msqold, msg_lrpid);
		CP(msqbuf, msqold, msg_stime);
		CP(msqbuf, msqold, msg_rtime);
		CP(msqbuf, msqold, msg_ctime);
		error = copyout(&msqold, uap->buf, sizeof(struct msqid_ds_old));
	}
	return (error);
}

#undef CP

#endif	/* COMPAT_FREEBSD4 || COMPAT_FREEBSD5 || COMPAT_FREEBSD6 ||
	   COMPAT_FREEBSD7 */
