/*
 *  Fast Userspace Mutexes (which I call "Futexes!").
 *  (C) Rusty Russell, IBM 2002
 *
 *  Generalized futexes, futex requeueing, misc fixes by Ingo Molnar
 *  (C) Copyright 2003 Red Hat Inc, All Rights Reserved
 *
 *  Removed page pinning, fix privately mapped COW pages and other cleanups
 *  (C) Copyright 2003, 2004 Jamie Lokier
 *
 *  Robust futex support started by Ingo Molnar
 *  (C) Copyright 2006 Red Hat Inc, All Rights Reserved
 *  Thanks to Thomas Gleixner for suggestions, analysis and fixes.
 *
 *  PI-futex support started by Ingo Molnar and Thomas Gleixner
 *  Copyright (C) 2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *  Copyright (C) 2006 Timesys Corp., Thomas Gleixner <tglx@timesys.com>
 *
 *  PRIVATE futexes by Eric Dumazet
 *  Copyright (C) 2007 Eric Dumazet <dada1@cosmosbay.com>
 *
 *  Requeue-PI support by Darren Hart <dvhltc@us.ibm.com>
 *  Copyright (C) IBM Corporation, 2009
 *  Thanks to Thomas Gleixner for conceptual design and careful reviews.
 *
 *  Thanks to Ben LaHaise for yelling "hashed waitqueues" loudly
 *  enough at me, Linus for the original (flawed) idea, Matthew
 *  Kirkwood for proof-of-concept implementation.
 *
 *  "The futexes are also cursed."
 *  "But they come in a choice of three flavours!"
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/jhash.h>
#include <linux/init.h>
#include <linux/futex.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/syscalls.h>
#include <linux/signal.h>
#include <linux/export.h>
#include <linux/magic.h>
#include <linux/pid.h>
#include <linux/nsproxy.h>
#include <linux/ptrace.h>
#include <linux/sched/rt.h>
#include <linux/sched/wake_q.h>
#include <linux/sched/mm.h>
#include <linux/hugetlb.h>
#include <linux/freezer.h>
#include <linux/bootmem.h>
#include <linux/fault-inject.h>

#include <asm/futex.h>

#include "locking/rtmutex_common.h"

/*
 * READ this before attempting to hack on futexes!
 *
 * Basic futex operation and ordering guarantees
 * =============================================
 *
 * The waiter reads the futex value in user space and calls
 * futex_wait(). This function computes the hash bucket and acquires
 * the hash bucket lock. After that it reads the futex user space value
 * again and verifies that the data has not changed. If it has not changed
 * it enqueues itself into the hash bucket, releases the hash bucket lock
 * and schedules.
 *
 * The waker side modifies the user space value of the futex and calls
 * futex_wake(). This function computes the hash bucket and acquires the
 * hash bucket lock. Then it looks for waiters on that futex in the hash
 * bucket and wakes them.
 *
 * In futex wake up scenarios where no tasks are blocked on a futex, taking
 * the hb spinlock can be avoided and simply return. In order for this
 * optimization to work, ordering guarantees must exist so that the waiter
 * being added to the list is acknowledged when the list is concurrently being
 * checked by the waker, avoiding scenarios like the following:
 *
 * CPU 0                               CPU 1
 * val = *futex;
 * sys_futex(WAIT, futex, val);
 *   futex_wait(futex, val);
 *   uval = *futex;
 *                                     *futex = newval;
 *                                     sys_futex(WAKE, futex);
 *                                       futex_wake(futex);
 *                                       if (queue_empty())
 *                                         return;
 *   if (uval == val)
 *      lock(hash_bucket(futex));
 *      queue();
 *     unlock(hash_bucket(futex));
 *     schedule();
 *
 * This would cause the waiter on CPU 0 to wait forever because it
 * missed the transition of the user space value from val to newval
 * and the waker did not find the waiter in the hash bucket queue.
 *
 * The correct serialization ensures that a waiter either observes
 * the changed user space value before blocking or is woken by a
 * concurrent waker:
 *
 * CPU 0                                 CPU 1
 * val = *futex;
 * sys_futex(WAIT, futex, val);
 *   futex_wait(futex, val);
 *
 *   waiters++; (a)
 *   smp_mb(); (A) <-- paired with -.
 *                                  |
 *   lock(hash_bucket(futex));      |
 *                                  |
 *   uval = *futex;                 |
 *                                  |        *futex = newval;
 *                                  |        sys_futex(WAKE, futex);
 *                                  |          futex_wake(futex);
 *                                  |
 *                                  `--------> smp_mb(); (B)
 *   if (uval == val)
 *     queue();
 *     unlock(hash_bucket(futex));
 *     schedule();                         if (waiters)
 *                                           lock(hash_bucket(futex));
 *   else                                    wake_waiters(futex);
 *     waiters--; (b)                        unlock(hash_bucket(futex));
 *
 * Where (A) orders the waiters increment and the futex value read through
 * atomic operations (see hb_waiters_inc) and where (B) orders the write
 * to futex and the waiters read -- this is done by the barriers for both
 * shared and private futexes in get_futex_key_refs().
 *
 * This yields the following case (where X:=waiters, Y:=futex):
 *
 *	X = Y = 0
 *
 *	w[X]=1		w[Y]=1
 *	MB		MB
 *	r[Y]=y		r[X]=x
 *
 * Which guarantees that x==0 && y==0 is impossible; which translates back into
 * the guarantee that we cannot both miss the futex variable change and the
 * enqueue.
 *
 * Note that a new waiter is accounted for in (a) even when it is possible that
 * the wait call can return error, in which case we backtrack from it in (b).
 * Refer to the comment in queue_lock().
 *
 * Similarly, in order to account for waiters being requeued on another
 * address we always increment the waiters for the destination bucket before
 * acquiring the lock. It then decrements them again  after releasing it -
 * the code that actually moves the futex(es) between hash buckets (requeue_futex)
 * will do the additional required waiter count housekeeping. This is done for
 * double_lock_hb() and double_unlock_hb(), respectively.
 */

#ifndef CONFIG_HAVE_FUTEX_CMPXCHG
int __read_mostly futex_cmpxchg_enabled;
#endif

/*
 * Futex flags used to encode options to functions and preserve them across
 * restarts.
 */
#ifdef CONFIG_MMU
# define FLAGS_SHARED		0x01
#else
/*
 * NOMMU does not have per process address space. Let the compiler optimize
 * code away.
 */
# define FLAGS_SHARED		0x00
#endif
#define FLAGS_CLOCKRT		0x02
#define FLAGS_HAS_TIMEOUT	0x04

/*
 * Priority Inheritance state:
 */
struct futex_pi_state {
	/*
	 * list of 'owned' pi_state instances - these have to be
	 * cleaned up in do_exit() if the task exits prematurely:
	 */
	struct list_head list;

	/*
	 * The PI object:
	 */
	struct rt_mutex pi_mutex;

	struct task_struct *owner;
	atomic_t refcount;

	union futex_key key;
};

/**
 * struct futex_q - The hashed futex queue entry, one per waiting task
 * @list:		priority-sorted list of tasks waiting on this futex
 * @task:		the task waiting on the futex
 * @lock_ptr:		the hash bucket lock
 * @key:		the key the futex is hashed on
 * @pi_state:		optional priority inheritance state
 * @rt_waiter:		rt_waiter storage for use with requeue_pi
 * @requeue_pi_key:	the requeue_pi target futex key
 * @bitset:		bitset for the optional bitmasked wakeup
 *
 * We use this hashed waitqueue, instead of a normal wait_queue_t, so
 * we can wake only the relevant ones (hashed queues may be shared).
 *
 * A futex_q has a woken state, just like tasks have TASK_RUNNING.
 * It is considered woken when plist_node_empty(&q->list) || q->lock_ptr == 0.
 * The order of wakeup is always to make the first condition true, then
 * the second.
 *
 * PI futexes are typically woken before they are removed from the hash list via
 * the rt_mutex code. See unqueue_me_pi().
 */
struct futex_q {
	struct plist_node list;

	struct task_struct *task;
	spinlock_t *lock_ptr;
	union futex_key key;
	struct futex_pi_state *pi_state;
	struct rt_mutex_waiter *rt_waiter;
	union futex_key *requeue_pi_key;
	u32 bitset;
};

static const struct futex_q futex_q_init = {
	/* list gets initialized in queue_me()*/
	.key = FUTEX_KEY_INIT,
	.bitset = FUTEX_BITSET_MATCH_ANY
};

/*
 * Hash buckets are shared by all the futex_keys that hash to the same
 * location.  Each key may have multiple futex_q structures, one for each task
 * waiting on a futex.
 */
struct futex_hash_bucket {
	atomic_t waiters;
	spinlock_t lock;
	struct plist_head chain;
} ____cacheline_aligned_in_smp;

/*
 * The base of the bucket array and its size are always used together
 * (after initialization only in hash_futex()), so ensure that they
 * reside in the same cacheline.
 */
static struct {
	struct futex_hash_bucket *queues;
	unsigned long            hashsize;
} __futex_data __read_mostly __aligned(2*sizeof(long));
#define futex_queues   (__futex_data.queues)
#define futex_hashsize (__futex_data.hashsize)


/*
 * Fault injections for futexes.
 */
#ifdef CONFIG_FAIL_FUTEX

static struct {
	struct fault_attr attr;

	bool ignore_private;
} fail_futex = {
	.attr = FAULT_ATTR_INITIALIZER,
	.ignore_private = false,
};

static int __init setup_fail_futex(char *str)
{
	return setup_fault_attr(&fail_futex.attr, str);
}
__setup("fail_futex=", setup_fail_futex);

static bool should_fail_futex(bool fshared)
{
	if (fail_futex.ignore_private && !fshared)
		return false;

	return should_fail(&fail_futex.attr, 1);
}

#ifdef CONFIG_FAULT_INJECTION_DEBUG_FS

static int __init fail_futex_debugfs(void)
{
	umode_t mode = S_IFREG | S_IRUSR | S_IWUSR;
	struct dentry *dir;

	dir = fault_create_debugfs_attr("fail_futex", NULL,
					&fail_futex.attr);
	if (IS_ERR(dir))
		return PTR_ERR(dir);

	if (!debugfs_create_bool("ignore-private", mode, dir,
				 &fail_futex.ignore_private)) {
		debugfs_remove_recursive(dir);
		return -ENOMEM;
	}

	return 0;
}

late_initcall(fail_futex_debugfs);

#endif /* CONFIG_FAULT_INJECTION_DEBUG_FS */

#else
static inline bool should_fail_futex(bool fshared)
{
	return false;
}
#endif /* CONFIG_FAIL_FUTEX */

static inline void futex_get_mm(union futex_key *key)
{
	mmgrab(key->private.mm);
	/*
	 * Ensure futex_get_mm() implies a full barrier such that
	 * get_futex_key() implies a full barrier. This is relied upon
	 * as smp_mb(); (B), see the ordering comment above.
	 */
	smp_mb__after_atomic();
}

/*
 * Reflects a new waiter being added to the waitqueue.
 */
static inline void hb_waiters_inc(struct futex_hash_bucket *hb)
{
#ifdef CONFIG_SMP
	atomic_inc(&hb->waiters);
	/*
	 * Full barrier (A), see the ordering comment above.
	 */
	smp_mb__after_atomic();
#endif
}

/*
 * Reflects a waiter being removed from the waitqueue by wakeup
 * paths.
 */
static inline void hb_waiters_dec(struct futex_hash_bucket *hb)
{
#ifdef CONFIG_SMP
	atomic_dec(&hb->waiters);
#endif
}

static inline int hb_waiters_pending(struct futex_hash_bucket *hb)
{
#ifdef CONFIG_SMP
	return atomic_read(&hb->waiters);
#else
	return 1;
#endif
}

/**
 * hash_futex - Return the hash bucket in the global hash
 * @key:	Pointer to the futex key for which the hash is calculated
 *
 * We hash on the keys returned from get_futex_key (see below) and return the
 * corresponding hash bucket in the global hash.
 */
static struct futex_hash_bucket *hash_futex(union futex_key *key)
{
	u32 hash = jhash2((u32*)&key->both.word,
			  (sizeof(key->both.word)+sizeof(key->both.ptr))/4,
			  key->both.offset);
	return &futex_queues[hash & (futex_hashsize - 1)];
}


/**
 * match_futex - Check whether two futex keys are equal
 * @key1:	Pointer to key1
 * @key2:	Pointer to key2
 *
 * Return 1 if two futex_keys are equal, 0 otherwise.
 */
static inline int match_futex(union futex_key *key1, union futex_key *key2)
{
	return (key1 && key2
		&& key1->both.word == key2->both.word
		&& key1->both.ptr == key2->both.ptr
		&& key1->both.offset == key2->both.offset);
}

/*
 * Take a reference to the resource addressed by a key.
 * Can be called while holding spinlocks.
 *
 */
