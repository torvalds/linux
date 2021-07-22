// SPDX-License-Identifier: GPL-2.0
/*
 * KCSAN test with various race scenarious to test runtime behaviour. Since the
 * interface with which KCSAN's reports are obtained is via the console, this is
 * the output we should verify. For each test case checks the presence (or
 * absence) of generated reports. Relies on 'console' tracepoint to capture
 * reports as they appear in the kernel log.
 *
 * Makes use of KUnit for test organization, and the Torture framework for test
 * thread control.
 *
 * Copyright (C) 2020, Google LLC.
 * Author: Marco Elver <elver@google.com>
 */

#define pr_fmt(fmt) "kcsan_test: " fmt

#include <kunit/test.h>
#include <linux/jiffies.h>
#include <linux/kcsan-checks.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/seqlock.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/torture.h>
#include <linux/tracepoint.h>
#include <linux/types.h>
#include <trace/events/printk.h>

#ifdef CONFIG_CC_HAS_TSAN_COMPOUND_READ_BEFORE_WRITE
#define __KCSAN_ACCESS_RW(alt) (KCSAN_ACCESS_COMPOUND | KCSAN_ACCESS_WRITE)
#else
#define __KCSAN_ACCESS_RW(alt) (alt)
#endif

/* Points to current test-case memory access "kernels". */
static void (*access_kernels[2])(void);

static struct task_struct **threads; /* Lists of threads. */
static unsigned long end_time;       /* End time of test. */

/* Report as observed from console. */
static struct {
	spinlock_t lock;
	int nlines;
	char lines[3][512];
} observed = {
	.lock = __SPIN_LOCK_UNLOCKED(observed.lock),
};

/* Setup test checking loop. */
static __no_kcsan inline void
begin_test_checks(void (*func1)(void), void (*func2)(void))
{
	kcsan_disable_current();

	/*
	 * Require at least as long as KCSAN_REPORT_ONCE_IN_MS, to ensure at
	 * least one race is reported.
	 */
	end_time = jiffies + msecs_to_jiffies(CONFIG_KCSAN_REPORT_ONCE_IN_MS + 500);

	/* Signal start; release potential initialization of shared data. */
	smp_store_release(&access_kernels[0], func1);
	smp_store_release(&access_kernels[1], func2);
}

/* End test checking loop. */
static __no_kcsan inline bool
end_test_checks(bool stop)
{
	if (!stop && time_before(jiffies, end_time)) {
		/* Continue checking */
		might_sleep();
		return false;
	}

	kcsan_enable_current();
	return true;
}

/*
 * Probe for console output: checks if a race was reported, and obtains observed
 * lines of interest.
 */
__no_kcsan
static void probe_console(void *ignore, const char *buf, size_t len)
{
	unsigned long flags;
	int nlines;

	/*
	 * Note that KCSAN reports under a global lock, so we do not risk the
	 * possibility of having multiple reports interleaved. If that were the
	 * case, we'd expect tests to fail.
	 */

	spin_lock_irqsave(&observed.lock, flags);
	nlines = observed.nlines;

	if (strnstr(buf, "BUG: KCSAN: ", len) && strnstr(buf, "test_", len)) {
		/*
		 * KCSAN report and related to the test.
		 *
		 * The provided @buf is not NUL-terminated; copy no more than
		 * @len bytes and let strscpy() add the missing NUL-terminator.
		 */
		strscpy(observed.lines[0], buf, min(len + 1, sizeof(observed.lines[0])));
		nlines = 1;
	} else if ((nlines == 1 || nlines == 2) && strnstr(buf, "bytes by", len)) {
		strscpy(observed.lines[nlines++], buf, min(len + 1, sizeof(observed.lines[0])));

		if (strnstr(buf, "race at unknown origin", len)) {
			if (WARN_ON(nlines != 2))
				goto out;

			/* No second line of interest. */
			strcpy(observed.lines[nlines++], "<none>");
		}
	}

out:
	WRITE_ONCE(observed.nlines, nlines); /* Publish new nlines. */
	spin_unlock_irqrestore(&observed.lock, flags);
}

/* Check if a report related to the test exists. */
__no_kcsan
static bool report_available(void)
{
	return READ_ONCE(observed.nlines) == ARRAY_SIZE(observed.lines);
}

/* Report information we expect in a report. */
struct expect_report {
	/* Access information of both accesses. */
	struct {
		void *fn;    /* Function pointer to expected function of top frame. */
		void *addr;  /* Address of access; unchecked if NULL. */
		size_t size; /* Size of access; unchecked if @addr is NULL. */
		int type;    /* Access type, see KCSAN_ACCESS definitions. */
	} access[2];
};

