/*
 * linux/ipc/sem.c
 * Copyright (C) 1992 Krishna Balasubramanian
 * Copyright (C) 1995 Eric Schenk, Bruno Haible
 *
 * IMPLEMENTATION NOTES ON CODE REWRITE (Eric Schenk, January 1995):
 * This code underwent a massive rewrite in order to solve some problems
 * with the original code. In particular the original code failed to
 * wake up processes that were waiting for semval to go to 0 if the
 * value went to 0 and was then incremented rapidly enough. In solving
 * this problem I have also modified the implementation so that it
 * processes pending operations in a FIFO manner, thus give a guarantee
 * that processes waiting for a lock on the semaphore won't starve
 * unless another locking process fails to unlock.
 * In addition the following two changes in behavior have been introduced:
 * - The original implementation of semop returned the value
 *   last semaphore element examined on success. This does not
 *   match the manual page specifications, and effectively
 *   allows the user to read the semaphore even if they do not
 *   have read permissions. The implementation now returns 0
 *   on success as stated in the manual page.
 * - There is some confusion over whether the set of undo adjustments
 *   to be performed at exit should be done in an atomic manner.
 *   That is, if we are attempting to decrement the semval should we queue
 *   up and wait until we can do so legally?
 *   The original implementation attempted to do this.
 *   The current implementation does not do so. This is because I don't
 *   think it is the right thing (TM) to do, and because I couldn't
 *   see a clean way to get the old behavior with the new design.
 *   The POSIX standard and SVID should be consulted to determine
 *   what behavior is mandated.
 *
 * Further notes on refinement (Christoph Rohland, December 1998):
 * - The POSIX standard says, that the undo adjustments simply should
 *   redo. So the current implementation is o.K.
 * - The previous code had two flaws:
 *   1) It actively gave the semaphore to the next waiting process
 *      sleeping on the semaphore. Since this process did not have the
 *      cpu this led to many unnecessary context switches and bad
 *      performance. Now we only check which process should be able to
 *      get the semaphore and if this process wants to reduce some
 *      semaphore value we simply wake it up without doing the
 *      operation. So it has to try to get it later. Thus e.g. the
 *      running process may reacquire the semaphore during the current
 *      time slice. If it only waits for zero or increases the semaphore,
 *      we do the operation in advance and wake it up.
 *   2) It did not wake up all zero waiting processes. We try to do
 *      better but only get the semops right which only wait for zero or
 *      increase. If there are decrement operations in the operations
 *      array we do the same as before.
 *
 * With the incarnation of O(1) scheduler, it becomes unnecessary to perform
 * check/retry algorithm for waking up blocked processes as the new scheduler
 * is better at handling thread switch than the old one.
 *
 * /proc/sysvipc/sem support (c) 1999 Dragos Acostachioaie <dragos@iname.com>
 *
 * SMP-threaded, sysctl's added
 * (c) 1999 Manfred Spraul <manfred@colorfullife.com>
 * Enforced range limit on SEM_UNDO
 * (c) 2001 Red Hat Inc <alan@redhat.com>
 * Lockless wakeup
 * (c) 2003 Manfred Spraul <manfred@colorfullife.com>
 *
 * support for audit of ipc object properties and permission changes
 * Dustin Kirkland <dustin.kirkland@us.ibm.com>
 *
 * namespaces support
 * OpenVZ, SWsoft Inc.
 * Pavel Emelianov <xemul@openvz.org>
 */

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/time.h>
#include <linux/smp_lock.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/audit.h>
#include <linux/capability.h>
#include <linux/seq_file.h>
#include <linux/mutex.h>
#include <linux/nsproxy.h>

#include <asm/uaccess.h>
#include "util.h"

#define sem_ids(ns)	(*((ns)->ids[IPC_SEM_IDS]))

#define sem_lock(ns, id)	((struct sem_array*)ipc_lock(&sem_ids(ns), id))
#define sem_unlock(sma)		ipc_unlock(&(sma)->sem_perm)
#define sem_rmid(ns, id)	((struct sem_array*)ipc_rmid(&sem_ids(ns), id))
#define sem_checkid(ns, sma, semid)	\
	ipc_checkid(&sem_ids(ns),&sma->sem_perm,semid)
#define sem_buildid(ns, id, seq) \
	ipc_buildid(&sem_ids(ns), id, seq)

static struct ipc_ids init_sem_ids;

static int newary(struct ipc_namespace *, key_t, int, int);
static void freeary(struct ipc_namespace *ns, struct sem_array *sma, int id);
#ifdef CONFIG_PROC_FS
static int sysvipc_sem_proc_show(struct seq_file *s, void *it);
#endif

#define SEMMSL_FAST	256 /* 512 bytes on stack */
#define SEMOPM_FAST	64  /* ~ 372 bytes on stack */

/*
 * linked list protection:
 *	sem_undo.id_next,
 *	sem_array.sem_pending{,last},
 *	sem_array.sem_undo: sem_lock() for read/write
 *	sem_undo.proc_next: only "current" is allowed to read/write that field.
 *	
 */

#define sc_semmsl	sem_ctls[0]
#define sc_semmns	sem_ctls[1]
#define sc_semopm	sem_ctls[2]
#define sc_semmni	sem_ctls[3]

static void __ipc_init __sem_init_ns(struct ipc_namespace *ns, struct ipc_ids *ids)
{
	ns->ids[IPC_SEM_IDS] = ids;
	ns->sc_semmsl = SEMMSL;
	ns->sc_semmns = SEMMNS;
	ns->sc_semopm = SEMOPM;
	ns->sc_semmni = SEMMNI;
	ns->used_sems = 0;
	ipc_init_ids(ids, ns->sc_semmni);
}

