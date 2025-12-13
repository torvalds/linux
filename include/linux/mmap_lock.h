/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MMAP_LOCK_H
#define _LINUX_MMAP_LOCK_H

/* Avoid a dependency loop by declaring here. */
extern int rcuwait_wake_up(struct rcuwait *w);

#include <linux/lockdep.h>
#include <linux/mm_types.h>
#include <linux/mmdebug.h>
#include <linux/rwsem.h>
#include <linux/tracepoint-defs.h>
#include <linux/types.h>
#include <linux/cleanup.h>
#include <linux/sched/mm.h>

#define MMAP_LOCK_INITIALIZER(name) \
	.mmap_lock = __RWSEM_INITIALIZER((name).mmap_lock),

DECLARE_TRACEPOINT(mmap_lock_start_locking);
DECLARE_TRACEPOINT(mmap_lock_acquire_returned);
DECLARE_TRACEPOINT(mmap_lock_released);

#ifdef CONFIG_TRACING

void __mmap_lock_do_trace_start_locking(struct mm_struct *mm, bool write);
void __mmap_lock_do_trace_acquire_returned(struct mm_struct *mm, bool write,
					   bool success);
void __mmap_lock_do_trace_released(struct mm_struct *mm, bool write);

static inline void __mmap_lock_trace_start_locking(struct mm_struct *mm,
						   bool write)
{
	if (tracepoint_enabled(mmap_lock_start_locking))
		__mmap_lock_do_trace_start_locking(mm, write);
}

static inline void __mmap_lock_trace_acquire_returned(struct mm_struct *mm,
						      bool write, bool success)
{
	if (tracepoint_enabled(mmap_lock_acquire_returned))
		__mmap_lock_do_trace_acquire_returned(mm, write, success);
}

static inline void __mmap_lock_trace_released(struct mm_struct *mm, bool write)
{
	if (tracepoint_enabled(mmap_lock_released))
		__mmap_lock_do_trace_released(mm, write);
}

#else /* !CONFIG_TRACING */

static inline void __mmap_lock_trace_start_locking(struct mm_struct *mm,
						   bool write)
{
}

static inline void __mmap_lock_trace_acquire_returned(struct mm_struct *mm,
						      bool write, bool success)
{
}

static inline void __mmap_lock_trace_released(struct mm_struct *mm, bool write)
{
}

#endif /* CONFIG_TRACING */

static inline void mmap_assert_locked(const struct mm_struct *mm)
{
	rwsem_assert_held(&mm->mmap_lock);
}

static inline void mmap_assert_write_locked(const struct mm_struct *mm)
{
	rwsem_assert_held_write(&mm->mmap_lock);
}

#ifdef CONFIG_PER_VMA_LOCK

static inline void mm_lock_seqcount_init(struct mm_struct *mm)
{
	seqcount_init(&mm->mm_lock_seq);
}

static inline void mm_lock_seqcount_begin(struct mm_struct *mm)
{
	do_raw_write_seqcount_begin(&mm->mm_lock_seq);
}

static inline void mm_lock_seqcount_end(struct mm_struct *mm)
{
	ASSERT_EXCLUSIVE_WRITER(mm->mm_lock_seq);
	do_raw_write_seqcount_end(&mm->mm_lock_seq);
}

static inline bool mmap_lock_speculate_try_begin(struct mm_struct *mm, unsigned int *seq)
{
	/*
	 * Since mmap_lock is a sleeping lock, and waiting for it to become
	 * unlocked is more or less equivalent with taking it ourselves, don't
	 * bother with the speculative path if mmap_lock is already write-locked
	 * and take the slow path, which takes the lock.
	 */
	return raw_seqcount_try_begin(&mm->mm_lock_seq, *seq);
}

static inline bool mmap_lock_speculate_retry(struct mm_struct *mm, unsigned int seq)
{
	return read_seqcount_retry(&mm->mm_lock_seq, seq);
}

static inline void vma_lock_init(struct vm_area_struct *vma, bool reset_refcnt)
{
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	static struct lock_class_key lockdep_key;

	lockdep_init_map(&vma->vmlock_dep_map, "vm_lock", &lockdep_key, 0);
#endif
	if (reset_refcnt)
		refcount_set(&vma->vm_refcnt, 0);
	vma->vm_lock_seq = UINT_MAX;
}

static inline bool is_vma_writer_only(int refcnt)
{
	/*
	 * With a writer and no readers, refcnt is VMA_LOCK_OFFSET if the vma
	 * is detached and (VMA_LOCK_OFFSET + 1) if it is attached. Waiting on
	 * a detached vma happens only in vma_mark_detached() and is a rare
	 * case, therefore most of the time there will be no unnecessary wakeup.
	 */
	return refcnt & VMA_LOCK_OFFSET && refcnt <= VMA_LOCK_OFFSET + 1;
}

