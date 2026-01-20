/* SPDX-License-Identifier: GPL-2.0 */
/*
 * LoongArch Constant Timer specific interface
 */
#ifndef SELFTEST_KVM_ARCH_TIMER_H
#define SELFTEST_KVM_ARCH_TIMER_H

#include "processor.h"

/* LoongArch timer frequency is constant 100MHZ */
#define TIMER_FREQ		(100UL << 20)
#define msec_to_cycles(msec)	(TIMER_FREQ * (unsigned long)(msec) / 1000)
#define usec_to_cycles(usec)	(TIMER_FREQ * (unsigned long)(usec) / 1000000)
#define cycles_to_usec(cycles)	((unsigned long)(cycles) * 1000000 / TIMER_FREQ)

static inline unsigned long timer_get_cycles(void)
{
	unsigned long val = 0;

	__asm__ __volatile__(
		"rdtime.d %0, $zero\n\t"
		: "=r"(val)
		:
	);

	return val;
}

static inline unsigned long timer_get_cfg(void)
{
	return csr_read(LOONGARCH_CSR_TCFG);
}

static inline unsigned long timer_get_val(void)
{
	return csr_read(LOONGARCH_CSR_TVAL);
}

static inline void disable_timer(void)
{
	csr_write(0, LOONGARCH_CSR_TCFG);
}

static inline void timer_irq_enable(void)
{
	unsigned long val;

	val = csr_read(LOONGARCH_CSR_ECFG);
	val |= ECFGF_TIMER;
	csr_write(val, LOONGARCH_CSR_ECFG);
}

static inline void timer_irq_disable(void)
{
	unsigned long val;

	val = csr_read(LOONGARCH_CSR_ECFG);
	val &= ~ECFGF_TIMER;
	csr_write(val, LOONGARCH_CSR_ECFG);
}

static inline void timer_set_next_cmp_ms(unsigned int msec, bool period)
{
	unsigned long val;

	val = msec_to_cycles(msec) & CSR_TCFG_VAL;
	val |= CSR_TCFG_EN;
	if (period)
		val |= CSR_TCFG_PERIOD;
	csr_write(val, LOONGARCH_CSR_TCFG);
}

static inline void __delay(uint64_t cycles)
{
	uint64_t start = timer_get_cycles();

	while ((timer_get_cycles() - start) < cycles)
		cpu_relax();
}

static inline void udelay(unsigned long usec)
{
	__delay(usec_to_cycles(usec));
}
#endif /* SELFTEST_KVM_ARCH_TIMER_H */
