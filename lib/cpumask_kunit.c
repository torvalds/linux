// SPDX-License-Identifier: GPL-2.0-only
/*
 * KUnit tests for cpumask.
 *
 * Author: Sander Vanheule <sander@svanheule.net>
 */

#include <kunit/test.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>

#define MASK_MSG(m) \
	"%s contains %sCPUs %*pbl", #m, (cpumask_weight(m) ? "" : "no "), \
	nr_cpumask_bits, cpumask_bits(m)

#define EXPECT_FOR_EACH_CPU_EQ(test, mask)			\
	do {							\
		const cpumask_t *m = (mask);			\
		int mask_weight = cpumask_weight(m);		\
		int cpu, iter = 0;				\
		for_each_cpu(cpu, m)				\
			iter++;					\
		KUNIT_EXPECT_EQ_MSG((test), mask_weight, iter, MASK_MSG(mask));	\
	} while (0)

#define EXPECT_FOR_EACH_CPU_NOT_EQ(test, mask)					\
	do {									\
		const cpumask_t *m = (mask);					\
		int mask_weight = cpumask_weight(m);				\
		int cpu, iter = 0;						\
		for_each_cpu_not(cpu, m)					\
			iter++;							\
		KUNIT_EXPECT_EQ_MSG((test), nr_cpu_ids - mask_weight, iter, MASK_MSG(mask));	\
	} while (0)

#define EXPECT_FOR_EACH_CPU_OP_EQ(test, op, mask1, mask2)			\
	do {									\
		const cpumask_t *m1 = (mask1);					\
		const cpumask_t *m2 = (mask2);					\
		int weight;                                                     \
		int cpu, iter = 0;						\
		cpumask_##op(&mask_tmp, m1, m2);                                \
		weight = cpumask_weight(&mask_tmp);				\
		for_each_cpu_##op(cpu, mask1, mask2)				\
			iter++;							\
		KUNIT_EXPECT_EQ((test), weight, iter);				\
	} while (0)

#define EXPECT_FOR_EACH_CPU_WRAP_EQ(test, mask)			\
	do {							\
		const cpumask_t *m = (mask);			\
		int mask_weight = cpumask_weight(m);		\
		int cpu, iter = 0;				\
		for_each_cpu_wrap(cpu, m, nr_cpu_ids / 2)	\
			iter++;					\
		KUNIT_EXPECT_EQ_MSG((test), mask_weight, iter, MASK_MSG(mask));	\
	} while (0)

