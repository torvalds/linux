// SPDX-License-Identifier: GPL-2.0
/*
 * KCSAN core runtime.
 *
 * Copyright (C) 2019, Google LLC.
 */

#define pr_fmt(fmt) "kcsan: " fmt

#include <linux/atomic.h>
#include <linux/bug.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/moduleparam.h>
#include <linux/percpu.h>
#include <linux/preempt.h>
#include <linux/sched.h>
#include <linux/uaccess.h>

#include "encoding.h"
#include "kcsan.h"
#include "permissive.h"

static bool kcsan_early_enable = IS_ENABLED(CONFIG_KCSAN_EARLY_ENABLE);
unsigned int kcsan_udelay_task = CONFIG_KCSAN_UDELAY_TASK;
unsigned int kcsan_udelay_interrupt = CONFIG_KCSAN_UDELAY_INTERRUPT;
static long kcsan_skip_watch = CONFIG_KCSAN_SKIP_WATCH;
static bool kcsan_interrupt_watcher = IS_ENABLED(CONFIG_KCSAN_INTERRUPT_WATCHER);

#ifdef MODULE_PARAM_PREFIX
#undef MODULE_PARAM_PREFIX
#endif
#define MODULE_PARAM_PREFIX "kcsan."
module_param_named(early_enable, kcsan_early_enable, bool, 0);
module_param_named(udelay_task, kcsan_udelay_task, uint, 0644);
module_param_named(udelay_interrupt, kcsan_udelay_interrupt, uint, 0644);
module_param_named(skip_watch, kcsan_skip_watch, long, 0644);
module_param_named(interrupt_watcher, kcsan_interrupt_watcher, bool, 0444);

bool kcsan_enabled;

/* Per-CPU kcsan_ctx for interrupts */
static DEFINE_PER_CPU(struct kcsan_ctx, kcsan_cpu_ctx) = {
	.disable_count		= 0,
	.atomic_next		= 0,
	.atomic_nest_count	= 0,
	.in_flat_atomic		= false,
	.access_mask		= 0,
	.scoped_accesses	= {LIST_POISON1, NULL},
};

/*
 * Helper macros to index into adjacent slots, starting from address slot
 * itself, followed by the right and left slots.
 *
 * The purpose is 2-fold:
 *
 *	1. if during insertion the address slot is already occupied, check if
 *	   any adjacent slots are free;
 *	2. accesses that straddle a slot boundary due to size that exceeds a
 *	   slot's range may check adjacent slots if any watchpoint matches.
 *
 * Note that accesses with very large size may still miss a watchpoint; however,
 * given this should be rare, this is a reasonable trade-off to make, since this
 * will avoid:
 *
 *	1. excessive contention between watchpoint checks and setup;
 *	2. larger number of simultaneous watchpoints without sacrificing
 *	   performance.
 *
 * Example: SLOT_IDX values for KCSAN_CHECK_ADJACENT=1, where i is [0, 1, 2]:
 *
 *   slot=0:  [ 1,  2,  0]
 *   slot=9:  [10, 11,  9]
 *   slot=63: [64, 65, 63]
 */
#define SLOT_IDX(slot, i) (slot + ((i + KCSAN_CHECK_ADJACENT) % NUM_SLOTS))

/*
 * SLOT_IDX_FAST is used in the fast-path. Not first checking the address's primary
 * slot (middle) is fine if we assume that races occur rarely. The set of
 * indices {SLOT_IDX(slot, i) | i in [0, NUM_SLOTS)} is equivalent to
 * {SLOT_IDX_FAST(slot, i) | i in [0, NUM_SLOTS)}.
 */
#define SLOT_IDX_FAST(slot, i) (slot + i)

/*
 * Watchpoints, with each entry encoded as defined in encoding.h: in order to be
 * able to safely update and access a watchpoint without introducing locking
 * overhead, we encode each watchpoint as a single atomic long. The initial
 * zero-initialized state matches INVALID_WATCHPOINT.
 *
 * Add NUM_SLOTS-1 entries to account for overflow; this helps avoid having to
 * use more complicated SLOT_IDX_FAST calculation with modulo in the fast-path.
 */
static atomic_long_t watchpoints[CONFIG_KCSAN_NUM_WATCHPOINTS + NUM_SLOTS-1];

/*
 * Instructions to skip watching counter, used in should_watch(). We use a
 * per-CPU counter to avoid excessive contention.
 */
static DEFINE_PER_CPU(long, kcsan_skip);

/* For kcsan_prandom_u32_max(). */
static DEFINE_PER_CPU(u32, kcsan_rand_state);

static __always_inline atomic_long_t *find_watchpoint(unsigned long addr,
						      size_t size,
						      bool expect_write,
						      long *encoded_watchpoint)
{
	const int slot = watchpoint_slot(addr);
	const unsigned long addr_masked = addr & WATCHPOINT_ADDR_MASK;
	atomic_long_t *watchpoint;
	unsigned long wp_addr_masked;
	size_t wp_size;
	bool is_write;
	int i;

	BUILD_BUG_ON(CONFIG_KCSAN_NUM_WATCHPOINTS < NUM_SLOTS);

	for (i = 0; i < NUM_SLOTS; ++i) {
		watchpoint = &watchpoints[SLOT_IDX_FAST(slot, i)];
		*encoded_watchpoint = atomic_long_read(watchpoint);
		if (!decode_watchpoint(*encoded_watchpoint, &wp_addr_masked,
				       &wp_size, &is_write))
			continue;

		if (expect_write && !is_write)
			continue;

		/* Check if the watchpoint matches the access. */
		if (matching_access(wp_addr_masked, wp_size, addr_masked, size))
			return watchpoint;
	}

	return NULL;
}

