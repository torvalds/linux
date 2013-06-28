/*
 * Copyright (C) ST-Ericsson SA 2010-2013
 * Author: Rickard Andersson <rickard.andersson@stericsson.com> for
 *         ST-Ericsson.
 * Author: Daniel Lezcano <daniel.lezcano@linaro.org> for Linaro.
 * License terms: GNU General Public License (GPL) version 2
 *
 */

#ifndef ARM_UX500_PM_H
#define ARM_UX500_PM_H

int prcmu_gic_decouple(void);
int prcmu_gic_recouple(void);
bool prcmu_gic_pending_irq(void);
bool prcmu_pending_irq(void);
bool prcmu_is_cpu_in_wfi(int cpu);
int prcmu_copy_gic_settings(void);
void ux500_pm_init(u32 phy_base, u32 size);

#endif /* ARM_UX500_PM_H */
