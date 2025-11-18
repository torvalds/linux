// SPDX-License-Identifier: LGPL-2.1+

#include <linux/cleanup.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdesc.h>
#include <linux/irqdomain.h>
#include <linux/nodemask.h>
#include <kunit/test.h>

#include "internals.h"

static irqreturn_t noop_handler(int irq, void *data)
{
	return IRQ_HANDLED;
}

static void noop(struct irq_data *data) { }
static unsigned int noop_ret(struct irq_data *data) { return 0; }

static int noop_affinity(struct irq_data *data, const struct cpumask *dest,
			 bool force)
{
	irq_data_update_effective_affinity(data, dest);

	return 0;
}

static struct irq_chip fake_irq_chip = {
	.name           = "fake",
	.irq_startup    = noop_ret,
	.irq_shutdown   = noop,
	.irq_enable     = noop,
	.irq_disable    = noop,
	.irq_ack        = noop,
	.irq_mask       = noop,
	.irq_unmask     = noop,
	.irq_set_affinity = noop_affinity,
	.flags          = IRQCHIP_SKIP_SET_WAKE,
};

static int irq_test_setup_fake_irq(struct kunit *test, struct irq_affinity_desc *affd)
{
	struct irq_desc *desc;
	int virq;

	virq = irq_domain_alloc_descs(-1, 1, 0, NUMA_NO_NODE, affd);
	KUNIT_ASSERT_GE(test, virq, 0);

	irq_set_chip_and_handler(virq, &fake_irq_chip, handle_simple_irq);

	desc = irq_to_desc(virq);
	KUNIT_ASSERT_PTR_NE(test, desc, NULL);

	/* On some architectures, IRQs are NOREQUEST | NOPROBE by default. */
	irq_settings_clr_norequest(desc);

	return virq;
}

static void irq_disable_depth_test(struct kunit *test)
{
	struct irq_desc *desc;
	int virq, ret;

	virq = irq_test_setup_fake_irq(test, NULL);

	desc = irq_to_desc(virq);
	KUNIT_ASSERT_PTR_NE(test, desc, NULL);

	ret = request_irq(virq, noop_handler, 0, "test_irq", NULL);
	KUNIT_ASSERT_EQ(test, ret, 0);

	KUNIT_EXPECT_EQ(test, desc->depth, 0);

	disable_irq(virq);
	KUNIT_EXPECT_EQ(test, desc->depth, 1);

	enable_irq(virq);
	KUNIT_EXPECT_EQ(test, desc->depth, 0);

	free_irq(virq, NULL);
}

static void irq_free_disabled_test(struct kunit *test)
{
	struct irq_desc *desc;
	int virq, ret;

	virq = irq_test_setup_fake_irq(test, NULL);

	desc = irq_to_desc(virq);
	KUNIT_ASSERT_PTR_NE(test, desc, NULL);

	ret = request_irq(virq, noop_handler, 0, "test_irq", NULL);
	KUNIT_ASSERT_EQ(test, ret, 0);

	KUNIT_EXPECT_EQ(test, desc->depth, 0);

	disable_irq(virq);
	KUNIT_EXPECT_EQ(test, desc->depth, 1);

	free_irq(virq, NULL);
	KUNIT_EXPECT_GE(test, desc->depth, 1);

	ret = request_irq(virq, noop_handler, 0, "test_irq", NULL);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, desc->depth, 0);

	free_irq(virq, NULL);
}