/* Check observed report matches information in @r. */
__no_kcsan
static bool report_matches(const struct expect_report *r)
{
	const bool is_assert = (r->access[0].type | r->access[1].type) & KCSAN_ACCESS_ASSERT;
	bool ret = false;
	unsigned long flags;
	typeof(observed.lines) expect;
	const char *end;
	char *cur;
	int i;

	/* Doubled-checked locking. */
	if (!report_available())
		return false;

	/* Generate expected report contents. */

	/* Title */
	cur = expect[0];
	end = &expect[0][sizeof(expect[0]) - 1];
	cur += scnprintf(cur, end - cur, "BUG: KCSAN: %s in ",
			 is_assert ? "assert: race" : "data-race");
	if (r->access[1].fn) {
		char tmp[2][64];
		int cmp;

		/* Expect lexographically sorted function names in title. */
		scnprintf(tmp[0], sizeof(tmp[0]), "%pS", r->access[0].fn);
		scnprintf(tmp[1], sizeof(tmp[1]), "%pS", r->access[1].fn);
		cmp = strcmp(tmp[0], tmp[1]);
		cur += scnprintf(cur, end - cur, "%ps / %ps",
				 cmp < 0 ? r->access[0].fn : r->access[1].fn,
				 cmp < 0 ? r->access[1].fn : r->access[0].fn);
	} else {
		scnprintf(cur, end - cur, "%pS", r->access[0].fn);
		/* The exact offset won't match, remove it. */
		cur = strchr(expect[0], '+');
		if (cur)
			*cur = '\0';
	}

	/* Access 1 */
	cur = expect[1];
	end = &expect[1][sizeof(expect[1]) - 1];
	if (!r->access[1].fn)
		cur += scnprintf(cur, end - cur, "race at unknown origin, with ");

	/* Access 1 & 2 */
	for (i = 0; i < 2; ++i) {
		const int ty = r->access[i].type;
		const char *const access_type =
			(ty & KCSAN_ACCESS_ASSERT) ?
				      ((ty & KCSAN_ACCESS_WRITE) ?
					       "assert no accesses" :
					       "assert no writes") :
				      ((ty & KCSAN_ACCESS_WRITE) ?
					       ((ty & KCSAN_ACCESS_COMPOUND) ?
							"read-write" :
							"write") :
					       "read");
		const char *const access_type_aux =
			(ty & KCSAN_ACCESS_ATOMIC) ?
				      " (marked)" :
				      ((ty & KCSAN_ACCESS_SCOPED) ? " (scoped)" : "");

		if (i == 1) {
			/* Access 2 */
			cur = expect[2];
			end = &expect[2][sizeof(expect[2]) - 1];

			if (!r->access[1].fn) {
				/* Dummy string if no second access is available. */
				strcpy(cur, "<none>");
				break;
			}
		}

		cur += scnprintf(cur, end - cur, "%s%s to ", access_type,
				 access_type_aux);

		if (r->access[i].addr) /* Address is optional. */
			cur += scnprintf(cur, end - cur, "0x%px of %zu bytes",
					 r->access[i].addr, r->access[i].size);
	}

	spin_lock_irqsave(&observed.lock, flags);
	if (!report_available())
		goto out; /* A new report is being captured. */

	/* Finally match expected output to what we actually observed. */
	ret = strstr(observed.lines[0], expect[0]) &&
	      /* Access info may appear in any order. */
	      ((strstr(observed.lines[1], expect[1]) &&
		strstr(observed.lines[2], expect[2])) ||
	       (strstr(observed.lines[1], expect[2]) &&
		strstr(observed.lines[2], expect[1])));
out:
	spin_unlock_irqrestore(&observed.lock, flags);
	return ret;
}

/* ===== Test kernels ===== */

static long test_sink;
static long test_var;
/* @test_array should be large enough to fall into multiple watchpoint slots. */
static long test_array[3 * PAGE_SIZE / sizeof(long)];
static struct {
	long val[8];
} test_struct;
static DEFINE_SEQLOCK(test_seqlock);

/*
 * Helper to avoid compiler optimizing out reads, and to generate source values
 * for writes.
 */
__no_kcsan
static noinline void sink_value(long v) { WRITE_ONCE(test_sink, v); }

static noinline void test_kernel_read(void) { sink_value(test_var); }

static noinline void test_kernel_write(void)
{
	test_var = READ_ONCE_NOCHECK(test_sink) + 1;
}

static noinline void test_kernel_write_nochange(void) { test_var = 42; }

/* Suffixed by value-change exception filter. */
static noinline void test_kernel_write_nochange_rcu(void) { test_var = 42; }

static noinline void test_kernel_read_atomic(void)
{
	sink_value(READ_ONCE(test_var));
}

static noinline void test_kernel_write_atomic(void)
{
	WRITE_ONCE(test_var, READ_ONCE_NOCHECK(test_sink) + 1);
}

static noinline void test_kernel_atomic_rmw(void)
{
	/* Use builtin, so we can set up the "bad" atomic/non-atomic scenario. */
	__atomic_fetch_add(&test_var, 1, __ATOMIC_RELAXED);
}

__no_kcsan
static noinline void test_kernel_write_uninstrumented(void) { test_var++; }

static noinline void test_kernel_data_race(void) { data_race(test_var++); }

static noinline void test_kernel_assert_writer(void)
{
	ASSERT_EXCLUSIVE_WRITER(test_var);
}

static noinline void test_kernel_assert_access(void)
{
	ASSERT_EXCLUSIVE_ACCESS(test_var);
}

#define TEST_CHANGE_BITS 0xff00ff00

static noinline void test_kernel_change_bits(void)
{
	if (IS_ENABLED(CONFIG_KCSAN_IGNORE_ATOMICS)) {
		/*
		 * Avoid race of unknown origin for this test, just pretend they
		 * are atomic.
		 */
		kcsan_nestable_atomic_begin();
		test_var ^= TEST_CHANGE_BITS;
		kcsan_nestable_atomic_end();
	} else
		WRITE_ONCE(test_var, READ_ONCE(test_var) ^ TEST_CHANGE_BITS);
}

