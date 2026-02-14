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
static inline bool __vma_enter_locked(struct vm_area_struct *vma, bool detaching)
{
	unsigned int tgt_refcnt = VMA_LOCK_OFFSET;

	/* Additional refcnt if the vma is attached. */
	if (!detaching)
		tgt_refcnt++;

	/*
	 * If vma is detached then only vma_mark_attached() can raise the
	 * vm_refcnt. mmap_write_lock prevents racing with vma_mark_attached().
	 */
	if (!refcount_add_not_zero(VMA_LOCK_OFFSET, &vma->vm_refcnt))
		return false;

	rwsem_acquire(&vma->vmlock_dep_map, 0, 0, _RET_IP_);
	rcuwait_wait_event(&vma->vm_mm->vma_writer_wait,
		   refcount_read(&vma->vm_refcnt) == tgt_refcnt,
		   TASK_UNINTERRUPTIBLE);
	lock_acquired(&vma->vmlock_dep_map, _RET_IP_);

	return true;
}

static inline void __vma_exit_locked(struct vm_area_struct *vma, bool *detached)
{
	*detached = refcount_sub_and_test(VMA_LOCK_OFFSET, &vma->vm_refcnt);
	rwsem_release(&vma->vmlock_dep_map, _RET_IP_);
}

void __vma_start_write(struct vm_area_struct *vma, unsigned int mm_lock_seq)
{
	bool locked;

	/*
	 * __vma_enter_locked() returns false immediately if the vma is not
	 * attached, otherwise it waits until refcnt is indicating that vma
	 * is attached with no readers.
	 */
	locked = __vma_enter_locked(vma, false);

	/*
	 * We should use WRITE_ONCE() here because we can have concurrent reads
	 * from the early lockless pessimistic check in vma_start_read().
	 * We don't really care about the correctness of that early check, but
	 * we should use WRITE_ONCE() for cleanliness and to keep KCSAN happy.
	 */
	WRITE_ONCE(vma->vm_lock_seq, mm_lock_seq);

	if (locked) {
		bool detached;

		__vma_exit_locked(vma, &detached);
		WARN_ON_ONCE(detached); /* vma should remain attached */
	}
}
EXPORT_SYMBOL_GPL(__vma_start_write);

void vma_mark_detached(struct vm_area_struct *vma)
{
	vma_assert_write_locked(vma);
	vma_assert_attached(vma);

	/*
	 * We are the only writer, so no need to use vma_refcount_put().
	 * The condition below is unlikely because the vma has been already
	 * write-locked and readers can increment vm_refcnt only temporarily
	 * before they check vm_lock_seq, realize the vma is locked and drop
	 * back the vm_refcnt. That is a narrow window for observing a raised
	 * vm_refcnt.
	 */
	if (unlikely(!refcount_dec_and_test(&vma->vm_refcnt))) {
		/* Wait until vma is detached with no readers. */
		if (__vma_enter_locked(vma, true)) {
			bool detached;

			__vma_exit_locked(vma, &detached);
			WARN_ON_ONCE(!detached);
		}
	}
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
	 * If VMA_LOCK_OFFSET is set, __refcount_inc_not_zero_limited_acquire()
	 * will fail because VMA_REF_LIMIT is less than VMA_LOCK_OFFSET.
	 * Acquire fence is required here to avoid reordering against later
	 * vm_lock_seq check and checks inside lock_vma_under_rcu().
	 */
	if (unlikely(!__refcount_inc_not_zero_limited_acquire(&vma->vm_refcnt, &oldcnt,
							      VMA_REF_LIMIT))) {
		/* return EAGAIN if vma got detached from under us */
		vma = oldcnt ? NULL : ERR_PTR(-EAGAIN);
		goto err;
	}

	rwsem_acquire_read(&vma->vmlock_dep_map, 0, 1, _RET_IP_);

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
