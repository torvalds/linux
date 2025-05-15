/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_MM_H
#define _LINUX_SCHED_MM_H

#include <linux/kernel.h>
#include <linux/atomic.h>
#include <linux/sched.h>
#include <linux/mm_types.h>
#include <linux/gfp.h>
#include <linux/sync_core.h>
#include <linux/sched/coredump.h>

/*
 * Routines for handling mm_structs
 */
extern struct mm_struct *mm_alloc(void);

/**
 * mmgrab() - Pin a &struct mm_struct.
 * @mm: The &struct mm_struct to pin.
 *
 * Make sure that @mm will not get freed even after the owning task
 * exits. This doesn't guarantee that the associated address space
 * will still exist later on and mmget_not_zero() has to be used before
 * accessing it.
 *
 * This is a preferred way to pin @mm for a longer/unbounded amount
 * of time.
 *
 * Use mmdrop() to release the reference acquired by mmgrab().
 *
 * See also <Documentation/mm/active_mm.rst> for an in-depth explanation
 * of &mm_struct.mm_count vs &mm_struct.mm_users.
 */
static inline void mmgrab(struct mm_struct *mm)
{
	atomic_inc(&mm->mm_count);
}

static inline void smp_mb__after_mmgrab(void)
{
	smp_mb__after_atomic();
}

extern void __mmdrop(struct mm_struct *mm);

static inline void mmdrop(struct mm_struct *mm)
{
	/*
	 * The implicit full barrier implied by atomic_dec_and_test() is
	 * required by the membarrier system call before returning to
	 * user-space, after storing to rq->curr.
	 */
	if (unlikely(atomic_dec_and_test(&mm->mm_count)))
		__mmdrop(mm);
}

#ifdef CONFIG_PREEMPT_RT
/*
 * RCU callback for delayed mm drop. Not strictly RCU, but call_rcu() is
 * by far the least expensive way to do that.
 */
static inline void __mmdrop_delayed(struct rcu_head *rhp)
{
	struct mm_struct *mm = container_of(rhp, struct mm_struct, delayed_drop);

	__mmdrop(mm);
}

/*
 * Invoked from finish_task_switch(). Delegates the heavy lifting on RT
 * kernels via RCU.
 */
static inline void mmdrop_sched(struct mm_struct *mm)
{
	/* Provides a full memory barrier. See mmdrop() */
	if (atomic_dec_and_test(&mm->mm_count))
		call_rcu(&mm->delayed_drop, __mmdrop_delayed);
}
#else
static inline void mmdrop_sched(struct mm_struct *mm)
{
	mmdrop(mm);
}
#endif

/* Helpers for lazy TLB mm refcounting */
static inline void mmgrab_lazy_tlb(struct mm_struct *mm)
{
	if (IS_ENABLED(CONFIG_MMU_LAZY_TLB_REFCOUNT))
		mmgrab(mm);
}

static inline void mmdrop_lazy_tlb(struct mm_struct *mm)
{
	if (IS_ENABLED(CONFIG_MMU_LAZY_TLB_REFCOUNT)) {
		mmdrop(mm);
	} else {
		/*
		 * mmdrop_lazy_tlb must provide a full memory barrier, see the
		 * membarrier comment finish_task_switch which relies on this.
		 */
		smp_mb();
	}
}

static inline void mmdrop_lazy_tlb_sched(struct mm_struct *mm)
{
	if (IS_ENABLED(CONFIG_MMU_LAZY_TLB_REFCOUNT))
		mmdrop_sched(mm);
	else
		smp_mb(); /* see mmdrop_lazy_tlb() above */
}

/**
 * mmget() - Pin the address space associated with a &struct mm_struct.
 * @mm: The address space to pin.
 *
 * Make sure that the address space of the given &struct mm_struct doesn't
 * go away. This does not protect against parts of the address space being
 * modified or freed, however.
 *
 * Never use this function to pin this address space for an
 * unbounded/indefinite amount of time.
 *
 * Use mmput() to release the reference acquired by mmget().
 *
 * See also <Documentation/mm/active_mm.rst> for an in-depth explanation
 * of &mm_struct.mm_count vs &mm_struct.mm_users.
 */