#ifdef CONFIG_IPC_NS
int sem_init_ns(struct ipc_namespace *ns)
{
	struct ipc_ids *ids;

	ids = kmalloc(sizeof(struct ipc_ids), GFP_KERNEL);
	if (ids == NULL)
		return -ENOMEM;

	__sem_init_ns(ns, ids);
	return 0;
}

void sem_exit_ns(struct ipc_namespace *ns)
{
	int i;
	struct sem_array *sma;

	mutex_lock(&sem_ids(ns).mutex);
	for (i = 0; i <= sem_ids(ns).max_id; i++) {
		sma = sem_lock(ns, i);
		if (sma == NULL)
			continue;

		freeary(ns, sma, i);
	}
	mutex_unlock(&sem_ids(ns).mutex);

	ipc_fini_ids(ns->ids[IPC_SEM_IDS]);
	kfree(ns->ids[IPC_SEM_IDS]);
	ns->ids[IPC_SEM_IDS] = NULL;
}
#endif

void __init sem_init (void)
{
	__sem_init_ns(&init_ipc_ns, &init_sem_ids);
	ipc_init_proc_interface("sysvipc/sem",
				"       key      semid perms      nsems   uid   gid  cuid  cgid      otime      ctime\n",
				IPC_SEM_IDS, sysvipc_sem_proc_show);
}

/*
 * Lockless wakeup algorithm:
 * Without the check/retry algorithm a lockless wakeup is possible:
 * - queue.status is initialized to -EINTR before blocking.
 * - wakeup is performed by
 *	* unlinking the queue entry from sma->sem_pending
 *	* setting queue.status to IN_WAKEUP
 *	  This is the notification for the blocked thread that a
 *	  result value is imminent.
 *	* call wake_up_process
 *	* set queue.status to the final value.
 * - the previously blocked thread checks queue.status:
 *   	* if it's IN_WAKEUP, then it must wait until the value changes
 *   	* if it's not -EINTR, then the operation was completed by
 *   	  update_queue. semtimedop can return queue.status without
 *   	  performing any operation on the sem array.
 *   	* otherwise it must acquire the spinlock and check what's up.
 *
 * The two-stage algorithm is necessary to protect against the following
 * races:
 * - if queue.status is set after wake_up_process, then the woken up idle
 *   thread could race forward and try (and fail) to acquire sma->lock
 *   before update_queue had a chance to set queue.status
 * - if queue.status is written before wake_up_process and if the
 *   blocked process is woken up by a signal between writing
 *   queue.status and the wake_up_process, then the woken up
 *   process could return from semtimedop and die by calling
 *   sys_exit before wake_up_process is called. Then wake_up_process
 *   will oops, because the task structure is already invalid.
 *   (yes, this happened on s390 with sysv msg).
 *
 */
#define IN_WAKEUP	1

static int newary (struct ipc_namespace *ns, key_t key, int nsems, int semflg)
{
	int id;
	int retval;
	struct sem_array *sma;
	int size;

	if (!nsems)
		return -EINVAL;
	if (ns->used_sems + nsems > ns->sc_semmns)
		return -ENOSPC;

	size = sizeof (*sma) + nsems * sizeof (struct sem);
	sma = ipc_rcu_alloc(size);
	if (!sma) {
		return -ENOMEM;
	}
	memset (sma, 0, size);

	sma->sem_perm.mode = (semflg & S_IRWXUGO);
	sma->sem_perm.key = key;

	sma->sem_perm.security = NULL;
	retval = security_sem_alloc(sma);
	if (retval) {
		ipc_rcu_putref(sma);
		return retval;
	}

	id = ipc_addid(&sem_ids(ns), &sma->sem_perm, ns->sc_semmni);
	if(id == -1) {
		security_sem_free(sma);
		ipc_rcu_putref(sma);
		return -ENOSPC;
	}
	ns->used_sems += nsems;

	sma->sem_id = sem_buildid(ns, id, sma->sem_perm.seq);
	sma->sem_base = (struct sem *) &sma[1];
	/* sma->sem_pending = NULL; */
	sma->sem_pending_last = &sma->sem_pending;
	/* sma->undo = NULL; */
	sma->sem_nsems = nsems;
	sma->sem_ctime = get_seconds();
	sem_unlock(sma);

	return sma->sem_id;
}

asmlinkage long sys_semget (key_t key, int nsems, int semflg)
{
	int id, err = -EINVAL;
	struct sem_array *sma;
	struct ipc_namespace *ns;

	ns = current->nsproxy->ipc_ns;

	if (nsems < 0 || nsems > ns->sc_semmsl)
		return -EINVAL;
	mutex_lock(&sem_ids(ns).mutex);
	
	if (key == IPC_PRIVATE) {
		err = newary(ns, key, nsems, semflg);
	} else if ((id = ipc_findkey(&sem_ids(ns), key)) == -1) {  /* key not used */
		if (!(semflg & IPC_CREAT))
			err = -ENOENT;
		else
			err = newary(ns, key, nsems, semflg);
	} else if (semflg & IPC_CREAT && semflg & IPC_EXCL) {
		err = -EEXIST;
	} else {
		sma = sem_lock(ns, id);
		BUG_ON(sma==NULL);
		if (nsems > sma->sem_nsems)
			err = -EINVAL;
		else if (ipcperms(&sma->sem_perm, semflg))
			err = -EACCES;
		else {
			int semid = sem_buildid(ns, id, sma->sem_perm.seq);
			err = security_sem_associate(sma, semflg);
			if (!err)
				err = semid;
		}
		sem_unlock(sma);
	}

	mutex_unlock(&sem_ids(ns).mutex);
	return err;
}

