#define pr_fmt(fmt) "%s: " fmt "\n", __func__

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/percpu-refcount.h>

/*
 * Initially, a percpu refcount is just a set of percpu counters. Initially, we
 * don't try to detect the ref hitting 0 - which means that get/put can just
 * increment or decrement the local counter. Note that the counter on a
 * particular cpu can (and will) wrap - this is fine, when we go to shutdown the
 * percpu counters will all sum to the correct value
 *
 * (More precisely: because modular arithmetic is commutative the sum of all the
 * percpu_count vars will be equal to what it would have been if all the gets
 * and puts were done to a single integer, even if some of the percpu integers
 * overflow or underflow).
 *
 * The real trick to implementing percpu refcounts is shutdown. We can't detect
 * the ref hitting 0 on every put - this would require global synchronization
 * and defeat the whole purpose of using percpu refs.
 *
 * What we do is require the user to keep track of the initial refcount; we know
 * the ref can't hit 0 before the user drops the initial ref, so as long as we
 * convert to non percpu mode before the initial ref is dropped everything
 * works.
 *
 * Converting to non percpu mode is done with some RCUish stuff in
 * percpu_ref_kill. Additionally, we need a bias value so that the
 * atomic_long_t can't hit 0 before we've added up all the percpu refs.
 */

#define PERCPU_COUNT_BIAS	(1LU << (BITS_PER_LONG - 1))

static DEFINE_SPINLOCK(percpu_ref_switch_lock);
static DECLARE_WAIT_QUEUE_HEAD(percpu_ref_switch_waitq);

static unsigned long __percpu *percpu_count_ptr(struct percpu_ref *ref)
{
	return (unsigned long __percpu *)
		(ref->percpu_count_ptr & ~__PERCPU_REF_ATOMIC_DEAD);
}

/**
 * percpu_ref_init - initialize a percpu refcount
 * @ref: percpu_ref to initialize
 * @release: function which will be called when refcount hits 0
 * @flags: PERCPU_REF_INIT_* flags
 * @gfp: allocation mask to use
 *
 * Initializes @ref.  If @flags is zero, @ref starts in percpu mode with a
 * refcount of 1; analagous to atomic_long_set(ref, 1).  See the
 * definitions of PERCPU_REF_INIT_* flags for flag behaviors.
 *
 * Note that @release must not sleep - it may potentially be called from RCU
 * callback context by percpu_ref_kill().
 */
int percpu_ref_init(struct percpu_ref *ref, percpu_ref_func_t *release,
		    unsigned int flags, gfp_t gfp)
{
	size_t align = max_t(size_t, 1 << __PERCPU_REF_FLAG_BITS,
			     __alignof__(unsigned long));
	unsigned long start_count = 0;

	ref->percpu_count_ptr = (unsigned long)
		__alloc_percpu_gfp(sizeof(unsigned long), align, gfp);
	if (!ref->percpu_count_ptr)
		return -ENOMEM;

	ref->force_atomic = flags & PERCPU_REF_INIT_ATOMIC;

	if (flags & (PERCPU_REF_INIT_ATOMIC | PERCPU_REF_INIT_DEAD))
		ref->percpu_count_ptr |= __PERCPU_REF_ATOMIC;
	else
		start_count += PERCPU_COUNT_BIAS;

	if (flags & PERCPU_REF_INIT_DEAD)
		ref->percpu_count_ptr |= __PERCPU_REF_DEAD;
	else
		start_count++;

	atomic_long_set(&ref->count, start_count);

	ref->release = release;
	ref->confirm_switch = NULL;
	return 0;
}
EXPORT_SYMBOL_GPL(percpu_ref_init);

/**
 * percpu_ref_exit - undo percpu_ref_init()
 * @ref: percpu_ref to exit
 *
 * This function exits @ref.  The caller is responsible for ensuring that
 * @ref is no longer in active use.  The usual places to invoke this
 * function from are the @ref->release() callback or in init failure path
 * where percpu_ref_init() succeeded but other parts of the initialization
 * of the embedding object failed.
 */
