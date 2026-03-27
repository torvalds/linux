// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Altivec XOR operations
 *
 * Copyright 2017 IBM Corp.
 */

#include <linux/preempt.h>
#include <linux/sched.h>
#include <linux/raid/xor_impl.h>
#include <asm/switch_to.h>
#include <asm/xor.h>
#include "xor_vmx.h"

static void xor_altivec_2(unsigned long bytes, unsigned long * __restrict p1,
		const unsigned long * __restrict p2)
{
	preempt_disable();
	enable_kernel_altivec();
	__xor_altivec_2(bytes, p1, p2);
	disable_kernel_altivec();
	preempt_enable();
}

static void xor_altivec_3(unsigned long bytes, unsigned long * __restrict p1,
		const unsigned long * __restrict p2,
		const unsigned long * __restrict p3)
{
	preempt_disable();
	enable_kernel_altivec();
	__xor_altivec_3(bytes, p1, p2, p3);
	disable_kernel_altivec();
	preempt_enable();
}

static void xor_altivec_4(unsigned long bytes, unsigned long * __restrict p1,
		const unsigned long * __restrict p2,
		const unsigned long * __restrict p3,
		const unsigned long * __restrict p4)
{
	preempt_disable();
	enable_kernel_altivec();
	__xor_altivec_4(bytes, p1, p2, p3, p4);
	disable_kernel_altivec();
	preempt_enable();
}

static void xor_altivec_5(unsigned long bytes, unsigned long * __restrict p1,
		const unsigned long * __restrict p2,
		const unsigned long * __restrict p3,
		const unsigned long * __restrict p4,
		const unsigned long * __restrict p5)
{
	preempt_disable();
	enable_kernel_altivec();
	__xor_altivec_5(bytes, p1, p2, p3, p4, p5);
	disable_kernel_altivec();
	preempt_enable();
}

struct xor_block_template xor_block_altivec = {
	.name = "altivec",
	.do_2 = xor_altivec_2,
	.do_3 = xor_altivec_3,
	.do_4 = xor_altivec_4,
	.do_5 = xor_altivec_5,
};
