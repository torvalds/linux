/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2021 Facebook
 */

#ifndef __MMAP_UNLOCK_WORK_H__
#define __MMAP_UNLOCK_WORK_H__
#include <linux/atomic.h>
#include <linux/err.h>
#include <linux/irq_work.h>

/* irq_work to run mmap_read_unlock() in irq_work */
struct mmap_unlock_irq_work {
	struct irq_work irq_work;
	struct mm_struct *mm;
	atomic_t active;
};

DECLARE_PER_CPU(struct mmap_unlock_irq_work, mmap_unlock_work);

/*
 * We cannot do mmap_read_unlock() when the irq is disabled, because of
 * risk to deadlock with rq_lock. To look up vma when the irqs are
 * disabled, we need to run mmap_read_unlock() in irq_work. We use a
 * percpu variable to do the irq_work. The active flag reserves the slot
 * before mmap_read_trylock() and until the irq_work callback consumes mm.
 */
static inline struct mmap_unlock_irq_work *bpf_mmap_unlock_guard_get(void)
{
	struct mmap_unlock_irq_work *work;

	if (!irqs_disabled())
		return NULL;

	/*
	 * PREEMPT_RT does not allow to trylock mmap sem in interrupt
	 * disabled context. Force the fallback code.
	 */
	if (IS_ENABLED(CONFIG_PREEMPT_RT))
		return ERR_PTR(-EBUSY);

	work = this_cpu_ptr(&mmap_unlock_work);
	if (irq_work_is_busy(&work->irq_work) ||
	    atomic_cmpxchg_acquire(&work->active, 0, 1))
		return ERR_PTR(-EBUSY);

	return work;
}

static inline void
bpf_mmap_unlock_guard_put(struct mmap_unlock_irq_work *work)
{
	if (work)
		atomic_set_release(&work->active, 0);
}

static inline void bpf_mmap_unlock_mm(struct mmap_unlock_irq_work *work, struct mm_struct *mm)
{
	if (!work) {
		mmap_read_unlock(mm);
	} else {
		work->mm = mm;

		/* The lock will be released once we're out of interrupt
		 * context. Tell lockdep that we've released it now so
		 * it doesn't complain that we forgot to release it.
		 */
		rwsem_release(&mm->mmap_lock.dep_map, _RET_IP_);
		irq_work_queue(&work->irq_work);
	}
}

#endif /* __MMAP_UNLOCK_WORK_H__ */
