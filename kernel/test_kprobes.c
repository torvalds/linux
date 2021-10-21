// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * test_kprobes.c - simple sanity test for *probes
 *
 * Copyright IBM Corp. 2008
 */

#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/random.h>
#include <kunit/test.h>

#define div_factor 3

static u32 rand1, preh_val, posth_val;
static u32 (*target)(u32 value);
static u32 (*target2)(u32 value);
static struct kunit *current_test;

static noinline u32 kprobe_target(u32 value)
{
	return (value / div_factor);
}

static int kp_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	KUNIT_EXPECT_FALSE(current_test, preemptible());
	preh_val = (rand1 / div_factor);
	return 0;
}

static void kp_post_handler(struct kprobe *p, struct pt_regs *regs,
		unsigned long flags)
{
	KUNIT_EXPECT_FALSE(current_test, preemptible());
	KUNIT_EXPECT_EQ(current_test, preh_val, (rand1 / div_factor));
	posth_val = preh_val + div_factor;
}

static struct kprobe kp = {
	.symbol_name = "kprobe_target",
	.pre_handler = kp_pre_handler,
	.post_handler = kp_post_handler
};

static void test_kprobe(struct kunit *test)
{
	current_test = test;
	KUNIT_EXPECT_EQ(test, 0, register_kprobe(&kp));
	target(rand1);
	unregister_kprobe(&kp);
	KUNIT_EXPECT_NE(test, 0, preh_val);
	KUNIT_EXPECT_NE(test, 0, posth_val);
}

static noinline u32 kprobe_target2(u32 value)
{
	return (value / div_factor) + 1;
}

static int kp_pre_handler2(struct kprobe *p, struct pt_regs *regs)
{
	preh_val = (rand1 / div_factor) + 1;
	return 0;
}

static void kp_post_handler2(struct kprobe *p, struct pt_regs *regs,
		unsigned long flags)
{
	KUNIT_EXPECT_EQ(current_test, preh_val, (rand1 / div_factor) + 1);
	posth_val = preh_val + div_factor;
}

static struct kprobe kp2 = {
	.symbol_name = "kprobe_target2",
	.pre_handler = kp_pre_handler2,
	.post_handler = kp_post_handler2
};

static void test_kprobes(struct kunit *test)
{
	struct kprobe *kps[2] = {&kp, &kp2};

	current_test = test;

	/* addr and flags should be cleard for reusing kprobe. */
	kp.addr = NULL;
	kp.flags = 0;

	KUNIT_EXPECT_EQ(test, 0, register_kprobes(kps, 2));
	preh_val = 0;
	posth_val = 0;
	target(rand1);

	KUNIT_EXPECT_NE(test, 0, preh_val);
	KUNIT_EXPECT_NE(test, 0, posth_val);

	preh_val = 0;
	posth_val = 0;
	target2(rand1);

	KUNIT_EXPECT_NE(test, 0, preh_val);
	KUNIT_EXPECT_NE(test, 0, posth_val);
	unregister_kprobes(kps, 2);
}

#ifdef CONFIG_KRETPROBES
static u32 krph_val;

static int entry_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	KUNIT_EXPECT_FALSE(current_test, preemptible());
	krph_val = (rand1 / div_factor);
	return 0;
}

static int return_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	unsigned long ret = regs_return_value(regs);

	KUNIT_EXPECT_FALSE(current_test, preemptible());
	KUNIT_EXPECT_EQ(current_test, ret, rand1 / div_factor);
	KUNIT_EXPECT_NE(current_test, krph_val, 0);
	krph_val = rand1;
	return 0;
}

static struct kretprobe rp = {
	.handler	= return_handler,
	.entry_handler  = entry_handler,
	.kp.symbol_name = "kprobe_target"
};

static void test_kretprobe(struct kunit *test)
{
	current_test = test;
	KUNIT_EXPECT_EQ(test, 0, register_kretprobe(&rp));
	target(rand1);
	unregister_kretprobe(&rp);
	KUNIT_EXPECT_EQ(test, krph_val, rand1);
}

static int return_handler2(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	unsigned long ret = regs_return_value(regs);

	KUNIT_EXPECT_EQ(current_test, ret, (rand1 / div_factor) + 1);
	KUNIT_EXPECT_NE(current_test, krph_val, 0);
	krph_val = rand1;
	return 0;
}

static struct kretprobe rp2 = {
	.handler	= return_handler2,
	.entry_handler  = entry_handler,
	.kp.symbol_name = "kprobe_target2"
};

static void test_kretprobes(struct kunit *test)
{
	struct kretprobe *rps[2] = {&rp, &rp2};

	current_test = test;
	/* addr and flags should be cleard for reusing kprobe. */
	rp.kp.addr = NULL;
	rp.kp.flags = 0;
	KUNIT_EXPECT_EQ(test, 0, register_kretprobes(rps, 2));

	krph_val = 0;
	target(rand1);
	KUNIT_EXPECT_EQ(test, krph_val, rand1);

	krph_val = 0;
	target2(rand1);
	KUNIT_EXPECT_EQ(test, krph_val, rand1);
	unregister_kretprobes(rps, 2);
}
#endif /* CONFIG_KRETPROBES */

static int kprobes_test_init(struct kunit *test)
{
	target = kprobe_target;
	target2 = kprobe_target2;

	do {
		rand1 = prandom_u32();
	} while (rand1 <= div_factor);
	return 0;
}

static struct kunit_case kprobes_testcases[] = {
	KUNIT_CASE(test_kprobe),
	KUNIT_CASE(test_kprobes),
#ifdef CONFIG_KRETPROBES
	KUNIT_CASE(test_kretprobe),
	KUNIT_CASE(test_kretprobes),
#endif
	{}
};

static struct kunit_suite kprobes_test_suite = {
	.name = "kprobes_test",
	.init = kprobes_test_init,
	.test_cases = kprobes_testcases,
};

kunit_test_suites(&kprobes_test_suite);

MODULE_LICENSE("GPL");
