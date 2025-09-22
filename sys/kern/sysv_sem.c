/*	$OpenBSD: sysv_sem.c,v 1.65 2025/08/01 08:40:25 mvs Exp $	*/
/*	$NetBSD: sysv_sem.c,v 1.26 1996/02/09 19:00:25 christos Exp $	*/

/*
 * Copyright (c) 2002,2003 Todd C. Miller <millert@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F39502-99-1-0512.
 */
/*
 * Implementation of SVID semaphores
 *
 * Author:  Daniel Boulet
 *
 * This software is provided ``AS IS'' without any warranties of any kind.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/sem.h>
#include <sys/sysctl.h>
#include <sys/malloc.h>
#include <sys/pool.h>

#include <sys/mount.h>
#include <sys/syscallargs.h>

#ifdef SEM_DEBUG
#define DPRINTF(x)	printf x
#else
#define DPRINTF(x)
#endif

#define SEMOP_MAX (MALLOC_MAX / sizeof(struct sembuf))

int	semtot = 0;
int	semutot = 0;
struct	semid_ds **sema;	/* semaphore id list */
SLIST_HEAD(, sem_undo) semu_list; /* list of undo structures */
struct	pool sema_pool;		/* pool for struct semid_ds */
struct	pool semu_pool;		/* pool for struct sem_undo (SEMUSZ) */
unsigned short *semseqs;	/* array of sem sequence numbers */

struct sem_undo *semu_alloc(struct process *);
int semundo_adjust(struct proc *, struct sem_undo **, int, int, int);
void semundo_clear(int, int);

void
seminit(void)
{

	pool_init(&sema_pool, sizeof(struct semid_ds), 0, 0, PR_WAITOK,
	    "semapl", NULL);
	pool_init(&semu_pool, SEMUSZ, 0, 0, PR_WAITOK, "semupl", NULL);
	sema = mallocarray(seminfo.semmni, sizeof(struct semid_ds *),
	    M_SEM, M_WAITOK|M_ZERO);
	semseqs = mallocarray(seminfo.semmni, sizeof(unsigned short),
	    M_SEM, M_WAITOK|M_ZERO);
	SLIST_INIT(&semu_list);
}

/*
 * Allocate a new sem_undo structure for a process
 * (returns ptr to structure or NULL if no more room)
 */
struct sem_undo *
semu_alloc(struct process *pr)
{
	struct sem_undo *suptr, *sutmp;

	if (semutot == seminfo.semmnu)
		return (NULL);		/* no space */

	/*
	 * Allocate a semu w/o waiting if possible.
	 * If we do have to wait, we must check to verify that a semu
	 * with un_proc == pr has not been allocated in the meantime.
	 */
	semutot++;
	if ((suptr = pool_get(&semu_pool, PR_NOWAIT)) == NULL) {
		sutmp = pool_get(&semu_pool, PR_WAITOK);
		SLIST_FOREACH(suptr, &semu_list, un_next) {
			if (suptr->un_proc == pr) {
				pool_put(&semu_pool, sutmp);
				semutot--;
				return (suptr);
			}
		}
		suptr = sutmp;
	}
	suptr->un_cnt = 0;
	suptr->un_proc = pr;
	SLIST_INSERT_HEAD(&semu_list, suptr, un_next);
	return (suptr);
}

/*
 * Adjust a particular entry for a particular proc
 */