static void irq_shutdown_depth_test(struct kunit *test)
{
	struct irq_desc *desc;
	struct irq_data *data;
	int virq, ret;
	struct irq_affinity_desc affinity = {
		.is_managed = 1,
		.mask = CPU_MASK_ALL,
	};

	if (!IS_ENABLED(CONFIG_SMP))
		kunit_skip(test, "requires CONFIG_SMP for managed shutdown");

	virq = irq_test_setup_fake_irq(test, &affinity);

	desc = irq_to_desc(virq);
	KUNIT_ASSERT_PTR_NE(test, desc, NULL);

	data = irq_desc_get_irq_data(desc);
	KUNIT_ASSERT_PTR_NE(test, data, NULL);

	ret = request_irq(virq, noop_handler, 0, "test_irq", NULL);
	KUNIT_ASSERT_EQ(test, ret, 0);

	KUNIT_EXPECT_TRUE(test, irqd_is_activated(data));
	KUNIT_EXPECT_TRUE(test, irqd_is_started(data));
	KUNIT_EXPECT_TRUE(test, irqd_affinity_is_managed(data));

	KUNIT_EXPECT_EQ(test, desc->depth, 0);

	disable_irq(virq);
	KUNIT_EXPECT_EQ(test, desc->depth, 1);

	scoped_guard(raw_spinlock_irqsave, &desc->lock)
		irq_shutdown_and_deactivate(desc);

	KUNIT_EXPECT_FALSE(test, irqd_is_activated(data));
	KUNIT_EXPECT_FALSE(test, irqd_is_started(data));

	KUNIT_EXPECT_EQ(test, irq_activate(desc), 0);
#ifdef CONFIG_SMP
	irq_startup_managed(desc);
#endif

	KUNIT_EXPECT_EQ(test, desc->depth, 1);

	enable_irq(virq);
	KUNIT_EXPECT_EQ(test, desc->depth, 0);

	free_irq(virq, NULL);
}

static void irq_cpuhotplug_test(struct kunit *test)
{
	struct irq_desc *desc;
	struct irq_data *data;
	int virq, ret;
	struct irq_affinity_desc affinity = {
		.is_managed = 1,
	};

	if (!IS_ENABLED(CONFIG_SMP))
		kunit_skip(test, "requires CONFIG_SMP for CPU hotplug");
	if (!get_cpu_device(1))
		kunit_skip(test, "requires more than 1 CPU for CPU hotplug");
	if (!cpu_is_hotpluggable(1))
		kunit_skip(test, "CPU 1 must be hotpluggable");
	if (!cpu_online(1))
		kunit_skip(test, "CPU 1 must be online");

	cpumask_copy(&affinity.mask, cpumask_of(1));

	virq = irq_test_setup_fake_irq(test, &affinity);

	desc = irq_to_desc(virq);
	KUNIT_ASSERT_PTR_NE(test, desc, NULL);

	data = irq_desc_get_irq_data(desc);
	KUNIT_ASSERT_PTR_NE(test, data, NULL);

	ret = request_irq(virq, noop_handler, 0, "test_irq", NULL);
	KUNIT_ASSERT_EQ(test, ret, 0);

	KUNIT_EXPECT_TRUE(test, irqd_is_activated(data));
	KUNIT_EXPECT_TRUE(test, irqd_is_started(data));
	KUNIT_EXPECT_TRUE(test, irqd_affinity_is_managed(data));

	KUNIT_EXPECT_EQ(test, desc->depth, 0);

	disable_irq(virq);
	KUNIT_EXPECT_EQ(test, desc->depth, 1);

	KUNIT_EXPECT_EQ(test, remove_cpu(1), 0);
	KUNIT_EXPECT_GE(test, desc->depth, 1);
	KUNIT_EXPECT_EQ(test, add_cpu(1), 0);

	KUNIT_EXPECT_EQ(test, desc->depth, 1);

	enable_irq(virq);
	KUNIT_EXPECT_TRUE(test, irqd_is_activated(data));
	KUNIT_EXPECT_TRUE(test, irqd_is_started(data));
	KUNIT_EXPECT_EQ(test, desc->depth, 0);

	free_irq(virq, NULL);
}

static struct kunit_case irq_test_cases[] = {
	KUNIT_CASE(irq_disable_depth_test),
	KUNIT_CASE(irq_free_disabled_test),
	KUNIT_CASE(irq_shutdown_depth_test),
	KUNIT_CASE(irq_cpuhotplug_test),
	{}
};

static struct kunit_suite irq_test_suite = {
	.name = "irq_test_cases",
	.test_cases = irq_test_cases,
};

kunit_test_suite(irq_test_suite);
MODULE_DESCRIPTION("IRQ unit test suite");
MODULE_LICENSE("GPL");
