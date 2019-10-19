/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * This header provides constants for most IRQ bindings.
 *
 * Most IRQ bindings include a flags cell as part of the IRQ specifier.
 * In most cases, the format of the flags cell uses the standard values
 * defined in this header.
 */

#ifndef _DT_BINDINGS_INTERRUPT_CONTROLLER_IRQ_H
#define _DT_BINDINGS_INTERRUPT_CONTROLLER_IRQ_H

#define IRQ_TYPE_NONE		0
#define IRQ_TYPE_EDGE_RISING	1
#define IRQ_TYPE_EDGE_FALLING	2
#define IRQ_TYPE_EDGE_BOTH	(IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING)
#define IRQ_TYPE_LEVEL_HIGH	4
#define IRQ_TYPE_LEVEL_LOW	8

#endif