void percpu_ref_exit(struct percpu_ref *ref)
{
	unsigned long __percpu *percpu_count = percpu_count_ptr(ref);

	if (percpu_count) {
		/* non-NULL confirm_switch indicates switching in progress */
		WARN_ON_ONCE(ref->confirm_switch);
		free_percpu(percpu_count);
		ref->percpu_count_ptr = __PERCPU_REF_ATOMIC_DEAD;
	}
}
EXPORT_SYMBOL_GPL(percpu_ref_exit);

static void percpu_ref_call_confirm_rcu(struct rcu_head *rcu)
{
	struct percpu_ref *ref = container_of(rcu, struct percpu_ref, rcu);

	ref->confirm_switch(ref);
	ref->confirm_switch = NULL;
	wake_up_all(&percpu_ref_switch_waitq);

	/* drop ref from percpu_ref_switch_to_atomic() */
	percpu_ref_put(ref);
}

static void percpu_ref_switch_to_atomic_rcu(struct rcu_head *rcu)
{
	struct percpu_ref *ref = container_of(rcu, struct percpu_ref, rcu);
	unsigned long __percpu *percpu_count = percpu_count_ptr(ref);
	unsigned long count = 0;
	int cpu;

	for_each_possible_cpu(cpu)
		count += *per_cpu_ptr(percpu_count, cpu);

	pr_debug("global %ld percpu %ld",
		 atomic_long_read(&ref->count), (long)count);

	/*
	 * It's crucial that we sum the percpu counters _before_ adding the sum
	 * to &ref->count; since gets could be happening on one cpu while puts
	 * happen on another, adding a single cpu's count could cause
	 * @ref->count to hit 0 before we've got a consistent value - but the
	 * sum of all the counts will be consistent and correct.
	 *
	 * Subtracting the bias value then has to happen _after_ adding count to
	 * &ref->count; we need the bias value to prevent &ref->count from
	 * reaching 0 before we add the percpu counts. But doing it at the same
	 * time is equivalent and saves us atomic operations:
	 */
	atomic_long_add((long)count - PERCPU_COUNT_BIAS, &ref->count);

	WARN_ONCE(atomic_long_read(&ref->count) <= 0,
		  "percpu ref (%pf) <= 0 (%ld) after switching to atomic",
		  ref->release, atomic_long_read(&ref->count));

	/* @ref is viewed as dead on all CPUs, send out switch confirmation */
	percpu_ref_call_confirm_rcu(rcu);
}

static void percpu_ref_noop_confirm_switch(struct percpu_ref *ref)
{
}

static void __percpu_ref_switch_to_atomic(struct percpu_ref *ref,
					  percpu_ref_func_t *confirm_switch)
{
	if (ref->percpu_count_ptr & __PERCPU_REF_ATOMIC) {
		if (confirm_switch)
			confirm_switch(ref);
		return;
	}

	/* switching from percpu to atomic */
	ref->percpu_count_ptr |= __PERCPU_REF_ATOMIC;

	/*
	 * Non-NULL ->confirm_switch is used to indicate that switching is
	 * in progress.  Use noop one if unspecified.
	 */
	ref->confirm_switch = confirm_switch ?: percpu_ref_noop_confirm_switch;

	percpu_ref_get(ref);	/* put after confirmation */
	call_rcu(&ref->rcu, percpu_ref_switch_to_atomic_rcu);
}

static void __percpu_ref_switch_to_percpu(struct percpu_ref *ref)
{
	unsigned long __percpu *percpu_count = percpu_count_ptr(ref);
	int cpu;

	BUG_ON(!percpu_count);

	if (!(ref->percpu_count_ptr & __PERCPU_REF_ATOMIC))
		return;

	atomic_long_add(PERCPU_COUNT_BIAS, &ref->count);

	/*
	 * Restore per-cpu operation.  smp_store_release() is paired
	 * with READ_ONCE() in __ref_is_percpu() and guarantees that the
	 * zeroing is visible to all percpu accesses which can see the
	 * following __PERCPU_REF_ATOMIC clearing.
	 */
	for_each_possible_cpu(cpu)
		*per_cpu_ptr(percpu_count, cpu) = 0;

	smp_store_release(&ref->percpu_count_ptr,
			  ref->percpu_count_ptr & ~__PERCPU_REF_ATOMIC);
}

