// SPDX-License-Identifier: GPL-2.0-only
/*
 * Compile-only tests for common patterns that should not generate false
 * positive errors when compiled with Clang's context analysis.
 */

#include <linux/bit_spinlock.h>
#include <linux/build_bug.h>
#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include <linux/seqlock.h>
#include <linux/spinlock.h>
#include <linux/srcu.h>

/*
 * Test that helper macros work as expected.
 */
static void __used test_common_helpers(void)
{
	BUILD_BUG_ON(context_unsafe(3) != 3); /* plain expression */
	BUILD_BUG_ON(context_unsafe((void)2; 3) != 3); /* does not swallow semi-colon */
	BUILD_BUG_ON(context_unsafe((void)2, 3) != 3); /* does not swallow commas */
	context_unsafe(do { } while (0)); /* works with void statements */
}

#define TEST_SPINLOCK_COMMON(class, type, type_init, type_lock, type_unlock, type_trylock, op)	\
	struct test_##class##_data {								\
		type lock;									\
		int counter __guarded_by(&lock);						\
		int *pointer __pt_guarded_by(&lock);						\
	};											\
	static void __used test_##class##_init(struct test_##class##_data *d)			\
	{											\
		type_init(&d->lock);								\
		d->counter = 0;									\
	}											\
	static void __used test_##class(struct test_##class##_data *d)				\
	{											\
		unsigned long flags;								\
		d->pointer++;									\
		type_lock(&d->lock);								\
		op(d->counter);									\
		op(*d->pointer);								\
		type_unlock(&d->lock);								\
		type_lock##_irq(&d->lock);							\
		op(d->counter);									\
		op(*d->pointer);								\
		type_unlock##_irq(&d->lock);							\
		type_lock##_bh(&d->lock);							\
		op(d->counter);									\
		op(*d->pointer);								\
		type_unlock##_bh(&d->lock);							\
		type_lock##_irqsave(&d->lock, flags);						\
		op(d->counter);									\
		op(*d->pointer);								\
		type_unlock##_irqrestore(&d->lock, flags);					\
	}											\
	static void __used test_##class##_trylock(struct test_##class##_data *d)		\
	{											\
		if (type_trylock(&d->lock)) {							\
			op(d->counter);								\
			type_unlock(&d->lock);							\
		}										\
	}											\
	static void __used test_##class##_assert(struct test_##class##_data *d)			\
	{											\
		lockdep_assert_held(&d->lock);							\
		op(d->counter);									\
	}											\
	static void __used test_##class##_guard(struct test_##class##_data *d)			\
	{											\
		{ guard(class)(&d->lock);		op(d->counter); }			\
		{ guard(class##_irq)(&d->lock);		op(d->counter); }			\
		{ guard(class##_irqsave)(&d->lock);	op(d->counter); }			\
	}

#define TEST_OP_RW(x) (x)++
#define TEST_OP_RO(x) ((void)(x))

TEST_SPINLOCK_COMMON(raw_spinlock,
		     raw_spinlock_t,
		     raw_spin_lock_init,
		     raw_spin_lock,
		     raw_spin_unlock,
		     raw_spin_trylock,
		     TEST_OP_RW);
static void __used test_raw_spinlock_trylock_extra(struct test_raw_spinlock_data *d)
{
	unsigned long flags;

	if (raw_spin_trylock_irq(&d->lock)) {
		d->counter++;
		raw_spin_unlock_irq(&d->lock);
	}
	if (raw_spin_trylock_irqsave(&d->lock, flags)) {
		d->counter++;
		raw_spin_unlock_irqrestore(&d->lock, flags);
	}
	scoped_cond_guard(raw_spinlock_try, return, &d->lock) {
		d->counter++;
	}
}

TEST_SPINLOCK_COMMON(spinlock,
		     spinlock_t,
		     spin_lock_init,
		     spin_lock,
		     spin_unlock,
		     spin_trylock,
		     TEST_OP_RW);
static void __used test_spinlock_trylock_extra(struct test_spinlock_data *d)
{
	unsigned long flags;

	if (spin_trylock_irq(&d->lock)) {
		d->counter++;
		spin_unlock_irq(&d->lock);
	}
	if (spin_trylock_irqsave(&d->lock, flags)) {
		d->counter++;
		spin_unlock_irqrestore(&d->lock, flags);
	}
	scoped_cond_guard(spinlock_try, return, &d->lock) {
		d->counter++;
	}
}

TEST_SPINLOCK_COMMON(write_lock,
		     rwlock_t,
		     rwlock_init,
		     write_lock,
		     write_unlock,
		     write_trylock,
		     TEST_OP_RW);
static void __used test_write_trylock_extra(struct test_write_lock_data *d)
{
	unsigned long flags;

	if (write_trylock_irqsave(&d->lock, flags)) {
		d->counter++;
		write_unlock_irqrestore(&d->lock, flags);
	}
}

TEST_SPINLOCK_COMMON(read_lock,
		     rwlock_t,
		     rwlock_init,
		     read_lock,
		     read_unlock,
		     read_trylock,
		     TEST_OP_RO);

struct test_mutex_data {
	struct mutex mtx;
	int counter __guarded_by(&mtx);
};

static void __used test_mutex_init(struct test_mutex_data *d)
{
	mutex_init(&d->mtx);
	d->counter = 0;
}

static void __used test_mutex_lock(struct test_mutex_data *d)
{
	mutex_lock(&d->mtx);
	d->counter++;
	mutex_unlock(&d->mtx);
	mutex_lock_io(&d->mtx);
	d->counter++;
	mutex_unlock(&d->mtx);
}

static void __used test_mutex_trylock(struct test_mutex_data *d, atomic_t *a)
{
	if (!mutex_lock_interruptible(&d->mtx)) {
		d->counter++;
		mutex_unlock(&d->mtx);
	}
	if (!mutex_lock_killable(&d->mtx)) {
		d->counter++;
		mutex_unlock(&d->mtx);
	}
	if (mutex_trylock(&d->mtx)) {
		d->counter++;
		mutex_unlock(&d->mtx);
	}
	if (atomic_dec_and_mutex_lock(a, &d->mtx)) {
		d->counter++;
		mutex_unlock(&d->mtx);
	}
}

static void __used test_mutex_assert(struct test_mutex_data *d)
{
	lockdep_assert_held(&d->mtx);
	d->counter++;
}

static void __used test_mutex_guard(struct test_mutex_data *d)
{
	guard(mutex)(&d->mtx);
	d->counter++;
}

static void __used test_mutex_cond_guard(struct test_mutex_data *d)
{
	scoped_cond_guard(mutex_try, return, &d->mtx) {
		d->counter++;
	}
	scoped_cond_guard(mutex_intr, return, &d->mtx) {
		d->counter++;
	}
}

struct test_seqlock_data {
	seqlock_t sl;
	int counter __guarded_by(&sl);
};

static void __used test_seqlock_init(struct test_seqlock_data *d)
{
	seqlock_init(&d->sl);
	d->counter = 0;
}

static void __used test_seqlock_reader(struct test_seqlock_data *d)
{
	unsigned int seq;

	do {
		seq = read_seqbegin(&d->sl);
		(void)d->counter;
	} while (read_seqretry(&d->sl, seq));
}

static void __used test_seqlock_writer(struct test_seqlock_data *d)
{
	unsigned long flags;

	write_seqlock(&d->sl);
	d->counter++;
	write_sequnlock(&d->sl);

	write_seqlock_irq(&d->sl);
	d->counter++;
	write_sequnlock_irq(&d->sl);

	write_seqlock_bh(&d->sl);
	d->counter++;
	write_sequnlock_bh(&d->sl);

	write_seqlock_irqsave(&d->sl, flags);
	d->counter++;
	write_sequnlock_irqrestore(&d->sl, flags);
}

static void __used test_seqlock_scoped(struct test_seqlock_data *d)
{
	scoped_seqlock_read (&d->sl, ss_lockless) {
		(void)d->counter;
	}
}

struct test_bit_spinlock_data {
	unsigned long bits;
	int counter __guarded_by(__bitlock(3, &bits));
};

static void __used test_bit_spin_lock(struct test_bit_spinlock_data *d)
{
	/*
	 * Note, the analysis seems to have false negatives, because it won't
	 * precisely recognize the bit of the fake __bitlock() token.
	 */
	bit_spin_lock(3, &d->bits);
	d->counter++;
	bit_spin_unlock(3, &d->bits);

	bit_spin_lock(3, &d->bits);
	d->counter++;
	__bit_spin_unlock(3, &d->bits);

	if (bit_spin_trylock(3, &d->bits)) {
		d->counter++;
		bit_spin_unlock(3, &d->bits);
	}
}

/*
 * Test that we can mark a variable guarded by RCU, and we can dereference and
 * write to the pointer with RCU's primitives.
 */
struct test_rcu_data {
	long __rcu_guarded *data;
};

static void __used test_rcu_guarded_reader(struct test_rcu_data *d)
{
	rcu_read_lock();
	(void)rcu_dereference(d->data);
	rcu_read_unlock();

	rcu_read_lock_bh();
	(void)rcu_dereference(d->data);
	rcu_read_unlock_bh();

	rcu_read_lock_sched();
	(void)rcu_dereference(d->data);
	rcu_read_unlock_sched();
}

static void __used test_rcu_guard(struct test_rcu_data *d)
{
	guard(rcu)();
	(void)rcu_dereference(d->data);
}

static void __used test_rcu_guarded_updater(struct test_rcu_data *d)
{
	rcu_assign_pointer(d->data, NULL);
	RCU_INIT_POINTER(d->data, NULL);
	(void)unrcu_pointer(d->data);
}

static void wants_rcu_held(void)	__must_hold_shared(RCU)       { }
static void wants_rcu_held_bh(void)	__must_hold_shared(RCU_BH)    { }
static void wants_rcu_held_sched(void)	__must_hold_shared(RCU_SCHED) { }

static void __used test_rcu_lock_variants(void)
{
	rcu_read_lock();
	wants_rcu_held();
	rcu_read_unlock();

	rcu_read_lock_bh();
	wants_rcu_held_bh();
	rcu_read_unlock_bh();

	rcu_read_lock_sched();
	wants_rcu_held_sched();
	rcu_read_unlock_sched();
}

static void __used test_rcu_lock_reentrant(void)
{
	rcu_read_lock();
	rcu_read_lock();
	rcu_read_lock_bh();
	rcu_read_lock_bh();
	rcu_read_lock_sched();
	rcu_read_lock_sched();

	rcu_read_unlock_sched();
	rcu_read_unlock_sched();
	rcu_read_unlock_bh();
	rcu_read_unlock_bh();
	rcu_read_unlock();
	rcu_read_unlock();
}

static void __used test_rcu_assert_variants(void)
{
	lockdep_assert_in_rcu_read_lock();
	wants_rcu_held();

	lockdep_assert_in_rcu_read_lock_bh();
	wants_rcu_held_bh();

	lockdep_assert_in_rcu_read_lock_sched();
	wants_rcu_held_sched();
}

struct test_srcu_data {
	struct srcu_struct srcu;
	long __rcu_guarded *data;
};

static void __used test_srcu(struct test_srcu_data *d)
{
	init_srcu_struct(&d->srcu);

	int idx = srcu_read_lock(&d->srcu);
	long *data = srcu_dereference(d->data, &d->srcu);
	(void)data;
	srcu_read_unlock(&d->srcu, idx);

	rcu_assign_pointer(d->data, NULL);
}

static void __used test_srcu_guard(struct test_srcu_data *d)
{
	{ guard(srcu)(&d->srcu); (void)srcu_dereference(d->data, &d->srcu); }
	{ guard(srcu_fast)(&d->srcu); (void)srcu_dereference(d->data, &d->srcu); }
	{ guard(srcu_fast_notrace)(&d->srcu); (void)srcu_dereference(d->data, &d->srcu); }
}
