// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 1996, 1997, 1998, 1999, 2000,
 * Ingo Molnar, Matti Aarnio, Jakub Jelinek, Richard Henderson.
 *
 * Dispatch optimized XOR parity functions.
 */

#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/raid/xor.h>
#include <linux/raid/xor_impl.h>
#include <linux/jiffies.h>
#include <linux/preempt.h>
#include <asm/xor.h>

#ifndef XOR_SELECT_TEMPLATE
#define XOR_SELECT_TEMPLATE(x) (x)
#endif

/* The xor routines to use.  */
static struct xor_block_template *active_template;

void
xor_blocks(unsigned int src_count, unsigned int bytes, void *dest, void **srcs)
{
	unsigned long *p1, *p2, *p3, *p4;

	WARN_ON_ONCE(!in_task() || irqs_disabled() || softirq_count());

	p1 = (unsigned long *) srcs[0];
	if (src_count == 1) {
		active_template->do_2(bytes, dest, p1);
		return;
	}

	p2 = (unsigned long *) srcs[1];
	if (src_count == 2) {
		active_template->do_3(bytes, dest, p1, p2);
		return;
	}

	p3 = (unsigned long *) srcs[2];
	if (src_count == 3) {
		active_template->do_4(bytes, dest, p1, p2, p3);
		return;
	}

	p4 = (unsigned long *) srcs[3];
	active_template->do_5(bytes, dest, p1, p2, p3, p4);
}
EXPORT_SYMBOL(xor_blocks);

/* Set of all registered templates.  */
static struct xor_block_template *__initdata template_list;
static bool __initdata xor_forced = false;

static void __init do_xor_register(struct xor_block_template *tmpl)
{
	tmpl->next = template_list;
	template_list = tmpl;
}

#define BENCH_SIZE	4096
#define REPS		800U

static void __init
do_xor_speed(struct xor_block_template *tmpl, void *b1, void *b2)
{
	int speed;
	unsigned long reps;
	ktime_t min, start, t0;

	preempt_disable();

	reps = 0;
	t0 = ktime_get();
	/* delay start until time has advanced */
	while ((start = ktime_get()) == t0)
		cpu_relax();
	do {
		mb(); /* prevent loop optimization */
		tmpl->do_2(BENCH_SIZE, b1, b2);
		mb();
	} while (reps++ < REPS || (t0 = ktime_get()) == start);
	min = ktime_sub(t0, start);

	preempt_enable();

	// bytes/ns == GB/s, multiply by 1000 to get MB/s [not MiB/s]
	speed = (1000 * reps * BENCH_SIZE) / (unsigned int)ktime_to_ns(min);
	tmpl->speed = speed;

	pr_info("   %-16s: %5d MB/sec\n", tmpl->name, speed);
}

static int __init calibrate_xor_blocks(void)
{
	void *b1, *b2;
	struct xor_block_template *f, *fastest;

	if (xor_forced)
		return 0;

	b1 = (void *) __get_free_pages(GFP_KERNEL, 2);
	if (!b1) {
		pr_warn("xor: Yikes!  No memory available.\n");
		return -ENOMEM;
	}
	b2 = b1 + 2*PAGE_SIZE + BENCH_SIZE;

	pr_info("xor: measuring software checksum speed\n");
	fastest = template_list;
	for (f = template_list; f; f = f->next) {
		do_xor_speed(f, b1, b2);
		if (f->speed > fastest->speed)
			fastest = f;
	}
	active_template = fastest;
	pr_info("xor: using function: %s (%d MB/sec)\n",
	       fastest->name, fastest->speed);

	free_pages((unsigned long)b1, 2);
	return 0;
}

static int __init xor_init(void)
{
	/*
	 * If this arch/cpu has a short-circuited selection, don't loop through
	 * all the possible functions, just use the best one.
	 */
	active_template = XOR_SELECT_TEMPLATE(NULL);
	if (active_template) {
		pr_info("xor: automatically using best checksumming function   %-10s\n",
			active_template->name);
		xor_forced = true;
		return 0;
	}

#define xor_speed	do_xor_register
	XOR_TRY_TEMPLATES;
#undef xor_speed

#ifdef MODULE
	return calibrate_xor_blocks();
#else
	/*
	 * Pick the first template as the temporary default until calibration
	 * happens.
	 */
	active_template = template_list;
	return 0;
#endif
}

static __exit void xor_exit(void)
{
}

MODULE_DESCRIPTION("RAID-5 checksumming functions");
MODULE_LICENSE("GPL");

/*
 * When built-in we must register the default template before md, but we don't
 * want calibration to run that early as that would delay the boot process.
 */
#ifndef MODULE
__initcall(calibrate_xor_blocks);
#endif
core_initcall(xor_init);
module_exit(xor_exit);