static noinline void test_kernel_assert_bits_change(void)
{
	ASSERT_EXCLUSIVE_BITS(test_var, TEST_CHANGE_BITS);
}

static noinline void test_kernel_assert_bits_nochange(void)
{
	ASSERT_EXCLUSIVE_BITS(test_var, ~TEST_CHANGE_BITS);
}

/* To check that scoped assertions do trigger anywhere in scope. */
static noinline void test_enter_scope(void)
{
	int x = 0;

	/* Unrelated accesses to scoped assert. */
	READ_ONCE(test_sink);
	kcsan_check_read(&x, sizeof(x));
}

static noinline void test_kernel_assert_writer_scoped(void)
{
	ASSERT_EXCLUSIVE_WRITER_SCOPED(test_var);
	test_enter_scope();
}

static noinline void test_kernel_assert_access_scoped(void)
{
	ASSERT_EXCLUSIVE_ACCESS_SCOPED(test_var);
	test_enter_scope();
}

static noinline void test_kernel_rmw_array(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(test_array); ++i)
		test_array[i]++;
}

static noinline void test_kernel_write_struct(void)
{
	kcsan_check_write(&test_struct, sizeof(test_struct));
	kcsan_disable_current();
	test_struct.val[3]++; /* induce value change */
	kcsan_enable_current();
}

static noinline void test_kernel_write_struct_part(void)
{
	test_struct.val[3] = 42;
}

static noinline void test_kernel_read_struct_zero_size(void)
{
	kcsan_check_read(&test_struct.val[3], 0);
}

static noinline void test_kernel_jiffies_reader(void)
{
	sink_value((long)jiffies);
}

static noinline void test_kernel_seqlock_reader(void)
{
	unsigned int seq;

	do {
		seq = read_seqbegin(&test_seqlock);
		sink_value(test_var);
	} while (read_seqretry(&test_seqlock, seq));
}

static noinline void test_kernel_seqlock_writer(void)
{
	unsigned long flags;

	write_seqlock_irqsave(&test_seqlock, flags);
	test_var++;
	write_sequnlock_irqrestore(&test_seqlock, flags);
}

static noinline void test_kernel_atomic_builtins(void)
{
	/*
	 * Generate concurrent accesses, expecting no reports, ensuring KCSAN
	 * treats builtin atomics as actually atomic.
	 */
	__atomic_load_n(&test_var, __ATOMIC_RELAXED);
}

/* ===== Test cases ===== */

/* Simple test with normal data race. */
__no_kcsan
static void test_basic(struct kunit *test)
{
	const struct expect_report expect = {
		.access = {
			{ test_kernel_write, &test_var, sizeof(test_var), KCSAN_ACCESS_WRITE },
			{ test_kernel_read, &test_var, sizeof(test_var), 0 },
		},
	};
	static const struct expect_report never = {
		.access = {
			{ test_kernel_read, &test_var, sizeof(test_var), 0 },
			{ test_kernel_read, &test_var, sizeof(test_var), 0 },
		},
	};
	bool match_expect = false;
	bool match_never = false;

	begin_test_checks(test_kernel_write, test_kernel_read);
	do {
		match_expect |= report_matches(&expect);
		match_never = report_matches(&never);
	} while (!end_test_checks(match_never));
	KUNIT_EXPECT_TRUE(test, match_expect);
	KUNIT_EXPECT_FALSE(test, match_never);
}

/*
 * Stress KCSAN with lots of concurrent races on different addresses until
 * timeout.
 */
__no_kcsan
static void test_concurrent_races(struct kunit *test)
{
	const struct expect_report expect = {
		.access = {
			/* NULL will match any address. */
			{ test_kernel_rmw_array, NULL, 0, __KCSAN_ACCESS_RW(KCSAN_ACCESS_WRITE) },
			{ test_kernel_rmw_array, NULL, 0, __KCSAN_ACCESS_RW(0) },
		},
	};
	static const struct expect_report never = {
		.access = {
			{ test_kernel_rmw_array, NULL, 0, 0 },
			{ test_kernel_rmw_array, NULL, 0, 0 },
		},
	};
	bool match_expect = false;
	bool match_never = false;

	begin_test_checks(test_kernel_rmw_array, test_kernel_rmw_array);
	do {
		match_expect |= report_matches(&expect);
		match_never |= report_matches(&never);
	} while (!end_test_checks(false));
	KUNIT_EXPECT_TRUE(test, match_expect); /* Sanity check matches exist. */
	KUNIT_EXPECT_FALSE(test, match_never);
}

/* Test the KCSAN_REPORT_VALUE_CHANGE_ONLY option. */
__no_kcsan
static void test_novalue_change(struct kunit *test)
{
	const struct expect_report expect = {
		.access = {
			{ test_kernel_write_nochange, &test_var, sizeof(test_var), KCSAN_ACCESS_WRITE },
			{ test_kernel_read, &test_var, sizeof(test_var), 0 },
		},
	};
	bool match_expect = false;

	begin_test_checks(test_kernel_write_nochange, test_kernel_read);
	do {
		match_expect = report_matches(&expect);
	} while (!end_test_checks(match_expect));
	if (IS_ENABLED(CONFIG_KCSAN_REPORT_VALUE_CHANGE_ONLY))
		KUNIT_EXPECT_FALSE(test, match_expect);
	else
		KUNIT_EXPECT_TRUE(test, match_expect);
}

/*
 * Test that the rules where the KCSAN_REPORT_VALUE_CHANGE_ONLY option should
 * never apply work.
 */
