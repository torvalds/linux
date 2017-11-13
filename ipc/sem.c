// SPDX-License-Identifier: GPL-2.0
/*
 * linux/ipc/sem.c
 * Copyright (C) 1992 Krishna Balasubramanian
 * Copyright (C) 1995 Eric Schenk, Bruno Haible
 *
 * /proc/sysvipc/sem support (c) 1999 Dragos Acostachioaie <dragos@iname.com>
 *
 * SMP-threaded, sysctl's added
 * (c) 1999 Manfred Spraul <manfred@colorfullife.com>
 * Enforced range limit on SEM_UNDO
 * (c) 2001 Red Hat Inc
 * Lockless wakeup
 * (c) 2003 Manfred Spraul <manfred@colorfullife.com>
 * (c) 2016 Davidlohr Bueso <dave@stgolabs.net>
 * Further wakeup optimizations, documentation
 * (c) 2010 Manfred Spraul <manfred@colorfullife.com>
 *
 * support for audit of ipc object properties and permission changes
 * Dustin Kirkland <dustin.kirkland@us.ibm.com>
 *
 * namespaces support
 * OpenVZ, SWsoft Inc.
 * Pavel Emelianov <xemul@openvz.org>
 *
 * Implementation notes: (May 2010)
 * This file implements System V semaphores.
 *
 * User space visible behavior:
 * - FIFO ordering for semop() operations (just FIFO, not starvation
 *   protection)
 * - multiple semaphore operations that alter the same semaphore in
 *   one semop() are handled.
 * - sem_ctime (time of last semctl()) is updated in the IPC_SET, SETVAL and
 *   SETALL calls.
 * - two Linux specific semctl() commands: SEM_STAT, SEM_INFO.
 * - undo adjustments at process exit are limited to 0..SEMVMX.
 * - namespace are supported.
 * - SEMMSL, SEMMNS, SEMOPM and SEMMNI can be configured at runtine by writing
 *   to /proc/sys/kernel/sem.
 * - statistics about the usage are reported in /proc/sysvipc/sem.
 *
 * Internals:
 * - scalability:
 *   - all global variables are read-mostly.
 *   - semop() calls and semctl(RMID) are synchronized by RCU.
 *   - most operations do write operations (actually: spin_lock calls) to
 *     the per-semaphore array structure.
 *   Thus: Perfect SMP scaling between independent semaphore arrays.
 *         If multiple semaphores in one array are used, then cache line
 *         trashing on the semaphore array spinlock will limit the scaling.
 * - semncnt and semzcnt are calculated on demand in count_semcnt()
 * - the task that performs a successful semop() scans the list of all
 *   sleeping tasks and completes any pending operations that can be fulfilled.
 *   Semaphores are actively given to waiting tasks (necessary for FIFO).
 *   (see update_queue())
 * - To improve the scalability, the actual wake-up calls are performed after
 *   dropping all locks. (see wake_up_sem_queue_prepare())
 * - All work is done by the waker, the woken up task does not have to do
 *   anything - not even acquiring a lock or dropping a refcount.
 * - A woken up task may not even touch the semaphore array anymore, it may
 *   have been destroyed already by a semctl(RMID).
 * - UNDO values are stored in an array (one per process and per
 *   semaphore array, lazily allocated). For backwards compatibility, multiple
 *   modes for the UNDO variables are supported (per process, per thread)
 *   (see copy_semundo, CLONE_SYSVSEM)
 * - There are two lists of the pending operations: a per-array list
 *   and per-semaphore list (stored in the array). This allows to achieve FIFO
 *   ordering without always scanning all pending operations.
 *   The worst-case behavior is nevertheless O(N^2) for N wakeups.
 */

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/time.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/audit.h>
#include <linux/capability.h>
#include <linux/seq_file.h>
#include <linux/rwsem.h>
#include <linux/nsproxy.h>
#include <linux/ipc_namespace.h>
#include <linux/sched/wake_q.h>

#include <linux/uaccess.h>
#include "util.h"


/* One queue for each sleeping process in the system. */
struct sem_queue {
	struct list_head	list;	 /* queue of pending operations */
	struct task_struct	*sleeper; /* this process */
	struct sem_undo		*undo;	 /* undo structure */
	int			pid;	 /* process id of requesting process */
	int			status;	 /* completion status of operation */
	struct sembuf		*sops;	 /* array of pending operations */
	struct sembuf		*blocking; /* the operation that blocked */
	int			nsops;	 /* number of operations */
	bool			alter;	 /* does *sops alter the array? */
	bool                    dupsop;	 /* sops on more than one sem_num */
};

/* Each task has a list of undo requests. They are executed automatically
 * when the process exits.
 */
struct sem_undo {
	struct list_head	list_proc;	/* per-process list: *
						 * all undos from one process
						 * rcu protected */
	struct rcu_head		rcu;		/* rcu struct for sem_undo */
	struct sem_undo_list	*ulp;		/* back ptr to sem_undo_list */
	struct list_head	list_id;	/* per semaphore array list:
						 * all undos for one array */
	int			semid;		/* semaphore set identifier */
	short			*semadj;	/* array of adjustments */
						/* one per semaphore */
};

/* sem_undo_list controls shared access to the list of sem_undo structures
 * that may be shared among all a CLONE_SYSVSEM task group.
 */
struct sem_undo_list {
	refcount_t		refcnt;
	spinlock_t		lock;
	struct list_head	list_proc;
};


#define sem_ids(ns)	((ns)->ids[IPC_SEM_IDS])

static int newary(struct ipc_namespace *, struct ipc_params *);
static void freeary(struct ipc_namespace *, struct kern_ipc_perm *);
#ifdef CONFIG_PROC_FS
static int sysvipc_sem_proc_show(struct seq_file *s, void *it);
#endif

#define SEMMSL_FAST	256 /* 512 bytes on stack */
#define SEMOPM_FAST	64  /* ~ 372 bytes on stack */

/*
 * Switching from the mode suitable for simple ops
 * to the mode for complex ops is costly. Therefore:
 * use some hysteresis
 */
#define USE_GLOBAL_LOCK_HYSTERESIS	10

/*
 * Locking:
 * a) global sem_lock() for read/write
 *	sem_undo.id_next,
 *	sem_array.complex_count,
 *	sem_array.pending{_alter,_const},
 *	sem_array.sem_undo
 *
 * b) global or semaphore sem_lock() for read/write:
 *	sem_array.sems[i].pending_{const,alter}:
 *
 * c) special:
 *	sem_undo_list.list_proc:
 *	* undo_list->lock for write
 *	* rcu for read
 *	use_global_lock:
 *	* global sem_lock() for write
 *	* either local or global sem_lock() for read.
 *
 * Memory ordering:
 * Most ordering is enforced by using spin_lock() and spin_unlock().
 * The special case is use_global_lock:
 * Setting it from non-zero to 0 is a RELEASE, this is ensured by
 * using smp_store_release().
 * Testing if it is non-zero is an ACQUIRE, this is ensured by using
 * smp_load_acquire().
 * Setting it from 0 to non-zero must be ordered with regards to
 * this smp_load_acquire(), this is guaranteed because the smp_load_acquire()
 * is inside a spin_lock() and after a write from 0 to non-zero a
 * spin_lock()+spin_unlock() is done.
 */

#define sc_semmsl	sem_ctls[0]
#define sc_semmns	sem_ctls[1]
#define sc_semopm	sem_ctls[2]
#define sc_semmni	sem_ctls[3]

int sem_init_ns(struct ipc_namespace *ns)
{
	ns->sc_semmsl = SEMMSL;
	ns->sc_semmns = SEMMNS;
	ns->sc_semopm = SEMOPM;
	ns->sc_semmni = SEMMNI;
	ns->used_sems = 0;
	return ipc_init_ids(&ns->ids[IPC_SEM_IDS]);
}

#ifdef CONFIG_IPC_NS
void sem_exit_ns(struct ipc_namespace *ns)
{
	free_ipcs(ns, &sem_ids(ns), freeary);
	idr_destroy(&ns->ids[IPC_SEM_IDS].ipcs_idr);
	rhashtable_destroy(&ns->ids[IPC_SEM_IDS].key_ht);
}
#endif

int __init sem_init(void)
{
	const int err = sem_init_ns(&init_ipc_ns);

	ipc_init_proc_interface("sysvipc/sem",
				"       key      semid perms      nsems   uid   gid  cuid  cgid      otime      ctime\n",
				IPC_SEM_IDS, sysvipc_sem_proc_show);
	return err;
}

/**
 * unmerge_queues - unmerge queues, if possible.
 * @sma: semaphore array
 *
 * The function unmerges the wait queues if complex_count is 0.
 * It must be called prior to dropping the global semaphore array lock.
 */