int
semundo_adjust(struct proc *p, struct sem_undo **supptr, int semid, int semnum,
	int adjval)
{
	struct process *pr = p->p_p;
	struct sem_undo *suptr;
	struct undo *sunptr;
	int i;

	/*
	 * Look for and remember the sem_undo if the caller doesn't provide it.
	 */
	suptr = *supptr;
	if (suptr == NULL) {
		SLIST_FOREACH(suptr, &semu_list, un_next) {
			if (suptr->un_proc == pr) {
				*supptr = suptr;
				break;
			}
		}
		if (suptr == NULL) {
			if (adjval == 0)
				return (0);
			suptr = semu_alloc(p->p_p);
			if (suptr == NULL)
				return (ENOSPC);
			*supptr = suptr;
		}
	}

	/*
	 * Look for the requested entry and adjust it
	 * (delete if adjval becomes 0).
	 */
	sunptr = &suptr->un_ent[0];
	for (i = 0; i < suptr->un_cnt; i++, sunptr++) {
		if (sunptr->un_id != semid || sunptr->un_num != semnum)
			continue;
		if (adjval == 0)
			sunptr->un_adjval = 0;
		else
			sunptr->un_adjval += adjval;
		if (sunptr->un_adjval != 0)
			return (0);

		if (--suptr->un_cnt == 0) {
			*supptr = NULL;
			SLIST_REMOVE(&semu_list, suptr, sem_undo, un_next);
			pool_put(&semu_pool, suptr);
			semutot--;
		} else if (i < suptr->un_cnt)
			suptr->un_ent[i] =
			    suptr->un_ent[suptr->un_cnt];
		return (0);
	}

	/* Didn't find the right entry - create it */
	if (adjval == 0)
		return (0);
	if (suptr->un_cnt == SEMUME)
		return (EINVAL);

	sunptr = &suptr->un_ent[suptr->un_cnt];
	suptr->un_cnt++;
	sunptr->un_adjval = adjval;
	sunptr->un_id = semid;
	sunptr->un_num = semnum;
	return (0);
}

void
semundo_clear(int semid, int semnum)
{
	struct sem_undo *suptr = SLIST_FIRST(&semu_list);
	struct sem_undo *suprev = NULL;
	struct undo *sunptr;
	int i;

	while (suptr != NULL) {
		sunptr = &suptr->un_ent[0];
		for (i = 0; i < suptr->un_cnt; i++, sunptr++) {
			if (sunptr->un_id == semid) {
				if (semnum == -1 || sunptr->un_num == semnum) {
					suptr->un_cnt--;
					if (i < suptr->un_cnt) {
						suptr->un_ent[i] =
						  suptr->un_ent[suptr->un_cnt];
						i--, sunptr--;
					}
				}
				if (semnum != -1)
					break;
			}
		}
		if (suptr->un_cnt == 0) {
			struct sem_undo *sutmp = suptr;

			if (suptr == SLIST_FIRST(&semu_list))
				SLIST_REMOVE_HEAD(&semu_list, un_next);
			else
				SLIST_REMOVE_AFTER(suprev, un_next);
			suptr = SLIST_NEXT(suptr, un_next);
			pool_put(&semu_pool, sutmp);
			semutot--;
		} else {
			suprev = suptr;
			suptr = SLIST_NEXT(suptr, un_next);
		}
	}
}

