/*-
 * Implementation of SVID semaphores
 *
 * Author:  Daniel Boulet
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
#include <sys/eventhandler.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/racct.h>
#include <sys/sem.h>
#include <sys/sx.h>
#include <sys/syscall.h>
#include <sys/syscallsubr.h>
#include <sys/sysent.h>
#include <sys/sysctl.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/jail.h>

#include <security/audit/audit.h>
#include <security/mac/mac_framework.h>

FEATURE(sysv_sem, "System V semaphores support");

static MALLOC_DEFINE(M_SEM, "sem", "SVID compatible semaphores");

#ifdef SEM_DEBUG
#define DPRINTF(a)	printf a
#else
#define DPRINTF(a)
#endif

static int seminit(void);
static int sysvsem_modload(struct module *, int, void *);
static int semunload(void);
static void semexit_myhook(void *arg, struct proc *p);
static int sysctl_sema(SYSCTL_HANDLER_ARGS);
static int semvalid(int semid, struct prison *rpr,
    struct semid_kernel *semakptr);
static void sem_remove(int semidx, struct ucred *cred);
static struct prison *sem_find_prison(struct ucred *);
static int sem_prison_cansee(struct prison *, struct semid_kernel *);
static int sem_prison_check(void *, void *);
static int sem_prison_set(void *, void *);
static int sem_prison_get(void *, void *);
static int sem_prison_remove(void *, void *);
static void sem_prison_cleanup(struct prison *);

#ifndef _SYS_SYSPROTO_H_
struct __semctl_args;
int __semctl(struct thread *td, struct __semctl_args *uap);
struct semget_args;
int semget(struct thread *td, struct semget_args *uap);
struct semop_args;
int semop(struct thread *td, struct semop_args *uap);
#endif

static struct sem_undo *semu_alloc(struct thread *td);
static int semundo_adjust(struct thread *td, struct sem_undo **supptr,
    int semid, int semseq, int semnum, int adjval);
static void semundo_clear(int semid, int semnum);

static struct mtx	sem_mtx;	/* semaphore global lock */
static struct mtx sem_undo_mtx;
static int	semtot = 0;
static struct semid_kernel *sema;	/* semaphore id pool */
static struct mtx *sema_mtx;	/* semaphore id pool mutexes*/
static struct sem *sem;		/* semaphore pool */
LIST_HEAD(, sem_undo) semu_list;	/* list of active undo structures */
LIST_HEAD(, sem_undo) semu_free_list;	/* list of free undo structures */
static int	*semu;		/* undo structure pool */
static eventhandler_tag semexit_tag;
static unsigned sem_prison_slot;	/* prison OSD slot */

#define SEMUNDO_MTX		sem_undo_mtx
#define SEMUNDO_LOCK()		mtx_lock(&SEMUNDO_MTX);
#define SEMUNDO_UNLOCK()	mtx_unlock(&SEMUNDO_MTX);
#define SEMUNDO_LOCKASSERT(how)	mtx_assert(&SEMUNDO_MTX, (how));

struct sem {
	u_short	semval;		/* semaphore value */
	pid_t	sempid;		/* pid of last operation */
	u_short	semncnt;	/* # awaiting semval > cval */
	u_short	semzcnt;	/* # awaiting semval = 0 */
};

/*
 * Undo structure (one per process)
 */
struct sem_undo {
	LIST_ENTRY(sem_undo) un_next;	/* ptr to next active undo structure */
	struct	proc *un_proc;		/* owner of this structure */
	short	un_cnt;			/* # of active entries */
	struct undo {
		short	un_adjval;	/* adjust on exit values */
		short	un_num;		/* semaphore # */
		int	un_id;		/* semid */
		unsigned short un_seq;
	} un_ent[1];			/* undo entries */
};

/*
 * Configuration parameters
 */
#ifndef SEMMNI
#define SEMMNI	50		/* # of semaphore identifiers */
#endif
#ifndef SEMMNS
#define SEMMNS	340		/* # of semaphores in system */
#endif
#ifndef SEMUME
#define SEMUME	50		/* max # of undo entries per process */
#endif
#ifndef SEMMNU
#define SEMMNU	150		/* # of undo structures in system */
#endif

/* shouldn't need tuning */
#ifndef SEMMSL
#define SEMMSL	SEMMNS		/* max # of semaphores per id */
#endif
#ifndef SEMOPM
#define SEMOPM	100		/* max # of operations per semop call */
#endif

#define SEMVMX	32767		/* semaphore maximum value */
#define SEMAEM	16384		/* adjust on exit max value */

/*
 * Due to the way semaphore memory is allocated, we have to ensure that
 * SEMUSZ is properly aligned.
 */

#define	SEM_ALIGN(bytes) roundup2(bytes, sizeof(long))

/* actual size of an undo structure */
#define SEMUSZ	SEM_ALIGN(offsetof(struct sem_undo, un_ent[SEMUME]))

/*
 * Macro to find a particular sem_undo vector
 */
#define SEMU(ix) \
	((struct sem_undo *)(((intptr_t)semu)+ix * seminfo.semusz))

/*
 * semaphore info struct
 */
struct seminfo seminfo = {
                SEMMNI,         /* # of semaphore identifiers */
                SEMMNS,         /* # of semaphores in system */
                SEMMNU,         /* # of undo structures in system */
                SEMMSL,         /* max # of semaphores per id */
                SEMOPM,         /* max # of operations per semop call */
                SEMUME,         /* max # of undo entries per process */
                SEMUSZ,         /* size in bytes of undo structure */
                SEMVMX,         /* semaphore maximum value */
                SEMAEM          /* adjust on exit max value */
};

SYSCTL_INT(_kern_ipc, OID_AUTO, semmni, CTLFLAG_RDTUN, &seminfo.semmni, 0,
    "Number of semaphore identifiers");
SYSCTL_INT(_kern_ipc, OID_AUTO, semmns, CTLFLAG_RDTUN, &seminfo.semmns, 0,
    "Maximum number of semaphores in the system");
SYSCTL_INT(_kern_ipc, OID_AUTO, semmnu, CTLFLAG_RDTUN, &seminfo.semmnu, 0,
    "Maximum number of undo structures in the system");
SYSCTL_INT(_kern_ipc, OID_AUTO, semmsl, CTLFLAG_RWTUN, &seminfo.semmsl, 0,
    "Max semaphores per id");
SYSCTL_INT(_kern_ipc, OID_AUTO, semopm, CTLFLAG_RDTUN, &seminfo.semopm, 0,
    "Max operations per semop call");
SYSCTL_INT(_kern_ipc, OID_AUTO, semume, CTLFLAG_RDTUN, &seminfo.semume, 0,
    "Max undo entries per process");
SYSCTL_INT(_kern_ipc, OID_AUTO, semusz, CTLFLAG_RDTUN, &seminfo.semusz, 0,
    "Size in bytes of undo structure");
SYSCTL_INT(_kern_ipc, OID_AUTO, semvmx, CTLFLAG_RWTUN, &seminfo.semvmx, 0,
    "Semaphore maximum value");
SYSCTL_INT(_kern_ipc, OID_AUTO, semaem, CTLFLAG_RWTUN, &seminfo.semaem, 0,
    "Adjust on exit max value");
SYSCTL_PROC(_kern_ipc, OID_AUTO, sema,
    CTLTYPE_OPAQUE | CTLFLAG_RD | CTLFLAG_MPSAFE,
    NULL, 0, sysctl_sema, "",
    "Array of struct semid_kernel for each potential semaphore");