/* Manage the doubly linked list sma->sem_pending as a FIFO:
 * insert new queue elements at the tail sma->sem_pending_last.
 */
static inline void append_to_queue (struct sem_array * sma,
				    struct sem_queue * q)
{
	*(q->prev = sma->sem_pending_last) = q;
	*(sma->sem_pending_last = &q->next) = NULL;
}

static inline void prepend_to_queue (struct sem_array * sma,
				     struct sem_queue * q)
{
	q->next = sma->sem_pending;
	*(q->prev = &sma->sem_pending) = q;
	if (q->next)
		q->next->prev = &q->next;
	else /* sma->sem_pending_last == &sma->sem_pending */
		sma->sem_pending_last = &q->next;
}

static inline void remove_from_queue (struct sem_array * sma,
				      struct sem_queue * q)
{
	*(q->prev) = q->next;
	if (q->next)
		q->next->prev = q->prev;
	else /* sma->sem_pending_last == &q->next */
		sma->sem_pending_last = q->prev;
	q->prev = NULL; /* mark as removed */
}

/*
 * Determine whether a sequence of semaphore operations would succeed
 * all at once. Return 0 if yes, 1 if need to sleep, else return error code.
 */

static int try_atomic_semop (struct sem_array * sma, struct sembuf * sops,
			     int nsops, struct sem_undo *un, int pid)
{
	int result, sem_op;
	struct sembuf *sop;
	struct sem * curr;

	for (sop = sops; sop < sops + nsops; sop++) {
		curr = sma->sem_base + sop->sem_num;
		sem_op = sop->sem_op;
		result = curr->semval;
  
		if (!sem_op && result)
			goto would_block;

		result += sem_op;
		if (result < 0)
			goto would_block;
		if (result > SEMVMX)
			goto out_of_range;
		if (sop->sem_flg & SEM_UNDO) {
			int undo = un->semadj[sop->sem_num] - sem_op;
			/*
	 		 *	Exceeding the undo range is an error.
			 */
			if (undo < (-SEMAEM - 1) || undo > SEMAEM)
				goto out_of_range;
		}
		curr->semval = result;
	}

	sop--;
	while (sop >= sops) {
		sma->sem_base[sop->sem_num].sempid = pid;
		if (sop->sem_flg & SEM_UNDO)
			un->semadj[sop->sem_num] -= sop->sem_op;
		sop--;
	}
	
	sma->sem_otime = get_seconds();
	return 0;

out_of_range:
	result = -ERANGE;
	goto undo;

would_block:
	if (sop->sem_flg & IPC_NOWAIT)
		result = -EAGAIN;
	else
		result = 1;

undo:
	sop--;
	while (sop >= sops) {
		sma->sem_base[sop->sem_num].semval -= sop->sem_op;
		sop--;
	}

	return result;
}

/* Go through the pending queue for the indicated semaphore
 * looking for tasks that can be completed.
 */
static void update_queue (struct sem_array * sma)
{
	int error;
	struct sem_queue * q;

	q = sma->sem_pending;
	while(q) {
		error = try_atomic_semop(sma, q->sops, q->nsops,
					 q->undo, q->pid);

		/* Does q->sleeper still need to sleep? */
		if (error <= 0) {
			struct sem_queue *n;
			remove_from_queue(sma,q);
			q->status = IN_WAKEUP;
			/*
			 * Continue scanning. The next operation
			 * that must be checked depends on the type of the
			 * completed operation:
			 * - if the operation modified the array, then
			 *   restart from the head of the queue and
			 *   check for threads that might be waiting
			 *   for semaphore values to become 0.
			 * - if the operation didn't modify the array,
			 *   then just continue.
			 */
			if (q->alter)
				n = sma->sem_pending;
			else
				n = q->next;
			wake_up_process(q->sleeper);
			/* hands-off: q will disappear immediately after
			 * writing q->status.
			 */
			smp_wmb();
			q->status = error;
			q = n;
		} else {
			q = q->next;
		}
	}
}

/* The following counts are associated to each semaphore:
 *   semncnt        number of tasks waiting on semval being nonzero
 *   semzcnt        number of tasks waiting on semval being zero
 * This model assumes that a task waits on exactly one semaphore.
 * Since semaphore operations are to be performed atomically, tasks actually
 * wait on a whole sequence of semaphores simultaneously.
 * The counts we return here are a rough approximation, but still
 * warrant that semncnt+semzcnt>0 if the task is on the pending queue.
 */
static int count_semncnt (struct sem_array * sma, ushort semnum)
{
	int semncnt;
	struct sem_queue * q;

	semncnt = 0;
	for (q = sma->sem_pending; q; q = q->next) {
		struct sembuf * sops = q->sops;
		int nsops = q->nsops;
		int i;
		for (i = 0; i < nsops; i++)
			if (sops[i].sem_num == semnum
			    && (sops[i].sem_op < 0)
			    && !(sops[i].sem_flg & IPC_NOWAIT))
				semncnt++;
	}
	return semncnt;
}
static int count_semzcnt (struct sem_array * sma, ushort semnum)
{
	int semzcnt;
	struct sem_queue * q;

	semzcnt = 0;
	for (q = sma->sem_pending; q; q = q->next) {
		struct sembuf * sops = q->sops;
		int nsops = q->nsops;
		int i;
		for (i = 0; i < nsops; i++)
			if (sops[i].sem_num == semnum
			    && (sops[i].sem_op == 0)
			    && !(sops[i].sem_flg & IPC_NOWAIT))
				semzcnt++;
	}
	return semzcnt;
}

