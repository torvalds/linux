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
#include <asm/futex.h>

#include "rtmutex_common.h"

#define FUTEX_HASHBITS (CONFIG_BASE_SMALL ? 4 : 8)

/*
 * Futexes are matched on equal values of this key.
 * The key type depends on whether it's a shared or private mapping.
 * Don't rearrange members without looking at hash_futex().
 *
 * offset is aligned to a multiple of sizeof(u32) (== 4) by definition.
 * We set bit 0 to indicate if it's an inode-based key.
 */
union futex_key {
	struct {
		unsigned long pgoff;
		struct inode *inode;
		int offset;
	} shared;
	struct {
		unsigned long address;
		struct mm_struct *mm;
		int offset;
	} private;
	struct {
		unsigned long word;
		void *ptr;
		int offset;
	} both;
};

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
 * It is considered woken when list_empty(&q->list) || q->lock_ptr == 0.
 * The order of wakup is always to make the first condition true, then
 * wake up q->waiters, then make the second condition true.
 */
struct futex_q {
	struct list_head list;
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
       spinlock_t              lock;
       struct list_head       chain;
};

static struct futex_hash_bucket futex_queues[1<<FUTEX_HASHBITS];

/* Futex-fs vfsmount entry: */
static struct vfsmount *futex_mnt;

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

/*
 * Get parameters which are the keys for a futex.
 *
 * For shared mappings, it's (page->index, vma->vm_file->f_dentry->d_inode,
 * offset_within_page).  For private mappings, it's (uaddr, current->mm).
 * We can usually work out the index without swapping in the page.
 *
 * Returns: 0, or negative error code.
 * The key words are stored in *key on success.
 *
 * Should be called with &current->mm->mmap_sem but NOT any spinlocks.
 */
static int get_futex_key(u32 __user *uaddr, union futex_key *key)
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
	if (unlikely((key->both.offset % sizeof(u32)) != 0))
		return -EINVAL;
	address -= key->both.offset;

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
		key->private.mm = mm;
		key->private.address = address;
		return 0;
	}

	/*
	 * Linear file mappings are also simple.
	 */
	key->shared.inode = vma->vm_file->f_dentry->d_inode;
	key->both.offset++; /* Bit 0 of offset indicates inode-based key. */
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

/*
 * Take a reference to the resource addressed by a key.
 * Can be called while holding spinlocks.
 *
 * NOTE: mmap_sem MUST be held between get_futex_key() and calling this
 * function, if it is called at all.  mmap_sem keeps key->shared.inode valid.
 */
static inline void get_key_refs(union futex_key *key)
{
	if (key->both.ptr != 0) {
		if (key->both.offset & 1)
			atomic_inc(&key->shared.inode->i_count);
		else
			atomic_inc(&key->private.mm->mm_count);
	}
}

/*
 * Drop a reference to the resource addressed by a key.
 * The hash bucket spinlock must not be held.
 */
static void drop_key_refs(union futex_key *key)
{
	if (key->both.ptr != 0) {
		if (key->both.offset & 1)
			iput(key->shared.inode);
		else
			mmdrop(key->private.mm);
	}
}

static inline int get_futex_value_locked(u32 *dest, u32 __user *from)
{
	int ret;

	pagefault_disable();
	ret = __copy_from_user_inatomic(dest, from, sizeof(u32));
	pagefault_enable();

	return ret ? -EFAULT : 0;
}

/*
 * Fault handling. Called with current->mm->mmap_sem held.
 */
