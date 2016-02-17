/*
 *  fs/userfaultfd.c
 *
 *  Copyright (C) 2007  Davide Libenzi <davidel@xmailserver.org>
 *  Copyright (C) 2008-2009 Red Hat, Inc.
 *  Copyright (C) 2015  Red Hat, Inc.
 *
 *  This work is licensed under the terms of the GNU GPL, version 2. See
 *  the COPYING file in the top-level directory.
 *
 *  Some part derived from fs/eventfd.c (anon inode setup) and
 *  mm/ksm.c (mm hashing).
 */

#include <linux/hashtable.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/file.h>
#include <linux/bug.h>
#include <linux/anon_inodes.h>
#include <linux/syscalls.h>
#include <linux/userfaultfd_k.h>
#include <linux/mempolicy.h>
#include <linux/ioctl.h>
#include <linux/security.h>

static struct kmem_cache *userfaultfd_ctx_cachep __read_mostly;

enum userfaultfd_state {
	UFFD_STATE_WAIT_API,
	UFFD_STATE_RUNNING,
};

/*
 * Start with fault_pending_wqh and fault_wqh so they're more likely
 * to be in the same cacheline.
 */
struct userfaultfd_ctx {
	/* waitqueue head for the pending (i.e. not read) userfaults */
	wait_queue_head_t fault_pending_wqh;
	/* waitqueue head for the userfaults */
	wait_queue_head_t fault_wqh;
	/* waitqueue head for the pseudo fd to wakeup poll/read */
	wait_queue_head_t fd_wqh;
	/* a refile sequence protected by fault_pending_wqh lock */
	struct seqcount refile_seq;
	/* pseudo fd refcounting */
	atomic_t refcount;
	/* userfaultfd syscall flags */
	unsigned int flags;
	/* state machine */
	enum userfaultfd_state state;
	/* released */
	bool released;
	/* mm with one ore more vmas attached to this userfaultfd_ctx */
	struct mm_struct *mm;
};

struct userfaultfd_wait_queue {
	struct uffd_msg msg;
	wait_queue_t wq;
	struct userfaultfd_ctx *ctx;
};

struct userfaultfd_wake_range {
	unsigned long start;
	unsigned long len;
};

static int userfaultfd_wake_function(wait_queue_t *wq, unsigned mode,
				     int wake_flags, void *key)
{
	struct userfaultfd_wake_range *range = key;
	int ret;
	struct userfaultfd_wait_queue *uwq;
	unsigned long start, len;

	uwq = container_of(wq, struct userfaultfd_wait_queue, wq);
	ret = 0;
	/* len == 0 means wake all */
	start = range->start;
	len = range->len;
	if (len && (start > uwq->msg.arg.pagefault.address ||
		    start + len <= uwq->msg.arg.pagefault.address))
		goto out;
	ret = wake_up_state(wq->private, mode);
	if (ret)
		/*
		 * Wake only once, autoremove behavior.
		 *
		 * After the effect of list_del_init is visible to the
		 * other CPUs, the waitqueue may disappear from under
		 * us, see the !list_empty_careful() in
		 * handle_userfault(). try_to_wake_up() has an
		 * implicit smp_mb__before_spinlock, and the
		 * wq->private is read before calling the extern
		 * function "wake_up_state" (which in turns calls
		 * try_to_wake_up). While the spin_lock;spin_unlock;
		 * wouldn't be enough, the smp_mb__before_spinlock is
		 * enough to avoid an explicit smp_mb() here.
		 */
		list_del_init(&wq->task_list);
out:
	return ret;
}

/**
 * userfaultfd_ctx_get - Acquires a reference to the internal userfaultfd
 * context.
 * @ctx: [in] Pointer to the userfaultfd context.
 *
 * Returns: In case of success, returns not zero.
 */
static void userfaultfd_ctx_get(struct userfaultfd_ctx *ctx)
{
	if (!atomic_inc_not_zero(&ctx->refcount))
		BUG();
}

/**
 * userfaultfd_ctx_put - Releases a reference to the internal userfaultfd
 * context.
 * @ctx: [in] Pointer to userfaultfd context.
 *
 * The userfaultfd context reference must have been previously acquired either
 * with userfaultfd_ctx_get() or userfaultfd_ctx_fdget().
 */
static void userfaultfd_ctx_put(struct userfaultfd_ctx *ctx)
{
	if (atomic_dec_and_test(&ctx->refcount)) {
		VM_BUG_ON(spin_is_locked(&ctx->fault_pending_wqh.lock));
		VM_BUG_ON(waitqueue_active(&ctx->fault_pending_wqh));
		VM_BUG_ON(spin_is_locked(&ctx->fault_wqh.lock));
		VM_BUG_ON(waitqueue_active(&ctx->fault_wqh));
		VM_BUG_ON(spin_is_locked(&ctx->fd_wqh.lock));
		VM_BUG_ON(waitqueue_active(&ctx->fd_wqh));
		mmput(ctx->mm);
		kmem_cache_free(userfaultfd_ctx_cachep, ctx);
	}
}

static inline void msg_init(struct uffd_msg *msg)
{
	BUILD_BUG_ON(sizeof(struct uffd_msg) != 32);
	/*
	 * Must use memset to zero out the paddings or kernel data is
	 * leaked to userland.
	 */
	memset(msg, 0, sizeof(struct uffd_msg));
}

