#ifndef __ASM_SH_IRQ_SH7780_H
#define __ASM_SH_IRQ_SH7780_H

/*
 * linux/include/asm-sh/irq-sh7780.h
 *
 * Copyright (C) 2004 Takashi SHUDO <shudo@hitachi-ul.co.jp>
 */

#ifdef CONFIG_IDE
# ifndef IRQ_CFCARD
#  define IRQ_CFCARD	14
# endif
# ifndef IRQ_PCMCIA
#  define IRQ_PCMCIA	15
# endif
#endif

#define INTC_BASE	0xffd00000
#define INTC_ICR0	(INTC_BASE+0x0)
#define INTC_ICR1	(INTC_BASE+0x1c)
#define INTC_INTPRI	(INTC_BASE+0x10)
#define INTC_INTREQ	(INTC_BASE+0x24)
#define INTC_INTMSK0	(INTC_BASE+0x44)
#define INTC_INTMSK1	(INTC_BASE+0x48)
#define INTC_INTMSK2	(INTC_BASE+0x40080)
#define INTC_INTMSKCLR0	(INTC_BASE+0x64)
#define INTC_INTMSKCLR1	(INTC_BASE+0x68)
#define INTC_INTMSKCLR2	(INTC_BASE+0x40084)
#define INTC_NMIFCR	(INTC_BASE+0xc0)
#define INTC_USERIMASK	(INTC_BASE+0x30000)

#define	INTC_INT2PRI0	(INTC_BASE+0x40000)
#define	INTC_INT2PRI1	(INTC_BASE+0x40004)
#define	INTC_INT2PRI2	(INTC_BASE+0x40008)
#define	INTC_INT2PRI3	(INTC_BASE+0x4000c)
#define	INTC_INT2PRI4	(INTC_BASE+0x40010)
#define	INTC_INT2PRI5	(INTC_BASE+0x40014)
#define	INTC_INT2PRI6	(INTC_BASE+0x40018)
#define	INTC_INT2PRI7	(INTC_BASE+0x4001c)
#define	INTC_INT2A0	(INTC_BASE+0x40030)
#define	INTC_INT2A1	(INTC_BASE+0x40034)
#define	INTC_INT2MSKR	(INTC_BASE+0x40038)
#define	INTC_INT2MSKCR	(INTC_BASE+0x4003c)
#define	INTC_INT2B0	(INTC_BASE+0x40040)
#define	INTC_INT2B1	(INTC_BASE+0x40044)
#define	INTC_INT2B2	(INTC_BASE+0x40048)
#define	INTC_INT2B3	(INTC_BASE+0x4004c)
#define	INTC_INT2B4	(INTC_BASE+0x40050)
#define	INTC_INT2B5	(INTC_BASE+0x40054)
#define	INTC_INT2B6	(INTC_BASE+0x40058)
#define	INTC_INT2B7	(INTC_BASE+0x4005c)
#define	INTC_INT2GPIC	(INTC_BASE+0x40090)
/*
  NOTE:
  *_IRQ = (INTEVT2 - 0x200)/0x20
*/
/* IRQ 0-7 line external int*/
#define IRQ0_IRQ	2
#define IRQ0_IPR_ADDR	INTC_INTPRI
#define IRQ0_IPR_POS	7
#define IRQ0_PRIORITY	2

#define IRQ1_IRQ	4
#define IRQ1_IPR_ADDR	INTC_INTPRI
#define IRQ1_IPR_POS	6
#define IRQ1_PRIORITY	2

#define IRQ2_IRQ	6
#define IRQ2_IPR_ADDR	INTC_INTPRI
#define IRQ2_IPR_POS	5
#define IRQ2_PRIORITY	2

#define IRQ3_IRQ	8
#define IRQ3_IPR_ADDR	INTC_INTPRI
#define IRQ3_IPR_POS	4
#define IRQ3_PRIORITY	2

#define IRQ4_IRQ	10
#define IRQ4_IPR_ADDR	INTC_INTPRI
#define IRQ4_IPR_POS	3
#define IRQ4_PRIORITY	2

#define IRQ5_IRQ	12
#define IRQ5_IPR_ADDR	INTC_INTPRI
#define IRQ5_IPR_POS	2
#define IRQ5_PRIORITY	2

#define IRQ6_IRQ	14
#define IRQ6_IPR_ADDR	INTC_INTPRI
#define IRQ6_IPR_POS	1
#define IRQ6_PRIORITY	2

#define IRQ7_IRQ	0
#define IRQ7_IPR_ADDR	INTC_INTPRI
#define IRQ7_IPR_POS	0
#define IRQ7_PRIORITY	2

/* TMU */
/* ch0 */
#define TMU_IRQ		28
#define	TMU_IPR_ADDR	INTC_INT2PRI0
#define	TMU_IPR_POS	3
#define TMU_PRIORITY	2