static inline atomic_long_t *
insert_watchpoint(unsigned long addr, size_t size, bool is_write)
{
	const int slot = watchpoint_slot(addr);
	const long encoded_watchpoint = encode_watchpoint(addr, size, is_write);
	atomic_long_t *watchpoint;
	int i;

	/* Check slot index logic, ensuring we stay within array bounds. */
	BUILD_BUG_ON(SLOT_IDX(0, 0) != KCSAN_CHECK_ADJACENT);
	BUILD_BUG_ON(SLOT_IDX(0, KCSAN_CHECK_ADJACENT+1) != 0);
	BUILD_BUG_ON(SLOT_IDX(CONFIG_KCSAN_NUM_WATCHPOINTS-1, KCSAN_CHECK_ADJACENT) != ARRAY_SIZE(watchpoints)-1);
	BUILD_BUG_ON(SLOT_IDX(CONFIG_KCSAN_NUM_WATCHPOINTS-1, KCSAN_CHECK_ADJACENT+1) != ARRAY_SIZE(watchpoints) - NUM_SLOTS);

	for (i = 0; i < NUM_SLOTS; ++i) {
		long expect_val = INVALID_WATCHPOINT;

		/* Try to acquire this slot. */
		watchpoint = &watchpoints[SLOT_IDX(slot, i)];
		if (atomic_long_try_cmpxchg_relaxed(watchpoint, &expect_val, encoded_watchpoint))
			return watchpoint;
	}

	return NULL;
}

/*
 * Return true if watchpoint was successfully consumed, false otherwise.
 *
 * This may return false if:
 *
 *	1. another thread already consumed the watchpoint;
 *	2. the thread that set up the watchpoint already removed it;
 *	3. the watchpoint was removed and then re-used.
 */
static __always_inline bool
try_consume_watchpoint(atomic_long_t *watchpoint, long encoded_watchpoint)
{
	return atomic_long_try_cmpxchg_relaxed(watchpoint, &encoded_watchpoint, CONSUMED_WATCHPOINT);
}

/* Return true if watchpoint was not touched, false if already consumed. */
static inline bool consume_watchpoint(atomic_long_t *watchpoint)
{
	return atomic_long_xchg_relaxed(watchpoint, CONSUMED_WATCHPOINT) != CONSUMED_WATCHPOINT;
}

/* Remove the watchpoint -- its slot may be reused after. */
static inline void remove_watchpoint(atomic_long_t *watchpoint)
{
	atomic_long_set(watchpoint, INVALID_WATCHPOINT);
}

static __always_inline struct kcsan_ctx *get_ctx(void)
{
	/*
	 * In interrupts, use raw_cpu_ptr to avoid unnecessary checks, that would
	 * also result in calls that generate warnings in uaccess regions.
	 */
	return in_task() ? &current->kcsan_ctx : raw_cpu_ptr(&kcsan_cpu_ctx);
}

static __always_inline void
check_access(const volatile void *ptr, size_t size, int type, unsigned long ip);

/* Check scoped accesses; never inline because this is a slow-path! */
static noinline void kcsan_check_scoped_accesses(void)
{
	struct kcsan_ctx *ctx = get_ctx();
	struct list_head *prev_save = ctx->scoped_accesses.prev;
	struct kcsan_scoped_access *scoped_access;

	ctx->scoped_accesses.prev = NULL;  /* Avoid recursion. */
	list_for_each_entry(scoped_access, &ctx->scoped_accesses, list) {
		check_access(scoped_access->ptr, scoped_access->size,
			     scoped_access->type, scoped_access->ip);
	}
	ctx->scoped_accesses.prev = prev_save;
}

/* Rules for generic atomic accesses. Called from fast-path. */
static __always_inline bool
is_atomic(const volatile void *ptr, size_t size, int type, struct kcsan_ctx *ctx)
{
	if (type & KCSAN_ACCESS_ATOMIC)
		return true;

	/*
	 * Unless explicitly declared atomic, never consider an assertion access
	 * as atomic. This allows using them also in atomic regions, such as
	 * seqlocks, without implicitly changing their semantics.
	 */
	if (type & KCSAN_ACCESS_ASSERT)
		return false;

	if (IS_ENABLED(CONFIG_KCSAN_ASSUME_PLAIN_WRITES_ATOMIC) &&
	    (type & KCSAN_ACCESS_WRITE) && size <= sizeof(long) &&
	    !(type & KCSAN_ACCESS_COMPOUND) && IS_ALIGNED((unsigned long)ptr, size))
		return true; /* Assume aligned writes up to word size are atomic. */

	if (ctx->atomic_next > 0) {
		/*
		 * Because we do not have separate contexts for nested
		 * interrupts, in case atomic_next is set, we simply assume that
		 * the outer interrupt set atomic_next. In the worst case, we
		 * will conservatively consider operations as atomic. This is a
		 * reasonable trade-off to make, since this case should be
		 * extremely rare; however, even if extremely rare, it could
		 * lead to false positives otherwise.
		 */
		if ((hardirq_count() >> HARDIRQ_SHIFT) < 2)
			--ctx->atomic_next; /* in task, or outer interrupt */
		return true;
	}

	return ctx->atomic_nest_count > 0 || ctx->in_flat_atomic;
}

