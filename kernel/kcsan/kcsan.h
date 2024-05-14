/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The Kernel Concurrency Sanitizer (KCSAN) infrastructure. For more info please
 * see Documentation/dev-tools/kcsan.rst.
 *
 * Copyright (C) 2019, Google LLC.
 */

#ifndef _KERNEL_KCSAN_KCSAN_H
#define _KERNEL_KCSAN_KCSAN_H

#include <linux/atomic.h>
#include <linux/kcsan.h>
#include <linux/sched.h>

/* The number of adjacent watchpoints to check. */
#define KCSAN_CHECK_ADJACENT 1
#define NUM_SLOTS (1 + 2*KCSAN_CHECK_ADJACENT)

extern unsigned int kcsan_udelay_task;
extern unsigned int kcsan_udelay_interrupt;

/*
 * Globally enable and disable KCSAN.
 */
extern bool kcsan_enabled;

/*
 * Save/restore IRQ flags state trace dirtied by KCSAN.
 */
void kcsan_save_irqtrace(struct task_struct *task);
void kcsan_restore_irqtrace(struct task_struct *task);

/*
 * Statistics counters displayed via debugfs; should only be modified in
 * slow-paths.
 */
enum kcsan_counter_id {
	/*
	 * Number of watchpoints currently in use.
	 */
	KCSAN_COUNTER_USED_WATCHPOINTS,

	/*
	 * Total number of watchpoints set up.
	 */
	KCSAN_COUNTER_SETUP_WATCHPOINTS,

	/*
	 * Total number of data races.
	 */
	KCSAN_COUNTER_DATA_RACES,

	/*
	 * Total number of ASSERT failures due to races. If the observed race is
	 * due to two conflicting ASSERT type accesses, then both will be
	 * counted.
	 */
	KCSAN_COUNTER_ASSERT_FAILURES,

	/*
	 * Number of times no watchpoints were available.
	 */
	KCSAN_COUNTER_NO_CAPACITY,

	/*
	 * A thread checking a watchpoint raced with another checking thread;
	 * only one will be reported.
	 */
	KCSAN_COUNTER_REPORT_RACES,

	/*
	 * Observed data value change, but writer thread unknown.
	 */
	KCSAN_COUNTER_RACES_UNKNOWN_ORIGIN,

	/*
	 * The access cannot be encoded to a valid watchpoint.
	 */
	KCSAN_COUNTER_UNENCODABLE_ACCESSES,

	/*
	 * Watchpoint encoding caused a watchpoint to fire on mismatching
	 * accesses.
	 */
	KCSAN_COUNTER_ENCODING_FALSE_POSITIVES,

	KCSAN_COUNTER_COUNT, /* number of counters */
};
extern atomic_long_t kcsan_counters[KCSAN_COUNTER_COUNT];

/*
 * Returns true if data races in the function symbol that maps to func_addr
 * (offsets are ignored) should *not* be reported.
 */
extern bool kcsan_skip_report_debugfs(unsigned long func_addr);

/*
 * Value-change states.
 */
enum kcsan_value_change {
	/*
	 * Did not observe a value-change, however, it is valid to report the
	 * race, depending on preferences.
	 */
	KCSAN_VALUE_CHANGE_MAYBE,

	/*
	 * Did not observe a value-change, and it is invalid to report the race.
	 */
	KCSAN_VALUE_CHANGE_FALSE,

	/*
	 * The value was observed to change, and the race should be reported.
	 */
	KCSAN_VALUE_CHANGE_TRUE,
};

/*
 * The calling thread hit and consumed a watchpoint: set the access information
 * to be consumed by the reporting thread. No report is printed yet.
 */
void kcsan_report_set_info(const volatile void *ptr, size_t size, int access_type,
			   unsigned long ip, int watchpoint_idx);

/*
 * The calling thread observed that the watchpoint it set up was hit and
 * consumed: print the full report based on information set by the racing
 * thread.
 */
void kcsan_report_known_origin(const volatile void *ptr, size_t size, int access_type,
			       unsigned long ip, enum kcsan_value_change value_change,
			       int watchpoint_idx, u64 old, u64 new, u64 mask);

/*
 * No other thread was observed to race with the access, but the data value
 * before and after the stall differs. Reports a race of "unknown origin".
 */
void kcsan_report_unknown_origin(const volatile void *ptr, size_t size, int access_type,
				 unsigned long ip, u64 old, u64 new, u64 mask);

#endif /* _KERNEL_KCSAN_KCSAN_H */