static inline void mmget(struct mm_struct *mm)
{
	atomic_inc(&mm->mm_users);
}

static inline bool mmget_not_zero(struct mm_struct *mm)
{
	return atomic_inc_not_zero(&mm->mm_users);
}

/* mmput gets rid of the mappings and all user-space */
extern void mmput(struct mm_struct *);
#ifdef CONFIG_MMU
/* same as above but performs the slow path from the async context. Can
 * be called from the atomic context as well
 */
void mmput_async(struct mm_struct *);
#endif

/* Grab a reference to a task's mm, if it is not already going away */
extern struct mm_struct *get_task_mm(struct task_struct *task);
/*
 * Grab a reference to a task's mm, if it is not already going away
 * and ptrace_may_access with the mode parameter passed to it
 * succeeds.
 */
extern struct mm_struct *mm_access(struct task_struct *task, unsigned int mode);
/* Remove the current tasks stale references to the old mm_struct on exit() */
extern void exit_mm_release(struct task_struct *, struct mm_struct *);
/* Remove the current tasks stale references to the old mm_struct on exec() */
extern void exec_mm_release(struct task_struct *, struct mm_struct *);

#ifdef CONFIG_MEMCG
extern void mm_update_next_owner(struct mm_struct *mm);
#else
static inline void mm_update_next_owner(struct mm_struct *mm)
{
}
#endif /* CONFIG_MEMCG */

#ifdef CONFIG_MMU
#ifndef arch_get_mmap_end
#define arch_get_mmap_end(addr, len, flags)	(TASK_SIZE)
#endif

#ifndef arch_get_mmap_base
#define arch_get_mmap_base(addr, base) (base)
#endif

extern void arch_pick_mmap_layout(struct mm_struct *mm,
				  struct rlimit *rlim_stack);

unsigned long
arch_get_unmapped_area(struct file *filp, unsigned long addr,
		       unsigned long len, unsigned long pgoff,
		       unsigned long flags, vm_flags_t vm_flags);
unsigned long
arch_get_unmapped_area_topdown(struct file *filp, unsigned long addr,
			       unsigned long len, unsigned long pgoff,
			       unsigned long flags, vm_flags_t);

unsigned long mm_get_unmapped_area(struct mm_struct *mm, struct file *filp,
				   unsigned long addr, unsigned long len,
				   unsigned long pgoff, unsigned long flags);

unsigned long mm_get_unmapped_area_vmflags(struct mm_struct *mm,
					   struct file *filp,
					   unsigned long addr,
					   unsigned long len,
					   unsigned long pgoff,
					   unsigned long flags,
					   vm_flags_t vm_flags);

unsigned long
generic_get_unmapped_area(struct file *filp, unsigned long addr,
			  unsigned long len, unsigned long pgoff,
			  unsigned long flags, vm_flags_t vm_flags);
unsigned long
generic_get_unmapped_area_topdown(struct file *filp, unsigned long addr,
				  unsigned long len, unsigned long pgoff,
				  unsigned long flags, vm_flags_t vm_flags);
#else
static inline void arch_pick_mmap_layout(struct mm_struct *mm,
					 struct rlimit *rlim_stack) {}
#endif

static inline bool in_vfork(struct task_struct *tsk)
{
	bool ret;

	/*
	 * need RCU to access ->real_parent if CLONE_VM was used along with
	 * CLONE_PARENT.
	 *
	 * We check real_parent->mm == tsk->mm because CLONE_VFORK does not
	 * imply CLONE_VM
	 *
	 * CLONE_VFORK can be used with CLONE_PARENT/CLONE_THREAD and thus
	 * ->real_parent is not necessarily the task doing vfork(), so in
	 * theory we can't rely on task_lock() if we want to dereference it.
	 *
	 * And in this case we can't trust the real_parent->mm == tsk->mm
	 * check, it can be false negative. But we do not care, if init or
	 * another oom-unkillable task does this it should blame itself.
	 */
	rcu_read_lock();
	ret = tsk->vfork_done &&
			rcu_dereference(tsk->real_parent)->mm == tsk->mm;
	rcu_read_unlock();

	return ret;
}