static __always_inline bool
should_watch(const volatile void *ptr, size_t size, int type, struct kcsan_ctx *ctx)
{
	/*
	 * Never set up watchpoints when memory operations are atomic.
	 *
	 * Need to check this first, before kcsan_skip check below: (1) atomics
	 * should not count towards skipped instructions, and (2) to actually
	 * decrement kcsan_atomic_next for consecutive instruction stream.
	 */
	if (is_atomic(ptr, size, type, ctx))
		return false;

	if (this_cpu_dec_return(kcsan_skip) >= 0)
		return false;

	/*
	 * NOTE: If we get here, kcsan_skip must always be reset in slow path
	 * via reset_kcsan_skip() to avoid underflow.
	 */

	/* this operation should be watched */
	return true;
}

/*
 * Returns a pseudo-random number in interval [0, ep_ro). Simple linear
 * congruential generator, using constants from "Numerical Recipes".
 */
static u32 kcsan_prandom_u32_max(u32 ep_ro)
{
	u32 state = this_cpu_read(kcsan_rand_state);

	state = 1664525 * state + 1013904223;
	this_cpu_write(kcsan_rand_state, state);

	return state % ep_ro;
}

static inline void reset_kcsan_skip(void)
{
	long skip_count = kcsan_skip_watch -
			  (IS_ENABLED(CONFIG_KCSAN_SKIP_WATCH_RANDOMIZE) ?
				   kcsan_prandom_u32_max(kcsan_skip_watch) :
				   0);
	this_cpu_write(kcsan_skip, skip_count);
}

static __always_inline bool kcsan_is_enabled(struct kcsan_ctx *ctx)
{
	return READ_ONCE(kcsan_enabled) && !ctx->disable_count;
}

/* Introduce delay depending on context and configuration. */
static void delay_access(int type)
{
	unsigned int delay = in_task() ? kcsan_udelay_task : kcsan_udelay_interrupt;
	/* For certain access types, skew the random delay to be longer. */
	unsigned int skew_delay_order =
		(type & (KCSAN_ACCESS_COMPOUND | KCSAN_ACCESS_ASSERT)) ? 1 : 0;

	delay -= IS_ENABLED(CONFIG_KCSAN_DELAY_RANDOMIZE) ?
			       kcsan_prandom_u32_max(delay >> skew_delay_order) :
			       0;
	udelay(delay);
}

void kcsan_save_irqtrace(struct task_struct *task)
{
#ifdef CONFIG_TRACE_IRQFLAGS
	task->kcsan_save_irqtrace = task->irqtrace;
#endif
}

void kcsan_restore_irqtrace(struct task_struct *task)
{
#ifdef CONFIG_TRACE_IRQFLAGS
	task->irqtrace = task->kcsan_save_irqtrace;
#endif
}

/*
 * Pull everything together: check_access() below contains the performance
 * critical operations; the fast-path (including check_access) functions should
 * all be inlinable by the instrumentation functions.
 *
 * The slow-path (kcsan_found_watchpoint, kcsan_setup_watchpoint) are
 * non-inlinable -- note that, we prefix these with "kcsan_" to ensure they can
 * be filtered from the stacktrace, as well as give them unique names for the
 * UACCESS whitelist of objtool. Each function uses user_access_save/restore(),
 * since they do not access any user memory, but instrumentation is still
 * emitted in UACCESS regions.
 */

static noinline void kcsan_found_watchpoint(const volatile void *ptr,
					    size_t size,
					    int type,
					    unsigned long ip,
					    atomic_long_t *watchpoint,
					    long encoded_watchpoint)
{
	const bool is_assert = (type & KCSAN_ACCESS_ASSERT) != 0;
	struct kcsan_ctx *ctx = get_ctx();
	unsigned long flags;
	bool consumed;

	/*
	 * We know a watchpoint exists. Let's try to keep the race-window
	 * between here and finally consuming the watchpoint below as small as
	 * possible -- avoid unneccessarily complex code until consumed.
	 */

	if (!kcsan_is_enabled(ctx))
		return;

	/*
	 * The access_mask check relies on value-change comparison. To avoid
	 * reporting a race where e.g. the writer set up the watchpoint, but the
	 * reader has access_mask!=0, we have to ignore the found watchpoint.
	 */
	if (ctx->access_mask)
		return;

	/*
	 * If the other thread does not want to ignore the access, and there was
	 * a value change as a result of this thread's operation, we will still
	 * generate a report of unknown origin.
	 *
	 * Use CONFIG_KCSAN_REPORT_RACE_UNKNOWN_ORIGIN=n to filter.
	 */
	if (!is_assert && kcsan_ignore_address(ptr))
		return;

	/*
	 * Consuming the watchpoint must be guarded by kcsan_is_enabled() to
	 * avoid erroneously triggering reports if the context is disabled.
	 */
	consumed = try_consume_watchpoint(watchpoint, encoded_watchpoint);

	/* keep this after try_consume_watchpoint */
	flags = user_access_save();

	if (consumed) {
		kcsan_save_irqtrace(current);
		kcsan_report_set_info(ptr, size, type, ip, watchpoint - watchpoints);
		kcsan_restore_irqtrace(current);
	} else {
		/*
		 * The other thread may not print any diagnostics, as it has
		 * already removed the watchpoint, or another thread consumed
		 * the watchpoint before this thread.
		 */
		atomic_long_inc(&kcsan_counters[KCSAN_COUNTER_REPORT_RACES]);
	}

	if (is_assert)
		atomic_long_inc(&kcsan_counters[KCSAN_COUNTER_ASSERT_FAILURES]);
	else
		atomic_long_inc(&kcsan_counters[KCSAN_COUNTER_DATA_RACES]);

	user_access_restore(flags);
}