static void get_futex_key_refs(union futex_key *key)
{
	if (!key->both.ptr)
		return;

	/*
	 * On MMU less systems futexes are always "private" as there is no per
	 * process address space. We need the smp wmb nevertheless - yes,
	 * arch/blackfin has MMU less SMP ...
	 */
	if (!IS_ENABLED(CONFIG_MMU)) {
		smp_mb(); /* explicit smp_mb(); (B) */
		return;
	}

	switch (key->both.offset & (FUT_OFF_INODE|FUT_OFF_MMSHARED)) {
	case FUT_OFF_INODE:
		ihold(key->shared.inode); /* implies smp_mb(); (B) */
		break;
	case FUT_OFF_MMSHARED:
		futex_get_mm(key); /* implies smp_mb(); (B) */
		break;
	default:
		/*
		 * Private futexes do not hold reference on an inode or
		 * mm, therefore the only purpose of calling get_futex_key_refs
		 * is because we need the barrier for the lockless waiter check.
		 */
		smp_mb(); /* explicit smp_mb(); (B) */
	}
}

/*
 * Drop a reference to the resource addressed by a key.
 * The hash bucket spinlock must not be held. This is
 * a no-op for private futexes, see comment in the get
 * counterpart.
 */
static void drop_futex_key_refs(union futex_key *key)
{
	if (!key->both.ptr) {
		/* If we're here then we tried to put a key we failed to get */
		WARN_ON_ONCE(1);
		return;
	}

	if (!IS_ENABLED(CONFIG_MMU))
		return;

	switch (key->both.offset & (FUT_OFF_INODE|FUT_OFF_MMSHARED)) {
	case FUT_OFF_INODE:
		iput(key->shared.inode);
		break;
	case FUT_OFF_MMSHARED:
		mmdrop(key->private.mm);
		break;
	}
}

/**
 * get_futex_key() - Get parameters which are the keys for a futex
 * @uaddr:	virtual address of the futex
 * @fshared:	0 for a PROCESS_PRIVATE futex, 1 for PROCESS_SHARED
 * @key:	address where result is stored.
 * @rw:		mapping needs to be read/write (values: VERIFY_READ,
 *              VERIFY_WRITE)
 *
 * Return: a negative error code or 0
 *
 * The key words are stored in *key on success.
 *
 * For shared mappings, it's (page->index, file_inode(vma->vm_file),
 * offset_within_page).  For private mappings, it's (uaddr, current->mm).
 * We can usually work out the index without swapping in the page.
 *
 * lock_page() might sleep, the caller should not hold a spinlock.
 */
static int
get_futex_key(u32 __user *uaddr, int fshared, union futex_key *key, int rw)
{
	unsigned long address = (unsigned long)uaddr;
	struct mm_struct *mm = current->mm;
	struct page *page, *tail;
	struct address_space *mapping;
	int err, ro = 0;

	/*
	 * The futex address must be "naturally" aligned.
	 */
	key->both.offset = address % PAGE_SIZE;
	if (unlikely((address % sizeof(u32)) != 0))
		return -EINVAL;
	address -= key->both.offset;

	if (unlikely(!access_ok(rw, uaddr, sizeof(u32))))
		return -EFAULT;

	if (unlikely(should_fail_futex(fshared)))
		return -EFAULT;

	/*
	 * PROCESS_PRIVATE futexes are fast.
	 * As the mm cannot disappear under us and the 'key' only needs
	 * virtual address, we dont even have to find the underlying vma.
	 * Note : We do have to check 'uaddr' is a valid user address,
	 *        but access_ok() should be faster than find_vma()
	 */
	if (!fshared) {
		key->private.mm = mm;
		key->private.address = address;
		get_futex_key_refs(key);  /* implies smp_mb(); (B) */
		return 0;
	}

again:
	/* Ignore any VERIFY_READ mapping (futex common case) */
	if (unlikely(should_fail_futex(fshared)))
		return -EFAULT;

	err = get_user_pages_fast(address, 1, 1, &page);
	/*
	 * If write access is not required (eg. FUTEX_WAIT), try
	 * and get read-only access.
	 */
	if (err == -EFAULT && rw == VERIFY_READ) {
		err = get_user_pages_fast(address, 1, 0, &page);
		ro = 1;
	}
	if (err < 0)
		return err;
	else
		err = 0;

	/*
	 * The treatment of mapping from this point on is critical. The page
	 * lock protects many things but in this context the page lock
	 * stabilizes mapping, prevents inode freeing in the shared
	 * file-backed region case and guards against movement to swap cache.
	 *
	 * Strictly speaking the page lock is not needed in all cases being
	 * considered here and page lock forces unnecessarily serialization
	 * From this point on, mapping will be re-verified if necessary and
	 * page lock will be acquired only if it is unavoidable
	 *
	 * Mapping checks require the head page for any compound page so the
	 * head page and mapping is looked up now. For anonymous pages, it
	 * does not matter if the page splits in the future as the key is
	 * based on the address. For filesystem-backed pages, the tail is
	 * required as the index of the page determines the key. For
	 * base pages, there is no tail page and tail == page.
	 */
	tail = page;
	page = compound_head(page);
	mapping = READ_ONCE(page->mapping);

	/*
	 * If page->mapping is NULL, then it cannot be a PageAnon
	 * page; but it might be the ZERO_PAGE or in the gate area or
	 * in a special mapping (all cases which we are happy to fail);
	 * or it may have been a good file page when get_user_pages_fast
	 * found it, but truncated or holepunched or subjected to
	 * invalidate_complete_page2 before we got the page lock (also
	 * cases which we are happy to fail).  And we hold a reference,
	 * so refcount care in invalidate_complete_page's remove_mapping
	 * prevents drop_caches from setting mapping to NULL beneath us.
	 *
	 * The case we do have to guard against is when memory pressure made
	 * shmem_writepage move it from filecache to swapcache beneath us:
	 * an unlikely race, but we do need to retry for page->mapping.
	 */
	if (unlikely(!mapping)) {
		int shmem_swizzled;

		/*
		 * Page lock is required to identify which special case above
		 * applies. If this is really a shmem page then the page lock
		 * will prevent unexpected transitions.
		 */
		lock_page(page);
		shmem_swizzled = PageSwapCache(page) || page->mapping;
		unlock_page(page);
		put_page(page);

		if (shmem_swizzled)
			goto again;

		return -EFAULT;
	}

	/*
	 * Private mappings are handled in a simple way.
	 *
	 * If the futex key is stored on an anonymous page, then the associated
	 * object is the mm which is implicitly pinned by the calling process.
	 *
	 * NOTE: When userspace waits on a MAP_SHARED mapping, even if
	 * it's a read-only handle, it's expected that futexes attach to
	 * the object not the particular process.
	 */
	if (PageAnon(page)) {
		/*
		 * A RO anonymous page will never change and thus doesn't make
		 * sense for futex operations.
		 */
		if (unlikely(should_fail_futex(fshared)) || ro) {
			err = -EFAULT;
			goto out;
		}

		key->both.offset |= FUT_OFF_MMSHARED; /* ref taken on mm */
		key->private.mm = mm;
		key->private.address = address;

		get_futex_key_refs(key); /* implies smp_mb(); (B) */

	} else {
		struct inode *inode;

		/*
		 * The associated futex object in this case is the inode and
		 * the page->mapping must be traversed. Ordinarily this should
		 * be stabilised under page lock but it's not strictly
		 * necessary in this case as we just want to pin the inode, not
		 * update the radix tree or anything like that.
		 *
		 * The RCU read lock is taken as the inode is finally freed
		 * under RCU. If the mapping still matches expectations then the
		 * mapping->host can be safely accessed as being a valid inode.
		 */
		rcu_read_lock();

		if (READ_ONCE(page->mapping) != mapping) {
			rcu_read_unlock();
			put_page(page);

			goto again;
		}

		inode = READ_ONCE(mapping->host);
		if (!inode) {
			rcu_read_unlock();
			put_page(page);

			goto again;
		}

		/*
		 * Take a reference unless it is about to be freed. Previously
		 * this reference was taken by ihold under the page lock
		 * pinning the inode in place so i_lock was unnecessary. The
		 * only way for this check to fail is if the inode was
		 * truncated in parallel so warn for now if this happens.
		 *
		 * We are not calling into get_futex_key_refs() in file-backed
		 * cases, therefore a successful atomic_inc return below will
		 * guarantee that get_futex_key() will still imply smp_mb(); (B).
		 */
		if (WARN_ON_ONCE(!atomic_inc_not_zero(&inode->i_count))) {
			rcu_read_unlock();
			put_page(page);

			goto again;
		}

		/* Should be impossible but lets be paranoid for now */
		if (WARN_ON_ONCE(inode->i_mapping != mapping)) {
			err = -EFAULT;
			rcu_read_unlock();
			iput(inode);

			goto out;
		}

		key->both.offset |= FUT_OFF_INODE; /* inode-based key */
		key->shared.inode = inode;
		key->shared.pgoff = basepage_index(tail);
		rcu_read_unlock();
	}

out:
	put_page(page);
	return err;
}

static inline void put_futex_key(union futex_key *key)
{
	drop_futex_key_refs(key);
}

/**
 * fault_in_user_writeable() - Fault in user address and verify RW access
 * @uaddr:	pointer to faulting user space address
 *
 * Slow path to fixup the fault we just took in the atomic write
 * access to @uaddr.
 *
 * We have no generic implementation of a non-destructive write to the
 * user address. We know that we faulted in the atomic pagefault
 * disabled section so we can as well avoid the #PF overhead by
 * calling get_user_pages() right away.
 */
static int fault_in_user_writeable(u32 __user *uaddr)
{
	struct mm_struct *mm = current->mm;
	int ret;

	down_read(&mm->mmap_sem);
	ret = fixup_user_fault(current, mm, (unsigned long)uaddr,
			       FAULT_FLAG_WRITE, NULL);
	up_read(&mm->mmap_sem);

	return ret < 0 ? ret : 0;
}

/**
 * futex_top_waiter() - Return the highest priority waiter on a futex
 * @hb:		the hash bucket the futex_q's reside in
 * @key:	the futex key (to distinguish it from other futex futex_q's)
 *
 * Must be called with the hb lock held.
 */
static struct futex_q *futex_top_waiter(struct futex_hash_bucket *hb,
					union futex_key *key)
{
	struct futex_q *this;

	plist_for_each_entry(this, &hb->chain, list) {
		if (match_futex(&this->key, key))
			return this;
	}
	return NULL;
}

static int cmpxchg_futex_value_locked(u32 *curval, u32 __user *uaddr,
				      u32 uval, u32 newval)
{
	int ret;

	pagefault_disable();
	ret = futex_atomic_cmpxchg_inatomic(curval, uaddr, uval, newval);
	pagefault_enable();

	return ret;
}

static int get_futex_value_locked(u32 *dest, u32 __user *from)
{
	int ret;

	pagefault_disable();
	ret = __get_user(*dest, from);
	pagefault_enable();

	return ret ? -EFAULT : 0;
}


/*
 * PI code:
 */
static int refill_pi_state_cache(void)
{
	struct futex_pi_state *pi_state;

	if (likely(current->pi_state_cache))
		return 0;

	pi_state = kzalloc(sizeof(*pi_state), GFP_KERNEL);

	if (!pi_state)
		return -ENOMEM;

	INIT_LIST_HEAD(&pi_state->list);
	/* pi_mutex gets initialized later */
	pi_state->owner = NULL;
	atomic_set(&pi_state->refcount, 1);
	pi_state->key = FUTEX_KEY_INIT;

	current->pi_state_cache = pi_state;

	return 0;
}

static struct futex_pi_state * alloc_pi_state(void)
{
	struct futex_pi_state *pi_state = current->pi_state_cache;

	WARN_ON(!pi_state);
	current->pi_state_cache = NULL;

	return pi_state;
}

/*
 * Drops a reference to the pi_state object and frees or caches it
 * when the last reference is gone.
 *
 * Must be called with the hb lock held.
 */
static void put_pi_state(struct futex_pi_state *pi_state)
{
	if (!pi_state)
		return;

	if (!atomic_dec_and_test(&pi_state->refcount))
		return;

	/*
	 * If pi_state->owner is NULL, the owner is most probably dying
	 * and has cleaned up the pi_state already
	 */
	if (pi_state->owner) {
		raw_spin_lock_irq(&pi_state->owner->pi_lock);
		list_del_init(&pi_state->list);
		raw_spin_unlock_irq(&pi_state->owner->pi_lock);

		rt_mutex_proxy_unlock(&pi_state->pi_mutex, pi_state->owner);
	}

	if (current->pi_state_cache)
		kfree(pi_state);
	else {
		/*
		 * pi_state->list is already empty.
		 * clear pi_state->owner.
		 * refcount is at 0 - put it back to 1.
		 */
		pi_state->owner = NULL;
		atomic_set(&pi_state->refcount, 1);
		current->pi_state_cache = pi_state;
	}
}

/*
 * Look up the task based on what TID userspace gave us.
 * We dont trust it.
 */
static struct task_struct * futex_find_get_task(pid_t pid)
{
	struct task_struct *p;

	rcu_read_lock();
	p = find_task_by_vpid(pid);
	if (p)
		get_task_struct(p);

	rcu_read_unlock();

	return p;
}

/*
 * This task is holding PI mutexes at exit time => bad.
 * Kernel cleans up PI-state, but userspace is likely hosed.
 * (Robust-futex cleanup is separate and might save the day for userspace.)
 */
