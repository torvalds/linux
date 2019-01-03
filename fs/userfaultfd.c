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

#include <linux/list.h>
#include <linux/hashtable.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
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
#include <linux/hugetlb.h>

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
	/* waitqueue head for events */
	wait_queue_head_t event_wqh;
	/* a refile sequence protected by fault_pending_wqh lock */
	struct seqcount refile_seq;
	/* pseudo fd refcounting */
	atomic_t refcount;
	/* userfaultfd syscall flags */
	unsigned int flags;
	/* features requested from the userspace */
	unsigned int features;
	/* state machine */
	enum userfaultfd_state state;
	/* released */
	bool released;
	/* memory mappings are changing because of non-cooperative event */
	bool mmap_changing;
	/* mm with one ore more vmas attached to this userfaultfd_ctx */
	struct mm_struct *mm;
};

struct userfaultfd_fork_ctx {
	struct userfaultfd_ctx *orig;
	struct userfaultfd_ctx *new;
	struct list_head list;
};

struct userfaultfd_unmap_ctx {
	struct userfaultfd_ctx *ctx;
	unsigned long start;
	unsigned long end;
	struct list_head list;
};

struct userfaultfd_wait_queue {
	struct uffd_msg msg;
	wait_queue_entry_t wq;
	struct userfaultfd_ctx *ctx;
	bool waken;
};

struct userfaultfd_wake_range {
	unsigned long start;
	unsigned long len;
};

static int userfaultfd_wake_function(wait_queue_entry_t *wq, unsigned mode,
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
	WRITE_ONCE(uwq->waken, true);
	/*
	 * The Program-Order guarantees provided by the scheduler
	 * ensure uwq->waken is visible before the task is woken.
	 */
	ret = wake_up_state(wq->private, mode);
	if (ret) {
		/*
		 * Wake only once, autoremove behavior.
		 *
		 * After the effect of list_del_init is visible to the other
		 * CPUs, the waitqueue may disappear from under us, see the
		 * !list_empty_careful() in handle_userfault().
		 *
		 * try_to_wake_up() has an implicit smp_mb(), and the
		 * wq->private is read before calling the extern function
		 * "wake_up_state" (which in turns calls try_to_wake_up).
		 */
		list_del_init(&wq->entry);
	}
out:
	return ret;
}

/**
 * userfaultfd_ctx_get - Acquires a reference to the internal userfaultfd
 * context.
 * @ctx: [in] Pointer to the userfaultfd context.
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
		VM_BUG_ON(spin_is_locked(&ctx->event_wqh.lock));
		VM_BUG_ON(waitqueue_active(&ctx->event_wqh));
		VM_BUG_ON(spin_is_locked(&ctx->fd_wqh.lock));
		VM_BUG_ON(waitqueue_active(&ctx->fd_wqh));
		mmdrop(ctx->mm);
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
					    unsigned long reason,
					    unsigned int features)
{
	struct uffd_msg msg;
	msg_init(&msg);
	msg.event = UFFD_EVENT_PAGEFAULT;
	msg.arg.pagefault.address = address;
	if (flags & FAULT_FLAG_WRITE)
		/*
		 * If UFFD_FEATURE_PAGEFAULT_FLAG_WP was set in the
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
	if (features & UFFD_FEATURE_THREAD_ID)
		msg.arg.pagefault.feat.ptid = task_pid_vnr(current);
	return msg;
}

#ifdef CONFIG_HUGETLB_PAGE
/*
 * Same functionality as userfaultfd_must_wait below with modifications for
 * hugepmd ranges.
 */