static noinline void
kcsan_setup_watchpoint(const volatile void *ptr, size_t size, int type, unsigned long ip)
{
	const bool is_write = (type & KCSAN_ACCESS_WRITE) != 0;
	const bool is_assert = (type & KCSAN_ACCESS_ASSERT) != 0;
	atomic_long_t *watchpoint;
	u64 old, new, diff;
	unsigned long access_mask;
	enum kcsan_value_change value_change = KCSAN_VALUE_CHANGE_MAYBE;
	unsigned long ua_flags = user_access_save();
	struct kcsan_ctx *ctx = get_ctx();
	unsigned long irq_flags = 0;

	/*
	 * Always reset kcsan_skip counter in slow-path to avoid underflow; see
	 * should_watch().
	 */
	reset_kcsan_skip();

	if (!kcsan_is_enabled(ctx))
		goto out;

	/*
	 * Check to-ignore addresses after kcsan_is_enabled(), as we may access
	 * memory that is not yet initialized during early boot.
	 */
	if (!is_assert && kcsan_ignore_address(ptr))
		goto out;

	if (!check_encodable((unsigned long)ptr, size)) {
		atomic_long_inc(&kcsan_counters[KCSAN_COUNTER_UNENCODABLE_ACCESSES]);
		goto out;
	}

	/*
	 * Save and restore the IRQ state trace touched by KCSAN, since KCSAN's
	 * runtime is entered for every memory access, and potentially useful
	 * information is lost if dirtied by KCSAN.
	 */
	kcsan_save_irqtrace(current);
	if (!kcsan_interrupt_watcher)
		local_irq_save(irq_flags);

	watchpoint = insert_watchpoint((unsigned long)ptr, size, is_write);
	if (watchpoint == NULL) {
		/*
		 * Out of capacity: the size of 'watchpoints', and the frequency
		 * with which should_watch() returns true should be tweaked so
		 * that this case happens very rarely.
		 */
		atomic_long_inc(&kcsan_counters[KCSAN_COUNTER_NO_CAPACITY]);
		goto out_unlock;
	}

	atomic_long_inc(&kcsan_counters[KCSAN_COUNTER_SETUP_WATCHPOINTS]);
	atomic_long_inc(&kcsan_counters[KCSAN_COUNTER_USED_WATCHPOINTS]);

	/*
	 * Read the current value, to later check and infer a race if the data
	 * was modified via a non-instrumented access, e.g. from a device.
	 */
	old = 0;
	switch (size) {
	case 1:
		old = READ_ONCE(*(const u8 *)ptr);
		break;
	case 2:
		old = READ_ONCE(*(const u16 *)ptr);
		break;
	case 4:
		old = READ_ONCE(*(const u32 *)ptr);
		break;
	case 8:
		old = READ_ONCE(*(const u64 *)ptr);
		break;
	default:
		break; /* ignore; we do not diff the values */
	}

	/*
	 * Delay this thread, to increase probability of observing a racy
	 * conflicting access.
	 */
	delay_access(type);

	/*
	 * Re-read value, and check if it is as expected; if not, we infer a
	 * racy access.
	 */
	access_mask = ctx->access_mask;
	new = 0;
	switch (size) {
	case 1:
		new = READ_ONCE(*(const u8 *)ptr);
		break;
	case 2:
		new = READ_ONCE(*(const u16 *)ptr);
		break;
	case 4:
		new = READ_ONCE(*(const u32 *)ptr);
		break;
	case 8:
		new = READ_ONCE(*(const u64 *)ptr);
		break;
	default:
		break; /* ignore; we do not diff the values */
	}

	diff = old ^ new;
	if (access_mask)
		diff &= access_mask;

	/*
	 * Check if we observed a value change.
	 *
	 * Also check if the data race should be ignored (the rules depend on
	 * non-zero diff); if it is to be ignored, the below rules for
	 * KCSAN_VALUE_CHANGE_MAYBE apply.
	 */
	if (diff && !kcsan_ignore_data_race(size, type, old, new, diff))
		value_change = KCSAN_VALUE_CHANGE_TRUE;

	/* Check if this access raced with another. */
	if (!consume_watchpoint(watchpoint)) {
		/*
		 * Depending on the access type, map a value_change of MAYBE to
		 * TRUE (always report) or FALSE (never report).
		 */
		if (value_change == KCSAN_VALUE_CHANGE_MAYBE) {
			if (access_mask != 0) {
				/*
				 * For access with access_mask, we require a
				 * value-change, as it is likely that races on
				 * ~access_mask bits are expected.
				 */
				value_change = KCSAN_VALUE_CHANGE_FALSE;
			} else if (size > 8 || is_assert) {
				/* Always assume a value-change. */
				value_change = KCSAN_VALUE_CHANGE_TRUE;
			}
		}

		/*
		 * No need to increment 'data_races' counter, as the racing
		 * thread already did.
		 *
		 * Count 'assert_failures' for each failed ASSERT access,
		 * therefore both this thread and the racing thread may
		 * increment this counter.
		 */
		if (is_assert && value_change == KCSAN_VALUE_CHANGE_TRUE)
			atomic_long_inc(&kcsan_counters[KCSAN_COUNTER_ASSERT_FAILURES]);

		kcsan_report_known_origin(ptr, size, type, ip,
					  value_change, watchpoint - watchpoints,
					  old, new, access_mask);
	} else if (value_change == KCSAN_VALUE_CHANGE_TRUE) {
		/* Inferring a race, since the value should not have changed. */

		atomic_long_inc(&kcsan_counters[KCSAN_COUNTER_RACES_UNKNOWN_ORIGIN]);
		if (is_assert)
			atomic_long_inc(&kcsan_counters[KCSAN_COUNTER_ASSERT_FAILURES]);

		if (IS_ENABLED(CONFIG_KCSAN_REPORT_RACE_UNKNOWN_ORIGIN) || is_assert) {
			kcsan_report_unknown_origin(ptr, size, type, ip,
						    old, new, access_mask);
		}
	}

	/*
	 * Remove watchpoint; must be after reporting, since the slot may be
	 * reused after this point.
	 */
	remove_watchpoint(watchpoint);
	atomic_long_dec(&kcsan_counters[KCSAN_COUNTER_USED_WATCHPOINTS]);
out_unlock:
	if (!kcsan_interrupt_watcher)
		local_irq_restore(irq_flags);
	kcsan_restore_irqtrace(current);
out:
	user_access_restore(ua_flags);
}

