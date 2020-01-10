// SPDX-License-Identifier: GPL-2.0

#include <linux/kernel.h>
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

/*
 * Other thread info: communicated from other racing thread to thread that set
 * up the watchpoint, which then prints the complete report atomically. Only
 * need one struct, as all threads should to be serialized regardless to print
 * the reports, with reporting being in the slow-path.
 */
static struct {
	const volatile void	*ptr;
	size_t			size;
	int			access_type;
	int			task_pid;
	int			cpu_id;
	unsigned long		stack_entries[NUM_STACK_ENTRIES];
	int			num_stack_entries;
} other_info = { .ptr = NULL };

/*
 * This spinlock protects reporting and other_info, since other_info is usually
 * required when reporting.
 */
static DEFINE_SPINLOCK(report_lock);

/*
 * Special rules to skip reporting.
 */
static bool
skip_report(int access_type, bool value_change, unsigned long top_frame)
{
	const bool is_write = (access_type & KCSAN_ACCESS_WRITE) != 0;

	if (IS_ENABLED(CONFIG_KCSAN_REPORT_VALUE_CHANGE_ONLY) && is_write &&
	    !value_change) {
		/*
		 * The access is a write, but the data value did not change.
		 *
		 * We opt-out of this filter for certain functions at request of
		 * maintainers.
		 */
		char buf[64];

		snprintf(buf, sizeof(buf), "%ps", (void *)top_frame);
		if (!strnstr(buf, "rcu_", sizeof(buf)) &&
		    !strnstr(buf, "_rcu", sizeof(buf)) &&
		    !strnstr(buf, "_srcu", sizeof(buf)))
			return true;
	}

	return kcsan_skip_report_debugfs(top_frame);
}

