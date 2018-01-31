/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PLAT_RK_FIQ_DEBUGGER_H
#define __PLAT_RK_FIQ_DEBUGGER_H

#ifdef CONFIG_FIQ_DEBUGGER
void rk_serial_debug_init(void __iomem *base, phys_addr_t phy_base,
			  int irq, int signal_irq,
			  int wakeup_irq, unsigned int baudrate);
#else
static inline void
void rk_serial_debug_init(void __iomem *base, phys_addr_t phy_base,
			  int irq, int signal_irq,
			  int wakeup_irq, unsigned int baudrate)
{
}
#endif

#ifdef CONFIG_FIQ_DEBUGGER_TRUST_ZONE
void fiq_debugger_fiq(void *regs, u32 cpu);
#endif

#endif
