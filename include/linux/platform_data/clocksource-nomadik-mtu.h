#ifndef __PLAT_MTU_H
#define __PLAT_MTU_H

void nmdk_timer_init(void __iomem *base, int irq);
void nmdk_clkevt_reset(void);
void nmdk_clksrc_reset(void);

#endif /* __PLAT_MTU_H */

