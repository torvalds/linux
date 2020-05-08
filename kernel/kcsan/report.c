// SPDX-License-Identifier: GPL-2.0

#include <linux/debug_locks.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/lockdep.h>
#include <linux/preempt.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/stacktrace.h>

#include "kcsan.h"
#include "encoding.h"

/*
 * Max. number of stack entries to show in the report.
 */
#define NUM_STACK_ENTRIES 64

/* Common access info. */
struct access_info {
	const volatile void	*ptr;
	size_t			size;
	int			access_type;
	int			task_pid;
	int			cpu_id;
};

/*
 * Other thread info: communicated from other racing thread to thread that set
 * up the watchpoint, which then prints the complete report atomically.
 */
struct other_info {
	struct access_info	ai;
	unsigned long		stack_entries[NUM_STACK_ENTRIES];
	int			num_stack_entries;

	/*
	 * Optionally pass @current. Typically we do not need to pass @current
	 * via @other_info since just @task_pid is sufficient. Passing @current
	 * has additional overhead.
	 *
	 * To safely pass @current, we must either use get_task_struct/
	 * put_task_struct, or stall the thread that populated @other_info.
	 *
	 * We cannot rely on get_task_struct/put_task_struct in case
	 * release_report() races with a task being released, and would have to
	 * free it in release_report(). This may result in deadlock if we want
	 * to use KCSAN on the allocators.
	 *
	 * Since we also want to reliably print held locks for
	 * CONFIG_KCSAN_VERBOSE, the current implementation stalls the thread
	 * that populated @other_info until it has been consumed.
	 */
	struct task_struct	*task;
};

/*
 * To never block any producers of struct other_info, we need as many elements
 * as we have watchpoints (upper bound on concurrent races to report).
 */
static struct other_info other_infos[CONFIG_KCSAN_NUM_WATCHPOINTS + NUM_SLOTS-1];

/*
 * Information about reported races; used to rate limit reporting.
 */
struct report_time {
	/*
	 * The last time the race was reported.
	 */
	unsigned long time;

	/*
	 * The frames of the 2 threads; if only 1 thread is known, one frame
	 * will be 0.
	 */
	unsigned long frame1;
	unsigned long frame2;
};

/*
 * Since we also want to be able to debug allocators with KCSAN, to avoid
 * deadlock, report_times cannot be dynamically resized with krealloc in
 * rate_limit_report.
 *
 * Therefore, we use a fixed-size array, which at most will occupy a page. This
 * still adequately rate limits reports, assuming that a) number of unique data
 * races is not excessive, and b) occurrence of unique races within the
 * same time window is limited.
 */
#define REPORT_TIMES_MAX (PAGE_SIZE / sizeof(struct report_time))
#define REPORT_TIMES_SIZE                                                      \
	(CONFIG_KCSAN_REPORT_ONCE_IN_MS > REPORT_TIMES_MAX ?                   \
		 REPORT_TIMES_MAX :                                            \
		 CONFIG_KCSAN_REPORT_ONCE_IN_MS)
static struct report_time report_times[REPORT_TIMES_SIZE];

/*
 * Spinlock serializing report generation, and access to @other_infos. Although
 * it could make sense to have a finer-grained locking story for @other_infos,
 * report generation needs to be serialized either way, so not much is gained.
 */
static DEFINE_RAW_SPINLOCK(report_lock);

/*
 * Checks if the race identified by thread frames frame1 and frame2 has
 * been reported since (now - KCSAN_REPORT_ONCE_IN_MS).
 */
static bool rate_limit_report(unsigned long frame1, unsigned long frame2)
{
	struct report_time *use_entry = &report_times[0];
	unsigned long invalid_before;
	int i;

	BUILD_BUG_ON(CONFIG_KCSAN_REPORT_ONCE_IN_MS != 0 && REPORT_TIMES_SIZE == 0);

	if (CONFIG_KCSAN_REPORT_ONCE_IN_MS == 0)
		return false;

	invalid_before = jiffies - msecs_to_jiffies(CONFIG_KCSAN_REPORT_ONCE_IN_MS);

	/* Check if a matching race report exists. */
	for (i = 0; i < REPORT_TIMES_SIZE; ++i) {
		struct report_time *rt = &report_times[i];

		/*
		 * Must always select an entry for use to store info as we
		 * cannot resize report_times; at the end of the scan, use_entry
		 * will be the oldest entry, which ideally also happened before
		 * KCSAN_REPORT_ONCE_IN_MS ago.
		 */
		if (time_before(rt->time, use_entry->time))
			use_entry = rt;

		/*
		 * Initially, no need to check any further as this entry as well
		 * as following entries have never been used.
		 */
		if (rt->time == 0)
			break;

		/* Check if entry expired. */
		if (time_before(rt->time, invalid_before))
			continue; /* before KCSAN_REPORT_ONCE_IN_MS ago */

		/* Reported recently, check if race matches. */
		if ((rt->frame1 == frame1 && rt->frame2 == frame2) ||
		    (rt->frame1 == frame2 && rt->frame2 == frame1))
			return true;
	}

	use_entry->time = jiffies;
	use_entry->frame1 = frame1;
	use_entry->frame2 = frame2;
	return false;
}

