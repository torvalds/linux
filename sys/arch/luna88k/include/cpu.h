/* $OpenBSD: cpu.h,v 1.11 2025/07/31 15:14:38 miod Exp $ */
/* public domain */
#ifndef	_MACHINE_CPU_H_
#define	_MACHINE_CPU_H_

#include <m88k/asm_macro.h>
#include <m88k/cpu.h>

#ifdef _KERNEL

#define	ci_curspl	ci_cpudep4
#define	ci_swireg	ci_cpudep5
#define	ci_intr_mask	ci_cpudep6
#define	ci_clock_ack	ci_cpudep7

void luna88k_ext_int(struct trapframe *eframe);
#define	md_interrupt_func	luna88k_ext_int

static inline u_long
intr_disable(void)
{
	u_long psr;

	psr = get_psr();
	set_psr(psr | PSR_IND);
	return psr;
}

static inline void
intr_restore(u_long psr)
{
	set_psr(psr);
}

#endif	/* _KERNEL */

#endif	/* _MACHINE_CPU_H_ */
