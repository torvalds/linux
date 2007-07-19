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
#include <linux/module.h>
#include <asm/futex.h>

#include "rtmutex_common.h"

#define FUTEX_HASHBITS (CONFIG_BASE_SMALL ? 4 : 8)

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

/*
 * We use this hashed waitqueue instead of a normal wait_queue_t, so
 * we can wake only the relevant ones (hashed queues may be shared).
 *
 * A futex_q has a woken state, just like tasks have TASK_RUNNING.
 * It is considered woken when plist_node_empty(&q->list) || q->lock_ptr == 0.
 * The order of wakup is always to make the first condition true, then
 * wake up q->waiters, then make the second condition true.
 */
struct futex_q {
	struct plist_node list;
	wait_queue_head_t waiters;

	/* Which hash list lock to use: */
	spinlock_t *lock_ptr;

	/* Key which the futex is hashed on: */
	union futex_key key;

	/* For fd, sigio sent using these: */
	int fd;
	struct file *filp;

	/* Optional priority inheritance state: */
	struct futex_pi_state *pi_state;
	struct task_struct *task;
};

/*
 * Split the global futex_lock into every hash list lock.
 */
struct futex_hash_bucket {
	spinlock_t lock;
	struct plist_head chain;
};

static struct futex_hash_bucket futex_queues[1<<FUTEX_HASHBITS];

/* Futex-fs vfsmount entry: */
static struct vfsmount *futex_mnt;

/*
 * Take mm->mmap_sem, when futex is shared
 */
static inline void futex_lock_mm(struct rw_semaphore *fshared)
{
	if (fshared)
		down_read(fshared);
}

/*
 * Release mm->mmap_sem, when the futex is shared
 */
static inline void futex_unlock_mm(struct rw_semaphore *fshared)
{
	if (fshared)
		up_read(fshared);
}

/*
 * We hash on the keys returned from get_futex_key (see below).
 */
static struct futex_hash_bucket *hash_futex(union futex_key *key)
{
	u32 hash = jhash2((u32*)&key->both.word,
			  (sizeof(key->both.word)+sizeof(key->both.ptr))/4,
			  key->both.offset);
	return &futex_queues[hash & ((1 << FUTEX_HASHBITS)-1)];
}

/*
 * Return 1 if two futex_keys are equal, 0 otherwise.
 */
static inline int match_futex(union futex_key *key1, union futex_key *key2)
{
	return (key1->both.word == key2->both.word
		&& key1->both.ptr == key2->both.ptr
		&& key1->both.offset == key2->both.offset);
}

/**
 * get_futex_key - Get parameters which are the keys for a futex.
 * @uaddr: virtual address of the futex
 * @shared: NULL for a PROCESS_PRIVATE futex,
 *	&current->mm->mmap_sem for a PROCESS_SHARED futex
 * @key: address where result is stored.
 *
 * Returns a negative error code or 0
 * The key words are stored in *key on success.
 *
 * For shared mappings, it's (page->index, vma->vm_file->f_path.dentry->d_inode,
 * offset_within_page).  For private mappings, it's (uaddr, current->mm).
 * We can usually work out the index without swapping in the page.
 *
 * fshared is NULL for PROCESS_PRIVATE futexes
 * For other futexes, it points to &current->mm->mmap_sem and
 * caller must have taken the reader lock. but NOT any spinlocks.
 */
int get_futex_key(u32 __user *uaddr, struct rw_semaphore *fshared,
		  union futex_key *key)
{
	unsigned long address = (unsigned long)uaddr;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	struct page *page;
	int err;

	/*
	 * The futex address must be "naturally" aligned.
	 */
	key->both.offset = address % PAGE_SIZE;
	if (unlikely((address % sizeof(u32)) != 0))
		return -EINVAL;
	address -= key->both.offset;

	/*
	 * PROCESS_PRIVATE futexes are fast.
	 * As the mm cannot disappear under us and the 'key' only needs
	 * virtual address, we dont even have to find the underlying vma.
	 * Note : We do have to check 'uaddr' is a valid user address,
	 *        but access_ok() should be faster than find_vma()
	 */
	if (!fshared) {
		if (unlikely(!access_ok(VERIFY_WRITE, uaddr, sizeof(u32))))
			return -EFAULT;
		key->private.mm = mm;
		key->private.address = address;
		return 0;
	}
	/*
	 * The futex is hashed differently depending on whether
	 * it's in a shared or private mapping.  So check vma first.
	 */
	vma = find_extend_vma(mm, address);
	if (unlikely(!vma))
		return -EFAULT;

	/*
	 * Permissions.
	 */
	if (unlikely((vma->vm_flags & (VM_IO|VM_READ)) != VM_READ))
		return (vma->vm_flags & VM_IO) ? -EPERM : -EACCES;

	/*
	 * Private mappings are handled in a simple way.
	 *
	 * NOTE: When userspace waits on a MAP_SHARED mapping, even if
	 * it's a read-only handle, it's expected that futexes attach to
	 * the object not the particular process.  Therefore we use
	 * VM_MAYSHARE here, not VM_SHARED which is restricted to shared
	 * mappings of _writable_ handles.
	 */
	if (likely(!(vma->vm_flags & VM_MAYSHARE))) {
		key->both.offset |= FUT_OFF_MMSHARED; /* reference taken on mm */
		key->private.mm = mm;
		key->private.address = address;
		return 0;
	}

	/*
	 * Linear file mappings are also simple.
	 */
	key->shared.inode = vma->vm_file->f_path.dentry->d_inode;
	key->both.offset |= FUT_OFF_INODE; /* inode-based key. */
	if (likely(!(vma->vm_flags & VM_NONLINEAR))) {
		key->shared.pgoff = (((address - vma->vm_start) >> PAGE_SHIFT)
				     + vma->vm_pgoff);
		return 0;
	}

	/*
	 * We could walk the page table to read the non-linear
	 * pte, and get the page index without fetching the page
	 * from swap.  But that's a lot of code to duplicate here
	 * for a rare case, so we simply fetch the page.
	 */
	err = get_user_pages(current, mm, address, 1, 0, 0, &page, NULL);
	if (err >= 0) {
		key->shared.pgoff =
			page->index << (PAGE_CACHE_SHIFT - PAGE_SHIFT);
		put_page(page);
		return 0;
	}
	return err;
}
EXPORT_SYMBOL_GPL(get_futex_key);

/*
 * Take a reference to the resource addressed by a key.
 * Can be called while holding spinlocks.
 *
 */