static inline struct uffd_msg userfault_msg(unsigned long address,
					    unsigned int flags,
					    unsigned long reason)
{
	struct uffd_msg msg;
	msg_init(&msg);
	msg.event = UFFD_EVENT_PAGEFAULT;
	msg.arg.pagefault.address = address;
	if (flags & FAULT_FLAG_WRITE)
		/*
		 * If UFFD_FEATURE_PAGEFAULT_FLAG_WRITE was set in the
		 * uffdio_api.features and UFFD_PAGEFAULT_FLAG_WRITE
		 * was not set in a UFFD_EVENT_PAGEFAULT, it means it
		 * was a read fault, otherwise if set it means it's
		 * a write fault.
		 */
		msg.arg.pagefault.flags |= UFFD_PAGEFAULT_FLAG_WRITE;
	if (reason & VM_UFFD_WP)
		/*
		 * If UFFD_FEATURE_PAGEFAULT_FLAG_WP was set in the
		 * uffdio_api.features and UFFD_PAGEFAULT_FLAG_WP was
		 * not set in a UFFD_EVENT_PAGEFAULT, it means it was
		 * a missing fault, otherwise if set it means it's a
		 * write protect fault.
		 */
		msg.arg.pagefault.flags |= UFFD_PAGEFAULT_FLAG_WP;
	return msg;
}

/*
 * Verify the pagetables are still not ok after having reigstered into
 * the fault_pending_wqh to avoid userland having to UFFDIO_WAKE any
 * userfault that has already been resolved, if userfaultfd_read and
 * UFFDIO_COPY|ZEROPAGE are being run simultaneously on two different
 * threads.
 */
static inline bool userfaultfd_must_wait(struct userfaultfd_ctx *ctx,
					 unsigned long address,
					 unsigned long flags,
					 unsigned long reason)
{
	struct mm_struct *mm = ctx->mm;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd, _pmd;
	pte_t *pte;
	bool ret = true;

	VM_BUG_ON(!rwsem_is_locked(&mm->mmap_sem));

	pgd = pgd_offset(mm, address);
	if (!pgd_present(*pgd))
		goto out;
	pud = pud_offset(pgd, address);
	if (!pud_present(*pud))
		goto out;
	pmd = pmd_offset(pud, address);
	/*
	 * READ_ONCE must function as a barrier with narrower scope
	 * and it must be equivalent to:
	 *	_pmd = *pmd; barrier();
	 *
	 * This is to deal with the instability (as in
	 * pmd_trans_unstable) of the pmd.
	 */
	_pmd = READ_ONCE(*pmd);
	if (!pmd_present(_pmd))
		goto out;

	ret = false;
	if (pmd_trans_huge(_pmd))
		goto out;

	/*
	 * the pmd is stable (as in !pmd_trans_unstable) so we can re-read it
	 * and use the standard pte_offset_map() instead of parsing _pmd.
	 */
	pte = pte_offset_map(pmd, address);
	/*
	 * Lockless access: we're in a wait_event so it's ok if it
	 * changes under us.
	 */
	if (pte_none(*pte))
		ret = true;
	pte_unmap(pte);

out:
	return ret;
}

/*
 * The locking rules involved in returning VM_FAULT_RETRY depending on
 * FAULT_FLAG_ALLOW_RETRY, FAULT_FLAG_RETRY_NOWAIT and
 * FAULT_FLAG_KILLABLE are not straightforward. The "Caution"
 * recommendation in __lock_page_or_retry is not an understatement.
 *
 * If FAULT_FLAG_ALLOW_RETRY is set, the mmap_sem must be released
 * before returning VM_FAULT_RETRY only if FAULT_FLAG_RETRY_NOWAIT is
 * not set.
 *
 * If FAULT_FLAG_ALLOW_RETRY is set but FAULT_FLAG_KILLABLE is not
 * set, VM_FAULT_RETRY can still be returned if and only if there are
 * fatal_signal_pending()s, and the mmap_sem must be released before
 * returning it.
 */
