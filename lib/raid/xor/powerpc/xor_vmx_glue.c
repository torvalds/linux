// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Altivec XOR operations
 *
 * Copyright 2017 IBM Corp.
 */

#include <linux/preempt.h>
#include <linux/sched.h>
#include <asm/switch_to.h>
#include "xor_impl.h"
#include "xor_arch.h"
#include "xor_vmx.h"

static void xor_gen_altivec(void *dest, void **srcs, unsigned int src_cnt,
		unsigned int bytes)
{
	preempt_disable();
	enable_kernel_altivec();
	xor_gen_altivec_inner(dest, srcs, src_cnt, bytes);
	disable_kernel_altivec();
	preempt_enable();
}

struct xor_block_template xor_block_altivec = {
	.name		= "altivec",
	.xor_gen	= xor_gen_altivec,
};