static struct syscall_helper_data sem_syscalls[] = {
	SYSCALL_INIT_HELPER(__semctl),
	SYSCALL_INIT_HELPER(semget),
	SYSCALL_INIT_HELPER(semop),
#if defined(COMPAT_FREEBSD4) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD7)
	SYSCALL_INIT_HELPER(semsys),
	SYSCALL_INIT_HELPER_COMPAT(freebsd7___semctl),
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

static struct syscall_helper_data sem32_syscalls[] = {
	SYSCALL32_INIT_HELPER(freebsd32_semctl),
	SYSCALL32_INIT_HELPER_COMPAT(semget),
	SYSCALL32_INIT_HELPER_COMPAT(semop),
	SYSCALL32_INIT_HELPER(freebsd32_semsys),
#if defined(COMPAT_FREEBSD4) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD7)
	SYSCALL32_INIT_HELPER(freebsd7_freebsd32_semctl),
#endif
	SYSCALL_INIT_LAST
};
#endif

static int
seminit(void)
{
	struct prison *pr;
	void **rsv;
	int i, error;
	osd_method_t methods[PR_MAXMETHOD] = {
	    [PR_METHOD_CHECK] =		sem_prison_check,
	    [PR_METHOD_SET] =		sem_prison_set,
	    [PR_METHOD_GET] =		sem_prison_get,
	    [PR_METHOD_REMOVE] =	sem_prison_remove,
	};

	sem = malloc(sizeof(struct sem) * seminfo.semmns, M_SEM, M_WAITOK);
	sema = malloc(sizeof(struct semid_kernel) * seminfo.semmni, M_SEM,
	    M_WAITOK | M_ZERO);
	sema_mtx = malloc(sizeof(struct mtx) * seminfo.semmni, M_SEM,
	    M_WAITOK | M_ZERO);
	semu = malloc(seminfo.semmnu * seminfo.semusz, M_SEM, M_WAITOK);

	for (i = 0; i < seminfo.semmni; i++) {
		sema[i].u.__sem_base = 0;
		sema[i].u.sem_perm.mode = 0;
		sema[i].u.sem_perm.seq = 0;
#ifdef MAC
		mac_sysvsem_init(&sema[i]);
#endif
	}
	for (i = 0; i < seminfo.semmni; i++)
		mtx_init(&sema_mtx[i], "semid", NULL, MTX_DEF);
	LIST_INIT(&semu_free_list);
	for (i = 0; i < seminfo.semmnu; i++) {
		struct sem_undo *suptr = SEMU(i);
		suptr->un_proc = NULL;
		LIST_INSERT_HEAD(&semu_free_list, suptr, un_next);
	}
	LIST_INIT(&semu_list);
	mtx_init(&sem_mtx, "sem", NULL, MTX_DEF);
	mtx_init(&sem_undo_mtx, "semu", NULL, MTX_DEF);
	semexit_tag = EVENTHANDLER_REGISTER(process_exit, semexit_myhook, NULL,
	    EVENTHANDLER_PRI_ANY);

	/* Set current prisons according to their allow.sysvipc. */
	sem_prison_slot = osd_jail_register(NULL, methods);
	rsv = osd_reserve(sem_prison_slot);
	prison_lock(&prison0);
	(void)osd_jail_set_reserved(&prison0, sem_prison_slot, rsv, &prison0);
	prison_unlock(&prison0);
	rsv = NULL;
	sx_slock(&allprison_lock);
	TAILQ_FOREACH(pr, &allprison, pr_list) {
		if (rsv == NULL)
			rsv = osd_reserve(sem_prison_slot);
		prison_lock(pr);
		if ((pr->pr_allow & PR_ALLOW_SYSVIPC) && pr->pr_ref > 0) {
			(void)osd_jail_set_reserved(pr, sem_prison_slot, rsv,
			    &prison0);
			rsv = NULL;
		}
		prison_unlock(pr);
	}
	if (rsv != NULL)
		osd_free_reserved(rsv);
	sx_sunlock(&allprison_lock);

	error = syscall_helper_register(sem_syscalls, SY_THR_STATIC_KLD);
	if (error != 0)
		return (error);
#ifdef COMPAT_FREEBSD32
	error = syscall32_helper_register(sem32_syscalls, SY_THR_STATIC_KLD);
	if (error != 0)
		return (error);
#endif
	return (0);
}

static int
semunload(void)
{
	int i;

	/* XXXKIB */
	if (semtot != 0)
		return (EBUSY);

#ifdef COMPAT_FREEBSD32
	syscall32_helper_unregister(sem32_syscalls);
#endif
	syscall_helper_unregister(sem_syscalls);
	EVENTHANDLER_DEREGISTER(process_exit, semexit_tag);
	if (sem_prison_slot != 0)
		osd_jail_deregister(sem_prison_slot);
#ifdef MAC
	for (i = 0; i < seminfo.semmni; i++)
		mac_sysvsem_destroy(&sema[i]);
#endif
	free(sem, M_SEM);
	free(sema, M_SEM);
	free(semu, M_SEM);
	for (i = 0; i < seminfo.semmni; i++)
		mtx_destroy(&sema_mtx[i]);
	free(sema_mtx, M_SEM);
	mtx_destroy(&sem_mtx);
	mtx_destroy(&sem_undo_mtx);
	return (0);
}

static int
sysvsem_modload(struct module *module, int cmd, void *arg)
{
	int error = 0;

	switch (cmd) {
	case MOD_LOAD:
		error = seminit();
		if (error != 0)
			semunload();
		break;
	case MOD_UNLOAD:
		error = semunload();
		break;
	case MOD_SHUTDOWN:
		break;
	default:
		error = EINVAL;
		break;
	}
	return (error);
}

static moduledata_t sysvsem_mod = {
	"sysvsem",
	&sysvsem_modload,
	NULL
};

DECLARE_MODULE(sysvsem, sysvsem_mod, SI_SUB_SYSV_SEM, SI_ORDER_FIRST);
MODULE_VERSION(sysvsem, 1);

/*
 * Allocate a new sem_undo structure for a process
 * (returns ptr to structure or NULL if no more room)
 */

static struct sem_undo *
semu_alloc(struct thread *td)
{
	struct sem_undo *suptr;

	SEMUNDO_LOCKASSERT(MA_OWNED);
	if ((suptr = LIST_FIRST(&semu_free_list)) == NULL)
		return (NULL);
	LIST_REMOVE(suptr, un_next);
	LIST_INSERT_HEAD(&semu_list, suptr, un_next);
	suptr->un_cnt = 0;
	suptr->un_proc = td->td_proc;
	return (suptr);
}

static int
semu_try_free(struct sem_undo *suptr)
{

	SEMUNDO_LOCKASSERT(MA_OWNED);

	if (suptr->un_cnt != 0)
		return (0);
	LIST_REMOVE(suptr, un_next);
	LIST_INSERT_HEAD(&semu_free_list, suptr, un_next);
	return (1);
}

/*
 * Adjust a particular entry for a particular proc
 */

static int
semundo_adjust(struct thread *td, struct sem_undo **supptr, int semid,
    int semseq, int semnum, int adjval)
{
	struct proc *p = td->td_proc;
	struct sem_undo *suptr;
	struct undo *sunptr;
	int i;

	SEMUNDO_LOCKASSERT(MA_OWNED);
	/* Look for and remember the sem_undo if the caller doesn't provide
	   it */

	suptr = *supptr;
	if (suptr == NULL) {
		LIST_FOREACH(suptr, &semu_list, un_next) {
			if (suptr->un_proc == p) {
				*supptr = suptr;
				break;
			}
		}
		if (suptr == NULL) {
			if (adjval == 0)
				return(0);
			suptr = semu_alloc(td);
			if (suptr == NULL)
				return (ENOSPC);
			*supptr = suptr;
		}
	}

	/*
	 * Look for the requested entry and adjust it (delete if adjval becomes
	 * 0).
	 */
	sunptr = &suptr->un_ent[0];
	for (i = 0; i < suptr->un_cnt; i++, sunptr++) {
		if (sunptr->un_id != semid || sunptr->un_num != semnum)
			continue;
		if (adjval != 0) {
			adjval += sunptr->un_adjval;
			if (adjval > seminfo.semaem || adjval < -seminfo.semaem)
				return (ERANGE);
		}
		sunptr->un_adjval = adjval;
		if (sunptr->un_adjval == 0) {
			suptr->un_cnt--;
			if (i < suptr->un_cnt)
				suptr->un_ent[i] =
				    suptr->un_ent[suptr->un_cnt];
			if (suptr->un_cnt == 0)
				semu_try_free(suptr);
		}
		return (0);
	}

	/* Didn't find the right entry - create it */
	if (adjval == 0)
		return (0);
	if (adjval > seminfo.semaem || adjval < -seminfo.semaem)
		return (ERANGE);
	if (suptr->un_cnt != seminfo.semume) {
		sunptr = &suptr->un_ent[suptr->un_cnt];
		suptr->un_cnt++;
		sunptr->un_adjval = adjval;
		sunptr->un_id = semid;
		sunptr->un_num = semnum;
		sunptr->un_seq = semseq;
	} else
		return (EINVAL);
	return (0);
}

static void
semundo_clear(int semid, int semnum)
{
	struct sem_undo *suptr, *suptr1;
	struct undo *sunptr;
	int i;

	SEMUNDO_LOCKASSERT(MA_OWNED);
	LIST_FOREACH_SAFE(suptr, &semu_list, un_next, suptr1) {
		sunptr = &suptr->un_ent[0];
		for (i = 0; i < suptr->un_cnt; i++, sunptr++) {
			if (sunptr->un_id != semid)
				continue;
			if (semnum == -1 || sunptr->un_num == semnum) {
				suptr->un_cnt--;
				if (i < suptr->un_cnt) {
					suptr->un_ent[i] =
					    suptr->un_ent[suptr->un_cnt];
					continue;
				}
				semu_try_free(suptr);
			}
			if (semnum != -1)
				break;
		}
	}
}

static int
semvalid(int semid, struct prison *rpr, struct semid_kernel *semakptr)
{

	return ((semakptr->u.sem_perm.mode & SEM_ALLOC) == 0 ||
	    semakptr->u.sem_perm.seq != IPCID_TO_SEQ(semid) ||
	    sem_prison_cansee(rpr, semakptr) ? EINVAL : 0);
}

static void
sem_remove(int semidx, struct ucred *cred)
{
	struct semid_kernel *semakptr;
	int i;

	KASSERT(semidx >= 0 && semidx < seminfo.semmni,
		("semidx out of bounds"));
	semakptr = &sema[semidx];
	semakptr->u.sem_perm.cuid = cred ? cred->cr_uid : 0;
	semakptr->u.sem_perm.uid = cred ? cred->cr_uid : 0;
	semakptr->u.sem_perm.mode = 0;
	racct_sub_cred(semakptr->cred, RACCT_NSEM, semakptr->u.sem_nsems);
	crfree(semakptr->cred);
	semakptr->cred = NULL;
	SEMUNDO_LOCK();
	semundo_clear(semidx, -1);
	SEMUNDO_UNLOCK();
#ifdef MAC
	mac_sysvsem_cleanup(semakptr);
#endif
	wakeup(semakptr);
	for (i = 0; i < seminfo.semmni; i++) {
		if ((sema[i].u.sem_perm.mode & SEM_ALLOC) &&
		    sema[i].u.__sem_base > semakptr->u.__sem_base)
			mtx_lock_flags(&sema_mtx[i], LOP_DUPOK);
	}
	for (i = semakptr->u.__sem_base - sem; i < semtot; i++)
		sem[i] = sem[i + semakptr->u.sem_nsems];
	for (i = 0; i < seminfo.semmni; i++) {
		if ((sema[i].u.sem_perm.mode & SEM_ALLOC) &&
		    sema[i].u.__sem_base > semakptr->u.__sem_base) {
			sema[i].u.__sem_base -= semakptr->u.sem_nsems;
			mtx_unlock(&sema_mtx[i]);
		}
	}
	semtot -= semakptr->u.sem_nsems;
}

static struct prison *
sem_find_prison(struct ucred *cred)
{
	struct prison *pr, *rpr;

	pr = cred->cr_prison;
	prison_lock(pr);
	rpr = osd_jail_get(pr, sem_prison_slot);
	prison_unlock(pr);
	return rpr;
}

static int
sem_prison_cansee(struct prison *rpr, struct semid_kernel *semakptr)
{

	if (semakptr->cred == NULL ||
	    !(rpr == semakptr->cred->cr_prison ||
	      prison_ischild(rpr, semakptr->cred->cr_prison)))
		return (EINVAL);
	return (0);
}

/*
 * Note that the user-mode half of this passes a union, not a pointer.
 */
#ifndef _SYS_SYSPROTO_H_
struct __semctl_args {
	int	semid;
	int	semnum;
	int	cmd;
	union	semun *arg;
};
#endif
int
sys___semctl(struct thread *td, struct __semctl_args *uap)
{
	struct semid_ds dsbuf;
	union semun arg, semun;
	register_t rval;
	int error;

	switch (uap->cmd) {
	case SEM_STAT:
	case IPC_SET:
	case IPC_STAT:
	case GETALL:
	case SETVAL:
	case SETALL:
		error = copyin(uap->arg, &arg, sizeof(arg));
		if (error)
			return (error);
		break;
	}

	switch (uap->cmd) {
	case SEM_STAT:
	case IPC_STAT:
		semun.buf = &dsbuf;
		break;
	case IPC_SET:
		error = copyin(arg.buf, &dsbuf, sizeof(dsbuf));
		if (error)
			return (error);
		semun.buf = &dsbuf;
		break;
	case GETALL:
	case SETALL:
		semun.array = arg.array;
		break;
	case SETVAL:
		semun.val = arg.val;
		break;		
	}

	error = kern_semctl(td, uap->semid, uap->semnum, uap->cmd, &semun,
	    &rval);
	if (error)
		return (error);

	switch (uap->cmd) {
	case SEM_STAT:
	case IPC_STAT:
		error = copyout(&dsbuf, arg.buf, sizeof(dsbuf));
		break;
	}

	if (error == 0)
		td->td_retval[0] = rval;
	return (error);
}

int
kern_semctl(struct thread *td, int semid, int semnum, int cmd,
    union semun *arg, register_t *rval)
{
	u_short *array;
	struct ucred *cred = td->td_ucred;
	int i, error;
	struct prison *rpr;
	struct semid_ds *sbuf;
	struct semid_kernel *semakptr;
	struct mtx *sema_mtxp;
	u_short usval, count;
	int semidx;

