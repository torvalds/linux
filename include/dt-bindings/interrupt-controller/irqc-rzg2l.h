/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * This header provides constants for Renesas RZ/G2L family IRQC bindings.
 *
 * Copyright (C) 2022 Renesas Electronics Corp.
 *
 */

#ifndef __DT_BINDINGS_IRQC_RZG2L_H
#define __DT_BINDINGS_IRQC_RZG2L_H

/* NMI maps to SPI0 */
#define RZG2L_NMI	0

/* IRQ0-7 map to SPI1-8 */
#define RZG2L_IRQ0	1
#define RZG2L_IRQ1	2
#define RZG2L_IRQ2	3
#define RZG2L_IRQ3	4
#define RZG2L_IRQ4	5
#define RZG2L_IRQ5	6
#define RZG2L_IRQ6	7
#define RZG2L_IRQ7	8

#endif /* __DT_BINDINGS_IRQC_RZG2L_H */