void exit_pi_state_list(struct task_struct *curr)
{
	struct list_head *next, *head = &curr->pi_state_list;
	struct futex_pi_state *pi_state;
	struct futex_hash_bucket *hb;
	union futex_key key = FUTEX_KEY_INIT;

	if (!futex_cmpxchg_enabled)
		return;
	/*
	 * We are a ZOMBIE and nobody can enqueue itself on
	 * pi_state_list anymore, but we have to be careful
	 * versus waiters unqueueing themselves:
	 */
	raw_spin_lock_irq(&curr->pi_lock);
	while (!list_empty(head)) {

		next = head->next;
		pi_state = list_entry(next, struct futex_pi_state, list);
		key = pi_state->key;
		hb = hash_futex(&key);
		raw_spin_unlock_irq(&curr->pi_lock);

		spin_lock(&hb->lock);

		raw_spin_lock_irq(&curr->pi_lock);
		/*
		 * We dropped the pi-lock, so re-check whether this
		 * task still owns the PI-state:
		 */
		if (head->next != next) {
			spin_unlock(&hb->lock);
			continue;
		}

		WARN_ON(pi_state->owner != curr);
		WARN_ON(list_empty(&pi_state->list));
		list_del_init(&pi_state->list);
		pi_state->owner = NULL;
		raw_spin_unlock_irq(&curr->pi_lock);

		rt_mutex_unlock(&pi_state->pi_mutex);

		spin_unlock(&hb->lock);

		raw_spin_lock_irq(&curr->pi_lock);
	}
	raw_spin_unlock_irq(&curr->pi_lock);
}

/*
 * We need to check the following states:
 *
 *      Waiter | pi_state | pi->owner | uTID      | uODIED | ?
 *
 * [1]  NULL   | ---      | ---       | 0         | 0/1    | Valid
 * [2]  NULL   | ---      | ---       | >0        | 0/1    | Valid
 *
 * [3]  Found  | NULL     | --        | Any       | 0/1    | Invalid
 *
 * [4]  Found  | Found    | NULL      | 0         | 1      | Valid
 * [5]  Found  | Found    | NULL      | >0        | 1      | Invalid
 *
 * [6]  Found  | Found    | task      | 0         | 1      | Valid
 *
 * [7]  Found  | Found    | NULL      | Any       | 0      | Invalid
 *
 * [8]  Found  | Found    | task      | ==taskTID | 0/1    | Valid
 * [9]  Found  | Found    | task      | 0         | 0      | Invalid
 * [10] Found  | Found    | task      | !=taskTID | 0/1    | Invalid
 *
 * [1]	Indicates that the kernel can acquire the futex atomically. We
 *	came came here due to a stale FUTEX_WAITERS/FUTEX_OWNER_DIED bit.
 *
 * [2]	Valid, if TID does not belong to a kernel thread. If no matching
 *      thread is found then it indicates that the owner TID has died.
 *
 * [3]	Invalid. The waiter is queued on a non PI futex
 *
 * [4]	Valid state after exit_robust_list(), which sets the user space
 *	value to FUTEX_WAITERS | FUTEX_OWNER_DIED.
 *
 * [5]	The user space value got manipulated between exit_robust_list()
 *	and exit_pi_state_list()
 *
 * [6]	Valid state after exit_pi_state_list() which sets the new owner in
 *	the pi_state but cannot access the user space value.
 *
 * [7]	pi_state->owner can only be NULL when the OWNER_DIED bit is set.
 *
 * [8]	Owner and user space value match
 *
 * [9]	There is no transient state which sets the user space TID to 0
 *	except exit_robust_list(), but this is indicated by the
 *	FUTEX_OWNER_DIED bit. See [4]
 *
 * [10] There is no transient state which leaves owner and user space
 *	TID out of sync.
 */

/*
 * Validate that the existing waiter has a pi_state and sanity check
 * the pi_state against the user space value. If correct, attach to
 * it.
 */
static int attach_to_pi_state(u32 uval, struct futex_pi_state *pi_state,
			      struct futex_pi_state **ps)
{
	pid_t pid = uval & FUTEX_TID_MASK;

	/*
	 * Userspace might have messed up non-PI and PI futexes [3]
	 */
	if (unlikely(!pi_state))
		return -EINVAL;

	WARN_ON(!atomic_read(&pi_state->refcount));

	/*
	 * Handle the owner died case:
	 */
	if (uval & FUTEX_OWNER_DIED) {
		/*
		 * exit_pi_state_list sets owner to NULL and wakes the
		 * topmost waiter. The task which acquires the
		 * pi_state->rt_mutex will fixup owner.
		 */
		if (!pi_state->owner) {
			/*
			 * No pi state owner, but the user space TID
			 * is not 0. Inconsistent state. [5]
			 */
			if (pid)
				return -EINVAL;
			/*
			 * Take a ref on the state and return success. [4]
			 */
			goto out_state;
		}

		/*
		 * If TID is 0, then either the dying owner has not
		 * yet executed exit_pi_state_list() or some waiter
		 * acquired the rtmutex in the pi state, but did not
		 * yet fixup the TID in user space.
		 *
		 * Take a ref on the state and return success. [6]
		 */
		if (!pid)
			goto out_state;
	} else {
		/*
		 * If the owner died bit is not set, then the pi_state
		 * must have an owner. [7]
		 */
		if (!pi_state->owner)
			return -EINVAL;
	}

	/*
	 * Bail out if user space manipulated the futex value. If pi
	 * state exists then the owner TID must be the same as the
	 * user space TID. [9/10]
	 */
	if (pid != task_pid_vnr(pi_state->owner))
		return -EINVAL;
out_state:
	atomic_inc(&pi_state->refcount);
	*ps = pi_state;
	return 0;
}

/*
 * Lookup the task for the TID provided from user space and attach to
 * it after doing proper sanity checks.
 */
static int attach_to_pi_owner(u32 uval, union futex_key *key,
			      struct futex_pi_state **ps)
{
	pid_t pid = uval & FUTEX_TID_MASK;
	struct futex_pi_state *pi_state;
	struct task_struct *p;

	/*
	 * We are the first waiter - try to look up the real owner and attach
	 * the new pi_state to it, but bail out when TID = 0 [1]
	 */
	if (!pid)
		return -ESRCH;
	p = futex_find_get_task(pid);
	if (!p)
		return -ESRCH;

	if (unlikely(p->flags & PF_KTHREAD)) {
		put_task_struct(p);
		return -EPERM;
	}

	/*
	 * We need to look at the task state flags to figure out,
	 * whether the task is exiting. To protect against the do_exit
	 * change of the task flags, we do this protected by
	 * p->pi_lock:
	 */
	raw_spin_lock_irq(&p->pi_lock);
	if (unlikely(p->flags & PF_EXITING)) {
		/*
		 * The task is on the way out. When PF_EXITPIDONE is
		 * set, we know that the task has finished the
		 * cleanup:
		 */
		int ret = (p->flags & PF_EXITPIDONE) ? -ESRCH : -EAGAIN;

		raw_spin_unlock_irq(&p->pi_lock);
		put_task_struct(p);
		return ret;
	}

	/*
	 * No existing pi state. First waiter. [2]
	 */
	pi_state = alloc_pi_state();

	/*
	 * Initialize the pi_mutex in locked state and make @p
	 * the owner of it:
	 */
	rt_mutex_init_proxy_locked(&pi_state->pi_mutex, p);

	/* Store the key for possible exit cleanups: */
	pi_state->key = *key;

	WARN_ON(!list_empty(&pi_state->list));
	list_add(&pi_state->list, &p->pi_state_list);
	pi_state->owner = p;
	raw_spin_unlock_irq(&p->pi_lock);

	put_task_struct(p);

	*ps = pi_state;

	return 0;
}

static int lookup_pi_state(u32 uval, struct futex_hash_bucket *hb,
			   union futex_key *key, struct futex_pi_state **ps)
{
	struct futex_q *match = futex_top_waiter(hb, key);

	/*
	 * If there is a waiter on that futex, validate it and
	 * attach to the pi_state when the validation succeeds.
	 */
	if (match)
		return attach_to_pi_state(uval, match->pi_state, ps);

	/*
	 * We are the first waiter - try to look up the owner based on
	 * @uval and attach to it.
	 */
	return attach_to_pi_owner(uval, key, ps);
}

static int lock_pi_update_atomic(u32 __user *uaddr, u32 uval, u32 newval)
{
	u32 uninitialized_var(curval);

	if (unlikely(should_fail_futex(true)))
		return -EFAULT;

	if (unlikely(cmpxchg_futex_value_locked(&curval, uaddr, uval, newval)))
		return -EFAULT;

	/*If user space value changed, let the caller retry */
	return curval != uval ? -EAGAIN : 0;
}

/**
 * futex_lock_pi_atomic() - Atomic work required to acquire a pi aware futex
 * @uaddr:		the pi futex user address
 * @hb:			the pi futex hash bucket
 * @key:		the futex key associated with uaddr and hb
 * @ps:			the pi_state pointer where we store the result of the
 *			lookup
 * @task:		the task to perform the atomic lock work for.  This will
 *			be "current" except in the case of requeue pi.
 * @set_waiters:	force setting the FUTEX_WAITERS bit (1) or not (0)
 *
 * Return:
 *  0 - ready to wait;
 *  1 - acquired the lock;
 * <0 - error
 *
 * The hb->lock and futex_key refs shall be held by the caller.
 */
static int futex_lock_pi_atomic(u32 __user *uaddr, struct futex_hash_bucket *hb,
				union futex_key *key,
				struct futex_pi_state **ps,
				struct task_struct *task, int set_waiters)
{
	u32 uval, newval, vpid = task_pid_vnr(task);
	struct futex_q *match;
	int ret;

	/*
	 * Read the user space value first so we can validate a few
	 * things before proceeding further.
	 */
	if (get_futex_value_locked(&uval, uaddr))
		return -EFAULT;

	if (unlikely(should_fail_futex(true)))
		return -EFAULT;

	/*
	 * Detect deadlocks.
	 */
	if ((unlikely((uval & FUTEX_TID_MASK) == vpid)))
		return -EDEADLK;

	if ((unlikely(should_fail_futex(true))))
		return -EDEADLK;

	/*
	 * Lookup existing state first. If it exists, try to attach to
	 * its pi_state.
	 */
	match = futex_top_waiter(hb, key);
	if (match)
		return attach_to_pi_state(uval, match->pi_state, ps);

	/*
	 * No waiter and user TID is 0. We are here because the
	 * waiters or the owner died bit is set or called from
	 * requeue_cmp_pi or for whatever reason something took the
	 * syscall.
	 */
	if (!(uval & FUTEX_TID_MASK)) {
		/*
		 * We take over the futex. No other waiters and the user space
		 * TID is 0. We preserve the owner died bit.
		 */
		newval = uval & FUTEX_OWNER_DIED;
		newval |= vpid;

		/* The futex requeue_pi code can enforce the waiters bit */
		if (set_waiters)
			newval |= FUTEX_WAITERS;

		ret = lock_pi_update_atomic(uaddr, uval, newval);
		/* If the take over worked, return 1 */
		return ret < 0 ? ret : 1;
	}

	/*
	 * First waiter. Set the waiters bit before attaching ourself to
	 * the owner. If owner tries to unlock, it will be forced into
	 * the kernel and blocked on hb->lock.
	 */
	newval = uval | FUTEX_WAITERS;
	ret = lock_pi_update_atomic(uaddr, uval, newval);
	if (ret)
		return ret;
	/*
	 * If the update of the user space value succeeded, we try to
	 * attach to the owner. If that fails, no harm done, we only
	 * set the FUTEX_WAITERS bit in the user space variable.
	 */
	return attach_to_pi_owner(uval, key, ps);
}

/**
 * __unqueue_futex() - Remove the futex_q from its futex_hash_bucket
 * @q:	The futex_q to unqueue
 *
 * The q->lock_ptr must not be NULL and must be held by the caller.
 */
static void __unqueue_futex(struct futex_q *q)
{
	struct futex_hash_bucket *hb;

	if (WARN_ON_SMP(!q->lock_ptr || !spin_is_locked(q->lock_ptr))
	    || WARN_ON(plist_node_empty(&q->list)))
		return;

	hb = container_of(q->lock_ptr, struct futex_hash_bucket, lock);
	plist_del(&q->list, &hb->chain);
	hb_waiters_dec(hb);
}

/*
 * The hash bucket lock must be held when this is called.
 * Afterwards, the futex_q must not be accessed. Callers
 * must ensure to later call wake_up_q() for the actual
 * wakeups to occur.
 */
static void mark_wake_futex(struct wake_q_head *wake_q, struct futex_q *q)
{
	struct task_struct *p = q->task;

	if (WARN(q->pi_state || q->rt_waiter, "refusing to wake PI futex\n"))
		return;

	/*
	 * Queue the task for later wakeup for after we've released
	 * the hb->lock. wake_q_add() grabs reference to p.
	 */
	wake_q_add(wake_q, p);
	__unqueue_futex(q);
	/*
	 * The waiting task can free the futex_q as soon as
	 * q->lock_ptr = NULL is written, without taking any locks. A
	 * memory barrier is required here to prevent the following
	 * store to lock_ptr from getting ahead of the plist_del.
	 */
	smp_wmb();
	q->lock_ptr = NULL;
}

static int wake_futex_pi(u32 __user *uaddr, u32 uval, struct futex_q *this,
			 struct futex_hash_bucket *hb)
{
	struct task_struct *new_owner;
	struct futex_pi_state *pi_state = this->pi_state;
	u32 uninitialized_var(curval), newval;
	DEFINE_WAKE_Q(wake_q);
	bool deboost;
	int ret = 0;