/*
 * Applies per-task gfp context to the given allocation flags.
 * PF_MEMALLOC_NOIO implies GFP_NOIO
 * PF_MEMALLOC_NOFS implies GFP_NOFS
 * PF_MEMALLOC_PIN  implies !GFP_MOVABLE
 */
static inline gfp_t current_gfp_context(gfp_t flags)
{
	unsigned int pflags = READ_ONCE(current->flags);

	if (unlikely(pflags & (PF_MEMALLOC_NOIO | PF_MEMALLOC_NOFS | PF_MEMALLOC_PIN))) {
		/*
		 * NOIO implies both NOIO and NOFS and it is a weaker context
		 * so always make sure it makes precedence
		 */
		if (pflags & PF_MEMALLOC_NOIO)
			flags &= ~(__GFP_IO | __GFP_FS);
		else if (pflags & PF_MEMALLOC_NOFS)
			flags &= ~__GFP_FS;

		if (pflags & PF_MEMALLOC_PIN)
			flags &= ~__GFP_MOVABLE;
	}
	return flags;
}

#ifdef CONFIG_LOCKDEP
extern void __fs_reclaim_acquire(unsigned long ip);
extern void __fs_reclaim_release(unsigned long ip);
extern void fs_reclaim_acquire(gfp_t gfp_mask);
extern void fs_reclaim_release(gfp_t gfp_mask);
#else
static inline void __fs_reclaim_acquire(unsigned long ip) { }
static inline void __fs_reclaim_release(unsigned long ip) { }
static inline void fs_reclaim_acquire(gfp_t gfp_mask) { }
static inline void fs_reclaim_release(gfp_t gfp_mask) { }
#endif

/* Any memory-allocation retry loop should use
 * memalloc_retry_wait(), and pass the flags for the most
 * constrained allocation attempt that might have failed.
 * This provides useful documentation of where loops are,
 * and a central place to fine tune the waiting as the MM
 * implementation changes.
 */
static inline void memalloc_retry_wait(gfp_t gfp_flags)
{
	/* We use io_schedule_timeout because waiting for memory
	 * typically included waiting for dirty pages to be
	 * written out, which requires IO.
	 */
	__set_current_state(TASK_UNINTERRUPTIBLE);
	gfp_flags = current_gfp_context(gfp_flags);
	if (gfpflags_allow_blocking(gfp_flags) &&
	    !(gfp_flags & __GFP_NORETRY))
		/* Probably waited already, no need for much more */
		io_schedule_timeout(1);
	else
		/* Probably didn't wait, and has now released a lock,
		 * so now is a good time to wait
		 */
		io_schedule_timeout(HZ/50);
}

/**
 * might_alloc - Mark possible allocation sites
 * @gfp_mask: gfp_t flags that would be used to allocate
 *
 * Similar to might_sleep() and other annotations, this can be used in functions
 * that might allocate, but often don't. Compiles to nothing without
 * CONFIG_LOCKDEP. Includes a conditional might_sleep() if @gfp allows blocking.
 */
static inline void might_alloc(gfp_t gfp_mask)
{
	fs_reclaim_acquire(gfp_mask);
	fs_reclaim_release(gfp_mask);

	might_sleep_if(gfpflags_allow_blocking(gfp_mask));
}

/**
 * memalloc_flags_save - Add a PF_* flag to current->flags, save old value
 *
 * This allows PF_* flags to be conveniently added, irrespective of current
 * value, and then the old version restored with memalloc_flags_restore().
 */
static inline unsigned memalloc_flags_save(unsigned flags)
{
	unsigned oldflags = ~current->flags & flags;
	current->flags |= flags;
	return oldflags;
}

static inline void memalloc_flags_restore(unsigned flags)
{
	current->flags &= ~flags;
}

/**
 * memalloc_noio_save - Marks implicit GFP_NOIO allocation scope.
 *
 * This functions marks the beginning of the GFP_NOIO allocation scope.
 * All further allocations will implicitly drop __GFP_IO flag and so
 * they are safe for the IO critical section from the allocation recursion
 * point of view. Use memalloc_noio_restore to end the scope with flags
 * returned by this function.
 *
 * Context: This function is safe to be used from any context.
 * Return: The saved flags to be passed to memalloc_noio_restore.
 */