#define TIMER_IRQ	28
#define	TIMER_IPR_ADDR	INTC_INT2PRI0
#define	TIMER_IPR_POS	3
#define TIMER_PRIORITY	2

/* ch 1*/
#define TMU_CH1_IRQ		29
#define	TMU_CH1_IPR_ADDR	INTC_INT2PRI0
#define	TMU_CH1_IPR_POS		2
#define TMU_CH1_PRIORITY	2

#define TIMER1_IRQ	29
#define	TIMER1_IPR_ADDR	INTC_INT2PRI0
#define	TIMER1_IPR_POS	2
#define TIMER1_PRIORITY	2

/* ch 2*/
#define TMU_CH2_IRQ		30
#define	TMU_CH2_IPR_ADDR	INTC_INT2PRI0
#define	TMU_CH2_IPR_POS		1
#define TMU_CH2_PRIORITY	2
/* ch 2 Input capture */
#define TMU_CH2IC_IRQ		31
#define	TMU_CH2IC_IPR_ADDR	INTC_INT2PRI0
#define	TMU_CH2IC_IPR_POS	0
#define TMU_CH2IC_PRIORITY	2
/* ch 3 */
#define TMU_CH3_IRQ		96
#define	TMU_CH3_IPR_ADDR	INTC_INT2PRI1
#define	TMU_CH3_IPR_POS		3
#define TMU_CH3_PRIORITY	2
/* ch 4 */
#define TMU_CH4_IRQ		97
#define	TMU_CH4_IPR_ADDR	INTC_INT2PRI1
#define	TMU_CH4_IPR_POS		2
#define TMU_CH4_PRIORITY	2
/* ch 5*/
#define TMU_CH5_IRQ		98
#define	TMU_CH5_IPR_ADDR	INTC_INT2PRI1
#define	TMU_CH5_IPR_POS		1
#define TMU_CH5_PRIORITY	2

#define	RTC_IRQ		22
#define	RTC_IPR_ADDR	INTC_INT2PRI1
#define	RTC_IPR_POS	0
#define	RTC_PRIORITY	TIMER_PRIORITY

/* SCIF0 */
#define SCIF0_ERI_IRQ	40
#define SCIF0_RXI_IRQ	41
#define SCIF0_BRI_IRQ	42
#define SCIF0_TXI_IRQ	43
#define	SCIF0_IPR_ADDR	INTC_INT2PRI2
#define	SCIF0_IPR_POS	3
#define SCIF0_PRIORITY	3

/* SCIF1 */
#define SCIF1_ERI_IRQ	76
#define SCIF1_RXI_IRQ	77
#define SCIF1_BRI_IRQ	78
#define SCIF1_TXI_IRQ	79
#define	SCIF1_IPR_ADDR	INTC_INT2PRI2
#define	SCIF1_IPR_POS	2
#define SCIF1_PRIORITY	3

#define	WDT_IRQ		27
#define	WDT_IPR_ADDR	INTC_INT2PRI2
#define	WDT_IPR_POS	1
#define	WDT_PRIORITY	2

/* DMAC(0) */
#define	DMINT0_IRQ	34
#define	DMINT1_IRQ	35
#define	DMINT2_IRQ	36
#define	DMINT3_IRQ	37
#define	DMINT4_IRQ	44
#define	DMINT5_IRQ	45
#define	DMINT6_IRQ	46
#define	DMINT7_IRQ	47
#define	DMAE_IRQ	38
#define	DMA0_IPR_ADDR	INTC_INT2PRI3
#define	DMA0_IPR_POS	2
#define	DMA0_PRIORITY	7

/* DMAC(1) */
#define	DMINT8_IRQ	92
#define	DMINT9_IRQ	93
#define	DMINT10_IRQ	94
#define	DMINT11_IRQ	95
#define	DMA1_IPR_ADDR	INTC_INT2PRI3
#define	DMA1_IPR_POS	1
#define	DMA1_PRIORITY	7

#define	DMTE0_IRQ	DMINT0_IRQ
#define	DMTE4_IRQ	DMINT4_IRQ
#define	DMA_IPR_ADDR	DMA0_IPR_ADDR
#define	DMA_IPR_POS	DMA0_IPR_POS
#define	DMA_PRIORITY	DMA0_PRIORITY

/* CMT */
#define	CMT_IRQ		56
#define	CMT_IPR_ADDR	INTC_INT2PRI4
#define	CMT_IPR_POS	3
#define	CMT_PRIORITY	0

/* HAC */
#define	HAC_IRQ		60
#define	HAC_IPR_ADDR	INTC_INT2PRI4
#define	HAC_IPR_POS	2
#define	CMT_PRIORITY	0

/* PCIC(0) */
#define	PCIC0_IRQ	64
#define	PCIC0_IPR_ADDR	INTC_INT2PRI4
#define	PCIC0_IPR_POS	1
#define	PCIC0_PRIORITY	2