static __always_inline void
check_access(const volatile void *ptr, size_t size, int type, unsigned long ip)
{
	const bool is_write = (type & KCSAN_ACCESS_WRITE) != 0;
	atomic_long_t *watchpoint;
	long encoded_watchpoint;

	/*
	 * Do nothing for 0 sized check; this comparison will be optimized out
	 * for constant sized instrumentation (__tsan_{read,write}N).
	 */
	if (unlikely(size == 0))
		return;

	/*
	 * Avoid user_access_save in fast-path: find_watchpoint is safe without
	 * user_access_save, as the address that ptr points to is only used to
	 * check if a watchpoint exists; ptr is never dereferenced.
	 */
	watchpoint = find_watchpoint((unsigned long)ptr, size, !is_write,
				     &encoded_watchpoint);
	/*
	 * It is safe to check kcsan_is_enabled() after find_watchpoint in the
	 * slow-path, as long as no state changes that cause a race to be
	 * detected and reported have occurred until kcsan_is_enabled() is
	 * checked.
	 */

	if (unlikely(watchpoint != NULL))
		kcsan_found_watchpoint(ptr, size, type, ip, watchpoint, encoded_watchpoint);
	else {
		struct kcsan_ctx *ctx = get_ctx(); /* Call only once in fast-path. */

		if (unlikely(should_watch(ptr, size, type, ctx)))
			kcsan_setup_watchpoint(ptr, size, type, ip);
		else if (unlikely(ctx->scoped_accesses.prev))
			kcsan_check_scoped_accesses();
	}
}

/* === Public interface ===================================================== */

void __init kcsan_init(void)
{
	int cpu;

	BUG_ON(!in_task());

	for_each_possible_cpu(cpu)
		per_cpu(kcsan_rand_state, cpu) = (u32)get_cycles();

	/*
	 * We are in the init task, and no other tasks should be running;
	 * WRITE_ONCE without memory barrier is sufficient.
	 */
	if (kcsan_early_enable) {
		pr_info("enabled early\n");
		WRITE_ONCE(kcsan_enabled, true);
	}

	if (IS_ENABLED(CONFIG_KCSAN_REPORT_VALUE_CHANGE_ONLY) ||
	    IS_ENABLED(CONFIG_KCSAN_ASSUME_PLAIN_WRITES_ATOMIC) ||
	    IS_ENABLED(CONFIG_KCSAN_PERMISSIVE) ||
	    IS_ENABLED(CONFIG_KCSAN_IGNORE_ATOMICS)) {
		pr_warn("non-strict mode configured - use CONFIG_KCSAN_STRICT=y to see all data races\n");
	} else {
		pr_info("strict mode configured\n");
	}
}

/* === Exported interface =================================================== */

void kcsan_disable_current(void)
{
	++get_ctx()->disable_count;
}
EXPORT_SYMBOL(kcsan_disable_current);

void kcsan_enable_current(void)
{
	if (get_ctx()->disable_count-- == 0) {
		/*
		 * Warn if kcsan_enable_current() calls are unbalanced with
		 * kcsan_disable_current() calls, which causes disable_count to
		 * become negative and should not happen.
		 */
		kcsan_disable_current(); /* restore to 0, KCSAN still enabled */
		kcsan_disable_current(); /* disable to generate warning */
		WARN(1, "Unbalanced %s()", __func__);
		kcsan_enable_current();
	}
}
EXPORT_SYMBOL(kcsan_enable_current);

void kcsan_enable_current_nowarn(void)
{
	if (get_ctx()->disable_count-- == 0)
		kcsan_disable_current();
}
EXPORT_SYMBOL(kcsan_enable_current_nowarn);

void kcsan_nestable_atomic_begin(void)
{
	/*
	 * Do *not* check and warn if we are in a flat atomic region: nestable
	 * and flat atomic regions are independent from each other.
	 * See include/linux/kcsan.h: struct kcsan_ctx comments for more
	 * comments.
	 */

	++get_ctx()->atomic_nest_count;
}
EXPORT_SYMBOL(kcsan_nestable_atomic_begin);

void kcsan_nestable_atomic_end(void)
{
	if (get_ctx()->atomic_nest_count-- == 0) {
		/*
		 * Warn if kcsan_nestable_atomic_end() calls are unbalanced with
		 * kcsan_nestable_atomic_begin() calls, which causes
		 * atomic_nest_count to become negative and should not happen.
		 */
		kcsan_nestable_atomic_begin(); /* restore to 0 */
		kcsan_disable_current(); /* disable to generate warning */
		WARN(1, "Unbalanced %s()", __func__);
		kcsan_enable_current();
	}
}
EXPORT_SYMBOL(kcsan_nestable_atomic_end);

