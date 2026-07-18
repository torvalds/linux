/* SPDX-License-Identifier: GPL-2.0 */
/*
 * RISC-V Arch Timer(sstc) specific interface
 *
 * Copyright (c) 2024 Intel Corporation
 */

#ifndef SELFTEST_KVM_ARCH_TIMER_H
#define SELFTEST_KVM_ARCH_TIMER_H

#include <asm/csr.h>
#include <asm/vdso/processor.h>

static unsigned long timer_freq;

#define msec_to_cycles(msec)	\
	((timer_freq) * (u64)(msec) / 1000)

#define usec_to_cycles(usec)	\
	((timer_freq) * (u64)(usec) / 1000000)

#define cycles_to_usec(cycles) \
	((u64)(cycles) * 1000000 / (timer_freq))

static inline u64 timer_get_cycles(void)
{
	return csr_read(CSR_TIME);
}

static inline void timer_set_cmp(u64 cval)
{
	csr_write(CSR_STIMECMP, cval);
}

static inline u64 timer_get_cmp(void)
{
	return csr_read(CSR_STIMECMP);
}

static inline void timer_irq_enable(void)
{
	csr_set(CSR_SIE, IE_TIE);
}

static inline void timer_irq_disable(void)
{
	csr_clear(CSR_SIE, IE_TIE);
}

static inline void timer_set_next_cmp_ms(u32 msec)
{
	u64 now_ct = timer_get_cycles();
	u64 next_ct = now_ct + msec_to_cycles(msec);

	timer_set_cmp(next_ct);
}

static inline void __delay(u64 cycles)
{
	u64 start = timer_get_cycles();

	while ((timer_get_cycles() - start) < cycles)
		cpu_relax();
}

static inline void udelay(unsigned long usec)
{
	__delay(usec_to_cycles(usec));
}

#endif /* SELFTEST_KVM_ARCH_TIMER_H */
