#define pr_fmt(fmt) "%s: " fmt "\n", __func__

#include <linux/kernel.h>
#include <linux/percpu-refcount.h>

/*
 * Initially, a percpu refcount is just a set of percpu counters. Initially, we
 * don't try to detect the ref hitting 0 - which means that get/put can just
 * increment or decrement the local counter. Note that the counter on a
 * particular cpu can (and will) wrap - this is fine, when we go to shutdown the
 * percpu counters will all sum to the correct value
 *
 * (More precisely: because moduler arithmatic is commutative the sum of all the
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

static unsigned long __percpu *percpu_count_ptr(struct percpu_ref *ref)
{
	return (unsigned long __percpu *)
		(ref->percpu_count_ptr & ~__PERCPU_REF_ATOMIC);
}

/**
 * percpu_ref_init - initialize a percpu refcount
 * @ref: percpu_ref to initialize
 * @release: function which will be called when refcount hits 0
 * @gfp: allocation mask to use
 *
 * Initializes the refcount in single atomic counter mode with a refcount of 1;
 * analagous to atomic_long_set(ref, 1).
 *
 * Note that @release must not sleep - it may potentially be called from RCU
 * callback context by percpu_ref_kill().
 */
int percpu_ref_init(struct percpu_ref *ref, percpu_ref_func_t *release,
		    gfp_t gfp)
{
	atomic_long_set(&ref->count, 1 + PERCPU_COUNT_BIAS);

	ref->percpu_count_ptr =
		(unsigned long)alloc_percpu_gfp(unsigned long, gfp);
	if (!ref->percpu_count_ptr)
		return -ENOMEM;

	ref->release = release;
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
		free_percpu(percpu_count);
		ref->percpu_count_ptr = __PERCPU_REF_ATOMIC;
	}
}
EXPORT_SYMBOL_GPL(percpu_ref_exit);

static void percpu_ref_kill_rcu(struct rcu_head *rcu)
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
		  "percpu ref (%pf) <= 0 (%ld) after killed",
		  ref->release, atomic_long_read(&ref->count));

	/* @ref is viewed as dead on all CPUs, send out kill confirmation */
	if (ref->confirm_switch)
		ref->confirm_switch(ref);

	/*
	 * Now we're in single atomic_long_t mode with a consistent
	 * refcount, so it's safe to drop our initial ref:
	 */
	percpu_ref_put(ref);
}

/**
 * percpu_ref_kill_and_confirm - drop the initial ref and schedule confirmation
 * @ref: percpu_ref to kill
 * @confirm_kill: optional confirmation callback
 *
 * Equivalent to percpu_ref_kill() but also schedules kill confirmation if
 * @confirm_kill is not NULL.  @confirm_kill, which may not block, will be
 * called after @ref is seen as dead from all CPUs - all further
 * invocations of percpu_ref_tryget_live() will fail.  See
 * percpu_ref_tryget_live() for more details.
 *
 * Due to the way percpu_ref is implemented, @confirm_kill will be called
 * after at least one full RCU grace period has passed but this is an
 * implementation detail and callers must not depend on it.
 */
void percpu_ref_kill_and_confirm(struct percpu_ref *ref,
				 percpu_ref_func_t *confirm_kill)
{
	WARN_ONCE(ref->percpu_count_ptr & __PERCPU_REF_ATOMIC,
		  "%s called more than once on %pf!", __func__, ref->release);

	ref->percpu_count_ptr |= __PERCPU_REF_ATOMIC;
	ref->confirm_switch = confirm_kill;

	call_rcu_sched(&ref->rcu, percpu_ref_kill_rcu);
}
EXPORT_SYMBOL_GPL(percpu_ref_kill_and_confirm);

/**
 * percpu_ref_reinit - re-initialize a percpu refcount
 * @ref: perpcu_ref to re-initialize
 *
 * Re-initialize @ref so that it's in the same state as when it finished
 * percpu_ref_init().  @ref must have been initialized successfully, killed
 * and reached 0 but not exited.
 *
 * Note that percpu_ref_tryget[_live]() are safe to perform on @ref while
 * this function is in progress.
 */
void percpu_ref_reinit(struct percpu_ref *ref)
{
	unsigned long __percpu *percpu_count = percpu_count_ptr(ref);
	int cpu;

	BUG_ON(!percpu_count);
	WARN_ON_ONCE(!percpu_ref_is_zero(ref));

	atomic_long_set(&ref->count, 1 + PERCPU_COUNT_BIAS);

	/*
	 * Restore per-cpu operation.  smp_store_release() is paired with
	 * smp_read_barrier_depends() in __ref_is_percpu() and guarantees
	 * that the zeroing is visible to all percpu accesses which can see
	 * the following __PERCPU_REF_ATOMIC clearing.
	 */
	for_each_possible_cpu(cpu)
		*per_cpu_ptr(percpu_count, cpu) = 0;

	smp_store_release(&ref->percpu_count_ptr,
			  ref->percpu_count_ptr & ~__PERCPU_REF_ATOMIC);
}
EXPORT_SYMBOL_GPL(percpu_ref_reinit);
