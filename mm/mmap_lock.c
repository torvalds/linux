// SPDX-License-Identifier: GPL-2.0
#define CREATE_TRACE_POINTS
#include <trace/events/mmap_lock.h>

#include <linux/mm.h>
#include <linux/cgroup.h>
#include <linux/memcontrol.h>
#include <linux/mmap_lock.h>
#include <linux/mutex.h>
#include <linux/percpu.h>
#include <linux/rcupdate.h>
#include <linux/smp.h>
#include <linux/trace_events.h>
#include <linux/local_lock.h>

EXPORT_TRACEPOINT_SYMBOL(mmap_lock_start_locking);
EXPORT_TRACEPOINT_SYMBOL(mmap_lock_acquire_returned);
EXPORT_TRACEPOINT_SYMBOL(mmap_lock_released);

#ifdef CONFIG_TRACING
/*
 * Trace calls must be in a separate file, as otherwise there's a circular
 * dependency between linux/mmap_lock.h and trace/events/mmap_lock.h.
 */

void __mmap_lock_do_trace_start_locking(struct mm_struct *mm, bool write)
{
	trace_mmap_lock_start_locking(mm, write);
}
EXPORT_SYMBOL(__mmap_lock_do_trace_start_locking);

void __mmap_lock_do_trace_acquire_returned(struct mm_struct *mm, bool write,
					   bool success)
{
	trace_mmap_lock_acquire_returned(mm, write, success);
}
EXPORT_SYMBOL(__mmap_lock_do_trace_acquire_returned);

void __mmap_lock_do_trace_released(struct mm_struct *mm, bool write)
{
	trace_mmap_lock_released(mm, write);
}
EXPORT_SYMBOL(__mmap_lock_do_trace_released);
#endif /* CONFIG_TRACING */

#ifdef CONFIG_MMU
#ifdef CONFIG_PER_VMA_LOCK

/* State shared across __vma_[start, end]_exclude_readers. */
struct vma_exclude_readers_state {
	/* Input parameters. */
	struct vm_area_struct *vma;
	int state; /* TASK_KILLABLE or TASK_UNINTERRUPTIBLE. */
	bool detaching;

	/* Output parameters. */
	bool detached;
	bool exclusive; /* Are we exclusively locked? */
};

/*
 * Now that all readers have been evicted, mark the VMA as being out of the
 * 'exclude readers' state.
 */
static void __vma_end_exclude_readers(struct vma_exclude_readers_state *ves)
{
	struct vm_area_struct *vma = ves->vma;

	VM_WARN_ON_ONCE(ves->detached);

	ves->detached = refcount_sub_and_test(VM_REFCNT_EXCLUDE_READERS_FLAG,
					      &vma->vm_refcnt);
	__vma_lockdep_release_exclusive(vma);
}

static unsigned int get_target_refcnt(struct vma_exclude_readers_state *ves)
{
	const unsigned int tgt = ves->detaching ? 0 : 1;

	return tgt | VM_REFCNT_EXCLUDE_READERS_FLAG;
}

/*
 * Mark the VMA as being in a state of excluding readers, check to see if any
 * VMA read locks are indeed held, and if so wait for them to be released.
 *
 * Note that this function pairs with vma_refcount_put() which will wake up this
 * thread when it detects that the last reader has released its lock.
 *
 * The ves->state parameter ought to be set to TASK_UNINTERRUPTIBLE in cases
 * where we wish the thread to sleep uninterruptibly or TASK_KILLABLE if a fatal
 * signal is permitted to kill it.
 *
 * The function sets the ves->exclusive parameter to true if readers were
 * excluded, or false if the VMA was detached or an error arose on wait.
 *
 * If the function indicates an exclusive lock was acquired via ves->exclusive
 * the caller is required to invoke __vma_end_exclude_readers() once the
 * exclusive state is no longer required.
 *
 * If ves->state is set to something other than TASK_UNINTERRUPTIBLE, the
 * function may also return -EINTR to indicate a fatal signal was received while
 * waiting.  Otherwise, the function returns 0.
 */