int handle_userfault(struct vm_area_struct *vma, unsigned long address,
		     unsigned int flags, unsigned long reason)
{
	struct mm_struct *mm = vma->vm_mm;
	struct userfaultfd_ctx *ctx;
	struct userfaultfd_wait_queue uwq;
	int ret;
	bool must_wait, return_to_userland;

	BUG_ON(!rwsem_is_locked(&mm->mmap_sem));

	ret = VM_FAULT_SIGBUS;
	ctx = vma->vm_userfaultfd_ctx.ctx;
	if (!ctx)
		goto out;

	BUG_ON(ctx->mm != mm);

	VM_BUG_ON(reason & ~(VM_UFFD_MISSING|VM_UFFD_WP));
	VM_BUG_ON(!(reason & VM_UFFD_MISSING) ^ !!(reason & VM_UFFD_WP));

	/*
	 * If it's already released don't get it. This avoids to loop
	 * in __get_user_pages if userfaultfd_release waits on the
	 * caller of handle_userfault to release the mmap_sem.
	 */
	if (unlikely(ACCESS_ONCE(ctx->released)))
		goto out;

	/*
	 * Check that we can return VM_FAULT_RETRY.
	 *
	 * NOTE: it should become possible to return VM_FAULT_RETRY
	 * even if FAULT_FLAG_TRIED is set without leading to gup()
	 * -EBUSY failures, if the userfaultfd is to be extended for
	 * VM_UFFD_WP tracking and we intend to arm the userfault
	 * without first stopping userland access to the memory. For
	 * VM_UFFD_MISSING userfaults this is enough for now.
	 */
	if (unlikely(!(flags & FAULT_FLAG_ALLOW_RETRY))) {
		/*
		 * Validate the invariant that nowait must allow retry
		 * to be sure not to return SIGBUS erroneously on
		 * nowait invocations.
		 */
		BUG_ON(flags & FAULT_FLAG_RETRY_NOWAIT);
#ifdef CONFIG_DEBUG_VM
		if (printk_ratelimit()) {
			printk(KERN_WARNING
			       "FAULT_FLAG_ALLOW_RETRY missing %x\n", flags);
			dump_stack();
		}
#endif
		goto out;
	}

	/*
	 * Handle nowait, not much to do other than tell it to retry
	 * and wait.
	 */
	ret = VM_FAULT_RETRY;
	if (flags & FAULT_FLAG_RETRY_NOWAIT)
		goto out;

	/* take the reference before dropping the mmap_sem */
	userfaultfd_ctx_get(ctx);

	init_waitqueue_func_entry(&uwq.wq, userfaultfd_wake_function);
	uwq.wq.private = current;
	uwq.msg = userfault_msg(address, flags, reason);
	uwq.ctx = ctx;

	return_to_userland = (flags & (FAULT_FLAG_USER|FAULT_FLAG_KILLABLE)) ==
		(FAULT_FLAG_USER|FAULT_FLAG_KILLABLE);

	spin_lock(&ctx->fault_pending_wqh.lock);
	/*
	 * After the __add_wait_queue the uwq is visible to userland
	 * through poll/read().
	 */
	__add_wait_queue(&ctx->fault_pending_wqh, &uwq.wq);
	/*
	 * The smp_mb() after __set_current_state prevents the reads
	 * following the spin_unlock to happen before the list_add in
	 * __add_wait_queue.
	 */
	set_current_state(return_to_userland ? TASK_INTERRUPTIBLE :
			  TASK_KILLABLE);
	spin_unlock(&ctx->fault_pending_wqh.lock);

	must_wait = userfaultfd_must_wait(ctx, address, flags, reason);
	up_read(&mm->mmap_sem);

	if (likely(must_wait && !ACCESS_ONCE(ctx->released) &&
		   (return_to_userland ? !signal_pending(current) :
		    !fatal_signal_pending(current)))) {
		wake_up_poll(&ctx->fd_wqh, POLLIN);
		schedule();
		ret |= VM_FAULT_MAJOR;
	}

	__set_current_state(TASK_RUNNING);

	if (return_to_userland) {
		if (signal_pending(current) &&
		    !fatal_signal_pending(current)) {
			/*
			 * If we got a SIGSTOP or SIGCONT and this is
			 * a normal userland page fault, just let
			 * userland return so the signal will be
			 * handled and gdb debugging works.  The page
			 * fault code immediately after we return from
			 * this function is going to release the
			 * mmap_sem and it's not depending on it
			 * (unlike gup would if we were not to return
			 * VM_FAULT_RETRY).
			 *
			 * If a fatal signal is pending we still take
			 * the streamlined VM_FAULT_RETRY failure path
			 * and there's no need to retake the mmap_sem
			 * in such case.
			 */
			down_read(&mm->mmap_sem);
			ret = 0;
		}
	}

	/*
	 * Here we race with the list_del; list_add in
	 * userfaultfd_ctx_read(), however because we don't ever run
	 * list_del_init() to refile across the two lists, the prev
	 * and next pointers will never point to self. list_add also
	 * would never let any of the two pointers to point to
	 * self. So list_empty_careful won't risk to see both pointers
	 * pointing to self at any time during the list refile. The
	 * only case where list_del_init() is called is the full
	 * removal in the wake function and there we don't re-list_add
	 * and it's fine not to block on the spinlock. The uwq on this
	 * kernel stack can be released after the list_del_init.
	 */
	if (!list_empty_careful(&uwq.wq.task_list)) {
		spin_lock(&ctx->fault_pending_wqh.lock);
		/*
		 * No need of list_del_init(), the uwq on the stack
		 * will be freed shortly anyway.
		 */
		list_del(&uwq.wq.task_list);
		spin_unlock(&ctx->fault_pending_wqh.lock);
	}

	/*
	 * ctx may go away after this if the userfault pseudo fd is
	 * already released.
	 */
	userfaultfd_ctx_put(ctx);

out:
	return ret;
}

static int userfaultfd_release(struct inode *inode, struct file *file)
{
	struct userfaultfd_ctx *ctx = file->private_data;
	struct mm_struct *mm = ctx->mm;
	struct vm_area_struct *vma, *prev;
	/* len == 0 means wake all */
	struct userfaultfd_wake_range range = { .len = 0, };
	unsigned long new_flags;

	ACCESS_ONCE(ctx->released) = true;

	/*
	 * Flush page faults out of all CPUs. NOTE: all page faults
	 * must be retried without returning VM_FAULT_SIGBUS if
	 * userfaultfd_ctx_get() succeeds but vma->vma_userfault_ctx
	 * changes while handle_userfault released the mmap_sem. So
	 * it's critical that released is set to true (above), before
	 * taking the mmap_sem for writing.
	 */
	down_write(&mm->mmap_sem);
	prev = NULL;
	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		cond_resched();
		BUG_ON(!!vma->vm_userfaultfd_ctx.ctx ^
		       !!(vma->vm_flags & (VM_UFFD_MISSING | VM_UFFD_WP)));
		if (vma->vm_userfaultfd_ctx.ctx != ctx) {
			prev = vma;
			continue;
		}
		new_flags = vma->vm_flags & ~(VM_UFFD_MISSING | VM_UFFD_WP);
		prev = vma_merge(mm, prev, vma->vm_start, vma->vm_end,
				 new_flags, vma->anon_vma,
				 vma->vm_file, vma->vm_pgoff,
				 vma_policy(vma),
				 NULL_VM_UFFD_CTX,
				 vma_get_anon_name(vma));
		if (prev)
			vma = prev;
		else
			prev = vma;
		vma->vm_flags = new_flags;
		vma->vm_userfaultfd_ctx = NULL_VM_UFFD_CTX;
	}
	up_write(&mm->mmap_sem);

	/*
	 * After no new page faults can wait on this fault_*wqh, flush
	 * the last page faults that may have been already waiting on
	 * the fault_*wqh.
	 */
	spin_lock(&ctx->fault_pending_wqh.lock);
	__wake_up_locked_key(&ctx->fault_pending_wqh, TASK_NORMAL, &range);
	__wake_up_locked_key(&ctx->fault_wqh, TASK_NORMAL, &range);
	spin_unlock(&ctx->fault_pending_wqh.lock);

	wake_up_poll(&ctx->fd_wqh, POLLHUP);
	userfaultfd_ctx_put(ctx);
	return 0;
}

