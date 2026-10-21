/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026-2029 Red Hat, Inc. Gabriele Monaco <gmonaco@redhat.com>
 *
 * Declaration of wrappers to allow mocking core functionality, like current,
 * and other testing utilities.
 * Necessary only when mocking may be needed. If the RV KUnit test is
 * enabled, the wrappers incur an additional function call overhead.
 */

#ifndef _RV_KUNIT_H
#define _RV_KUNIT_H

#if IS_ENABLED(CONFIG_RV_MONITORS_KUNIT_TEST)

#include <kunit/test.h>
#include <kunit/test-bug.h>
#include <linux/delay.h>

int rv_set_testing(struct kunit_suite *suite);
void rv_clear_testing(struct kunit_suite *suite);

#define RV_KUNIT_MAX_MOCK_TASKS 8

struct rv_kunit_ctx {
	int reactions, expected;
	int mock_task_count;
	struct task_struct *mock_tasks[RV_KUNIT_MAX_MOCK_TASKS];
};

#define RV_KUNIT_EXPECT_REACTION(test, ctx)                             \
	do {                                                            \
		KUNIT_EXPECT_EQ(test, ctx->reactions, ++ctx->expected); \
		if (ctx->reactions != ctx->expected)                    \
			ctx->expected = ctx->reactions;                 \
	} while (0)

#define RV_KUNIT_EXPECT_NO_REACTION(test, ctx)                        \
	do {                                                          \
		KUNIT_EXPECT_EQ(test, ctx->reactions, ctx->expected); \
		if (ctx->reactions != ctx->expected)                  \
			ctx->expected = ctx->reactions;               \
	} while (0)

#define RV_KUNIT_EXPECT_REACTION_HERE(test, ctx)                             \
	for (int __done = ({ RV_KUNIT_EXPECT_NO_REACTION(test, ctx); 0; });  \
	     !__done;                                                        \
	     __done = ({ RV_KUNIT_EXPECT_REACTION(test, ctx); 1; }))

struct rv_kunit_mon {
	struct rv_monitor *rv_this;
	int (*monitor_init)(void);
	void (*monitor_destroy)(void);
	bool is_per_task;
	int *task_slot;
	void (*task_reset)(struct task_struct *task);
};

void prepare_test(struct kunit *test, const struct rv_kunit_mon *mon);
void teardown_test(void *arg);
struct task_struct *rv_kunit_alloc_mock_task(struct kunit *test);

void rv_mock_current(struct task_struct *tsk);
struct task_struct *rv_get_mock_current(void);

#define rv_get_current() (unlikely(kunit_get_current_test()) ? rv_get_mock_current() : current)

#else /* !CONFIG_RV_MONITORS_KUNIT_TEST */

#define rv_get_current() current

#endif /* CONFIG_RV_MONITORS_KUNIT_TEST */
#endif /* _RV_KUNIT_H */