	if (!pi_state)
		return -EINVAL;

	/*
	 * If current does not own the pi_state then the futex is
	 * inconsistent and user space fiddled with the futex value.
	 */
	if (pi_state->owner != current)
		return -EINVAL;

	raw_spin_lock_irq(&pi_state->pi_mutex.wait_lock);
	new_owner = rt_mutex_next_owner(&pi_state->pi_mutex);

	/*
	 * It is possible that the next waiter (the one that brought
	 * this owner to the kernel) timed out and is no longer
	 * waiting on the lock.
	 */
	if (!new_owner)
		new_owner = this->task;

	/*
	 * We pass it to the next owner. The WAITERS bit is always
	 * kept enabled while there is PI state around. We cleanup the
	 * owner died bit, because we are the owner.
	 */
	newval = FUTEX_WAITERS | task_pid_vnr(new_owner);

	if (unlikely(should_fail_futex(true)))
		ret = -EFAULT;

	if (cmpxchg_futex_value_locked(&curval, uaddr, uval, newval)) {
		ret = -EFAULT;
	} else if (curval != uval) {
		/*
		 * If a unconditional UNLOCK_PI operation (user space did not
		 * try the TID->0 transition) raced with a waiter setting the
		 * FUTEX_WAITERS flag between get_user() and locking the hash
		 * bucket lock, retry the operation.
		 */
		if ((FUTEX_TID_MASK & curval) == uval)
			ret = -EAGAIN;
		else
			ret = -EINVAL;
	}
	if (ret) {
		raw_spin_unlock_irq(&pi_state->pi_mutex.wait_lock);
		return ret;
	}

	raw_spin_lock(&pi_state->owner->pi_lock);
	WARN_ON(list_empty(&pi_state->list));
	list_del_init(&pi_state->list);
	raw_spin_unlock(&pi_state->owner->pi_lock);

	raw_spin_lock(&new_owner->pi_lock);
	WARN_ON(!list_empty(&pi_state->list));
	list_add(&pi_state->list, &new_owner->pi_state_list);
	pi_state->owner = new_owner;
	raw_spin_unlock(&new_owner->pi_lock);

	raw_spin_unlock_irq(&pi_state->pi_mutex.wait_lock);

	deboost = rt_mutex_futex_unlock(&pi_state->pi_mutex, &wake_q);

	/*
	 * First unlock HB so the waiter does not spin on it once he got woken
	 * up. Second wake up the waiter before the priority is adjusted. If we
	 * deboost first (and lose our higher priority), then the task might get
	 * scheduled away before the wake up can take place.
	 */
	spin_unlock(&hb->lock);
	wake_up_q(&wake_q);
	if (deboost)
		rt_mutex_adjust_prio(current);

	return 0;
}

/*
 * Express the locking dependencies for lockdep:
 */
static inline void
double_lock_hb(struct futex_hash_bucket *hb1, struct futex_hash_bucket *hb2)
{
	if (hb1 <= hb2) {
		spin_lock(&hb1->lock);
		if (hb1 < hb2)
			spin_lock_nested(&hb2->lock, SINGLE_DEPTH_NESTING);
	} else { /* hb1 > hb2 */
		spin_lock(&hb2->lock);
		spin_lock_nested(&hb1->lock, SINGLE_DEPTH_NESTING);
	}
}

static inline void
double_unlock_hb(struct futex_hash_bucket *hb1, struct futex_hash_bucket *hb2)
{
	spin_unlock(&hb1->lock);
	if (hb1 != hb2)
		spin_unlock(&hb2->lock);
}

/*
 * Wake up waiters matching bitset queued on this futex (uaddr).
 */
static int
futex_wake(u32 __user *uaddr, unsigned int flags, int nr_wake, u32 bitset)
{
	struct futex_hash_bucket *hb;
	struct futex_q *this, *next;
	union futex_key key = FUTEX_KEY_INIT;
	int ret;
	DEFINE_WAKE_Q(wake_q);

	if (!bitset)
		return -EINVAL;

	ret = get_futex_key(uaddr, flags & FLAGS_SHARED, &key, VERIFY_READ);
	if (unlikely(ret != 0))
		goto out;

	hb = hash_futex(&key);

	/* Make sure we really have tasks to wakeup */
	if (!hb_waiters_pending(hb))
		goto out_put_key;

	spin_lock(&hb->lock);

	plist_for_each_entry_safe(this, next, &hb->chain, list) {
		if (match_futex (&this->key, &key)) {
			if (this->pi_state || this->rt_waiter) {
				ret = -EINVAL;
				break;
			}

			/* Check if one of the bits is set in both bitsets */
			if (!(this->bitset & bitset))
				continue;

			mark_wake_futex(&wake_q, this);
			if (++ret >= nr_wake)
				break;
		}
	}

	spin_unlock(&hb->lock);
	wake_up_q(&wake_q);
out_put_key:
	put_futex_key(&key);
out:
	return ret;
}

/*
 * Wake up all waiters hashed on the physical page that is mapped
 * to this virtual address:
 */
static int
futex_wake_op(u32 __user *uaddr1, unsigned int flags, u32 __user *uaddr2,
	      int nr_wake, int nr_wake2, int op)
{
	union futex_key key1 = FUTEX_KEY_INIT, key2 = FUTEX_KEY_INIT;
	struct futex_hash_bucket *hb1, *hb2;
	struct futex_q *this, *next;
	int ret, op_ret;
	DEFINE_WAKE_Q(wake_q);

retry:
	ret = get_futex_key(uaddr1, flags & FLAGS_SHARED, &key1, VERIFY_READ);
	if (unlikely(ret != 0))
		goto out;
	ret = get_futex_key(uaddr2, flags & FLAGS_SHARED, &key2, VERIFY_WRITE);
	if (unlikely(ret != 0))
		goto out_put_key1;

	hb1 = hash_futex(&key1);
	hb2 = hash_futex(&key2);

retry_private:
	double_lock_hb(hb1, hb2);
	op_ret = futex_atomic_op_inuser(op, uaddr2);
	if (unlikely(op_ret < 0)) {

		double_unlock_hb(hb1, hb2);

#ifndef CONFIG_MMU
		/*
		 * we don't get EFAULT from MMU faults if we don't have an MMU,
		 * but we might get them from range checking
		 */
		ret = op_ret;
		goto out_put_keys;
#endif

		if (unlikely(op_ret != -EFAULT)) {
			ret = op_ret;
			goto out_put_keys;
		}

		ret = fault_in_user_writeable(uaddr2);
		if (ret)
			goto out_put_keys;

		if (!(flags & FLAGS_SHARED))
			goto retry_private;

		put_futex_key(&key2);
		put_futex_key(&key1);
		goto retry;
	}

	plist_for_each_entry_safe(this, next, &hb1->chain, list) {
		if (match_futex (&this->key, &key1)) {
			if (this->pi_state || this->rt_waiter) {
				ret = -EINVAL;
				goto out_unlock;
			}
			mark_wake_futex(&wake_q, this);
			if (++ret >= nr_wake)
				break;
		}
	}

	if (op_ret > 0) {
		op_ret = 0;
		plist_for_each_entry_safe(this, next, &hb2->chain, list) {
			if (match_futex (&this->key, &key2)) {
				if (this->pi_state || this->rt_waiter) {
					ret = -EINVAL;
					goto out_unlock;
				}
				mark_wake_futex(&wake_q, this);
				if (++op_ret >= nr_wake2)
					break;
			}
		}
		ret += op_ret;
	}

out_unlock:
	double_unlock_hb(hb1, hb2);
	wake_up_q(&wake_q);
out_put_keys:
	put_futex_key(&key2);
out_put_key1:
	put_futex_key(&key1);
out:
	return ret;
}

/**
 * requeue_futex() - Requeue a futex_q from one hb to another
 * @q:		the futex_q to requeue
 * @hb1:	the source hash_bucket
 * @hb2:	the target hash_bucket
 * @key2:	the new key for the requeued futex_q
 */
static inline
void requeue_futex(struct futex_q *q, struct futex_hash_bucket *hb1,
		   struct futex_hash_bucket *hb2, union futex_key *key2)
{

	/*
	 * If key1 and key2 hash to the same bucket, no need to
	 * requeue.
	 */
	if (likely(&hb1->chain != &hb2->chain)) {
		plist_del(&q->list, &hb1->chain);
		hb_waiters_dec(hb1);
		hb_waiters_inc(hb2);
		plist_add(&q->list, &hb2->chain);
		q->lock_ptr = &hb2->lock;
	}
	get_futex_key_refs(key2);
	q->key = *key2;
}

/**
 * requeue_pi_wake_futex() - Wake a task that acquired the lock during requeue
 * @q:		the futex_q
 * @key:	the key of the requeue target futex
 * @hb:		the hash_bucket of the requeue target futex
 *
 * During futex_requeue, with requeue_pi=1, it is possible to acquire the
 * target futex if it is uncontended or via a lock steal.  Set the futex_q key
 * to the requeue target futex so the waiter can detect the wakeup on the right
 * futex, but remove it from the hb and NULL the rt_waiter so it can detect
 * atomic lock acquisition.  Set the q->lock_ptr to the requeue target hb->lock
 * to protect access to the pi_state to fixup the owner later.  Must be called
 * with both q->lock_ptr and hb->lock held.
 */
static inline
void requeue_pi_wake_futex(struct futex_q *q, union futex_key *key,
			   struct futex_hash_bucket *hb)
{
	get_futex_key_refs(key);
	q->key = *key;

	__unqueue_futex(q);

	WARN_ON(!q->rt_waiter);
	q->rt_waiter = NULL;

	q->lock_ptr = &hb->lock;

	wake_up_state(q->task, TASK_NORMAL);
}

/**
 * futex_proxy_trylock_atomic() - Attempt an atomic lock for the top waiter
 * @pifutex:		the user address of the to futex
 * @hb1:		the from futex hash bucket, must be locked by the caller
 * @hb2:		the to futex hash bucket, must be locked by the caller
 * @key1:		the from futex key
 * @key2:		the to futex key
 * @ps:			address to store the pi_state pointer
 * @set_waiters:	force setting the FUTEX_WAITERS bit (1) or not (0)
 *
 * Try and get the lock on behalf of the top waiter if we can do it atomically.
 * Wake the top waiter if we succeed.  If the caller specified set_waiters,
 * then direct futex_lock_pi_atomic() to force setting the FUTEX_WAITERS bit.
 * hb1 and hb2 must be held by the caller.
 *
 * Return:
 *  0 - failed to acquire the lock atomically;
 * >0 - acquired the lock, return value is vpid of the top_waiter
 * <0 - error
 */
static int futex_proxy_trylock_atomic(u32 __user *pifutex,
				 struct futex_hash_bucket *hb1,
				 struct futex_hash_bucket *hb2,
				 union futex_key *key1, union futex_key *key2,
				 struct futex_pi_state **ps, int set_waiters)
{
	struct futex_q *top_waiter = NULL;
	u32 curval;
	int ret, vpid;

	if (get_futex_value_locked(&curval, pifutex))
		return -EFAULT;

	if (unlikely(should_fail_futex(true)))
		return -EFAULT;

	/*
	 * Find the top_waiter and determine if there are additional waiters.
	 * If the caller intends to requeue more than 1 waiter to pifutex,
	 * force futex_lock_pi_atomic() to set the FUTEX_WAITERS bit now,
	 * as we have means to handle the possible fault.  If not, don't set
	 * the bit unecessarily as it will force the subsequent unlock to enter
	 * the kernel.
	 */
	top_waiter = futex_top_waiter(hb1, key1);

	/* There are no waiters, nothing for us to do. */
	if (!top_waiter)
		return 0;

	/* Ensure we requeue to the expected futex. */
	if (!match_futex(top_waiter->requeue_pi_key, key2))
		return -EINVAL;

	/*
	 * Try to take the lock for top_waiter.  Set the FUTEX_WAITERS bit in
	 * the contended case or if set_waiters is 1.  The pi_state is returned
	 * in ps in contended cases.
	 */
	vpid = task_pid_vnr(top_waiter->task);
	ret = futex_lock_pi_atomic(pifutex, hb2, key2, ps, top_waiter->task,
				   set_waiters);
	if (ret == 1) {
		requeue_pi_wake_futex(top_waiter, key2, hb2);
		return vpid;
	}
	return ret;
}

/**
 * futex_requeue() - Requeue waiters from uaddr1 to uaddr2
 * @uaddr1:	source futex user address
 * @flags:	futex flags (FLAGS_SHARED, etc.)
 * @uaddr2:	target futex user address
 * @nr_wake:	number of waiters to wake (must be 1 for requeue_pi)
 * @nr_requeue:	number of waiters to requeue (0-INT_MAX)
 * @cmpval:	@uaddr1 expected value (or %NULL)
 * @requeue_pi:	if we are attempting to requeue from a non-pi futex to a
 *		pi futex (pi to pi requeue is not supported)
 *
 * Requeue waiters on uaddr1 to uaddr2. In the requeue_pi case, try to acquire
 * uaddr2 atomically on behalf of the top waiter.
 *
 * Return:
 * >=0 - on success, the number of tasks requeued or woken;
 *  <0 - on error
 */