static void __percpu_ref_switch_mode(struct percpu_ref *ref,
				     percpu_ref_func_t *confirm_switch)
{
	lockdep_assert_held(&percpu_ref_switch_lock);

	/*
	 * If the previous ATOMIC switching hasn't finished yet, wait for
	 * its completion.  If the caller ensures that ATOMIC switching
	 * isn't in progress, this function can be called from any context.
	 */
	wait_event_lock_irq(percpu_ref_switch_waitq, !ref->confirm_switch,
			    percpu_ref_switch_lock);

	if (ref->force_atomic || (ref->percpu_count_ptr & __PERCPU_REF_DEAD))
		__percpu_ref_switch_to_atomic(ref, confirm_switch);
	else
		__percpu_ref_switch_to_percpu(ref);
}

/**
 * percpu_ref_switch_to_atomic - switch a percpu_ref to atomic mode
 * @ref: percpu_ref to switch to atomic mode
 * @confirm_switch: optional confirmation callback
 *
 * There's no reason to use this function for the usual reference counting.
 * Use percpu_ref_kill[_and_confirm]().
 *
 * Schedule switching of @ref to atomic mode.  All its percpu counts will
 * be collected to the main atomic counter.  On completion, when all CPUs
 * are guaraneed to be in atomic mode, @confirm_switch, which may not
 * block, is invoked.  This function may be invoked concurrently with all
 * the get/put operations and can safely be mixed with kill and reinit
 * operations.  Note that @ref will stay in atomic mode across kill/reinit
 * cycles until percpu_ref_switch_to_percpu() is called.
 *
 * This function may block if @ref is in the process of switching to atomic
 * mode.  If the caller ensures that @ref is not in the process of
 * switching to atomic mode, this function can be called from any context.
 */
void percpu_ref_switch_to_atomic(struct percpu_ref *ref,
				 percpu_ref_func_t *confirm_switch)
{
	unsigned long flags;

	spin_lock_irqsave(&percpu_ref_switch_lock, flags);

	ref->force_atomic = true;
	__percpu_ref_switch_mode(ref, confirm_switch);

	spin_unlock_irqrestore(&percpu_ref_switch_lock, flags);
}
EXPORT_SYMBOL_GPL(percpu_ref_switch_to_atomic);

/**
 * percpu_ref_switch_to_atomic_sync - switch a percpu_ref to atomic mode
 * @ref: percpu_ref to switch to atomic mode
 *
 * Schedule switching the ref to atomic mode, and wait for the
 * switch to complete.  Caller must ensure that no other thread
 * will switch back to percpu mode.
 */
void percpu_ref_switch_to_atomic_sync(struct percpu_ref *ref)
{
	percpu_ref_switch_to_atomic(ref, NULL);
	wait_event(percpu_ref_switch_waitq, !ref->confirm_switch);
}
EXPORT_SYMBOL_GPL(percpu_ref_switch_to_atomic_sync);

/**
 * percpu_ref_switch_to_percpu - switch a percpu_ref to percpu mode
 * @ref: percpu_ref to switch to percpu mode
 *
 * There's no reason to use this function for the usual reference counting.
 * To re-use an expired ref, use percpu_ref_reinit().
 *
 * Switch @ref to percpu mode.  This function may be invoked concurrently
 * with all the get/put operations and can safely be mixed with kill and
 * reinit operations.  This function reverses the sticky atomic state set
 * by PERCPU_REF_INIT_ATOMIC or percpu_ref_switch_to_atomic().  If @ref is
 * dying or dead, the actual switching takes place on the following
 * percpu_ref_reinit().
 *
 * This function may block if @ref is in the process of switching to atomic
 * mode.  If the caller ensures that @ref is not in the process of
 * switching to atomic mode, this function can be called from any context.
 */
