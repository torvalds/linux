/*
 * test_kprobes.c - simple sanity test for *probes
 *
 * Copyright IBM Corp. 2008
 *
 * This program is free software;  you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
 * the GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/random.h>

#define div_factor 3

static u32 rand1, preh_val, posth_val, jph_val;
static int errors, handler_errors, num_tests;
static u32 (*target)(u32 value);
static u32 (*target2)(u32 value);

static noinline u32 kprobe_target(u32 value)
{
	return (value / div_factor);
}

static int kp_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	preh_val = (rand1 / div_factor);
	return 0;
}

static void kp_post_handler(struct kprobe *p, struct pt_regs *regs,
		unsigned long flags)
{
	if (preh_val != (rand1 / div_factor)) {
		handler_errors++;
		printk(KERN_ERR "Kprobe smoke test failed: "
				"incorrect value in post_handler\n");
	}
	posth_val = preh_val + div_factor;
}

static struct kprobe kp = {
	.symbol_name = "kprobe_target",
	.pre_handler = kp_pre_handler,
	.post_handler = kp_post_handler
};

static int test_kprobe(void)
{
	int ret;

	ret = register_kprobe(&kp);
	if (ret < 0) {
		printk(KERN_ERR "Kprobe smoke test failed: "
				"register_kprobe returned %d\n", ret);
		return ret;
	}

	ret = target(rand1);
	unregister_kprobe(&kp);

	if (preh_val == 0) {
		printk(KERN_ERR "Kprobe smoke test failed: "
				"kprobe pre_handler not called\n");
		handler_errors++;
	}

	if (posth_val == 0) {
		printk(KERN_ERR "Kprobe smoke test failed: "
				"kprobe post_handler not called\n");
		handler_errors++;
	}

	return 0;
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
	if (preh_val != (rand1 / div_factor) + 1) {
		handler_errors++;
		printk(KERN_ERR "Kprobe smoke test failed: "
				"incorrect value in post_handler2\n");
	}
	posth_val = preh_val + div_factor;
}

static struct kprobe kp2 = {
	.symbol_name = "kprobe_target2",
	.pre_handler = kp_pre_handler2,
	.post_handler = kp_post_handler2
};

static int test_kprobes(void)
{
	int ret;
	struct kprobe *kps[2] = {&kp, &kp2};

	/* addr and flags should be cleard for reusing kprobe. */
	kp.addr = NULL;
	kp.flags = 0;
	ret = register_kprobes(kps, 2);
	if (ret < 0) {
		printk(KERN_ERR "Kprobe smoke test failed: "
				"register_kprobes returned %d\n", ret);
		return ret;
	}

	preh_val = 0;
	posth_val = 0;
	ret = target(rand1);

	if (preh_val == 0) {
		printk(KERN_ERR "Kprobe smoke test failed: "
				"kprobe pre_handler not called\n");
		handler_errors++;
	}

	if (posth_val == 0) {
		printk(KERN_ERR "Kprobe smoke test failed: "
				"kprobe post_handler not called\n");
		handler_errors++;
	}

	preh_val = 0;
	posth_val = 0;
	ret = target2(rand1);

	if (preh_val == 0) {
		printk(KERN_ERR "Kprobe smoke test failed: "
				"kprobe pre_handler2 not called\n");
		handler_errors++;
	}

	if (posth_val == 0) {
		printk(KERN_ERR "Kprobe smoke test failed: "
				"kprobe post_handler2 not called\n");
		handler_errors++;
	}

	unregister_kprobes(kps, 2);
	return 0;

}

static u32 j_kprobe_target(u32 value)
{
	if (value != rand1) {
		handler_errors++;
		printk(KERN_ERR "Kprobe smoke test failed: "
				"incorrect value in jprobe handler\n");
	}

	jph_val = rand1;
	jprobe_return();
	return 0;
}

static struct jprobe jp = {
	.entry		= j_kprobe_target,
	.kp.symbol_name = "kprobe_target"
};

static int test_jprobe(void)
{
	int ret;

	ret = register_jprobe(&jp);
	if (ret < 0) {
		printk(KERN_ERR "Kprobe smoke test failed: "
				"register_jprobe returned %d\n", ret);
		return ret;
	}

	ret = target(rand1);
	unregister_jprobe(&jp);
	if (jph_val == 0) {
		printk(KERN_ERR "Kprobe smoke test failed: "
				"jprobe handler not called\n");
		handler_errors++;
	}

	return 0;
}

static struct jprobe jp2 = {
	.entry          = j_kprobe_target,
	.kp.symbol_name = "kprobe_target2"
};