__no_kcsan
static void test_novalue_change_exception(struct kunit *test)
{
	const struct expect_report expect = {
		.access = {
			{ test_kernel_write_nochange_rcu, &test_var, sizeof(test_var), KCSAN_ACCESS_WRITE },
			{ test_kernel_read, &test_var, sizeof(test_var), 0 },
		},
	};
	bool match_expect = false;

	begin_test_checks(test_kernel_write_nochange_rcu, test_kernel_read);
	do {
		match_expect = report_matches(&expect);
	} while (!end_test_checks(match_expect));
	KUNIT_EXPECT_TRUE(test, match_expect);
}

/* Test that data races of unknown origin are reported. */
__no_kcsan
static void test_unknown_origin(struct kunit *test)
{
	const struct expect_report expect = {
		.access = {
			{ test_kernel_read, &test_var, sizeof(test_var), 0 },
			{ NULL },
		},
	};
	bool match_expect = false;

	begin_test_checks(test_kernel_write_uninstrumented, test_kernel_read);
	do {
		match_expect = report_matches(&expect);
	} while (!end_test_checks(match_expect));
	if (IS_ENABLED(CONFIG_KCSAN_REPORT_RACE_UNKNOWN_ORIGIN))
		KUNIT_EXPECT_TRUE(test, match_expect);
	else
		KUNIT_EXPECT_FALSE(test, match_expect);
}

/* Test KCSAN_ASSUME_PLAIN_WRITES_ATOMIC if it is selected. */
__no_kcsan
static void test_write_write_assume_atomic(struct kunit *test)
{
	const struct expect_report expect = {
		.access = {
			{ test_kernel_write, &test_var, sizeof(test_var), KCSAN_ACCESS_WRITE },
			{ test_kernel_write, &test_var, sizeof(test_var), KCSAN_ACCESS_WRITE },
		},
	};
	bool match_expect = false;

	begin_test_checks(test_kernel_write, test_kernel_write);
	do {
		sink_value(READ_ONCE(test_var)); /* induce value-change */
		match_expect = report_matches(&expect);
	} while (!end_test_checks(match_expect));
	if (IS_ENABLED(CONFIG_KCSAN_ASSUME_PLAIN_WRITES_ATOMIC))
		KUNIT_EXPECT_FALSE(test, match_expect);
	else
		KUNIT_EXPECT_TRUE(test, match_expect);
}

/*
 * Test that data races with writes larger than word-size are always reported,
 * even if KCSAN_ASSUME_PLAIN_WRITES_ATOMIC is selected.
 */
__no_kcsan
static void test_write_write_struct(struct kunit *test)
{
	const struct expect_report expect = {
		.access = {
			{ test_kernel_write_struct, &test_struct, sizeof(test_struct), KCSAN_ACCESS_WRITE },
			{ test_kernel_write_struct, &test_struct, sizeof(test_struct), KCSAN_ACCESS_WRITE },
		},
	};
	bool match_expect = false;

	begin_test_checks(test_kernel_write_struct, test_kernel_write_struct);
	do {
		match_expect = report_matches(&expect);
	} while (!end_test_checks(match_expect));
	KUNIT_EXPECT_TRUE(test, match_expect);
}

/*
 * Test that data races where only one write is larger than word-size are always
 * reported, even if KCSAN_ASSUME_PLAIN_WRITES_ATOMIC is selected.
 */
__no_kcsan
static void test_write_write_struct_part(struct kunit *test)
{
	const struct expect_report expect = {
		.access = {
			{ test_kernel_write_struct, &test_struct, sizeof(test_struct), KCSAN_ACCESS_WRITE },
			{ test_kernel_write_struct_part, &test_struct.val[3], sizeof(test_struct.val[3]), KCSAN_ACCESS_WRITE },
		},
	};
	bool match_expect = false;

	begin_test_checks(test_kernel_write_struct, test_kernel_write_struct_part);
	do {
		match_expect = report_matches(&expect);
	} while (!end_test_checks(match_expect));
	KUNIT_EXPECT_TRUE(test, match_expect);
}

/* Test that races with atomic accesses never result in reports. */
__no_kcsan
static void test_read_atomic_write_atomic(struct kunit *test)
{
	bool match_never = false;

	begin_test_checks(test_kernel_read_atomic, test_kernel_write_atomic);
	do {
		match_never = report_available();
	} while (!end_test_checks(match_never));
	KUNIT_EXPECT_FALSE(test, match_never);
}

/* Test that a race with an atomic and plain access result in reports. */
__no_kcsan
static void test_read_plain_atomic_write(struct kunit *test)
{
	const struct expect_report expect = {
		.access = {
			{ test_kernel_read, &test_var, sizeof(test_var), 0 },
			{ test_kernel_write_atomic, &test_var, sizeof(test_var), KCSAN_ACCESS_WRITE | KCSAN_ACCESS_ATOMIC },
		},
	};
	bool match_expect = false;

	if (IS_ENABLED(CONFIG_KCSAN_IGNORE_ATOMICS))
		return;

	begin_test_checks(test_kernel_read, test_kernel_write_atomic);
	do {
		match_expect = report_matches(&expect);
	} while (!end_test_checks(match_expect));
	KUNIT_EXPECT_TRUE(test, match_expect);
}

