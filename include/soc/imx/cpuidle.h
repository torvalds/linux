/*
 * Copyright 2016 Pengutronix, <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __SOC_IMX_CPUIDLE_H__
#define __SOC_IMX_CPUIDLE_H__

#if defined(CONFIG_CPU_IDLE) && defined(CONFIG_SOC_IMX6Q)
void imx6q_cpuidle_fec_irqs_used(void);
void imx6q_cpuidle_fec_irqs_unused(void);
#else
static inline void imx6q_cpuidle_fec_irqs_used(void) { }
static inline void imx6q_cpuidle_fec_irqs_unused(void) { }
#endif

#endif /* __SOC_IMX_CPUIDLE_H__ */