static int futex_requeue(u32 __user *uaddr1, unsigned int flags,
			 u32 __user *uaddr2, int nr_wake, int nr_requeue,
			 u32 *cmpval, int requeue_pi)
{
	union futex_key key1 = FUTEX_KEY_INIT, key2 = FUTEX_KEY_INIT;
	int drop_count = 0, task_count = 0, ret;
	struct futex_pi_state *pi_state = NULL;
	struct futex_hash_bucket *hb1, *hb2;
	struct futex_q *this, *next;
	DEFINE_WAKE_Q(wake_q);

	if (requeue_pi) {
		/*
		 * Requeue PI only works on two distinct uaddrs. This
		 * check is only valid for private futexes. See below.
		 */
		if (uaddr1 == uaddr2)
			return -EINVAL;

		/*
		 * requeue_pi requires a pi_state, try to allocate it now
		 * without any locks in case it fails.
		 */
		if (refill_pi_state_cache())
			return -ENOMEM;
		/*
		 * requeue_pi must wake as many tasks as it can, up to nr_wake
		 * + nr_requeue, since it acquires the rt_mutex prior to
		 * returning to userspace, so as to not leave the rt_mutex with
		 * waiters and no owner.  However, second and third wake-ups
		 * cannot be predicted as they involve race conditions with the
		 * first wake and a fault while looking up the pi_state.  Both
		 * pthread_cond_signal() and pthread_cond_broadcast() should
		 * use nr_wake=1.
		 */
		if (nr_wake != 1)
			return -EINVAL;
	}

retry:
	ret = get_futex_key(uaddr1, flags & FLAGS_SHARED, &key1, VERIFY_READ);
	if (unlikely(ret != 0))
		goto out;
	ret = get_futex_key(uaddr2, flags & FLAGS_SHARED, &key2,
			    requeue_pi ? VERIFY_WRITE : VERIFY_READ);
	if (unlikely(ret != 0))
		goto out_put_key1;

	/*
	 * The check above which compares uaddrs is not sufficient for
	 * shared futexes. We need to compare the keys:
	 */
	if (requeue_pi && match_futex(&key1, &key2)) {
		ret = -EINVAL;
		goto out_put_keys;
	}

	hb1 = hash_futex(&key1);
	hb2 = hash_futex(&key2);

retry_private:
	hb_waiters_inc(hb2);
	double_lock_hb(hb1, hb2);

	if (likely(cmpval != NULL)) {
		u32 curval;

		ret = get_futex_value_locked(&curval, uaddr1);

		if (unlikely(ret)) {
			double_unlock_hb(hb1, hb2);
			hb_waiters_dec(hb2);

			ret = get_user(curval, uaddr1);
			if (ret)
				goto out_put_keys;

			if (!(flags & FLAGS_SHARED))
				goto retry_private;

			put_futex_key(&key2);
			put_futex_key(&key1);
			goto retry;
		}
		if (curval != *cmpval) {
			ret = -EAGAIN;
			goto out_unlock;
		}
	}

	if (requeue_pi && (task_count - nr_wake < nr_requeue)) {
		/*
		 * Attempt to acquire uaddr2 and wake the top waiter. If we
		 * intend to requeue waiters, force setting the FUTEX_WAITERS
		 * bit.  We force this here where we are able to easily handle
		 * faults rather in the requeue loop below.
		 */
		ret = futex_proxy_trylock_atomic(uaddr2, hb1, hb2, &key1,
						 &key2, &pi_state, nr_requeue);

		/*
		 * At this point the top_waiter has either taken uaddr2 or is
		 * waiting on it.  If the former, then the pi_state will not
		 * exist yet, look it up one more time to ensure we have a
		 * reference to it. If the lock was taken, ret contains the
		 * vpid of the top waiter task.
		 * If the lock was not taken, we have pi_state and an initial
		 * refcount on it. In case of an error we have nothing.
		 */
		if (ret > 0) {
			WARN_ON(pi_state);
			drop_count++;
			task_count++;
			/*
			 * If we acquired the lock, then the user space value
			 * of uaddr2 should be vpid. It cannot be changed by
			 * the top waiter as it is blocked on hb2 lock if it
			 * tries to do so. If something fiddled with it behind
			 * our back the pi state lookup might unearth it. So
			 * we rather use the known value than rereading and
			 * handing potential crap to lookup_pi_state.
			 *
			 * If that call succeeds then we have pi_state and an
			 * initial refcount on it.
			 */
			ret = lookup_pi_state(ret, hb2, &key2, &pi_state);
		}

		switch (ret) {
		case 0:
			/* We hold a reference on the pi state. */
			break;

			/* If the above failed, then pi_state is NULL */
		case -EFAULT:
			double_unlock_hb(hb1, hb2);
			hb_waiters_dec(hb2);
			put_futex_key(&key2);
			put_futex_key(&key1);
			ret = fault_in_user_writeable(uaddr2);
			if (!ret)
				goto retry;
			goto out;
		case -EAGAIN:
			/*
			 * Two reasons for this:
			 * - Owner is exiting and we just wait for the
			 *   exit to complete.
			 * - The user space value changed.
			 */
			double_unlock_hb(hb1, hb2);
			hb_waiters_dec(hb2);
			put_futex_key(&key2);
			put_futex_key(&key1);
			cond_resched();
			goto retry;
		default:
			goto out_unlock;
		}
	}

	plist_for_each_entry_safe(this, next, &hb1->chain, list) {
		if (task_count - nr_wake >= nr_requeue)
			break;

		if (!match_futex(&this->key, &key1))
			continue;

		/*
		 * FUTEX_WAIT_REQEUE_PI and FUTEX_CMP_REQUEUE_PI should always
		 * be paired with each other and no other futex ops.
		 *
		 * We should never be requeueing a futex_q with a pi_state,
		 * which is awaiting a futex_unlock_pi().
		 */
		if ((requeue_pi && !this->rt_waiter) ||
		    (!requeue_pi && this->rt_waiter) ||
		    this->pi_state) {
			ret = -EINVAL;
			break;
		}

		/*
		 * Wake nr_wake waiters.  For requeue_pi, if we acquired the
		 * lock, we already woke the top_waiter.  If not, it will be
		 * woken by futex_unlock_pi().
		 */
		if (++task_count <= nr_wake && !requeue_pi) {
			mark_wake_futex(&wake_q, this);
			continue;
		}

		/* Ensure we requeue to the expected futex for requeue_pi. */
		if (requeue_pi && !match_futex(this->requeue_pi_key, &key2)) {
			ret = -EINVAL;
			break;
		}

		/*
		 * Requeue nr_requeue waiters and possibly one more in the case
		 * of requeue_pi if we couldn't acquire the lock atomically.
		 */
		if (requeue_pi) {
			/*
			 * Prepare the waiter to take the rt_mutex. Take a
			 * refcount on the pi_state and store the pointer in
			 * the futex_q object of the waiter.
			 */
			atomic_inc(&pi_state->refcount);
			this->pi_state = pi_state;
			ret = rt_mutex_start_proxy_lock(&pi_state->pi_mutex,
							this->rt_waiter,
							this->task);
			if (ret == 1) {
				/*
				 * We got the lock. We do neither drop the
				 * refcount on pi_state nor clear
				 * this->pi_state because the waiter needs the
				 * pi_state for cleaning up the user space
				 * value. It will drop the refcount after
				 * doing so.
				 */
				requeue_pi_wake_futex(this, &key2, hb2);
				drop_count++;
				continue;
			} else if (ret) {
				/*
				 * rt_mutex_start_proxy_lock() detected a
				 * potential deadlock when we tried to queue
				 * that waiter. Drop the pi_state reference
				 * which we took above and remove the pointer
				 * to the state from the waiters futex_q
				 * object.
				 */
				this->pi_state = NULL;
				put_pi_state(pi_state);
				/*
				 * We stop queueing more waiters and let user
				 * space deal with the mess.
				 */
				break;
			}
		}
		requeue_futex(this, hb1, hb2, &key2);
		drop_count++;
	}

	/*
	 * We took an extra initial reference to the pi_state either
	 * in futex_proxy_trylock_atomic() or in lookup_pi_state(). We
	 * need to drop it here again.
	 */
	put_pi_state(pi_state);

out_unlock:
	double_unlock_hb(hb1, hb2);
	wake_up_q(&wake_q);
	hb_waiters_dec(hb2);

	/*
	 * drop_futex_key_refs() must be called outside the spinlocks. During
	 * the requeue we moved futex_q's from the hash bucket at key1 to the
	 * one at key2 and updated their key pointer.  We no longer need to
	 * hold the references to key1.
	 */
	while (--drop_count >= 0)
		drop_futex_key_refs(&key1);

out_put_keys:
	put_futex_key(&key2);
out_put_key1:
	put_futex_key(&key1);
out:
	return ret ? ret : task_count;
}

/* The key must be already stored in q->key. */
static inline struct futex_hash_bucket *queue_lock(struct futex_q *q)
	__acquires(&hb->lock)
{
	struct futex_hash_bucket *hb;

	hb = hash_futex(&q->key);

	/*
	 * Increment the counter before taking the lock so that
	 * a potential waker won't miss a to-be-slept task that is
	 * waiting for the spinlock. This is safe as all queue_lock()
	 * users end up calling queue_me(). Similarly, for housekeeping,
	 * decrement the counter at queue_unlock() when some error has
	 * occurred and we don't end up adding the task to the list.
	 */
	hb_waiters_inc(hb);

	q->lock_ptr = &hb->lock;

	spin_lock(&hb->lock); /* implies smp_mb(); (A) */
	return hb;
}

static inline void
queue_unlock(struct futex_hash_bucket *hb)
	__releases(&hb->lock)
{
	spin_unlock(&hb->lock);
	hb_waiters_dec(hb);
}

/**
 * queue_me() - Enqueue the futex_q on the futex_hash_bucket
 * @q:	The futex_q to enqueue
 * @hb:	The destination hash bucket
 *
 * The hb->lock must be held by the caller, and is released here. A call to
 * queue_me() is typically paired with exactly one call to unqueue_me().  The
 * exceptions involve the PI related operations, which may use unqueue_me_pi()
 * or nothing if the unqueue is done as part of the wake process and the unqueue
 * state is implicit in the state of woken task (see futex_wait_requeue_pi() for
 * an example).
 */
static inline void queue_me(struct futex_q *q, struct futex_hash_bucket *hb)
	__releases(&hb->lock)
{
	int prio;

	/*
	 * The priority used to register this element is
	 * - either the real thread-priority for the real-time threads
	 * (i.e. threads with a priority lower than MAX_RT_PRIO)
	 * - or MAX_RT_PRIO for non-RT threads.
	 * Thus, all RT-threads are woken first in priority order, and
	 * the others are woken last, in FIFO order.
	 */
	prio = min(current->normal_prio, MAX_RT_PRIO);

	plist_node_init(&q->list, prio);
	plist_add(&q->list, &hb->chain);
	q->task = current;
	spin_unlock(&hb->lock);
}

/**
 * unqueue_me() - Remove the futex_q from its futex_hash_bucket
 * @q:	The futex_q to unqueue
 *
 * The q->lock_ptr must not be held by the caller. A call to unqueue_me() must
 * be paired with exactly one earlier call to queue_me().
 *
 * Return:
 *   1 - if the futex_q was still queued (and we removed unqueued it);
 *   0 - if the futex_q was already removed by the waking thread
 */
static int unqueue_me(struct futex_q *q)
{
	spinlock_t *lock_ptr;
	int ret = 0;

	/* In the common case we don't take the spinlock, which is nice. */
retry:
	/*
	 * q->lock_ptr can change between this read and the following spin_lock.
	 * Use READ_ONCE to forbid the compiler from reloading q->lock_ptr and
	 * optimizing lock_ptr out of the logic below.
	 */
	lock_ptr = READ_ONCE(q->lock_ptr);
	if (lock_ptr != NULL) {
		spin_lock(lock_ptr);
		/*
		 * q->lock_ptr can change between reading it and
		 * spin_lock(), causing us to take the wrong lock.  This
		 * corrects the race condition.
		 *
		 * Reasoning goes like this: if we have the wrong lock,
		 * q->lock_ptr must have changed (maybe several times)
		 * between reading it and the spin_lock().  It can
		 * change again after the spin_lock() but only if it was
		 * already changed before the spin_lock().  It cannot,
		 * however, change back to the original value.  Therefore
		 * we can detect whether we acquired the correct lock.
		 */
		if (unlikely(lock_ptr != q->lock_ptr)) {
			spin_unlock(lock_ptr);
			goto retry;
		}
		__unqueue_futex(q);

		BUG_ON(q->pi_state);

		spin_unlock(lock_ptr);
		ret = 1;
	}

	drop_futex_key_refs(&q->key);
	return ret;
}

/*
 * PI futexes can not be requeued and must remove themself from the
 * hash bucket. The hash bucket lock (i.e. lock_ptr) is held on entry
 * and dropped here.
 */
static void unqueue_me_pi(struct futex_q *q)
	__releases(q->lock_ptr)
{
	__unqueue_futex(q);

	BUG_ON(!q->pi_state);
	put_pi_state(q->pi_state);
	q->pi_state = NULL;

	spin_unlock(q->lock_ptr);
}

/*
 * Fixup the pi_state owner with the new owner.
 *
 * Must be called with hash bucket lock held and mm->sem held for non
 * private futexes.
 */