void kcsan_flat_atomic_begin(void)
{
	get_ctx()->in_flat_atomic = true;
}
EXPORT_SYMBOL(kcsan_flat_atomic_begin);

void kcsan_flat_atomic_end(void)
{
	get_ctx()->in_flat_atomic = false;
}
EXPORT_SYMBOL(kcsan_flat_atomic_end);

void kcsan_atomic_next(int n)
{
	get_ctx()->atomic_next = n;
}
EXPORT_SYMBOL(kcsan_atomic_next);

void kcsan_set_access_mask(unsigned long mask)
{
	get_ctx()->access_mask = mask;
}
EXPORT_SYMBOL(kcsan_set_access_mask);

struct kcsan_scoped_access *
kcsan_begin_scoped_access(const volatile void *ptr, size_t size, int type,
			  struct kcsan_scoped_access *sa)
{
	struct kcsan_ctx *ctx = get_ctx();

	check_access(ptr, size, type, _RET_IP_);

	ctx->disable_count++; /* Disable KCSAN, in case list debugging is on. */

	INIT_LIST_HEAD(&sa->list);
	sa->ptr = ptr;
	sa->size = size;
	sa->type = type;
	sa->ip = _RET_IP_;

	if (!ctx->scoped_accesses.prev) /* Lazy initialize list head. */
		INIT_LIST_HEAD(&ctx->scoped_accesses);
	list_add(&sa->list, &ctx->scoped_accesses);

	ctx->disable_count--;
	return sa;
}
EXPORT_SYMBOL(kcsan_begin_scoped_access);

void kcsan_end_scoped_access(struct kcsan_scoped_access *sa)
{
	struct kcsan_ctx *ctx = get_ctx();

	if (WARN(!ctx->scoped_accesses.prev, "Unbalanced %s()?", __func__))
		return;

	ctx->disable_count++; /* Disable KCSAN, in case list debugging is on. */

	list_del(&sa->list);
	if (list_empty(&ctx->scoped_accesses))
		/*
		 * Ensure we do not enter kcsan_check_scoped_accesses()
		 * slow-path if unnecessary, and avoids requiring list_empty()
		 * in the fast-path (to avoid a READ_ONCE() and potential
		 * uaccess warning).
		 */
		ctx->scoped_accesses.prev = NULL;

	ctx->disable_count--;

	check_access(sa->ptr, sa->size, sa->type, sa->ip);
}
EXPORT_SYMBOL(kcsan_end_scoped_access);

void __kcsan_check_access(const volatile void *ptr, size_t size, int type)
{
	check_access(ptr, size, type, _RET_IP_);
}
EXPORT_SYMBOL(__kcsan_check_access);

/*
 * KCSAN uses the same instrumentation that is emitted by supported compilers
 * for ThreadSanitizer (TSAN).
 *
 * When enabled, the compiler emits instrumentation calls (the functions
 * prefixed with "__tsan" below) for all loads and stores that it generated;
 * inline asm is not instrumented.
 *
 * Note that, not all supported compiler versions distinguish aligned/unaligned
 * accesses, but e.g. recent versions of Clang do. We simply alias the unaligned
 * version to the generic version, which can handle both.
 */

