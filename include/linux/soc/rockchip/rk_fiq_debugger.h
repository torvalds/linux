#ifndef __PLAT_RK_FIQ_DEBUGGER_H
#define __PLAT_RK_FIQ_DEBUGGER_H

#ifdef CONFIG_FIQ_DEBUGGER
void rk_serial_debug_init(void __iomem *base, int irq, int signal_irq,
			  int wakeup_irq, unsigned int baudrate);
#else
static inline void
rk_serial_debug_init(void __iomem *base, int irq, int signal_irq,
		     int wakeup_irq, unsigned int baudrate)
{
}
#endif

#ifdef CONFIG_FIQ_DEBUGGER_EL3_TO_EL1
void fiq_debugger_fiq(void *regs);
#endif

#endif
