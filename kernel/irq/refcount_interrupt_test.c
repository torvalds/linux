// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit test for refcounted interrupt enable/disables.
 */

#include <kunit/test.h>
#include <linux/interrupt_rc.h>

#define TEST_IRQ_ON() KUNIT_EXPECT_FALSE(test, irqs_disabled())
#define TEST_IRQ_OFF() KUNIT_EXPECT_TRUE(test, irqs_disabled())

/* ===== Test cases ===== */
static void test_single_irq_change(struct kunit *test)
{
	local_interrupt_disable();
	TEST_IRQ_OFF();
	local_interrupt_enable();
}

static void test_nested_irq_change(struct kunit *test)
{
	local_interrupt_disable();
	TEST_IRQ_OFF();
	local_interrupt_disable();
	TEST_IRQ_OFF();
	local_interrupt_disable();
	TEST_IRQ_OFF();

	local_interrupt_enable();
	TEST_IRQ_OFF();
	local_interrupt_enable();
	TEST_IRQ_OFF();
	local_interrupt_enable();
	TEST_IRQ_ON();
}

static void test_multiple_irq_change(struct kunit *test)
{
	local_interrupt_disable();
	TEST_IRQ_OFF();
	local_interrupt_disable();
	TEST_IRQ_OFF();

	local_interrupt_enable();
	TEST_IRQ_OFF();
	local_interrupt_enable();
	TEST_IRQ_ON();

	local_interrupt_disable();
	TEST_IRQ_OFF();
	local_interrupt_enable();
	TEST_IRQ_ON();
}

static void test_irq_save(struct kunit *test)
{
	unsigned long flags;

	local_irq_save(flags);
	TEST_IRQ_OFF();
	local_interrupt_disable();
	TEST_IRQ_OFF();
	local_interrupt_enable();
	TEST_IRQ_OFF();
	local_irq_restore(flags);
	TEST_IRQ_ON();

	local_interrupt_disable();
	TEST_IRQ_OFF();
	local_irq_save(flags);
	TEST_IRQ_OFF();
	local_irq_restore(flags);
	TEST_IRQ_OFF();
	local_interrupt_enable();
	TEST_IRQ_ON();
}

static struct kunit_case test_cases[] = {
	KUNIT_CASE(test_single_irq_change),
	KUNIT_CASE(test_nested_irq_change),
	KUNIT_CASE(test_multiple_irq_change),
	KUNIT_CASE(test_irq_save),
	{},
};

/* init and exit are the same. */
static int test_init(struct kunit *test)
{
	TEST_IRQ_ON();

	return 0;
}

static void test_exit(struct kunit *test)
{
	TEST_IRQ_ON();
}

static struct kunit_suite refcount_interrupt_test_suite = {
	.name = "refcount_interrupt",
	.test_cases = test_cases,
	.init = test_init,
	.exit = test_exit,
};

kunit_test_suite(refcount_interrupt_test_suite);
MODULE_AUTHOR("Lyude Paul <lyude@redhat.com>");
MODULE_DESCRIPTION("Refcounted interrupt unit test suite");
MODULE_LICENSE("GPL");
