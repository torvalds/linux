/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * This header provides constants for the ARM GIC.
 */

#ifndef _DT_BINDINGS_INTERRUPT_CONTROLLER_ARM_GIC_H
#define _DT_BINDINGS_INTERRUPT_CONTROLLER_ARM_GIC_H

#include <dt-bindings/interrupt-controller/irq.h>

/* interrupt specifier cell 0 */

#define GIC_SPI 0
#define GIC_PPI 1
#define GIC_ESPI 2
#define GIC_EPPI 3

/*
 * Interrupt specifier cell 2.
 * The flags in irq.h are valid, plus those below.
 */
#define GIC_CPU_MASK_RAW(x) ((x) << 8)
#define GIC_CPU_MASK_SIMPLE(num) GIC_CPU_MASK_RAW((1 << (num)) - 1)

#endif