/*
 * Special rules to skip reporting.
 */
static bool
skip_report(enum kcsan_value_change value_change, unsigned long top_frame)
{
	/* Should never get here if value_change==FALSE. */
	WARN_ON_ONCE(value_change == KCSAN_VALUE_CHANGE_FALSE);

	/*
	 * The first call to skip_report always has value_change==TRUE, since we
	 * cannot know the value written of an instrumented access. For the 2nd
	 * call there are 6 cases with CONFIG_KCSAN_REPORT_VALUE_CHANGE_ONLY:
	 *
	 * 1. read watchpoint, conflicting write (value_change==TRUE): report;
	 * 2. read watchpoint, conflicting write (value_change==MAYBE): skip;
	 * 3. write watchpoint, conflicting write (value_change==TRUE): report;
	 * 4. write watchpoint, conflicting write (value_change==MAYBE): skip;
	 * 5. write watchpoint, conflicting read (value_change==MAYBE): skip;
	 * 6. write watchpoint, conflicting read (value_change==TRUE): report;
	 *
	 * Cases 1-4 are intuitive and expected; case 5 ensures we do not report
	 * data races where the write may have rewritten the same value; case 6
	 * is possible either if the size is larger than what we check value
	 * changes for or the access type is KCSAN_ACCESS_ASSERT.
	 */
	if (IS_ENABLED(CONFIG_KCSAN_REPORT_VALUE_CHANGE_ONLY) &&
	    value_change == KCSAN_VALUE_CHANGE_MAYBE) {
		/*
		 * The access is a write, but the data value did not change.
		 *
		 * We opt-out of this filter for certain functions at request of
		 * maintainers.
		 */
		char buf[64];
		int len = scnprintf(buf, sizeof(buf), "%ps", (void *)top_frame);

		if (!strnstr(buf, "rcu_", len) &&
		    !strnstr(buf, "_rcu", len) &&
		    !strnstr(buf, "_srcu", len))
			return true;
	}

	return kcsan_skip_report_debugfs(top_frame);
}

static const char *get_access_type(int type)
{
	if (type & KCSAN_ACCESS_ASSERT) {
		if (type & KCSAN_ACCESS_SCOPED) {
			if (type & KCSAN_ACCESS_WRITE)
				return "assert no accesses (scoped)";
			else
				return "assert no writes (scoped)";
		} else {
			if (type & KCSAN_ACCESS_WRITE)
				return "assert no accesses";
			else
				return "assert no writes";
		}
	}

	switch (type) {
	case 0:
		return "read";
	case KCSAN_ACCESS_ATOMIC:
		return "read (marked)";
	case KCSAN_ACCESS_WRITE:
		return "write";
	case KCSAN_ACCESS_WRITE | KCSAN_ACCESS_ATOMIC:
		return "write (marked)";
	case KCSAN_ACCESS_SCOPED:
		return "read (scoped)";
	case KCSAN_ACCESS_SCOPED | KCSAN_ACCESS_ATOMIC:
		return "read (marked, scoped)";
	case KCSAN_ACCESS_SCOPED | KCSAN_ACCESS_WRITE:
		return "write (scoped)";
	case KCSAN_ACCESS_SCOPED | KCSAN_ACCESS_WRITE | KCSAN_ACCESS_ATOMIC:
		return "write (marked, scoped)";
	default:
		BUG();
	}
}

static const char *get_bug_type(int type)
{
	return (type & KCSAN_ACCESS_ASSERT) != 0 ? "assert: race" : "data-race";
}