/* Free a semaphore set. freeary() is called with sem_ids.mutex locked and
 * the spinlock for this semaphore set hold. sem_ids.mutex remains locked
 * on exit.
 */
static void freeary (struct ipc_namespace *ns, struct sem_array *sma, int id)
{
	struct sem_undo *un;
	struct sem_queue *q;
	int size;

	/* Invalidate the existing undo structures for this semaphore set.
	 * (They will be freed without any further action in exit_sem()
	 * or during the next semop.)
	 */
	for (un = sma->undo; un; un = un->id_next)
		un->semid = -1;

	/* Wake up all pending processes and let them fail with EIDRM. */
	q = sma->sem_pending;
	while(q) {
		struct sem_queue *n;
		/* lazy remove_from_queue: we are killing the whole queue */
		q->prev = NULL;
		n = q->next;
		q->status = IN_WAKEUP;
		wake_up_process(q->sleeper); /* doesn't sleep */
		smp_wmb();
		q->status = -EIDRM;	/* hands-off q */
		q = n;
	}

	/* Remove the semaphore set from the ID array*/
	sma = sem_rmid(ns, id);
	sem_unlock(sma);

	ns->used_sems -= sma->sem_nsems;
	size = sizeof (*sma) + sma->sem_nsems * sizeof (struct sem);
	security_sem_free(sma);
	ipc_rcu_putref(sma);
}

static unsigned long copy_semid_to_user(void __user *buf, struct semid64_ds *in, int version)
{
	switch(version) {
	case IPC_64:
		return copy_to_user(buf, in, sizeof(*in));
	case IPC_OLD:
	    {
		struct semid_ds out;

		ipc64_perm_to_ipc_perm(&in->sem_perm, &out.sem_perm);

		out.sem_otime	= in->sem_otime;
		out.sem_ctime	= in->sem_ctime;
		out.sem_nsems	= in->sem_nsems;

		return copy_to_user(buf, &out, sizeof(out));
	    }
	default:
		return -EINVAL;
	}
}

static int semctl_nolock(struct ipc_namespace *ns, int semid, int semnum,
		int cmd, int version, union semun arg)
{
	int err = -EINVAL;
	struct sem_array *sma;

	switch(cmd) {
	case IPC_INFO:
	case SEM_INFO:
	{
		struct seminfo seminfo;
		int max_id;

		err = security_sem_semctl(NULL, cmd);
		if (err)
			return err;
		
		memset(&seminfo,0,sizeof(seminfo));
		seminfo.semmni = ns->sc_semmni;
		seminfo.semmns = ns->sc_semmns;
		seminfo.semmsl = ns->sc_semmsl;
		seminfo.semopm = ns->sc_semopm;
		seminfo.semvmx = SEMVMX;
		seminfo.semmnu = SEMMNU;
		seminfo.semmap = SEMMAP;
		seminfo.semume = SEMUME;
		mutex_lock(&sem_ids(ns).mutex);
		if (cmd == SEM_INFO) {
			seminfo.semusz = sem_ids(ns).in_use;
			seminfo.semaem = ns->used_sems;
		} else {
			seminfo.semusz = SEMUSZ;
			seminfo.semaem = SEMAEM;
		}
		max_id = sem_ids(ns).max_id;
		mutex_unlock(&sem_ids(ns).mutex);
		if (copy_to_user (arg.__buf, &seminfo, sizeof(struct seminfo))) 
			return -EFAULT;
		return (max_id < 0) ? 0: max_id;
	}
	case SEM_STAT:
	{
		struct semid64_ds tbuf;
		int id;

		if(semid >= sem_ids(ns).entries->size)
			return -EINVAL;

		memset(&tbuf,0,sizeof(tbuf));

		sma = sem_lock(ns, semid);
		if(sma == NULL)
			return -EINVAL;

		err = -EACCES;
		if (ipcperms (&sma->sem_perm, S_IRUGO))
			goto out_unlock;

		err = security_sem_semctl(sma, cmd);
		if (err)
			goto out_unlock;

		id = sem_buildid(ns, semid, sma->sem_perm.seq);

		kernel_to_ipc64_perm(&sma->sem_perm, &tbuf.sem_perm);
		tbuf.sem_otime  = sma->sem_otime;
		tbuf.sem_ctime  = sma->sem_ctime;
		tbuf.sem_nsems  = sma->sem_nsems;
		sem_unlock(sma);
		if (copy_semid_to_user (arg.buf, &tbuf, version))
			return -EFAULT;
		return id;
	}
	default:
		return -EINVAL;
	}
	return err;
out_unlock:
	sem_unlock(sma);
	return err;
}