static void unmerge_queues(struct sem_array *sma)
{
	struct sem_queue *q, *tq;

	/* complex operations still around? */
	if (sma->complex_count)
		return;
	/*
	 * We will switch back to simple mode.
	 * Move all pending operation back into the per-semaphore
	 * queues.
	 */
	list_for_each_entry_safe(q, tq, &sma->pending_alter, list) {
		struct sem *curr;
		curr = &sma->sems[q->sops[0].sem_num];

		list_add_tail(&q->list, &curr->pending_alter);
	}
	INIT_LIST_HEAD(&sma->pending_alter);
}

/**
 * merge_queues - merge single semop queues into global queue
 * @sma: semaphore array
 *
 * This function merges all per-semaphore queues into the global queue.
 * It is necessary to achieve FIFO ordering for the pending single-sop
 * operations when a multi-semop operation must sleep.
 * Only the alter operations must be moved, the const operations can stay.
 */
static void merge_queues(struct sem_array *sma)
{
	int i;
	for (i = 0; i < sma->sem_nsems; i++) {
		struct sem *sem = &sma->sems[i];

		list_splice_init(&sem->pending_alter, &sma->pending_alter);
	}
}

static void sem_rcu_free(struct rcu_head *head)
{
	struct kern_ipc_perm *p = container_of(head, struct kern_ipc_perm, rcu);
	struct sem_array *sma = container_of(p, struct sem_array, sem_perm);

	security_sem_free(sma);
	kvfree(sma);
}

/*
 * Enter the mode suitable for non-simple operations:
 * Caller must own sem_perm.lock.
 */
static void complexmode_enter(struct sem_array *sma)
{
	int i;
	struct sem *sem;

	if (sma->use_global_lock > 0)  {
		/*
		 * We are already in global lock mode.
		 * Nothing to do, just reset the
		 * counter until we return to simple mode.
		 */
		sma->use_global_lock = USE_GLOBAL_LOCK_HYSTERESIS;
		return;
	}
	sma->use_global_lock = USE_GLOBAL_LOCK_HYSTERESIS;

	for (i = 0; i < sma->sem_nsems; i++) {
		sem = &sma->sems[i];
		spin_lock(&sem->lock);
		spin_unlock(&sem->lock);
	}
}

/*
 * Try to leave the mode that disallows simple operations:
 * Caller must own sem_perm.lock.
 */
static void complexmode_tryleave(struct sem_array *sma)
{
	if (sma->complex_count)  {
		/* Complex ops are sleeping.
		 * We must stay in complex mode
		 */
		return;
	}
	if (sma->use_global_lock == 1) {
		/*
		 * Immediately after setting use_global_lock to 0,
		 * a simple op can start. Thus: all memory writes
		 * performed by the current operation must be visible
		 * before we set use_global_lock to 0.
		 */
		smp_store_release(&sma->use_global_lock, 0);
	} else {
		sma->use_global_lock--;
	}
}

#define SEM_GLOBAL_LOCK	(-1)
/*
 * If the request contains only one semaphore operation, and there are
 * no complex transactions pending, lock only the semaphore involved.
 * Otherwise, lock the entire semaphore array, since we either have
 * multiple semaphores in our own semops, or we need to look at
 * semaphores from other pending complex operations.
 */
static inline int sem_lock(struct sem_array *sma, struct sembuf *sops,
			      int nsops)
{
	struct sem *sem;

	if (nsops != 1) {
		/* Complex operation - acquire a full lock */
		ipc_lock_object(&sma->sem_perm);

		/* Prevent parallel simple ops */
		complexmode_enter(sma);
		return SEM_GLOBAL_LOCK;
	}

	/*
	 * Only one semaphore affected - try to optimize locking.
	 * Optimized locking is possible if no complex operation
	 * is either enqueued or processed right now.
	 *
	 * Both facts are tracked by use_global_mode.
	 */
	sem = &sma->sems[sops->sem_num];

	/*
	 * Initial check for use_global_lock. Just an optimization,
	 * no locking, no memory barrier.
	 */
	if (!sma->use_global_lock) {
		/*
		 * It appears that no complex operation is around.
		 * Acquire the per-semaphore lock.
		 */
		spin_lock(&sem->lock);

		/* pairs with smp_store_release() */
		if (!smp_load_acquire(&sma->use_global_lock)) {
			/* fast path successful! */
			return sops->sem_num;
		}
		spin_unlock(&sem->lock);
	}

	/* slow path: acquire the full lock */
	ipc_lock_object(&sma->sem_perm);

	if (sma->use_global_lock == 0) {
		/*
		 * The use_global_lock mode ended while we waited for
		 * sma->sem_perm.lock. Thus we must switch to locking
		 * with sem->lock.
		 * Unlike in the fast path, there is no need to recheck
		 * sma->use_global_lock after we have acquired sem->lock:
		 * We own sma->sem_perm.lock, thus use_global_lock cannot
		 * change.
		 */
		spin_lock(&sem->lock);

		ipc_unlock_object(&sma->sem_perm);
		return sops->sem_num;
	} else {
		/*
		 * Not a false alarm, thus continue to use the global lock
		 * mode. No need for complexmode_enter(), this was done by
		 * the caller that has set use_global_mode to non-zero.
		 */
		return SEM_GLOBAL_LOCK;
	}
}

static inline void sem_unlock(struct sem_array *sma, int locknum)
{
	if (locknum == SEM_GLOBAL_LOCK) {
		unmerge_queues(sma);
		complexmode_tryleave(sma);
		ipc_unlock_object(&sma->sem_perm);
	} else {
		struct sem *sem = &sma->sems[locknum];
		spin_unlock(&sem->lock);
	}
}

/*
 * sem_lock_(check_) routines are called in the paths where the rwsem
 * is not held.
 *
 * The caller holds the RCU read lock.
 */
static inline struct sem_array *sem_obtain_object(struct ipc_namespace *ns, int id)
{
	struct kern_ipc_perm *ipcp = ipc_obtain_object_idr(&sem_ids(ns), id);

	if (IS_ERR(ipcp))
		return ERR_CAST(ipcp);

	return container_of(ipcp, struct sem_array, sem_perm);
}

static inline struct sem_array *sem_obtain_object_check(struct ipc_namespace *ns,
							int id)
{
	struct kern_ipc_perm *ipcp = ipc_obtain_object_check(&sem_ids(ns), id);

	if (IS_ERR(ipcp))
		return ERR_CAST(ipcp);

	return container_of(ipcp, struct sem_array, sem_perm);
}

static inline void sem_lock_and_putref(struct sem_array *sma)
{
	sem_lock(sma, NULL, -1);
	ipc_rcu_putref(&sma->sem_perm, sem_rcu_free);
}

static inline void sem_rmid(struct ipc_namespace *ns, struct sem_array *s)
{
	ipc_rmid(&sem_ids(ns), &s->sem_perm);
}

static struct sem_array *sem_alloc(size_t nsems)
{
	struct sem_array *sma;
	size_t size;

	if (nsems > (INT_MAX - sizeof(*sma)) / sizeof(sma->sems[0]))
		return NULL;

	size = sizeof(*sma) + nsems * sizeof(sma->sems[0]);
	sma = kvmalloc(size, GFP_KERNEL);
	if (unlikely(!sma))
		return NULL;

	memset(sma, 0, size);

	return sma;
}

/**
 * newary - Create a new semaphore set
 * @ns: namespace
 * @params: ptr to the structure that contains key, semflg and nsems
 *
 * Called with sem_ids.rwsem held (as a writer)
 */
static int newary(struct ipc_namespace *ns, struct ipc_params *params)
{
	int retval;
	struct sem_array *sma;
	key_t key = params->key;
	int nsems = params->u.nsems;
	int semflg = params->flg;
	int i;

	if (!nsems)
		return -EINVAL;
	if (ns->used_sems + nsems > ns->sc_semmns)
		return -ENOSPC;

	sma = sem_alloc(nsems);
	if (!sma)
		return -ENOMEM;

	sma->sem_perm.mode = (semflg & S_IRWXUGO);
	sma->sem_perm.key = key;

	sma->sem_perm.security = NULL;
	retval = security_sem_alloc(sma);
	if (retval) {
		kvfree(sma);
		return retval;
	}

	for (i = 0; i < nsems; i++) {
		INIT_LIST_HEAD(&sma->sems[i].pending_alter);
		INIT_LIST_HEAD(&sma->sems[i].pending_const);
		spin_lock_init(&sma->sems[i].lock);
	}

	sma->complex_count = 0;
	sma->use_global_lock = USE_GLOBAL_LOCK_HYSTERESIS;
	INIT_LIST_HEAD(&sma->pending_alter);
	INIT_LIST_HEAD(&sma->pending_const);
	INIT_LIST_HEAD(&sma->list_id);
	sma->sem_nsems = nsems;
	sma->sem_ctime = ktime_get_real_seconds();

	retval = ipc_addid(&sem_ids(ns), &sma->sem_perm, ns->sc_semmni);
	if (retval < 0) {
		call_rcu(&sma->sem_perm.rcu, sem_rcu_free);
		return retval;
	}
	ns->used_sems += nsems;

	sem_unlock(sma, -1);
	rcu_read_unlock();

	return sma->sem_perm.id;
}