inline void get_futex_key_refs(union futex_key *key)
{
	if (key->both.ptr == 0)
		return;
	switch (key->both.offset & (FUT_OFF_INODE|FUT_OFF_MMSHARED)) {
		case FUT_OFF_INODE:
			atomic_inc(&key->shared.inode->i_count);
			break;
		case FUT_OFF_MMSHARED:
			atomic_inc(&key->private.mm->mm_count);
			break;
	}
}
EXPORT_SYMBOL_GPL(get_futex_key_refs);

/*
 * Drop a reference to the resource addressed by a key.
 * The hash bucket spinlock must not be held.
 */
void drop_futex_key_refs(union futex_key *key)
{
	if (key->both.ptr == 0)
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
EXPORT_SYMBOL_GPL(drop_futex_key_refs);

static u32 cmpxchg_futex_value_locked(u32 __user *uaddr, u32 uval, u32 newval)
{
	u32 curval;

	pagefault_disable();
	curval = futex_atomic_cmpxchg_inatomic(uaddr, uval, newval);
	pagefault_enable();

	return curval;
}

static int get_futex_value_locked(u32 *dest, u32 __user *from)
{
	int ret;

	pagefault_disable();
	ret = __copy_from_user_inatomic(dest, from, sizeof(u32));
	pagefault_enable();

	return ret ? -EFAULT : 0;
}

/*
 * Fault handling.
 * if fshared is non NULL, current->mm->mmap_sem is already held
 */
static int futex_handle_fault(unsigned long address,
			      struct rw_semaphore *fshared, int attempt)
{
	struct vm_area_struct * vma;
	struct mm_struct *mm = current->mm;
	int ret = -EFAULT;

	if (attempt > 2)
		return ret;

	if (!fshared)
		down_read(&mm->mmap_sem);
	vma = find_vma(mm, address);
	if (vma && address >= vma->vm_start &&
	    (vma->vm_flags & VM_WRITE)) {
		switch (handle_mm_fault(mm, vma, address, 1)) {
		case VM_FAULT_MINOR:
			ret = 0;
			current->min_flt++;
			break;
		case VM_FAULT_MAJOR:
			ret = 0;
			current->maj_flt++;
			break;
		}
	}
	if (!fshared)
		up_read(&mm->mmap_sem);
	return ret;
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

static void free_pi_state(struct futex_pi_state *pi_state)
{
	if (!atomic_dec_and_test(&pi_state->refcount))
		return;

	/*
	 * If pi_state->owner is NULL, the owner is most probably dying
	 * and has cleaned up the pi_state already
	 */
	if (pi_state->owner) {
		spin_lock_irq(&pi_state->owner->pi_lock);
		list_del_init(&pi_state->list);
		spin_unlock_irq(&pi_state->owner->pi_lock);

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
	p = find_task_by_pid(pid);

	if (!p || ((current->euid != p->euid) && (current->euid != p->uid)))
		p = ERR_PTR(-ESRCH);
	else
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
	union futex_key key;

	/*
	 * We are a ZOMBIE and nobody can enqueue itself on
	 * pi_state_list anymore, but we have to be careful
	 * versus waiters unqueueing themselves:
	 */
	spin_lock_irq(&curr->pi_lock);
	while (!list_empty(head)) {

		next = head->next;
		pi_state = list_entry(next, struct futex_pi_state, list);
		key = pi_state->key;
		hb = hash_futex(&key);
		spin_unlock_irq(&curr->pi_lock);

		spin_lock(&hb->lock);

		spin_lock_irq(&curr->pi_lock);
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
		spin_unlock_irq(&curr->pi_lock);

		rt_mutex_unlock(&pi_state->pi_mutex);

		spin_unlock(&hb->lock);

		spin_lock_irq(&curr->pi_lock);
	}
	spin_unlock_irq(&curr->pi_lock);
}

static int
lookup_pi_state(u32 uval, struct futex_hash_bucket *hb,
		union futex_key *key, struct futex_pi_state **ps)
{
	struct futex_pi_state *pi_state = NULL;
	struct futex_q *this, *next;
	struct plist_head *head;
	struct task_struct *p;
	pid_t pid = uval & FUTEX_TID_MASK;

	head = &hb->chain;

	plist_for_each_entry_safe(this, next, head, list) {
		if (match_futex(&this->key, key)) {
			/*
			 * Another waiter already exists - bump up
			 * the refcount and return its pi_state:
			 */
			pi_state = this->pi_state;
			/*
			 * Userspace might have messed up non PI and PI futexes
			 */
			if (unlikely(!pi_state))
				return -EINVAL;

			WARN_ON(!atomic_read(&pi_state->refcount));
			WARN_ON(pid && pi_state->owner &&
				pi_state->owner->pid != pid);

			atomic_inc(&pi_state->refcount);
			*ps = pi_state;

			return 0;
		}
	}

	/*
	 * We are the first waiter - try to look up the real owner and attach
	 * the new pi_state to it, but bail out when TID = 0
	 */
	if (!pid)
		return -ESRCH;
	p = futex_find_get_task(pid);
	if (IS_ERR(p))
		return PTR_ERR(p);

	/*
	 * We need to look at the task state flags to figure out,
	 * whether the task is exiting. To protect against the do_exit
	 * change of the task flags, we do this protected by
	 * p->pi_lock:
	 */
	spin_lock_irq(&p->pi_lock);
	if (unlikely(p->flags & PF_EXITING)) {
		/*
		 * The task is on the way out. When PF_EXITPIDONE is
		 * set, we know that the task has finished the
		 * cleanup:
		 */
		int ret = (p->flags & PF_EXITPIDONE) ? -ESRCH : -EAGAIN;

		spin_unlock_irq(&p->pi_lock);
		put_task_struct(p);
		return ret;
	}

	pi_state = alloc_pi_state();

	/*
	 * Initialize the pi_mutex in locked state and make 'p'
	 * the owner of it:
	 */
	rt_mutex_init_proxy_locked(&pi_state->pi_mutex, p);

	/* Store the key for possible exit cleanups: */
	pi_state->key = *key;

	WARN_ON(!list_empty(&pi_state->list));
	list_add(&pi_state->list, &p->pi_state_list);
	pi_state->owner = p;
	spin_unlock_irq(&p->pi_lock);

	put_task_struct(p);

	*ps = pi_state;

	return 0;
}

/*
 * The hash bucket lock must be held when this is called.
 * Afterwards, the futex_q must not be accessed.
 */
static void wake_futex(struct futex_q *q)
{
	plist_del(&q->list, &q->list.plist);
	if (q->filp)
		send_sigio(&q->filp->f_owner, q->fd, POLL_IN);
	/*
	 * The lock in wake_up_all() is a crucial memory barrier after the
	 * plist_del() and also before assigning to q->lock_ptr.
	 */
	wake_up_all(&q->waiters);
	/*
	 * The waiting task can free the futex_q as soon as this is written,
	 * without taking any locks.  This must come last.
	 *
	 * A memory barrier is required here to prevent the following store
	 * to lock_ptr from getting ahead of the wakeup. Clearing the lock
	 * at the end of wake_up_all() does not prevent this store from
	 * moving.
	 */
	smp_wmb();
	q->lock_ptr = NULL;
}

static int wake_futex_pi(u32 __user *uaddr, u32 uval, struct futex_q *this)
{
	struct task_struct *new_owner;
	struct futex_pi_state *pi_state = this->pi_state;
	u32 curval, newval;

	if (!pi_state)
		return -EINVAL;

	spin_lock(&pi_state->pi_mutex.wait_lock);
	new_owner = rt_mutex_next_owner(&pi_state->pi_mutex);

	/*
	 * This happens when we have stolen the lock and the original
	 * pending owner did not enqueue itself back on the rt_mutex.
	 * Thats not a tragedy. We know that way, that a lock waiter
	 * is on the fly. We make the futex_q waiter the pending owner.
	 */
	if (!new_owner)
		new_owner = this->task;

	/*
	 * We pass it to the next owner. (The WAITERS bit is always
	 * kept enabled while there is PI state around. We must also
	 * preserve the owner died bit.)
	 */
	if (!(uval & FUTEX_OWNER_DIED)) {
		int ret = 0;

		newval = FUTEX_WAITERS | new_owner->pid;

		curval = cmpxchg_futex_value_locked(uaddr, uval, newval);

		if (curval == -EFAULT)
			ret = -EFAULT;
		if (curval != uval)
			ret = -EINVAL;
		if (ret) {
			spin_unlock(&pi_state->pi_mutex.wait_lock);
			return ret;
		}
	}

	spin_lock_irq(&pi_state->owner->pi_lock);
	WARN_ON(list_empty(&pi_state->list));
	list_del_init(&pi_state->list);
	spin_unlock_irq(&pi_state->owner->pi_lock);

	spin_lock_irq(&new_owner->pi_lock);
	WARN_ON(!list_empty(&pi_state->list));
	list_add(&pi_state->list, &new_owner->pi_state_list);
	pi_state->owner = new_owner;
	spin_unlock_irq(&new_owner->pi_lock);

	spin_unlock(&pi_state->pi_mutex.wait_lock);
	rt_mutex_unlock(&pi_state->pi_mutex);

	return 0;
}

static int unlock_futex_pi(u32 __user *uaddr, u32 uval)
{
	u32 oldval;

	/*
	 * There is no waiter, so we unlock the futex. The owner died
	 * bit has not to be preserved here. We are the owner:
	 */
	oldval = cmpxchg_futex_value_locked(uaddr, uval, 0);

	if (oldval == -EFAULT)
		return oldval;
	if (oldval != uval)
		return -EAGAIN;

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

/*
 * Wake up all waiters hashed on the physical page that is mapped
 * to this virtual address:
 */
static int futex_wake(u32 __user *uaddr, struct rw_semaphore *fshared,
		      int nr_wake)
{
	struct futex_hash_bucket *hb;
	struct futex_q *this, *next;
	struct plist_head *head;
	union futex_key key;
	int ret;

	futex_lock_mm(fshared);

	ret = get_futex_key(uaddr, fshared, &key);
	if (unlikely(ret != 0))
		goto out;

	hb = hash_futex(&key);
	spin_lock(&hb->lock);
	head = &hb->chain;

	plist_for_each_entry_safe(this, next, head, list) {
		if (match_futex (&this->key, &key)) {
			if (this->pi_state) {
				ret = -EINVAL;
				break;
			}
			wake_futex(this);
			if (++ret >= nr_wake)
				break;
		}
	}

	spin_unlock(&hb->lock);
out:
	futex_unlock_mm(fshared);
	return ret;
}

/*
 * Wake up all waiters hashed on the physical page that is mapped
 * to this virtual address:
 */
static int
futex_wake_op(u32 __user *uaddr1, struct rw_semaphore *fshared,
	      u32 __user *uaddr2,
	      int nr_wake, int nr_wake2, int op)
{
	union futex_key key1, key2;
	struct futex_hash_bucket *hb1, *hb2;
	struct plist_head *head;
	struct futex_q *this, *next;
	int ret, op_ret, attempt = 0;

retryfull:
	futex_lock_mm(fshared);

	ret = get_futex_key(uaddr1, fshared, &key1);
	if (unlikely(ret != 0))
		goto out;
	ret = get_futex_key(uaddr2, fshared, &key2);
	if (unlikely(ret != 0))
		goto out;

	hb1 = hash_futex(&key1);
	hb2 = hash_futex(&key2);

retry:
	double_lock_hb(hb1, hb2);

	op_ret = futex_atomic_op_inuser(op, uaddr2);
	if (unlikely(op_ret < 0)) {
		u32 dummy;

		spin_unlock(&hb1->lock);
		if (hb1 != hb2)
			spin_unlock(&hb2->lock);

#ifndef CONFIG_MMU
		/*
		 * we don't get EFAULT from MMU faults if we don't have an MMU,
		 * but we might get them from range checking
		 */
		ret = op_ret;
		goto out;
#endif

		if (unlikely(op_ret != -EFAULT)) {
			ret = op_ret;
			goto out;
		}

		/*
		 * futex_atomic_op_inuser needs to both read and write
		 * *(int __user *)uaddr2, but we can't modify it
		 * non-atomically.  Therefore, if get_user below is not
		 * enough, we need to handle the fault ourselves, while
		 * still holding the mmap_sem.
		 */
		if (attempt++) {
			ret = futex_handle_fault((unsigned long)uaddr2,
						 fshared, attempt);
			if (ret)
				goto out;
			goto retry;
		}

		/*
		 * If we would have faulted, release mmap_sem,
		 * fault it in and start all over again.
		 */
		futex_unlock_mm(fshared);

		ret = get_user(dummy, uaddr2);
		if (ret)
			return ret;

		goto retryfull;
	}

	head = &hb1->chain;

	plist_for_each_entry_safe(this, next, head, list) {
		if (match_futex (&this->key, &key1)) {
			wake_futex(this);
			if (++ret >= nr_wake)
				break;
		}
	}

	if (op_ret > 0) {
		head = &hb2->chain;

		op_ret = 0;
		plist_for_each_entry_safe(this, next, head, list) {
			if (match_futex (&this->key, &key2)) {
				wake_futex(this);
				if (++op_ret >= nr_wake2)
					break;
			}
		}
		ret += op_ret;
	}

	spin_unlock(&hb1->lock);
	if (hb1 != hb2)
		spin_unlock(&hb2->lock);
out:
	futex_unlock_mm(fshared);

	return ret;
}

/*
 * Requeue all waiters hashed on one physical page to another
 * physical page.
 */
static int futex_requeue(u32 __user *uaddr1, struct rw_semaphore *fshared,
			 u32 __user *uaddr2,
			 int nr_wake, int nr_requeue, u32 *cmpval)
{
	union futex_key key1, key2;
	struct futex_hash_bucket *hb1, *hb2;
	struct plist_head *head1;
	struct futex_q *this, *next;
	int ret, drop_count = 0;

 retry:
	futex_lock_mm(fshared);

	ret = get_futex_key(uaddr1, fshared, &key1);
	if (unlikely(ret != 0))
		goto out;
	ret = get_futex_key(uaddr2, fshared, &key2);
	if (unlikely(ret != 0))
		goto out;

	hb1 = hash_futex(&key1);
	hb2 = hash_futex(&key2);

	double_lock_hb(hb1, hb2);

	if (likely(cmpval != NULL)) {
		u32 curval;

		ret = get_futex_value_locked(&curval, uaddr1);

		if (unlikely(ret)) {
			spin_unlock(&hb1->lock);
			if (hb1 != hb2)
				spin_unlock(&hb2->lock);

			/*
			 * If we would have faulted, release mmap_sem, fault
			 * it in and start all over again.
			 */
			futex_unlock_mm(fshared);

			ret = get_user(curval, uaddr1);

			if (!ret)
				goto retry;

			return ret;
		}
		if (curval != *cmpval) {
			ret = -EAGAIN;
			goto out_unlock;
		}
	}

	head1 = &hb1->chain;
	plist_for_each_entry_safe(this, next, head1, list) {
		if (!match_futex (&this->key, &key1))
			continue;
		if (++ret <= nr_wake) {
			wake_futex(this);
		} else {
			/*
			 * If key1 and key2 hash to the same bucket, no need to
			 * requeue.
			 */
			if (likely(head1 != &hb2->chain)) {
				plist_del(&this->list, &hb1->chain);
				plist_add(&this->list, &hb2->chain);
				this->lock_ptr = &hb2->lock;
#ifdef CONFIG_DEBUG_PI_LIST
				this->list.plist.lock = &hb2->lock;
#endif
			}
			this->key = key2;
			get_futex_key_refs(&key2);
			drop_count++;

			if (ret - nr_wake >= nr_requeue)
				break;
		}
	}

out_unlock:
	spin_unlock(&hb1->lock);
	if (hb1 != hb2)
		spin_unlock(&hb2->lock);

	/* drop_futex_key_refs() must be called outside the spinlocks. */
	while (--drop_count >= 0)
		drop_futex_key_refs(&key1);

out:
	futex_unlock_mm(fshared);
	return ret;
}

/* The key must be already stored in q->key. */
static inline struct futex_hash_bucket *
queue_lock(struct futex_q *q, int fd, struct file *filp)
{
	struct futex_hash_bucket *hb;

	q->fd = fd;
	q->filp = filp;

	init_waitqueue_head(&q->waiters);

	get_futex_key_refs(&q->key);
	hb = hash_futex(&q->key);
	q->lock_ptr = &hb->lock;

	spin_lock(&hb->lock);
	return hb;
}

static inline void __queue_me(struct futex_q *q, struct futex_hash_bucket *hb)
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
#ifdef CONFIG_DEBUG_PI_LIST
	q->list.plist.lock = &hb->lock;
#endif
	plist_add(&q->list, &hb->chain);
	q->task = current;
	spin_unlock(&hb->lock);
}

static inline void
queue_unlock(struct futex_q *q, struct futex_hash_bucket *hb)
{
	spin_unlock(&hb->lock);
	drop_futex_key_refs(&q->key);
}

/*
 * queue_me and unqueue_me must be called as a pair, each
 * exactly once.  They are called with the hashed spinlock held.
 */

/* The key must be already stored in q->key. */
static void queue_me(struct futex_q *q, int fd, struct file *filp)
{
	struct futex_hash_bucket *hb;

	hb = queue_lock(q, fd, filp);
	__queue_me(q, hb);
}

/* Return 1 if we were still queued (ie. 0 means we were woken) */
static int unqueue_me(struct futex_q *q)
{
	spinlock_t *lock_ptr;
	int ret = 0;

	/* In the common case we don't take the spinlock, which is nice. */
 retry:
	lock_ptr = q->lock_ptr;
	barrier();
	if (lock_ptr != 0) {
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
		WARN_ON(plist_node_empty(&q->list));
		plist_del(&q->list, &q->list.plist);

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
{
	WARN_ON(plist_node_empty(&q->list));
	plist_del(&q->list, &q->list.plist);

	BUG_ON(!q->pi_state);
	free_pi_state(q->pi_state);
	q->pi_state = NULL;

	spin_unlock(q->lock_ptr);

	drop_futex_key_refs(&q->key);
}

/*
 * Fixup the pi_state owner with current.
 *
 * Must be called with hash bucket lock held and mm->sem held for non
 * private futexes.
 */
static int fixup_pi_state_owner(u32 __user *uaddr, struct futex_q *q,
				struct task_struct *curr)
{
	u32 newtid = curr->pid | FUTEX_WAITERS;
	struct futex_pi_state *pi_state = q->pi_state;
	u32 uval, curval, newval;
	int ret;

	/* Owner died? */
	if (pi_state->owner != NULL) {
		spin_lock_irq(&pi_state->owner->pi_lock);
		WARN_ON(list_empty(&pi_state->list));
		list_del_init(&pi_state->list);
		spin_unlock_irq(&pi_state->owner->pi_lock);
	} else
		newtid |= FUTEX_OWNER_DIED;

	pi_state->owner = curr;

	spin_lock_irq(&curr->pi_lock);
	WARN_ON(!list_empty(&pi_state->list));
	list_add(&pi_state->list, &curr->pi_state_list);
	spin_unlock_irq(&curr->pi_lock);

	/*
	 * We own it, so we have to replace the pending owner
	 * TID. This must be atomic as we have preserve the
	 * owner died bit here.
	 */
	ret = get_futex_value_locked(&uval, uaddr);

	while (!ret) {
		newval = (uval & FUTEX_OWNER_DIED) | newtid;

		curval = cmpxchg_futex_value_locked(uaddr, uval, newval);

		if (curval == -EFAULT)
			ret = -EFAULT;
		if (curval == uval)
			break;
		uval = curval;
	}
	return ret;
}

/*
 * In case we must use restart_block to restart a futex_wait,
 * we encode in the 'arg3' shared capability
 */
#define ARG3_SHARED  1

static long futex_wait_restart(struct restart_block *restart);

static int futex_wait(u32 __user *uaddr, struct rw_semaphore *fshared,
		      u32 val, ktime_t *abs_time)
{
	struct task_struct *curr = current;
	DECLARE_WAITQUEUE(wait, curr);
	struct futex_hash_bucket *hb;
	struct futex_q q;
	u32 uval;
	int ret;
	struct hrtimer_sleeper t;
	int rem = 0;

	q.pi_state = NULL;
 retry:
	futex_lock_mm(fshared);

	ret = get_futex_key(uaddr, fshared, &q.key);
	if (unlikely(ret != 0))
		goto out_release_sem;

	hb = queue_lock(&q, -1, NULL);

	/*
	 * Access the page AFTER the futex is queued.
	 * Order is important:
	 *
	 *   Userspace waiter: val = var; if (cond(val)) futex_wait(&var, val);
	 *   Userspace waker:  if (cond(var)) { var = new; futex_wake(&var); }
	 *
	 * The basic logical guarantee of a futex is that it blocks ONLY
	 * if cond(var) is known to be true at the time of blocking, for
	 * any cond.  If we queued after testing *uaddr, that would open
	 * a race condition where we could block indefinitely with
	 * cond(var) false, which would violate the guarantee.
	 *
	 * A consequence is that futex_wait() can return zero and absorb
	 * a wakeup when *uaddr != val on entry to the syscall.  This is
	 * rare, but normal.
	 *
	 * for shared futexes, we hold the mmap semaphore, so the mapping
	 * cannot have changed since we looked it up in get_futex_key.
	 */
	ret = get_futex_value_locked(&uval, uaddr);

	if (unlikely(ret)) {
		queue_unlock(&q, hb);

		/*
		 * If we would have faulted, release mmap_sem, fault it in and
		 * start all over again.
		 */
		futex_unlock_mm(fshared);

		ret = get_user(uval, uaddr);

		if (!ret)
			goto retry;
		return ret;
	}
	ret = -EWOULDBLOCK;
	if (uval != val)
		goto out_unlock_release_sem;

	/* Only actually queue if *uaddr contained val.  */
	__queue_me(&q, hb);

	/*
	 * Now the futex is queued and we have checked the data, we
	 * don't want to hold mmap_sem while we sleep.
	 */
	futex_unlock_mm(fshared);

	/*
	 * There might have been scheduling since the queue_me(), as we
	 * cannot hold a spinlock across the get_user() in case it
	 * faults, and we cannot just set TASK_INTERRUPTIBLE state when
	 * queueing ourselves into the futex hash.  This code thus has to
	 * rely on the futex_wake() code removing us from hash when it
	 * wakes us up.
	 */

	/* add_wait_queue is the barrier after __set_current_state. */
	__set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&q.waiters, &wait);
	/*
	 * !plist_node_empty() is safe here without any lock.
	 * q.lock_ptr != 0 is not safe, because of ordering against wakeup.
	 */
	if (likely(!plist_node_empty(&q.list))) {
		if (!abs_time)
			schedule();
		else {
			hrtimer_init(&t.timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
			hrtimer_init_sleeper(&t, current);
			t.timer.expires = *abs_time;

			hrtimer_start(&t.timer, t.timer.expires, HRTIMER_MODE_ABS);

			/*
			 * the timer could have already expired, in which
			 * case current would be flagged for rescheduling.
			 * Don't bother calling schedule.
			 */
			if (likely(t.task))
				schedule();

			hrtimer_cancel(&t.timer);

			/* Flag if a timeout occured */
			rem = (t.task == NULL);
		}
	}
	__set_current_state(TASK_RUNNING);

	/*
	 * NOTE: we don't remove ourselves from the waitqueue because
	 * we are the only user of it.
	 */

	/* If we were woken (and unqueued), we succeeded, whatever. */
	if (!unqueue_me(&q))
		return 0;
	if (rem)
		return -ETIMEDOUT;

	/*
	 * We expect signal_pending(current), but another thread may
	 * have handled it for us already.
	 */
	if (!abs_time)
		return -ERESTARTSYS;
	else {
		struct restart_block *restart;
		restart = &current_thread_info()->restart_block;
		restart->fn = futex_wait_restart;
		restart->arg0 = (unsigned long)uaddr;
		restart->arg1 = (unsigned long)val;
		restart->arg2 = (unsigned long)abs_time;
		restart->arg3 = 0;
		if (fshared)
			restart->arg3 |= ARG3_SHARED;
		return -ERESTART_RESTARTBLOCK;
	}

 out_unlock_release_sem:
	queue_unlock(&q, hb);

 out_release_sem:
	futex_unlock_mm(fshared);
	return ret;
}


static long futex_wait_restart(struct restart_block *restart)
{
	u32 __user *uaddr = (u32 __user *)restart->arg0;
	u32 val = (u32)restart->arg1;
	ktime_t *abs_time = (ktime_t *)restart->arg2;
	struct rw_semaphore *fshared = NULL;

	restart->fn = do_no_restart_syscall;
	if (restart->arg3 & ARG3_SHARED)
		fshared = &current->mm->mmap_sem;
	return (long)futex_wait(uaddr, fshared, val, abs_time);
}


/*
 * Userspace tried a 0 -> TID atomic transition of the futex value
 * and failed. The kernel side here does the whole locking operation:
 * if there are waiters then it will block, it does PI, etc. (Due to
 * races the kernel might see a 0 value of the futex too.)
 */
static int futex_lock_pi(u32 __user *uaddr, struct rw_semaphore *fshared,
			 int detect, ktime_t *time, int trylock)
{
	struct hrtimer_sleeper timeout, *to = NULL;
	struct task_struct *curr = current;
	struct futex_hash_bucket *hb;
	u32 uval, newval, curval;
	struct futex_q q;
	int ret, lock_taken, ownerdied = 0, attempt = 0;

	if (refill_pi_state_cache())
		return -ENOMEM;

	if (time) {
		to = &timeout;
		hrtimer_init(&to->timer, CLOCK_REALTIME, HRTIMER_MODE_ABS);
		hrtimer_init_sleeper(to, current);
		to->timer.expires = *time;
	}

	q.pi_state = NULL;
 retry:
	futex_lock_mm(fshared);

	ret = get_futex_key(uaddr, fshared, &q.key);
	if (unlikely(ret != 0))
		goto out_release_sem;

 retry_unlocked:
	hb = queue_lock(&q, -1, NULL);

 retry_locked:
	ret = lock_taken = 0;

	/*
	 * To avoid races, we attempt to take the lock here again
	 * (by doing a 0 -> TID atomic cmpxchg), while holding all
	 * the locks. It will most likely not succeed.
	 */
	newval = current->pid;

	curval = cmpxchg_futex_value_locked(uaddr, 0, newval);

	if (unlikely(curval == -EFAULT))
		goto uaddr_faulted;

	/*
	 * Detect deadlocks. In case of REQUEUE_PI this is a valid
	 * situation and we return success to user space.
	 */
	if (unlikely((curval & FUTEX_TID_MASK) == current->pid)) {
		ret = -EDEADLK;
		goto out_unlock_release_sem;
	}

	/*
	 * Surprise - we got the lock. Just return to userspace:
	 */
	if (unlikely(!curval))
		goto out_unlock_release_sem;

	uval = curval;

	/*
	 * Set the WAITERS flag, so the owner will know it has someone
	 * to wake at next unlock
	 */
	newval = curval | FUTEX_WAITERS;

	/*
	 * There are two cases, where a futex might have no owner (the
	 * owner TID is 0): OWNER_DIED. We take over the futex in this
	 * case. We also do an unconditional take over, when the owner
	 * of the futex died.
	 *
	 * This is safe as we are protected by the hash bucket lock !
	 */
	if (unlikely(ownerdied || !(curval & FUTEX_TID_MASK))) {
		/* Keep the OWNER_DIED bit */
		newval = (curval & ~FUTEX_TID_MASK) | current->pid;
		ownerdied = 0;
		lock_taken = 1;
	}

	curval = cmpxchg_futex_value_locked(uaddr, uval, newval);

	if (unlikely(curval == -EFAULT))
		goto uaddr_faulted;
	if (unlikely(curval != uval))
		goto retry_locked;

	/*
	 * We took the lock due to owner died take over.
	 */
	if (unlikely(lock_taken))
		goto out_unlock_release_sem;

	/*
	 * We dont have the lock. Look up the PI state (or create it if
	 * we are the first waiter):
	 */
	ret = lookup_pi_state(uval, hb, &q.key, &q.pi_state);

	if (unlikely(ret)) {
		switch (ret) {

		case -EAGAIN:
			/*
			 * Task is exiting and we just wait for the
			 * exit to complete.
			 */
			queue_unlock(&q, hb);
			futex_unlock_mm(fshared);
			cond_resched();
			goto retry;

		case -ESRCH:
			/*
			 * No owner found for this futex. Check if the
			 * OWNER_DIED bit is set to figure out whether
			 * this is a robust futex or not.
			 */
			if (get_futex_value_locked(&curval, uaddr))
				goto uaddr_faulted;

			/*
			 * We simply start over in case of a robust
			 * futex. The code above will take the futex
			 * and return happy.
			 */
			if (curval & FUTEX_OWNER_DIED) {
				ownerdied = 1;
				goto retry_locked;
			}
		default:
			goto out_unlock_release_sem;
		}
	}

	/*
	 * Only actually queue now that the atomic ops are done:
	 */
	__queue_me(&q, hb);

	/*
	 * Now the futex is queued and we have checked the data, we
	 * don't want to hold mmap_sem while we sleep.
	 */
	futex_unlock_mm(fshared);

	WARN_ON(!q.pi_state);
	/*
	 * Block on the PI mutex:
	 */
	if (!trylock)
		ret = rt_mutex_timed_lock(&q.pi_state->pi_mutex, to, 1);
	else {
		ret = rt_mutex_trylock(&q.pi_state->pi_mutex);
		/* Fixup the trylock return value: */
		ret = ret ? 0 : -EWOULDBLOCK;
	}

	futex_lock_mm(fshared);
	spin_lock(q.lock_ptr);

	if (!ret) {
		/*
		 * Got the lock. We might not be the anticipated owner
		 * if we did a lock-steal - fix up the PI-state in
		 * that case:
		 */
		if (q.pi_state->owner != curr)
			ret = fixup_pi_state_owner(uaddr, &q, curr);
	} else {
		/*
		 * Catch the rare case, where the lock was released
		 * when we were on the way back before we locked the
		 * hash bucket.
		 */
		if (q.pi_state->owner == curr &&
		    rt_mutex_trylock(&q.pi_state->pi_mutex)) {
			ret = 0;
		} else {
			/*
			 * Paranoia check. If we did not take the lock
			 * in the trylock above, then we should not be
			 * the owner of the rtmutex, neither the real
			 * nor the pending one:
			 */
			if (rt_mutex_owner(&q.pi_state->pi_mutex) == curr)
				printk(KERN_ERR "futex_lock_pi: ret = %d "
				       "pi-mutex: %p pi-state %p\n", ret,
				       q.pi_state->pi_mutex.owner,
				       q.pi_state->owner);
		}
	}

	/* Unqueue and drop the lock */
	unqueue_me_pi(&q);
	futex_unlock_mm(fshared);

	return ret != -EINTR ? ret : -ERESTARTNOINTR;

 out_unlock_release_sem:
	queue_unlock(&q, hb);

 out_release_sem:
	futex_unlock_mm(fshared);
	return ret;

 uaddr_faulted:
	/*
	 * We have to r/w  *(int __user *)uaddr, but we can't modify it
	 * non-atomically.  Therefore, if get_user below is not
	 * enough, we need to handle the fault ourselves, while
	 * still holding the mmap_sem.
	 *
	 * ... and hb->lock. :-) --ANK
	 */
	queue_unlock(&q, hb);

	if (attempt++) {
		ret = futex_handle_fault((unsigned long)uaddr, fshared,
					 attempt);
		if (ret)
			goto out_release_sem;
		goto retry_unlocked;
	}

	futex_unlock_mm(fshared);

	ret = get_user(uval, uaddr);
	if (!ret && (uval != -EFAULT))
		goto retry;

	return ret;
}

/*
 * Userspace attempted a TID -> 0 atomic transition, and failed.
 * This is the in-kernel slowpath: we look up the PI state (if any),
 * and do the rt-mutex unlock.
 */
static int futex_unlock_pi(u32 __user *uaddr, struct rw_semaphore *fshared)
{
	struct futex_hash_bucket *hb;
	struct futex_q *this, *next;
	u32 uval;
	struct plist_head *head;
	union futex_key key;
	int ret, attempt = 0;

retry:
	if (get_user(uval, uaddr))
		return -EFAULT;
	/*
	 * We release only a lock we actually own:
	 */
	if ((uval & FUTEX_TID_MASK) != current->pid)
		return -EPERM;
	/*
	 * First take all the futex related locks:
	 */
	futex_lock_mm(fshared);

	ret = get_futex_key(uaddr, fshared, &key);
	if (unlikely(ret != 0))
		goto out;

	hb = hash_futex(&key);
retry_unlocked:
	spin_lock(&hb->lock);

	/*
	 * To avoid races, try to do the TID -> 0 atomic transition
	 * again. If it succeeds then we can return without waking
	 * anyone else up:
	 */
	if (!(uval & FUTEX_OWNER_DIED))
		uval = cmpxchg_futex_value_locked(uaddr, current->pid, 0);


	if (unlikely(uval == -EFAULT))
		goto pi_faulted;
	/*
	 * Rare case: we managed to release the lock atomically,
	 * no need to wake anyone else up:
	 */
	if (unlikely(uval == current->pid))
		goto out_unlock;

	/*
	 * Ok, other tasks may need to be woken up - check waiters
	 * and do the wakeup if necessary:
	 */
	head = &hb->chain;

	plist_for_each_entry_safe(this, next, head, list) {
		if (!match_futex (&this->key, &key))
			continue;
		ret = wake_futex_pi(uaddr, uval, this);
		/*
		 * The atomic access to the futex value
		 * generated a pagefault, so retry the
		 * user-access and the wakeup:
		 */
		if (ret == -EFAULT)
			goto pi_faulted;
		goto out_unlock;
	}
	/*
	 * No waiters - kernel unlocks the futex:
	 */
	if (!(uval & FUTEX_OWNER_DIED)) {
		ret = unlock_futex_pi(uaddr, uval);
		if (ret == -EFAULT)
			goto pi_faulted;
	}

out_unlock:
	spin_unlock(&hb->lock);
out:
	futex_unlock_mm(fshared);

	return ret;

pi_faulted:
	/*
	 * We have to r/w  *(int __user *)uaddr, but we can't modify it
	 * non-atomically.  Therefore, if get_user below is not
	 * enough, we need to handle the fault ourselves, while
	 * still holding the mmap_sem.
	 *
	 * ... and hb->lock. --ANK
	 */
	spin_unlock(&hb->lock);

	if (attempt++) {
		ret = futex_handle_fault((unsigned long)uaddr, fshared,
					 attempt);
		if (ret)
			goto out;
		goto retry_unlocked;
	}

	futex_unlock_mm(fshared);

	ret = get_user(uval, uaddr);
	if (!ret && (uval != -EFAULT))
		goto retry;

	return ret;
}

static int futex_close(struct inode *inode, struct file *filp)
{
	struct futex_q *q = filp->private_data;

	unqueue_me(q);
	kfree(q);

	return 0;
}

/* This is one-shot: once it's gone off you need a new fd */
static unsigned int futex_poll(struct file *filp,
			       struct poll_table_struct *wait)
{
	struct futex_q *q = filp->private_data;
	int ret = 0;

	poll_wait(filp, &q->waiters, wait);

	/*
	 * plist_node_empty() is safe here without any lock.
	 * q->lock_ptr != 0 is not safe, because of ordering against wakeup.
	 */
	if (plist_node_empty(&q->list))
		ret = POLLIN | POLLRDNORM;

	return ret;
}

static const struct file_operations futex_fops = {
	.release	= futex_close,
	.poll		= futex_poll,
};

/*
 * Signal allows caller to avoid the race which would occur if they
 * set the sigio stuff up afterwards.
 */
static int futex_fd(u32 __user *uaddr, int signal)
{
	struct futex_q *q;
	struct file *filp;
	int ret, err;
	struct rw_semaphore *fshared;
	static unsigned long printk_interval;

	if (printk_timed_ratelimit(&printk_interval, 60 * 60 * 1000)) {
		printk(KERN_WARNING "Process `%s' used FUTEX_FD, which "
		       "will be removed from the kernel in June 2007\n",
		       current->comm);
	}

	ret = -EINVAL;
	if (!valid_signal(signal))
		goto out;

	ret = get_unused_fd();
	if (ret < 0)
		goto out;
	filp = get_empty_filp();
	if (!filp) {
		put_unused_fd(ret);
		ret = -ENFILE;
		goto out;
	}
	filp->f_op = &futex_fops;
	filp->f_path.mnt = mntget(futex_mnt);
	filp->f_path.dentry = dget(futex_mnt->mnt_root);
	filp->f_mapping = filp->f_path.dentry->d_inode->i_mapping;

	if (signal) {
		err = __f_setown(filp, task_pid(current), PIDTYPE_PID, 1);
		if (err < 0) {
			goto error;
		}
		filp->f_owner.signum = signal;
	}

	q = kmalloc(sizeof(*q), GFP_KERNEL);
	if (!q) {
		err = -ENOMEM;
		goto error;
	}
	q->pi_state = NULL;

	fshared = &current->mm->mmap_sem;
	down_read(fshared);
	err = get_futex_key(uaddr, fshared, &q->key);

	if (unlikely(err != 0)) {
		up_read(fshared);
		kfree(q);
		goto error;
	}

	/*
	 * queue_me() must be called before releasing mmap_sem, because
	 * key->shared.inode needs to be referenced while holding it.
	 */
	filp->private_data = q;

	queue_me(q, ret, filp);
	up_read(fshared);

	/* Now we map fd to filp, so userspace can access it */
	fd_install(ret, filp);
out:
	return ret;
error:
	put_unused_fd(ret);
	put_filp(filp);
	ret = err;
	goto out;
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
 * sys_set_robust_list - set the robust-futex list head of a task
 * @head: pointer to the list-head
 * @len: length of the list-head, as userspace expects
 */
asmlinkage long
sys_set_robust_list(struct robust_list_head __user *head,
		    size_t len)
{
	/*
	 * The kernel knows only one size for now:
	 */
	if (unlikely(len != sizeof(*head)))
		return -EINVAL;

	current->robust_list = head;

	return 0;
}

/**
 * sys_get_robust_list - get the robust-futex list head of a task
 * @pid: pid of the process [zero for current task]
 * @head_ptr: pointer to a list-head pointer, the kernel fills it in
 * @len_ptr: pointer to a length field, the kernel fills in the header size
 */
asmlinkage long
sys_get_robust_list(int pid, struct robust_list_head __user * __user *head_ptr,
		    size_t __user *len_ptr)
{
	struct robust_list_head __user *head;
	unsigned long ret;

	if (!pid)
		head = current->robust_list;
	else {
		struct task_struct *p;

		ret = -ESRCH;
		rcu_read_lock();
		p = find_task_by_pid(pid);
		if (!p)
			goto err_unlock;
		ret = -EPERM;
		if ((current->euid != p->euid) && (current->euid != p->uid) &&
				!capable(CAP_SYS_PTRACE))
			goto err_unlock;
		head = p->robust_list;
		rcu_read_unlock();
	}

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
	u32 uval, nval, mval;

retry:
	if (get_user(uval, uaddr))
		return -1;

	if ((uval & FUTEX_TID_MASK) == curr->pid) {
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
		nval = futex_atomic_cmpxchg_inatomic(uaddr, uval, mval);

		if (nval == -EFAULT)
			return -1;

		if (nval != uval)
			goto retry;

		/*
		 * Wake robust non-PI futexes here. The wakeup of
		 * PI futexes happens in exit_pi_state():
		 */
		if (!pi && (uval & FUTEX_WAITERS))
				futex_wake(uaddr, &curr->mm->mmap_sem, 1);
	}
	return 0;
}

/*
 * Fetch a robust-list pointer. Bit 0 signals PI futexes:
 */
static inline int fetch_robust_entry(struct robust_list __user **entry,
				     struct robust_list __user * __user *head,
				     int *pi)
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
	struct robust_list __user *entry, *pending;
	unsigned int limit = ROBUST_LIST_LIMIT, pi, pip;
	unsigned long futex_offset;

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

	if (pending)
		handle_futex_death((void __user *)pending + futex_offset,
				   curr, pip);

	while (entry != &head->list) {
		/*
		 * A pending lock might already be on the list, so
		 * don't process it twice:
		 */
		if (entry != pending)
			if (handle_futex_death((void __user *)entry + futex_offset,
						curr, pi))
				return;
		/*
		 * Fetch the next entry in the list:
		 */
		if (fetch_robust_entry(&entry, &entry->next, &pi))
			return;
		/*
		 * Avoid excessively long or circular lists:
		 */
		if (!--limit)
			break;

		cond_resched();
	}
}

long do_futex(u32 __user *uaddr, int op, u32 val, ktime_t *timeout,
		u32 __user *uaddr2, u32 val2, u32 val3)
{
	int ret;
	int cmd = op & FUTEX_CMD_MASK;
	struct rw_semaphore *fshared = NULL;

	if (!(op & FUTEX_PRIVATE_FLAG))
		fshared = &current->mm->mmap_sem;

	switch (cmd) {
	case FUTEX_WAIT:
		ret = futex_wait(uaddr, fshared, val, timeout);
		break;
	case FUTEX_WAKE:
		ret = futex_wake(uaddr, fshared, val);
		break;
	case FUTEX_FD:
		/* non-zero val means F_SETOWN(getpid()) & F_SETSIG(val) */
		ret = futex_fd(uaddr, val);
		break;
	case FUTEX_REQUEUE:
		ret = futex_requeue(uaddr, fshared, uaddr2, val, val2, NULL);
		break;
	case FUTEX_CMP_REQUEUE:
		ret = futex_requeue(uaddr, fshared, uaddr2, val, val2, &val3);
		break;
	case FUTEX_WAKE_OP:
		ret = futex_wake_op(uaddr, fshared, uaddr2, val, val2, val3);
		break;
	case FUTEX_LOCK_PI:
		ret = futex_lock_pi(uaddr, fshared, val, timeout, 0);
		break;
	case FUTEX_UNLOCK_PI:
		ret = futex_unlock_pi(uaddr, fshared);
		break;
	case FUTEX_TRYLOCK_PI:
		ret = futex_lock_pi(uaddr, fshared, 0, timeout, 1);
		break;
	default:
		ret = -ENOSYS;
	}
	return ret;
}


asmlinkage long sys_futex(u32 __user *uaddr, int op, u32 val,
			  struct timespec __user *utime, u32 __user *uaddr2,
			  u32 val3)
{
	struct timespec ts;
	ktime_t t, *tp = NULL;
	u32 val2 = 0;
	int cmd = op & FUTEX_CMD_MASK;

	if (utime && (cmd == FUTEX_WAIT || cmd == FUTEX_LOCK_PI)) {
		if (copy_from_user(&ts, utime, sizeof(ts)) != 0)
			return -EFAULT;
		if (!timespec_valid(&ts))
			return -EINVAL;

		t = timespec_to_ktime(ts);
		if (cmd == FUTEX_WAIT)
			t = ktime_add(ktime_get(), t);
		tp = &t;
	}
	/*
	 * requeue parameter in 'utime' if cmd == FUTEX_REQUEUE.
	 */
	if (cmd == FUTEX_REQUEUE || cmd == FUTEX_CMP_REQUEUE)
		val2 = (u32) (unsigned long) utime;

	return do_futex(uaddr, op, val, tp, uaddr2, val2, val3);
}

static int futexfs_get_sb(struct file_system_type *fs_type,
			  int flags, const char *dev_name, void *data,
			  struct vfsmount *mnt)
{
	return get_sb_pseudo(fs_type, "futex", NULL, 0xBAD1DEA, mnt);
}

static struct file_system_type futex_fs_type = {
	.name		= "futexfs",
	.get_sb		= futexfs_get_sb,
	.kill_sb	= kill_anon_super,
};

static int __init init(void)
{
	int i = register_filesystem(&futex_fs_type);

	if (i)
		return i;

	futex_mnt = kern_mount(&futex_fs_type);
	if (IS_ERR(futex_mnt)) {
		unregister_filesystem(&futex_fs_type);
		return PTR_ERR(futex_mnt);
	}

	for (i = 0; i < ARRAY_SIZE(futex_queues); i++) {
		plist_head_init(&futex_queues[i].chain, &futex_queues[i].lock);
		spin_lock_init(&futex_queues[i].lock);
	}
	return 0;
}
__initcall(init);