static int semctl_main(struct ipc_namespace *ns, int semid, int semnum,
		int cmd, int version, union semun arg)
{
	struct sem_array *sma;
	struct sem* curr;
	int err;
	ushort fast_sem_io[SEMMSL_FAST];
	ushort* sem_io = fast_sem_io;
	int nsems;

	sma = sem_lock(ns, semid);
	if(sma==NULL)
		return -EINVAL;

	nsems = sma->sem_nsems;

	err=-EIDRM;
	if (sem_checkid(ns,sma,semid))
		goto out_unlock;

	err = -EACCES;
	if (ipcperms (&sma->sem_perm, (cmd==SETVAL||cmd==SETALL)?S_IWUGO:S_IRUGO))
		goto out_unlock;

	err = security_sem_semctl(sma, cmd);
	if (err)
		goto out_unlock;

	err = -EACCES;
	switch (cmd) {
	case GETALL:
	{
		ushort __user *array = arg.array;
		int i;

		if(nsems > SEMMSL_FAST) {
			ipc_rcu_getref(sma);
			sem_unlock(sma);			

			sem_io = ipc_alloc(sizeof(ushort)*nsems);
			if(sem_io == NULL) {
				ipc_lock_by_ptr(&sma->sem_perm);
				ipc_rcu_putref(sma);
				sem_unlock(sma);
				return -ENOMEM;
			}

			ipc_lock_by_ptr(&sma->sem_perm);
			ipc_rcu_putref(sma);
			if (sma->sem_perm.deleted) {
				sem_unlock(sma);
				err = -EIDRM;
				goto out_free;
			}
		}

		for (i = 0; i < sma->sem_nsems; i++)
			sem_io[i] = sma->sem_base[i].semval;
		sem_unlock(sma);
		err = 0;
		if(copy_to_user(array, sem_io, nsems*sizeof(ushort)))
			err = -EFAULT;
		goto out_free;
	}
	case SETALL:
	{
		int i;
		struct sem_undo *un;

		ipc_rcu_getref(sma);
		sem_unlock(sma);

		if(nsems > SEMMSL_FAST) {
			sem_io = ipc_alloc(sizeof(ushort)*nsems);
			if(sem_io == NULL) {
				ipc_lock_by_ptr(&sma->sem_perm);
				ipc_rcu_putref(sma);
				sem_unlock(sma);
				return -ENOMEM;
			}
		}

		if (copy_from_user (sem_io, arg.array, nsems*sizeof(ushort))) {
			ipc_lock_by_ptr(&sma->sem_perm);
			ipc_rcu_putref(sma);
			sem_unlock(sma);
			err = -EFAULT;
			goto out_free;
		}

		for (i = 0; i < nsems; i++) {
			if (sem_io[i] > SEMVMX) {
				ipc_lock_by_ptr(&sma->sem_perm);
				ipc_rcu_putref(sma);
				sem_unlock(sma);
				err = -ERANGE;
				goto out_free;
			}
		}
		ipc_lock_by_ptr(&sma->sem_perm);
		ipc_rcu_putref(sma);
		if (sma->sem_perm.deleted) {
			sem_unlock(sma);
			err = -EIDRM;
			goto out_free;
		}

		for (i = 0; i < nsems; i++)
			sma->sem_base[i].semval = sem_io[i];
		for (un = sma->undo; un; un = un->id_next)
			for (i = 0; i < nsems; i++)
				un->semadj[i] = 0;
		sma->sem_ctime = get_seconds();
		/* maybe some queued-up processes were waiting for this */
		update_queue(sma);
		err = 0;
		goto out_unlock;
	}
	case IPC_STAT:
	{
		struct semid64_ds tbuf;
		memset(&tbuf,0,sizeof(tbuf));
		kernel_to_ipc64_perm(&sma->sem_perm, &tbuf.sem_perm);
		tbuf.sem_otime  = sma->sem_otime;
		tbuf.sem_ctime  = sma->sem_ctime;
		tbuf.sem_nsems  = sma->sem_nsems;
		sem_unlock(sma);
		if (copy_semid_to_user (arg.buf, &tbuf, version))
			return -EFAULT;
		return 0;
	}
	/* GETVAL, GETPID, GETNCTN, GETZCNT, SETVAL: fall-through */
	}
	err = -EINVAL;
	if(semnum < 0 || semnum >= nsems)
		goto out_unlock;

	curr = &sma->sem_base[semnum];

	switch (cmd) {
	case GETVAL:
		err = curr->semval;
		goto out_unlock;
	case GETPID:
		err = curr->sempid;
		goto out_unlock;
	case GETNCNT:
		err = count_semncnt(sma,semnum);
		goto out_unlock;
	case GETZCNT:
		err = count_semzcnt(sma,semnum);
		goto out_unlock;
	case SETVAL:
	{
		int val = arg.val;
		struct sem_undo *un;
		err = -ERANGE;
		if (val > SEMVMX || val < 0)
			goto out_unlock;

		for (un = sma->undo; un; un = un->id_next)
			un->semadj[semnum] = 0;
		curr->semval = val;
		curr->sempid = current->tgid;
		sma->sem_ctime = get_seconds();
		/* maybe some queued-up processes were waiting for this */
		update_queue(sma);
		err = 0;
		goto out_unlock;
	}
	}
out_unlock:
	sem_unlock(sma);
out_free:
	if(sem_io != fast_sem_io)
		ipc_free(sem_io, sizeof(ushort)*nsems);
	return err;
}

struct sem_setbuf {
	uid_t	uid;
	gid_t	gid;
	mode_t	mode;
};

static inline unsigned long copy_semid_from_user(struct sem_setbuf *out, void __user *buf, int version)
{
	switch(version) {
	case IPC_64:
	    {
		struct semid64_ds tbuf;

		if(copy_from_user(&tbuf, buf, sizeof(tbuf)))
			return -EFAULT;

		out->uid	= tbuf.sem_perm.uid;
		out->gid	= tbuf.sem_perm.gid;
		out->mode	= tbuf.sem_perm.mode;

		return 0;
	    }
	case IPC_OLD:
	    {
		struct semid_ds tbuf_old;

		if(copy_from_user(&tbuf_old, buf, sizeof(tbuf_old)))
			return -EFAULT;

		out->uid	= tbuf_old.sem_perm.uid;
		out->gid	= tbuf_old.sem_perm.gid;
		out->mode	= tbuf_old.sem_perm.mode;

		return 0;
	    }
	default:
		return -EINVAL;
	}
}