/* Return thread description: in task or interrupt. */
static const char *get_thread_desc(int task_id)
{
	if (task_id != -1) {
		static char buf[32]; /* safe: protected by report_lock */

		snprintf(buf, sizeof(buf), "task %i", task_id);
		return buf;
	}
	return "interrupt";
}

/* Helper to skip KCSAN-related functions in stack-trace. */
static int get_stack_skipnr(const unsigned long stack_entries[], int num_entries)
{
	char buf[64];
	char *cur;
	int len, skip;

	for (skip = 0; skip < num_entries; ++skip) {
		len = scnprintf(buf, sizeof(buf), "%ps", (void *)stack_entries[skip]);

		/* Never show tsan_* or {read,write}_once_size. */
		if (strnstr(buf, "tsan_", len) ||
		    strnstr(buf, "_once_size", len))
			continue;

		cur = strnstr(buf, "kcsan_", len);
		if (cur) {
			cur += sizeof("kcsan_") - 1;
			if (strncmp(cur, "test", sizeof("test") - 1))
				continue; /* KCSAN runtime function. */
			/* KCSAN related test. */
		}

		/*
		 * No match for runtime functions -- @skip entries to skip to
		 * get to first frame of interest.
		 */
		break;
	}

	return skip;
}

/* Compares symbolized strings of addr1 and addr2. */
static int sym_strcmp(void *addr1, void *addr2)
{
	char buf1[64];
	char buf2[64];

	snprintf(buf1, sizeof(buf1), "%pS", addr1);
	snprintf(buf2, sizeof(buf2), "%pS", addr2);

	return strncmp(buf1, buf2, sizeof(buf1));
}

static void print_verbose_info(struct task_struct *task)
{
	if (!task)
		return;

	pr_err("\n");
	debug_show_held_locks(task);
	print_irqtrace_events(task);
}

/*
 * Returns true if a report was generated, false otherwise.
 */
static bool print_report(enum kcsan_value_change value_change,
			 enum kcsan_report_type type,
			 const struct access_info *ai,
			 const struct other_info *other_info)
{
	unsigned long stack_entries[NUM_STACK_ENTRIES] = { 0 };
	int num_stack_entries = stack_trace_save(stack_entries, NUM_STACK_ENTRIES, 1);
	int skipnr = get_stack_skipnr(stack_entries, num_stack_entries);
	unsigned long this_frame = stack_entries[skipnr];
	unsigned long other_frame = 0;
	int other_skipnr = 0; /* silence uninit warnings */

	/*
	 * Must check report filter rules before starting to print.
	 */
	if (skip_report(KCSAN_VALUE_CHANGE_TRUE, stack_entries[skipnr]))
		return false;

	if (type == KCSAN_REPORT_RACE_SIGNAL) {
		other_skipnr = get_stack_skipnr(other_info->stack_entries,
						other_info->num_stack_entries);
		other_frame = other_info->stack_entries[other_skipnr];

		/* @value_change is only known for the other thread */
		if (skip_report(value_change, other_frame))
			return false;
	}

	if (rate_limit_report(this_frame, other_frame))
		return false;

	/* Print report header. */
	pr_err("==================================================================\n");
	switch (type) {
	case KCSAN_REPORT_RACE_SIGNAL: {
		int cmp;

		/*
		 * Order functions lexographically for consistent bug titles.
		 * Do not print offset of functions to keep title short.
		 */
		cmp = sym_strcmp((void *)other_frame, (void *)this_frame);
		pr_err("BUG: KCSAN: %s in %ps / %ps\n",
		       get_bug_type(ai->access_type | other_info->ai.access_type),
		       (void *)(cmp < 0 ? other_frame : this_frame),
		       (void *)(cmp < 0 ? this_frame : other_frame));
	} break;

	case KCSAN_REPORT_RACE_UNKNOWN_ORIGIN:
		pr_err("BUG: KCSAN: %s in %pS\n", get_bug_type(ai->access_type),
		       (void *)this_frame);
		break;

	default:
		BUG();
	}

	pr_err("\n");

	/* Print information about the racing accesses. */
	switch (type) {
	case KCSAN_REPORT_RACE_SIGNAL:
		pr_err("%s to 0x%px of %zu bytes by %s on cpu %i:\n",
		       get_access_type(other_info->ai.access_type), other_info->ai.ptr,
		       other_info->ai.size, get_thread_desc(other_info->ai.task_pid),
		       other_info->ai.cpu_id);

		/* Print the other thread's stack trace. */
		stack_trace_print(other_info->stack_entries + other_skipnr,
				  other_info->num_stack_entries - other_skipnr,
				  0);

		if (IS_ENABLED(CONFIG_KCSAN_VERBOSE))
			print_verbose_info(other_info->task);

		pr_err("\n");
		pr_err("%s to 0x%px of %zu bytes by %s on cpu %i:\n",
		       get_access_type(ai->access_type), ai->ptr, ai->size,
		       get_thread_desc(ai->task_pid), ai->cpu_id);
		break;

	case KCSAN_REPORT_RACE_UNKNOWN_ORIGIN:
		pr_err("race at unknown origin, with %s to 0x%px of %zu bytes by %s on cpu %i:\n",
		       get_access_type(ai->access_type), ai->ptr, ai->size,
		       get_thread_desc(ai->task_pid), ai->cpu_id);
		break;

	default:
		BUG();
	}
	/* Print stack trace of this thread. */
	stack_trace_print(stack_entries + skipnr, num_stack_entries - skipnr,
			  0);

	if (IS_ENABLED(CONFIG_KCSAN_VERBOSE))
		print_verbose_info(current);

	/* Print report footer. */
	pr_err("\n");
	pr_err("Reported by Kernel Concurrency Sanitizer on:\n");
	dump_stack_print_info(KERN_DEFAULT);
	pr_err("==================================================================\n");

	return true;
}

