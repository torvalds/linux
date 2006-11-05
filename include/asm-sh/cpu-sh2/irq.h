#ifndef __ASM_SH_CPU_SH2_IRQ_H
#define __ASM_SH_CPU_SH2_IRQ_H

/*
 *
 * linux/include/asm-sh/cpu-sh2/irq.h
 *
 * Copyright (C) 1999  Niibe Yutaka & Takeshi Yaegashi
 * Copyright (C) 2000  Kazumoto Kojima
 * Copyright (C) 2003  Paul Mundt
 *
 */

#include <linux/config.h>

#if defined(CONFIG_CPU_SUBTYPE_SH7044)
#define INTC_IPRA	0xffff8348UL
#define INTC_IPRB	0xffff834aUL
#define INTC_IPRC	0xffff834cUL
#define INTC_IPRD	0xffff834eUL
#define INTC_IPRE	0xffff8350UL
#define INTC_IPRF	0xffff8352UL
#define INTC_IPRG	0xffff8354UL
#define INTC_IPRH	0xffff8356UL

#define INTC_ICR	0xffff8358UL
#define INTC_ISR	0xffff835aUL
#elif defined(CONFIG_CPU_SUBTYPE_SH7604)
#define INTC_IPRA	0xfffffee2UL
#define INTC_IPRB	0xfffffe60UL

#define INTC_VCRA	0xfffffe62UL
#define INTC_VCRB	0xfffffe64UL
#define INTC_VCRC	0xfffffe66UL
#define INTC_VCRD	0xfffffe68UL

#define INTC_VCRWDT	0xfffffee4UL
#define INTC_VCRDIV	0xffffff0cUL
#define INTC_VCRDMA0	0xffffffa0UL
#define INTC_VCRDMA1	0xffffffa8UL

#define INTC_ICR	0xfffffee0UL
#elif defined(CONFIG_CPU_SUBTYPE_SH7619)
#define INTC_IPRA	0xf8140006UL
#define INTC_IPRB	0xf8140008UL
#define INTC_IPRC	0xf8080000UL
#define INTC_IPRD	0xf8080002UL
#define INTC_IPRE	0xf8080004UL
#define INTC_IPRF	0xf8080006UL
#define INTC_IPRG	0xf8080008UL

#define INTC_ICR0	0xf8140000UL
#define INTC_IRQCR	0xf8140002UL
#define INTC_IRQSR	0xf8140004UL

#define CMI0_IRQ	86
#define CMI1_IRQ	87

#define SCIF_ERI_IRQ	88
#define SCIF_RXI_IRQ	89
#define SCIF_BRI_IRQ	90
#define SCIF_TXI_IRQ	91
#define SCIF_IPR_ADDR	INTC_IPRD
#define SCIF_IPR_POS	3
#define SCIF_PRIORITY	3

#define SCIF1_ERI_IRQ	92
#define SCIF1_RXI_IRQ	93
#define SCIF1_BRI_IRQ	94
#define SCIF1_TXI_IRQ	95
#define SCIF1_IPR_ADDR	INTC_IPRD
#define SCIF1_IPR_POS	2
#define SCIF1_PRIORITY	3

#define SCIF2_BRI_IRQ	96
#define SCIF2_RXI_IRQ	97
#define SCIF2_ERI_IRQ	98
#define SCIF2_TXI_IRQ	99
#define SCIF2_IPR_ADDR	INTC_IPRD
#define SCIF2_IPR_POS	1
#define SCIF2_PRIORITY	3
#endif

#endif /* __ASM_SH_CPU_SH2_IRQ_H */