/* PCIC(1) */
#define	PCIC1_IRQ	65
#define	PCIC1_IPR_ADDR	INTC_INT2PRI4
#define	PCIC1_IPR_POS	0
#define	PCIC1_PRIORITY	2

/* PCIC(2) */
#define	PCIC2_IRQ	66
#define	PCIC2_IPR_ADDR	INTC_INT2PRI5
#define	PCIC2_IPR_POS	3
#define	PCIC2_PRIORITY	2

/* PCIC(3) */
#define	PCIC3_IRQ	67
#define	PCIC3_IPR_ADDR	INTC_INT2PRI5
#define	PCIC3_IPR_POS	2
#define	PCIC3_PRIORITY	2

/* PCIC(4) */
#define	PCIC4_IRQ	68
#define	PCIC4_IPR_ADDR	INTC_INT2PRI5
#define	PCIC4_IPR_POS	1
#define	PCIC4_PRIORITY	2

/* PCIC(5) */
#define	PCICERR_IRQ	69
#define	PCICPWD3_IRQ	70
#define	PCICPWD2_IRQ	71
#define	PCICPWD1_IRQ	72
#define	PCICPWD0_IRQ	73
#define	PCIC5_IPR_ADDR	INTC_INT2PRI5
#define	PCIC5_IPR_POS	0
#define	PCIC5_PRIORITY	2

/* SIOF */
#define	SIOF_IRQ	80
#define	SIOF_IPR_ADDR	INTC_INT2PRI6
#define	SIOF_IPR_POS	3
#define	SIOF_PRIORITY	3

/* HSPI */
#define	HSPI_IRQ	84
#define	HSPI_IPR_ADDR	INTC_INT2PRI6
#define	HSPI_IPR_POS	2
#define	HSPI_PRIORITY	3

/* MMCIF */
#define	MMCIF_FSTAT_IRQ	88
#define	MMCIF_TRAN_IRQ	89
#define	MMCIF_ERR_IRQ	90
#define	MMCIF_FRDY_IRQ	91
#define	MMCIF_IPR_ADDR	INTC_INT2PRI6
#define	MMCIF_IPR_POS	1
#define	HSPI_PRIORITY	3

/* SSI */
#define	SSI_IRQ		100
#define	SSI_IPR_ADDR	INTC_INT2PRI6
#define	SSI_IPR_POS	0
#define	SSI_PRIORITY	3

/* FLCTL */
#define	FLCTL_FLSTE_IRQ		104
#define	FLCTL_FLTEND_IRQ	105
#define	FLCTL_FLTRQ0_IRQ	106
#define	FLCTL_FLTRQ1_IRQ	107
#define	FLCTL_IPR_ADDR		INTC_INT2PRI7
#define	FLCTL_IPR_POS		3
#define	FLCTL_PRIORITY		3

/* GPIO */
#define	GPIO0_IRQ	108
#define	GPIO1_IRQ	109
#define	GPIO2_IRQ	110
#define	GPIO3_IRQ	111
#define	GPIO_IPR_ADDR	INTC_INT2PRI7
#define	GPIO_IPR_POS	2
#define	GPIO_PRIORITY	3

/* ONCHIP_NR_IRQS */
#define NR_IRQS 150	/* 111 + 16 */

/* In a generic kernel, NR_IRQS is an upper bound, and we should use
 * ACTUAL_NR_IRQS (which uses the machine vector) to get the correct value.
 */
#define ACTUAL_NR_IRQS NR_IRQS

extern void disable_irq(unsigned int);
extern void disable_irq_nosync(unsigned int);
extern void enable_irq(unsigned int);

/*
 * Simple Mask Register Support
 */
extern void make_maskreg_irq(unsigned int irq);
extern unsigned short *irq_mask_register;

/*
 * Function for "on chip support modules".
 */
extern void make_imask_irq(unsigned int irq);

#define	INTC_TMU0_MSK	0
#define	INTC_TMU3_MSK	1
#define	INTC_RTC_MSK	2
#define	INTC_SCIF0_MSK	3
#define	INTC_SCIF1_MSK	4
#define	INTC_WDT_MSK	5
#define	INTC_HUID_MSK	7
#define	INTC_DMAC0_MSK	8
#define	INTC_DMAC1_MSK	9
#define	INTC_CMT_MSK	12
#define	INTC_HAC_MSK	13
#define	INTC_PCIC0_MSK	14
#define	INTC_PCIC1_MSK	15
#define	INTC_PCIC2_MSK	16
#define	INTC_PCIC3_MSK	17
#define	INTC_PCIC4_MSK	18
#define	INTC_PCIC5_MSK	19
#define	INTC_SIOF_MSK	20
#define	INTC_HSPI_MSK	21
#define	INTC_MMCIF_MSK	22
#define	INTC_SSI_MSK	23
#define	INTC_FLCTL_MSK	24
#define	INTC_GPIO_MSK	25

#endif /* __ASM_SH_IRQ_SH7780_H */