static void release_report(unsigned long *flags, struct other_info *other_info)
{
	if (other_info)
		/*
		 * Use size to denote valid/invalid, since KCSAN entirely
		 * ignores 0-sized accesses.
		 */
		other_info->ai.size = 0;

	raw_spin_unlock_irqrestore(&report_lock, *flags);
}

/*
 * Sets @other_info->task and awaits consumption of @other_info.
 *
 * Precondition: report_lock is held.
 * Postcondition: report_lock is held.
 */
static void set_other_info_task_blocking(unsigned long *flags,
					 const struct access_info *ai,
					 struct other_info *other_info)
{
	/*
	 * We may be instrumenting a code-path where current->state is already
	 * something other than TASK_RUNNING.
	 */
	const bool is_running = current->state == TASK_RUNNING;
	/*
	 * To avoid deadlock in case we are in an interrupt here and this is a
	 * race with a task on the same CPU (KCSAN_INTERRUPT_WATCHER), provide a
	 * timeout to ensure this works in all contexts.
	 *
	 * Await approximately the worst case delay of the reporting thread (if
	 * we are not interrupted).
	 */
	int timeout = max(kcsan_udelay_task, kcsan_udelay_interrupt);

	other_info->task = current;
	do {
		if (is_running) {
			/*
			 * Let lockdep know the real task is sleeping, to print
			 * the held locks (recall we turned lockdep off, so
			 * locking/unlocking @report_lock won't be recorded).
			 */
			set_current_state(TASK_UNINTERRUPTIBLE);
		}
		raw_spin_unlock_irqrestore(&report_lock, *flags);
		/*
		 * We cannot call schedule() since we also cannot reliably
		 * determine if sleeping here is permitted -- see in_atomic().
		 */

		udelay(1);
		raw_spin_lock_irqsave(&report_lock, *flags);
		if (timeout-- < 0) {
			/*
			 * Abort. Reset @other_info->task to NULL, since it
			 * appears the other thread is still going to consume
			 * it. It will result in no verbose info printed for
			 * this task.
			 */
			other_info->task = NULL;
			break;
		}
		/*
		 * If invalid, or @ptr nor @current matches, then @other_info
		 * has been consumed and we may continue. If not, retry.
		 */
	} while (other_info->ai.size && other_info->ai.ptr == ai->ptr &&
		 other_info->task == current);
	if (is_running)
		set_current_state(TASK_RUNNING);
}

/* Populate @other_info; requires that the provided @other_info not in use. */
static void prepare_report_producer(unsigned long *flags,
				    const struct access_info *ai,
				    struct other_info *other_info)
{
	raw_spin_lock_irqsave(&report_lock, *flags);

	/*
	 * The same @other_infos entry cannot be used concurrently, because
	 * there is a one-to-one mapping to watchpoint slots (@watchpoints in
	 * core.c), and a watchpoint is only released for reuse after reporting
	 * is done by the consumer of @other_info. Therefore, it is impossible
	 * for another concurrent prepare_report_producer() to set the same
	 * @other_info, and are guaranteed exclusivity for the @other_infos
	 * entry pointed to by @other_info.
	 *
	 * To check this property holds, size should never be non-zero here,
	 * because every consumer of struct other_info resets size to 0 in
	 * release_report().
	 */
	WARN_ON(other_info->ai.size);