static inline unsigned int memalloc_noio_save(void)
{
	return memalloc_flags_save(PF_MEMALLOC_NOIO);
}

/**
 * memalloc_noio_restore - Ends the implicit GFP_NOIO scope.
 * @flags: Flags to restore.
 *
 * Ends the implicit GFP_NOIO scope started by memalloc_noio_save function.
 * Always make sure that the given flags is the return value from the
 * pairing memalloc_noio_save call.
 */
static inline void memalloc_noio_restore(unsigned int flags)
{
	memalloc_flags_restore(flags);
}

/**
 * memalloc_nofs_save - Marks implicit GFP_NOFS allocation scope.
 *
 * This functions marks the beginning of the GFP_NOFS allocation scope.
 * All further allocations will implicitly drop __GFP_FS flag and so
 * they are safe for the FS critical section from the allocation recursion
 * point of view. Use memalloc_nofs_restore to end the scope with flags
 * returned by this function.
 *
 * Context: This function is safe to be used from any context.
 * Return: The saved flags to be passed to memalloc_nofs_restore.
 */
static inline unsigned int memalloc_nofs_save(void)
{
	return memalloc_flags_save(PF_MEMALLOC_NOFS);
}

/**
 * memalloc_nofs_restore - Ends the implicit GFP_NOFS scope.
 * @flags: Flags to restore.
 *
 * Ends the implicit GFP_NOFS scope started by memalloc_nofs_save function.
 * Always make sure that the given flags is the return value from the
 * pairing memalloc_nofs_save call.
 */
static inline void memalloc_nofs_restore(unsigned int flags)
{
	memalloc_flags_restore(flags);
}

/**
 * memalloc_noreclaim_save - Marks implicit __GFP_MEMALLOC scope.
 *
 * This function marks the beginning of the __GFP_MEMALLOC allocation scope.
 * All further allocations will implicitly add the __GFP_MEMALLOC flag, which
 * prevents entering reclaim and allows access to all memory reserves. This
 * should only be used when the caller guarantees the allocation will allow more
 * memory to be freed very shortly, i.e. it needs to allocate some memory in
 * the process of freeing memory, and cannot reclaim due to potential recursion.
 *
 * Users of this scope have to be extremely careful to not deplete the reserves
 * completely and implement a throttling mechanism which controls the
 * consumption of the reserve based on the amount of freed memory. Usage of a
 * pre-allocated pool (e.g. mempool) should be always considered before using
 * this scope.
 *
 * Individual allocations under the scope can opt out using __GFP_NOMEMALLOC
 *
 * Context: This function should not be used in an interrupt context as that one
 *          does not give PF_MEMALLOC access to reserves.
 *          See __gfp_pfmemalloc_flags().
 * Return: The saved flags to be passed to memalloc_noreclaim_restore.
 */
static inline unsigned int memalloc_noreclaim_save(void)
{
	return memalloc_flags_save(PF_MEMALLOC);
}

/**
 * memalloc_noreclaim_restore - Ends the implicit __GFP_MEMALLOC scope.
 * @flags: Flags to restore.
 *
 * Ends the implicit __GFP_MEMALLOC scope started by memalloc_noreclaim_save
 * function. Always make sure that the given flags is the return value from the
 * pairing memalloc_noreclaim_save call.
 */
static inline void memalloc_noreclaim_restore(unsigned int flags)
{
	memalloc_flags_restore(flags);
}

/**
 * memalloc_pin_save - Marks implicit ~__GFP_MOVABLE scope.
 *
 * This function marks the beginning of the ~__GFP_MOVABLE allocation scope.
 * All further allocations will implicitly remove the __GFP_MOVABLE flag, which
 * will constraint the allocations to zones that allow long term pinning, i.e.
 * not ZONE_MOVABLE zones.
 *
 * Return: The saved flags to be passed to memalloc_pin_restore.
 */
static inline unsigned int memalloc_pin_save(void)
{
	return memalloc_flags_save(PF_MEMALLOC_PIN);
}