void percpu_ref_switch_to_percpu(struct percpu_ref *ref)
{
	unsigned long flags;

	spin_lock_irqsave(&percpu_ref_switch_lock, flags);

	ref->force_atomic = false;
	__percpu_ref_switch_mode(ref, NULL);

	spin_unlock_irqrestore(&percpu_ref_switch_lock, flags);
}
EXPORT_SYMBOL_GPL(percpu_ref_switch_to_percpu);

/**
 * percpu_ref_kill_and_confirm - drop the initial ref and schedule confirmation
 * @ref: percpu_ref to kill
 * @confirm_kill: optional confirmation callback
 *
 * Equivalent to percpu_ref_kill() but also schedules kill confirmation if
 * @confirm_kill is not NULL.  @confirm_kill, which may not block, will be
 * called after @ref is seen as dead from all CPUs at which point all
 * further invocations of percpu_ref_tryget_live() will fail.  See
 * percpu_ref_tryget_live() for details.
 *
 * This function normally doesn't block and can be called from any context
 * but it may block if @confirm_kill is specified and @ref is in the
 * process of switching to atomic mode by percpu_ref_switch_to_atomic().
 *
 * There are no implied RCU grace periods between kill and release.
 */
void percpu_ref_kill_and_confirm(struct percpu_ref *ref,
				 percpu_ref_func_t *confirm_kill)
{
	unsigned long flags;

	spin_lock_irqsave(&percpu_ref_switch_lock, flags);

	WARN_ONCE(ref->percpu_count_ptr & __PERCPU_REF_DEAD,
		  "%s called more than once on %pf!", __func__, ref->release);

	ref->percpu_count_ptr |= __PERCPU_REF_DEAD;
	__percpu_ref_switch_mode(ref, confirm_kill);
	percpu_ref_put(ref);

	spin_unlock_irqrestore(&percpu_ref_switch_lock, flags);
}
EXPORT_SYMBOL_GPL(percpu_ref_kill_and_confirm);

/**
 * percpu_ref_reinit - re-initialize a percpu refcount
 * @ref: perpcu_ref to re-initialize
 *
 * Re-initialize @ref so that it's in the same state as when it finished
 * percpu_ref_init() ignoring %PERCPU_REF_INIT_DEAD.  @ref must have been
 * initialized successfully and reached 0 but not exited.
 *
 * Note that percpu_ref_tryget[_live]() are safe to perform on @ref while
 * this function is in progress.
 */
void percpu_ref_reinit(struct percpu_ref *ref)
{
	WARN_ON_ONCE(!percpu_ref_is_zero(ref));

	percpu_ref_resurrect(ref);
}
EXPORT_SYMBOL_GPL(percpu_ref_reinit);

/**
 * percpu_ref_resurrect - modify a percpu refcount from dead to live
 * @ref: perpcu_ref to resurrect
 *
 * Modify @ref so that it's in the same state as before percpu_ref_kill() was
 * called. @ref must be dead but must not yet have exited.
 *
 * If @ref->release() frees @ref then the caller is responsible for
 * guaranteeing that @ref->release() does not get called while this
 * function is in progress.
 *
 * Note that percpu_ref_tryget[_live]() are safe to perform on @ref while
 * this function is in progress.
 */
void percpu_ref_resurrect(struct percpu_ref *ref)
{
	unsigned long __percpu *percpu_count;
	unsigned long flags;

	spin_lock_irqsave(&percpu_ref_switch_lock, flags);

	WARN_ON_ONCE(!(ref->percpu_count_ptr & __PERCPU_REF_DEAD));
	WARN_ON_ONCE(__ref_is_percpu(ref, &percpu_count));

	ref->percpu_count_ptr &= ~__PERCPU_REF_DEAD;
	percpu_ref_get(ref);
	__percpu_ref_switch_mode(ref, NULL);

	spin_unlock_irqrestore(&percpu_ref_switch_lock, flags);
}
EXPORT_SYMBOL_GPL(percpu_ref_resurrect);
