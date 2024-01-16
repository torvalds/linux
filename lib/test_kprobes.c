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

static unsigned long (*internal_target)(void);
static unsigned long (*stacktrace_target)(void);
static unsigned long (*stacktrace_driver)(void);
static unsigned long target_return_address[2];

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

static noinline unsigned long kprobe_stacktrace_internal_target(void)
{
	if (!target_return_address[0])
		target_return_address[0] = (unsigned long)__builtin_return_address(0);
	return target_return_address[0];
}

static noinline unsigned long kprobe_stacktrace_target(void)
{
	if (!target_return_address[1])
		target_return_address[1] = (unsigned long)__builtin_return_address(0);

	if (internal_target)
		internal_target();

	return target_return_address[1];
}

static noinline unsigned long kprobe_stacktrace_driver(void)
{
	if (stacktrace_target)
		stacktrace_target();

	/* This is for preventing inlining the function */
	return (unsigned long)__builtin_return_address(0);
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

#ifdef CONFIG_ARCH_CORRECT_STACKTRACE_ON_KRETPROBE
#define STACK_BUF_SIZE 16
static unsigned long stack_buf[STACK_BUF_SIZE];

static int stacktrace_return_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	unsigned long retval = regs_return_value(regs);
	int i, ret;

	KUNIT_EXPECT_FALSE(current_test, preemptible());
	KUNIT_EXPECT_EQ(current_test, retval, target_return_address[1]);

	/*
	 * Test stacktrace inside the kretprobe handler, this will involves
	 * kretprobe trampoline, but must include correct return address
	 * of the target function.
	 */
	ret = stack_trace_save(stack_buf, STACK_BUF_SIZE, 0);
	KUNIT_EXPECT_NE(current_test, ret, 0);

	for (i = 0; i < ret; i++) {
		if (stack_buf[i] == target_return_address[1])
			break;
	}
	KUNIT_EXPECT_NE(current_test, i, ret);

#if !IS_MODULE(CONFIG_KPROBES_SANITY_TEST)
	/*
	 * Test stacktrace from pt_regs at the return address. Thus the stack
	 * trace must start from the target return address.
	 */
	ret = stack_trace_save_regs(regs, stack_buf, STACK_BUF_SIZE, 0);
	KUNIT_EXPECT_NE(current_test, ret, 0);
	KUNIT_EXPECT_EQ(current_test, stack_buf[0], target_return_address[1]);
#endif

	return 0;
}

static struct kretprobe rp3 = {
	.handler	= stacktrace_return_handler,
	.kp.symbol_name = "kprobe_stacktrace_target"
};

static void test_stacktrace_on_kretprobe(struct kunit *test)
{
	unsigned long myretaddr = (unsigned long)__builtin_return_address(0);

	current_test = test;
	rp3.kp.addr = NULL;
	rp3.kp.flags = 0;

	/*
	 * Run the stacktrace_driver() to record correct return address in
	 * stacktrace_target() and ensure stacktrace_driver() call is not
	 * inlined by checking the return address of stacktrace_driver()
	 * and the return address of this function is different.
	 */
	KUNIT_ASSERT_NE(test, myretaddr, stacktrace_driver());

	KUNIT_ASSERT_EQ(test, 0, register_kretprobe(&rp3));
	KUNIT_ASSERT_NE(test, myretaddr, stacktrace_driver());
	unregister_kretprobe(&rp3);
}

static int stacktrace_internal_return_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	unsigned long retval = regs_return_value(regs);
	int i, ret;

	KUNIT_EXPECT_FALSE(current_test, preemptible());
	KUNIT_EXPECT_EQ(current_test, retval, target_return_address[0]);

	/*
	 * Test stacktrace inside the kretprobe handler for nested case.
	 * The unwinder will find the kretprobe_trampoline address on the
	 * return address, and kretprobe must solve that.
	 */
	ret = stack_trace_save(stack_buf, STACK_BUF_SIZE, 0);
	KUNIT_EXPECT_NE(current_test, ret, 0);

	for (i = 0; i < ret - 1; i++) {
		if (stack_buf[i] == target_return_address[0]) {
			KUNIT_EXPECT_EQ(current_test, stack_buf[i + 1], target_return_address[1]);
			break;
		}
	}
	KUNIT_EXPECT_NE(current_test, i, ret);

#if !IS_MODULE(CONFIG_KPROBES_SANITY_TEST)
	/* Ditto for the regs version. */
	ret = stack_trace_save_regs(regs, stack_buf, STACK_BUF_SIZE, 0);
	KUNIT_EXPECT_NE(current_test, ret, 0);
	KUNIT_EXPECT_EQ(current_test, stack_buf[0], target_return_address[0]);
	KUNIT_EXPECT_EQ(current_test, stack_buf[1], target_return_address[1]);
#endif

	return 0;
}

static struct kretprobe rp4 = {
	.handler	= stacktrace_internal_return_handler,
	.kp.symbol_name = "kprobe_stacktrace_internal_target"
};

static void test_stacktrace_on_nested_kretprobe(struct kunit *test)
{
	unsigned long myretaddr = (unsigned long)__builtin_return_address(0);
	struct kretprobe *rps[2] = {&rp3, &rp4};

	current_test = test;
	rp3.kp.addr = NULL;
	rp3.kp.flags = 0;

	//KUNIT_ASSERT_NE(test, myretaddr, stacktrace_driver());

	KUNIT_ASSERT_EQ(test, 0, register_kretprobes(rps, 2));
	KUNIT_ASSERT_NE(test, myretaddr, stacktrace_driver());
	unregister_kretprobes(rps, 2);
}
#endif /* CONFIG_ARCH_CORRECT_STACKTRACE_ON_KRETPROBE */

#endif /* CONFIG_KRETPROBES */

static int kprobes_test_init(struct kunit *test)
{
	target = kprobe_target;
	target2 = kprobe_target2;
	stacktrace_target = kprobe_stacktrace_target;
	internal_target = kprobe_stacktrace_internal_target;
	stacktrace_driver = kprobe_stacktrace_driver;

	do {
		rand1 = get_random_u32();
	} while (rand1 <= div_factor);
	return 0;
}

static struct kunit_case kprobes_testcases[] = {
	KUNIT_CASE(test_kprobe),
	KUNIT_CASE(test_kprobes),
#ifdef CONFIG_KRETPROBES
	KUNIT_CASE(test_kretprobe),
	KUNIT_CASE(test_kretprobes),
#ifdef CONFIG_ARCH_CORRECT_STACKTRACE_ON_KRETPROBE
	KUNIT_CASE(test_stacktrace_on_kretprobe),
	KUNIT_CASE(test_stacktrace_on_nested_kretprobe),
#endif
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