static int fixup_pi_state_owner(u32 __user *uaddr, struct futex_q *q,
				struct task_struct *newowner)
{
	u32 newtid = task_pid_vnr(newowner) | FUTEX_WAITERS;
	struct futex_pi_state *pi_state = q->pi_state;
	struct task_struct *oldowner = pi_state->owner;
	u32 uval, uninitialized_var(curval), newval;
	int ret;

	/* Owner died? */
	if (!pi_state->owner)
		newtid |= FUTEX_OWNER_DIED;

	/*
	 * We are here either because we stole the rtmutex from the
	 * previous highest priority waiter or we are the highest priority
	 * waiter but failed to get the rtmutex the first time.
	 * We have to replace the newowner TID in the user space variable.
	 * This must be atomic as we have to preserve the owner died bit here.
	 *
	 * Note: We write the user space value _before_ changing the pi_state
	 * because we can fault here. Imagine swapped out pages or a fork
	 * that marked all the anonymous memory readonly for cow.
	 *
	 * Modifying pi_state _before_ the user space value would
	 * leave the pi_state in an inconsistent state when we fault
	 * here, because we need to drop the hash bucket lock to
	 * handle the fault. This might be observed in the PID check
	 * in lookup_pi_state.
	 */
retry:
	if (get_futex_value_locked(&uval, uaddr))
		goto handle_fault;

	while (1) {
		newval = (uval & FUTEX_OWNER_DIED) | newtid;

		if (cmpxchg_futex_value_locked(&curval, uaddr, uval, newval))
			goto handle_fault;
		if (curval == uval)
			break;
		uval = curval;
	}

	/*
	 * We fixed up user space. Now we need to fix the pi_state
	 * itself.
	 */
	if (pi_state->owner != NULL) {
		raw_spin_lock_irq(&pi_state->owner->pi_lock);
		WARN_ON(list_empty(&pi_state->list));
		list_del_init(&pi_state->list);
		raw_spin_unlock_irq(&pi_state->owner->pi_lock);
	}

	pi_state->owner = newowner;

	raw_spin_lock_irq(&newowner->pi_lock);
	WARN_ON(!list_empty(&pi_state->list));
	list_add(&pi_state->list, &newowner->pi_state_list);
	raw_spin_unlock_irq(&newowner->pi_lock);
	return 0;

	/*
	 * To handle the page fault we need to drop the hash bucket
	 * lock here. That gives the other task (either the highest priority
	 * waiter itself or the task which stole the rtmutex) the
	 * chance to try the fixup of the pi_state. So once we are
	 * back from handling the fault we need to check the pi_state
	 * after reacquiring the hash bucket lock and before trying to
	 * do another fixup. When the fixup has been done already we
	 * simply return.
	 */
handle_fault:
	spin_unlock(q->lock_ptr);

	ret = fault_in_user_writeable(uaddr);

	spin_lock(q->lock_ptr);

	/*
	 * Check if someone else fixed it for us:
	 */
	if (pi_state->owner != oldowner)
		return 0;

	if (ret)
		return ret;

	goto retry;
}

static long futex_wait_restart(struct restart_block *restart);

/**
 * fixup_owner() - Post lock pi_state and corner case management
 * @uaddr:	user address of the futex
 * @q:		futex_q (contains pi_state and access to the rt_mutex)
 * @locked:	if the attempt to take the rt_mutex succeeded (1) or not (0)
 *
 * After attempting to lock an rt_mutex, this function is called to cleanup
 * the pi_state owner as well as handle race conditions that may allow us to
 * acquire the lock. Must be called with the hb lock held.
 *
 * Return:
 *  1 - success, lock taken;
 *  0 - success, lock not taken;
 * <0 - on error (-EFAULT)
 */
static int fixup_owner(u32 __user *uaddr, struct futex_q *q, int locked)
{
	struct task_struct *owner;
	int ret = 0;

	if (locked) {
		/*
		 * Got the lock. We might not be the anticipated owner if we
		 * did a lock-steal - fix up the PI-state in that case:
		 */
		if (q->pi_state->owner != current)
			ret = fixup_pi_state_owner(uaddr, q, current);
		goto out;
	}

	/*
	 * Catch the rare case, where the lock was released when we were on the
	 * way back before we locked the hash bucket.
	 */
	if (q->pi_state->owner == current) {
		/*
		 * Try to get the rt_mutex now. This might fail as some other
		 * task acquired the rt_mutex after we removed ourself from the
		 * rt_mutex waiters list.
		 */
		if (rt_mutex_trylock(&q->pi_state->pi_mutex)) {
			locked = 1;
			goto out;
		}

		/*
		 * pi_state is incorrect, some other task did a lock steal and
		 * we returned due to timeout or signal without taking the
		 * rt_mutex. Too late.
		 */
		raw_spin_lock_irq(&q->pi_state->pi_mutex.wait_lock);
		owner = rt_mutex_owner(&q->pi_state->pi_mutex);
		if (!owner)
			owner = rt_mutex_next_owner(&q->pi_state->pi_mutex);
		raw_spin_unlock_irq(&q->pi_state->pi_mutex.wait_lock);
		ret = fixup_pi_state_owner(uaddr, q, owner);
		goto out;
	}

	/*
	 * Paranoia check. If we did not take the lock, then we should not be
	 * the owner of the rt_mutex.
	 */
	if (rt_mutex_owner(&q->pi_state->pi_mutex) == current)
		printk(KERN_ERR "fixup_owner: ret = %d pi-mutex: %p "
				"pi-state %p\n", ret,
				q->pi_state->pi_mutex.owner,
				q->pi_state->owner);

out:
	return ret ? ret : locked;
}

/**
 * futex_wait_queue_me() - queue_me() and wait for wakeup, timeout, or signal
 * @hb:		the futex hash bucket, must be locked by the caller
 * @q:		the futex_q to queue up on
 * @timeout:	the prepared hrtimer_sleeper, or null for no timeout
 */
static void futex_wait_queue_me(struct futex_hash_bucket *hb, struct futex_q *q,
				struct hrtimer_sleeper *timeout)
{
	/*
	 * The task state is guaranteed to be set before another task can
	 * wake it. set_current_state() is implemented using smp_store_mb() and
	 * queue_me() calls spin_unlock() upon completion, both serializing
	 * access to the hash list and forcing another memory barrier.
	 */
	set_current_state(TASK_INTERRUPTIBLE);
	queue_me(q, hb);

	/* Arm the timer */
	if (timeout)
		hrtimer_start_expires(&timeout->timer, HRTIMER_MODE_ABS);

	/*
	 * If we have been removed from the hash list, then another task
	 * has tried to wake us, and we can skip the call to schedule().
	 */
	if (likely(!plist_node_empty(&q->list))) {
		/*
		 * If the timer has already expired, current will already be
		 * flagged for rescheduling. Only call schedule if there
		 * is no timeout, or if it has yet to expire.
		 */
		if (!timeout || timeout->task)
			freezable_schedule();
	}
	__set_current_state(TASK_RUNNING);
}

/**
 * futex_wait_setup() - Prepare to wait on a futex
 * @uaddr:	the futex userspace address
 * @val:	the expected value
 * @flags:	futex flags (FLAGS_SHARED, etc.)
 * @q:		the associated futex_q
 * @hb:		storage for hash_bucket pointer to be returned to caller
 *
 * Setup the futex_q and locate the hash_bucket.  Get the futex value and
 * compare it with the expected value.  Handle atomic faults internally.
 * Return with the hb lock held and a q.key reference on success, and unlocked
 * with no q.key reference on failure.
 *
 * Return:
 *  0 - uaddr contains val and hb has been locked;
 * <1 - -EFAULT or -EWOULDBLOCK (uaddr does not contain val) and hb is unlocked
 */
static int futex_wait_setup(u32 __user *uaddr, u32 val, unsigned int flags,
			   struct futex_q *q, struct futex_hash_bucket **hb)
{
	u32 uval;
	int ret;

	/*
	 * Access the page AFTER the hash-bucket is locked.
	 * Order is important:
	 *
	 *   Userspace waiter: val = var; if (cond(val)) futex_wait(&var, val);
	 *   Userspace waker:  if (cond(var)) { var = new; futex_wake(&var); }
	 *
	 * The basic logical guarantee of a futex is that it blocks ONLY
	 * if cond(var) is known to be true at the time of blocking, for
	 * any cond.  If we locked the hash-bucket after testing *uaddr, that
	 * would open a race condition where we could block indefinitely with
	 * cond(var) false, which would violate the guarantee.
	 *
	 * On the other hand, we insert q and release the hash-bucket only
	 * after testing *uaddr.  This guarantees that futex_wait() will NOT
	 * absorb a wakeup if *uaddr does not match the desired values
	 * while the syscall executes.
	 */
retry:
	ret = get_futex_key(uaddr, flags & FLAGS_SHARED, &q->key, VERIFY_READ);
	if (unlikely(ret != 0))
		return ret;

retry_private:
	*hb = queue_lock(q);

	ret = get_futex_value_locked(&uval, uaddr);

	if (ret) {
		queue_unlock(*hb);

		ret = get_user(uval, uaddr);
		if (ret)
			goto out;

		if (!(flags & FLAGS_SHARED))
			goto retry_private;

		put_futex_key(&q->key);
		goto retry;
	}

	if (uval != val) {
		queue_unlock(*hb);
		ret = -EWOULDBLOCK;
	}

out:
	if (ret)
		put_futex_key(&q->key);
	return ret;
}

static int futex_wait(u32 __user *uaddr, unsigned int flags, u32 val,
		      ktime_t *abs_time, u32 bitset)
{
	struct hrtimer_sleeper timeout, *to = NULL;
	struct restart_block *restart;
	struct futex_hash_bucket *hb;
	struct futex_q q = futex_q_init;
	int ret;

	if (!bitset)
		return -EINVAL;
	q.bitset = bitset;

	if (abs_time) {
		to = &timeout;

		hrtimer_init_on_stack(&to->timer, (flags & FLAGS_CLOCKRT) ?
				      CLOCK_REALTIME : CLOCK_MONOTONIC,
				      HRTIMER_MODE_ABS);
		hrtimer_init_sleeper(to, current);
		hrtimer_set_expires_range_ns(&to->timer, *abs_time,
					     current->timer_slack_ns);
	}

retry:
	/*
	 * Prepare to wait on uaddr. On success, holds hb lock and increments
	 * q.key refs.
	 */
	ret = futex_wait_setup(uaddr, val, flags, &q, &hb);
	if (ret)
		goto out;

	/* queue_me and wait for wakeup, timeout, or a signal. */
	futex_wait_queue_me(hb, &q, to);

	/* If we were woken (and unqueued), we succeeded, whatever. */
	ret = 0;
	/* unqueue_me() drops q.key ref */
	if (!unqueue_me(&q))
		goto out;
	ret = -ETIMEDOUT;
	if (to && !to->task)
		goto out;

	/*
	 * We expect signal_pending(current), but we might be the
	 * victim of a spurious wakeup as well.
	 */
	if (!signal_pending(current))
		goto retry;

	ret = -ERESTARTSYS;
	if (!abs_time)
		goto out;

	restart = &current->restart_block;
	restart->fn = futex_wait_restart;
	restart->futex.uaddr = uaddr;
	restart->futex.val = val;
	restart->futex.time = *abs_time;
	restart->futex.bitset = bitset;
	restart->futex.flags = flags | FLAGS_HAS_TIMEOUT;

	ret = -ERESTART_RESTARTBLOCK;

out:
	if (to) {
		hrtimer_cancel(&to->timer);
		destroy_hrtimer_on_stack(&to->timer);
	}
	return ret;
}


static long futex_wait_restart(struct restart_block *restart)
{
	u32 __user *uaddr = restart->futex.uaddr;
	ktime_t t, *tp = NULL;

	if (restart->futex.flags & FLAGS_HAS_TIMEOUT) {
		t = restart->futex.time;
		tp = &t;
	}
	restart->fn = do_no_restart_syscall;

	return (long)futex_wait(uaddr, restart->futex.flags,
				restart->futex.val, tp, restart->futex.bitset);
}


/*
 * Userspace tried a 0 -> TID atomic transition of the futex value
 * and failed. The kernel side here does the whole locking operation:
 * if there are waiters then it will block as a consequence of relying
 * on rt-mutexes, it does PI, etc. (Due to races the kernel might see
 * a 0 value of the futex too.).
 *
 * Also serves as futex trylock_pi()'ing, and due semantics.
 */
