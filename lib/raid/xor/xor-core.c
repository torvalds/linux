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
#include <linux/jiffies.h>
#include <linux/preempt.h>
#include <linux/static_call.h>
#include "xor_impl.h"

DEFINE_STATIC_CALL_NULL(xor_gen_impl, *xor_block_8regs.xor_gen);

/**
 * xor_gen - generate RAID-style XOR information
 * @dest:	destination vector
 * @srcs:	source vectors
 * @src_cnt:	number of source vectors
 * @bytes:	length in bytes of each vector
 *
 * Performs bit-wise XOR operation into @dest for each of the @src_cnt vectors
 * in @srcs for a length of @bytes bytes.  @src_cnt must be non-zero, and the
 * memory pointed to by @dest and each member of @srcs must be at least 64-byte
 * aligned.  @bytes must be non-zero and a multiple of 512.
 *
 * Note: for typical RAID uses, @dest either needs to be zeroed, or filled with
 * the first disk, which then needs to be removed from @srcs.
 */
void xor_gen(void *dest, void **srcs, unsigned int src_cnt, unsigned int bytes)
{
	WARN_ON_ONCE(!in_task() || irqs_disabled() || softirq_count());
	WARN_ON_ONCE(bytes == 0);
	WARN_ON_ONCE(bytes & 511);

	static_call(xor_gen_impl)(dest, srcs, src_cnt, bytes);
}
EXPORT_SYMBOL(xor_gen);

/* Set of all registered templates.  */
static struct xor_block_template *__initdata template_list;
static struct xor_block_template *forced_template;

/**
 * xor_register - register a XOR template
 * @tmpl:	template to register
 *
 * Register a XOR implementation with the core.  Registered implementations
 * will be measured by a trivial benchmark, and the fastest one is chosen
 * unless an implementation is forced using xor_force().
 */
void __init xor_register(struct xor_block_template *tmpl)
{
	tmpl->next = template_list;
	template_list = tmpl;
}

/**
 * xor_force - force use of a XOR template
 * @tmpl:	template to register
 *
 * Register a XOR implementation with the core and force using it.  Forcing
 * an implementation will make the core ignore any template registered using
 * xor_register(), or any previous implementation forced using xor_force().
 */
void __init xor_force(struct xor_block_template *tmpl)
{
	forced_template = tmpl;
}

#define BENCH_SIZE	4096
#define REPS		800U

static void __init
do_xor_speed(struct xor_block_template *tmpl, void *b1, void *b2)
{
	int speed;
	unsigned long reps;
	ktime_t min, start, t0;
	void *srcs[1] = { b2 };

	preempt_disable();

	reps = 0;
	t0 = ktime_get();
	/* delay start until time has advanced */
	while ((start = ktime_get()) == t0)
		cpu_relax();
	do {
		mb(); /* prevent loop optimization */
		tmpl->xor_gen(b1, srcs, 1, BENCH_SIZE);
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

	if (forced_template)
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
	static_call_update(xor_gen_impl, fastest->xor_gen);
	pr_info("xor: using function: %s (%d MB/sec)\n",
	       fastest->name, fastest->speed);

	free_pages((unsigned long)b1, 2);
	return 0;
}

#ifdef CONFIG_XOR_BLOCKS_ARCH
#include "xor_arch.h" /* $SRCARCH/xor_arch.h */
#else
static void __init arch_xor_init(void)
{
	xor_register(&xor_block_8regs);
	xor_register(&xor_block_8regs_p);
	xor_register(&xor_block_32regs);
	xor_register(&xor_block_32regs_p);
}
#endif /* CONFIG_XOR_BLOCKS_ARCH */

static int __init xor_init(void)
{
	arch_xor_init();

	/*
	 * If this arch/cpu has a short-circuited selection, don't loop through
	 * all the possible functions, just use the best one.
	 */
	if (forced_template) {
		pr_info("xor: automatically using best checksumming function   %-10s\n",
			forced_template->name);
		static_call_update(xor_gen_impl, forced_template->xor_gen);
		return 0;
	}

#ifdef MODULE
	return calibrate_xor_blocks();
#else
	/*
	 * Pick the first template as the temporary default until calibration
	 * happens.
	 */
	static_call_update(xor_gen_impl, template_list->xor_gen);
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