static int semctl_down(struct ipc_namespace *ns, int semid, int semnum,
		int cmd, int version, union semun arg)
{
	struct sem_array *sma;
	int err;
	struct sem_setbuf setbuf;
	struct kern_ipc_perm *ipcp;

	if(cmd == IPC_SET) {
		if(copy_semid_from_user (&setbuf, arg.buf, version))
			return -EFAULT;
	}
	sma = sem_lock(ns, semid);
	if(sma==NULL)
		return -EINVAL;

	if (sem_checkid(ns,sma,semid)) {
		err=-EIDRM;
		goto out_unlock;
	}	
	ipcp = &sma->sem_perm;

	err = audit_ipc_obj(ipcp);
	if (err)
		goto out_unlock;

	if (cmd == IPC_SET) {
		err = audit_ipc_set_perm(0, setbuf.uid, setbuf.gid, setbuf.mode);
		if (err)
			goto out_unlock;
	}
	if (current->euid != ipcp->cuid && 
	    current->euid != ipcp->uid && !capable(CAP_SYS_ADMIN)) {
	    	err=-EPERM;
		goto out_unlock;
	}

	err = security_sem_semctl(sma, cmd);
	if (err)
		goto out_unlock;

	switch(cmd){
	case IPC_RMID:
		freeary(ns, sma, semid);
		err = 0;
		break;
	case IPC_SET:
		ipcp->uid = setbuf.uid;
		ipcp->gid = setbuf.gid;
		ipcp->mode = (ipcp->mode & ~S_IRWXUGO)
				| (setbuf.mode & S_IRWXUGO);
		sma->sem_ctime = get_seconds();
		sem_unlock(sma);
		err = 0;
		break;
	default:
		sem_unlock(sma);
		err = -EINVAL;
		break;
	}
	return err;

out_unlock:
	sem_unlock(sma);
	return err;
}

asmlinkage long sys_semctl (int semid, int semnum, int cmd, union semun arg)
{
	int err = -EINVAL;
	int version;
	struct ipc_namespace *ns;

	if (semid < 0)
		return -EINVAL;

	version = ipc_parse_version(&cmd);
	ns = current->nsproxy->ipc_ns;

	switch(cmd) {
	case IPC_INFO:
	case SEM_INFO:
	case SEM_STAT:
		err = semctl_nolock(ns,semid,semnum,cmd,version,arg);
		return err;
	case GETALL:
	case GETVAL:
	case GETPID:
	case GETNCNT:
	case GETZCNT:
	case IPC_STAT:
	case SETVAL:
	case SETALL:
		err = semctl_main(ns,semid,semnum,cmd,version,arg);
		return err;
	case IPC_RMID:
	case IPC_SET:
		mutex_lock(&sem_ids(ns).mutex);
		err = semctl_down(ns,semid,semnum,cmd,version,arg);
		mutex_unlock(&sem_ids(ns).mutex);
		return err;
	default:
		return -EINVAL;
	}
}

static inline void lock_semundo(void)
{
	struct sem_undo_list *undo_list;

	undo_list = current->sysvsem.undo_list;
	if (undo_list)
		spin_lock(&undo_list->lock);
}

/* This code has an interaction with copy_semundo().
 * Consider; two tasks are sharing the undo_list. task1
 * acquires the undo_list lock in lock_semundo().  If task2 now
 * exits before task1 releases the lock (by calling
 * unlock_semundo()), then task1 will never call spin_unlock().
 * This leave the sem_undo_list in a locked state.  If task1 now creats task3
 * and once again shares the sem_undo_list, the sem_undo_list will still be
 * locked, and future SEM_UNDO operations will deadlock.  This case is
 * dealt with in copy_semundo() by having it reinitialize the spin lock when 
 * the refcnt goes from 1 to 2.
 */
static inline void unlock_semundo(void)
{
	struct sem_undo_list *undo_list;

	undo_list = current->sysvsem.undo_list;
	if (undo_list)
		spin_unlock(&undo_list->lock);
}


/* If the task doesn't already have a undo_list, then allocate one
 * here.  We guarantee there is only one thread using this undo list,
 * and current is THE ONE
 *
 * If this allocation and assignment succeeds, but later
 * portions of this code fail, there is no need to free the sem_undo_list.
 * Just let it stay associated with the task, and it'll be freed later
 * at exit time.
 *
 * This can block, so callers must hold no locks.
 */
static inline int get_undo_list(struct sem_undo_list **undo_listp)
{
	struct sem_undo_list *undo_list;

	undo_list = current->sysvsem.undo_list;
	if (!undo_list) {
		undo_list = kzalloc(sizeof(*undo_list), GFP_KERNEL);
		if (undo_list == NULL)
			return -ENOMEM;
		spin_lock_init(&undo_list->lock);
		atomic_set(&undo_list->refcnt, 1);
		current->sysvsem.undo_list = undo_list;
	}
	*undo_listp = undo_list;
	return 0;
}

static struct sem_undo *lookup_undo(struct sem_undo_list *ulp, int semid)
{
	struct sem_undo **last, *un;

	last = &ulp->proc_list;
	un = *last;
	while(un != NULL) {
		if(un->semid==semid)
			break;
		if(un->semid==-1) {
			*last=un->proc_next;
			kfree(un);
		} else {
			last=&un->proc_next;
		}
		un=*last;
	}
	return un;
}

static struct sem_undo *find_undo(struct ipc_namespace *ns, int semid)
{
	struct sem_array *sma;
	struct sem_undo_list *ulp;
	struct sem_undo *un, *new;
	int nsems;
	int error;

	error = get_undo_list(&ulp);
	if (error)
		return ERR_PTR(error);