static int test_jprobes(void)
{
	int ret;
	struct jprobe *jps[2] = {&jp, &jp2};

	/* addr and flags should be cleard for reusing kprobe. */
	jp.kp.addr = NULL;
	jp.kp.flags = 0;
	ret = register_jprobes(jps, 2);
	if (ret < 0) {
		printk(KERN_ERR "Kprobe smoke test failed: "
				"register_jprobes returned %d\n", ret);
		return ret;
	}

	jph_val = 0;
	ret = target(rand1);
	if (jph_val == 0) {
		printk(KERN_ERR "Kprobe smoke test failed: "
				"jprobe handler not called\n");
		handler_errors++;
	}

	jph_val = 0;
	ret = target2(rand1);
	if (jph_val == 0) {
		printk(KERN_ERR "Kprobe smoke test failed: "
				"jprobe handler2 not called\n");
		handler_errors++;
	}
	unregister_jprobes(jps, 2);

	return 0;
}
#ifdef CONFIG_KRETPROBES
static u32 krph_val;

static int entry_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	krph_val = (rand1 / div_factor);
	return 0;
}

static int return_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	unsigned long ret = regs_return_value(regs);

	if (ret != (rand1 / div_factor)) {
		handler_errors++;
		printk(KERN_ERR "Kprobe smoke test failed: "
				"incorrect value in kretprobe handler\n");
	}
	if (krph_val == 0) {
		handler_errors++;
		printk(KERN_ERR "Kprobe smoke test failed: "
				"call to kretprobe entry handler failed\n");
	}

	krph_val = rand1;
	return 0;
}

static struct kretprobe rp = {
	.handler	= return_handler,
	.entry_handler  = entry_handler,
	.kp.symbol_name = "kprobe_target"
};

static int test_kretprobe(void)
{
	int ret;

	ret = register_kretprobe(&rp);
	if (ret < 0) {
		printk(KERN_ERR "Kprobe smoke test failed: "
				"register_kretprobe returned %d\n", ret);
		return ret;
	}

	ret = target(rand1);
	unregister_kretprobe(&rp);
	if (krph_val != rand1) {
		printk(KERN_ERR "Kprobe smoke test failed: "
				"kretprobe handler not called\n");
		handler_errors++;
	}

	return 0;
}

static int return_handler2(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	unsigned long ret = regs_return_value(regs);

	if (ret != (rand1 / div_factor) + 1) {
		handler_errors++;
		printk(KERN_ERR "Kprobe smoke test failed: "
				"incorrect value in kretprobe handler2\n");
	}
	if (krph_val == 0) {
		handler_errors++;
		printk(KERN_ERR "Kprobe smoke test failed: "
				"call to kretprobe entry handler failed\n");
	}

	krph_val = rand1;
	return 0;
}

static struct kretprobe rp2 = {
	.handler	= return_handler2,
	.entry_handler  = entry_handler,
	.kp.symbol_name = "kprobe_target2"
};

static int test_kretprobes(void)
{
	int ret;
	struct kretprobe *rps[2] = {&rp, &rp2};

	/* addr and flags should be cleard for reusing kprobe. */
	rp.kp.addr = NULL;
	rp.kp.flags = 0;
	ret = register_kretprobes(rps, 2);
	if (ret < 0) {
		printk(KERN_ERR "Kprobe smoke test failed: "
				"register_kretprobe returned %d\n", ret);
		return ret;
	}

	krph_val = 0;
	ret = target(rand1);
	if (krph_val != rand1) {
		printk(KERN_ERR "Kprobe smoke test failed: "
				"kretprobe handler not called\n");
		handler_errors++;
	}

	krph_val = 0;
	ret = target2(rand1);
	if (krph_val != rand1) {
		printk(KERN_ERR "Kprobe smoke test failed: "
				"kretprobe handler2 not called\n");
		handler_errors++;
	}
	unregister_kretprobes(rps, 2);
	return 0;
}
#endif /* CONFIG_KRETPROBES */

int init_test_probes(void)
{
	int ret;

	target = kprobe_target;
	target2 = kprobe_target2;

	do {
		rand1 = random32();
	} while (rand1 <= div_factor);

	printk(KERN_INFO "Kprobe smoke test started\n");
	num_tests++;
	ret = test_kprobe();
	if (ret < 0)
		errors++;

	num_tests++;
	ret = test_kprobes();
	if (ret < 0)
		errors++;

	num_tests++;
	ret = test_jprobe();
	if (ret < 0)
		errors++;

	num_tests++;
	ret = test_jprobes();
	if (ret < 0)
		errors++;

#ifdef CONFIG_KRETPROBES
	num_tests++;
	ret = test_kretprobe();
	if (ret < 0)
		errors++;

	num_tests++;
	ret = test_kretprobes();
	if (ret < 0)
		errors++;
#endif /* CONFIG_KRETPROBES */

	if (errors)
		printk(KERN_ERR "BUG: Kprobe smoke test: %d out of "
				"%d tests failed\n", errors, num_tests);
	else if (handler_errors)
		printk(KERN_ERR "BUG: Kprobe smoke test: %d error(s) "
				"running handlers\n", handler_errors);
	else
		printk(KERN_INFO "Kprobe smoke test passed successfully\n");

	return 0;
}