static int futex_lock_pi(u32 __user *uaddr, unsigned int flags,
			 ktime_t *time, int trylock)
{
	struct hrtimer_sleeper timeout, *to = NULL;
	struct futex_hash_bucket *hb;
	struct futex_q q = futex_q_init;
	int res, ret;

	if (refill_pi_state_cache())
		return -ENOMEM;

	if (time) {
		to = &timeout;
		hrtimer_init_on_stack(&to->timer, CLOCK_REALTIME,
				      HRTIMER_MODE_ABS);
		hrtimer_init_sleeper(to, current);
		hrtimer_set_expires(&to->timer, *time);
	}

retry:
	ret = get_futex_key(uaddr, flags & FLAGS_SHARED, &q.key, VERIFY_WRITE);
	if (unlikely(ret != 0))
		goto out;

retry_private:
	hb = queue_lock(&q);

	ret = futex_lock_pi_atomic(uaddr, hb, &q.key, &q.pi_state, current, 0);
	if (unlikely(ret)) {
		/*
		 * Atomic work succeeded and we got the lock,
		 * or failed. Either way, we do _not_ block.
		 */
		switch (ret) {
		case 1:
			/* We got the lock. */
			ret = 0;
			goto out_unlock_put_key;
		case -EFAULT:
			goto uaddr_faulted;
		case -EAGAIN:
			/*
			 * Two reasons for this:
			 * - Task is exiting and we just wait for the
			 *   exit to complete.
			 * - The user space value changed.
			 */
			queue_unlock(hb);
			put_futex_key(&q.key);
			cond_resched();
			goto retry;
		default:
			goto out_unlock_put_key;
		}
	}

	/*
	 * Only actually queue now that the atomic ops are done:
	 */
	queue_me(&q, hb);

	WARN_ON(!q.pi_state);
	/*
	 * Block on the PI mutex:
	 */
	if (!trylock) {
		ret = rt_mutex_timed_futex_lock(&q.pi_state->pi_mutex, to);
	} else {
		ret = rt_mutex_trylock(&q.pi_state->pi_mutex);
		/* Fixup the trylock return value: */
		ret = ret ? 0 : -EWOULDBLOCK;
	}

	spin_lock(q.lock_ptr);
	/*
	 * Fixup the pi_state owner and possibly acquire the lock if we
	 * haven't already.
	 */
	res = fixup_owner(uaddr, &q, !ret);
	/*
	 * If fixup_owner() returned an error, proprogate that.  If it acquired
	 * the lock, clear our -ETIMEDOUT or -EINTR.
	 */
	if (res)
		ret = (res < 0) ? res : 0;

	/*
	 * If fixup_owner() faulted and was unable to handle the fault, unlock
	 * it and return the fault to userspace.
	 */
	if (ret && (rt_mutex_owner(&q.pi_state->pi_mutex) == current))
		rt_mutex_unlock(&q.pi_state->pi_mutex);

	/* Unqueue and drop the lock */
	unqueue_me_pi(&q);

	goto out_put_key;

out_unlock_put_key:
	queue_unlock(hb);

out_put_key:
	put_futex_key(&q.key);
out:
	if (to)
		destroy_hrtimer_on_stack(&to->timer);
	return ret != -EINTR ? ret : -ERESTARTNOINTR;

uaddr_faulted:
	queue_unlock(hb);

	ret = fault_in_user_writeable(uaddr);
	if (ret)
		goto out_put_key;

	if (!(flags & FLAGS_SHARED))
		goto retry_private;

	put_futex_key(&q.key);
	goto retry;
}

/*
 * Userspace attempted a TID -> 0 atomic transition, and failed.
 * This is the in-kernel slowpath: we look up the PI state (if any),
 * and do the rt-mutex unlock.
 */
static int futex_unlock_pi(u32 __user *uaddr, unsigned int flags)
{
	u32 uninitialized_var(curval), uval, vpid = task_pid_vnr(current);
	union futex_key key = FUTEX_KEY_INIT;
	struct futex_hash_bucket *hb;
	struct futex_q *match;
	int ret;

retry:
	if (get_user(uval, uaddr))
		return -EFAULT;
	/*
	 * We release only a lock we actually own:
	 */
	if ((uval & FUTEX_TID_MASK) != vpid)
		return -EPERM;

	ret = get_futex_key(uaddr, flags & FLAGS_SHARED, &key, VERIFY_WRITE);
	if (ret)
		return ret;

	hb = hash_futex(&key);
	spin_lock(&hb->lock);

	/*
	 * Check waiters first. We do not trust user space values at
	 * all and we at least want to know if user space fiddled
	 * with the futex value instead of blindly unlocking.
	 */
	match = futex_top_waiter(hb, &key);
	if (match) {
		ret = wake_futex_pi(uaddr, uval, match, hb);
		/*
		 * In case of success wake_futex_pi dropped the hash
		 * bucket lock.
		 */
		if (!ret)
			goto out_putkey;
		/*
		 * The atomic access to the futex value generated a
		 * pagefault, so retry the user-access and the wakeup:
		 */
		if (ret == -EFAULT)
			goto pi_faulted;
		/*
		 * A unconditional UNLOCK_PI op raced against a waiter
		 * setting the FUTEX_WAITERS bit. Try again.
		 */
		if (ret == -EAGAIN) {
			spin_unlock(&hb->lock);
			put_futex_key(&key);
			goto retry;
		}
		/*
		 * wake_futex_pi has detected invalid state. Tell user
		 * space.
		 */
		goto out_unlock;
	}

	/*
	 * We have no kernel internal state, i.e. no waiters in the
	 * kernel. Waiters which are about to queue themselves are stuck
	 * on hb->lock. So we can safely ignore them. We do neither
	 * preserve the WAITERS bit not the OWNER_DIED one. We are the
	 * owner.
	 */
	if (cmpxchg_futex_value_locked(&curval, uaddr, uval, 0))
		goto pi_faulted;

	/*
	 * If uval has changed, let user space handle it.
	 */
	ret = (curval == uval) ? 0 : -EAGAIN;

out_unlock:
	spin_unlock(&hb->lock);
out_putkey:
	put_futex_key(&key);
	return ret;

pi_faulted:
	spin_unlock(&hb->lock);
	put_futex_key(&key);

	ret = fault_in_user_writeable(uaddr);
	if (!ret)
		goto retry;

	return ret;
}

/**
 * handle_early_requeue_pi_wakeup() - Detect early wakeup on the initial futex
 * @hb:		the hash_bucket futex_q was original enqueued on
 * @q:		the futex_q woken while waiting to be requeued
 * @key2:	the futex_key of the requeue target futex
 * @timeout:	the timeout associated with the wait (NULL if none)
 *
 * Detect if the task was woken on the initial futex as opposed to the requeue
 * target futex.  If so, determine if it was a timeout or a signal that caused
 * the wakeup and return the appropriate error code to the caller.  Must be
 * called with the hb lock held.
 *
 * Return:
 *  0 = no early wakeup detected;
 * <0 = -ETIMEDOUT or -ERESTARTNOINTR
 */
static inline
int handle_early_requeue_pi_wakeup(struct futex_hash_bucket *hb,
				   struct futex_q *q, union futex_key *key2,
				   struct hrtimer_sleeper *timeout)
{
	int ret = 0;

	/*
	 * With the hb lock held, we avoid races while we process the wakeup.
	 * We only need to hold hb (and not hb2) to ensure atomicity as the
	 * wakeup code can't change q.key from uaddr to uaddr2 if we hold hb.
	 * It can't be requeued from uaddr2 to something else since we don't
	 * support a PI aware source futex for requeue.
	 */
	if (!match_futex(&q->key, key2)) {
		WARN_ON(q->lock_ptr && (&hb->lock != q->lock_ptr));
		/*
		 * We were woken prior to requeue by a timeout or a signal.
		 * Unqueue the futex_q and determine which it was.
		 */
		plist_del(&q->list, &hb->chain);
		hb_waiters_dec(hb);

		/* Handle spurious wakeups gracefully */
		ret = -EWOULDBLOCK;
		if (timeout && !timeout->task)
			ret = -ETIMEDOUT;
		else if (signal_pending(current))
			ret = -ERESTARTNOINTR;
	}
	return ret;
}

/**
 * futex_wait_requeue_pi() - Wait on uaddr and take uaddr2
 * @uaddr:	the futex we initially wait on (non-pi)
 * @flags:	futex flags (FLAGS_SHARED, FLAGS_CLOCKRT, etc.), they must be
 *		the same type, no requeueing from private to shared, etc.
 * @val:	the expected value of uaddr
 * @abs_time:	absolute timeout
 * @bitset:	32 bit wakeup bitset set by userspace, defaults to all
 * @uaddr2:	the pi futex we will take prior to returning to user-space
 *
 * The caller will wait on uaddr and will be requeued by futex_requeue() to
 * uaddr2 which must be PI aware and unique from uaddr.  Normal wakeup will wake
 * on uaddr2 and complete the acquisition of the rt_mutex prior to returning to
 * userspace.  This ensures the rt_mutex maintains an owner when it has waiters;
 * without one, the pi logic would not know which task to boost/deboost, if
 * there was a need to.
 *
 * We call schedule in futex_wait_queue_me() when we enqueue and return there
 * via the following--
 * 1) wakeup on uaddr2 after an atomic lock acquisition by futex_requeue()
 * 2) wakeup on uaddr2 after a requeue
 * 3) signal
 * 4) timeout
 *
 * If 3, cleanup and return -ERESTARTNOINTR.
 *
 * If 2, we may then block on trying to take the rt_mutex and return via:
 * 5) successful lock
 * 6) signal
 * 7) timeout
 * 8) other lock acquisition failure
 *
 * If 6, return -EWOULDBLOCK (restarting the syscall would do the same).
 *
 * If 4 or 7, we cleanup and return with -ETIMEDOUT.
 *
 * Return:
 *  0 - On success;
 * <0 - On error
 */
static int futex_wait_requeue_pi(u32 __user *uaddr, unsigned int flags,
				 u32 val, ktime_t *abs_time, u32 bitset,
				 u32 __user *uaddr2)
{
	struct hrtimer_sleeper timeout, *to = NULL;
	struct rt_mutex_waiter rt_waiter;
	struct rt_mutex *pi_mutex = NULL;
	struct futex_hash_bucket *hb;
	union futex_key key2 = FUTEX_KEY_INIT;
	struct futex_q q = futex_q_init;
	int res, ret;

	if (uaddr == uaddr2)
		return -EINVAL;

	if (!bitset)
		return -EINVAL;

	if (abs_time) {
		to = &timeout;
		hrtimer_init_on_stack(&to->timer, (flags & FLAGS_CLOCKRT) ?
				      CLOCK_REALTIME : CLOCK_MONOTONIC,
				      HRTIMER_MODE_ABS);
		hrtimer_init_sleeper(to, current);
		hrtimer_set_expires_range_ns(&to->timer, *abs_time,
					     current->timer_slack_ns);
	}

	/*
	 * The waiter is allocated on our stack, manipulated by the requeue
	 * code while we sleep on uaddr.
	 */
	debug_rt_mutex_init_waiter(&rt_waiter);
	RB_CLEAR_NODE(&rt_waiter.pi_tree_entry);
	RB_CLEAR_NODE(&rt_waiter.tree_entry);
	rt_waiter.task = NULL;

	ret = get_futex_key(uaddr2, flags & FLAGS_SHARED, &key2, VERIFY_WRITE);
	if (unlikely(ret != 0))
		goto out;

	q.bitset = bitset;
	q.rt_waiter = &rt_waiter;
	q.requeue_pi_key = &key2;

	/*
	 * Prepare to wait on uaddr. On success, increments q.key (key1) ref
	 * count.
	 */
	ret = futex_wait_setup(uaddr, val, flags, &q, &hb);
	if (ret)
		goto out_key2;

	/*
	 * The check above which compares uaddrs is not sufficient for
	 * shared futexes. We need to compare the keys:
	 */
	if (match_futex(&q.key, &key2)) {
		queue_unlock(hb);
		ret = -EINVAL;
		goto out_put_keys;
	}

	/* Queue the futex_q, drop the hb lock, wait for wakeup. */
	futex_wait_queue_me(hb, &q, to);

	spin_lock(&hb->lock);
	ret = handle_early_requeue_pi_wakeup(hb, &q, &key2, to);
	spin_unlock(&hb->lock);
	if (ret)
		goto out_put_keys;

	/*
	 * In order for us to be here, we know our q.key == key2, and since
	 * we took the hb->lock above, we also know that futex_requeue() has
	 * completed and we no longer have to concern ourselves with a wakeup
	 * race with the atomic proxy lock acquisition by the requeue code. The
	 * futex_requeue dropped our key1 reference and incremented our key2
	 * reference count.
	 */

	/* Check if the requeue code acquired the second futex for us. */
	if (!q.rt_waiter) {
		/*
		 * Got the lock. We might not be the anticipated owner if we
		 * did a lock-steal - fix up the PI-state in that case.
		 */
		if (q.pi_state && (q.pi_state->owner != current)) {
			spin_lock(q.lock_ptr);
			ret = fixup_pi_state_owner(uaddr2, &q, current);
			/*
			 * Drop the reference to the pi state which
			 * the requeue_pi() code acquired for us.
			 */
			put_pi_state(q.pi_state);
			spin_unlock(q.lock_ptr);
		}
	} else {
		/*
		 * We have been woken up by futex_unlock_pi(), a timeout, or a
		 * signal.  futex_unlock_pi() will not destroy the lock_ptr nor
		 * the pi_state.
		 */
		WARN_ON(!q.pi_state);
		pi_mutex = &q.pi_state->pi_mutex;
		ret = rt_mutex_finish_proxy_lock(pi_mutex, to, &rt_waiter);
		debug_rt_mutex_free_waiter(&rt_waiter);

		spin_lock(q.lock_ptr);
		/*
		 * Fixup the pi_state owner and possibly acquire the lock if we
		 * haven't already.
		 */
		res = fixup_owner(uaddr2, &q, !ret);
		/*
		 * If fixup_owner() returned an error, proprogate that.  If it
		 * acquired the lock, clear -ETIMEDOUT or -EINTR.
		 */
		if (res)
			ret = (res < 0) ? res : 0;

		/* Unqueue and drop the lock. */
		unqueue_me_pi(&q);
	}

	/*
	 * If fixup_pi_state_owner() faulted and was unable to handle the
	 * fault, unlock the rt_mutex and return the fault to userspace.
	 */
	if (ret == -EFAULT) {
		if (pi_mutex && rt_mutex_owner(pi_mutex) == current)
			rt_mutex_unlock(pi_mutex);
	} else if (ret == -EINTR) {
		/*
		 * We've already been requeued, but cannot restart by calling
		 * futex_lock_pi() directly. We could restart this syscall, but
		 * it would detect that the user space "val" changed and return
		 * -EWOULDBLOCK.  Save the overhead of the restart and return
		 * -EWOULDBLOCK directly.
		 */
		ret = -EWOULDBLOCK;
	}

out_put_keys:
	put_futex_key(&q.key);
out_key2:
	put_futex_key(&key2);

out:
	if (to) {
		hrtimer_cancel(&to->timer);
		destroy_hrtimer_on_stack(&to->timer);
	}
	return ret;
}