static int __vma_start_exclude_readers(struct vma_exclude_readers_state *ves)
{
	struct vm_area_struct *vma = ves->vma;
	unsigned int tgt_refcnt = get_target_refcnt(ves);
	int err = 0;

	mmap_assert_write_locked(vma->vm_mm);

	/*
	 * If vma is detached then only vma_mark_attached() can raise the
	 * vm_refcnt. mmap_write_lock prevents racing with vma_mark_attached().
	 *
	 * See the comment describing the vm_area_struct->vm_refcnt field for
	 * details of possible refcnt values.
	 */
	if (!refcount_add_not_zero(VM_REFCNT_EXCLUDE_READERS_FLAG, &vma->vm_refcnt)) {
		ves->detached = true;
		return 0;
	}

	__vma_lockdep_acquire_exclusive(vma);
	err = rcuwait_wait_event(&vma->vm_mm->vma_writer_wait,
		   refcount_read(&vma->vm_refcnt) == tgt_refcnt,
		   ves->state);
	if (err) {
		__vma_end_exclude_readers(ves);
		return err;
	}

	__vma_lockdep_stat_mark_acquired(vma);
	ves->exclusive = true;
	return 0;
}

int __vma_start_write(struct vm_area_struct *vma, int state)
{
	const unsigned int mm_lock_seq = __vma_raw_mm_seqnum(vma);
	struct vma_exclude_readers_state ves = {
		.vma = vma,
		.state = state,
	};
	int err;

	err = __vma_start_exclude_readers(&ves);
	if (err) {
		WARN_ON_ONCE(ves.detached);
		return err;
	}

	/*
	 * We should use WRITE_ONCE() here because we can have concurrent reads
	 * from the early lockless pessimistic check in vma_start_read().
	 * We don't really care about the correctness of that early check, but
	 * we should use WRITE_ONCE() for cleanliness and to keep KCSAN happy.
	 */
	WRITE_ONCE(vma->vm_lock_seq, mm_lock_seq);

	if (ves.exclusive) {
		__vma_end_exclude_readers(&ves);
		/* VMA should remain attached. */
		WARN_ON_ONCE(ves.detached);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(__vma_start_write);

void __vma_exclude_readers_for_detach(struct vm_area_struct *vma)
{
	struct vma_exclude_readers_state ves = {
		.vma = vma,
		.state = TASK_UNINTERRUPTIBLE,
		.detaching = true,
	};
	int err;

	/*
	 * Wait until the VMA is detached with no readers. Since we hold the VMA
	 * write lock, the only read locks that might be present are those from
	 * threads trying to acquire the read lock and incrementing the
	 * reference count before realising the write lock is held and
	 * decrementing it.
	 */
	err = __vma_start_exclude_readers(&ves);
	if (!err && ves.exclusive) {
		/*
		 * Once this is complete, no readers can increment the
		 * reference count, and the VMA is marked detached.
		 */
		__vma_end_exclude_readers(&ves);
	}
	/* If an error arose but we were detached anyway, we don't care. */
	WARN_ON_ONCE(!ves.detached);
}

/*
 * Try to read-lock a vma. The function is allowed to occasionally yield false
 * locked result to avoid performance overhead, in which case we fall back to
 * using mmap_lock. The function should never yield false unlocked result.
 * False locked result is possible if mm_lock_seq overflows or if vma gets
 * reused and attached to a different mm before we lock it.
 * Returns the vma on success, NULL on failure to lock and EAGAIN if vma got
 * detached.
 *
 * IMPORTANT: RCU lock must be held upon entering the function, but upon error
 *            IT IS RELEASED. The caller must handle this correctly.
 */
static inline struct vm_area_struct *vma_start_read(struct mm_struct *mm,
						    struct vm_area_struct *vma)
{
	struct mm_struct *other_mm;
	int oldcnt;

	RCU_LOCKDEP_WARN(!rcu_read_lock_held(), "no rcu lock held");
	/*
	 * Check before locking. A race might cause false locked result.
	 * We can use READ_ONCE() for the mm_lock_seq here, and don't need
	 * ACQUIRE semantics, because this is just a lockless check whose result
	 * we don't rely on for anything - the mm_lock_seq read against which we
	 * need ordering is below.
	 */
	if (READ_ONCE(vma->vm_lock_seq) == READ_ONCE(mm->mm_lock_seq.sequence)) {
		vma = NULL;
		goto err;
	}

	/*
	 * If VM_REFCNT_EXCLUDE_READERS_FLAG is set,
	 * __refcount_inc_not_zero_limited_acquire() will fail because
	 * VM_REFCNT_LIMIT is less than VM_REFCNT_EXCLUDE_READERS_FLAG.
	 *
	 * Acquire fence is required here to avoid reordering against later
	 * vm_lock_seq check and checks inside lock_vma_under_rcu().
	 */
	if (unlikely(!__refcount_inc_not_zero_limited_acquire(&vma->vm_refcnt, &oldcnt,
							      VM_REFCNT_LIMIT))) {
		/* return EAGAIN if vma got detached from under us */
		vma = oldcnt ? NULL : ERR_PTR(-EAGAIN);
		goto err;
	}

	__vma_lockdep_acquire_read(vma);

	if (unlikely(vma->vm_mm != mm))
		goto err_unstable;

	/*
	 * Overflow of vm_lock_seq/mm_lock_seq might produce false locked result.
	 * False unlocked result is impossible because we modify and check
	 * vma->vm_lock_seq under vma->vm_refcnt protection and mm->mm_lock_seq
	 * modification invalidates all existing locks.
	 *
	 * We must use ACQUIRE semantics for the mm_lock_seq so that if we are
	 * racing with vma_end_write_all(), we only start reading from the VMA
	 * after it has been unlocked.
	 * This pairs with RELEASE semantics in vma_end_write_all().
	 */
	if (unlikely(vma->vm_lock_seq == raw_read_seqcount(&mm->mm_lock_seq))) {
		vma_refcount_put(vma);
		vma = NULL;
		goto err;
	}

	return vma;
err:
	rcu_read_unlock();

	return vma;
err_unstable:
	/*
	 * If vma got attached to another mm from under us, that mm is not
	 * stable and can be freed in the narrow window after vma->vm_refcnt
	 * is dropped and before rcuwait_wake_up(mm) is called. Grab it before
	 * releasing vma->vm_refcnt.
	 */
	other_mm = vma->vm_mm; /* use a copy as vma can be freed after we drop vm_refcnt */

	/* __mmdrop() is a heavy operation, do it after dropping RCU lock. */
	rcu_read_unlock();
	mmgrab(other_mm);
	vma_refcount_put(vma);
	mmdrop(other_mm);

	return NULL;
}

/*
 * Lookup and lock a VMA under RCU protection. Returned VMA is guaranteed to be
 * stable and not isolated. If the VMA is not found or is being modified the
 * function returns NULL.
 */
struct vm_area_struct *lock_vma_under_rcu(struct mm_struct *mm,
					  unsigned long address)
{
	MA_STATE(mas, &mm->mm_mt, address, address);
	struct vm_area_struct *vma;

retry:
	rcu_read_lock();
	vma = mas_walk(&mas);
	if (!vma) {
		rcu_read_unlock();
		goto inval;
	}

	vma = vma_start_read(mm, vma);
	if (IS_ERR_OR_NULL(vma)) {
		/* Check if the VMA got isolated after we found it */
		if (PTR_ERR(vma) == -EAGAIN) {
			count_vm_vma_lock_event(VMA_LOCK_MISS);
			/* The area was replaced with another one */
			mas_set(&mas, address);
			goto retry;
		}

		/* Failed to lock the VMA */
		goto inval;
	}
	/*
	 * At this point, we have a stable reference to a VMA: The VMA is
	 * locked and we know it hasn't already been isolated.
	 * From here on, we can access the VMA without worrying about which
	 * fields are accessible for RCU readers.
	 */
	rcu_read_unlock();

	/* Check if the vma we locked is the right one. */
	if (unlikely(address < vma->vm_start || address >= vma->vm_end)) {
		vma_end_read(vma);
		goto inval;
	}

	return vma;

inval:
	count_vm_vma_lock_event(VMA_LOCK_ABORT);
	return NULL;
}

static struct vm_area_struct *lock_next_vma_under_mmap_lock(struct mm_struct *mm,
							    struct vma_iterator *vmi,
							    unsigned long from_addr)
{
	struct vm_area_struct *vma;
	int ret;

	ret = mmap_read_lock_killable(mm);
	if (ret)
		return ERR_PTR(ret);

	/* Lookup the vma at the last position again under mmap_read_lock */
	vma_iter_set(vmi, from_addr);
	vma = vma_next(vmi);
	if (vma) {
		/* Very unlikely vma->vm_refcnt overflow case */
		if (unlikely(!vma_start_read_locked(vma)))
			vma = ERR_PTR(-EAGAIN);
	}

	mmap_read_unlock(mm);

	return vma;
}

struct vm_area_struct *lock_next_vma(struct mm_struct *mm,
				     struct vma_iterator *vmi,
				     unsigned long from_addr)
{
	struct vm_area_struct *vma;
	unsigned int mm_wr_seq;
	bool mmap_unlocked;

	RCU_LOCKDEP_WARN(!rcu_read_lock_held(), "no rcu read lock held");
retry:
	/* Start mmap_lock speculation in case we need to verify the vma later */
	mmap_unlocked = mmap_lock_speculate_try_begin(mm, &mm_wr_seq);
	vma = vma_next(vmi);
	if (!vma)
		return NULL;

	vma = vma_start_read(mm, vma);
	if (IS_ERR_OR_NULL(vma)) {
		/*
		 * Retry immediately if the vma gets detached from under us.
		 * Infinite loop should not happen because the vma we find will
		 * have to be constantly knocked out from under us.
		 */
		if (PTR_ERR(vma) == -EAGAIN) {
			/* reset to search from the last address */
			rcu_read_lock();
			vma_iter_set(vmi, from_addr);
			goto retry;
		}

		goto fallback;
	}

	/* Verify the vma is not behind the last search position. */
	if (unlikely(from_addr >= vma->vm_end))
		goto fallback_unlock;

	/*
	 * vma can be ahead of the last search position but we need to verify
	 * it was not shrunk after we found it and another vma has not been
	 * installed ahead of it. Otherwise we might observe a gap that should
	 * not be there.
	 */
	if (from_addr < vma->vm_start) {
		/* Verify only if the address space might have changed since vma lookup. */
		if (!mmap_unlocked || mmap_lock_speculate_retry(mm, mm_wr_seq)) {
			vma_iter_set(vmi, from_addr);
			if (vma != vma_next(vmi))
				goto fallback_unlock;
		}
	}

	return vma;

fallback_unlock:
	rcu_read_unlock();
	vma_end_read(vma);
fallback:
	vma = lock_next_vma_under_mmap_lock(mm, vmi, from_addr);
	rcu_read_lock();
	/* Reinitialize the iterator after re-entering rcu read section */
	vma_iter_set(vmi, IS_ERR_OR_NULL(vma) ? from_addr : vma->vm_end);

	return vma;
}
#endif /* CONFIG_PER_VMA_LOCK */

#ifdef CONFIG_LOCK_MM_AND_FIND_VMA
#include <linux/extable.h>

static inline bool get_mmap_lock_carefully(struct mm_struct *mm, struct pt_regs *regs)
{
	if (likely(mmap_read_trylock(mm)))
		return true;

	if (regs && !user_mode(regs)) {
		unsigned long ip = exception_ip(regs);
		if (!search_exception_tables(ip))
			return false;
	}

	return !mmap_read_lock_killable(mm);
}

static inline bool mmap_upgrade_trylock(struct mm_struct *mm)
{
	/*
	 * We don't have this operation yet.
	 *
	 * It should be easy enough to do: it's basically a
	 *    atomic_long_try_cmpxchg_acquire()
	 * from RWSEM_READER_BIAS -> RWSEM_WRITER_LOCKED, but
	 * it also needs the proper lockdep magic etc.
	 */
	return false;
}

static inline bool upgrade_mmap_lock_carefully(struct mm_struct *mm, struct pt_regs *regs)
{
	mmap_read_unlock(mm);
	if (regs && !user_mode(regs)) {
		unsigned long ip = exception_ip(regs);
		if (!search_exception_tables(ip))
			return false;
	}
	return !mmap_write_lock_killable(mm);
}

/*
 * Helper for page fault handling.
 *
 * This is kind of equivalent to "mmap_read_lock()" followed
 * by "find_extend_vma()", except it's a lot more careful about
 * the locking (and will drop the lock on failure).
 *
 * For example, if we have a kernel bug that causes a page
 * fault, we don't want to just use mmap_read_lock() to get
 * the mm lock, because that would deadlock if the bug were
 * to happen while we're holding the mm lock for writing.
 *
 * So this checks the exception tables on kernel faults in
 * order to only do this all for instructions that are actually
 * expected to fault.
 *
 * We can also actually take the mm lock for writing if we
 * need to extend the vma, which helps the VM layer a lot.
 */
struct vm_area_struct *lock_mm_and_find_vma(struct mm_struct *mm,
			unsigned long addr, struct pt_regs *regs)
{
	struct vm_area_struct *vma;

	if (!get_mmap_lock_carefully(mm, regs))
		return NULL;

	vma = find_vma(mm, addr);
	if (likely(vma && (vma->vm_start <= addr)))
		return vma;

	/*
	 * Well, dang. We might still be successful, but only
	 * if we can extend a vma to do so.
	 */
	if (!vma || !(vma->vm_flags & VM_GROWSDOWN)) {
		mmap_read_unlock(mm);
		return NULL;
	}

	/*
	 * We can try to upgrade the mmap lock atomically,
	 * in which case we can continue to use the vma
	 * we already looked up.
	 *
	 * Otherwise we'll have to drop the mmap lock and
	 * re-take it, and also look up the vma again,
	 * re-checking it.
	 */
	if (!mmap_upgrade_trylock(mm)) {
		if (!upgrade_mmap_lock_carefully(mm, regs))
			return NULL;

		vma = find_vma(mm, addr);
		if (!vma)
			goto fail;
		if (vma->vm_start <= addr)
			goto success;
		if (!(vma->vm_flags & VM_GROWSDOWN))
			goto fail;
	}

	if (expand_stack_locked(vma, addr))
		goto fail;

success:
	mmap_write_downgrade(mm);
	return vma;

fail:
	mmap_write_unlock(mm);
	return NULL;
}
#endif /* CONFIG_LOCK_MM_AND_FIND_VMA */

#else /* CONFIG_MMU */

/*
 * At least xtensa ends up having protection faults even with no
 * MMU.. No stack expansion, at least.
 */
struct vm_area_struct *lock_mm_and_find_vma(struct mm_struct *mm,
			unsigned long addr, struct pt_regs *regs)
{
	struct vm_area_struct *vma;

	mmap_read_lock(mm);
	vma = vma_lookup(mm, addr);
	if (!vma)
		mmap_read_unlock(mm);
	return vma;
}

#endif /* CONFIG_MMU */