/* fault_pending_wqh.lock must be hold by the caller */
static inline struct userfaultfd_wait_queue *find_userfault(
	struct userfaultfd_ctx *ctx)
{
	wait_queue_t *wq;
	struct userfaultfd_wait_queue *uwq;

	VM_BUG_ON(!spin_is_locked(&ctx->fault_pending_wqh.lock));

	uwq = NULL;
	if (!waitqueue_active(&ctx->fault_pending_wqh))
		goto out;
	/* walk in reverse to provide FIFO behavior to read userfaults */
	wq = list_last_entry(&ctx->fault_pending_wqh.task_list,
			     typeof(*wq), task_list);
	uwq = container_of(wq, struct userfaultfd_wait_queue, wq);
out:
	return uwq;
}

static unsigned int userfaultfd_poll(struct file *file, poll_table *wait)
{
	struct userfaultfd_ctx *ctx = file->private_data;
	unsigned int ret;

	poll_wait(file, &ctx->fd_wqh, wait);

	switch (ctx->state) {
	case UFFD_STATE_WAIT_API:
		return POLLERR;
	case UFFD_STATE_RUNNING:
		/*
		 * poll() never guarantees that read won't block.
		 * userfaults can be waken before they're read().
		 */
		if (unlikely(!(file->f_flags & O_NONBLOCK)))
			return POLLERR;
		/*
		 * lockless access to see if there are pending faults
		 * __pollwait last action is the add_wait_queue but
		 * the spin_unlock would allow the waitqueue_active to
		 * pass above the actual list_add inside
		 * add_wait_queue critical section. So use a full
		 * memory barrier to serialize the list_add write of
		 * add_wait_queue() with the waitqueue_active read
		 * below.
		 */
		ret = 0;
		smp_mb();
		if (waitqueue_active(&ctx->fault_pending_wqh))
			ret = POLLIN;
		return ret;
	default:
		BUG();
	}
}

static ssize_t userfaultfd_ctx_read(struct userfaultfd_ctx *ctx, int no_wait,
				    struct uffd_msg *msg)
{
	ssize_t ret;
	DECLARE_WAITQUEUE(wait, current);
	struct userfaultfd_wait_queue *uwq;

	/* always take the fd_wqh lock before the fault_pending_wqh lock */
	spin_lock(&ctx->fd_wqh.lock);
	__add_wait_queue(&ctx->fd_wqh, &wait);
	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);
		spin_lock(&ctx->fault_pending_wqh.lock);
		uwq = find_userfault(ctx);
		if (uwq) {
			/*
			 * Use a seqcount to repeat the lockless check
			 * in wake_userfault() to avoid missing
			 * wakeups because during the refile both
			 * waitqueue could become empty if this is the
			 * only userfault.
			 */
			write_seqcount_begin(&ctx->refile_seq);

			/*
			 * The fault_pending_wqh.lock prevents the uwq
			 * to disappear from under us.
			 *
			 * Refile this userfault from
			 * fault_pending_wqh to fault_wqh, it's not
			 * pending anymore after we read it.
			 *
			 * Use list_del() by hand (as
			 * userfaultfd_wake_function also uses
			 * list_del_init() by hand) to be sure nobody
			 * changes __remove_wait_queue() to use
			 * list_del_init() in turn breaking the
			 * !list_empty_careful() check in
			 * handle_userfault(). The uwq->wq.task_list
			 * must never be empty at any time during the
			 * refile, or the waitqueue could disappear
			 * from under us. The "wait_queue_head_t"
			 * parameter of __remove_wait_queue() is unused
			 * anyway.
			 */
			list_del(&uwq->wq.task_list);
			__add_wait_queue(&ctx->fault_wqh, &uwq->wq);

			write_seqcount_end(&ctx->refile_seq);

			/* careful to always initialize msg if ret == 0 */
			*msg = uwq->msg;
			spin_unlock(&ctx->fault_pending_wqh.lock);
			ret = 0;
			break;
		}
		spin_unlock(&ctx->fault_pending_wqh.lock);
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}
		if (no_wait) {
			ret = -EAGAIN;
			break;
		}
		spin_unlock(&ctx->fd_wqh.lock);
		schedule();
		spin_lock(&ctx->fd_wqh.lock);
	}
	__remove_wait_queue(&ctx->fd_wqh, &wait);
	__set_current_state(TASK_RUNNING);
	spin_unlock(&ctx->fd_wqh.lock);

	return ret;
}

static ssize_t userfaultfd_read(struct file *file, char __user *buf,
				size_t count, loff_t *ppos)
{
	struct userfaultfd_ctx *ctx = file->private_data;
	ssize_t _ret, ret = 0;
	struct uffd_msg msg;
	int no_wait = file->f_flags & O_NONBLOCK;

	if (ctx->state == UFFD_STATE_WAIT_API)
		return -EINVAL;

	for (;;) {
		if (count < sizeof(msg))
			return ret ? ret : -EINVAL;
		_ret = userfaultfd_ctx_read(ctx, no_wait, &msg);
		if (_ret < 0)
			return ret ? ret : _ret;
		if (copy_to_user((__u64 __user *) buf, &msg, sizeof(msg)))
			return ret ? ret : -EFAULT;
		ret += sizeof(msg);
		buf += sizeof(msg);
		count -= sizeof(msg);
		/*
		 * Allow to read more than one fault at time but only
		 * block if waiting for the very first one.
		 */
		no_wait = O_NONBLOCK;
	}
}