static inline void vma_refcount_put(struct vm_area_struct *vma)
{
	/* Use a copy of vm_mm in case vma is freed after we drop vm_refcnt */
	struct mm_struct *mm = vma->vm_mm;
	int oldcnt;

	rwsem_release(&vma->vmlock_dep_map, _RET_IP_);
	if (!__refcount_dec_and_test(&vma->vm_refcnt, &oldcnt)) {

		if (is_vma_writer_only(oldcnt - 1))
			rcuwait_wake_up(&mm->vma_writer_wait);
	}
}

/*
 * Use only while holding mmap read lock which guarantees that locking will not
 * fail (nobody can concurrently write-lock the vma). vma_start_read() should
 * not be used in such cases because it might fail due to mm_lock_seq overflow.
 * This functionality is used to obtain vma read lock and drop the mmap read lock.
 */
static inline bool vma_start_read_locked_nested(struct vm_area_struct *vma, int subclass)
{
	int oldcnt;

	mmap_assert_locked(vma->vm_mm);
	if (unlikely(!__refcount_inc_not_zero_limited_acquire(&vma->vm_refcnt, &oldcnt,
							      VMA_REF_LIMIT)))
		return false;

	rwsem_acquire_read(&vma->vmlock_dep_map, 0, 1, _RET_IP_);
	return true;
}

/*
 * Use only while holding mmap read lock which guarantees that locking will not
 * fail (nobody can concurrently write-lock the vma). vma_start_read() should
 * not be used in such cases because it might fail due to mm_lock_seq overflow.
 * This functionality is used to obtain vma read lock and drop the mmap read lock.
 */
static inline bool vma_start_read_locked(struct vm_area_struct *vma)
{
	return vma_start_read_locked_nested(vma, 0);
}

static inline void vma_end_read(struct vm_area_struct *vma)
{
	vma_refcount_put(vma);
}

/* WARNING! Can only be used if mmap_lock is expected to be write-locked */
static bool __is_vma_write_locked(struct vm_area_struct *vma, unsigned int *mm_lock_seq)
{
	mmap_assert_write_locked(vma->vm_mm);

	/*
	 * current task is holding mmap_write_lock, both vma->vm_lock_seq and
	 * mm->mm_lock_seq can't be concurrently modified.
	 */
	*mm_lock_seq = vma->vm_mm->mm_lock_seq.sequence;
	return (vma->vm_lock_seq == *mm_lock_seq);
}

void __vma_start_write(struct vm_area_struct *vma, unsigned int mm_lock_seq);

/*
 * Begin writing to a VMA.
 * Exclude concurrent readers under the per-VMA lock until the currently
 * write-locked mmap_lock is dropped or downgraded.
 */
static inline void vma_start_write(struct vm_area_struct *vma)
{
	unsigned int mm_lock_seq;

	if (__is_vma_write_locked(vma, &mm_lock_seq))
		return;

	__vma_start_write(vma, mm_lock_seq);
}

static inline void vma_assert_write_locked(struct vm_area_struct *vma)
{
	unsigned int mm_lock_seq;

	VM_BUG_ON_VMA(!__is_vma_write_locked(vma, &mm_lock_seq), vma);
}

static inline void vma_assert_locked(struct vm_area_struct *vma)
{
	unsigned int mm_lock_seq;

	VM_BUG_ON_VMA(refcount_read(&vma->vm_refcnt) <= 1 &&
		      !__is_vma_write_locked(vma, &mm_lock_seq), vma);
}

/*
 * WARNING: to avoid racing with vma_mark_attached()/vma_mark_detached(), these
 * assertions should be made either under mmap_write_lock or when the object
 * has been isolated under mmap_write_lock, ensuring no competing writers.
 */
static inline void vma_assert_attached(struct vm_area_struct *vma)
{
	WARN_ON_ONCE(!refcount_read(&vma->vm_refcnt));
}

static inline void vma_assert_detached(struct vm_area_struct *vma)
{
	WARN_ON_ONCE(refcount_read(&vma->vm_refcnt));
}

static inline void vma_mark_attached(struct vm_area_struct *vma)
{
	vma_assert_write_locked(vma);
	vma_assert_detached(vma);
	refcount_set_release(&vma->vm_refcnt, 1);
}

void vma_mark_detached(struct vm_area_struct *vma);

struct vm_area_struct *lock_vma_under_rcu(struct mm_struct *mm,
					  unsigned long address);

/*
 * Locks next vma pointed by the iterator. Confirms the locked vma has not
 * been modified and will retry under mmap_lock protection if modification
 * was detected. Should be called from read RCU section.
 * Returns either a valid locked VMA, NULL if no more VMAs or -EINTR if the
 * process was interrupted.
 */
struct vm_area_struct *lock_next_vma(struct mm_struct *mm,
				     struct vma_iterator *iter,
				     unsigned long address);

#else /* CONFIG_PER_VMA_LOCK */

static inline void mm_lock_seqcount_init(struct mm_struct *mm) {}
static inline void mm_lock_seqcount_begin(struct mm_struct *mm) {}
static inline void mm_lock_seqcount_end(struct mm_struct *mm) {}