	other_info->ai = *ai;
	other_info->num_stack_entries = stack_trace_save(other_info->stack_entries, NUM_STACK_ENTRIES, 2);

	if (IS_ENABLED(CONFIG_KCSAN_VERBOSE))
		set_other_info_task_blocking(flags, ai, other_info);

	raw_spin_unlock_irqrestore(&report_lock, *flags);
}

/* Awaits producer to fill @other_info and then returns. */
static bool prepare_report_consumer(unsigned long *flags,
				    const struct access_info *ai,
				    struct other_info *other_info)
{

	raw_spin_lock_irqsave(&report_lock, *flags);
	while (!other_info->ai.size) { /* Await valid @other_info. */
		raw_spin_unlock_irqrestore(&report_lock, *flags);
		cpu_relax();
		raw_spin_lock_irqsave(&report_lock, *flags);
	}

	/* Should always have a matching access based on watchpoint encoding. */
	if (WARN_ON(!matching_access((unsigned long)other_info->ai.ptr & WATCHPOINT_ADDR_MASK, other_info->ai.size,
				     (unsigned long)ai->ptr & WATCHPOINT_ADDR_MASK, ai->size)))
		goto discard;

	if (!matching_access((unsigned long)other_info->ai.ptr, other_info->ai.size,
			     (unsigned long)ai->ptr, ai->size)) {
		/*
		 * If the actual accesses to not match, this was a false
		 * positive due to watchpoint encoding.
		 */
		kcsan_counter_inc(KCSAN_COUNTER_ENCODING_FALSE_POSITIVES);
		goto discard;
	}

	return true;

discard:
	release_report(flags, other_info);
	return false;
}

/*
 * Depending on the report type either sets @other_info and returns false, or
 * awaits @other_info and returns true. If @other_info is not required for the
 * report type, simply acquires @report_lock and returns true.
 */
static noinline bool prepare_report(unsigned long *flags,
				    enum kcsan_report_type type,
				    const struct access_info *ai,
				    struct other_info *other_info)
{
	switch (type) {
	case KCSAN_REPORT_CONSUMED_WATCHPOINT:
		prepare_report_producer(flags, ai, other_info);
		return false;
	case KCSAN_REPORT_RACE_SIGNAL:
		return prepare_report_consumer(flags, ai, other_info);
	default:
		/* @other_info not required; just acquire @report_lock. */
		raw_spin_lock_irqsave(&report_lock, *flags);
		return true;
	}
}

void kcsan_report(const volatile void *ptr, size_t size, int access_type,
		  enum kcsan_value_change value_change,
		  enum kcsan_report_type type, int watchpoint_idx)
{
	unsigned long flags = 0;
	const struct access_info ai = {
		.ptr		= ptr,
		.size		= size,
		.access_type	= access_type,
		.task_pid	= in_task() ? task_pid_nr(current) : -1,
		.cpu_id		= raw_smp_processor_id()
	};
	struct other_info *other_info = type == KCSAN_REPORT_RACE_UNKNOWN_ORIGIN
					? NULL : &other_infos[watchpoint_idx];

	kcsan_disable_current();
	if (WARN_ON(watchpoint_idx < 0 || watchpoint_idx >= ARRAY_SIZE(other_infos)))
		goto out;

	/*
	 * With TRACE_IRQFLAGS, lockdep's IRQ trace state becomes corrupted if
	 * we do not turn off lockdep here; this could happen due to recursion
	 * into lockdep via KCSAN if we detect a race in utilities used by
	 * lockdep.
	 */
	lockdep_off();

	if (prepare_report(&flags, type, &ai, other_info)) {
		/*
		 * Never report if value_change is FALSE, only if we it is
		 * either TRUE or MAYBE. In case of MAYBE, further filtering may
		 * be done once we know the full stack trace in print_report().
		 */
		bool reported = value_change != KCSAN_VALUE_CHANGE_FALSE &&
				print_report(value_change, type, &ai, other_info);

		if (reported && panic_on_warn)
			panic("panic_on_warn set ...\n");

		release_report(&flags, other_info);
	}

	lockdep_on();
out:
	kcsan_enable_current();
}