int
sys___semctl(struct proc *p, void *v, register_t *retval)
{
	struct sys___semctl_args /* {
		syscallarg(int) semid;
		syscallarg(int) semnum;
		syscallarg(int) cmd;
		syscallarg(union semun *) arg;
	} */ *uap = v;
	struct ucred *cred = p->p_ucred;
	int semid = SCARG(uap, semid);
	int semnum = SCARG(uap, semnum);
	int cmd = SCARG(uap, cmd);
	union semun arg, *uarg = SCARG(uap, arg);
	struct semid_ds sbuf;
	struct semid_ds *semaptr;
	unsigned short *semval = NULL, nsems;
	int i, ix, error;

	switch (cmd) {
	case IPC_SET:
	case IPC_STAT:
	case GETALL:
	case SETVAL:
	case SETALL:
		if ((error = copyin(uarg, &arg, sizeof(union semun))))
			return (error);
	}
	if (cmd == IPC_SET)
		if ((error = copyin(arg.buf, &sbuf, sizeof(sbuf))))
			return (error);

	DPRINTF(("call to semctl(%d, %d, %d, %p)\n", semid, semnum, cmd, uarg));

	ix = IPCID_TO_IX(semid);
	if (ix < 0 || ix >= seminfo.semmni)
		return (EINVAL);

again:
	if ((semaptr = sema[ix]) == NULL ||
	    semaptr->sem_perm.seq != IPCID_TO_SEQ(semid))
		return (EINVAL);

	switch (cmd) {
	case IPC_RMID:
		if ((error = ipcperm(cred, &semaptr->sem_perm, IPC_M)) != 0)
			return (error);
		semaptr->sem_perm.cuid = cred->cr_uid;
		semaptr->sem_perm.uid = cred->cr_uid;
		semtot -= semaptr->sem_nsems;
		free(semaptr->sem_base, M_SEM,
		    semaptr->sem_nsems * sizeof(struct sem));
		pool_put(&sema_pool, semaptr);
		sema[ix] = NULL;
		semundo_clear(ix, -1);
		wakeup(&sema[ix]);
		break;

	case IPC_SET:
		if ((error = ipcperm(cred, &semaptr->sem_perm, IPC_M)))
			return (error);
		semaptr->sem_perm.uid = sbuf.sem_perm.uid;
		semaptr->sem_perm.gid = sbuf.sem_perm.gid;
		semaptr->sem_perm.mode = (semaptr->sem_perm.mode & ~0777) |
		    (sbuf.sem_perm.mode & 0777);
		semaptr->sem_ctime = gettime();
		break;

	case IPC_STAT:
		if ((error = ipcperm(cred, &semaptr->sem_perm, IPC_R)))
			return (error);
		memcpy(&sbuf, semaptr, sizeof sbuf);
		sbuf.sem_base = NULL;
		error = copyout(&sbuf, arg.buf, sizeof(struct semid_ds));
		break;

	case GETNCNT:
		if ((error = ipcperm(cred, &semaptr->sem_perm, IPC_R)))
			return (error);
		if (semnum < 0 || semnum >= semaptr->sem_nsems)
			return (EINVAL);
		*retval = semaptr->sem_base[semnum].semncnt;
		break;

	case GETPID:
		if ((error = ipcperm(cred, &semaptr->sem_perm, IPC_R)))
			return (error);
		if (semnum < 0 || semnum >= semaptr->sem_nsems)
			return (EINVAL);
		*retval = semaptr->sem_base[semnum].sempid;
		break;

	case GETVAL:
		if ((error = ipcperm(cred, &semaptr->sem_perm, IPC_R)))
			return (error);
		if (semnum < 0 || semnum >= semaptr->sem_nsems)
			return (EINVAL);
		*retval = semaptr->sem_base[semnum].semval;
		break;

	case GETALL:
		nsems = semaptr->sem_nsems;
		semval = mallocarray(nsems, sizeof(arg.array[0]),
		    M_TEMP, M_WAITOK);
		if (semaptr != sema[ix] || 
		    semaptr->sem_perm.seq != IPCID_TO_SEQ(semid) ||
		    semaptr->sem_nsems != nsems) {
			free(semval, M_TEMP, nsems * sizeof(arg.array[0]));
			goto again;
		}
		if ((error = ipcperm(cred, &semaptr->sem_perm, IPC_R)))
			goto error;
		for (i = 0; i < nsems; i++)
			semval[i] = semaptr->sem_base[i].semval;
		for (i = 0; i < nsems; i++) {
			error = copyout(&semval[i], &arg.array[i],
			    sizeof(arg.array[0]));
			if (error != 0)
				break;
		}
		break;

	case GETZCNT:
		if ((error = ipcperm(cred, &semaptr->sem_perm, IPC_R)))
			return (error);
		if (semnum < 0 || semnum >= semaptr->sem_nsems)
			return (EINVAL);
		*retval = semaptr->sem_base[semnum].semzcnt;
		break;

	case SETVAL:
		if ((error = ipcperm(cred, &semaptr->sem_perm, IPC_W)))
			return (error);
		if (semnum < 0 || semnum >= semaptr->sem_nsems)
			return (EINVAL);
		if (arg.val > seminfo.semvmx)
			return (ERANGE);
		semaptr->sem_base[semnum].semval = arg.val;
		semundo_clear(ix, semnum);
		wakeup(&sema[ix]);
		break;

	case SETALL:
		nsems = semaptr->sem_nsems;
		semval = mallocarray(nsems, sizeof(arg.array[0]),
		    M_TEMP, M_WAITOK);
		for (i = 0; i < nsems; i++) {
			error = copyin(&arg.array[i], &semval[i],
			    sizeof(arg.array[0]));
			if (error != 0)
				goto error;
			if (semval[i] > seminfo.semvmx) {
				error = ERANGE;
				goto error;
			}
		}
		if (semaptr != sema[ix] ||
		    semaptr->sem_perm.seq != IPCID_TO_SEQ(semid) ||
		    semaptr->sem_nsems != nsems) {
			free(semval, M_TEMP, nsems * sizeof(arg.array[0]));
			goto again;
		}
		if ((error = ipcperm(cred, &semaptr->sem_perm, IPC_W)))
			goto error;
		for (i = 0; i < nsems; i++)
			semaptr->sem_base[i].semval = semval[i];
		semundo_clear(ix, -1);
		wakeup(&sema[ix]);
		break;

	default:
		return (EINVAL);
	}

error:
	free(semval, M_TEMP, nsems * sizeof(arg.array[0]));

	return (error);
}