/*
 * Support for robust futexes: the kernel cleans up held futexes at
 * thread exit time.
 *
 * Implementation: user-space maintains a per-thread list of locks it
 * is holding. Upon do_exit(), the kernel carefully walks this list,
 * and marks all locks that are owned by this thread with the
 * FUTEX_OWNER_DIED bit, and wakes up a waiter (if any). The list is
 * always manipulated with the lock held, so the list is private and
 * per-thread. Userspace also maintains a per-thread 'list_op_pending'
 * field, to allow the kernel to clean up if the thread dies after
 * acquiring the lock, but just before it could have added itself to
 * the list. There can only be one such pending lock.
 */

/**
 * sys_set_robust_list() - Set the robust-futex list head of a task
 * @head:	pointer to the list-head
 * @len:	length of the list-head, as userspace expects
 */
SYSCALL_DEFINE2(set_robust_list, struct robust_list_head __user *, head,
		size_t, len)
{
	if (!futex_cmpxchg_enabled)
		return -ENOSYS;
	/*
	 * The kernel knows only one size for now:
	 */
	if (unlikely(len != sizeof(*head)))
		return -EINVAL;

	current->robust_list = head;

	return 0;
}

/**
 * sys_get_robust_list() - Get the robust-futex list head of a task
 * @pid:	pid of the process [zero for current task]
 * @head_ptr:	pointer to a list-head pointer, the kernel fills it in
 * @len_ptr:	pointer to a length field, the kernel fills in the header size
 */
SYSCALL_DEFINE3(get_robust_list, int, pid,
		struct robust_list_head __user * __user *, head_ptr,
		size_t __user *, len_ptr)
{
	struct robust_list_head __user *head;
	unsigned long ret;
	struct task_struct *p;

	if (!futex_cmpxchg_enabled)
		return -ENOSYS;

	rcu_read_lock();

	ret = -ESRCH;
	if (!pid)
		p = current;
	else {
		p = find_task_by_vpid(pid);
		if (!p)
			goto err_unlock;
	}

	ret = -EPERM;
	if (!ptrace_may_access(p, PTRACE_MODE_READ_REALCREDS))
		goto err_unlock;

	head = p->robust_list;
	rcu_read_unlock();

	if (put_user(sizeof(*head), len_ptr))
		return -EFAULT;
	return put_user(head, head_ptr);

err_unlock:
	rcu_read_unlock();

	return ret;
}

/*
 * Process a futex-list entry, check whether it's owned by the
 * dying task, and do notification if so:
 */
int handle_futex_death(u32 __user *uaddr, struct task_struct *curr, int pi)
{
	u32 uval, uninitialized_var(nval), mval;

retry:
	if (get_user(uval, uaddr))
		return -1;

	if ((uval & FUTEX_TID_MASK) == task_pid_vnr(curr)) {
		/*
		 * Ok, this dying thread is truly holding a futex
		 * of interest. Set the OWNER_DIED bit atomically
		 * via cmpxchg, and if the value had FUTEX_WAITERS
		 * set, wake up a waiter (if any). (We have to do a
		 * futex_wake() even if OWNER_DIED is already set -
		 * to handle the rare but possible case of recursive
		 * thread-death.) The rest of the cleanup is done in
		 * userspace.
		 */
		mval = (uval & FUTEX_WAITERS) | FUTEX_OWNER_DIED;
		/*
		 * We are not holding a lock here, but we want to have
		 * the pagefault_disable/enable() protection because
		 * we want to handle the fault gracefully. If the
		 * access fails we try to fault in the futex with R/W
		 * verification via get_user_pages. get_user() above
		 * does not guarantee R/W access. If that fails we
		 * give up and leave the futex locked.
		 */
		if (cmpxchg_futex_value_locked(&nval, uaddr, uval, mval)) {
			if (fault_in_user_writeable(uaddr))
				return -1;
			goto retry;
		}
		if (nval != uval)
			goto retry;

		/*
		 * Wake robust non-PI futexes here. The wakeup of
		 * PI futexes happens in exit_pi_state():
		 */
		if (!pi && (uval & FUTEX_WAITERS))
			futex_wake(uaddr, 1, 1, FUTEX_BITSET_MATCH_ANY);
	}
	return 0;
}

/*
 * Fetch a robust-list pointer. Bit 0 signals PI futexes:
 */
static inline int fetch_robust_entry(struct robust_list __user **entry,
				     struct robust_list __user * __user *head,
				     unsigned int *pi)
{
	unsigned long uentry;

	if (get_user(uentry, (unsigned long __user *)head))
		return -EFAULT;

	*entry = (void __user *)(uentry & ~1UL);
	*pi = uentry & 1;

	return 0;
}

/*
 * Walk curr->robust_list (very carefully, it's a userspace list!)
 * and mark any locks found there dead, and notify any waiters.
 *
 * We silently return on any sign of list-walking problem.
 */
void exit_robust_list(struct task_struct *curr)
{
	struct robust_list_head __user *head = curr->robust_list;
	struct robust_list __user *entry, *next_entry, *pending;
	unsigned int limit = ROBUST_LIST_LIMIT, pi, pip;
	unsigned int uninitialized_var(next_pi);
	unsigned long futex_offset;
	int rc;

	if (!futex_cmpxchg_enabled)
		return;

	/*
	 * Fetch the list head (which was registered earlier, via
	 * sys_set_robust_list()):
	 */
	if (fetch_robust_entry(&entry, &head->list.next, &pi))
		return;
	/*
	 * Fetch the relative futex offset:
	 */
	if (get_user(futex_offset, &head->futex_offset))
		return;
	/*
	 * Fetch any possibly pending lock-add first, and handle it
	 * if it exists:
	 */
	if (fetch_robust_entry(&pending, &head->list_op_pending, &pip))
		return;

	next_entry = NULL;	/* avoid warning with gcc */
	while (entry != &head->list) {
		/*
		 * Fetch the next entry in the list before calling
		 * handle_futex_death:
		 */
		rc = fetch_robust_entry(&next_entry, &entry->next, &next_pi);
		/*
		 * A pending lock might already be on the list, so
		 * don't process it twice:
		 */
		if (entry != pending)
			if (handle_futex_death((void __user *)entry + futex_offset,
						curr, pi))
				return;
		if (rc)
			return;
		entry = next_entry;
		pi = next_pi;
		/*
		 * Avoid excessively long or circular lists:
		 */
		if (!--limit)
			break;

		cond_resched();
	}

	if (pending)
		handle_futex_death((void __user *)pending + futex_offset,
				   curr, pip);
}

long do_futex(u32 __user *uaddr, int op, u32 val, ktime_t *timeout,
		u32 __user *uaddr2, u32 val2, u32 val3)
{
	int cmd = op & FUTEX_CMD_MASK;
	unsigned int flags = 0;

	if (!(op & FUTEX_PRIVATE_FLAG))
		flags |= FLAGS_SHARED;

	if (op & FUTEX_CLOCK_REALTIME) {
		flags |= FLAGS_CLOCKRT;
		if (cmd != FUTEX_WAIT && cmd != FUTEX_WAIT_BITSET && \
		    cmd != FUTEX_WAIT_REQUEUE_PI)
			return -ENOSYS;
	}

	switch (cmd) {
	case FUTEX_LOCK_PI:
	case FUTEX_UNLOCK_PI:
	case FUTEX_TRYLOCK_PI:
	case FUTEX_WAIT_REQUEUE_PI:
	case FUTEX_CMP_REQUEUE_PI:
		if (!futex_cmpxchg_enabled)
			return -ENOSYS;
	}

	switch (cmd) {
	case FUTEX_WAIT:
		val3 = FUTEX_BITSET_MATCH_ANY;
	case FUTEX_WAIT_BITSET:
		return futex_wait(uaddr, flags, val, timeout, val3);
	case FUTEX_WAKE:
		val3 = FUTEX_BITSET_MATCH_ANY;
	case FUTEX_WAKE_BITSET:
		return futex_wake(uaddr, flags, val, val3);
	case FUTEX_REQUEUE:
		return futex_requeue(uaddr, flags, uaddr2, val, val2, NULL, 0);
	case FUTEX_CMP_REQUEUE:
		return futex_requeue(uaddr, flags, uaddr2, val, val2, &val3, 0);
	case FUTEX_WAKE_OP:
		return futex_wake_op(uaddr, flags, uaddr2, val, val2, val3);
	case FUTEX_LOCK_PI:
		return futex_lock_pi(uaddr, flags, timeout, 0);
	case FUTEX_UNLOCK_PI:
		return futex_unlock_pi(uaddr, flags);
	case FUTEX_TRYLOCK_PI:
		return futex_lock_pi(uaddr, flags, NULL, 1);
	case FUTEX_WAIT_REQUEUE_PI:
		val3 = FUTEX_BITSET_MATCH_ANY;
		return futex_wait_requeue_pi(uaddr, flags, val, timeout, val3,
					     uaddr2);
	case FUTEX_CMP_REQUEUE_PI:
		return futex_requeue(uaddr, flags, uaddr2, val, val2, &val3, 1);
	}
	return -ENOSYS;
}


SYSCALL_DEFINE6(futex, u32 __user *, uaddr, int, op, u32, val,
		struct timespec __user *, utime, u32 __user *, uaddr2,
		u32, val3)
{
	struct timespec ts;
	ktime_t t, *tp = NULL;
	u32 val2 = 0;
	int cmd = op & FUTEX_CMD_MASK;

	if (utime && (cmd == FUTEX_WAIT || cmd == FUTEX_LOCK_PI ||
		      cmd == FUTEX_WAIT_BITSET ||
		      cmd == FUTEX_WAIT_REQUEUE_PI)) {
		if (unlikely(should_fail_futex(!(op & FUTEX_PRIVATE_FLAG))))
			return -EFAULT;
		if (copy_from_user(&ts, utime, sizeof(ts)) != 0)
			return -EFAULT;
		if (!timespec_valid(&ts))
			return -EINVAL;

		t = timespec_to_ktime(ts);
		if (cmd == FUTEX_WAIT)
			t = ktime_add_safe(ktime_get(), t);
		tp = &t;
	}
	/*
	 * requeue parameter in 'utime' if cmd == FUTEX_*_REQUEUE_*.
	 * number of waiters to wake in 'utime' if cmd == FUTEX_WAKE_OP.
	 */
	if (cmd == FUTEX_REQUEUE || cmd == FUTEX_CMP_REQUEUE ||
	    cmd == FUTEX_CMP_REQUEUE_PI || cmd == FUTEX_WAKE_OP)
		val2 = (u32) (unsigned long) utime;

	return do_futex(uaddr, op, val, tp, uaddr2, val2, val3);
}

static void __init futex_detect_cmpxchg(void)
{
#ifndef CONFIG_HAVE_FUTEX_CMPXCHG
	u32 curval;

	/*
	 * This will fail and we want it. Some arch implementations do
	 * runtime detection of the futex_atomic_cmpxchg_inatomic()
	 * functionality. We want to know that before we call in any
	 * of the complex code paths. Also we want to prevent
	 * registration of robust lists in that case. NULL is
	 * guaranteed to fault and we get -EFAULT on functional
	 * implementation, the non-functional ones will return
	 * -ENOSYS.
	 */
	if (cmpxchg_futex_value_locked(&curval, NULL, 0, 0) == -EFAULT)
		futex_cmpxchg_enabled = 1;
#endif
}

static int __init futex_init(void)
{
	unsigned int futex_shift;
	unsigned long i;

#if CONFIG_BASE_SMALL
	futex_hashsize = 16;
#else
	futex_hashsize = roundup_pow_of_two(256 * num_possible_cpus());
#endif

	futex_queues = alloc_large_system_hash("futex", sizeof(*futex_queues),
					       futex_hashsize, 0,
					       futex_hashsize < 256 ? HASH_SMALL : 0,
					       &futex_shift, NULL,
					       futex_hashsize, futex_hashsize);
	futex_hashsize = 1UL << futex_shift;

	futex_detect_cmpxchg();

	for (i = 0; i < futex_hashsize; i++) {
		atomic_set(&futex_queues[i].waiters, 0);
		plist_head_init(&futex_queues[i].chain);
		spin_lock_init(&futex_queues[i].lock);
	}

	return 0;
}
core_initcall(futex_init);