	lock_semundo();
	un = lookup_undo(ulp, semid);
	unlock_semundo();
	if (likely(un!=NULL))
		goto out;

	/* no undo structure around - allocate one. */
	sma = sem_lock(ns, semid);
	un = ERR_PTR(-EINVAL);
	if(sma==NULL)
		goto out;
	un = ERR_PTR(-EIDRM);
	if (sem_checkid(ns,sma,semid)) {
		sem_unlock(sma);
		goto out;
	}
	nsems = sma->sem_nsems;
	ipc_rcu_getref(sma);
	sem_unlock(sma);

	new = (struct sem_undo *) kmalloc(sizeof(struct sem_undo) + sizeof(short)*nsems, GFP_KERNEL);
	if (!new) {
		ipc_lock_by_ptr(&sma->sem_perm);
		ipc_rcu_putref(sma);
		sem_unlock(sma);
		return ERR_PTR(-ENOMEM);
	}
	memset(new, 0, sizeof(struct sem_undo) + sizeof(short)*nsems);
	new->semadj = (short *) &new[1];
	new->semid = semid;

	lock_semundo();
	un = lookup_undo(ulp, semid);
	if (un) {
		unlock_semundo();
		kfree(new);
		ipc_lock_by_ptr(&sma->sem_perm);
		ipc_rcu_putref(sma);
		sem_unlock(sma);
		goto out;
	}
	ipc_lock_by_ptr(&sma->sem_perm);
	ipc_rcu_putref(sma);
	if (sma->sem_perm.deleted) {
		sem_unlock(sma);
		unlock_semundo();
		kfree(new);
		un = ERR_PTR(-EIDRM);
		goto out;
	}
	new->proc_next = ulp->proc_list;
	ulp->proc_list = new;
	new->id_next = sma->undo;
	sma->undo = new;
	sem_unlock(sma);
	un = new;
	unlock_semundo();
out:
	return un;
}

asmlinkage long sys_semtimedop(int semid, struct sembuf __user *tsops,
			unsigned nsops, const struct timespec __user *timeout)
{
	int error = -EINVAL;
	struct sem_array *sma;
	struct sembuf fast_sops[SEMOPM_FAST];
	struct sembuf* sops = fast_sops, *sop;
	struct sem_undo *un;
	int undos = 0, alter = 0, max;
	struct sem_queue queue;
	unsigned long jiffies_left = 0;
	struct ipc_namespace *ns;

	ns = current->nsproxy->ipc_ns;

	if (nsops < 1 || semid < 0)
		return -EINVAL;
	if (nsops > ns->sc_semopm)
		return -E2BIG;
	if(nsops > SEMOPM_FAST) {
		sops = kmalloc(sizeof(*sops)*nsops,GFP_KERNEL);
		if(sops==NULL)
			return -ENOMEM;
	}
	if (copy_from_user (sops, tsops, nsops * sizeof(*tsops))) {
		error=-EFAULT;
		goto out_free;
	}
	if (timeout) {
		struct timespec _timeout;
		if (copy_from_user(&_timeout, timeout, sizeof(*timeout))) {
			error = -EFAULT;
			goto out_free;
		}
		if (_timeout.tv_sec < 0 || _timeout.tv_nsec < 0 ||
			_timeout.tv_nsec >= 1000000000L) {
			error = -EINVAL;
			goto out_free;
		}
		jiffies_left = timespec_to_jiffies(&_timeout);
	}
	max = 0;
	for (sop = sops; sop < sops + nsops; sop++) {
		if (sop->sem_num >= max)
			max = sop->sem_num;
		if (sop->sem_flg & SEM_UNDO)
			undos = 1;
		if (sop->sem_op != 0)
			alter = 1;
	}

retry_undos:
	if (undos) {
		un = find_undo(ns, semid);
		if (IS_ERR(un)) {
			error = PTR_ERR(un);
			goto out_free;
		}
	} else
		un = NULL;

	sma = sem_lock(ns, semid);
	error=-EINVAL;
	if(sma==NULL)
		goto out_free;
	error = -EIDRM;
	if (sem_checkid(ns,sma,semid))
		goto out_unlock_free;
	/*
	 * semid identifies are not unique - find_undo may have
	 * allocated an undo structure, it was invalidated by an RMID
	 * and now a new array with received the same id. Check and retry.
	 */
	if (un && un->semid == -1) {
		sem_unlock(sma);
		goto retry_undos;
	}
	error = -EFBIG;
	if (max >= sma->sem_nsems)
		goto out_unlock_free;

	error = -EACCES;
	if (ipcperms(&sma->sem_perm, alter ? S_IWUGO : S_IRUGO))
		goto out_unlock_free;

	error = security_sem_semop(sma, sops, nsops, alter);
	if (error)
		goto out_unlock_free;

	error = try_atomic_semop (sma, sops, nsops, un, current->tgid);
	if (error <= 0) {
		if (alter && error == 0)
			update_queue (sma);
		goto out_unlock_free;
	}

	/* We need to sleep on this operation, so we put the current
	 * task into the pending queue and go to sleep.
	 */
		
	queue.sma = sma;
	queue.sops = sops;
	queue.nsops = nsops;
	queue.undo = un;
	queue.pid = current->tgid;
	queue.id = semid;
	queue.alter = alter;
	if (alter)
		append_to_queue(sma ,&queue);
	else
		prepend_to_queue(sma ,&queue);

	queue.status = -EINTR;
	queue.sleeper = current;
	current->state = TASK_INTERRUPTIBLE;
	sem_unlock(sma);

	if (timeout)
		jiffies_left = schedule_timeout(jiffies_left);
	else
		schedule();

	error = queue.status;
	while(unlikely(error == IN_WAKEUP)) {
		cpu_relax();
		error = queue.status;
	}

	if (error != -EINTR) {
		/* fast path: update_queue already obtained all requested
		 * resources */
		goto out_free;
	}

	sma = sem_lock(ns, semid);
	if(sma==NULL) {
		BUG_ON(queue.prev != NULL);
		error = -EIDRM;
		goto out_free;
	}

	/*
	 * If queue.status != -EINTR we are woken up by another process
	 */
	error = queue.status;
	if (error != -EINTR) {
		goto out_unlock_free;
	}

	/*
	 * If an interrupt occurred we have to clean up the queue
	 */
	if (timeout && jiffies_left == 0)
		error = -EAGAIN;
	remove_from_queue(sma,&queue);
	goto out_unlock_free;

out_unlock_free:
	sem_unlock(sma);
out_free:
	if(sops != fast_sops)
		kfree(sops);
	return error;
}