static void __wake_userfault(struct userfaultfd_ctx *ctx,
			     struct userfaultfd_wake_range *range)
{
	unsigned long start, end;

	start = range->start;
	end = range->start + range->len;

	spin_lock(&ctx->fault_pending_wqh.lock);
	/* wake all in the range and autoremove */
	if (waitqueue_active(&ctx->fault_pending_wqh))
		__wake_up_locked_key(&ctx->fault_pending_wqh, TASK_NORMAL,
				     range);
	if (waitqueue_active(&ctx->fault_wqh))
		__wake_up_locked_key(&ctx->fault_wqh, TASK_NORMAL, range);
	spin_unlock(&ctx->fault_pending_wqh.lock);
}

static __always_inline void wake_userfault(struct userfaultfd_ctx *ctx,
					   struct userfaultfd_wake_range *range)
{
	unsigned seq;
	bool need_wakeup;

	/*
	 * To be sure waitqueue_active() is not reordered by the CPU
	 * before the pagetable update, use an explicit SMP memory
	 * barrier here. PT lock release or up_read(mmap_sem) still
	 * have release semantics that can allow the
	 * waitqueue_active() to be reordered before the pte update.
	 */
	smp_mb();

	/*
	 * Use waitqueue_active because it's very frequent to
	 * change the address space atomically even if there are no
	 * userfaults yet. So we take the spinlock only when we're
	 * sure we've userfaults to wake.
	 */
	do {
		seq = read_seqcount_begin(&ctx->refile_seq);
		need_wakeup = waitqueue_active(&ctx->fault_pending_wqh) ||
			waitqueue_active(&ctx->fault_wqh);
		cond_resched();
	} while (read_seqcount_retry(&ctx->refile_seq, seq));
	if (need_wakeup)
		__wake_userfault(ctx, range);
}

static __always_inline int validate_range(struct mm_struct *mm,
					  __u64 start, __u64 len)
{
	__u64 task_size = mm->task_size;

	if (start & ~PAGE_MASK)
		return -EINVAL;
	if (len & ~PAGE_MASK)
		return -EINVAL;
	if (!len)
		return -EINVAL;
	if (start < mmap_min_addr)
		return -EINVAL;
	if (start >= task_size)
		return -EINVAL;
	if (len > task_size - start)
		return -EINVAL;
	return 0;
}

static int userfaultfd_register(struct userfaultfd_ctx *ctx,
				unsigned long arg)
{
	struct mm_struct *mm = ctx->mm;
	struct vm_area_struct *vma, *prev, *cur;
	int ret;
	struct uffdio_register uffdio_register;
	struct uffdio_register __user *user_uffdio_register;
	unsigned long vm_flags, new_flags;
	bool found;
	unsigned long start, end, vma_end;

	user_uffdio_register = (struct uffdio_register __user *) arg;

	ret = -EFAULT;
	if (copy_from_user(&uffdio_register, user_uffdio_register,
			   sizeof(uffdio_register)-sizeof(__u64)))
		goto out;

	ret = -EINVAL;
	if (!uffdio_register.mode)
		goto out;
	if (uffdio_register.mode & ~(UFFDIO_REGISTER_MODE_MISSING|
				     UFFDIO_REGISTER_MODE_WP))
		goto out;
	vm_flags = 0;
	if (uffdio_register.mode & UFFDIO_REGISTER_MODE_MISSING)
		vm_flags |= VM_UFFD_MISSING;
	if (uffdio_register.mode & UFFDIO_REGISTER_MODE_WP) {
		vm_flags |= VM_UFFD_WP;
		/*
		 * FIXME: remove the below error constraint by
		 * implementing the wprotect tracking mode.
		 */
		ret = -EINVAL;
		goto out;
	}

	ret = validate_range(mm, uffdio_register.range.start,
			     uffdio_register.range.len);
	if (ret)
		goto out;

	start = uffdio_register.range.start;
	end = start + uffdio_register.range.len;

	down_write(&mm->mmap_sem);
	vma = find_vma_prev(mm, start, &prev);

	ret = -ENOMEM;
	if (!vma)
		goto out_unlock;

	/* check that there's at least one vma in the range */
	ret = -EINVAL;
	if (vma->vm_start >= end)
		goto out_unlock;

	/*
	 * Search for not compatible vmas.
	 *
	 * FIXME: this shall be relaxed later so that it doesn't fail
	 * on tmpfs backed vmas (in addition to the current allowance
	 * on anonymous vmas).
	 */
	found = false;
	for (cur = vma; cur && cur->vm_start < end; cur = cur->vm_next) {
		cond_resched();

		BUG_ON(!!cur->vm_userfaultfd_ctx.ctx ^
		       !!(cur->vm_flags & (VM_UFFD_MISSING | VM_UFFD_WP)));

		/* check not compatible vmas */
		ret = -EINVAL;
		if (cur->vm_ops)
			goto out_unlock;

		/*
		 * Check that this vma isn't already owned by a
		 * different userfaultfd. We can't allow more than one
		 * userfaultfd to own a single vma simultaneously or we
		 * wouldn't know which one to deliver the userfaults to.
		 */
		ret = -EBUSY;
		if (cur->vm_userfaultfd_ctx.ctx &&
		    cur->vm_userfaultfd_ctx.ctx != ctx)
			goto out_unlock;

		found = true;
	}
	BUG_ON(!found);

	if (vma->vm_start < start)
		prev = vma;

	ret = 0;
	do {
		cond_resched();

		BUG_ON(vma->vm_ops);
		BUG_ON(vma->vm_userfaultfd_ctx.ctx &&
		       vma->vm_userfaultfd_ctx.ctx != ctx);

		/*
		 * Nothing to do: this vma is already registered into this
		 * userfaultfd and with the right tracking mode too.
		 */
		if (vma->vm_userfaultfd_ctx.ctx == ctx &&
		    (vma->vm_flags & vm_flags) == vm_flags)
			goto skip;

		if (vma->vm_start > start)
			start = vma->vm_start;
		vma_end = min(end, vma->vm_end);

		new_flags = (vma->vm_flags & ~vm_flags) | vm_flags;
		prev = vma_merge(mm, prev, start, vma_end, new_flags,
				 vma->anon_vma, vma->vm_file, vma->vm_pgoff,
				 vma_policy(vma),
				 ((struct vm_userfaultfd_ctx){ ctx }),
				 vma_get_anon_name(vma));
		if (prev) {
			vma = prev;
			goto next;
		}
		if (vma->vm_start < start) {
			ret = split_vma(mm, vma, start, 1);
			if (ret)
				break;
		}
		if (vma->vm_end > end) {
			ret = split_vma(mm, vma, end, 0);
			if (ret)
				break;
		}
	next:
		/*
		 * In the vma_merge() successful mprotect-like case 8:
		 * the next vma was merged into the current one and
		 * the current one has not been updated yet.
		 */
		vma->vm_flags = new_flags;
		vma->vm_userfaultfd_ctx.ctx = ctx;

	skip:
		prev = vma;
		start = vma->vm_end;
		vma = vma->vm_next;
	} while (vma && vma->vm_start < end);
out_unlock:
	up_write(&mm->mmap_sem);
	if (!ret) {
		/*
		 * Now that we scanned all vmas we can already tell
		 * userland which ioctls methods are guaranteed to
		 * succeed on this range.
		 */
		if (put_user(UFFD_API_RANGE_IOCTLS,
			     &user_uffdio_register->ioctls))
			ret = -EFAULT;
	}
out:
	return ret;
}