/* Test that atomic RMWs generate correct report. */
__no_kcsan
static void test_read_plain_atomic_rmw(struct kunit *test)
{
	const struct expect_report expect = {
		.access = {
			{ test_kernel_read, &test_var, sizeof(test_var), 0 },
			{ test_kernel_atomic_rmw, &test_var, sizeof(test_var),
				KCSAN_ACCESS_COMPOUND | KCSAN_ACCESS_WRITE | KCSAN_ACCESS_ATOMIC },
		},
	};
	bool match_expect = false;

	if (IS_ENABLED(CONFIG_KCSAN_IGNORE_ATOMICS))
		return;

	begin_test_checks(test_kernel_read, test_kernel_atomic_rmw);
	do {
		match_expect = report_matches(&expect);
	} while (!end_test_checks(match_expect));
	KUNIT_EXPECT_TRUE(test, match_expect);
}

/* Zero-sized accesses should never cause data race reports. */
__no_kcsan
static void test_zero_size_access(struct kunit *test)
{
	const struct expect_report expect = {
		.access = {
			{ test_kernel_write_struct, &test_struct, sizeof(test_struct), KCSAN_ACCESS_WRITE },
			{ test_kernel_write_struct, &test_struct, sizeof(test_struct), KCSAN_ACCESS_WRITE },
		},
	};
	const struct expect_report never = {
		.access = {
			{ test_kernel_write_struct, &test_struct, sizeof(test_struct), KCSAN_ACCESS_WRITE },
			{ test_kernel_read_struct_zero_size, &test_struct.val[3], 0, 0 },
		},
	};
	bool match_expect = false;
	bool match_never = false;

	begin_test_checks(test_kernel_write_struct, test_kernel_read_struct_zero_size);
	do {
		match_expect |= report_matches(&expect);
		match_never = report_matches(&never);
	} while (!end_test_checks(match_never));
	KUNIT_EXPECT_TRUE(test, match_expect); /* Sanity check. */
	KUNIT_EXPECT_FALSE(test, match_never);
}

/* Test the data_race() macro. */
__no_kcsan
static void test_data_race(struct kunit *test)
{
	bool match_never = false;

	begin_test_checks(test_kernel_data_race, test_kernel_data_race);
	do {
		match_never = report_available();
	} while (!end_test_checks(match_never));
	KUNIT_EXPECT_FALSE(test, match_never);
}

__no_kcsan
static void test_assert_exclusive_writer(struct kunit *test)
{
	const struct expect_report expect = {
		.access = {
			{ test_kernel_assert_writer, &test_var, sizeof(test_var), KCSAN_ACCESS_ASSERT },
			{ test_kernel_write_nochange, &test_var, sizeof(test_var), KCSAN_ACCESS_WRITE },
		},
	};
	bool match_expect = false;

	begin_test_checks(test_kernel_assert_writer, test_kernel_write_nochange);
	do {
		match_expect = report_matches(&expect);
	} while (!end_test_checks(match_expect));
	KUNIT_EXPECT_TRUE(test, match_expect);
}

__no_kcsan
static void test_assert_exclusive_access(struct kunit *test)
{
	const struct expect_report expect = {
		.access = {
			{ test_kernel_assert_access, &test_var, sizeof(test_var), KCSAN_ACCESS_ASSERT | KCSAN_ACCESS_WRITE },
			{ test_kernel_read, &test_var, sizeof(test_var), 0 },
		},
	};
	bool match_expect = false;

	begin_test_checks(test_kernel_assert_access, test_kernel_read);
	do {
		match_expect = report_matches(&expect);
	} while (!end_test_checks(match_expect));
	KUNIT_EXPECT_TRUE(test, match_expect);
}

__no_kcsan
static void test_assert_exclusive_access_writer(struct kunit *test)
{
	const struct expect_report expect_access_writer = {
		.access = {
			{ test_kernel_assert_access, &test_var, sizeof(test_var), KCSAN_ACCESS_ASSERT | KCSAN_ACCESS_WRITE },
			{ test_kernel_assert_writer, &test_var, sizeof(test_var), KCSAN_ACCESS_ASSERT },
		},
	};
	const struct expect_report expect_access_access = {
		.access = {
			{ test_kernel_assert_access, &test_var, sizeof(test_var), KCSAN_ACCESS_ASSERT | KCSAN_ACCESS_WRITE },
			{ test_kernel_assert_access, &test_var, sizeof(test_var), KCSAN_ACCESS_ASSERT | KCSAN_ACCESS_WRITE },
		},
	};
	const struct expect_report never = {
		.access = {
			{ test_kernel_assert_writer, &test_var, sizeof(test_var), KCSAN_ACCESS_ASSERT },
			{ test_kernel_assert_writer, &test_var, sizeof(test_var), KCSAN_ACCESS_ASSERT },
		},
	};
	bool match_expect_access_writer = false;
	bool match_expect_access_access = false;
	bool match_never = false;

	begin_test_checks(test_kernel_assert_access, test_kernel_assert_writer);
	do {
		match_expect_access_writer |= report_matches(&expect_access_writer);
		match_expect_access_access |= report_matches(&expect_access_access);
		match_never |= report_matches(&never);
	} while (!end_test_checks(match_never));
	KUNIT_EXPECT_TRUE(test, match_expect_access_writer);
	KUNIT_EXPECT_TRUE(test, match_expect_access_access);
	KUNIT_EXPECT_FALSE(test, match_never);
}