	DPRINTF(("call to semctl(%d, %d, %d, 0x%p)\n",
	    semid, semnum, cmd, arg));

	AUDIT_ARG_SVIPC_CMD(cmd);
	AUDIT_ARG_SVIPC_ID(semid);

	rpr = sem_find_prison(td->td_ucred);
	if (sem == NULL)
		return (ENOSYS);

	array = NULL;

	switch(cmd) {
	case SEM_STAT:
		/*
		 * For this command we assume semid is an array index
		 * rather than an IPC id.
		 */
		if (semid < 0 || semid >= seminfo.semmni)
			return (EINVAL);
		semakptr = &sema[semid];
		sema_mtxp = &sema_mtx[semid];
		mtx_lock(sema_mtxp);
		if ((semakptr->u.sem_perm.mode & SEM_ALLOC) == 0) {
			error = EINVAL;
			goto done2;
		}
		if ((error = sem_prison_cansee(rpr, semakptr)))
			goto done2;
		if ((error = ipcperm(td, &semakptr->u.sem_perm, IPC_R)))
			goto done2;
#ifdef MAC
		error = mac_sysvsem_check_semctl(cred, semakptr, cmd);
		if (error != 0)
			goto done2;
#endif
		bcopy(&semakptr->u, arg->buf, sizeof(struct semid_ds));
		if (cred->cr_prison != semakptr->cred->cr_prison)
			arg->buf->sem_perm.key = IPC_PRIVATE;
		*rval = IXSEQ_TO_IPCID(semid, semakptr->u.sem_perm);
		mtx_unlock(sema_mtxp);
		return (0);
	}

	semidx = IPCID_TO_IX(semid);
	if (semidx < 0 || semidx >= seminfo.semmni)
		return (EINVAL);

	semakptr = &sema[semidx];
	sema_mtxp = &sema_mtx[semidx];
	if (cmd == IPC_RMID)
		mtx_lock(&sem_mtx);
	mtx_lock(sema_mtxp);

#ifdef MAC
	error = mac_sysvsem_check_semctl(cred, semakptr, cmd);
	if (error != 0)
		goto done2;
#endif

	error = 0;
	*rval = 0;