int
sys_semget(struct proc *p, void *v, register_t *retval)
{
	struct sys_semget_args /* {
		syscallarg(key_t) key;
		syscallarg(int) nsems;
		syscallarg(int) semflg;
	} */ *uap = v;
	int semid, error;
	int key = SCARG(uap, key);
	int nsems = SCARG(uap, nsems);
	int semflg = SCARG(uap, semflg);
	struct semid_ds *semaptr, *semaptr_new = NULL;
	struct ucred *cred = p->p_ucred;

	DPRINTF(("semget(0x%x, %d, 0%o)\n", key, nsems, semflg));

	/*
	 * Preallocate space for the new semaphore.  If we are going
	 * to sleep, we want to sleep now to eliminate any race
	 * condition in allocating a semaphore with a specific key.
	 */
	if (key == IPC_PRIVATE || (semflg & IPC_CREAT)) {
		if (nsems <= 0 || nsems > seminfo.semmsl) {
			DPRINTF(("nsems out of range (0<%d<=%d)\n", nsems,
			    seminfo.semmsl));
			return (EINVAL);
		}
		if (nsems > seminfo.semmns - semtot) {
			DPRINTF(("not enough semaphores left (need %d, got %d)\n",
			    nsems, seminfo.semmns - semtot));
			return (ENOSPC);
		}
		semaptr_new = pool_get(&sema_pool, PR_WAITOK | PR_ZERO);
		semaptr_new->sem_base = mallocarray(nsems, sizeof(struct sem),
		    M_SEM, M_WAITOK|M_ZERO);
		if (nsems > seminfo.semmns - semtot) {
			error = ENOSPC;
			goto error;
		}
	}

	if (key != IPC_PRIVATE) {
		for (semid = 0, semaptr = NULL; semid < seminfo.semmni; semid++) {
			if ((semaptr = sema[semid]) != NULL &&
			    semaptr->sem_perm.key == key) {
				DPRINTF(("found public key\n"));
				if ((error = ipcperm(cred, &semaptr->sem_perm,
				    semflg & 0700)))
					goto error;
				if (nsems > 0 && semaptr->sem_nsems < nsems) {
					DPRINTF(("too small\n"));
					error = EINVAL;
					goto error;
				}
				if ((semflg & IPC_CREAT) && (semflg & IPC_EXCL)) {
					DPRINTF(("not exclusive\n"));
					error = EEXIST;
					goto error;
				}
				if (semaptr_new != NULL) {
					free(semaptr_new->sem_base, M_SEM,
					    nsems * sizeof(struct sem));
					pool_put(&sema_pool, semaptr_new);
				}
				goto found;
			}
		}
	}

	DPRINTF(("need to allocate the semid_ds\n"));
	if (key == IPC_PRIVATE || (semflg & IPC_CREAT)) {
		for (semid = 0; semid < seminfo.semmni; semid++) {
			if ((semaptr = sema[semid]) == NULL)
				break;
		}
		if (semid == seminfo.semmni) {
			DPRINTF(("no more semid_ds's available\n"));
			error = ENOSPC;
			goto error;
		}
		DPRINTF(("semid %d is available\n", semid));
		semaptr_new->sem_perm.key = key;
		semaptr_new->sem_perm.cuid = cred->cr_uid;
		semaptr_new->sem_perm.uid = cred->cr_uid;
		semaptr_new->sem_perm.cgid = cred->cr_gid;
		semaptr_new->sem_perm.gid = cred->cr_gid;
		semaptr_new->sem_perm.mode = (semflg & 0777);
		semaptr_new->sem_perm.seq = semseqs[semid] =
		    (semseqs[semid] + 1) & 0x7fff;
		semaptr_new->sem_nsems = nsems;
		semaptr_new->sem_otime = 0;
		semaptr_new->sem_ctime = gettime();
		sema[semid] = semaptr_new;
		semtot += nsems;
	} else {
		DPRINTF(("didn't find it and wasn't asked to create it\n"));
		return (ENOENT);
	}

found:
	*retval = IXSEQ_TO_IPCID(semid, sema[semid]->sem_perm);
	return (0);
error:
	if (semaptr_new != NULL) {
		free(semaptr_new->sem_base, M_SEM, nsems * sizeof(struct sem));
		pool_put(&sema_pool, semaptr_new);
	}
	return (error);
}