#define EXPECT_FOR_EACH_CPU_BUILTIN_EQ(test, name)		\
	do {							\
		int mask_weight = num_##name##_cpus();		\
		int cpu, iter = 0;				\
		for_each_##name##_cpu(cpu)			\
			iter++;					\
		KUNIT_EXPECT_EQ_MSG((test), mask_weight, iter, MASK_MSG(cpu_##name##_mask));	\
	} while (0)

static cpumask_t mask_empty;
static cpumask_t mask_all;
static cpumask_t mask_tmp;

static void test_cpumask_weight(struct kunit *test)
{
	KUNIT_EXPECT_TRUE_MSG(test, cpumask_empty(&mask_empty), MASK_MSG(&mask_empty));
	KUNIT_EXPECT_TRUE_MSG(test, cpumask_full(&mask_all), MASK_MSG(&mask_all));

	KUNIT_EXPECT_EQ_MSG(test, 0, cpumask_weight(&mask_empty), MASK_MSG(&mask_empty));
	KUNIT_EXPECT_EQ_MSG(test, nr_cpu_ids, cpumask_weight(cpu_possible_mask),
			    MASK_MSG(cpu_possible_mask));
	KUNIT_EXPECT_EQ_MSG(test, nr_cpumask_bits, cpumask_weight(&mask_all), MASK_MSG(&mask_all));
}

static void test_cpumask_first(struct kunit *test)
{
	KUNIT_EXPECT_LE_MSG(test, nr_cpu_ids, cpumask_first(&mask_empty), MASK_MSG(&mask_empty));
	KUNIT_EXPECT_EQ_MSG(test, 0, cpumask_first(cpu_possible_mask), MASK_MSG(cpu_possible_mask));

	KUNIT_EXPECT_EQ_MSG(test, 0, cpumask_first_zero(&mask_empty), MASK_MSG(&mask_empty));
	KUNIT_EXPECT_LE_MSG(test, nr_cpu_ids, cpumask_first_zero(cpu_possible_mask),
			    MASK_MSG(cpu_possible_mask));
}

static void test_cpumask_last(struct kunit *test)
{
	KUNIT_EXPECT_LE_MSG(test, nr_cpumask_bits, cpumask_last(&mask_empty),
			    MASK_MSG(&mask_empty));
	KUNIT_EXPECT_EQ_MSG(test, nr_cpu_ids - 1, cpumask_last(cpu_possible_mask),
			    MASK_MSG(cpu_possible_mask));
}

static void test_cpumask_next(struct kunit *test)
{
	KUNIT_EXPECT_EQ_MSG(test, 0, cpumask_next_zero(-1, &mask_empty), MASK_MSG(&mask_empty));
	KUNIT_EXPECT_LE_MSG(test, nr_cpu_ids, cpumask_next_zero(-1, cpu_possible_mask),
			    MASK_MSG(cpu_possible_mask));

	KUNIT_EXPECT_LE_MSG(test, nr_cpu_ids, cpumask_next(-1, &mask_empty),
			    MASK_MSG(&mask_empty));
	KUNIT_EXPECT_EQ_MSG(test, 0, cpumask_next(-1, cpu_possible_mask),
			    MASK_MSG(cpu_possible_mask));
}

static void test_cpumask_iterators(struct kunit *test)
{
	EXPECT_FOR_EACH_CPU_EQ(test, &mask_empty);
	EXPECT_FOR_EACH_CPU_NOT_EQ(test, &mask_empty);
	EXPECT_FOR_EACH_CPU_WRAP_EQ(test, &mask_empty);
	EXPECT_FOR_EACH_CPU_OP_EQ(test, and, &mask_empty, &mask_empty);
	EXPECT_FOR_EACH_CPU_OP_EQ(test, and, cpu_possible_mask, &mask_empty);
	EXPECT_FOR_EACH_CPU_OP_EQ(test, andnot, &mask_empty, &mask_empty);

	EXPECT_FOR_EACH_CPU_EQ(test, cpu_possible_mask);
	EXPECT_FOR_EACH_CPU_NOT_EQ(test, cpu_possible_mask);
	EXPECT_FOR_EACH_CPU_WRAP_EQ(test, cpu_possible_mask);
	EXPECT_FOR_EACH_CPU_OP_EQ(test, and, cpu_possible_mask, cpu_possible_mask);
	EXPECT_FOR_EACH_CPU_OP_EQ(test, andnot, cpu_possible_mask, &mask_empty);
}

static void test_cpumask_iterators_builtin(struct kunit *test)
{
	EXPECT_FOR_EACH_CPU_BUILTIN_EQ(test, possible);

	/* Ensure the dynamic masks are stable while running the tests */
	cpu_hotplug_disable();

	EXPECT_FOR_EACH_CPU_BUILTIN_EQ(test, online);
	EXPECT_FOR_EACH_CPU_BUILTIN_EQ(test, present);

	cpu_hotplug_enable();
}

static int test_cpumask_init(struct kunit *test)
{
	cpumask_clear(&mask_empty);
	cpumask_setall(&mask_all);

	return 0;
}

static struct kunit_case test_cpumask_cases[] = {
	KUNIT_CASE(test_cpumask_weight),
	KUNIT_CASE(test_cpumask_first),
	KUNIT_CASE(test_cpumask_last),
	KUNIT_CASE(test_cpumask_next),
	KUNIT_CASE(test_cpumask_iterators),
	KUNIT_CASE(test_cpumask_iterators_builtin),
	{}
};

static struct kunit_suite test_cpumask_suite = {
	.name = "cpumask",
	.init = test_cpumask_init,
	.test_cases = test_cpumask_cases,
};
kunit_test_suite(test_cpumask_suite);

MODULE_LICENSE("GPL");