/*
 * Called with sem_ids.rwsem and ipcp locked.
 */
static inline int sem_security(struct kern_ipc_perm *ipcp, int semflg)
{
	struct sem_array *sma;

	sma = container_of(ipcp, struct sem_array, sem_perm);
	return security_sem_associate(sma, semflg);
}

/*
 * Called with sem_ids.rwsem and ipcp locked.
 */
static inline int sem_more_checks(struct kern_ipc_perm *ipcp,
				struct ipc_params *params)
{
	struct sem_array *sma;

	sma = container_of(ipcp, struct sem_array, sem_perm);
	if (params->u.nsems > sma->sem_nsems)
		return -EINVAL;

	return 0;
}

SYSCALL_DEFINE3(semget, key_t, key, int, nsems, int, semflg)
{
	struct ipc_namespace *ns;
	static const struct ipc_ops sem_ops = {
		.getnew = newary,
		.associate = sem_security,
		.more_checks = sem_more_checks,
	};
	struct ipc_params sem_params;

	ns = current->nsproxy->ipc_ns;

	if (nsems < 0 || nsems > ns->sc_semmsl)
		return -EINVAL;

	sem_params.key = key;
	sem_params.flg = semflg;
	sem_params.u.nsems = nsems;

	return ipcget(ns, &sem_ids(ns), &sem_ops, &sem_params);
}

/**
 * perform_atomic_semop[_slow] - Attempt to perform semaphore
 *                               operations on a given array.
 * @sma: semaphore array
 * @q: struct sem_queue that describes the operation
 *
 * Caller blocking are as follows, based the value
 * indicated by the semaphore operation (sem_op):
 *
 *  (1) >0 never blocks.
 *  (2)  0 (wait-for-zero operation): semval is non-zero.
 *  (3) <0 attempting to decrement semval to a value smaller than zero.
 *
 * Returns 0 if the operation was possible.
 * Returns 1 if the operation is impossible, the caller must sleep.
 * Returns <0 for error codes.
 */
static int perform_atomic_semop_slow(struct sem_array *sma, struct sem_queue *q)
{
	int result, sem_op, nsops, pid;
	struct sembuf *sop;
	struct sem *curr;
	struct sembuf *sops;
	struct sem_undo *un;

	sops = q->sops;
	nsops = q->nsops;
	un = q->undo;

	for (sop = sops; sop < sops + nsops; sop++) {
		curr = &sma->sems[sop->sem_num];
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
			/* Exceeding the undo range is an error. */
			if (undo < (-SEMAEM - 1) || undo > SEMAEM)
				goto out_of_range;
			un->semadj[sop->sem_num] = undo;
		}

		curr->semval = result;
	}

	sop--;
	pid = q->pid;
	while (sop >= sops) {
		sma->sems[sop->sem_num].sempid = pid;
		sop--;
	}

	return 0;

out_of_range:
	result = -ERANGE;
	goto undo;

would_block:
	q->blocking = sop;

	if (sop->sem_flg & IPC_NOWAIT)
		result = -EAGAIN;
	else
		result = 1;

undo:
	sop--;
	while (sop >= sops) {
		sem_op = sop->sem_op;
		sma->sems[sop->sem_num].semval -= sem_op;
		if (sop->sem_flg & SEM_UNDO)
			un->semadj[sop->sem_num] += sem_op;
		sop--;
	}

	return result;
}

static int perform_atomic_semop(struct sem_array *sma, struct sem_queue *q)
{
	int result, sem_op, nsops;
	struct sembuf *sop;
	struct sem *curr;
	struct sembuf *sops;
	struct sem_undo *un;

	sops = q->sops;
	nsops = q->nsops;
	un = q->undo;

	if (unlikely(q->dupsop))
		return perform_atomic_semop_slow(sma, q);

	/*
	 * We scan the semaphore set twice, first to ensure that the entire
	 * operation can succeed, therefore avoiding any pointless writes
	 * to shared memory and having to undo such changes in order to block
	 * until the operations can go through.
	 */
	for (sop = sops; sop < sops + nsops; sop++) {
		curr = &sma->sems[sop->sem_num];
		sem_op = sop->sem_op;
		result = curr->semval;

		if (!sem_op && result)
			goto would_block; /* wait-for-zero */

		result += sem_op;
		if (result < 0)
			goto would_block;

		if (result > SEMVMX)
			return -ERANGE;

		if (sop->sem_flg & SEM_UNDO) {
			int undo = un->semadj[sop->sem_num] - sem_op;

			/* Exceeding the undo range is an error. */
			if (undo < (-SEMAEM - 1) || undo > SEMAEM)
				return -ERANGE;
		}
	}

	for (sop = sops; sop < sops + nsops; sop++) {
		curr = &sma->sems[sop->sem_num];
		sem_op = sop->sem_op;
		result = curr->semval;

		if (sop->sem_flg & SEM_UNDO) {
			int undo = un->semadj[sop->sem_num] - sem_op;

			un->semadj[sop->sem_num] = undo;
		}
		curr->semval += sem_op;
		curr->sempid = q->pid;
	}

	return 0;

would_block:
	q->blocking = sop;
	return sop->sem_flg & IPC_NOWAIT ? -EAGAIN : 1;
}

static inline void wake_up_sem_queue_prepare(struct sem_queue *q, int error,
					     struct wake_q_head *wake_q)
{
	wake_q_add(wake_q, q->sleeper);
	/*
	 * Rely on the above implicit barrier, such that we can
	 * ensure that we hold reference to the task before setting
	 * q->status. Otherwise we could race with do_exit if the
	 * task is awoken by an external event before calling
	 * wake_up_process().
	 */
	WRITE_ONCE(q->status, error);
}

static void unlink_queue(struct sem_array *sma, struct sem_queue *q)
{
	list_del(&q->list);
	if (q->nsops > 1)
		sma->complex_count--;
}

/** check_restart(sma, q)
 * @sma: semaphore array
 * @q: the operation that just completed
 *
 * update_queue is O(N^2) when it restarts scanning the whole queue of
 * waiting operations. Therefore this function checks if the restart is
 * really necessary. It is called after a previously waiting operation
 * modified the array.
 * Note that wait-for-zero operations are handled without restart.
 */
static inline int check_restart(struct sem_array *sma, struct sem_queue *q)
{
	/* pending complex alter operations are too difficult to analyse */
	if (!list_empty(&sma->pending_alter))
		return 1;

	/* we were a sleeping complex operation. Too difficult */
	if (q->nsops > 1)
		return 1;

	/* It is impossible that someone waits for the new value:
	 * - complex operations always restart.
	 * - wait-for-zero are handled seperately.
	 * - q is a previously sleeping simple operation that
	 *   altered the array. It must be a decrement, because
	 *   simple increments never sleep.
	 * - If there are older (higher priority) decrements
	 *   in the queue, then they have observed the original
	 *   semval value and couldn't proceed. The operation
	 *   decremented to value - thus they won't proceed either.
	 */
	return 0;
}

/**
 * wake_const_ops - wake up non-alter tasks
 * @sma: semaphore array.
 * @semnum: semaphore that was modified.
 * @wake_q: lockless wake-queue head.
 *
 * wake_const_ops must be called after a semaphore in a semaphore array
 * was set to 0. If complex const operations are pending, wake_const_ops must
 * be called with semnum = -1, as well as with the number of each modified
 * semaphore.
 * The tasks that must be woken up are added to @wake_q. The return code
 * is stored in q->pid.
 * The function returns 1 if at least one operation was completed successfully.
 */
static int wake_const_ops(struct sem_array *sma, int semnum,
			  struct wake_q_head *wake_q)
{
	struct sem_queue *q, *tmp;
	struct list_head *pending_list;
	int semop_completed = 0;

	if (semnum == -1)
		pending_list = &sma->pending_const;
	else
		pending_list = &sma->sems[semnum].pending_const;

	list_for_each_entry_safe(q, tmp, pending_list, list) {
		int error = perform_atomic_semop(sma, q);

		if (error > 0)
			continue;
		/* operation completed, remove from queue & wakeup */
		unlink_queue(sma, q);

		wake_up_sem_queue_prepare(q, error, wake_q);
		if (error == 0)
			semop_completed = 1;
	}

	return semop_completed;
}