int
sys_semop(struct proc *p, void *v, register_t *retval)
{
	struct sys_semop_args /* {
		syscallarg(int) semid;
		syscallarg(struct sembuf *) sops;
		syscallarg(size_t) nsops;
	} */ *uap = v;
#define	NSOPS	8
	struct sembuf sopbuf[NSOPS];
	int semid = SCARG(uap, semid);
	size_t nsops = SCARG(uap, nsops);
	struct sembuf *sops;
	struct semid_ds *semaptr;
	struct sembuf *sopptr = NULL;
	struct sem *semptr = NULL;
	struct sem_undo *suptr = NULL;
	struct ucred *cred = p->p_ucred;
	size_t i, j;
	int do_wakeup, do_undos, error;

	DPRINTF(("call to semop(%d, %p, %lu)\n", semid, SCARG(uap, sops),
	    (u_long)nsops));

	semid = IPCID_TO_IX(semid);	/* Convert back to zero origin */

	if (semid < 0 || semid >= seminfo.semmni)
		return (EINVAL);

	if ((semaptr = sema[semid]) == NULL ||
	    semaptr->sem_perm.seq != IPCID_TO_SEQ(SCARG(uap, semid)))
		return (EINVAL);

	if ((error = ipcperm(cred, &semaptr->sem_perm, IPC_W))) {
		DPRINTF(("error = %d from ipaccess\n", error));
		return (error);
	}

	if (nsops == 0) {
		*retval = 0;
		return (0);
	} else if (nsops > (size_t)seminfo.semopm) {
		DPRINTF(("too many sops (max=%d, nsops=%lu)\n", seminfo.semopm,
		    (u_long)nsops));
		return (E2BIG);
	}

	if (nsops <= NSOPS)
		sops = sopbuf;
	else
		sops = mallocarray(nsops, sizeof(struct sembuf), M_SEM, M_WAITOK);
	error = copyin(SCARG(uap, sops), sops, nsops * sizeof(struct sembuf));
	if (error != 0) {
		DPRINTF(("error = %d from copyin(%p, %p, %zu)\n", error,
		    SCARG(uap, sops), &sops, nsops * sizeof(struct sembuf)));
		goto done2;
	}

	/* 
	 * Loop trying to satisfy the vector of requests.
	 * If we reach a point where we must wait, any requests already
	 * performed are rolled back and we go to sleep until some other
	 * process wakes us up.  At this point, we start all over again.
	 *
	 * This ensures that from the perspective of other tasks, a set
	 * of requests is atomic (never partially satisfied).
	 */
	do_undos = 0;

	for (;;) {
		do_wakeup = 0;

		for (i = 0; i < nsops; i++) {
			sopptr = &sops[i];

			if (sopptr->sem_num >= semaptr->sem_nsems) {
				error = EFBIG;
				goto done2;
			}

			semptr = &semaptr->sem_base[sopptr->sem_num];

			DPRINTF(("semop:  semaptr=%p, sem_base=%p, semptr=%p, sem[%d]=%d : op=%d, flag=%s\n",
			    semaptr, semaptr->sem_base, semptr,
			    sopptr->sem_num, semptr->semval, sopptr->sem_op,
			    (sopptr->sem_flg & IPC_NOWAIT) ? "nowait" : "wait"));

			if (sopptr->sem_op < 0) {
				if ((int)(semptr->semval +
					  sopptr->sem_op) < 0) {
					DPRINTF(("semop:  can't do it now\n"));
					break;
				} else {
					semptr->semval += sopptr->sem_op;
					if (semptr->semval == 0 &&
					    semptr->semzcnt > 0)
						do_wakeup = 1;
				}
				if (sopptr->sem_flg & SEM_UNDO)
					do_undos++;
			} else if (sopptr->sem_op == 0) {
				if (semptr->semval > 0) {
					DPRINTF(("semop:  not zero now\n"));
					break;
				}
			} else {
				if (semptr->semncnt > 0)
					do_wakeup = 1;
				semptr->semval += sopptr->sem_op;
				if (sopptr->sem_flg & SEM_UNDO)
					do_undos++;
			}
		}

		/*
		 * Did we get through the entire vector and can we undo it?
		 */
		if (i >= nsops && do_undos <= SEMUME)
			goto done;

		/*
		 * No ... rollback anything that we've already done
		 */
		DPRINTF(("semop:  rollback 0 through %zu\n", i - 1));
		for (j = 0; j < i; j++)
			semaptr->sem_base[sops[j].sem_num].semval -=
			    sops[j].sem_op;

		/*
		 * Did we have too many SEM_UNDO's
		 */
		if (do_undos > SEMUME) {
			error = ENOSPC;
			goto done2;
		}

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
		error = tsleep_nsec(&sema[semid], PLOCK | PCATCH,
		    "semwait", INFSLP);
		DPRINTF(("semop:  good morning (error=%d)!\n", error));

		suptr = NULL;	/* sem_undo may have been reallocated */

		/*
		 * Make sure that the semaphore still exists
		 */
		if (sema[semid] == NULL ||
		    semaptr->sem_perm.seq != IPCID_TO_SEQ(SCARG(uap, semid))) {
			error = EIDRM;
			goto done2;
		}

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
		 * (Delayed check of tsleep() return code because we
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
			error = semundo_adjust(p, &suptr, semid,
			    sops[i].sem_num, -adjval);
			if (error == 0)
				continue;

			/*
			 * Uh-Oh!  We ran out of either sem_undo's or undo's.
			 * Rollback the adjustments to this point and then
			 * rollback the semaphore ups and down so we can return
			 * with an error with all structures restored.  We
			 * rollback the undo's in the exact reverse order that
			 * we applied them.  This guarantees that we won't run
			 * out of space as we roll things back out.
			 */
			for (j = i; j > 0;) {
				j--;
				if ((sops[j].sem_flg & SEM_UNDO) == 0)
					continue;
				adjval = sops[j].sem_op;
				if (adjval == 0)
					continue;
				if (semundo_adjust(p, &suptr, semid,
				    sops[j].sem_num, adjval) != 0)
					panic("semop - can't undo undos");
			}

			for (j = 0; j < nsops; j++)
				semaptr->sem_base[sops[j].sem_num].semval -=
				    sops[j].sem_op;

			DPRINTF(("error = %d from semundo_adjust\n", error));
			goto done2;
		} /* loop through the sops */
	} /* if (do_undos) */

	/* We're definitely done - set the sempid's */
	for (i = 0; i < nsops; i++) {
		sopptr = &sops[i];
		semptr = &semaptr->sem_base[sopptr->sem_num];
		semptr->sempid = p->p_p->ps_pid;
	}

	semaptr->sem_otime = gettime();

	/* Do a wakeup if any semaphore was up'd. */
	if (do_wakeup) {
		DPRINTF(("semop:  doing wakeup\n"));
		wakeup(&sema[semid]);
		DPRINTF(("semop:  back from wakeup\n"));
	}
	DPRINTF(("semop:  done\n"));
	*retval = 0;
done2:
	if (sops != sopbuf)
		free(sops, M_SEM, nsops * sizeof(struct sembuf));
	return (error);
}