static int userfaultfd_unregister(struct userfaultfd_ctx *ctx,
				  unsigned long arg)
{
	struct mm_struct *mm = ctx->mm;
	struct vm_area_struct *vma, *prev, *cur;
	int ret;
	struct uffdio_range uffdio_unregister;
	unsigned long new_flags;
	bool found;
	unsigned long start, end, vma_end;
	const void __user *buf = (void __user *)arg;

	ret = -EFAULT;
	if (copy_from_user(&uffdio_unregister, buf, sizeof(uffdio_unregister)))
		goto out;

	ret = validate_range(mm, uffdio_unregister.start,
			     uffdio_unregister.len);
	if (ret)
		goto out;

	start = uffdio_unregister.start;
	end = start + uffdio_unregister.len;

	down_write(&mm->mmap_sem);
	vma = find_vma_prev(mm, start, &prev);

	ret = -ENOMEM;
	if (!vma)
		goto out_unlock;

	/* check that there's at least one vma in the range */
	ret = -EINVAL;
	if (vma->vm_start >= end)
		goto out_unlock;

	/*
	 * Search for not compatible vmas.
	 *
	 * FIXME: this shall be relaxed later so that it doesn't fail
	 * on tmpfs backed vmas (in addition to the current allowance
	 * on anonymous vmas).
	 */
	found = false;
	ret = -EINVAL;
	for (cur = vma; cur && cur->vm_start < end; cur = cur->vm_next) {
		cond_resched();

		BUG_ON(!!cur->vm_userfaultfd_ctx.ctx ^
		       !!(cur->vm_flags & (VM_UFFD_MISSING | VM_UFFD_WP)));

		/*
		 * Check not compatible vmas, not strictly required
		 * here as not compatible vmas cannot have an
		 * userfaultfd_ctx registered on them, but this
		 * provides for more strict behavior to notice
		 * unregistration errors.
		 */
		if (cur->vm_ops)
			goto out_unlock;

		found = true;
	}
	BUG_ON(!found);

	if (vma->vm_start < start)
		prev = vma;

	ret = 0;
	do {
		cond_resched();

		BUG_ON(vma->vm_ops);

		/*
		 * Nothing to do: this vma is already registered into this
		 * userfaultfd and with the right tracking mode too.
		 */
		if (!vma->vm_userfaultfd_ctx.ctx)
			goto skip;

		if (vma->vm_start > start)
			start = vma->vm_start;
		vma_end = min(end, vma->vm_end);

		new_flags = vma->vm_flags & ~(VM_UFFD_MISSING | VM_UFFD_WP);
		prev = vma_merge(mm, prev, start, vma_end, new_flags,
				 vma->anon_vma, vma->vm_file, vma->vm_pgoff,
				 vma_policy(vma),
				 NULL_VM_UFFD_CTX,
				 vma_get_anon_name(vma));
		if (prev) {
			vma = prev;
			goto next;
		}
		if (vma->vm_start < start) {
			ret = split_vma(mm, vma, start, 1);
			if (ret)
				break;
		}
		if (vma->vm_end > end) {
			ret = split_vma(mm, vma, end, 0);
			if (ret)
				break;
		}
	next:
		/*
		 * In the vma_merge() successful mprotect-like case 8:
		 * the next vma was merged into the current one and
		 * the current one has not been updated yet.
		 */
		vma->vm_flags = new_flags;
		vma->vm_userfaultfd_ctx = NULL_VM_UFFD_CTX;

	skip:
		prev = vma;
		start = vma->vm_end;
		vma = vma->vm_next;
	} while (vma && vma->vm_start < end);
out_unlock:
	up_write(&mm->mmap_sem);
out:
	return ret;
}

/*
 * userfaultfd_wake may be used in combination with the
 * UFFDIO_*_MODE_DONTWAKE to wakeup userfaults in batches.
 */