static const char *get_access_type(int type)
{
	switch (type) {
	case 0:
		return "read";
	case KCSAN_ACCESS_ATOMIC:
		return "read (marked)";
	case KCSAN_ACCESS_WRITE:
		return "write";
	case KCSAN_ACCESS_WRITE | KCSAN_ACCESS_ATOMIC:
		return "write (marked)";
	default:
		BUG();
	}
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
static int get_stack_skipnr(unsigned long stack_entries[], int num_entries)
{
	char buf[64];
	int skip = 0;

	for (; skip < num_entries; ++skip) {
		snprintf(buf, sizeof(buf), "%ps", (void *)stack_entries[skip]);
		if (!strnstr(buf, "csan_", sizeof(buf)) &&
		    !strnstr(buf, "tsan_", sizeof(buf)) &&
		    !strnstr(buf, "_once_size", sizeof(buf))) {
			break;
		}
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

/*
 * Returns true if a report was generated, false otherwise.
 */
static bool print_report(const volatile void *ptr, size_t size, int access_type,
			 bool value_change, int cpu_id,
			 enum kcsan_report_type type)
{
	unsigned long stack_entries[NUM_STACK_ENTRIES] = { 0 };
	int num_stack_entries = stack_trace_save(stack_entries, NUM_STACK_ENTRIES, 1);
	int skipnr = get_stack_skipnr(stack_entries, num_stack_entries);
	int other_skipnr;

	/*
	 * Must check report filter rules before starting to print.
	 */
	if (skip_report(access_type, true, stack_entries[skipnr]))
		return false;

	if (type == KCSAN_REPORT_RACE_SIGNAL) {
		other_skipnr = get_stack_skipnr(other_info.stack_entries,
						other_info.num_stack_entries);

		/* @value_change is only known for the other thread */
		if (skip_report(other_info.access_type, value_change,
				other_info.stack_entries[other_skipnr]))
			return false;
	}

	/* Print report header. */
	pr_err("==================================================================\n");
	switch (type) {
	case KCSAN_REPORT_RACE_SIGNAL: {
		void *this_fn = (void *)stack_entries[skipnr];
		void *other_fn = (void *)other_info.stack_entries[other_skipnr];
		int cmp;

		/*
		 * Order functions lexographically for consistent bug titles.
		 * Do not print offset of functions to keep title short.
		 */
		cmp = sym_strcmp(other_fn, this_fn);
		pr_err("BUG: KCSAN: data-race in %ps / %ps\n",
		       cmp < 0 ? other_fn : this_fn,
		       cmp < 0 ? this_fn : other_fn);
	} break;

	case KCSAN_REPORT_RACE_UNKNOWN_ORIGIN:
		pr_err("BUG: KCSAN: data-race in %pS\n",
		       (void *)stack_entries[skipnr]);
		break;

	default:
		BUG();
	}

	pr_err("\n");

	/* Print information about the racing accesses. */
	switch (type) {
	case KCSAN_REPORT_RACE_SIGNAL:
		pr_err("%s to 0x%px of %zu bytes by %s on cpu %i:\n",
		       get_access_type(other_info.access_type), other_info.ptr,
		       other_info.size, get_thread_desc(other_info.task_pid),
		       other_info.cpu_id);

		/* Print the other thread's stack trace. */
		stack_trace_print(other_info.stack_entries + other_skipnr,
				  other_info.num_stack_entries - other_skipnr,
				  0);

		pr_err("\n");
		pr_err("%s to 0x%px of %zu bytes by %s on cpu %i:\n",
		       get_access_type(access_type), ptr, size,
		       get_thread_desc(in_task() ? task_pid_nr(current) : -1),
		       cpu_id);
		break;

	case KCSAN_REPORT_RACE_UNKNOWN_ORIGIN:
		pr_err("race at unknown origin, with %s to 0x%px of %zu bytes by %s on cpu %i:\n",
		       get_access_type(access_type), ptr, size,
		       get_thread_desc(in_task() ? task_pid_nr(current) : -1),
		       cpu_id);
		break;

	default:
		BUG();
	}
	/* Print stack trace of this thread. */
	stack_trace_print(stack_entries + skipnr, num_stack_entries - skipnr,
			  0);

	/* Print report footer. */
	pr_err("\n");
	pr_err("Reported by Kernel Concurrency Sanitizer on:\n");
	dump_stack_print_info(KERN_DEFAULT);
	pr_err("==================================================================\n");

	return true;
}

static void release_report(unsigned long *flags, enum kcsan_report_type type)
{
	if (type == KCSAN_REPORT_RACE_SIGNAL)
		other_info.ptr = NULL; /* mark for reuse */

	spin_unlock_irqrestore(&report_lock, *flags);
}

/*
 * Depending on the report type either sets other_info and returns false, or
 * acquires the matching other_info and returns true. If other_info is not
 * required for the report type, simply acquires report_lock and returns true.
 */
static bool prepare_report(unsigned long *flags, const volatile void *ptr,
			   size_t size, int access_type, int cpu_id,
			   enum kcsan_report_type type)
{
	if (type != KCSAN_REPORT_CONSUMED_WATCHPOINT &&
	    type != KCSAN_REPORT_RACE_SIGNAL) {
		/* other_info not required; just acquire report_lock */
		spin_lock_irqsave(&report_lock, *flags);
		return true;
	}

retry:
	spin_lock_irqsave(&report_lock, *flags);

	switch (type) {
	case KCSAN_REPORT_CONSUMED_WATCHPOINT:
		if (other_info.ptr != NULL)
			break; /* still in use, retry */

		other_info.ptr			= ptr;
		other_info.size			= size;
		other_info.access_type		= access_type;
		other_info.task_pid		= in_task() ? task_pid_nr(current) : -1;
		other_info.cpu_id		= cpu_id;
		other_info.num_stack_entries	= stack_trace_save(other_info.stack_entries, NUM_STACK_ENTRIES, 1);

		spin_unlock_irqrestore(&report_lock, *flags);

		/*
		 * The other thread will print the summary; other_info may now
		 * be consumed.
		 */
		return false;

	case KCSAN_REPORT_RACE_SIGNAL:
		if (other_info.ptr == NULL)
			break; /* no data available yet, retry */

		/*
		 * First check if this is the other_info we are expecting, i.e.
		 * matches based on how watchpoint was encoded.
		 */
		if (!matching_access((unsigned long)other_info.ptr &
					     WATCHPOINT_ADDR_MASK,
				     other_info.size,
				     (unsigned long)ptr & WATCHPOINT_ADDR_MASK,
				     size))
			break; /* mismatching watchpoint, retry */

		if (!matching_access((unsigned long)other_info.ptr,
				     other_info.size, (unsigned long)ptr,
				     size)) {
			/*
			 * If the actual accesses to not match, this was a false
			 * positive due to watchpoint encoding.
			 */
			kcsan_counter_inc(
				KCSAN_COUNTER_ENCODING_FALSE_POSITIVES);

			/* discard this other_info */
			release_report(flags, KCSAN_REPORT_RACE_SIGNAL);
			return false;
		}

		/*
		 * Matching & usable access in other_info: keep other_info_lock
		 * locked, as this thread consumes it to print the full report;
		 * unlocked in release_report.
		 */
		return true;

	default:
		BUG();
	}

	spin_unlock_irqrestore(&report_lock, *flags);

	goto retry;
}

void kcsan_report(const volatile void *ptr, size_t size, int access_type,
		  bool value_change, int cpu_id, enum kcsan_report_type type)
{
	unsigned long flags = 0;

	kcsan_disable_current();
	if (prepare_report(&flags, ptr, size, access_type, cpu_id, type)) {
		if (print_report(ptr, size, access_type, value_change, cpu_id, type) && panic_on_warn)
			panic("panic_on_warn set ...\n");

		release_report(&flags, type);
	}
	kcsan_enable_current();
}