/*
 * Go through the undo structures for this process and apply the adjustments to
 * semaphores.
 */
void
semexit(struct process *pr)
{
	struct sem_undo *suptr;
	struct sem_undo **supptr;

	/*
	 * Go through the chain of undo vectors looking for one associated with
	 * this process.  Remember the pointer to the pointer to the element
	 * to dequeue it later.
	 */
	supptr = &SLIST_FIRST(&semu_list);
	SLIST_FOREACH(suptr, &semu_list, un_next) {
		if (suptr->un_proc == pr)
			break;
		supptr = &SLIST_NEXT(suptr, un_next);
	}

	/*
	 * If there is no undo vector, skip to the end.
	 */
	if (suptr == NULL)
		return;

	/*
	 * We now have an undo vector for this process.
	 */
	DPRINTF(("process @%p has undo structure with %d entries\n", pr,
	    suptr->un_cnt));

	/*
	 * If there are any active undo elements then process them.
	 */
	if (suptr->un_cnt > 0) {
		int ix;

		for (ix = 0; ix < suptr->un_cnt; ix++) {
			int semid = suptr->un_ent[ix].un_id;
			int semnum = suptr->un_ent[ix].un_num;
			int adjval = suptr->un_ent[ix].un_adjval;
			struct semid_ds *semaptr;

			if ((semaptr = sema[semid]) == NULL)
				panic("semexit - semid not allocated");
			if (semnum >= semaptr->sem_nsems)
				panic("semexit - semnum out of range");

			DPRINTF(("semexit:  %p id=%d num=%d(adj=%d) ; sem=%d\n",
			    suptr->un_proc, suptr->un_ent[ix].un_id,
			    suptr->un_ent[ix].un_num,
			    suptr->un_ent[ix].un_adjval,
			    semaptr->sem_base[semnum].semval));

			if (adjval < 0 &&
			    semaptr->sem_base[semnum].semval < -adjval)
				semaptr->sem_base[semnum].semval = 0;
			else
				semaptr->sem_base[semnum].semval += adjval;

			wakeup(&sema[semid]);
			DPRINTF(("semexit:  back from wakeup\n"));
		}
	}

	/*
	 * Deallocate the undo vector.
	 */
	DPRINTF(("removing vector\n"));
	*supptr = SLIST_NEXT(suptr, un_next);
	pool_put(&semu_pool, suptr);
	semutot--;
}