/**
 * do_smart_wakeup_zero - wakeup all wait for zero tasks
 * @sma: semaphore array
 * @sops: operations that were performed
 * @nsops: number of operations
 * @wake_q: lockless wake-queue head
 *
 * Checks all required queue for wait-for-zero operations, based
 * on the actual changes that were performed on the semaphore array.
 * The function returns 1 if at least one operation was completed successfully.
 */
static int do_smart_wakeup_zero(struct sem_array *sma, struct sembuf *sops,
				int nsops, struct wake_q_head *wake_q)
{
	int i;
	int semop_completed = 0;
	int got_zero = 0;

	/* first: the per-semaphore queues, if known */
	if (sops) {
		for (i = 0; i < nsops; i++) {
			int num = sops[i].sem_num;

			if (sma->sems[num].semval == 0) {
				got_zero = 1;
				semop_completed |= wake_const_ops(sma, num, wake_q);
			}
		}
	} else {
		/*
		 * No sops means modified semaphores not known.
		 * Assume all were changed.
		 */
		for (i = 0; i < sma->sem_nsems; i++) {
			if (sma->sems[i].semval == 0) {
				got_zero = 1;
				semop_completed |= wake_const_ops(sma, i, wake_q);
			}
		}
	}
	/*
	 * If one of the modified semaphores got 0,
	 * then check the global queue, too.
	 */
	if (got_zero)
		semop_completed |= wake_const_ops(sma, -1, wake_q);

	return semop_completed;
}


/**
 * update_queue - look for tasks that can be completed.
 * @sma: semaphore array.
 * @semnum: semaphore that was modified.
 * @wake_q: lockless wake-queue head.
 *
 * update_queue must be called after a semaphore in a semaphore array
 * was modified. If multiple semaphores were modified, update_queue must
 * be called with semnum = -1, as well as with the number of each modified
 * semaphore.
 * The tasks that must be woken up are added to @wake_q. The return code
 * is stored in q->pid.
 * The function internally checks if const operations can now succeed.
 *
 * The function return 1 if at least one semop was completed successfully.
 */
static int update_queue(struct sem_array *sma, int semnum, struct wake_q_head *wake_q)
{
	struct sem_queue *q, *tmp;
	struct list_head *pending_list;
	int semop_completed = 0;

	if (semnum == -1)
		pending_list = &sma->pending_alter;
	else
		pending_list = &sma->sems[semnum].pending_alter;

again:
	list_for_each_entry_safe(q, tmp, pending_list, list) {
		int error, restart;

		/* If we are scanning the single sop, per-semaphore list of
		 * one semaphore and that semaphore is 0, then it is not
		 * necessary to scan further: simple increments
		 * that affect only one entry succeed immediately and cannot
		 * be in the  per semaphore pending queue, and decrements
		 * cannot be successful if the value is already 0.
		 */
		if (semnum != -1 && sma->sems[semnum].semval == 0)
			break;

		error = perform_atomic_semop(sma, q);

		/* Does q->sleeper still need to sleep? */
		if (error > 0)
			continue;

		unlink_queue(sma, q);

		if (error) {
			restart = 0;
		} else {
			semop_completed = 1;
			do_smart_wakeup_zero(sma, q->sops, q->nsops, wake_q);
			restart = check_restart(sma, q);
		}

		wake_up_sem_queue_prepare(q, error, wake_q);
		if (restart)
			goto again;
	}
	return semop_completed;
}

/**
 * set_semotime - set sem_otime
 * @sma: semaphore array
 * @sops: operations that modified the array, may be NULL
 *
 * sem_otime is replicated to avoid cache line trashing.
 * This function sets one instance to the current time.
 */
static void set_semotime(struct sem_array *sma, struct sembuf *sops)
{
	if (sops == NULL) {
		sma->sems[0].sem_otime = get_seconds();
	} else {
		sma->sems[sops[0].sem_num].sem_otime =
							get_seconds();
	}
}

/**
 * do_smart_update - optimized update_queue
 * @sma: semaphore array
 * @sops: operations that were performed
 * @nsops: number of operations
 * @otime: force setting otime
 * @wake_q: lockless wake-queue head
 *
 * do_smart_update() does the required calls to update_queue and wakeup_zero,
 * based on the actual changes that were performed on the semaphore array.
 * Note that the function does not do the actual wake-up: the caller is
 * responsible for calling wake_up_q().
 * It is safe to perform this call after dropping all locks.
 */
static void do_smart_update(struct sem_array *sma, struct sembuf *sops, int nsops,
			    int otime, struct wake_q_head *wake_q)
{
	int i;

	otime |= do_smart_wakeup_zero(sma, sops, nsops, wake_q);

	if (!list_empty(&sma->pending_alter)) {
		/* semaphore array uses the global queue - just process it. */
		otime |= update_queue(sma, -1, wake_q);
	} else {
		if (!sops) {
			/*
			 * No sops, thus the modified semaphores are not
			 * known. Check all.
			 */
			for (i = 0; i < sma->sem_nsems; i++)
				otime |= update_queue(sma, i, wake_q);
		} else {
			/*
			 * Check the semaphores that were increased:
			 * - No complex ops, thus all sleeping ops are
			 *   decrease.
			 * - if we decreased the value, then any sleeping
			 *   semaphore ops wont be able to run: If the
			 *   previous value was too small, then the new
			 *   value will be too small, too.
			 */
			for (i = 0; i < nsops; i++) {
				if (sops[i].sem_op > 0) {
					otime |= update_queue(sma,
							      sops[i].sem_num, wake_q);
				}
			}
		}
	}
	if (otime)
		set_semotime(sma, sops);
}

/*
 * check_qop: Test if a queued operation sleeps on the semaphore semnum
 */
static int check_qop(struct sem_array *sma, int semnum, struct sem_queue *q,
			bool count_zero)
{
	struct sembuf *sop = q->blocking;

	/*
	 * Linux always (since 0.99.10) reported a task as sleeping on all
	 * semaphores. This violates SUS, therefore it was changed to the
	 * standard compliant behavior.
	 * Give the administrators a chance to notice that an application
	 * might misbehave because it relies on the Linux behavior.
	 */
	pr_info_once("semctl(GETNCNT/GETZCNT) is since 3.16 Single Unix Specification compliant.\n"
			"The task %s (%d) triggered the difference, watch for misbehavior.\n",
			current->comm, task_pid_nr(current));

	if (sop->sem_num != semnum)
		return 0;

	if (count_zero && sop->sem_op == 0)
		return 1;
	if (!count_zero && sop->sem_op < 0)
		return 1;

	return 0;
}

/* The following counts are associated to each semaphore:
 *   semncnt        number of tasks waiting on semval being nonzero
 *   semzcnt        number of tasks waiting on semval being zero
 *
 * Per definition, a task waits only on the semaphore of the first semop
 * that cannot proceed, even if additional operation would block, too.
 */
static int count_semcnt(struct sem_array *sma, ushort semnum,
			bool count_zero)
{
	struct list_head *l;
	struct sem_queue *q;
	int semcnt;

	semcnt = 0;
	/* First: check the simple operations. They are easy to evaluate */
	if (count_zero)
		l = &sma->sems[semnum].pending_const;
	else
		l = &sma->sems[semnum].pending_alter;

	list_for_each_entry(q, l, list) {
		/* all task on a per-semaphore list sleep on exactly
		 * that semaphore
		 */
		semcnt++;
	}

	/* Then: check the complex operations. */
	list_for_each_entry(q, &sma->pending_alter, list) {
		semcnt += check_qop(sma, semnum, q, count_zero);
	}
	if (count_zero) {
		list_for_each_entry(q, &sma->pending_const, list) {
			semcnt += check_qop(sma, semnum, q, count_zero);
		}
	}
	return semcnt;
}

/* Free a semaphore set. freeary() is called with sem_ids.rwsem locked
 * as a writer and the spinlock for this semaphore set hold. sem_ids.rwsem
 * remains locked on exit.
 */