static int futex_handle_fault(unsigned long address, int attempt)
{
	struct vm_area_struct * vma;
	struct mm_struct *mm = current->mm;

	if (attempt > 2 || !(vma = find_vma(mm, address)) ||
	    vma->vm_start > address || !(vma->vm_flags & VM_WRITE))
		return -EFAULT;

	switch (handle_mm_fault(mm, vma, address, 1)) {
	case VM_FAULT_MINOR:
		current->min_flt++;
		break;
	case VM_FAULT_MAJOR:
		current->maj_flt++;
		break;
	default:
		return -EFAULT;
	}
	return 0;
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
	if (!p)
		goto out_unlock;
	if ((current->euid != p->euid) && (current->euid != p->uid)) {
		p = NULL;
		goto out_unlock;
	}
	if (p->exit_state != 0) {
		p = NULL;
		goto out_unlock;
	}
	get_task_struct(p);
out_unlock:
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
lookup_pi_state(u32 uval, struct futex_hash_bucket *hb, struct futex_q *me)
{
	struct futex_pi_state *pi_state = NULL;
	struct futex_q *this, *next;
	struct list_head *head;
	struct task_struct *p;
	pid_t pid;

	head = &hb->chain;

	list_for_each_entry_safe(this, next, head, list) {
		if (match_futex(&this->key, &me->key)) {
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

			atomic_inc(&pi_state->refcount);
			me->pi_state = pi_state;

			return 0;
		}
	}

	/*
	 * We are the first waiter - try to look up the real owner and attach
	 * the new pi_state to it, but bail out when the owner died bit is set
	 * and TID = 0:
	 */
	pid = uval & FUTEX_TID_MASK;
	if (!pid && (uval & FUTEX_OWNER_DIED))
		return -ESRCH;
	p = futex_find_get_task(pid);
	if (!p)
		return -ESRCH;

	pi_state = alloc_pi_state();

	/*
	 * Initialize the pi_mutex in locked state and make 'p'
	 * the owner of it:
	 */
	rt_mutex_init_proxy_locked(&pi_state->pi_mutex, p);

	/* Store the key for possible exit cleanups: */
	pi_state->key = me->key;

	spin_lock_irq(&p->pi_lock);
	WARN_ON(!list_empty(&pi_state->list));
	list_add(&pi_state->list, &p->pi_state_list);
	pi_state->owner = p;
	spin_unlock_irq(&p->pi_lock);

	put_task_struct(p);

	me->pi_state = pi_state;

	return 0;
}

/*
 * The hash bucket lock must be held when this is called.
 * Afterwards, the futex_q must not be accessed.
 */
static void wake_futex(struct futex_q *q)
{
	list_del_init(&q->list);
	if (q->filp)
		send_sigio(&q->filp->f_owner, q->fd, POLL_IN);
	/*
	 * The lock in wake_up_all() is a crucial memory barrier after the
	 * list_del_init() and also before assigning to q->lock_ptr.
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
	wmb();
	q->lock_ptr = NULL;
}

static int wake_futex_pi(u32 __user *uaddr, u32 uval, struct futex_q *this)
{
	struct task_struct *new_owner;
	struct futex_pi_state *pi_state = this->pi_state;
	u32 curval, newval;

	if (!pi_state)
		return -EINVAL;

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
		newval = FUTEX_WAITERS | new_owner->pid;

		pagefault_disable();
		curval = futex_atomic_cmpxchg_inatomic(uaddr, uval, newval);
		pagefault_enable();
		if (curval == -EFAULT)
			return -EFAULT;
		if (curval != uval)
			return -EINVAL;
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
	pagefault_disable();
	oldval = futex_atomic_cmpxchg_inatomic(uaddr, uval, 0);
	pagefault_enable();

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
static int futex_wake(u32 __user *uaddr, int nr_wake)
{
	struct futex_hash_bucket *hb;
	struct futex_q *this, *next;
	struct list_head *head;
	union futex_key key;
	int ret;

	down_read(&current->mm->mmap_sem);

	ret = get_futex_key(uaddr, &key);
	if (unlikely(ret != 0))
		goto out;

	hb = hash_futex(&key);
	spin_lock(&hb->lock);
	head = &hb->chain;

	list_for_each_entry_safe(this, next, head, list) {
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
	up_read(&current->mm->mmap_sem);
	return ret;
}

/*
 * Wake up all waiters hashed on the physical page that is mapped
 * to this virtual address:
 */
static int
futex_wake_op(u32 __user *uaddr1, u32 __user *uaddr2,
	      int nr_wake, int nr_wake2, int op)
{
	union futex_key key1, key2;
	struct futex_hash_bucket *hb1, *hb2;
	struct list_head *head;
	struct futex_q *this, *next;
	int ret, op_ret, attempt = 0;

retryfull:
	down_read(&current->mm->mmap_sem);

	ret = get_futex_key(uaddr1, &key1);
	if (unlikely(ret != 0))
		goto out;
	ret = get_futex_key(uaddr2, &key2);
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
			if (futex_handle_fault((unsigned long)uaddr2,
						attempt)) {
				ret = -EFAULT;
				goto out;
			}
			goto retry;
		}

		/*
		 * If we would have faulted, release mmap_sem,
		 * fault it in and start all over again.
		 */
		up_read(&current->mm->mmap_sem);

		ret = get_user(dummy, uaddr2);
		if (ret)
			return ret;

		goto retryfull;
	}

	head = &hb1->chain;

	list_for_each_entry_safe(this, next, head, list) {
		if (match_futex (&this->key, &key1)) {
			wake_futex(this);
			if (++ret >= nr_wake)
				break;
		}
	}

	if (op_ret > 0) {
		head = &hb2->chain;

		op_ret = 0;
		list_for_each_entry_safe(this, next, head, list) {
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
	up_read(&current->mm->mmap_sem);
	return ret;
}

/*
 * Requeue all waiters hashed on one physical page to another
 * physical page.
 */
static int futex_requeue(u32 __user *uaddr1, u32 __user *uaddr2,
			 int nr_wake, int nr_requeue, u32 *cmpval)
{
	union futex_key key1, key2;
	struct futex_hash_bucket *hb1, *hb2;
	struct list_head *head1;
	struct futex_q *this, *next;
	int ret, drop_count = 0;

 retry:
	down_read(&current->mm->mmap_sem);

	ret = get_futex_key(uaddr1, &key1);
	if (unlikely(ret != 0))
		goto out;
	ret = get_futex_key(uaddr2, &key2);
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
			up_read(&current->mm->mmap_sem);

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
	list_for_each_entry_safe(this, next, head1, list) {
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
				list_move_tail(&this->list, &hb2->chain);
				this->lock_ptr = &hb2->lock;
			}
			this->key = key2;
			get_key_refs(&key2);
			drop_count++;

			if (ret - nr_wake >= nr_requeue)
				break;
		}
	}

out_unlock:
	spin_unlock(&hb1->lock);
	if (hb1 != hb2)
		spin_unlock(&hb2->lock);

	/* drop_key_refs() must be called outside the spinlocks. */
	while (--drop_count >= 0)
		drop_key_refs(&key1);

out:
	up_read(&current->mm->mmap_sem);
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

	get_key_refs(&q->key);
	hb = hash_futex(&q->key);
	q->lock_ptr = &hb->lock;

	spin_lock(&hb->lock);
	return hb;
}

