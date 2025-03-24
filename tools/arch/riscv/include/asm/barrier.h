/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copied from the kernel sources to tools/arch/riscv:
 *
 * Copyright (C) 2012 ARM Ltd.
 * Copyright (C) 2013 Regents of the University of California
 * Copyright (C) 2017 SiFive
 */

#ifndef _TOOLS_LINUX_ASM_RISCV_BARRIER_H
#define _TOOLS_LINUX_ASM_RISCV_BARRIER_H

#include <asm/fence.h>
#include <linux/compiler.h>

/* These barriers need to enforce ordering on both devices and memory. */
#define mb()		RISCV_FENCE(iorw, iorw)
#define rmb()		RISCV_FENCE(ir, ir)
#define wmb()		RISCV_FENCE(ow, ow)

/* These barriers do not need to enforce ordering on devices, just memory. */
#define smp_mb()	RISCV_FENCE(rw, rw)
#define smp_rmb()	RISCV_FENCE(r, r)
#define smp_wmb()	RISCV_FENCE(w, w)

#define smp_store_release(p, v)						\
do {									\
	RISCV_FENCE(rw, w);						\
	WRITE_ONCE(*p, v);						\
} while (0)

#define smp_load_acquire(p)						\
({									\
	typeof(*p) ___p1 = READ_ONCE(*p);				\
	RISCV_FENCE(r, rw);						\
	___p1;								\
})

#endif /* _TOOLS_LINUX_ASM_RISCV_BARRIER_H */