static int userfaultfd_wake(struct userfaultfd_ctx *ctx,
			    unsigned long arg)
{
	int ret;
	struct uffdio_range uffdio_wake;
	struct userfaultfd_wake_range range;
	const void __user *buf = (void __user *)arg;

	ret = -EFAULT;
	if (copy_from_user(&uffdio_wake, buf, sizeof(uffdio_wake)))
		goto out;

	ret = validate_range(ctx->mm, uffdio_wake.start, uffdio_wake.len);
	if (ret)
		goto out;

	range.start = uffdio_wake.start;
	range.len = uffdio_wake.len;

	/*
	 * len == 0 means wake all and we don't want to wake all here,
	 * so check it again to be sure.
	 */
	VM_BUG_ON(!range.len);

	wake_userfault(ctx, &range);
	ret = 0;

out:
	return ret;
}

static int userfaultfd_copy(struct userfaultfd_ctx *ctx,
			    unsigned long arg)
{
	__s64 ret;
	struct uffdio_copy uffdio_copy;
	struct uffdio_copy __user *user_uffdio_copy;
	struct userfaultfd_wake_range range;

	user_uffdio_copy = (struct uffdio_copy __user *) arg;

	ret = -EFAULT;
	if (copy_from_user(&uffdio_copy, user_uffdio_copy,
			   /* don't copy "copy" last field */
			   sizeof(uffdio_copy)-sizeof(__s64)))
		goto out;

	ret = validate_range(ctx->mm, uffdio_copy.dst, uffdio_copy.len);
	if (ret)
		goto out;
	/*
	 * double check for wraparound just in case. copy_from_user()
	 * will later check uffdio_copy.src + uffdio_copy.len to fit
	 * in the userland range.
	 */
	ret = -EINVAL;
	if (uffdio_copy.src + uffdio_copy.len <= uffdio_copy.src)
		goto out;
	if (uffdio_copy.mode & ~UFFDIO_COPY_MODE_DONTWAKE)
		goto out;

	ret = mcopy_atomic(ctx->mm, uffdio_copy.dst, uffdio_copy.src,
			   uffdio_copy.len);
	if (unlikely(put_user(ret, &user_uffdio_copy->copy)))
		return -EFAULT;
	if (ret < 0)
		goto out;
	BUG_ON(!ret);
	/* len == 0 would wake all */
	range.len = ret;
	if (!(uffdio_copy.mode & UFFDIO_COPY_MODE_DONTWAKE)) {
		range.start = uffdio_copy.dst;
		wake_userfault(ctx, &range);
	}
	ret = range.len == uffdio_copy.len ? 0 : -EAGAIN;
out:
	return ret;
}

static int userfaultfd_zeropage(struct userfaultfd_ctx *ctx,
				unsigned long arg)
{
	__s64 ret;
	struct uffdio_zeropage uffdio_zeropage;
	struct uffdio_zeropage __user *user_uffdio_zeropage;
	struct userfaultfd_wake_range range;

	user_uffdio_zeropage = (struct uffdio_zeropage __user *) arg;

	ret = -EFAULT;
	if (copy_from_user(&uffdio_zeropage, user_uffdio_zeropage,
			   /* don't copy "zeropage" last field */
			   sizeof(uffdio_zeropage)-sizeof(__s64)))
		goto out;

	ret = validate_range(ctx->mm, uffdio_zeropage.range.start,
			     uffdio_zeropage.range.len);
	if (ret)
		goto out;
	ret = -EINVAL;
	if (uffdio_zeropage.mode & ~UFFDIO_ZEROPAGE_MODE_DONTWAKE)
		goto out;

	ret = mfill_zeropage(ctx->mm, uffdio_zeropage.range.start,
			     uffdio_zeropage.range.len);
	if (unlikely(put_user(ret, &user_uffdio_zeropage->zeropage)))
		return -EFAULT;
	if (ret < 0)
		goto out;
	/* len == 0 would wake all */
	BUG_ON(!ret);
	range.len = ret;
	if (!(uffdio_zeropage.mode & UFFDIO_ZEROPAGE_MODE_DONTWAKE)) {
		range.start = uffdio_zeropage.range.start;
		wake_userfault(ctx, &range);
	}
	ret = range.len == uffdio_zeropage.range.len ? 0 : -EAGAIN;
out:
	return ret;
}

/*
 * userland asks for a certain API version and we return which bits
 * and ioctl commands are implemented in this kernel for such API
 * version or -EINVAL if unknown.
 */
static int userfaultfd_api(struct userfaultfd_ctx *ctx,
			   unsigned long arg)
{
	struct uffdio_api uffdio_api;
	void __user *buf = (void __user *)arg;
	int ret;

	ret = -EINVAL;
	if (ctx->state != UFFD_STATE_WAIT_API)
		goto out;
	ret = -EFAULT;
	if (copy_from_user(&uffdio_api, buf, sizeof(uffdio_api)))
		goto out;
	if (uffdio_api.api != UFFD_API || uffdio_api.features) {
		memset(&uffdio_api, 0, sizeof(uffdio_api));
		if (copy_to_user(buf, &uffdio_api, sizeof(uffdio_api)))
			goto out;
		ret = -EINVAL;
		goto out;
	}
	uffdio_api.features = UFFD_API_FEATURES;
	uffdio_api.ioctls = UFFD_API_IOCTLS;
	ret = -EFAULT;
	if (copy_to_user(buf, &uffdio_api, sizeof(uffdio_api)))
		goto out;
	ctx->state = UFFD_STATE_RUNNING;
	ret = 0;
out:
	return ret;
}