static inline void __queue_me(struct futex_q *q, struct futex_hash_bucket *hb)
{
	list_add_tail(&q->list, &hb->chain);
	q->task = current;
	spin_unlock(&hb->lock);
}

static inline void
queue_unlock(struct futex_q *q, struct futex_hash_bucket *hb)
{
	spin_unlock(&hb->lock);
	drop_key_refs(&q->key);
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
		WARN_ON(list_empty(&q->list));
		list_del(&q->list);

		BUG_ON(q->pi_state);

		spin_unlock(lock_ptr);
		ret = 1;
	}

	drop_key_refs(&q->key);
	return ret;
}

/*
 * PI futexes can not be requeued and must remove themself from the
 * hash bucket. The hash bucket lock is held on entry and dropped here.
 */
static void unqueue_me_pi(struct futex_q *q, struct futex_hash_bucket *hb)
{
	WARN_ON(list_empty(&q->list));
	list_del(&q->list);

	BUG_ON(!q->pi_state);
	free_pi_state(q->pi_state);
	q->pi_state = NULL;

	spin_unlock(&hb->lock);

	drop_key_refs(&q->key);
}

static int futex_wait(u32 __user *uaddr, u32 val, unsigned long time)
{
	struct task_struct *curr = current;
	DECLARE_WAITQUEUE(wait, curr);
	struct futex_hash_bucket *hb;
	struct futex_q q;
	u32 uval;
	int ret;

	q.pi_state = NULL;
 retry:
	down_read(&curr->mm->mmap_sem);

	ret = get_futex_key(uaddr, &q.key);
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
	 * We hold the mmap semaphore, so the mapping cannot have changed
	 * since we looked it up in get_futex_key.
	 */
	ret = get_futex_value_locked(&uval, uaddr);

	if (unlikely(ret)) {
		queue_unlock(&q, hb);

		/*
		 * If we would have faulted, release mmap_sem, fault it in and
		 * start all over again.
		 */
		up_read(&curr->mm->mmap_sem);

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
	up_read(&curr->mm->mmap_sem);

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
	 * !list_empty() is safe here without any lock.
	 * q.lock_ptr != 0 is not safe, because of ordering against wakeup.
	 */
	if (likely(!list_empty(&q.list)))
		time = schedule_timeout(time);
	__set_current_state(TASK_RUNNING);

	/*
	 * NOTE: we don't remove ourselves from the waitqueue because
	 * we are the only user of it.
	 */

	/* If we were woken (and unqueued), we succeeded, whatever. */
	if (!unqueue_me(&q))
		return 0;
	if (time == 0)
		return -ETIMEDOUT;
	/*
	 * We expect signal_pending(current), but another thread may
	 * have handled it for us already.
	 */
	return -EINTR;

 out_unlock_release_sem:
	queue_unlock(&q, hb);

 out_release_sem:
	up_read(&curr->mm->mmap_sem);
	return ret;
}

/*
 * Userspace tried a 0 -> TID atomic transition of the futex value
 * and failed. The kernel side here does the whole locking operation:
 * if there are waiters then it will block, it does PI, etc. (Due to
 * races the kernel might see a 0 value of the futex too.)
 */
static int futex_lock_pi(u32 __user *uaddr, int detect, unsigned long sec,
			 long nsec, int trylock)
{
	struct hrtimer_sleeper timeout, *to = NULL;
	struct task_struct *curr = current;
	struct futex_hash_bucket *hb;
	u32 uval, newval, curval;
	struct futex_q q;
	int ret, attempt = 0;

	if (refill_pi_state_cache())
		return -ENOMEM;

	if (sec != MAX_SCHEDULE_TIMEOUT) {
		to = &timeout;
		hrtimer_init(&to->timer, CLOCK_REALTIME, HRTIMER_ABS);
		hrtimer_init_sleeper(to, current);
		to->timer.expires = ktime_set(sec, nsec);
	}

	q.pi_state = NULL;
 retry:
	down_read(&curr->mm->mmap_sem);

	ret = get_futex_key(uaddr, &q.key);
	if (unlikely(ret != 0))
		goto out_release_sem;

	hb = queue_lock(&q, -1, NULL);

 retry_locked:
	/*
	 * To avoid races, we attempt to take the lock here again
	 * (by doing a 0 -> TID atomic cmpxchg), while holding all
	 * the locks. It will most likely not succeed.
	 */
	newval = current->pid;

	pagefault_disable();
	curval = futex_atomic_cmpxchg_inatomic(uaddr, 0, newval);
	pagefault_enable();

	if (unlikely(curval == -EFAULT))
		goto uaddr_faulted;

	/* We own the lock already */
	if (unlikely((curval & FUTEX_TID_MASK) == current->pid)) {
		if (!detect && 0)
			force_sig(SIGKILL, current);
		ret = -EDEADLK;
		goto out_unlock_release_sem;
	}

	/*
	 * Surprise - we got the lock. Just return
	 * to userspace:
	 */
	if (unlikely(!curval))
		goto out_unlock_release_sem;

	uval = curval;
	newval = uval | FUTEX_WAITERS;

	pagefault_disable();
	curval = futex_atomic_cmpxchg_inatomic(uaddr, uval, newval);
	pagefault_enable();

	if (unlikely(curval == -EFAULT))
		goto uaddr_faulted;
	if (unlikely(curval != uval))
		goto retry_locked;

	/*
	 * We dont have the lock. Look up the PI state (or create it if
	 * we are the first waiter):
	 */
	ret = lookup_pi_state(uval, hb, &q);

	if (unlikely(ret)) {
		/*
		 * There were no waiters and the owner task lookup
		 * failed. When the OWNER_DIED bit is set, then we
		 * know that this is a robust futex and we actually
		 * take the lock. This is safe as we are protected by
		 * the hash bucket lock. We also set the waiters bit
		 * unconditionally here, to simplify glibc handling of
		 * multiple tasks racing to acquire the lock and
		 * cleanup the problems which were left by the dead
		 * owner.
		 */
		if (curval & FUTEX_OWNER_DIED) {
			uval = newval;
			newval = current->pid |
				FUTEX_OWNER_DIED | FUTEX_WAITERS;

			pagefault_disable();
			curval = futex_atomic_cmpxchg_inatomic(uaddr,
							       uval, newval);
			pagefault_enable();

			if (unlikely(curval == -EFAULT))
				goto uaddr_faulted;
			if (unlikely(curval != uval))
				goto retry_locked;
			ret = 0;
		}
		goto out_unlock_release_sem;
	}

	/*
	 * Only actually queue now that the atomic ops are done:
	 */
	__queue_me(&q, hb);

	/*
	 * Now the futex is queued and we have checked the data, we
	 * don't want to hold mmap_sem while we sleep.
	 */
	up_read(&curr->mm->mmap_sem);

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

	down_read(&curr->mm->mmap_sem);
	spin_lock(q.lock_ptr);

	/*
	 * Got the lock. We might not be the anticipated owner if we
	 * did a lock-steal - fix up the PI-state in that case.
	 */
	if (!ret && q.pi_state->owner != curr) {
		u32 newtid = current->pid | FUTEX_WAITERS;

		/* Owner died? */
		if (q.pi_state->owner != NULL) {
			spin_lock_irq(&q.pi_state->owner->pi_lock);
			WARN_ON(list_empty(&q.pi_state->list));
			list_del_init(&q.pi_state->list);
			spin_unlock_irq(&q.pi_state->owner->pi_lock);
		} else
			newtid |= FUTEX_OWNER_DIED;

		q.pi_state->owner = current;

		spin_lock_irq(&current->pi_lock);
		WARN_ON(!list_empty(&q.pi_state->list));
		list_add(&q.pi_state->list, &current->pi_state_list);
		spin_unlock_irq(&current->pi_lock);

		/* Unqueue and drop the lock */
		unqueue_me_pi(&q, hb);
		up_read(&curr->mm->mmap_sem);
		/*
		 * We own it, so we have to replace the pending owner
		 * TID. This must be atomic as we have preserve the
		 * owner died bit here.
		 */
		ret = get_user(uval, uaddr);
		while (!ret) {
			newval = (uval & FUTEX_OWNER_DIED) | newtid;
			curval = futex_atomic_cmpxchg_inatomic(uaddr,
							       uval, newval);
			if (curval == -EFAULT)
				ret = -EFAULT;
			if (curval == uval)
				break;
			uval = curval;
		}
	} else {
		/*
		 * Catch the rare case, where the lock was released
		 * when we were on the way back before we locked
		 * the hash bucket.
		 */
		if (ret && q.pi_state->owner == curr) {
			if (rt_mutex_trylock(&q.pi_state->pi_mutex))
				ret = 0;
		}
		/* Unqueue and drop the lock */
		unqueue_me_pi(&q, hb);
		up_read(&curr->mm->mmap_sem);
	}

	if (!detect && ret == -EDEADLK && 0)
		force_sig(SIGKILL, current);

	return ret != -EINTR ? ret : -ERESTARTNOINTR;

 out_unlock_release_sem:
	queue_unlock(&q, hb);

 out_release_sem:
	up_read(&curr->mm->mmap_sem);
	return ret;

 uaddr_faulted:
	/*
	 * We have to r/w  *(int __user *)uaddr, but we can't modify it
	 * non-atomically.  Therefore, if get_user below is not
	 * enough, we need to handle the fault ourselves, while
	 * still holding the mmap_sem.
	 */
	if (attempt++) {
		if (futex_handle_fault((unsigned long)uaddr, attempt)) {
			ret = -EFAULT;
			goto out_unlock_release_sem;
		}
		goto retry_locked;
	}

	queue_unlock(&q, hb);
	up_read(&curr->mm->mmap_sem);

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
static int futex_unlock_pi(u32 __user *uaddr)
{
	struct futex_hash_bucket *hb;
	struct futex_q *this, *next;
	u32 uval;
	struct list_head *head;
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
	down_read(&current->mm->mmap_sem);

	ret = get_futex_key(uaddr, &key);
	if (unlikely(ret != 0))
		goto out;

	hb = hash_futex(&key);
	spin_lock(&hb->lock);

retry_locked:
	/*
	 * To avoid races, try to do the TID -> 0 atomic transition
	 * again. If it succeeds then we can return without waking
	 * anyone else up:
	 */
	if (!(uval & FUTEX_OWNER_DIED)) {
		pagefault_disable();
		uval = futex_atomic_cmpxchg_inatomic(uaddr, current->pid, 0);
		pagefault_enable();
	}

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

	list_for_each_entry_safe(this, next, head, list) {
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
	up_read(&current->mm->mmap_sem);

	return ret;

pi_faulted:
	/*
	 * We have to r/w  *(int __user *)uaddr, but we can't modify it
	 * non-atomically.  Therefore, if get_user below is not
	 * enough, we need to handle the fault ourselves, while
	 * still holding the mmap_sem.
	 */
	if (attempt++) {
		if (futex_handle_fault((unsigned long)uaddr, attempt)) {
			ret = -EFAULT;
			goto out_unlock;
		}
		goto retry_locked;
	}

	spin_unlock(&hb->lock);
	up_read(&current->mm->mmap_sem);

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
	 * list_empty() is safe here without any lock.
	 * q->lock_ptr != 0 is not safe, because of ordering against wakeup.
	 */
	if (list_empty(&q->list))
		ret = POLLIN | POLLRDNORM;

	return ret;
}

static struct file_operations futex_fops = {
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
	filp->f_vfsmnt = mntget(futex_mnt);
	filp->f_dentry = dget(futex_mnt->mnt_root);
	filp->f_mapping = filp->f_dentry->d_inode->i_mapping;

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

	down_read(&current->mm->mmap_sem);
	err = get_futex_key(uaddr, &q->key);

	if (unlikely(err != 0)) {
		up_read(&current->mm->mmap_sem);
		kfree(q);
		goto error;
	}

	/*
	 * queue_me() must be called before releasing mmap_sem, because
	 * key->shared.inode needs to be referenced while holding it.
	 */
	filp->private_data = q;

	queue_me(q, ret, filp);
	up_read(&current->mm->mmap_sem);

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
		if (!pi) {
			if (uval & FUTEX_WAITERS)
				futex_wake(uaddr, 1);
		}
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
		handle_futex_death((void __user *)pending + futex_offset, curr, pip);

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

long do_futex(u32 __user *uaddr, int op, u32 val, unsigned long timeout,
		u32 __user *uaddr2, u32 val2, u32 val3)
{
	int ret;

	switch (op) {
	case FUTEX_WAIT:
		ret = futex_wait(uaddr, val, timeout);
		break;
	case FUTEX_WAKE:
		ret = futex_wake(uaddr, val);
		break;
	case FUTEX_FD:
		/* non-zero val means F_SETOWN(getpid()) & F_SETSIG(val) */
		ret = futex_fd(uaddr, val);
		break;
	case FUTEX_REQUEUE:
		ret = futex_requeue(uaddr, uaddr2, val, val2, NULL);
		break;
	case FUTEX_CMP_REQUEUE:
		ret = futex_requeue(uaddr, uaddr2, val, val2, &val3);
		break;
	case FUTEX_WAKE_OP:
		ret = futex_wake_op(uaddr, uaddr2, val, val2, val3);
		break;
	case FUTEX_LOCK_PI:
		ret = futex_lock_pi(uaddr, val, timeout, val2, 0);
		break;
	case FUTEX_UNLOCK_PI:
		ret = futex_unlock_pi(uaddr);
		break;
	case FUTEX_TRYLOCK_PI:
		ret = futex_lock_pi(uaddr, 0, timeout, val2, 1);
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
	struct timespec t;
	unsigned long timeout = MAX_SCHEDULE_TIMEOUT;
	u32 val2 = 0;

	if (utime && (op == FUTEX_WAIT || op == FUTEX_LOCK_PI)) {
		if (copy_from_user(&t, utime, sizeof(t)) != 0)
			return -EFAULT;
		if (!timespec_valid(&t))
			return -EINVAL;
		if (op == FUTEX_WAIT)
			timeout = timespec_to_jiffies(&t) + 1;
		else {
			timeout = t.tv_sec;
			val2 = t.tv_nsec;
		}
	}
	/*
	 * requeue parameter in 'utime' if op == FUTEX_REQUEUE.
	 */
	if (op == FUTEX_REQUEUE || op == FUTEX_CMP_REQUEUE)
		val2 = (u32) (unsigned long) utime;

	return do_futex(uaddr, op, val, timeout, uaddr2, val2, val3);
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
		INIT_LIST_HEAD(&futex_queues[i].chain);
		spin_lock_init(&futex_queues[i].lock);
	}
	return 0;
}
__initcall(init);