	switch (cmd) {
	case IPC_RMID:
		if ((error = semvalid(semid, rpr, semakptr)) != 0)
			goto done2;
		if ((error = ipcperm(td, &semakptr->u.sem_perm, IPC_M)))
			goto done2;
		sem_remove(semidx, cred);
		break;

	case IPC_SET:
		AUDIT_ARG_SVIPC_PERM(&arg->buf->sem_perm);
		if ((error = semvalid(semid, rpr, semakptr)) != 0)
			goto done2;
		if ((error = ipcperm(td, &semakptr->u.sem_perm, IPC_M)))
			goto done2;
		sbuf = arg->buf;
		semakptr->u.sem_perm.uid = sbuf->sem_perm.uid;
		semakptr->u.sem_perm.gid = sbuf->sem_perm.gid;
		semakptr->u.sem_perm.mode = (semakptr->u.sem_perm.mode &
		    ~0777) | (sbuf->sem_perm.mode & 0777);
		semakptr->u.sem_ctime = time_second;
		break;

	case IPC_STAT:
		if ((error = semvalid(semid, rpr, semakptr)) != 0)
			goto done2;
		if ((error = ipcperm(td, &semakptr->u.sem_perm, IPC_R)))
			goto done2;
		bcopy(&semakptr->u, arg->buf, sizeof(struct semid_ds));
		if (cred->cr_prison != semakptr->cred->cr_prison)
			arg->buf->sem_perm.key = IPC_PRIVATE;
		break;

	case GETNCNT:
		if ((error = semvalid(semid, rpr, semakptr)) != 0)
			goto done2;
		if ((error = ipcperm(td, &semakptr->u.sem_perm, IPC_R)))
			goto done2;
		if (semnum < 0 || semnum >= semakptr->u.sem_nsems) {
			error = EINVAL;
			goto done2;
		}
		*rval = semakptr->u.__sem_base[semnum].semncnt;
		break;

	case GETPID:
		if ((error = semvalid(semid, rpr, semakptr)) != 0)
			goto done2;
		if ((error = ipcperm(td, &semakptr->u.sem_perm, IPC_R)))
			goto done2;
		if (semnum < 0 || semnum >= semakptr->u.sem_nsems) {
			error = EINVAL;
			goto done2;
		}
		*rval = semakptr->u.__sem_base[semnum].sempid;
		break;

	case GETVAL:
		if ((error = semvalid(semid, rpr, semakptr)) != 0)
			goto done2;
		if ((error = ipcperm(td, &semakptr->u.sem_perm, IPC_R)))
			goto done2;
		if (semnum < 0 || semnum >= semakptr->u.sem_nsems) {
			error = EINVAL;
			goto done2;
		}
		*rval = semakptr->u.__sem_base[semnum].semval;
		break;

	case GETALL:
		/*
		 * Unfortunately, callers of this function don't know
		 * in advance how many semaphores are in this set.
		 * While we could just allocate the maximum size array
		 * and pass the actual size back to the caller, that
		 * won't work for SETALL since we can't copyin() more
		 * data than the user specified as we may return a
		 * spurious EFAULT.
		 * 
		 * Note that the number of semaphores in a set is
		 * fixed for the life of that set.  The only way that
		 * the 'count' could change while are blocked in
		 * malloc() is if this semaphore set were destroyed
		 * and a new one created with the same index.
		 * However, semvalid() will catch that due to the
		 * sequence number unless exactly 0x8000 (or a
		 * multiple thereof) semaphore sets for the same index
		 * are created and destroyed while we are in malloc!
		 *
		 */
		count = semakptr->u.sem_nsems;
		mtx_unlock(sema_mtxp);		    
		array = malloc(sizeof(*array) * count, M_TEMP, M_WAITOK);
		mtx_lock(sema_mtxp);
		if ((error = semvalid(semid, rpr, semakptr)) != 0)
			goto done2;
		KASSERT(count == semakptr->u.sem_nsems, ("nsems changed"));
		if ((error = ipcperm(td, &semakptr->u.sem_perm, IPC_R)))
			goto done2;
		for (i = 0; i < semakptr->u.sem_nsems; i++)
			array[i] = semakptr->u.__sem_base[i].semval;
		mtx_unlock(sema_mtxp);
		error = copyout(array, arg->array, count * sizeof(*array));
		mtx_lock(sema_mtxp);
		break;

	case GETZCNT:
		if ((error = semvalid(semid, rpr, semakptr)) != 0)
			goto done2;
		if ((error = ipcperm(td, &semakptr->u.sem_perm, IPC_R)))
			goto done2;
		if (semnum < 0 || semnum >= semakptr->u.sem_nsems) {
			error = EINVAL;
			goto done2;
		}
		*rval = semakptr->u.__sem_base[semnum].semzcnt;
		break;

	case SETVAL:
		if ((error = semvalid(semid, rpr, semakptr)) != 0)
			goto done2;
		if ((error = ipcperm(td, &semakptr->u.sem_perm, IPC_W)))
			goto done2;
		if (semnum < 0 || semnum >= semakptr->u.sem_nsems) {
			error = EINVAL;
			goto done2;
		}
		if (arg->val < 0 || arg->val > seminfo.semvmx) {
			error = ERANGE;
			goto done2;
		}
		semakptr->u.__sem_base[semnum].semval = arg->val;
		SEMUNDO_LOCK();
		semundo_clear(semidx, semnum);
		SEMUNDO_UNLOCK();
		wakeup(semakptr);
		break;

	case SETALL:
		/*
		 * See comment on GETALL for why 'count' shouldn't change
		 * and why we require a userland buffer.
		 */
		count = semakptr->u.sem_nsems;
		mtx_unlock(sema_mtxp);		    
		array = malloc(sizeof(*array) * count, M_TEMP, M_WAITOK);
		error = copyin(arg->array, array, count * sizeof(*array));
		mtx_lock(sema_mtxp);
		if (error)
			break;
		if ((error = semvalid(semid, rpr, semakptr)) != 0)
			goto done2;
		KASSERT(count == semakptr->u.sem_nsems, ("nsems changed"));
		if ((error = ipcperm(td, &semakptr->u.sem_perm, IPC_W)))
			goto done2;
		for (i = 0; i < semakptr->u.sem_nsems; i++) {
			usval = array[i];
			if (usval > seminfo.semvmx) {
				error = ERANGE;
				break;
			}
			semakptr->u.__sem_base[i].semval = usval;
		}
		SEMUNDO_LOCK();
		semundo_clear(semidx, -1);
		SEMUNDO_UNLOCK();
		wakeup(semakptr);
		break;

	default:
		error = EINVAL;
		break;
	}

done2:
	mtx_unlock(sema_mtxp);
	if (cmd == IPC_RMID)
		mtx_unlock(&sem_mtx);
	if (array != NULL)
		free(array, M_TEMP);
	return(error);
}

#ifndef _SYS_SYSPROTO_H_
struct semget_args {
	key_t	key;
	int	nsems;
	int	semflg;
};
#endif
int
sys_semget(struct thread *td, struct semget_args *uap)
{
	int semid, error = 0;
	int key = uap->key;
	int nsems = uap->nsems;
	int semflg = uap->semflg;
	struct ucred *cred = td->td_ucred;

	DPRINTF(("semget(0x%x, %d, 0%o)\n", key, nsems, semflg));

	AUDIT_ARG_VALUE(semflg);

	if (sem_find_prison(cred) == NULL)
		return (ENOSYS);

	mtx_lock(&sem_mtx);
	if (key != IPC_PRIVATE) {
		for (semid = 0; semid < seminfo.semmni; semid++) {
			if ((sema[semid].u.sem_perm.mode & SEM_ALLOC) &&
			    sema[semid].cred != NULL &&
			    sema[semid].cred->cr_prison == cred->cr_prison &&
			    sema[semid].u.sem_perm.key == key)
				break;
		}
		if (semid < seminfo.semmni) {
			AUDIT_ARG_SVIPC_ID(semid);
			DPRINTF(("found public key\n"));
			if ((semflg & IPC_CREAT) && (semflg & IPC_EXCL)) {
				DPRINTF(("not exclusive\n"));
				error = EEXIST;
				goto done2;
			}
			if ((error = ipcperm(td, &sema[semid].u.sem_perm,
			    semflg & 0700))) {
				goto done2;
			}
			if (nsems > 0 && sema[semid].u.sem_nsems < nsems) {
				DPRINTF(("too small\n"));
				error = EINVAL;
				goto done2;
			}
#ifdef MAC
			error = mac_sysvsem_check_semget(cred, &sema[semid]);
			if (error != 0)
				goto done2;
#endif
			goto found;
		}
	}

	DPRINTF(("need to allocate the semid_kernel\n"));
	if (key == IPC_PRIVATE || (semflg & IPC_CREAT)) {
		if (nsems <= 0 || nsems > seminfo.semmsl) {
			DPRINTF(("nsems out of range (0<%d<=%d)\n", nsems,
			    seminfo.semmsl));
			error = EINVAL;
			goto done2;
		}
		if (nsems > seminfo.semmns - semtot) {
			DPRINTF((
			    "not enough semaphores left (need %d, got %d)\n",
			    nsems, seminfo.semmns - semtot));
			error = ENOSPC;
			goto done2;
		}
		for (semid = 0; semid < seminfo.semmni; semid++) {
			if ((sema[semid].u.sem_perm.mode & SEM_ALLOC) == 0)
				break;
		}
		if (semid == seminfo.semmni) {
			DPRINTF(("no more semid_kernel's available\n"));
			error = ENOSPC;
			goto done2;
		}
#ifdef RACCT
		if (racct_enable) {
			PROC_LOCK(td->td_proc);
			error = racct_add(td->td_proc, RACCT_NSEM, nsems);
			PROC_UNLOCK(td->td_proc);
			if (error != 0) {
				error = ENOSPC;
				goto done2;
			}
		}
#endif
		DPRINTF(("semid %d is available\n", semid));
		mtx_lock(&sema_mtx[semid]);
		KASSERT((sema[semid].u.sem_perm.mode & SEM_ALLOC) == 0,
		    ("Lost semaphore %d", semid));
		sema[semid].u.sem_perm.key = key;
		sema[semid].u.sem_perm.cuid = cred->cr_uid;
		sema[semid].u.sem_perm.uid = cred->cr_uid;
		sema[semid].u.sem_perm.cgid = cred->cr_gid;
		sema[semid].u.sem_perm.gid = cred->cr_gid;
		sema[semid].u.sem_perm.mode = (semflg & 0777) | SEM_ALLOC;
		sema[semid].cred = crhold(cred);
		sema[semid].u.sem_perm.seq =
		    (sema[semid].u.sem_perm.seq + 1) & 0x7fff;
		sema[semid].u.sem_nsems = nsems;
		sema[semid].u.sem_otime = 0;
		sema[semid].u.sem_ctime = time_second;
		sema[semid].u.__sem_base = &sem[semtot];
		semtot += nsems;
		bzero(sema[semid].u.__sem_base,
		    sizeof(sema[semid].u.__sem_base[0])*nsems);
#ifdef MAC
		mac_sysvsem_create(cred, &sema[semid]);
#endif
		mtx_unlock(&sema_mtx[semid]);
		DPRINTF(("sembase = %p, next = %p\n",
		    sema[semid].u.__sem_base, &sem[semtot]));
	} else {
		DPRINTF(("didn't find it and wasn't asked to create it\n"));
		error = ENOENT;
		goto done2;
	}

found:
	td->td_retval[0] = IXSEQ_TO_IPCID(semid, sema[semid].u.sem_perm);
done2:
	mtx_unlock(&sem_mtx);
	return (error);
}

#ifndef _SYS_SYSPROTO_H_
struct semop_args {
	int	semid;
	struct	sembuf *sops;
	size_t	nsops;
};
#endif
int
sys_semop(struct thread *td, struct semop_args *uap)
{
#define SMALL_SOPS	8
	struct sembuf small_sops[SMALL_SOPS];
	int semid = uap->semid;
	size_t nsops = uap->nsops;
	struct prison *rpr;
	struct sembuf *sops;
	struct semid_kernel *semakptr;
	struct sembuf *sopptr = NULL;
	struct sem *semptr = NULL;
	struct sem_undo *suptr;
	struct mtx *sema_mtxp;
	size_t i, j, k;
	int error;
	int do_wakeup, do_undos;
	unsigned short seq;

#ifdef SEM_DEBUG
	sops = NULL;
#endif
	DPRINTF(("call to semop(%d, %p, %u)\n", semid, sops, nsops));

	AUDIT_ARG_SVIPC_ID(semid);

	rpr = sem_find_prison(td->td_ucred);
	if (sem == NULL)
		return (ENOSYS);

	semid = IPCID_TO_IX(semid);	/* Convert back to zero origin */

	if (semid < 0 || semid >= seminfo.semmni)
		return (EINVAL);

	/* Allocate memory for sem_ops */
	if (nsops <= SMALL_SOPS)
		sops = small_sops;
	else if (nsops > seminfo.semopm) {
		DPRINTF(("too many sops (max=%d, nsops=%d)\n", seminfo.semopm,
		    nsops));
		return (E2BIG);
	} else {
#ifdef RACCT
		if (racct_enable) {
			PROC_LOCK(td->td_proc);
			if (nsops >
			    racct_get_available(td->td_proc, RACCT_NSEMOP)) {
				PROC_UNLOCK(td->td_proc);
				return (E2BIG);
			}
			PROC_UNLOCK(td->td_proc);
		}
#endif

		sops = malloc(nsops * sizeof(*sops), M_TEMP, M_WAITOK);
	}
	if ((error = copyin(uap->sops, sops, nsops * sizeof(sops[0]))) != 0) {
		DPRINTF(("error = %d from copyin(%p, %p, %d)\n", error,
		    uap->sops, sops, nsops * sizeof(sops[0])));
		if (sops != small_sops)
			free(sops, M_SEM);
		return (error);
	}

	semakptr = &sema[semid];
	sema_mtxp = &sema_mtx[semid];
	mtx_lock(sema_mtxp);
	if ((semakptr->u.sem_perm.mode & SEM_ALLOC) == 0) {
		error = EINVAL;
		goto done2;
	}
	seq = semakptr->u.sem_perm.seq;
	if (seq != IPCID_TO_SEQ(uap->semid)) {
		error = EINVAL;
		goto done2;
	}
	if ((error = sem_prison_cansee(rpr, semakptr)) != 0)
		goto done2;
	/*
	 * Initial pass through sops to see what permissions are needed.
	 * Also perform any checks that don't need repeating on each
	 * attempt to satisfy the request vector.
	 */
	j = 0;		/* permission needed */
	do_undos = 0;
	for (i = 0; i < nsops; i++) {
		sopptr = &sops[i];
		if (sopptr->sem_num >= semakptr->u.sem_nsems) {
			error = EFBIG;
			goto done2;
		}
		if (sopptr->sem_flg & SEM_UNDO && sopptr->sem_op != 0)
			do_undos = 1;
		j |= (sopptr->sem_op == 0) ? SEM_R : SEM_A;
	}

	if ((error = ipcperm(td, &semakptr->u.sem_perm, j))) {
		DPRINTF(("error = %d from ipaccess\n", error));
		goto done2;
	}
#ifdef MAC
	error = mac_sysvsem_check_semop(td->td_ucred, semakptr, j);
	if (error != 0)
		goto done2;
#endif

	/*
	 * Loop trying to satisfy the vector of requests.
	 * If we reach a point where we must wait, any requests already
	 * performed are rolled back and we go to sleep until some other
	 * process wakes us up.  At this point, we start all over again.
	 *
	 * This ensures that from the perspective of other tasks, a set
	 * of requests is atomic (never partially satisfied).
	 */
	for (;;) {
		do_wakeup = 0;
		error = 0;	/* error return if necessary */

		for (i = 0; i < nsops; i++) {
			sopptr = &sops[i];
			semptr = &semakptr->u.__sem_base[sopptr->sem_num];

			DPRINTF((
			    "semop:  semakptr=%p, __sem_base=%p, "
			    "semptr=%p, sem[%d]=%d : op=%d, flag=%s\n",
			    semakptr, semakptr->u.__sem_base, semptr,
			    sopptr->sem_num, semptr->semval, sopptr->sem_op,
			    (sopptr->sem_flg & IPC_NOWAIT) ?
			    "nowait" : "wait"));

			if (sopptr->sem_op < 0) {
				if (semptr->semval + sopptr->sem_op < 0) {
					DPRINTF(("semop:  can't do it now\n"));
					break;
				} else {
					semptr->semval += sopptr->sem_op;
					if (semptr->semval == 0 &&
					    semptr->semzcnt > 0)
						do_wakeup = 1;
				}
			} else if (sopptr->sem_op == 0) {
				if (semptr->semval != 0) {
					DPRINTF(("semop:  not zero now\n"));
					break;
				}
			} else if (semptr->semval + sopptr->sem_op >
			    seminfo.semvmx) {
				error = ERANGE;
				break;
			} else {
				if (semptr->semncnt > 0)
					do_wakeup = 1;
				semptr->semval += sopptr->sem_op;
			}
		}

		/*
		 * Did we get through the entire vector?
		 */
		if (i >= nsops)
			goto done;

		/*
		 * No ... rollback anything that we've already done
		 */
		DPRINTF(("semop:  rollback 0 through %d\n", i-1));
		for (j = 0; j < i; j++)
			semakptr->u.__sem_base[sops[j].sem_num].semval -=
			    sops[j].sem_op;

		/* If we detected an error, return it */
		if (error != 0)
			goto done2;

		/*
		 * If the request that we couldn't satisfy has the
		 * NOWAIT flag set then return with EAGAIN.
		 */
		if (sopptr->sem_flg & IPC_NOWAIT) {
			error = EAGAIN;
			goto done2;
		}

		if (sopptr->sem_op == 0)
			semptr->semzcnt++;
		else
			semptr->semncnt++;

		DPRINTF(("semop:  good night!\n"));
		error = msleep(semakptr, sema_mtxp, (PZERO - 4) | PCATCH,
		    "semwait", 0);
		DPRINTF(("semop:  good morning (error=%d)!\n", error));
		/* return code is checked below, after sem[nz]cnt-- */

		/*
		 * Make sure that the semaphore still exists
		 */
		seq = semakptr->u.sem_perm.seq;
		if ((semakptr->u.sem_perm.mode & SEM_ALLOC) == 0 ||
		    seq != IPCID_TO_SEQ(uap->semid)) {
			error = EIDRM;
			goto done2;
		}

		/*
		 * Renew the semaphore's pointer after wakeup since
		 * during msleep __sem_base may have been modified and semptr
		 * is not valid any more
		 */
		semptr = &semakptr->u.__sem_base[sopptr->sem_num];

		/*
		 * The semaphore is still alive.  Readjust the count of
		 * waiting processes.
		 */
		if (sopptr->sem_op == 0)
			semptr->semzcnt--;
		else
			semptr->semncnt--;

		/*
		 * Is it really morning, or was our sleep interrupted?
		 * (Delayed check of msleep() return code because we
		 * need to decrement sem[nz]cnt either way.)
		 */
		if (error != 0) {
			error = EINTR;
			goto done2;
		}
		DPRINTF(("semop:  good morning!\n"));
	}

done:
	/*
	 * Process any SEM_UNDO requests.
	 */
	if (do_undos) {
		SEMUNDO_LOCK();
		suptr = NULL;
		for (i = 0; i < nsops; i++) {
			/*
			 * We only need to deal with SEM_UNDO's for non-zero
			 * op's.
			 */
			int adjval;

			if ((sops[i].sem_flg & SEM_UNDO) == 0)
				continue;
			adjval = sops[i].sem_op;
			if (adjval == 0)
				continue;
			error = semundo_adjust(td, &suptr, semid, seq,
			    sops[i].sem_num, -adjval);
			if (error == 0)
				continue;

			/*
			 * Oh-Oh!  We ran out of either sem_undo's or undo's.
			 * Rollback the adjustments to this point and then
			 * rollback the semaphore ups and down so we can return
			 * with an error with all structures restored.  We
			 * rollback the undo's in the exact reverse order that
			 * we applied them.  This guarantees that we won't run
			 * out of space as we roll things back out.
			 */
			for (j = 0; j < i; j++) {
				k = i - j - 1;
				if ((sops[k].sem_flg & SEM_UNDO) == 0)
					continue;
				adjval = sops[k].sem_op;
				if (adjval == 0)
					continue;
				if (semundo_adjust(td, &suptr, semid, seq,
				    sops[k].sem_num, adjval) != 0)
					panic("semop - can't undo undos");
			}

			for (j = 0; j < nsops; j++)
				semakptr->u.__sem_base[sops[j].sem_num].semval -=
				    sops[j].sem_op;

			DPRINTF(("error = %d from semundo_adjust\n", error));
			SEMUNDO_UNLOCK();
			goto done2;
		} /* loop through the sops */
		SEMUNDO_UNLOCK();
	} /* if (do_undos) */

	/* We're definitely done - set the sempid's and time */
	for (i = 0; i < nsops; i++) {
		sopptr = &sops[i];
		semptr = &semakptr->u.__sem_base[sopptr->sem_num];
		semptr->sempid = td->td_proc->p_pid;
	}
	semakptr->u.sem_otime = time_second;

	/*
	 * Do a wakeup if any semaphore was up'd whilst something was
	 * sleeping on it.
	 */
	if (do_wakeup) {
		DPRINTF(("semop:  doing wakeup\n"));
		wakeup(semakptr);
		DPRINTF(("semop:  back from wakeup\n"));
	}
	DPRINTF(("semop:  done\n"));
	td->td_retval[0] = 0;
done2:
	mtx_unlock(sema_mtxp);
	if (sops != small_sops)
		free(sops, M_SEM);
	return (error);
}

/*
 * Go through the undo structures for this process and apply the adjustments to
 * semaphores.
 */
static void
semexit_myhook(void *arg, struct proc *p)
{
	struct sem_undo *suptr;
	struct semid_kernel *semakptr;
	struct mtx *sema_mtxp;
	int semid, semnum, adjval, ix;
	unsigned short seq;

	/*
	 * Go through the chain of undo vectors looking for one
	 * associated with this process.
	 */
	if (LIST_EMPTY(&semu_list))
		return;
	SEMUNDO_LOCK();
	LIST_FOREACH(suptr, &semu_list, un_next) {
		if (suptr->un_proc == p)
			break;
	}
	if (suptr == NULL) {
		SEMUNDO_UNLOCK();
		return;
	}
	LIST_REMOVE(suptr, un_next);

	DPRINTF(("proc @%p has undo structure with %d entries\n", p,
	    suptr->un_cnt));

	/*
	 * If there are any active undo elements then process them.
	 */
	if (suptr->un_cnt > 0) {
		SEMUNDO_UNLOCK();
		for (ix = 0; ix < suptr->un_cnt; ix++) {
			semid = suptr->un_ent[ix].un_id;
			semnum = suptr->un_ent[ix].un_num;
			adjval = suptr->un_ent[ix].un_adjval;
			seq = suptr->un_ent[ix].un_seq;
			semakptr = &sema[semid];
			sema_mtxp = &sema_mtx[semid];

			mtx_lock(sema_mtxp);
			if ((semakptr->u.sem_perm.mode & SEM_ALLOC) == 0 ||
			    (semakptr->u.sem_perm.seq != seq)) {
				mtx_unlock(sema_mtxp);
				continue;
			}
			if (semnum >= semakptr->u.sem_nsems)
				panic("semexit - semnum out of range");

			DPRINTF((
			    "semexit:  %p id=%d num=%d(adj=%d) ; sem=%d\n",
			    suptr->un_proc, suptr->un_ent[ix].un_id,
			    suptr->un_ent[ix].un_num,
			    suptr->un_ent[ix].un_adjval,
			    semakptr->u.__sem_base[semnum].semval));

			if (adjval < 0 && semakptr->u.__sem_base[semnum].semval <
			    -adjval)
				semakptr->u.__sem_base[semnum].semval = 0;
			else
				semakptr->u.__sem_base[semnum].semval += adjval;

			wakeup(semakptr);
			DPRINTF(("semexit:  back from wakeup\n"));
			mtx_unlock(sema_mtxp);
		}
		SEMUNDO_LOCK();
	}

	/*
	 * Deallocate the undo vector.
	 */
	DPRINTF(("removing vector\n"));
	suptr->un_proc = NULL;
	suptr->un_cnt = 0;
	LIST_INSERT_HEAD(&semu_free_list, suptr, un_next);
	SEMUNDO_UNLOCK();
}

static int
sysctl_sema(SYSCTL_HANDLER_ARGS)
{
	struct prison *pr, *rpr;
	struct semid_kernel tsemak;
#ifdef COMPAT_FREEBSD32
	struct semid_kernel32 tsemak32;
#endif
	void *outaddr;
	size_t outsize;
	int error, i;

	pr = req->td->td_ucred->cr_prison;
	rpr = sem_find_prison(req->td->td_ucred);
	error = 0;
	for (i = 0; i < seminfo.semmni; i++) {
		mtx_lock(&sema_mtx[i]);
		if ((sema[i].u.sem_perm.mode & SEM_ALLOC) == 0 ||
		    rpr == NULL || sem_prison_cansee(rpr, &sema[i]) != 0)
			bzero(&tsemak, sizeof(tsemak));
		else {
			tsemak = sema[i];
			if (tsemak.cred->cr_prison != pr)
				tsemak.u.sem_perm.key = IPC_PRIVATE;
		}
		mtx_unlock(&sema_mtx[i]);
#ifdef COMPAT_FREEBSD32
		if (SV_CURPROC_FLAG(SV_ILP32)) {
			bzero(&tsemak32, sizeof(tsemak32));
			freebsd32_ipcperm_out(&tsemak.u.sem_perm,
			    &tsemak32.u.sem_perm);
			/* Don't copy u.__sem_base */
			CP(tsemak, tsemak32, u.sem_nsems);
			CP(tsemak, tsemak32, u.sem_otime);
			CP(tsemak, tsemak32, u.sem_ctime);
			/* Don't copy label or cred */
			outaddr = &tsemak32;
			outsize = sizeof(tsemak32);
		} else
#endif
		{
			tsemak.u.__sem_base = NULL;
			tsemak.label = NULL;
			tsemak.cred = NULL;
			outaddr = &tsemak;
			outsize = sizeof(tsemak);
		}
		error = SYSCTL_OUT(req, outaddr, outsize);
		if (error != 0)
			break;
	}
	return (error);
}

static int
sem_prison_check(void *obj, void *data)
{
	struct prison *pr = obj;
	struct prison *prpr;
	struct vfsoptlist *opts = data;
	int error, jsys;

	/*
	 * sysvsem is a jailsys integer.
	 * It must be "disable" if the parent jail is disabled.
	 */
	error = vfs_copyopt(opts, "sysvsem", &jsys, sizeof(jsys));
	if (error != ENOENT) {
		if (error != 0)
			return (error);
		switch (jsys) {
		case JAIL_SYS_DISABLE:
			break;
		case JAIL_SYS_NEW:
		case JAIL_SYS_INHERIT:
			prison_lock(pr->pr_parent);
			prpr = osd_jail_get(pr->pr_parent, sem_prison_slot);
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
sem_prison_set(void *obj, void *data)
{
	struct prison *pr = obj;
	struct prison *tpr, *orpr, *nrpr, *trpr;
	struct vfsoptlist *opts = data;
	void *rsv;
	int jsys, descend;

	/*
	 * sysvsem controls which jail is the root of the associated sems (this
	 * jail or same as the parent), or if the feature is available at all.
	 */
	if (vfs_copyopt(opts, "sysvsem", &jsys, sizeof(jsys)) == ENOENT)
		jsys = vfs_flagopt(opts, "allow.sysvipc", NULL, 0)
		    ? JAIL_SYS_INHERIT
		    : vfs_flagopt(opts, "allow.nosysvipc", NULL, 0)
		    ? JAIL_SYS_DISABLE
		    : -1;
	if (jsys == JAIL_SYS_DISABLE) {
		prison_lock(pr);
		orpr = osd_jail_get(pr, sem_prison_slot);
		if (orpr != NULL)
			osd_jail_del(pr, sem_prison_slot);
		prison_unlock(pr);
		if (orpr != NULL) {
			if (orpr == pr)
				sem_prison_cleanup(pr);
			/* Disable all child jails as well. */
			FOREACH_PRISON_DESCENDANT(pr, tpr, descend) {
				prison_lock(tpr);
				trpr = osd_jail_get(tpr, sem_prison_slot);
				if (trpr != NULL) {
					osd_jail_del(tpr, sem_prison_slot);
					prison_unlock(tpr);
					if (trpr == tpr)
						sem_prison_cleanup(tpr);
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
			nrpr = osd_jail_get(pr->pr_parent, sem_prison_slot);
			prison_unlock(pr->pr_parent);
		}
		rsv = osd_reserve(sem_prison_slot);
		prison_lock(pr);
		orpr = osd_jail_get(pr, sem_prison_slot);
		if (orpr != nrpr)
			(void)osd_jail_set_reserved(pr, sem_prison_slot, rsv,
			    nrpr);
		else
			osd_free_reserved(rsv);
		prison_unlock(pr);
		if (orpr != nrpr) {
			if (orpr == pr)
				sem_prison_cleanup(pr);
			if (orpr != NULL) {
				/* Change child jails matching the old root, */
				FOREACH_PRISON_DESCENDANT(pr, tpr, descend) {
					prison_lock(tpr);
					trpr = osd_jail_get(tpr,
					    sem_prison_slot);
					if (trpr == orpr) {
						(void)osd_jail_set(tpr,
						    sem_prison_slot, nrpr);
						prison_unlock(tpr);
						if (trpr == tpr)
							sem_prison_cleanup(tpr);
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
sem_prison_get(void *obj, void *data)
{
	struct prison *pr = obj;
	struct prison *rpr;
	struct vfsoptlist *opts = data;
	int error, jsys;

	/* Set sysvsem based on the jail's root prison. */
	prison_lock(pr);
	rpr = osd_jail_get(pr, sem_prison_slot);
	prison_unlock(pr);
	jsys = rpr == NULL ? JAIL_SYS_DISABLE
	    : rpr == pr ? JAIL_SYS_NEW : JAIL_SYS_INHERIT;
	error = vfs_setopt(opts, "sysvsem", &jsys, sizeof(jsys));
	if (error == ENOENT)
		error = 0;
	return (error);
}

static int
sem_prison_remove(void *obj, void *data __unused)
{
	struct prison *pr = obj;
	struct prison *rpr;

	prison_lock(pr);
	rpr = osd_jail_get(pr, sem_prison_slot);
	prison_unlock(pr);
	if (rpr == pr)
		sem_prison_cleanup(pr);
	return (0);
}

static void
sem_prison_cleanup(struct prison *pr)
{
	int i;

	/* Remove any sems that belong to this jail. */
	mtx_lock(&sem_mtx);
	for (i = 0; i < seminfo.semmni; i++) {
		if ((sema[i].u.sem_perm.mode & SEM_ALLOC) &&
		    sema[i].cred != NULL && sema[i].cred->cr_prison == pr) {
			mtx_lock(&sema_mtx[i]);
			sem_remove(i, NULL);
			mtx_unlock(&sema_mtx[i]);
		}
	}
	mtx_unlock(&sem_mtx);
}

SYSCTL_JAIL_PARAM_SYS_NODE(sysvsem, CTLFLAG_RW, "SYSV semaphores");

#if defined(COMPAT_FREEBSD4) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD7)

/* XXX casting to (sy_call_t *) is bogus, as usual. */
static sy_call_t *semcalls[] = {
	(sy_call_t *)freebsd7___semctl, (sy_call_t *)sys_semget,
	(sy_call_t *)sys_semop
};

/*
 * Entry point for all SEM calls.
 */
int
sys_semsys(td, uap)
	struct thread *td;
	/* XXX actually varargs. */
	struct semsys_args /* {
		int	which;
		int	a2;
		int	a3;
		int	a4;
		int	a5;
	} */ *uap;
{
	int error;

	AUDIT_ARG_SVIPC_WHICH(uap->which);
	if (uap->which < 0 || uap->which >= nitems(semcalls))
		return (EINVAL);
	error = (*semcalls[uap->which])(td, &uap->a2);
	return (error);
}

#ifndef CP
#define CP(src, dst, fld)	do { (dst).fld = (src).fld; } while (0)
#endif

#ifndef _SYS_SYSPROTO_H_
struct freebsd7___semctl_args {
	int	semid;
	int	semnum;
	int	cmd;
	union	semun_old *arg;
};
#endif
int
freebsd7___semctl(struct thread *td, struct freebsd7___semctl_args *uap)
{
	struct semid_ds_old dsold;
	struct semid_ds dsbuf;
	union semun_old arg;
	union semun semun;
	register_t rval;
	int error;

	switch (uap->cmd) {
	case SEM_STAT:
	case IPC_SET:
	case IPC_STAT:
	case GETALL:
	case SETVAL:
	case SETALL:
		error = copyin(uap->arg, &arg, sizeof(arg));
		if (error)
			return (error);
		break;
	}

	switch (uap->cmd) {
	case SEM_STAT:
	case IPC_STAT:
		semun.buf = &dsbuf;
		break;
	case IPC_SET:
		error = copyin(arg.buf, &dsold, sizeof(dsold));
		if (error)
			return (error);
		ipcperm_old2new(&dsold.sem_perm, &dsbuf.sem_perm);
		CP(dsold, dsbuf, __sem_base);
		CP(dsold, dsbuf, sem_nsems);
		CP(dsold, dsbuf, sem_otime);
		CP(dsold, dsbuf, sem_ctime);
		semun.buf = &dsbuf;
		break;
	case GETALL:
	case SETALL:
		semun.array = arg.array;
		break;
	case SETVAL:
		semun.val = arg.val;
		break;		
	}

	error = kern_semctl(td, uap->semid, uap->semnum, uap->cmd, &semun,
	    &rval);
	if (error)
		return (error);

	switch (uap->cmd) {
	case SEM_STAT:
	case IPC_STAT:
		bzero(&dsold, sizeof(dsold));
		ipcperm_new2old(&dsbuf.sem_perm, &dsold.sem_perm);
		CP(dsbuf, dsold, __sem_base);
		CP(dsbuf, dsold, sem_nsems);
		CP(dsbuf, dsold, sem_otime);
		CP(dsbuf, dsold, sem_ctime);
		error = copyout(&dsold, arg.buf, sizeof(dsold));
		break;
	}

	if (error == 0)
		td->td_retval[0] = rval;
	return (error);
}

#endif /* COMPAT_FREEBSD{4,5,6,7} */

#ifdef COMPAT_FREEBSD32

int
freebsd32_semsys(struct thread *td, struct freebsd32_semsys_args *uap)
{

#if defined(COMPAT_FREEBSD4) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD7)
	AUDIT_ARG_SVIPC_WHICH(uap->which);
	switch (uap->which) {
	case 0:
		return (freebsd7_freebsd32_semctl(td,
		    (struct freebsd7_freebsd32_semctl_args *)&uap->a2));
	default:
		return (sys_semsys(td, (struct semsys_args *)uap));
	}
#else
	return (nosys(td, NULL));
#endif
}

#if defined(COMPAT_FREEBSD4) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD7)
int
freebsd7_freebsd32_semctl(struct thread *td,
    struct freebsd7_freebsd32_semctl_args *uap)
{
	struct semid_ds32_old dsbuf32;
	struct semid_ds dsbuf;
	union semun semun;
	union semun32 arg;
	register_t rval;
	int error;

	switch (uap->cmd) {
	case SEM_STAT:
	case IPC_SET:
	case IPC_STAT:
	case GETALL:
	case SETVAL:
	case SETALL:
		error = copyin(uap->arg, &arg, sizeof(arg));
		if (error)
			return (error);		
		break;
	}

	switch (uap->cmd) {
	case SEM_STAT:
	case IPC_STAT:
		semun.buf = &dsbuf;
		break;
	case IPC_SET:
		error = copyin(PTRIN(arg.buf), &dsbuf32, sizeof(dsbuf32));
		if (error)
			return (error);
		freebsd32_ipcperm_old_in(&dsbuf32.sem_perm, &dsbuf.sem_perm);
		PTRIN_CP(dsbuf32, dsbuf, __sem_base);
		CP(dsbuf32, dsbuf, sem_nsems);
		CP(dsbuf32, dsbuf, sem_otime);
		CP(dsbuf32, dsbuf, sem_ctime);
		semun.buf = &dsbuf;
		break;
	case GETALL:
	case SETALL:
		semun.array = PTRIN(arg.array);
		break;
	case SETVAL:
		semun.val = arg.val;
		break;
	}

	error = kern_semctl(td, uap->semid, uap->semnum, uap->cmd, &semun,
	    &rval);
	if (error)
		return (error);

	switch (uap->cmd) {
	case SEM_STAT:
	case IPC_STAT:
		bzero(&dsbuf32, sizeof(dsbuf32));
		freebsd32_ipcperm_old_out(&dsbuf.sem_perm, &dsbuf32.sem_perm);
		PTROUT_CP(dsbuf, dsbuf32, __sem_base);
		CP(dsbuf, dsbuf32, sem_nsems);
		CP(dsbuf, dsbuf32, sem_otime);
		CP(dsbuf, dsbuf32, sem_ctime);
		error = copyout(&dsbuf32, PTRIN(arg.buf), sizeof(dsbuf32));
		break;
	}

	if (error == 0)
		td->td_retval[0] = rval;
	return (error);
}
#endif

int
freebsd32_semctl(struct thread *td, struct freebsd32_semctl_args *uap)
{
	struct semid_ds32 dsbuf32;
	struct semid_ds dsbuf;
	union semun semun;
	union semun32 arg;
	register_t rval;
	int error;

	switch (uap->cmd) {
	case SEM_STAT:
	case IPC_SET:
	case IPC_STAT:
	case GETALL:
	case SETVAL:
	case SETALL:
		error = copyin(uap->arg, &arg, sizeof(arg));
		if (error)
			return (error);		
		break;
	}

	switch (uap->cmd) {
	case SEM_STAT:
	case IPC_STAT:
		semun.buf = &dsbuf;
		break;
	case IPC_SET:
		error = copyin(PTRIN(arg.buf), &dsbuf32, sizeof(dsbuf32));
		if (error)
			return (error);
		freebsd32_ipcperm_in(&dsbuf32.sem_perm, &dsbuf.sem_perm);
		PTRIN_CP(dsbuf32, dsbuf, __sem_base);
		CP(dsbuf32, dsbuf, sem_nsems);
		CP(dsbuf32, dsbuf, sem_otime);
		CP(dsbuf32, dsbuf, sem_ctime);
		semun.buf = &dsbuf;
		break;
	case GETALL:
	case SETALL:
		semun.array = PTRIN(arg.array);
		break;
	case SETVAL:
		semun.val = arg.val;
		break;		
	}

	error = kern_semctl(td, uap->semid, uap->semnum, uap->cmd, &semun,
	    &rval);
	if (error)
		return (error);

	switch (uap->cmd) {
	case SEM_STAT:
	case IPC_STAT:
		bzero(&dsbuf32, sizeof(dsbuf32));
		freebsd32_ipcperm_out(&dsbuf.sem_perm, &dsbuf32.sem_perm);
		PTROUT_CP(dsbuf, dsbuf32, __sem_base);
		CP(dsbuf, dsbuf32, sem_nsems);
		CP(dsbuf, dsbuf32, sem_otime);
		CP(dsbuf, dsbuf32, sem_ctime);
		error = copyout(&dsbuf32, PTRIN(arg.buf), sizeof(dsbuf32));
		break;
	}

	if (error == 0)
		td->td_retval[0] = rval;
	return (error);
}

#endif /* COMPAT_FREEBSD32 */