static long userfaultfd_ioctl(struct file *file, unsigned cmd,
			      unsigned long arg)
{
	int ret = -EINVAL;
	struct userfaultfd_ctx *ctx = file->private_data;

	if (cmd != UFFDIO_API && ctx->state == UFFD_STATE_WAIT_API)
		return -EINVAL;

	switch(cmd) {
	case UFFDIO_API:
		ret = userfaultfd_api(ctx, arg);
		break;
	case UFFDIO_REGISTER:
		ret = userfaultfd_register(ctx, arg);
		break;
	case UFFDIO_UNREGISTER:
		ret = userfaultfd_unregister(ctx, arg);
		break;
	case UFFDIO_WAKE:
		ret = userfaultfd_wake(ctx, arg);
		break;
	case UFFDIO_COPY:
		ret = userfaultfd_copy(ctx, arg);
		break;
	case UFFDIO_ZEROPAGE:
		ret = userfaultfd_zeropage(ctx, arg);
		break;
	}
	return ret;
}

#ifdef CONFIG_PROC_FS
static void userfaultfd_show_fdinfo(struct seq_file *m, struct file *f)
{
	struct userfaultfd_ctx *ctx = f->private_data;
	wait_queue_t *wq;
	struct userfaultfd_wait_queue *uwq;
	unsigned long pending = 0, total = 0;

	spin_lock(&ctx->fault_pending_wqh.lock);
	list_for_each_entry(wq, &ctx->fault_pending_wqh.task_list, task_list) {
		uwq = container_of(wq, struct userfaultfd_wait_queue, wq);
		pending++;
		total++;
	}
	list_for_each_entry(wq, &ctx->fault_wqh.task_list, task_list) {
		uwq = container_of(wq, struct userfaultfd_wait_queue, wq);
		total++;
	}
	spin_unlock(&ctx->fault_pending_wqh.lock);

	/*
	 * If more protocols will be added, there will be all shown
	 * separated by a space. Like this:
	 *	protocols: aa:... bb:...
	 */
	seq_printf(m, "pending:\t%lu\ntotal:\t%lu\nAPI:\t%Lx:%x:%Lx\n",
		   pending, total, UFFD_API, UFFD_API_FEATURES,
		   UFFD_API_IOCTLS|UFFD_API_RANGE_IOCTLS);
}
#endif

static const struct file_operations userfaultfd_fops = {
#ifdef CONFIG_PROC_FS
	.show_fdinfo	= userfaultfd_show_fdinfo,
#endif
	.release	= userfaultfd_release,
	.poll		= userfaultfd_poll,
	.read		= userfaultfd_read,
	.unlocked_ioctl = userfaultfd_ioctl,
	.compat_ioctl	= userfaultfd_ioctl,
	.llseek		= noop_llseek,
};

static void init_once_userfaultfd_ctx(void *mem)
{
	struct userfaultfd_ctx *ctx = (struct userfaultfd_ctx *) mem;

	init_waitqueue_head(&ctx->fault_pending_wqh);
	init_waitqueue_head(&ctx->fault_wqh);
	init_waitqueue_head(&ctx->fd_wqh);
	seqcount_init(&ctx->refile_seq);
}

/**
 * userfaultfd_file_create - Creates an userfaultfd file pointer.
 * @flags: Flags for the userfaultfd file.
 *
 * This function creates an userfaultfd file pointer, w/out installing
 * it into the fd table. This is useful when the userfaultfd file is
 * used during the initialization of data structures that require
 * extra setup after the userfaultfd creation. So the userfaultfd
 * creation is split into the file pointer creation phase, and the
 * file descriptor installation phase.  In this way races with
 * userspace closing the newly installed file descriptor can be
 * avoided.  Returns an userfaultfd file pointer, or a proper error
 * pointer.
 */
static struct file *userfaultfd_file_create(int flags)
{
	struct file *file;
	struct userfaultfd_ctx *ctx;

	BUG_ON(!current->mm);

	/* Check the UFFD_* constants for consistency.  */
	BUILD_BUG_ON(UFFD_CLOEXEC != O_CLOEXEC);
	BUILD_BUG_ON(UFFD_NONBLOCK != O_NONBLOCK);

	file = ERR_PTR(-EINVAL);
	if (flags & ~UFFD_SHARED_FCNTL_FLAGS)
		goto out;

	file = ERR_PTR(-ENOMEM);
	ctx = kmem_cache_alloc(userfaultfd_ctx_cachep, GFP_KERNEL);
	if (!ctx)
		goto out;

	atomic_set(&ctx->refcount, 1);
	ctx->flags = flags;
	ctx->state = UFFD_STATE_WAIT_API;
	ctx->released = false;
	ctx->mm = current->mm;
	/* prevent the mm struct to be freed */
	atomic_inc(&ctx->mm->mm_users);

	file = anon_inode_getfile("[userfaultfd]", &userfaultfd_fops, ctx,
				  O_RDWR | (flags & UFFD_SHARED_FCNTL_FLAGS));
	if (IS_ERR(file)) {
		mmput(ctx->mm);
		kmem_cache_free(userfaultfd_ctx_cachep, ctx);
	}
out:
	return file;
}

SYSCALL_DEFINE1(userfaultfd, int, flags)
{
	int fd, error;
	struct file *file;

	error = get_unused_fd_flags(flags & UFFD_SHARED_FCNTL_FLAGS);
	if (error < 0)
		return error;
	fd = error;

	file = userfaultfd_file_create(flags);
	if (IS_ERR(file)) {
		error = PTR_ERR(file);
		goto err_put_unused_fd;
	}
	fd_install(fd, file);

	return fd;

err_put_unused_fd:
	put_unused_fd(fd);

	return error;
}

static int __init userfaultfd_init(void)
{
	userfaultfd_ctx_cachep = kmem_cache_create("userfaultfd_ctx_cache",
						sizeof(struct userfaultfd_ctx),
						0,
						SLAB_HWCACHE_ALIGN|SLAB_PANIC,
						init_once_userfaultfd_ctx);
	return 0;
}
__initcall(userfaultfd_init);