static void freeary(struct ipc_namespace *ns, struct kern_ipc_perm *ipcp)
{
	struct sem_undo *un, *tu;
	struct sem_queue *q, *tq;
	struct sem_array *sma = container_of(ipcp, struct sem_array, sem_perm);
	int i;
	DEFINE_WAKE_Q(wake_q);

	/* Free the existing undo structures for this semaphore set.  */
	ipc_assert_locked_object(&sma->sem_perm);
	list_for_each_entry_safe(un, tu, &sma->list_id, list_id) {
		list_del(&un->list_id);
		spin_lock(&un->ulp->lock);
		un->semid = -1;
		list_del_rcu(&un->list_proc);
		spin_unlock(&un->ulp->lock);
		kfree_rcu(un, rcu);
	}

	/* Wake up all pending processes and let them fail with EIDRM. */
	list_for_each_entry_safe(q, tq, &sma->pending_const, list) {
		unlink_queue(sma, q);
		wake_up_sem_queue_prepare(q, -EIDRM, &wake_q);
	}

	list_for_each_entry_safe(q, tq, &sma->pending_alter, list) {
		unlink_queue(sma, q);
		wake_up_sem_queue_prepare(q, -EIDRM, &wake_q);
	}
	for (i = 0; i < sma->sem_nsems; i++) {
		struct sem *sem = &sma->sems[i];
		list_for_each_entry_safe(q, tq, &sem->pending_const, list) {
			unlink_queue(sma, q);
			wake_up_sem_queue_prepare(q, -EIDRM, &wake_q);
		}
		list_for_each_entry_safe(q, tq, &sem->pending_alter, list) {
			unlink_queue(sma, q);
			wake_up_sem_queue_prepare(q, -EIDRM, &wake_q);
		}
	}

	/* Remove the semaphore set from the IDR */
	sem_rmid(ns, sma);
	sem_unlock(sma, -1);
	rcu_read_unlock();

	wake_up_q(&wake_q);
	ns->used_sems -= sma->sem_nsems;
	ipc_rcu_putref(&sma->sem_perm, sem_rcu_free);
}