static inline bool userfaultfd_huge_must_wait(struct userfaultfd_ctx *ctx,
					 struct vm_area_struct *vma,
					 unsigned long address,
					 unsigned long flags,
					 unsigned long reason)
{
	struct mm_struct *mm = ctx->mm;
	pte_t *ptep, pte;
	bool ret = true;

	VM_BUG_ON(!rwsem_is_locked(&mm->mmap_sem));

	ptep = huge_pte_offset(mm, address, vma_mmu_pagesize(vma));

	if (!ptep)
		goto out;

	ret = false;
	pte = huge_ptep_get(ptep);

	/*
	 * Lockless access: we're in a wait_event so it's ok if it
	 * changes under us.
	 */
	if (huge_pte_none(pte))
		ret = true;
	if (!huge_pte_write(pte) && (reason & VM_UFFD_WP))
		ret = true;
out:
	return ret;
}
#else
static inline bool userfaultfd_huge_must_wait(struct userfaultfd_ctx *ctx,
					 struct vm_area_struct *vma,
					 unsigned long address,
					 unsigned long flags,
					 unsigned long reason)
{
	return false;	/* should never get here */
}
#endif /* CONFIG_HUGETLB_PAGE */

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
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd, _pmd;
	pte_t *pte;
	bool ret = true;

	VM_BUG_ON(!rwsem_is_locked(&mm->mmap_sem));

	pgd = pgd_offset(mm, address);
	if (!pgd_present(*pgd))
		goto out;
	p4d = p4d_offset(pgd, address);
	if (!p4d_present(*p4d))
		goto out;
	pud = pud_offset(p4d, address);
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
	if (pmd_none(_pmd))
		goto out;

	ret = false;
	if (!pmd_present(_pmd))
		goto out;

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
vm_fault_t handle_userfault(struct vm_fault *vmf, unsigned long reason)
{
	struct mm_struct *mm = vmf->vma->vm_mm;
	struct userfaultfd_ctx *ctx;
	struct userfaultfd_wait_queue uwq;
	vm_fault_t ret = VM_FAULT_SIGBUS;
	bool must_wait, return_to_userland;
	long blocking_state;

	/*
	 * We don't do userfault handling for the final child pid update.
	 *
	 * We also don't do userfault handling during
	 * coredumping. hugetlbfs has the special
	 * follow_hugetlb_page() to skip missing pages in the
	 * FOLL_DUMP case, anon memory also checks for FOLL_DUMP with
	 * the no_page_table() helper in follow_page_mask(), but the
	 * shmem_vm_ops->fault method is invoked even during
	 * coredumping without mmap_sem and it ends up here.
	 */
	if (current->flags & (PF_EXITING|PF_DUMPCORE))
		goto out;

	/*
	 * Coredumping runs without mmap_sem so we can only check that
	 * the mmap_sem is held, if PF_DUMPCORE was not set.
	 */
	WARN_ON_ONCE(!rwsem_is_locked(&mm->mmap_sem));

	ctx = vmf->vma->vm_userfaultfd_ctx.ctx;
	if (!ctx)
		goto out;

	BUG_ON(ctx->mm != mm);

	VM_BUG_ON(reason & ~(VM_UFFD_MISSING|VM_UFFD_WP));
	VM_BUG_ON(!(reason & VM_UFFD_MISSING) ^ !!(reason & VM_UFFD_WP));

	if (ctx->features & UFFD_FEATURE_SIGBUS)
		goto out;

	/*
	 * If it's already released don't get it. This avoids to loop
	 * in __get_user_pages if userfaultfd_release waits on the
	 * caller of handle_userfault to release the mmap_sem.
	 */
	if (unlikely(READ_ONCE(ctx->released))) {
		/*
		 * Don't return VM_FAULT_SIGBUS in this case, so a non
		 * cooperative manager can close the uffd after the
		 * last UFFDIO_COPY, without risking to trigger an
		 * involuntary SIGBUS if the process was starting the
		 * userfaultfd while the userfaultfd was still armed
		 * (but after the last UFFDIO_COPY). If the uffd
		 * wasn't already closed when the userfault reached
		 * this point, that would normally be solved by
		 * userfaultfd_must_wait returning 'false'.
		 *
		 * If we were to return VM_FAULT_SIGBUS here, the non
		 * cooperative manager would be instead forced to
		 * always call UFFDIO_UNREGISTER before it can safely
		 * close the uffd.
		 */
		ret = VM_FAULT_NOPAGE;
		goto out;
	}

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
	if (unlikely(!(vmf->flags & FAULT_FLAG_ALLOW_RETRY))) {
		/*
		 * Validate the invariant that nowait must allow retry
		 * to be sure not to return SIGBUS erroneously on
		 * nowait invocations.
		 */
		BUG_ON(vmf->flags & FAULT_FLAG_RETRY_NOWAIT);
#ifdef CONFIG_DEBUG_VM
		if (printk_ratelimit()) {
			printk(KERN_WARNING
			       "FAULT_FLAG_ALLOW_RETRY missing %x\n",
			       vmf->flags);
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
	if (vmf->flags & FAULT_FLAG_RETRY_NOWAIT)
		goto out;

	/* take the reference before dropping the mmap_sem */
	userfaultfd_ctx_get(ctx);

	init_waitqueue_func_entry(&uwq.wq, userfaultfd_wake_function);
	uwq.wq.private = current;
	uwq.msg = userfault_msg(vmf->address, vmf->flags, reason,
			ctx->features);
	uwq.ctx = ctx;
	uwq.waken = false;

	return_to_userland =
		(vmf->flags & (FAULT_FLAG_USER|FAULT_FLAG_KILLABLE)) ==
		(FAULT_FLAG_USER|FAULT_FLAG_KILLABLE);
	blocking_state = return_to_userland ? TASK_INTERRUPTIBLE :
			 TASK_KILLABLE;

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
	set_current_state(blocking_state);
	spin_unlock(&ctx->fault_pending_wqh.lock);

	if (!is_vm_hugetlb_page(vmf->vma))
		must_wait = userfaultfd_must_wait(ctx, vmf->address, vmf->flags,
						  reason);
	else
		must_wait = userfaultfd_huge_must_wait(ctx, vmf->vma,
						       vmf->address,
						       vmf->flags, reason);
	up_read(&mm->mmap_sem);

	if (likely(must_wait && !READ_ONCE(ctx->released) &&
		   (return_to_userland ? !signal_pending(current) :
		    !fatal_signal_pending(current)))) {
		wake_up_poll(&ctx->fd_wqh, EPOLLIN);
		schedule();
		ret |= VM_FAULT_MAJOR;

		/*
		 * False wakeups can orginate even from rwsem before
		 * up_read() however userfaults will wait either for a
		 * targeted wakeup on the specific uwq waitqueue from
		 * wake_userfault() or for signals or for uffd
		 * release.
		 */
		while (!READ_ONCE(uwq.waken)) {
			/*
			 * This needs the full smp_store_mb()
			 * guarantee as the state write must be
			 * visible to other CPUs before reading
			 * uwq.waken from other CPUs.
			 */
			set_current_state(blocking_state);
			if (READ_ONCE(uwq.waken) ||
			    READ_ONCE(ctx->released) ||
			    (return_to_userland ? signal_pending(current) :
			     fatal_signal_pending(current)))
				break;
			schedule();
		}
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
			ret = VM_FAULT_NOPAGE;
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
	if (!list_empty_careful(&uwq.wq.entry)) {
		spin_lock(&ctx->fault_pending_wqh.lock);
		/*
		 * No need of list_del_init(), the uwq on the stack
		 * will be freed shortly anyway.
		 */
		list_del(&uwq.wq.entry);
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

static void userfaultfd_event_wait_completion(struct userfaultfd_ctx *ctx,
					      struct userfaultfd_wait_queue *ewq)
{
	struct userfaultfd_ctx *release_new_ctx;

	if (WARN_ON_ONCE(current->flags & PF_EXITING))
		goto out;

	ewq->ctx = ctx;
	init_waitqueue_entry(&ewq->wq, current);
	release_new_ctx = NULL;

	spin_lock(&ctx->event_wqh.lock);
	/*
	 * After the __add_wait_queue the uwq is visible to userland
	 * through poll/read().
	 */
	__add_wait_queue(&ctx->event_wqh, &ewq->wq);
	for (;;) {
		set_current_state(TASK_KILLABLE);
		if (ewq->msg.event == 0)
			break;
		if (READ_ONCE(ctx->released) ||
		    fatal_signal_pending(current)) {
			/*
			 * &ewq->wq may be queued in fork_event, but
			 * __remove_wait_queue ignores the head
			 * parameter. It would be a problem if it
			 * didn't.
			 */
			__remove_wait_queue(&ctx->event_wqh, &ewq->wq);
			if (ewq->msg.event == UFFD_EVENT_FORK) {
				struct userfaultfd_ctx *new;

				new = (struct userfaultfd_ctx *)
					(unsigned long)
					ewq->msg.arg.reserved.reserved1;
				release_new_ctx = new;
			}
			break;
		}

		spin_unlock(&ctx->event_wqh.lock);

		wake_up_poll(&ctx->fd_wqh, EPOLLIN);
		schedule();

		spin_lock(&ctx->event_wqh.lock);
	}
	__set_current_state(TASK_RUNNING);
	spin_unlock(&ctx->event_wqh.lock);

	if (release_new_ctx) {
		struct vm_area_struct *vma;
		struct mm_struct *mm = release_new_ctx->mm;

		/* the various vma->vm_userfaultfd_ctx still points to it */
		down_write(&mm->mmap_sem);
		for (vma = mm->mmap; vma; vma = vma->vm_next)
			if (vma->vm_userfaultfd_ctx.ctx == release_new_ctx) {
				vma->vm_userfaultfd_ctx = NULL_VM_UFFD_CTX;
				vma->vm_flags &= ~(VM_UFFD_WP | VM_UFFD_MISSING);
			}
		up_write(&mm->mmap_sem);

		userfaultfd_ctx_put(release_new_ctx);
	}

	/*
	 * ctx may go away after this if the userfault pseudo fd is
	 * already released.
	 */
out:
	WRITE_ONCE(ctx->mmap_changing, false);
	userfaultfd_ctx_put(ctx);
}

static void userfaultfd_event_complete(struct userfaultfd_ctx *ctx,
				       struct userfaultfd_wait_queue *ewq)
{
	ewq->msg.event = 0;
	wake_up_locked(&ctx->event_wqh);
	__remove_wait_queue(&ctx->event_wqh, &ewq->wq);
}

int dup_userfaultfd(struct vm_area_struct *vma, struct list_head *fcs)
{
	struct userfaultfd_ctx *ctx = NULL, *octx;
	struct userfaultfd_fork_ctx *fctx;

	octx = vma->vm_userfaultfd_ctx.ctx;
	if (!octx || !(octx->features & UFFD_FEATURE_EVENT_FORK)) {
		vma->vm_userfaultfd_ctx = NULL_VM_UFFD_CTX;
		vma->vm_flags &= ~(VM_UFFD_WP | VM_UFFD_MISSING);
		return 0;
	}

	list_for_each_entry(fctx, fcs, list)
		if (fctx->orig == octx) {
			ctx = fctx->new;
			break;
		}

	if (!ctx) {
		fctx = kmalloc(sizeof(*fctx), GFP_KERNEL);
		if (!fctx)
			return -ENOMEM;

		ctx = kmem_cache_alloc(userfaultfd_ctx_cachep, GFP_KERNEL);
		if (!ctx) {
			kfree(fctx);
			return -ENOMEM;
		}

		atomic_set(&ctx->refcount, 1);
		ctx->flags = octx->flags;
		ctx->state = UFFD_STATE_RUNNING;
		ctx->features = octx->features;
		ctx->released = false;
		ctx->mmap_changing = false;
		ctx->mm = vma->vm_mm;
		mmgrab(ctx->mm);

		userfaultfd_ctx_get(octx);
		WRITE_ONCE(octx->mmap_changing, true);
		fctx->orig = octx;
		fctx->new = ctx;
		list_add_tail(&fctx->list, fcs);
	}

	vma->vm_userfaultfd_ctx.ctx = ctx;
	return 0;
}

static void dup_fctx(struct userfaultfd_fork_ctx *fctx)
{
	struct userfaultfd_ctx *ctx = fctx->orig;
	struct userfaultfd_wait_queue ewq;

	msg_init(&ewq.msg);

	ewq.msg.event = UFFD_EVENT_FORK;
	ewq.msg.arg.reserved.reserved1 = (unsigned long)fctx->new;

	userfaultfd_event_wait_completion(ctx, &ewq);
}

void dup_userfaultfd_complete(struct list_head *fcs)
{
	struct userfaultfd_fork_ctx *fctx, *n;

	list_for_each_entry_safe(fctx, n, fcs, list) {
		dup_fctx(fctx);
		list_del(&fctx->list);
		kfree(fctx);
	}
}

void mremap_userfaultfd_prep(struct vm_area_struct *vma,
			     struct vm_userfaultfd_ctx *vm_ctx)
{
	struct userfaultfd_ctx *ctx;

	ctx = vma->vm_userfaultfd_ctx.ctx;
	if (ctx && (ctx->features & UFFD_FEATURE_EVENT_REMAP)) {
		vm_ctx->ctx = ctx;
		userfaultfd_ctx_get(ctx);
		WRITE_ONCE(ctx->mmap_changing, true);
	}
}

void mremap_userfaultfd_complete(struct vm_userfaultfd_ctx *vm_ctx,
				 unsigned long from, unsigned long to,
				 unsigned long len)
{
	struct userfaultfd_ctx *ctx = vm_ctx->ctx;
	struct userfaultfd_wait_queue ewq;

	if (!ctx)
		return;

	if (to & ~PAGE_MASK) {
		userfaultfd_ctx_put(ctx);
		return;
	}

	msg_init(&ewq.msg);

	ewq.msg.event = UFFD_EVENT_REMAP;
	ewq.msg.arg.remap.from = from;
	ewq.msg.arg.remap.to = to;
	ewq.msg.arg.remap.len = len;

	userfaultfd_event_wait_completion(ctx, &ewq);
}

bool userfaultfd_remove(struct vm_area_struct *vma,
			unsigned long start, unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;
	struct userfaultfd_ctx *ctx;
	struct userfaultfd_wait_queue ewq;

	ctx = vma->vm_userfaultfd_ctx.ctx;
	if (!ctx || !(ctx->features & UFFD_FEATURE_EVENT_REMOVE))
		return true;

	userfaultfd_ctx_get(ctx);
	WRITE_ONCE(ctx->mmap_changing, true);
	up_read(&mm->mmap_sem);

	msg_init(&ewq.msg);

	ewq.msg.event = UFFD_EVENT_REMOVE;
	ewq.msg.arg.remove.start = start;
	ewq.msg.arg.remove.end = end;

	userfaultfd_event_wait_completion(ctx, &ewq);

	return false;
}

static bool has_unmap_ctx(struct userfaultfd_ctx *ctx, struct list_head *unmaps,
			  unsigned long start, unsigned long end)
{
	struct userfaultfd_unmap_ctx *unmap_ctx;

	list_for_each_entry(unmap_ctx, unmaps, list)
		if (unmap_ctx->ctx == ctx && unmap_ctx->start == start &&
		    unmap_ctx->end == end)
			return true;

	return false;
}

int userfaultfd_unmap_prep(struct vm_area_struct *vma,
			   unsigned long start, unsigned long end,
			   struct list_head *unmaps)
{
	for ( ; vma && vma->vm_start < end; vma = vma->vm_next) {
		struct userfaultfd_unmap_ctx *unmap_ctx;
		struct userfaultfd_ctx *ctx = vma->vm_userfaultfd_ctx.ctx;

		if (!ctx || !(ctx->features & UFFD_FEATURE_EVENT_UNMAP) ||
		    has_unmap_ctx(ctx, unmaps, start, end))
			continue;

		unmap_ctx = kzalloc(sizeof(*unmap_ctx), GFP_KERNEL);
		if (!unmap_ctx)
			return -ENOMEM;

		userfaultfd_ctx_get(ctx);
		WRITE_ONCE(ctx->mmap_changing, true);
		unmap_ctx->ctx = ctx;
		unmap_ctx->start = start;
		unmap_ctx->end = end;
		list_add_tail(&unmap_ctx->list, unmaps);
	}

	return 0;
}

void userfaultfd_unmap_complete(struct mm_struct *mm, struct list_head *uf)
{
	struct userfaultfd_unmap_ctx *ctx, *n;
	struct userfaultfd_wait_queue ewq;

	list_for_each_entry_safe(ctx, n, uf, list) {
		msg_init(&ewq.msg);

		ewq.msg.event = UFFD_EVENT_UNMAP;
		ewq.msg.arg.remove.start = ctx->start;
		ewq.msg.arg.remove.end = ctx->end;

		userfaultfd_event_wait_completion(ctx->ctx, &ewq);

		list_del(&ctx->list);
		kfree(ctx);
	}
}

static int userfaultfd_release(struct inode *inode, struct file *file)
{
	struct userfaultfd_ctx *ctx = file->private_data;
	struct mm_struct *mm = ctx->mm;
	struct vm_area_struct *vma, *prev;
	/* len == 0 means wake all */
	struct userfaultfd_wake_range range = { .len = 0, };
	unsigned long new_flags;

	WRITE_ONCE(ctx->released, true);

	if (!mmget_not_zero(mm))
		goto wakeup;

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
				 NULL_VM_UFFD_CTX);
		if (prev)
			vma = prev;
		else
			prev = vma;
		vma->vm_flags = new_flags;
		vma->vm_userfaultfd_ctx = NULL_VM_UFFD_CTX;
	}
	up_write(&mm->mmap_sem);
	mmput(mm);
wakeup:
	/*
	 * After no new page faults can wait on this fault_*wqh, flush
	 * the last page faults that may have been already waiting on
	 * the fault_*wqh.
	 */
	spin_lock(&ctx->fault_pending_wqh.lock);
	__wake_up_locked_key(&ctx->fault_pending_wqh, TASK_NORMAL, &range);
	__wake_up(&ctx->fault_wqh, TASK_NORMAL, 1, &range);
	spin_unlock(&ctx->fault_pending_wqh.lock);

	/* Flush pending events that may still wait on event_wqh */
	wake_up_all(&ctx->event_wqh);

	wake_up_poll(&ctx->fd_wqh, EPOLLHUP);
	userfaultfd_ctx_put(ctx);
	return 0;
}

/* fault_pending_wqh.lock must be hold by the caller */
static inline struct userfaultfd_wait_queue *find_userfault_in(
		wait_queue_head_t *wqh)
{
	wait_queue_entry_t *wq;
	struct userfaultfd_wait_queue *uwq;

	VM_BUG_ON(!spin_is_locked(&wqh->lock));

	uwq = NULL;
	if (!waitqueue_active(wqh))
		goto out;
	/* walk in reverse to provide FIFO behavior to read userfaults */
	wq = list_last_entry(&wqh->head, typeof(*wq), entry);
	uwq = container_of(wq, struct userfaultfd_wait_queue, wq);
out:
	return uwq;
}

static inline struct userfaultfd_wait_queue *find_userfault(
		struct userfaultfd_ctx *ctx)
{
	return find_userfault_in(&ctx->fault_pending_wqh);
}

static inline struct userfaultfd_wait_queue *find_userfault_evt(
		struct userfaultfd_ctx *ctx)
{
	return find_userfault_in(&ctx->event_wqh);
}

static __poll_t userfaultfd_poll(struct file *file, poll_table *wait)
{
	struct userfaultfd_ctx *ctx = file->private_data;
	__poll_t ret;

	poll_wait(file, &ctx->fd_wqh, wait);

	switch (ctx->state) {
	case UFFD_STATE_WAIT_API:
		return EPOLLERR;
	case UFFD_STATE_RUNNING:
		/*
		 * poll() never guarantees that read won't block.
		 * userfaults can be waken before they're read().
		 */
		if (unlikely(!(file->f_flags & O_NONBLOCK)))
			return EPOLLERR;
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
			ret = EPOLLIN;
		else if (waitqueue_active(&ctx->event_wqh))
			ret = EPOLLIN;

		return ret;
	default:
		WARN_ON_ONCE(1);
		return EPOLLERR;
	}
}

static const struct file_operations userfaultfd_fops;

static int resolve_userfault_fork(struct userfaultfd_ctx *ctx,
				  struct userfaultfd_ctx *new,
				  struct uffd_msg *msg)
{
	int fd;

	fd = anon_inode_getfd("[userfaultfd]", &userfaultfd_fops, new,
			      O_RDWR | (new->flags & UFFD_SHARED_FCNTL_FLAGS));
	if (fd < 0)
		return fd;

	msg->arg.reserved.reserved1 = 0;
	msg->arg.fork.ufd = fd;
	return 0;
}

static ssize_t userfaultfd_ctx_read(struct userfaultfd_ctx *ctx, int no_wait,
				    struct uffd_msg *msg)
{
	ssize_t ret;
	DECLARE_WAITQUEUE(wait, current);
	struct userfaultfd_wait_queue *uwq;
	/*
	 * Handling fork event requires sleeping operations, so
	 * we drop the event_wqh lock, then do these ops, then
	 * lock it back and wake up the waiter. While the lock is
	 * dropped the ewq may go away so we keep track of it
	 * carefully.
	 */
	LIST_HEAD(fork_event);
	struct userfaultfd_ctx *fork_nctx = NULL;

	/* always take the fd_wqh lock before the fault_pending_wqh lock */
	spin_lock_irq(&ctx->fd_wqh.lock);
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
			 * handle_userfault(). The uwq->wq.head list
			 * must never be empty at any time during the
			 * refile, or the waitqueue could disappear
			 * from under us. The "wait_queue_head_t"
			 * parameter of __remove_wait_queue() is unused
			 * anyway.
			 */
			list_del(&uwq->wq.entry);
			add_wait_queue(&ctx->fault_wqh, &uwq->wq);

			write_seqcount_end(&ctx->refile_seq);

			/* careful to always initialize msg if ret == 0 */
			*msg = uwq->msg;
			spin_unlock(&ctx->fault_pending_wqh.lock);
			ret = 0;
			break;
		}
		spin_unlock(&ctx->fault_pending_wqh.lock);

		spin_lock(&ctx->event_wqh.lock);
		uwq = find_userfault_evt(ctx);
		if (uwq) {
			*msg = uwq->msg;

			if (uwq->msg.event == UFFD_EVENT_FORK) {
				fork_nctx = (struct userfaultfd_ctx *)
					(unsigned long)
					uwq->msg.arg.reserved.reserved1;
				list_move(&uwq->wq.entry, &fork_event);
				/*
				 * fork_nctx can be freed as soon as
				 * we drop the lock, unless we take a
				 * reference on it.
				 */
				userfaultfd_ctx_get(fork_nctx);
				spin_unlock(&ctx->event_wqh.lock);
				ret = 0;
				break;
			}

			userfaultfd_event_complete(ctx, uwq);
			spin_unlock(&ctx->event_wqh.lock);
			ret = 0;
			break;
		}
		spin_unlock(&ctx->event_wqh.lock);

		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}
		if (no_wait) {
			ret = -EAGAIN;
			break;
		}
		spin_unlock_irq(&ctx->fd_wqh.lock);
		schedule();
		spin_lock_irq(&ctx->fd_wqh.lock);
	}
	__remove_wait_queue(&ctx->fd_wqh, &wait);
	__set_current_state(TASK_RUNNING);
	spin_unlock_irq(&ctx->fd_wqh.lock);

	if (!ret && msg->event == UFFD_EVENT_FORK) {
		ret = resolve_userfault_fork(ctx, fork_nctx, msg);
		spin_lock(&ctx->event_wqh.lock);
		if (!list_empty(&fork_event)) {
			/*
			 * The fork thread didn't abort, so we can
			 * drop the temporary refcount.
			 */
			userfaultfd_ctx_put(fork_nctx);

			uwq = list_first_entry(&fork_event,
					       typeof(*uwq),
					       wq.entry);
			/*
			 * If fork_event list wasn't empty and in turn
			 * the event wasn't already released by fork
			 * (the event is allocated on fork kernel
			 * stack), put the event back to its place in
			 * the event_wq. fork_event head will be freed
			 * as soon as we return so the event cannot
			 * stay queued there no matter the current
			 * "ret" value.
			 */
			list_del(&uwq->wq.entry);
			__add_wait_queue(&ctx->event_wqh, &uwq->wq);

			/*
			 * Leave the event in the waitqueue and report
			 * error to userland if we failed to resolve
			 * the userfault fork.
			 */
			if (likely(!ret))
				userfaultfd_event_complete(ctx, uwq);
		} else {
			/*
			 * Here the fork thread aborted and the
			 * refcount from the fork thread on fork_nctx
			 * has already been released. We still hold
			 * the reference we took before releasing the
			 * lock above. If resolve_userfault_fork
			 * failed we've to drop it because the
			 * fork_nctx has to be freed in such case. If
			 * it succeeded we'll hold it because the new
			 * uffd references it.
			 */
			if (ret)
				userfaultfd_ctx_put(fork_nctx);
		}
		spin_unlock(&ctx->event_wqh.lock);
	}

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
	spin_lock(&ctx->fault_pending_wqh.lock);
	/* wake all in the range and autoremove */
	if (waitqueue_active(&ctx->fault_pending_wqh))
		__wake_up_locked_key(&ctx->fault_pending_wqh, TASK_NORMAL,
				     range);
	if (waitqueue_active(&ctx->fault_wqh))
		__wake_up(&ctx->fault_wqh, TASK_NORMAL, 1, range);
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

static inline bool vma_can_userfault(struct vm_area_struct *vma)
{
	return vma_is_anonymous(vma) || is_vm_hugetlb_page(vma) ||
		vma_is_shmem(vma);
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
	bool basic_ioctls;
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

	ret = -ENOMEM;
	if (!mmget_not_zero(mm))
		goto out;

	down_write(&mm->mmap_sem);
	vma = find_vma_prev(mm, start, &prev);
	if (!vma)
		goto out_unlock;

	/* check that there's at least one vma in the range */
	ret = -EINVAL;
	if (vma->vm_start >= end)
		goto out_unlock;

	/*
	 * If the first vma contains huge pages, make sure start address
	 * is aligned to huge page size.
	 */
	if (is_vm_hugetlb_page(vma)) {
		unsigned long vma_hpagesize = vma_kernel_pagesize(vma);

		if (start & (vma_hpagesize - 1))
			goto out_unlock;
	}

	/*
	 * Search for not compatible vmas.
	 */
	found = false;
	basic_ioctls = false;
	for (cur = vma; cur && cur->vm_start < end; cur = cur->vm_next) {
		cond_resched();

		BUG_ON(!!cur->vm_userfaultfd_ctx.ctx ^
		       !!(cur->vm_flags & (VM_UFFD_MISSING | VM_UFFD_WP)));

		/* check not compatible vmas */
		ret = -EINVAL;
		if (!vma_can_userfault(cur))
			goto out_unlock;

		/*
		 * UFFDIO_COPY will fill file holes even without
		 * PROT_WRITE. This check enforces that if this is a
		 * MAP_SHARED, the process has write permission to the backing
		 * file. If VM_MAYWRITE is set it also enforces that on a
		 * MAP_SHARED vma: there is no F_WRITE_SEAL and no further
		 * F_WRITE_SEAL can be taken until the vma is destroyed.
		 */
		ret = -EPERM;
		if (unlikely(!(cur->vm_flags & VM_MAYWRITE)))
			goto out_unlock;

		/*
		 * If this vma contains ending address, and huge pages
		 * check alignment.
		 */
		if (is_vm_hugetlb_page(cur) && end <= cur->vm_end &&
		    end > cur->vm_start) {
			unsigned long vma_hpagesize = vma_kernel_pagesize(cur);

			ret = -EINVAL;

			if (end & (vma_hpagesize - 1))
				goto out_unlock;
		}

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

		/*
		 * Note vmas containing huge pages
		 */
		if (is_vm_hugetlb_page(cur))
			basic_ioctls = true;

		found = true;
	}
	BUG_ON(!found);

	if (vma->vm_start < start)
		prev = vma;

	ret = 0;
	do {
		cond_resched();

		BUG_ON(!vma_can_userfault(vma));
		BUG_ON(vma->vm_userfaultfd_ctx.ctx &&
		       vma->vm_userfaultfd_ctx.ctx != ctx);
		WARN_ON(!(vma->vm_flags & VM_MAYWRITE));

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
				 ((struct vm_userfaultfd_ctx){ ctx }));
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
	mmput(mm);
	if (!ret) {
		/*
		 * Now that we scanned all vmas we can already tell
		 * userland which ioctls methods are guaranteed to
		 * succeed on this range.
		 */
		if (put_user(basic_ioctls ? UFFD_API_RANGE_IOCTLS_BASIC :
			     UFFD_API_RANGE_IOCTLS,
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

	ret = -ENOMEM;
	if (!mmget_not_zero(mm))
		goto out;

	down_write(&mm->mmap_sem);
	vma = find_vma_prev(mm, start, &prev);
	if (!vma)
		goto out_unlock;

	/* check that there's at least one vma in the range */
	ret = -EINVAL;
	if (vma->vm_start >= end)
		goto out_unlock;

	/*
	 * If the first vma contains huge pages, make sure start address
	 * is aligned to huge page size.
	 */
	if (is_vm_hugetlb_page(vma)) {
		unsigned long vma_hpagesize = vma_kernel_pagesize(vma);

		if (start & (vma_hpagesize - 1))
			goto out_unlock;
	}

	/*
	 * Search for not compatible vmas.
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
		if (!vma_can_userfault(cur))
			goto out_unlock;

		found = true;
	}
	BUG_ON(!found);

	if (vma->vm_start < start)
		prev = vma;

	ret = 0;
	do {
		cond_resched();

		BUG_ON(!vma_can_userfault(vma));
		WARN_ON(!(vma->vm_flags & VM_MAYWRITE));

		/*
		 * Nothing to do: this vma is already registered into this
		 * userfaultfd and with the right tracking mode too.
		 */
		if (!vma->vm_userfaultfd_ctx.ctx)
			goto skip;

		if (vma->vm_start > start)
			start = vma->vm_start;
		vma_end = min(end, vma->vm_end);

		if (userfaultfd_missing(vma)) {
			/*
			 * Wake any concurrent pending userfault while
			 * we unregister, so they will not hang
			 * permanently and it avoids userland to call
			 * UFFDIO_WAKE explicitly.
			 */
			struct userfaultfd_wake_range range;
			range.start = start;
			range.len = vma_end - start;
			wake_userfault(vma->vm_userfaultfd_ctx.ctx, &range);
		}

		new_flags = vma->vm_flags & ~(VM_UFFD_MISSING | VM_UFFD_WP);
		prev = vma_merge(mm, prev, start, vma_end, new_flags,
				 vma->anon_vma, vma->vm_file, vma->vm_pgoff,
				 vma_policy(vma),
				 NULL_VM_UFFD_CTX);
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
	mmput(mm);
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

	ret = -EAGAIN;
	if (READ_ONCE(ctx->mmap_changing))
		goto out;

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
	if (mmget_not_zero(ctx->mm)) {
		ret = mcopy_atomic(ctx->mm, uffdio_copy.dst, uffdio_copy.src,
				   uffdio_copy.len, &ctx->mmap_changing);
		mmput(ctx->mm);
	} else {
		return -ESRCH;
	}
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

	ret = -EAGAIN;
	if (READ_ONCE(ctx->mmap_changing))
		goto out;

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

	if (mmget_not_zero(ctx->mm)) {
		ret = mfill_zeropage(ctx->mm, uffdio_zeropage.range.start,
				     uffdio_zeropage.range.len,
				     &ctx->mmap_changing);
		mmput(ctx->mm);
	} else {
		return -ESRCH;
	}
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

static inline unsigned int uffd_ctx_features(__u64 user_features)
{
	/*
	 * For the current set of features the bits just coincide
	 */
	return (unsigned int)user_features;
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
	__u64 features;

	ret = -EINVAL;
	if (ctx->state != UFFD_STATE_WAIT_API)
		goto out;
	ret = -EFAULT;
	if (copy_from_user(&uffdio_api, buf, sizeof(uffdio_api)))
		goto out;
	features = uffdio_api.features;
	if (uffdio_api.api != UFFD_API || (features & ~UFFD_API_FEATURES)) {
		memset(&uffdio_api, 0, sizeof(uffdio_api));
		if (copy_to_user(buf, &uffdio_api, sizeof(uffdio_api)))
			goto out;
		ret = -EINVAL;
		goto out;
	}
	/* report all available features and ioctls to userland */
	uffdio_api.features = UFFD_API_FEATURES;
	uffdio_api.ioctls = UFFD_API_IOCTLS;
	ret = -EFAULT;
	if (copy_to_user(buf, &uffdio_api, sizeof(uffdio_api)))
		goto out;
	ctx->state = UFFD_STATE_RUNNING;
	/* only enable the requested features for this uffd context */
	ctx->features = uffd_ctx_features(features);
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
	wait_queue_entry_t *wq;
	unsigned long pending = 0, total = 0;

	spin_lock(&ctx->fault_pending_wqh.lock);
	list_for_each_entry(wq, &ctx->fault_pending_wqh.head, entry) {
		pending++;
		total++;
	}
	list_for_each_entry(wq, &ctx->fault_wqh.head, entry) {
		total++;
	}
	spin_unlock(&ctx->fault_pending_wqh.lock);

	/*
	 * If more protocols will be added, there will be all shown
	 * separated by a space. Like this:
	 *	protocols: aa:... bb:...
	 */
	seq_printf(m, "pending:\t%lu\ntotal:\t%lu\nAPI:\t%Lx:%x:%Lx\n",
		   pending, total, UFFD_API, ctx->features,
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
	init_waitqueue_head(&ctx->event_wqh);
	init_waitqueue_head(&ctx->fd_wqh);
	seqcount_init(&ctx->refile_seq);
}

SYSCALL_DEFINE1(userfaultfd, int, flags)
{
	struct userfaultfd_ctx *ctx;
	int fd;

	BUG_ON(!current->mm);

	/* Check the UFFD_* constants for consistency.  */
	BUILD_BUG_ON(UFFD_CLOEXEC != O_CLOEXEC);
	BUILD_BUG_ON(UFFD_NONBLOCK != O_NONBLOCK);

	if (flags & ~UFFD_SHARED_FCNTL_FLAGS)
		return -EINVAL;

	ctx = kmem_cache_alloc(userfaultfd_ctx_cachep, GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	atomic_set(&ctx->refcount, 1);
	ctx->flags = flags;
	ctx->features = 0;
	ctx->state = UFFD_STATE_WAIT_API;
	ctx->released = false;
	ctx->mmap_changing = false;
	ctx->mm = current->mm;
	/* prevent the mm struct to be freed */
	mmgrab(ctx->mm);

	fd = anon_inode_getfd("[userfaultfd]", &userfaultfd_fops, ctx,
			      O_RDWR | (flags & UFFD_SHARED_FCNTL_FLAGS));
	if (fd < 0) {
		mmdrop(ctx->mm);
		kmem_cache_free(userfaultfd_ctx_cachep, ctx);
	}
	return fd;
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