/**
 * memalloc_pin_restore - Ends the implicit ~__GFP_MOVABLE scope.
 * @flags: Flags to restore.
 *
 * Ends the implicit ~__GFP_MOVABLE scope started by memalloc_pin_save function.
 * Always make sure that the given flags is the return value from the pairing
 * memalloc_pin_save call.
 */
static inline void memalloc_pin_restore(unsigned int flags)
{
	memalloc_flags_restore(flags);
}

#ifdef CONFIG_MEMCG
DECLARE_PER_CPU(struct mem_cgroup *, int_active_memcg);
/**
 * set_active_memcg - Starts the remote memcg charging scope.
 * @memcg: memcg to charge.
 *
 * This function marks the beginning of the remote memcg charging scope. All the
 * __GFP_ACCOUNT allocations till the end of the scope will be charged to the
 * given memcg.
 *
 * Please, make sure that caller has a reference to the passed memcg structure,
 * so its lifetime is guaranteed to exceed the scope between two
 * set_active_memcg() calls.
 *
 * NOTE: This function can nest. Users must save the return value and
 * reset the previous value after their own charging scope is over.
 */
static inline struct mem_cgroup *
set_active_memcg(struct mem_cgroup *memcg)
{
	struct mem_cgroup *old;

	if (!in_task()) {
		old = this_cpu_read(int_active_memcg);
		this_cpu_write(int_active_memcg, memcg);
	} else {
		old = current->active_memcg;
		current->active_memcg = memcg;
	}

	return old;
}
#else
static inline struct mem_cgroup *
set_active_memcg(struct mem_cgroup *memcg)
{
	return NULL;
}
#endif

#ifdef CONFIG_MEMBARRIER
enum {
	MEMBARRIER_STATE_PRIVATE_EXPEDITED_READY		= (1U << 0),
	MEMBARRIER_STATE_PRIVATE_EXPEDITED			= (1U << 1),
	MEMBARRIER_STATE_GLOBAL_EXPEDITED_READY			= (1U << 2),
	MEMBARRIER_STATE_GLOBAL_EXPEDITED			= (1U << 3),
	MEMBARRIER_STATE_PRIVATE_EXPEDITED_SYNC_CORE_READY	= (1U << 4),
	MEMBARRIER_STATE_PRIVATE_EXPEDITED_SYNC_CORE		= (1U << 5),
	MEMBARRIER_STATE_PRIVATE_EXPEDITED_RSEQ_READY		= (1U << 6),
	MEMBARRIER_STATE_PRIVATE_EXPEDITED_RSEQ			= (1U << 7),
};

enum {
	MEMBARRIER_FLAG_SYNC_CORE	= (1U << 0),
	MEMBARRIER_FLAG_RSEQ		= (1U << 1),
};

#ifdef CONFIG_ARCH_HAS_MEMBARRIER_CALLBACKS
#include <asm/membarrier.h>
#endif

static inline void membarrier_mm_sync_core_before_usermode(struct mm_struct *mm)
{
	/*
	 * The atomic_read() below prevents CSE. The following should
	 * help the compiler generate more efficient code on architectures
	 * where sync_core_before_usermode() is a no-op.
	 */
	if (!IS_ENABLED(CONFIG_ARCH_HAS_SYNC_CORE_BEFORE_USERMODE))
		return;
	if (current->mm != mm)
		return;
	if (likely(!(atomic_read(&mm->membarrier_state) &
		     MEMBARRIER_STATE_PRIVATE_EXPEDITED_SYNC_CORE)))
		return;
	sync_core_before_usermode();
}

extern void membarrier_exec_mmap(struct mm_struct *mm);

extern void membarrier_update_current_mm(struct mm_struct *next_mm);

#else
#ifdef CONFIG_ARCH_HAS_MEMBARRIER_CALLBACKS
static inline void membarrier_arch_switch_mm(struct mm_struct *prev,
					     struct mm_struct *next,
					     struct task_struct *tsk)
{
}
#endif
static inline void membarrier_exec_mmap(struct mm_struct *mm)
{
}
static inline void membarrier_mm_sync_core_before_usermode(struct mm_struct *mm)
{
}
static inline void membarrier_update_current_mm(struct mm_struct *next_mm)
{
}
#endif

#endif /* _LINUX_SCHED_MM_H */