asmlinkage long sys_semop (int semid, struct sembuf __user *tsops, unsigned nsops)
{
	return sys_semtimedop(semid, tsops, nsops, NULL);
}

/* If CLONE_SYSVSEM is set, establish sharing of SEM_UNDO state between
 * parent and child tasks.
 *
 * See the notes above unlock_semundo() regarding the spin_lock_init()
 * in this code.  Initialize the undo_list->lock here instead of get_undo_list()
 * because of the reasoning in the comment above unlock_semundo.
 */

int copy_semundo(unsigned long clone_flags, struct task_struct *tsk)
{
	struct sem_undo_list *undo_list;
	int error;

	if (clone_flags & CLONE_SYSVSEM) {
		error = get_undo_list(&undo_list);
		if (error)
			return error;
		atomic_inc(&undo_list->refcnt);
		tsk->sysvsem.undo_list = undo_list;
	} else 
		tsk->sysvsem.undo_list = NULL;

	return 0;
}

/*
 * add semadj values to semaphores, free undo structures.
 * undo structures are not freed when semaphore arrays are destroyed
 * so some of them may be out of date.
 * IMPLEMENTATION NOTE: There is some confusion over whether the
 * set of adjustments that needs to be done should be done in an atomic
 * manner or not. That is, if we are attempting to decrement the semval
 * should we queue up and wait until we can do so legally?
 * The original implementation attempted to do this (queue and wait).
 * The current implementation does not do so. The POSIX standard
 * and SVID should be consulted to determine what behavior is mandated.
 */
void exit_sem(struct task_struct *tsk)
{
	struct sem_undo_list *undo_list;
	struct sem_undo *u, **up;
	struct ipc_namespace *ns;

	undo_list = tsk->sysvsem.undo_list;
	if (!undo_list)
		return;

	if (!atomic_dec_and_test(&undo_list->refcnt))
		return;

	ns = tsk->nsproxy->ipc_ns;
	/* There's no need to hold the semundo list lock, as current
         * is the last task exiting for this undo list.
	 */
	for (up = &undo_list->proc_list; (u = *up); *up = u->proc_next, kfree(u)) {
		struct sem_array *sma;
		int nsems, i;
		struct sem_undo *un, **unp;
		int semid;
	       
		semid = u->semid;

		if(semid == -1)
			continue;
		sma = sem_lock(ns, semid);
		if (sma == NULL)
			continue;

		if (u->semid == -1)
			goto next_entry;

		BUG_ON(sem_checkid(ns,sma,u->semid));

		/* remove u from the sma->undo list */
		for (unp = &sma->undo; (un = *unp); unp = &un->id_next) {
			if (u == un)
				goto found;
		}
		printk ("exit_sem undo list error id=%d\n", u->semid);
		goto next_entry;
found:
		*unp = un->id_next;
		/* perform adjustments registered in u */
		nsems = sma->sem_nsems;
		for (i = 0; i < nsems; i++) {
			struct sem * semaphore = &sma->sem_base[i];
			if (u->semadj[i]) {
				semaphore->semval += u->semadj[i];
				/*
				 * Range checks of the new semaphore value,
				 * not defined by sus:
				 * - Some unices ignore the undo entirely
				 *   (e.g. HP UX 11i 11.22, Tru64 V5.1)
				 * - some cap the value (e.g. FreeBSD caps
				 *   at 0, but doesn't enforce SEMVMX)
				 *
				 * Linux caps the semaphore value, both at 0
				 * and at SEMVMX.
				 *
				 * 	Manfred <manfred@colorfullife.com>
				 */
				if (semaphore->semval < 0)
					semaphore->semval = 0;
				if (semaphore->semval > SEMVMX)
					semaphore->semval = SEMVMX;
				semaphore->sempid = current->tgid;
			}
		}
		sma->sem_otime = get_seconds();
		/* maybe some queued-up processes were waiting for this */
		update_queue(sma);
next_entry:
		sem_unlock(sma);
	}
	kfree(undo_list);
}

#ifdef CONFIG_PROC_FS
static int sysvipc_sem_proc_show(struct seq_file *s, void *it)
{
	struct sem_array *sma = it;

	return seq_printf(s,
			  "%10d %10d  %4o %10lu %5u %5u %5u %5u %10lu %10lu\n",
			  sma->sem_perm.key,
			  sma->sem_id,
			  sma->sem_perm.mode,
			  sma->sem_nsems,
			  sma->sem_perm.uid,
			  sma->sem_perm.gid,
			  sma->sem_perm.cuid,
			  sma->sem_perm.cgid,
			  sma->sem_otime,
			  sma->sem_ctime);
}
#endif