#define DEFINE_TSAN_READ_WRITE(size)                                           \
	void __tsan_read##size(void *ptr);                                     \
	void __tsan_read##size(void *ptr)                                      \
	{                                                                      \
		check_access(ptr, size, 0, _RET_IP_);                          \
	}                                                                      \
	EXPORT_SYMBOL(__tsan_read##size);                                      \
	void __tsan_unaligned_read##size(void *ptr)                            \
		__alias(__tsan_read##size);                                    \
	EXPORT_SYMBOL(__tsan_unaligned_read##size);                            \
	void __tsan_write##size(void *ptr);                                    \
	void __tsan_write##size(void *ptr)                                     \
	{                                                                      \
		check_access(ptr, size, KCSAN_ACCESS_WRITE, _RET_IP_);         \
	}                                                                      \
	EXPORT_SYMBOL(__tsan_write##size);                                     \
	void __tsan_unaligned_write##size(void *ptr)                           \
		__alias(__tsan_write##size);                                   \
	EXPORT_SYMBOL(__tsan_unaligned_write##size);                           \
	void __tsan_read_write##size(void *ptr);                               \
	void __tsan_read_write##size(void *ptr)                                \
	{                                                                      \
		check_access(ptr, size,                                        \
			     KCSAN_ACCESS_COMPOUND | KCSAN_ACCESS_WRITE,       \
			     _RET_IP_);                                        \
	}                                                                      \
	EXPORT_SYMBOL(__tsan_read_write##size);                                \
	void __tsan_unaligned_read_write##size(void *ptr)                      \
		__alias(__tsan_read_write##size);                              \
	EXPORT_SYMBOL(__tsan_unaligned_read_write##size)

DEFINE_TSAN_READ_WRITE(1);
DEFINE_TSAN_READ_WRITE(2);
DEFINE_TSAN_READ_WRITE(4);
DEFINE_TSAN_READ_WRITE(8);
DEFINE_TSAN_READ_WRITE(16);

void __tsan_read_range(void *ptr, size_t size);
void __tsan_read_range(void *ptr, size_t size)
{
	check_access(ptr, size, 0, _RET_IP_);
}
EXPORT_SYMBOL(__tsan_read_range);

void __tsan_write_range(void *ptr, size_t size);
void __tsan_write_range(void *ptr, size_t size)
{
	check_access(ptr, size, KCSAN_ACCESS_WRITE, _RET_IP_);
}
EXPORT_SYMBOL(__tsan_write_range);

/*
 * Use of explicit volatile is generally disallowed [1], however, volatile is
 * still used in various concurrent context, whether in low-level
 * synchronization primitives or for legacy reasons.
 * [1] https://lwn.net/Articles/233479/
 *
 * We only consider volatile accesses atomic if they are aligned and would pass
 * the size-check of compiletime_assert_rwonce_type().
 */
#define DEFINE_TSAN_VOLATILE_READ_WRITE(size)                                  \
	void __tsan_volatile_read##size(void *ptr);                            \
	void __tsan_volatile_read##size(void *ptr)                             \
	{                                                                      \
		const bool is_atomic = size <= sizeof(long long) &&            \
				       IS_ALIGNED((unsigned long)ptr, size);   \
		if (IS_ENABLED(CONFIG_KCSAN_IGNORE_ATOMICS) && is_atomic)      \
			return;                                                \
		check_access(ptr, size, is_atomic ? KCSAN_ACCESS_ATOMIC : 0,   \
			     _RET_IP_);                                        \
	}                                                                      \
	EXPORT_SYMBOL(__tsan_volatile_read##size);                             \
	void __tsan_unaligned_volatile_read##size(void *ptr)                   \
		__alias(__tsan_volatile_read##size);                           \
	EXPORT_SYMBOL(__tsan_unaligned_volatile_read##size);                   \
	void __tsan_volatile_write##size(void *ptr);                           \
	void __tsan_volatile_write##size(void *ptr)                            \
	{                                                                      \
		const bool is_atomic = size <= sizeof(long long) &&            \
				       IS_ALIGNED((unsigned long)ptr, size);   \
		if (IS_ENABLED(CONFIG_KCSAN_IGNORE_ATOMICS) && is_atomic)      \
			return;                                                \
		check_access(ptr, size,                                        \
			     KCSAN_ACCESS_WRITE |                              \
				     (is_atomic ? KCSAN_ACCESS_ATOMIC : 0),    \
			     _RET_IP_);                                        \
	}                                                                      \
	EXPORT_SYMBOL(__tsan_volatile_write##size);                            \
	void __tsan_unaligned_volatile_write##size(void *ptr)                  \
		__alias(__tsan_volatile_write##size);                          \
	EXPORT_SYMBOL(__tsan_unaligned_volatile_write##size)

DEFINE_TSAN_VOLATILE_READ_WRITE(1);
DEFINE_TSAN_VOLATILE_READ_WRITE(2);
DEFINE_TSAN_VOLATILE_READ_WRITE(4);
DEFINE_TSAN_VOLATILE_READ_WRITE(8);
DEFINE_TSAN_VOLATILE_READ_WRITE(16);

/*
 * The below are not required by KCSAN, but can still be emitted by the
 * compiler.
 */
void __tsan_func_entry(void *call_pc);
void __tsan_func_entry(void *call_pc)
{
}
EXPORT_SYMBOL(__tsan_func_entry);
void __tsan_func_exit(void);
void __tsan_func_exit(void)
{
}
EXPORT_SYMBOL(__tsan_func_exit);
void __tsan_init(void);
void __tsan_init(void)
{
}
EXPORT_SYMBOL(__tsan_init);

/*
 * Instrumentation for atomic builtins (__atomic_*, __sync_*).
 *
 * Normal kernel code _should not_ be using them directly, but some
 * architectures may implement some or all atomics using the compilers'
 * builtins.
 *
 * Note: If an architecture decides to fully implement atomics using the
 * builtins, because they are implicitly instrumented by KCSAN (and KASAN,
 * etc.), implementing the ARCH_ATOMIC interface (to get instrumentation via
 * atomic-instrumented) is no longer necessary.
 *
 * TSAN instrumentation replaces atomic accesses with calls to any of the below
 * functions, whose job is to also execute the operation itself.
 */

#define DEFINE_TSAN_ATOMIC_LOAD_STORE(bits)                                                        \
	u##bits __tsan_atomic##bits##_load(const u##bits *ptr, int memorder);                      \
	u##bits __tsan_atomic##bits##_load(const u##bits *ptr, int memorder)                       \
	{                                                                                          \
		if (!IS_ENABLED(CONFIG_KCSAN_IGNORE_ATOMICS)) {                                    \
			check_access(ptr, bits / BITS_PER_BYTE, KCSAN_ACCESS_ATOMIC, _RET_IP_);    \
		}                                                                                  \
		return __atomic_load_n(ptr, memorder);                                             \
	}                                                                                          \
	EXPORT_SYMBOL(__tsan_atomic##bits##_load);                                                 \
	void __tsan_atomic##bits##_store(u##bits *ptr, u##bits v, int memorder);                   \
	void __tsan_atomic##bits##_store(u##bits *ptr, u##bits v, int memorder)                    \
	{                                                                                          \
		if (!IS_ENABLED(CONFIG_KCSAN_IGNORE_ATOMICS)) {                                    \
			check_access(ptr, bits / BITS_PER_BYTE,                                    \
				     KCSAN_ACCESS_WRITE | KCSAN_ACCESS_ATOMIC, _RET_IP_);          \
		}                                                                                  \
		__atomic_store_n(ptr, v, memorder);                                                \
	}                                                                                          \
	EXPORT_SYMBOL(__tsan_atomic##bits##_store)

#define DEFINE_TSAN_ATOMIC_RMW(op, bits, suffix)                                                   \
	u##bits __tsan_atomic##bits##_##op(u##bits *ptr, u##bits v, int memorder);                 \
	u##bits __tsan_atomic##bits##_##op(u##bits *ptr, u##bits v, int memorder)                  \
	{                                                                                          \
		if (!IS_ENABLED(CONFIG_KCSAN_IGNORE_ATOMICS)) {                                    \
			check_access(ptr, bits / BITS_PER_BYTE,                                    \
				     KCSAN_ACCESS_COMPOUND | KCSAN_ACCESS_WRITE |                  \
					     KCSAN_ACCESS_ATOMIC, _RET_IP_);                       \
		}                                                                                  \
		return __atomic_##op##suffix(ptr, v, memorder);                                    \
	}                                                                                          \
	EXPORT_SYMBOL(__tsan_atomic##bits##_##op)

/*
 * Note: CAS operations are always classified as write, even in case they
 * fail. We cannot perform check_access() after a write, as it might lead to
 * false positives, in cases such as:
 *
 *	T0: __atomic_compare_exchange_n(&p->flag, &old, 1, ...)
 *
 *	T1: if (__atomic_load_n(&p->flag, ...)) {
 *		modify *p;
 *		p->flag = 0;
 *	    }
 *
 * The only downside is that, if there are 3 threads, with one CAS that
 * succeeds, another CAS that fails, and an unmarked racing operation, we may
 * point at the wrong CAS as the source of the race. However, if we assume that
 * all CAS can succeed in some other execution, the data race is still valid.
 */
#define DEFINE_TSAN_ATOMIC_CMPXCHG(bits, strength, weak)                                           \
	int __tsan_atomic##bits##_compare_exchange_##strength(u##bits *ptr, u##bits *exp,          \
							      u##bits val, int mo, int fail_mo);   \
	int __tsan_atomic##bits##_compare_exchange_##strength(u##bits *ptr, u##bits *exp,          \
							      u##bits val, int mo, int fail_mo)    \
	{                                                                                          \
		if (!IS_ENABLED(CONFIG_KCSAN_IGNORE_ATOMICS)) {                                    \
			check_access(ptr, bits / BITS_PER_BYTE,                                    \
				     KCSAN_ACCESS_COMPOUND | KCSAN_ACCESS_WRITE |                  \
					     KCSAN_ACCESS_ATOMIC, _RET_IP_);                       \
		}                                                                                  \
		return __atomic_compare_exchange_n(ptr, exp, val, weak, mo, fail_mo);              \
	}                                                                                          \
	EXPORT_SYMBOL(__tsan_atomic##bits##_compare_exchange_##strength)

#define DEFINE_TSAN_ATOMIC_CMPXCHG_VAL(bits)                                                       \
	u##bits __tsan_atomic##bits##_compare_exchange_val(u##bits *ptr, u##bits exp, u##bits val, \
							   int mo, int fail_mo);                   \
	u##bits __tsan_atomic##bits##_compare_exchange_val(u##bits *ptr, u##bits exp, u##bits val, \
							   int mo, int fail_mo)                    \
	{                                                                                          \
		if (!IS_ENABLED(CONFIG_KCSAN_IGNORE_ATOMICS)) {                                    \
			check_access(ptr, bits / BITS_PER_BYTE,                                    \
				     KCSAN_ACCESS_COMPOUND | KCSAN_ACCESS_WRITE |                  \
					     KCSAN_ACCESS_ATOMIC, _RET_IP_);                       \
		}                                                                                  \
		__atomic_compare_exchange_n(ptr, &exp, val, 0, mo, fail_mo);                       \
		return exp;                                                                        \
	}                                                                                          \
	EXPORT_SYMBOL(__tsan_atomic##bits##_compare_exchange_val)

#define DEFINE_TSAN_ATOMIC_OPS(bits)                                                               \
	DEFINE_TSAN_ATOMIC_LOAD_STORE(bits);                                                       \
	DEFINE_TSAN_ATOMIC_RMW(exchange, bits, _n);                                                \
	DEFINE_TSAN_ATOMIC_RMW(fetch_add, bits, );                                                 \
	DEFINE_TSAN_ATOMIC_RMW(fetch_sub, bits, );                                                 \
	DEFINE_TSAN_ATOMIC_RMW(fetch_and, bits, );                                                 \
	DEFINE_TSAN_ATOMIC_RMW(fetch_or, bits, );                                                  \
	DEFINE_TSAN_ATOMIC_RMW(fetch_xor, bits, );                                                 \
	DEFINE_TSAN_ATOMIC_RMW(fetch_nand, bits, );                                                \
	DEFINE_TSAN_ATOMIC_CMPXCHG(bits, strong, 0);                                               \
	DEFINE_TSAN_ATOMIC_CMPXCHG(bits, weak, 1);                                                 \
	DEFINE_TSAN_ATOMIC_CMPXCHG_VAL(bits)

DEFINE_TSAN_ATOMIC_OPS(8);
DEFINE_TSAN_ATOMIC_OPS(16);
DEFINE_TSAN_ATOMIC_OPS(32);
DEFINE_TSAN_ATOMIC_OPS(64);

void __tsan_atomic_thread_fence(int memorder);
void __tsan_atomic_thread_fence(int memorder)
{
	__atomic_thread_fence(memorder);
}
EXPORT_SYMBOL(__tsan_atomic_thread_fence);

void __tsan_atomic_signal_fence(int memorder);
void __tsan_atomic_signal_fence(int memorder) { }
EXPORT_SYMBOL(__tsan_atomic_signal_fence);