__no_kcsan
static void test_assert_exclusive_bits_change(struct kunit *test)
{
	const struct expect_report expect = {
		.access = {
			{ test_kernel_assert_bits_change, &test_var, sizeof(test_var), KCSAN_ACCESS_ASSERT },
			{ test_kernel_change_bits, &test_var, sizeof(test_var),
				KCSAN_ACCESS_WRITE | (IS_ENABLED(CONFIG_KCSAN_IGNORE_ATOMICS) ? 0 : KCSAN_ACCESS_ATOMIC) },
		},
	};
	bool match_expect = false;

	begin_test_checks(test_kernel_assert_bits_change, test_kernel_change_bits);
	do {
		match_expect = report_matches(&expect);
	} while (!end_test_checks(match_expect));
	KUNIT_EXPECT_TRUE(test, match_expect);
}

__no_kcsan
static void test_assert_exclusive_bits_nochange(struct kunit *test)
{
	bool match_never = false;

	begin_test_checks(test_kernel_assert_bits_nochange, test_kernel_change_bits);
	do {
		match_never = report_available();
	} while (!end_test_checks(match_never));
	KUNIT_EXPECT_FALSE(test, match_never);
}

__no_kcsan
static void test_assert_exclusive_writer_scoped(struct kunit *test)
{
	const struct expect_report expect_start = {
		.access = {
			{ test_kernel_assert_writer_scoped, &test_var, sizeof(test_var), KCSAN_ACCESS_ASSERT | KCSAN_ACCESS_SCOPED },
			{ test_kernel_write_nochange, &test_var, sizeof(test_var), KCSAN_ACCESS_WRITE },
		},
	};
	const struct expect_report expect_anywhere = {
		.access = {
			{ test_enter_scope, &test_var, sizeof(test_var), KCSAN_ACCESS_ASSERT | KCSAN_ACCESS_SCOPED },
			{ test_kernel_write_nochange, &test_var, sizeof(test_var), KCSAN_ACCESS_WRITE },
		},
	};
	bool match_expect_start = false;
	bool match_expect_anywhere = false;

	begin_test_checks(test_kernel_assert_writer_scoped, test_kernel_write_nochange);
	do {
		match_expect_start |= report_matches(&expect_start);
		match_expect_anywhere |= report_matches(&expect_anywhere);
	} while (!end_test_checks(match_expect_start && match_expect_anywhere));
	KUNIT_EXPECT_TRUE(test, match_expect_start);
	KUNIT_EXPECT_TRUE(test, match_expect_anywhere);
}

__no_kcsan
static void test_assert_exclusive_access_scoped(struct kunit *test)
{
	const struct expect_report expect_start1 = {
		.access = {
			{ test_kernel_assert_access_scoped, &test_var, sizeof(test_var), KCSAN_ACCESS_ASSERT | KCSAN_ACCESS_WRITE | KCSAN_ACCESS_SCOPED },
			{ test_kernel_read, &test_var, sizeof(test_var), 0 },
		},
	};
	const struct expect_report expect_start2 = {
		.access = { expect_start1.access[0], expect_start1.access[0] },
	};
	const struct expect_report expect_inscope = {
		.access = {
			{ test_enter_scope, &test_var, sizeof(test_var), KCSAN_ACCESS_ASSERT | KCSAN_ACCESS_WRITE | KCSAN_ACCESS_SCOPED },
			{ test_kernel_read, &test_var, sizeof(test_var), 0 },
		},
	};
	bool match_expect_start = false;
	bool match_expect_inscope = false;

	begin_test_checks(test_kernel_assert_access_scoped, test_kernel_read);
	end_time += msecs_to_jiffies(1000); /* This test requires a bit more time. */
	do {
		match_expect_start |= report_matches(&expect_start1) || report_matches(&expect_start2);
		match_expect_inscope |= report_matches(&expect_inscope);
	} while (!end_test_checks(match_expect_start && match_expect_inscope));
	KUNIT_EXPECT_TRUE(test, match_expect_start);
	KUNIT_EXPECT_TRUE(test, match_expect_inscope);
}

/*
 * jiffies is special (declared to be volatile) and its accesses are typically
 * not marked; this test ensures that the compiler nor KCSAN gets confused about
 * jiffies's declaration on different architectures.
 */
__no_kcsan
static void test_jiffies_noreport(struct kunit *test)
{
	bool match_never = false;

	begin_test_checks(test_kernel_jiffies_reader, test_kernel_jiffies_reader);
	do {
		match_never = report_available();
	} while (!end_test_checks(match_never));
	KUNIT_EXPECT_FALSE(test, match_never);
}

/* Test that racing accesses in seqlock critical sections are not reported. */
__no_kcsan
static void test_seqlock_noreport(struct kunit *test)
{
	bool match_never = false;

	begin_test_checks(test_kernel_seqlock_reader, test_kernel_seqlock_writer);
	do {
		match_never = report_available();
	} while (!end_test_checks(match_never));
	KUNIT_EXPECT_FALSE(test, match_never);
}

/*
 * Test atomic builtins work and required instrumentation functions exist. We
 * also test that KCSAN understands they're atomic by racing with them via
 * test_kernel_atomic_builtins(), and expect no reports.
 *
 * The atomic builtins _SHOULD NOT_ be used in normal kernel code!
 */