/* Expand semsegs and semseqs arrays */
void
sema_reallocate(int val)
{
	struct semid_ds **sema_new;
	unsigned short *newseqs;
	sema_new = mallocarray(val, sizeof(struct semid_ds *),
	    M_SEM, M_WAITOK|M_ZERO);
	memcpy(sema_new, sema,
	    seminfo.semmni * sizeof(struct semid_ds *));
	newseqs = mallocarray(val, sizeof(unsigned short), M_SEM,
	    M_WAITOK|M_ZERO);
	memcpy(newseqs, semseqs,
	    seminfo.semmni * sizeof(unsigned short));
	free(sema, M_SEM, seminfo.semmni * sizeof(struct semid_ds *));
	free(semseqs, M_SEM, seminfo.semmni * sizeof(unsigned short));
	sema = sema_new;
	semseqs = newseqs;
	seminfo.semmni = val;
}

const struct sysctl_bounded_args sysvsem_vars[] = {
	{ KERN_SEMINFO_SEMUME, &seminfo.semume, SYSCTL_INT_READONLY },
	{ KERN_SEMINFO_SEMUSZ, &seminfo.semusz, SYSCTL_INT_READONLY },
	{ KERN_SEMINFO_SEMVMX, &seminfo.semvmx, SYSCTL_INT_READONLY },
	{ KERN_SEMINFO_SEMAEM, &seminfo.semaem, SYSCTL_INT_READONLY },
	{ KERN_SEMINFO_SEMOPM, &seminfo.semopm, 1, SEMOP_MAX },
};

/*
 * Userland access to struct seminfo.
 */
int
sysctl_sysvsem(int *name, u_int namelen, void *oldp, size_t *oldlenp,
	void *newp, size_t newlen)
{
	int error, val;

	if (namelen != 1)
                        return (ENOTDIR);       /* leaf-only */

	switch (name[0]) {
	case KERN_SEMINFO_SEMMNI:
		val = seminfo.semmni;
		error = sysctl_int_bounded(oldp, oldlenp, newp, newlen,
		    &val, val, 0xffff);
		/* returns success and skips reallocation if val is unchanged */
		if (error || val == seminfo.semmni)
			return (error);
		sema_reallocate(val);
		return (0);
	case KERN_SEMINFO_SEMMNS:
		/* can't decrease semmns or go over 2^16 */
		return (sysctl_int_bounded(oldp, oldlenp, newp, newlen,
		    &seminfo.semmns, seminfo.semmns, 0xffff));
	case KERN_SEMINFO_SEMMNU:
		/* can't decrease semmnu or go over 2^16 */
		return (sysctl_int_bounded(oldp, oldlenp, newp, newlen,
		    &seminfo.semmnu, seminfo.semmnu, 0xffff));
	case KERN_SEMINFO_SEMMSL:
		/* can't decrease semmsl or go over 2^16 */
		return (sysctl_int_bounded(oldp, oldlenp, newp, newlen,
		    &seminfo.semmsl, seminfo.semmsl, 0xffff));
	default:
		return (sysctl_bounded_arr(sysvsem_vars, nitems(sysvsem_vars),
		    name, namelen, oldp, oldlenp, newp, newlen));
	}
	/* NOTREACHED */
}
