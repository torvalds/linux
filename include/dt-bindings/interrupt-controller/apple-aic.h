/* SPDX-License-Identifier: GPL-2.0+ OR MIT */
#ifndef _DT_BINDINGS_INTERRUPT_CONTROLLER_APPLE_AIC_H
#define _DT_BINDINGS_INTERRUPT_CONTROLLER_APPLE_AIC_H

#include <dt-bindings/interrupt-controller/irq.h>

#define AIC_IRQ	0
#define AIC_FIQ	1

#define AIC_TMR_HV_PHYS		0
#define AIC_TMR_HV_VIRT		1
#define AIC_TMR_GUEST_PHYS	2
#define AIC_TMR_GUEST_VIRT	3
#define AIC_CPU_PMU_E		4
#define AIC_CPU_PMU_P		5

#endif