static void test_atomic_builtins(struct kunit *test)
{
	bool match_never = false;

	begin_test_checks(test_kernel_atomic_builtins, test_kernel_atomic_builtins);
	do {
		long tmp;

		kcsan_enable_current();

		__atomic_store_n(&test_var, 42L, __ATOMIC_RELAXED);
		KUNIT_EXPECT_EQ(test, 42L, __atomic_load_n(&test_var, __ATOMIC_RELAXED));

		KUNIT_EXPECT_EQ(test, 42L, __atomic_exchange_n(&test_var, 20, __ATOMIC_RELAXED));
		KUNIT_EXPECT_EQ(test, 20L, test_var);

		tmp = 20L;
		KUNIT_EXPECT_TRUE(test, __atomic_compare_exchange_n(&test_var, &tmp, 30L,
								    0, __ATOMIC_RELAXED,
								    __ATOMIC_RELAXED));
		KUNIT_EXPECT_EQ(test, tmp, 20L);
		KUNIT_EXPECT_EQ(test, test_var, 30L);
		KUNIT_EXPECT_FALSE(test, __atomic_compare_exchange_n(&test_var, &tmp, 40L,
								     1, __ATOMIC_RELAXED,
								     __ATOMIC_RELAXED));
		KUNIT_EXPECT_EQ(test, tmp, 30L);
		KUNIT_EXPECT_EQ(test, test_var, 30L);

		KUNIT_EXPECT_EQ(test, 30L, __atomic_fetch_add(&test_var, 1, __ATOMIC_RELAXED));
		KUNIT_EXPECT_EQ(test, 31L, __atomic_fetch_sub(&test_var, 1, __ATOMIC_RELAXED));
		KUNIT_EXPECT_EQ(test, 30L, __atomic_fetch_and(&test_var, 0xf, __ATOMIC_RELAXED));
		KUNIT_EXPECT_EQ(test, 14L, __atomic_fetch_xor(&test_var, 0xf, __ATOMIC_RELAXED));
		KUNIT_EXPECT_EQ(test, 1L, __atomic_fetch_or(&test_var, 0xf0, __ATOMIC_RELAXED));
		KUNIT_EXPECT_EQ(test, 241L, __atomic_fetch_nand(&test_var, 0xf, __ATOMIC_RELAXED));
		KUNIT_EXPECT_EQ(test, -2L, test_var);

		__atomic_thread_fence(__ATOMIC_SEQ_CST);
		__atomic_signal_fence(__ATOMIC_SEQ_CST);

		kcsan_disable_current();

		match_never = report_available();
	} while (!end_test_checks(match_never));
	KUNIT_EXPECT_FALSE(test, match_never);
}

/*
 * Generate thread counts for all test cases. Values generated are in interval
 * [2, 5] followed by exponentially increasing thread counts from 8 to 32.
 *
 * The thread counts are chosen to cover potentially interesting boundaries and
 * corner cases (2 to 5), and then stress the system with larger counts.
 */
static const void *nthreads_gen_params(const void *prev, char *desc)
{
	long nthreads = (long)prev;

	if (nthreads < 0 || nthreads >= 32)
		nthreads = 0; /* stop */
	else if (!nthreads)
		nthreads = 2; /* initial value */
	else if (nthreads < 5)
		nthreads++;
	else if (nthreads == 5)
		nthreads = 8;
	else
		nthreads *= 2;

	if (!IS_ENABLED(CONFIG_PREEMPT) || !IS_ENABLED(CONFIG_KCSAN_INTERRUPT_WATCHER)) {
		/*
		 * Without any preemption, keep 2 CPUs free for other tasks, one
		 * of which is the main test case function checking for
		 * completion or failure.
		 */
		const long min_unused_cpus = IS_ENABLED(CONFIG_PREEMPT_NONE) ? 2 : 0;
		const long min_required_cpus = 2 + min_unused_cpus;

		if (num_online_cpus() < min_required_cpus) {
			pr_err_once("Too few online CPUs (%u < %ld) for test\n",
				    num_online_cpus(), min_required_cpus);
			nthreads = 0;
		} else if (nthreads >= num_online_cpus() - min_unused_cpus) {
			/* Use negative value to indicate last param. */
			nthreads = -(num_online_cpus() - min_unused_cpus);
			pr_warn_once("Limiting number of threads to %ld (only %d online CPUs)\n",
				     -nthreads, num_online_cpus());
		}
	}

	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "threads=%ld", abs(nthreads));
	return (void *)nthreads;
}

#define KCSAN_KUNIT_CASE(test_name) KUNIT_CASE_PARAM(test_name, nthreads_gen_params)
static struct kunit_case kcsan_test_cases[] = {
	KCSAN_KUNIT_CASE(test_basic),
	KCSAN_KUNIT_CASE(test_concurrent_races),
	KCSAN_KUNIT_CASE(test_novalue_change),
	KCSAN_KUNIT_CASE(test_novalue_change_exception),
	KCSAN_KUNIT_CASE(test_unknown_origin),
	KCSAN_KUNIT_CASE(test_write_write_assume_atomic),
	KCSAN_KUNIT_CASE(test_write_write_struct),
	KCSAN_KUNIT_CASE(test_write_write_struct_part),
	KCSAN_KUNIT_CASE(test_read_atomic_write_atomic),
	KCSAN_KUNIT_CASE(test_read_plain_atomic_write),
	KCSAN_KUNIT_CASE(test_read_plain_atomic_rmw),
	KCSAN_KUNIT_CASE(test_zero_size_access),
	KCSAN_KUNIT_CASE(test_data_race),
	KCSAN_KUNIT_CASE(test_assert_exclusive_writer),
	KCSAN_KUNIT_CASE(test_assert_exclusive_access),
	KCSAN_KUNIT_CASE(test_assert_exclusive_access_writer),
	KCSAN_KUNIT_CASE(test_assert_exclusive_bits_change),
	KCSAN_KUNIT_CASE(test_assert_exclusive_bits_nochange),
	KCSAN_KUNIT_CASE(test_assert_exclusive_writer_scoped),
	KCSAN_KUNIT_CASE(test_assert_exclusive_access_scoped),
	KCSAN_KUNIT_CASE(test_jiffies_noreport),
	KCSAN_KUNIT_CASE(test_seqlock_noreport),
	KCSAN_KUNIT_CASE(test_atomic_builtins),
	{},
};