static inline bool mmap_lock_speculate_try_begin(struct mm_struct *mm, unsigned int *seq)
{
	return false;
}

static inline bool mmap_lock_speculate_retry(struct mm_struct *mm, unsigned int seq)
{
	return true;
}
static inline void vma_lock_init(struct vm_area_struct *vma, bool reset_refcnt) {}
static inline struct vm_area_struct *vma_start_read(struct mm_struct *mm,
						    struct vm_area_struct *vma)
		{ return NULL; }
static inline void vma_end_read(struct vm_area_struct *vma) {}
static inline void vma_start_write(struct vm_area_struct *vma) {}
static inline void vma_assert_write_locked(struct vm_area_struct *vma)
		{ mmap_assert_write_locked(vma->vm_mm); }
static inline void vma_assert_attached(struct vm_area_struct *vma) {}
static inline void vma_assert_detached(struct vm_area_struct *vma) {}
static inline void vma_mark_attached(struct vm_area_struct *vma) {}
static inline void vma_mark_detached(struct vm_area_struct *vma) {}

static inline struct vm_area_struct *lock_vma_under_rcu(struct mm_struct *mm,
		unsigned long address)
{
	return NULL;
}

static inline void vma_assert_locked(struct vm_area_struct *vma)
{
	mmap_assert_locked(vma->vm_mm);
}

#endif /* CONFIG_PER_VMA_LOCK */

static inline void mmap_write_lock(struct mm_struct *mm)
{
	__mmap_lock_trace_start_locking(mm, true);
	down_write(&mm->mmap_lock);
	mm_lock_seqcount_begin(mm);
	__mmap_lock_trace_acquire_returned(mm, true, true);
}

static inline void mmap_write_lock_nested(struct mm_struct *mm, int subclass)
{
	__mmap_lock_trace_start_locking(mm, true);
	down_write_nested(&mm->mmap_lock, subclass);
	mm_lock_seqcount_begin(mm);
	__mmap_lock_trace_acquire_returned(mm, true, true);
}

static inline int mmap_write_lock_killable(struct mm_struct *mm)
{
	int ret;

	__mmap_lock_trace_start_locking(mm, true);
	ret = down_write_killable(&mm->mmap_lock);
	if (!ret)
		mm_lock_seqcount_begin(mm);
	__mmap_lock_trace_acquire_returned(mm, true, ret == 0);
	return ret;
}

/*
 * Drop all currently-held per-VMA locks.
 * This is called from the mmap_lock implementation directly before releasing
 * a write-locked mmap_lock (or downgrading it to read-locked).
 * This should normally NOT be called manually from other places.
 * If you want to call this manually anyway, keep in mind that this will release
 * *all* VMA write locks, including ones from further up the stack.
 */
static inline void vma_end_write_all(struct mm_struct *mm)
{
	mmap_assert_write_locked(mm);
	mm_lock_seqcount_end(mm);
}

static inline void mmap_write_unlock(struct mm_struct *mm)
{
	__mmap_lock_trace_released(mm, true);
	vma_end_write_all(mm);
	up_write(&mm->mmap_lock);
}

static inline void mmap_write_downgrade(struct mm_struct *mm)
{
	__mmap_lock_trace_acquire_returned(mm, false, true);
	vma_end_write_all(mm);
	downgrade_write(&mm->mmap_lock);
}

static inline void mmap_read_lock(struct mm_struct *mm)
{
	__mmap_lock_trace_start_locking(mm, false);
	down_read(&mm->mmap_lock);
	__mmap_lock_trace_acquire_returned(mm, false, true);
}

static inline int mmap_read_lock_killable(struct mm_struct *mm)
{
	int ret;

	__mmap_lock_trace_start_locking(mm, false);
	ret = down_read_killable(&mm->mmap_lock);
	__mmap_lock_trace_acquire_returned(mm, false, ret == 0);
	return ret;
}

static inline bool mmap_read_trylock(struct mm_struct *mm)
{
	bool ret;

	__mmap_lock_trace_start_locking(mm, false);
	ret = down_read_trylock(&mm->mmap_lock) != 0;
	__mmap_lock_trace_acquire_returned(mm, false, ret);
	return ret;
}

static inline void mmap_read_unlock(struct mm_struct *mm)
{
	__mmap_lock_trace_released(mm, false);
	up_read(&mm->mmap_lock);
}

DEFINE_GUARD(mmap_read_lock, struct mm_struct *,
	     mmap_read_lock(_T), mmap_read_unlock(_T))

static inline void mmap_read_unlock_non_owner(struct mm_struct *mm)
{
	__mmap_lock_trace_released(mm, false);
	up_read_non_owner(&mm->mmap_lock);
}

static inline int mmap_lock_is_contended(struct mm_struct *mm)
{
	return rwsem_is_contended(&mm->mmap_lock);
}

#endif /* _LINUX_MMAP_LOCK_H */