static unsigned long copy_semid_to_user(void __user *buf, struct semid64_ds *in, int version)
{
	switch (version) {
	case IPC_64:
		return copy_to_user(buf, in, sizeof(*in));
	case IPC_OLD:
	    {
		struct semid_ds out;

		memset(&out, 0, sizeof(out));

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

static time64_t get_semotime(struct sem_array *sma)
{
	int i;
	time64_t res;

	res = sma->sems[0].sem_otime;
	for (i = 1; i < sma->sem_nsems; i++) {
		time64_t to = sma->sems[i].sem_otime;

		if (to > res)
			res = to;
	}
	return res;
}

static int semctl_stat(struct ipc_namespace *ns, int semid,
			 int cmd, struct semid64_ds *semid64)
{
	struct sem_array *sma;
	int id = 0;
	int err;

	memset(semid64, 0, sizeof(*semid64));

	rcu_read_lock();
	if (cmd == SEM_STAT) {
		sma = sem_obtain_object(ns, semid);
		if (IS_ERR(sma)) {
			err = PTR_ERR(sma);
			goto out_unlock;
		}
		id = sma->sem_perm.id;
	} else {
		sma = sem_obtain_object_check(ns, semid);
		if (IS_ERR(sma)) {
			err = PTR_ERR(sma);
			goto out_unlock;
		}
	}

	err = -EACCES;
	if (ipcperms(ns, &sma->sem_perm, S_IRUGO))
		goto out_unlock;

	err = security_sem_semctl(sma, cmd);
	if (err)
		goto out_unlock;

	kernel_to_ipc64_perm(&sma->sem_perm, &semid64->sem_perm);
	semid64->sem_otime = get_semotime(sma);
	semid64->sem_ctime = sma->sem_ctime;
	semid64->sem_nsems = sma->sem_nsems;
	rcu_read_unlock();
	return id;

out_unlock:
	rcu_read_unlock();
	return err;
}

static int semctl_info(struct ipc_namespace *ns, int semid,
			 int cmd, void __user *p)
{
	struct seminfo seminfo;
	int max_id;
	int err;

	err = security_sem_semctl(NULL, cmd);
	if (err)
		return err;

	memset(&seminfo, 0, sizeof(seminfo));
	seminfo.semmni = ns->sc_semmni;
	seminfo.semmns = ns->sc_semmns;
	seminfo.semmsl = ns->sc_semmsl;
	seminfo.semopm = ns->sc_semopm;
	seminfo.semvmx = SEMVMX;
	seminfo.semmnu = SEMMNU;
	seminfo.semmap = SEMMAP;
	seminfo.semume = SEMUME;
	down_read(&sem_ids(ns).rwsem);
	if (cmd == SEM_INFO) {
		seminfo.semusz = sem_ids(ns).in_use;
		seminfo.semaem = ns->used_sems;
	} else {
		seminfo.semusz = SEMUSZ;
		seminfo.semaem = SEMAEM;
	}
	max_id = ipc_get_maxid(&sem_ids(ns));
	up_read(&sem_ids(ns).rwsem);
	if (copy_to_user(p, &seminfo, sizeof(struct seminfo)))
		return -EFAULT;
	return (max_id < 0) ? 0 : max_id;
}

static int semctl_setval(struct ipc_namespace *ns, int semid, int semnum,
		int val)
{
	struct sem_undo *un;
	struct sem_array *sma;
	struct sem *curr;
	int err;
	DEFINE_WAKE_Q(wake_q);

	if (val > SEMVMX || val < 0)
		return -ERANGE;

	rcu_read_lock();
	sma = sem_obtain_object_check(ns, semid);
	if (IS_ERR(sma)) {
		rcu_read_unlock();
		return PTR_ERR(sma);
	}

	if (semnum < 0 || semnum >= sma->sem_nsems) {
		rcu_read_unlock();
		return -EINVAL;
	}


	if (ipcperms(ns, &sma->sem_perm, S_IWUGO)) {
		rcu_read_unlock();
		return -EACCES;
	}

	err = security_sem_semctl(sma, SETVAL);
	if (err) {
		rcu_read_unlock();
		return -EACCES;
	}

	sem_lock(sma, NULL, -1);

	if (!ipc_valid_object(&sma->sem_perm)) {
		sem_unlock(sma, -1);
		rcu_read_unlock();
		return -EIDRM;
	}

	curr = &sma->sems[semnum];

	ipc_assert_locked_object(&sma->sem_perm);
	list_for_each_entry(un, &sma->list_id, list_id)
		un->semadj[semnum] = 0;

	curr->semval = val;
	curr->sempid = task_tgid_vnr(current);
	sma->sem_ctime = ktime_get_real_seconds();
	/* maybe some queued-up processes were waiting for this */
	do_smart_update(sma, NULL, 0, 0, &wake_q);
	sem_unlock(sma, -1);
	rcu_read_unlock();
	wake_up_q(&wake_q);
	return 0;
}

static int semctl_main(struct ipc_namespace *ns, int semid, int semnum,
		int cmd, void __user *p)
{
	struct sem_array *sma;
	struct sem *curr;
	int err, nsems;
	ushort fast_sem_io[SEMMSL_FAST];
	ushort *sem_io = fast_sem_io;
	DEFINE_WAKE_Q(wake_q);

	rcu_read_lock();
	sma = sem_obtain_object_check(ns, semid);
	if (IS_ERR(sma)) {
		rcu_read_unlock();
		return PTR_ERR(sma);
	}

	nsems = sma->sem_nsems;

	err = -EACCES;
	if (ipcperms(ns, &sma->sem_perm, cmd == SETALL ? S_IWUGO : S_IRUGO))
		goto out_rcu_wakeup;

	err = security_sem_semctl(sma, cmd);
	if (err)
		goto out_rcu_wakeup;

	err = -EACCES;
	switch (cmd) {
	case GETALL:
	{
		ushort __user *array = p;
		int i;

		sem_lock(sma, NULL, -1);
		if (!ipc_valid_object(&sma->sem_perm)) {
			err = -EIDRM;
			goto out_unlock;
		}
		if (nsems > SEMMSL_FAST) {
			if (!ipc_rcu_getref(&sma->sem_perm)) {
				err = -EIDRM;
				goto out_unlock;
			}
			sem_unlock(sma, -1);
			rcu_read_unlock();
			sem_io = kvmalloc_array(nsems, sizeof(ushort),
						GFP_KERNEL);
			if (sem_io == NULL) {
				ipc_rcu_putref(&sma->sem_perm, sem_rcu_free);
				return -ENOMEM;
			}

			rcu_read_lock();
			sem_lock_and_putref(sma);
			if (!ipc_valid_object(&sma->sem_perm)) {
				err = -EIDRM;
				goto out_unlock;
			}
		}
		for (i = 0; i < sma->sem_nsems; i++)
			sem_io[i] = sma->sems[i].semval;
		sem_unlock(sma, -1);
		rcu_read_unlock();
		err = 0;
		if (copy_to_user(array, sem_io, nsems*sizeof(ushort)))
			err = -EFAULT;
		goto out_free;
	}
	case SETALL:
	{
		int i;
		struct sem_undo *un;

		if (!ipc_rcu_getref(&sma->sem_perm)) {
			err = -EIDRM;
			goto out_rcu_wakeup;
		}
		rcu_read_unlock();

		if (nsems > SEMMSL_FAST) {
			sem_io = kvmalloc_array(nsems, sizeof(ushort),
						GFP_KERNEL);
			if (sem_io == NULL) {
				ipc_rcu_putref(&sma->sem_perm, sem_rcu_free);
				return -ENOMEM;
			}
		}

		if (copy_from_user(sem_io, p, nsems*sizeof(ushort))) {
			ipc_rcu_putref(&sma->sem_perm, sem_rcu_free);
			err = -EFAULT;
			goto out_free;
		}

		for (i = 0; i < nsems; i++) {
			if (sem_io[i] > SEMVMX) {
				ipc_rcu_putref(&sma->sem_perm, sem_rcu_free);
				err = -ERANGE;
				goto out_free;
			}
		}
		rcu_read_lock();
		sem_lock_and_putref(sma);
		if (!ipc_valid_object(&sma->sem_perm)) {
			err = -EIDRM;
			goto out_unlock;
		}

		for (i = 0; i < nsems; i++) {
			sma->sems[i].semval = sem_io[i];
			sma->sems[i].sempid = task_tgid_vnr(current);
		}

		ipc_assert_locked_object(&sma->sem_perm);
		list_for_each_entry(un, &sma->list_id, list_id) {
			for (i = 0; i < nsems; i++)
				un->semadj[i] = 0;
		}
		sma->sem_ctime = ktime_get_real_seconds();
		/* maybe some queued-up processes were waiting for this */
		do_smart_update(sma, NULL, 0, 0, &wake_q);
		err = 0;
		goto out_unlock;
	}
	/* GETVAL, GETPID, GETNCTN, GETZCNT: fall-through */
	}
	err = -EINVAL;
	if (semnum < 0 || semnum >= nsems)
		goto out_rcu_wakeup;

	sem_lock(sma, NULL, -1);
	if (!ipc_valid_object(&sma->sem_perm)) {
		err = -EIDRM;
		goto out_unlock;
	}
	curr = &sma->sems[semnum];

	switch (cmd) {
	case GETVAL:
		err = curr->semval;
		goto out_unlock;
	case GETPID:
		err = curr->sempid;
		goto out_unlock;
	case GETNCNT:
		err = count_semcnt(sma, semnum, 0);
		goto out_unlock;
	case GETZCNT:
		err = count_semcnt(sma, semnum, 1);
		goto out_unlock;
	}

out_unlock:
	sem_unlock(sma, -1);
out_rcu_wakeup:
	rcu_read_unlock();
	wake_up_q(&wake_q);
out_free:
	if (sem_io != fast_sem_io)
		kvfree(sem_io);
	return err;
}

static inline unsigned long
copy_semid_from_user(struct semid64_ds *out, void __user *buf, int version)
{
	switch (version) {
	case IPC_64:
		if (copy_from_user(out, buf, sizeof(*out)))
			return -EFAULT;
		return 0;
	case IPC_OLD:
	    {
		struct semid_ds tbuf_old;

		if (copy_from_user(&tbuf_old, buf, sizeof(tbuf_old)))
			return -EFAULT;

		out->sem_perm.uid	= tbuf_old.sem_perm.uid;
		out->sem_perm.gid	= tbuf_old.sem_perm.gid;
		out->sem_perm.mode	= tbuf_old.sem_perm.mode;

		return 0;
	    }
	default:
		return -EINVAL;
	}
}

/*
 * This function handles some semctl commands which require the rwsem
 * to be held in write mode.
 * NOTE: no locks must be held, the rwsem is taken inside this function.
 */
static int semctl_down(struct ipc_namespace *ns, int semid,
		       int cmd, struct semid64_ds *semid64)
{
	struct sem_array *sma;
	int err;
	struct kern_ipc_perm *ipcp;

	down_write(&sem_ids(ns).rwsem);
	rcu_read_lock();

	ipcp = ipcctl_pre_down_nolock(ns, &sem_ids(ns), semid, cmd,
				      &semid64->sem_perm, 0);
	if (IS_ERR(ipcp)) {
		err = PTR_ERR(ipcp);
		goto out_unlock1;
	}

	sma = container_of(ipcp, struct sem_array, sem_perm);

	err = security_sem_semctl(sma, cmd);
	if (err)
		goto out_unlock1;

	switch (cmd) {
	case IPC_RMID:
		sem_lock(sma, NULL, -1);
		/* freeary unlocks the ipc object and rcu */
		freeary(ns, ipcp);
		goto out_up;
	case IPC_SET:
		sem_lock(sma, NULL, -1);
		err = ipc_update_perm(&semid64->sem_perm, ipcp);
		if (err)
			goto out_unlock0;
		sma->sem_ctime = ktime_get_real_seconds();
		break;
	default:
		err = -EINVAL;
		goto out_unlock1;
	}

out_unlock0:
	sem_unlock(sma, -1);
out_unlock1:
	rcu_read_unlock();
out_up:
	up_write(&sem_ids(ns).rwsem);
	return err;
}

SYSCALL_DEFINE4(semctl, int, semid, int, semnum, int, cmd, unsigned long, arg)
{
	int version;
	struct ipc_namespace *ns;
	void __user *p = (void __user *)arg;
	struct semid64_ds semid64;
	int err;

	if (semid < 0)
		return -EINVAL;

	version = ipc_parse_version(&cmd);
	ns = current->nsproxy->ipc_ns;

	switch (cmd) {
	case IPC_INFO:
	case SEM_INFO:
		return semctl_info(ns, semid, cmd, p);
	case IPC_STAT:
	case SEM_STAT:
		err = semctl_stat(ns, semid, cmd, &semid64);
		if (err < 0)
			return err;
		if (copy_semid_to_user(p, &semid64, version))
			err = -EFAULT;
		return err;
	case GETALL:
	case GETVAL:
	case GETPID:
	case GETNCNT:
	case GETZCNT:
	case SETALL:
		return semctl_main(ns, semid, semnum, cmd, p);
	case SETVAL: {
		int val;
#if defined(CONFIG_64BIT) && defined(__BIG_ENDIAN)
		/* big-endian 64bit */
		val = arg >> 32;
#else
		/* 32bit or little-endian 64bit */
		val = arg;
#endif
		return semctl_setval(ns, semid, semnum, val);
	}
	case IPC_SET:
		if (copy_semid_from_user(&semid64, p, version))
			return -EFAULT;
	case IPC_RMID:
		return semctl_down(ns, semid, cmd, &semid64);
	default:
		return -EINVAL;
	}
}

#ifdef CONFIG_COMPAT

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

static int copy_compat_semid_from_user(struct semid64_ds *out, void __user *buf,
					int version)
{
	memset(out, 0, sizeof(*out));
	if (version == IPC_64) {
		struct compat_semid64_ds *p = buf;
		return get_compat_ipc64_perm(&out->sem_perm, &p->sem_perm);
	} else {
		struct compat_semid_ds *p = buf;
		return get_compat_ipc_perm(&out->sem_perm, &p->sem_perm);
	}
}

static int copy_compat_semid_to_user(void __user *buf, struct semid64_ds *in,
					int version)
{
	if (version == IPC_64) {
		struct compat_semid64_ds v;
		memset(&v, 0, sizeof(v));
		to_compat_ipc64_perm(&v.sem_perm, &in->sem_perm);
		v.sem_otime = in->sem_otime;
		v.sem_ctime = in->sem_ctime;
		v.sem_nsems = in->sem_nsems;
		return copy_to_user(buf, &v, sizeof(v));
	} else {
		struct compat_semid_ds v;
		memset(&v, 0, sizeof(v));
		to_compat_ipc_perm(&v.sem_perm, &in->sem_perm);
		v.sem_otime = in->sem_otime;
		v.sem_ctime = in->sem_ctime;
		v.sem_nsems = in->sem_nsems;
		return copy_to_user(buf, &v, sizeof(v));
	}
}

COMPAT_SYSCALL_DEFINE4(semctl, int, semid, int, semnum, int, cmd, int, arg)
{
	void __user *p = compat_ptr(arg);
	struct ipc_namespace *ns;
	struct semid64_ds semid64;
	int version = compat_ipc_parse_version(&cmd);
	int err;

	ns = current->nsproxy->ipc_ns;

	if (semid < 0)
		return -EINVAL;

	switch (cmd & (~IPC_64)) {
	case IPC_INFO:
	case SEM_INFO:
		return semctl_info(ns, semid, cmd, p);
	case IPC_STAT:
	case SEM_STAT:
		err = semctl_stat(ns, semid, cmd, &semid64);
		if (err < 0)
			return err;
		if (copy_compat_semid_to_user(p, &semid64, version))
			err = -EFAULT;
		return err;
	case GETVAL:
	case GETPID:
	case GETNCNT:
	case GETZCNT:
	case GETALL:
	case SETALL:
		return semctl_main(ns, semid, semnum, cmd, p);
	case SETVAL:
		return semctl_setval(ns, semid, semnum, arg);
	case IPC_SET:
		if (copy_compat_semid_from_user(&semid64, p, version))
			return -EFAULT;
		/* fallthru */
	case IPC_RMID:
		return semctl_down(ns, semid, cmd, &semid64);
	default:
		return -EINVAL;
	}
}
#endif

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
		refcount_set(&undo_list->refcnt, 1);
		INIT_LIST_HEAD(&undo_list->list_proc);

		current->sysvsem.undo_list = undo_list;
	}
	*undo_listp = undo_list;
	return 0;
}

static struct sem_undo *__lookup_undo(struct sem_undo_list *ulp, int semid)
{
	struct sem_undo *un;

	list_for_each_entry_rcu(un, &ulp->list_proc, list_proc) {
		if (un->semid == semid)
			return un;
	}
	return NULL;
}

static struct sem_undo *lookup_undo(struct sem_undo_list *ulp, int semid)
{
	struct sem_undo *un;

	assert_spin_locked(&ulp->lock);

	un = __lookup_undo(ulp, semid);
	if (un) {
		list_del_rcu(&un->list_proc);
		list_add_rcu(&un->list_proc, &ulp->list_proc);
	}
	return un;
}

/**
 * find_alloc_undo - lookup (and if not present create) undo array
 * @ns: namespace
 * @semid: semaphore array id
 *
 * The function looks up (and if not present creates) the undo structure.
 * The size of the undo structure depends on the size of the semaphore
 * array, thus the alloc path is not that straightforward.
 * Lifetime-rules: sem_undo is rcu-protected, on success, the function
 * performs a rcu_read_lock().
 */
static struct sem_undo *find_alloc_undo(struct ipc_namespace *ns, int semid)
{
	struct sem_array *sma;
	struct sem_undo_list *ulp;
	struct sem_undo *un, *new;
	int nsems, error;

	error = get_undo_list(&ulp);
	if (error)
		return ERR_PTR(error);

	rcu_read_lock();
	spin_lock(&ulp->lock);
	un = lookup_undo(ulp, semid);
	spin_unlock(&ulp->lock);
	if (likely(un != NULL))
		goto out;

	/* no undo structure around - allocate one. */
	/* step 1: figure out the size of the semaphore array */
	sma = sem_obtain_object_check(ns, semid);
	if (IS_ERR(sma)) {
		rcu_read_unlock();
		return ERR_CAST(sma);
	}

	nsems = sma->sem_nsems;
	if (!ipc_rcu_getref(&sma->sem_perm)) {
		rcu_read_unlock();
		un = ERR_PTR(-EIDRM);
		goto out;
	}
	rcu_read_unlock();

	/* step 2: allocate new undo structure */
	new = kzalloc(sizeof(struct sem_undo) + sizeof(short)*nsems, GFP_KERNEL);
	if (!new) {
		ipc_rcu_putref(&sma->sem_perm, sem_rcu_free);
		return ERR_PTR(-ENOMEM);
	}

	/* step 3: Acquire the lock on semaphore array */
	rcu_read_lock();
	sem_lock_and_putref(sma);
	if (!ipc_valid_object(&sma->sem_perm)) {
		sem_unlock(sma, -1);
		rcu_read_unlock();
		kfree(new);
		un = ERR_PTR(-EIDRM);
		goto out;
	}
	spin_lock(&ulp->lock);

	/*
	 * step 4: check for races: did someone else allocate the undo struct?
	 */
	un = lookup_undo(ulp, semid);
	if (un) {
		kfree(new);
		goto success;
	}
	/* step 5: initialize & link new undo structure */
	new->semadj = (short *) &new[1];
	new->ulp = ulp;
	new->semid = semid;
	assert_spin_locked(&ulp->lock);
	list_add_rcu(&new->list_proc, &ulp->list_proc);
	ipc_assert_locked_object(&sma->sem_perm);
	list_add(&new->list_id, &sma->list_id);
	un = new;

success:
	spin_unlock(&ulp->lock);
	sem_unlock(sma, -1);
out:
	return un;
}

static long do_semtimedop(int semid, struct sembuf __user *tsops,
		unsigned nsops, const struct timespec64 *timeout)
{
	int error = -EINVAL;
	struct sem_array *sma;
	struct sembuf fast_sops[SEMOPM_FAST];
	struct sembuf *sops = fast_sops, *sop;
	struct sem_undo *un;
	int max, locknum;
	bool undos = false, alter = false, dupsop = false;
	struct sem_queue queue;
	unsigned long dup = 0, jiffies_left = 0;
	struct ipc_namespace *ns;

	ns = current->nsproxy->ipc_ns;

	if (nsops < 1 || semid < 0)
		return -EINVAL;
	if (nsops > ns->sc_semopm)
		return -E2BIG;
	if (nsops > SEMOPM_FAST) {
		sops = kvmalloc(sizeof(*sops)*nsops, GFP_KERNEL);
		if (sops == NULL)
			return -ENOMEM;
	}

	if (copy_from_user(sops, tsops, nsops * sizeof(*tsops))) {
		error =  -EFAULT;
		goto out_free;
	}

	if (timeout) {
		if (timeout->tv_sec < 0 || timeout->tv_nsec < 0 ||
			timeout->tv_nsec >= 1000000000L) {
			error = -EINVAL;
			goto out_free;
		}
		jiffies_left = timespec64_to_jiffies(timeout);
	}

	max = 0;
	for (sop = sops; sop < sops + nsops; sop++) {
		unsigned long mask = 1ULL << ((sop->sem_num) % BITS_PER_LONG);

		if (sop->sem_num >= max)
			max = sop->sem_num;
		if (sop->sem_flg & SEM_UNDO)
			undos = true;
		if (dup & mask) {
			/*
			 * There was a previous alter access that appears
			 * to have accessed the same semaphore, thus use
			 * the dupsop logic. "appears", because the detection
			 * can only check % BITS_PER_LONG.
			 */
			dupsop = true;
		}
		if (sop->sem_op != 0) {
			alter = true;
			dup |= mask;
		}
	}

	if (undos) {
		/* On success, find_alloc_undo takes the rcu_read_lock */
		un = find_alloc_undo(ns, semid);
		if (IS_ERR(un)) {
			error = PTR_ERR(un);
			goto out_free;
		}
	} else {
		un = NULL;
		rcu_read_lock();
	}

	sma = sem_obtain_object_check(ns, semid);
	if (IS_ERR(sma)) {
		rcu_read_unlock();
		error = PTR_ERR(sma);
		goto out_free;
	}

	error = -EFBIG;
	if (max >= sma->sem_nsems) {
		rcu_read_unlock();
		goto out_free;
	}

	error = -EACCES;
	if (ipcperms(ns, &sma->sem_perm, alter ? S_IWUGO : S_IRUGO)) {
		rcu_read_unlock();
		goto out_free;
	}

	error = security_sem_semop(sma, sops, nsops, alter);
	if (error) {
		rcu_read_unlock();
		goto out_free;
	}

	error = -EIDRM;
	locknum = sem_lock(sma, sops, nsops);
	/*
	 * We eventually might perform the following check in a lockless
	 * fashion, considering ipc_valid_object() locking constraints.
	 * If nsops == 1 and there is no contention for sem_perm.lock, then
	 * only a per-semaphore lock is held and it's OK to proceed with the
	 * check below. More details on the fine grained locking scheme
	 * entangled here and why it's RMID race safe on comments at sem_lock()
	 */
	if (!ipc_valid_object(&sma->sem_perm))
		goto out_unlock_free;
	/*
	 * semid identifiers are not unique - find_alloc_undo may have
	 * allocated an undo structure, it was invalidated by an RMID
	 * and now a new array with received the same id. Check and fail.
	 * This case can be detected checking un->semid. The existence of
	 * "un" itself is guaranteed by rcu.
	 */
	if (un && un->semid == -1)
		goto out_unlock_free;

	queue.sops = sops;
	queue.nsops = nsops;
	queue.undo = un;
	queue.pid = task_tgid_vnr(current);
	queue.alter = alter;
	queue.dupsop = dupsop;

	error = perform_atomic_semop(sma, &queue);
	if (error == 0) { /* non-blocking succesfull path */
		DEFINE_WAKE_Q(wake_q);

		/*
		 * If the operation was successful, then do
		 * the required updates.
		 */
		if (alter)
			do_smart_update(sma, sops, nsops, 1, &wake_q);
		else
			set_semotime(sma, sops);

		sem_unlock(sma, locknum);
		rcu_read_unlock();
		wake_up_q(&wake_q);

		goto out_free;
	}
	if (error < 0) /* non-blocking error path */
		goto out_unlock_free;

	/*
	 * We need to sleep on this operation, so we put the current
	 * task into the pending queue and go to sleep.
	 */
	if (nsops == 1) {
		struct sem *curr;
		curr = &sma->sems[sops->sem_num];

		if (alter) {
			if (sma->complex_count) {
				list_add_tail(&queue.list,
						&sma->pending_alter);
			} else {

				list_add_tail(&queue.list,
						&curr->pending_alter);
			}
		} else {
			list_add_tail(&queue.list, &curr->pending_const);
		}
	} else {
		if (!sma->complex_count)
			merge_queues(sma);

		if (alter)
			list_add_tail(&queue.list, &sma->pending_alter);
		else
			list_add_tail(&queue.list, &sma->pending_const);

		sma->complex_count++;
	}

	do {
		queue.status = -EINTR;
		queue.sleeper = current;

		__set_current_state(TASK_INTERRUPTIBLE);
		sem_unlock(sma, locknum);
		rcu_read_unlock();

		if (timeout)
			jiffies_left = schedule_timeout(jiffies_left);
		else
			schedule();

		/*
		 * fastpath: the semop has completed, either successfully or
		 * not, from the syscall pov, is quite irrelevant to us at this
		 * point; we're done.
		 *
		 * We _do_ care, nonetheless, about being awoken by a signal or
		 * spuriously.  The queue.status is checked again in the
		 * slowpath (aka after taking sem_lock), such that we can detect
		 * scenarios where we were awakened externally, during the
		 * window between wake_q_add() and wake_up_q().
		 */
		error = READ_ONCE(queue.status);
		if (error != -EINTR) {
			/*
			 * User space could assume that semop() is a memory
			 * barrier: Without the mb(), the cpu could
			 * speculatively read in userspace stale data that was
			 * overwritten by the previous owner of the semaphore.
			 */
			smp_mb();
			goto out_free;
		}

		rcu_read_lock();
		locknum = sem_lock(sma, sops, nsops);

		if (!ipc_valid_object(&sma->sem_perm))
			goto out_unlock_free;

		error = READ_ONCE(queue.status);

		/*
		 * If queue.status != -EINTR we are woken up by another process.
		 * Leave without unlink_queue(), but with sem_unlock().
		 */
		if (error != -EINTR)
			goto out_unlock_free;

		/*
		 * If an interrupt occurred we have to clean up the queue.
		 */
		if (timeout && jiffies_left == 0)
			error = -EAGAIN;
	} while (error == -EINTR && !signal_pending(current)); /* spurious */

	unlink_queue(sma, &queue);

out_unlock_free:
	sem_unlock(sma, locknum);
	rcu_read_unlock();
out_free:
	if (sops != fast_sops)
		kvfree(sops);
	return error;
}

SYSCALL_DEFINE4(semtimedop, int, semid, struct sembuf __user *, tsops,
		unsigned, nsops, const struct timespec __user *, timeout)
{
	if (timeout) {
		struct timespec64 ts;
		if (get_timespec64(&ts, timeout))
			return -EFAULT;
		return do_semtimedop(semid, tsops, nsops, &ts);
	}
	return do_semtimedop(semid, tsops, nsops, NULL);
}

#ifdef CONFIG_COMPAT
COMPAT_SYSCALL_DEFINE4(semtimedop, int, semid, struct sembuf __user *, tsems,
		       unsigned, nsops,
		       const struct compat_timespec __user *, timeout)
{
	if (timeout) {
		struct timespec64 ts;
		if (compat_get_timespec64(&ts, timeout))
			return -EFAULT;
		return do_semtimedop(semid, tsems, nsops, &ts);
	}
	return do_semtimedop(semid, tsems, nsops, NULL);
}
#endif

SYSCALL_DEFINE3(semop, int, semid, struct sembuf __user *, tsops,
		unsigned, nsops)
{
	return do_semtimedop(semid, tsops, nsops, NULL);
}

/* If CLONE_SYSVSEM is set, establish sharing of SEM_UNDO state between
 * parent and child tasks.
 */

int copy_semundo(unsigned long clone_flags, struct task_struct *tsk)
{
	struct sem_undo_list *undo_list;
	int error;

	if (clone_flags & CLONE_SYSVSEM) {
		error = get_undo_list(&undo_list);
		if (error)
			return error;
		refcount_inc(&undo_list->refcnt);
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
	struct sem_undo_list *ulp;

	ulp = tsk->sysvsem.undo_list;
	if (!ulp)
		return;
	tsk->sysvsem.undo_list = NULL;

	if (!refcount_dec_and_test(&ulp->refcnt))
		return;

	for (;;) {
		struct sem_array *sma;
		struct sem_undo *un;
		int semid, i;
		DEFINE_WAKE_Q(wake_q);

		cond_resched();

		rcu_read_lock();
		un = list_entry_rcu(ulp->list_proc.next,
				    struct sem_undo, list_proc);
		if (&un->list_proc == &ulp->list_proc) {
			/*
			 * We must wait for freeary() before freeing this ulp,
			 * in case we raced with last sem_undo. There is a small
			 * possibility where we exit while freeary() didn't
			 * finish unlocking sem_undo_list.
			 */
			spin_lock(&ulp->lock);
			spin_unlock(&ulp->lock);
			rcu_read_unlock();
			break;
		}
		spin_lock(&ulp->lock);
		semid = un->semid;
		spin_unlock(&ulp->lock);

		/* exit_sem raced with IPC_RMID, nothing to do */
		if (semid == -1) {
			rcu_read_unlock();
			continue;
		}

		sma = sem_obtain_object_check(tsk->nsproxy->ipc_ns, semid);
		/* exit_sem raced with IPC_RMID, nothing to do */
		if (IS_ERR(sma)) {
			rcu_read_unlock();
			continue;
		}

		sem_lock(sma, NULL, -1);
		/* exit_sem raced with IPC_RMID, nothing to do */
		if (!ipc_valid_object(&sma->sem_perm)) {
			sem_unlock(sma, -1);
			rcu_read_unlock();
			continue;
		}
		un = __lookup_undo(ulp, semid);
		if (un == NULL) {
			/* exit_sem raced with IPC_RMID+semget() that created
			 * exactly the same semid. Nothing to do.
			 */
			sem_unlock(sma, -1);
			rcu_read_unlock();
			continue;
		}

		/* remove un from the linked lists */
		ipc_assert_locked_object(&sma->sem_perm);
		list_del(&un->list_id);

		/* we are the last process using this ulp, acquiring ulp->lock
		 * isn't required. Besides that, we are also protected against
		 * IPC_RMID as we hold sma->sem_perm lock now
		 */
		list_del_rcu(&un->list_proc);

		/* perform adjustments registered in un */
		for (i = 0; i < sma->sem_nsems; i++) {
			struct sem *semaphore = &sma->sems[i];
			if (un->semadj[i]) {
				semaphore->semval += un->semadj[i];
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
				 *	Manfred <manfred@colorfullife.com>
				 */
				if (semaphore->semval < 0)
					semaphore->semval = 0;
				if (semaphore->semval > SEMVMX)
					semaphore->semval = SEMVMX;
				semaphore->sempid = task_tgid_vnr(current);
			}
		}
		/* maybe some queued-up processes were waiting for this */
		do_smart_update(sma, NULL, 0, 1, &wake_q);
		sem_unlock(sma, -1);
		rcu_read_unlock();
		wake_up_q(&wake_q);

		kfree_rcu(un, rcu);
	}
	kfree(ulp);
}

#ifdef CONFIG_PROC_FS
static int sysvipc_sem_proc_show(struct seq_file *s, void *it)
{
	struct user_namespace *user_ns = seq_user_ns(s);
	struct kern_ipc_perm *ipcp = it;
	struct sem_array *sma = container_of(ipcp, struct sem_array, sem_perm);
	time64_t sem_otime;

	/*
	 * The proc interface isn't aware of sem_lock(), it calls
	 * ipc_lock_object() directly (in sysvipc_find_ipc).
	 * In order to stay compatible with sem_lock(), we must
	 * enter / leave complex_mode.
	 */
	complexmode_enter(sma);

	sem_otime = get_semotime(sma);

	seq_printf(s,
		   "%10d %10d  %4o %10u %5u %5u %5u %5u %10llu %10llu\n",
		   sma->sem_perm.key,
		   sma->sem_perm.id,
		   sma->sem_perm.mode,
		   sma->sem_nsems,
		   from_kuid_munged(user_ns, sma->sem_perm.uid),
		   from_kgid_munged(user_ns, sma->sem_perm.gid),
		   from_kuid_munged(user_ns, sma->sem_perm.cuid),
		   from_kgid_munged(user_ns, sma->sem_perm.cgid),
		   sem_otime,
		   sma->sem_ctime);

	complexmode_tryleave(sma);

	return 0;
}
#endif