/* ===== End test cases ===== */

/* Concurrent accesses from interrupts. */
__no_kcsan
static void access_thread_timer(struct timer_list *timer)
{
	static atomic_t cnt = ATOMIC_INIT(0);
	unsigned int idx;
	void (*func)(void);

	idx = (unsigned int)atomic_inc_return(&cnt) % ARRAY_SIZE(access_kernels);
	/* Acquire potential initialization. */
	func = smp_load_acquire(&access_kernels[idx]);
	if (func)
		func();
}

/* The main loop for each thread. */
__no_kcsan
static int access_thread(void *arg)
{
	struct timer_list timer;
	unsigned int cnt = 0;
	unsigned int idx;
	void (*func)(void);

	timer_setup_on_stack(&timer, access_thread_timer, 0);
	do {
		might_sleep();

		if (!timer_pending(&timer))
			mod_timer(&timer, jiffies + 1);
		else {
			/* Iterate through all kernels. */
			idx = cnt++ % ARRAY_SIZE(access_kernels);
			/* Acquire potential initialization. */
			func = smp_load_acquire(&access_kernels[idx]);
			if (func)
				func();
		}
	} while (!torture_must_stop());
	del_timer_sync(&timer);
	destroy_timer_on_stack(&timer);

	torture_kthread_stopping("access_thread");
	return 0;
}

__no_kcsan
static int test_init(struct kunit *test)
{
	unsigned long flags;
	int nthreads;
	int i;

	spin_lock_irqsave(&observed.lock, flags);
	for (i = 0; i < ARRAY_SIZE(observed.lines); ++i)
		observed.lines[i][0] = '\0';
	observed.nlines = 0;
	spin_unlock_irqrestore(&observed.lock, flags);

	if (!torture_init_begin((char *)test->name, 1))
		return -EBUSY;

	if (WARN_ON(threads))
		goto err;

	for (i = 0; i < ARRAY_SIZE(access_kernels); ++i) {
		if (WARN_ON(access_kernels[i]))
			goto err;
	}

	nthreads = abs((long)test->param_value);
	if (WARN_ON(!nthreads))
		goto err;

	threads = kcalloc(nthreads + 1, sizeof(struct task_struct *), GFP_KERNEL);
	if (WARN_ON(!threads))
		goto err;

	threads[nthreads] = NULL;
	for (i = 0; i < nthreads; ++i) {
		if (torture_create_kthread(access_thread, NULL, threads[i]))
			goto err;
	}

	torture_init_end();

	return 0;

err:
	kfree(threads);
	threads = NULL;
	torture_init_end();
	return -EINVAL;
}

__no_kcsan
static void test_exit(struct kunit *test)
{
	struct task_struct **stop_thread;
	int i;

	if (torture_cleanup_begin())
		return;

	for (i = 0; i < ARRAY_SIZE(access_kernels); ++i)
		WRITE_ONCE(access_kernels[i], NULL);

	if (threads) {
		for (stop_thread = threads; *stop_thread; stop_thread++)
			torture_stop_kthread(reader_thread, *stop_thread);

		kfree(threads);
		threads = NULL;
	}

	torture_cleanup_end();
}

static struct kunit_suite kcsan_test_suite = {
	.name = "kcsan",
	.test_cases = kcsan_test_cases,
	.init = test_init,
	.exit = test_exit,
};
static struct kunit_suite *kcsan_test_suites[] = { &kcsan_test_suite, NULL };

__no_kcsan
static void register_tracepoints(struct tracepoint *tp, void *ignore)
{
	check_trace_callback_type_console(probe_console);
	if (!strcmp(tp->name, "console"))
		WARN_ON(tracepoint_probe_register(tp, probe_console, NULL));
}

__no_kcsan
static void unregister_tracepoints(struct tracepoint *tp, void *ignore)
{
	if (!strcmp(tp->name, "console"))
		tracepoint_probe_unregister(tp, probe_console, NULL);
}

/*
 * We only want to do tracepoints setup and teardown once, therefore we have to
 * customize the init and exit functions and cannot rely on kunit_test_suite().
 */
static int __init kcsan_test_init(void)
{
	/*
	 * Because we want to be able to build the test as a module, we need to
	 * iterate through all known tracepoints, since the static registration
	 * won't work here.
	 */
	for_each_kernel_tracepoint(register_tracepoints, NULL);
	return __kunit_test_suites_init(kcsan_test_suites);
}

static void kcsan_test_exit(void)
{
	__kunit_test_suites_exit(kcsan_test_suites);
	for_each_kernel_tracepoint(unregister_tracepoints, NULL);
	tracepoint_synchronize_unregister();
}

late_initcall(kcsan_test_init);
module_exit(kcsan_test_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Marco Elver <elver@google.com>");
