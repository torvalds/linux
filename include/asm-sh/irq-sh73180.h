#ifndef __ASM_SH_IRQ_SH73180_H
#define __ASM_SH_IRQ_SH73180_H

/*
 * linux/include/asm-sh/irq-sh73180.h
 *
 * Copyright (C) 2004 Takashi SHUDO <shudo@hitachi-ul.co.jp>
 */

#undef INTC_IPRA
#undef INTC_IPRB
#undef INTC_IPRC
#undef INTC_IPRD

#undef DMTE0_IRQ
#undef DMTE1_IRQ
#undef DMTE2_IRQ
#undef DMTE3_IRQ
#undef DMTE4_IRQ
#undef DMTE5_IRQ
#undef DMTE6_IRQ
#undef DMTE7_IRQ
#undef DMAE_IRQ
#undef DMA_IPR_ADDR
#undef DMA_IPR_POS
#undef DMA_PRIORITY

#undef NR_IRQS

#undef __irq_demux
#undef irq_demux

#undef INTC_IMCR0
#undef INTC_IMCR1
#undef INTC_IMCR2
#undef INTC_IMCR3
#undef INTC_IMCR4
#undef INTC_IMCR5
#undef INTC_IMCR6
#undef INTC_IMCR7
#undef INTC_IMCR8
#undef INTC_IMCR9
#undef INTC_IMCR10


#define INTC_IPRA  	0xA4080000UL
#define INTC_IPRB  	0xA4080004UL
#define INTC_IPRC  	0xA4080008UL
#define INTC_IPRD  	0xA408000CUL
#define INTC_IPRE  	0xA4080010UL
#define INTC_IPRF  	0xA4080014UL
#define INTC_IPRG  	0xA4080018UL
#define INTC_IPRH  	0xA408001CUL
#define INTC_IPRI  	0xA4080020UL
#define INTC_IPRJ  	0xA4080024UL
#define INTC_IPRK  	0xA4080028UL

#define INTC_IMR0	0xA4080080UL
#define INTC_IMR1	0xA4080084UL
#define INTC_IMR2	0xA4080088UL
#define INTC_IMR3	0xA408008CUL
#define INTC_IMR4	0xA4080090UL
#define INTC_IMR5	0xA4080094UL
#define INTC_IMR6	0xA4080098UL
#define INTC_IMR7	0xA408009CUL
#define INTC_IMR8	0xA40800A0UL
#define INTC_IMR9	0xA40800A4UL
#define INTC_IMR10	0xA40800A8UL
#define INTC_IMR11	0xA40800ACUL

#define INTC_IMCR0	0xA40800C0UL
#define INTC_IMCR1	0xA40800C4UL
#define INTC_IMCR2	0xA40800C8UL
#define INTC_IMCR3	0xA40800CCUL
#define INTC_IMCR4	0xA40800D0UL
#define INTC_IMCR5	0xA40800D4UL
#define INTC_IMCR6	0xA40800D8UL
#define INTC_IMCR7	0xA40800DCUL
#define INTC_IMCR8	0xA40800E0UL
#define INTC_IMCR9	0xA40800E4UL
#define INTC_IMCR10	0xA40800E8UL
#define INTC_IMCR11	0xA40800ECUL

#define INTC_ICR0	0xA4140000UL
#define INTC_ICR1	0xA414001CUL

#define INTMSK0		0xa4140044
#define INTMSKCLR0	0xa4140064
#define INTC_INTPRI0	0xa4140010

/*
  NOTE:

  *_IRQ = (INTEVT2 - 0x200)/0x20
*/

/* TMU0 */
#define TMU0_IRQ	16
#define TMU0_IPR_ADDR	INTC_IPRA
#define TMU0_IPR_POS	 3
#define TMU0_PRIORITY	 2

#define TIMER_IRQ       16
#define TIMER_IPR_ADDR  INTC_IPRA
#define TIMER_IPR_POS    3
#define TIMER_PRIORITY   2

/* TMU1 */
#define TMU1_IRQ	17
#define TMU1_IPR_ADDR	INTC_IPRA
#define TMU1_IPR_POS	 2
#define TMU1_PRIORITY	 2

/* TMU2 */
#define TMU2_IRQ	18
#define TMU2_IPR_ADDR	INTC_IPRA
#define TMU2_IPR_POS	 1
#define TMU2_PRIORITY	 2

/* LCDC */
#define LCDC_IRQ	28
#define LCDC_IPR_ADDR	INTC_IPRB
#define LCDC_IPR_POS	 2
#define LCDC_PRIORITY	 2

/* VIO (Video I/O) */
#define CEU_IRQ		52
#define BEU_IRQ		53
#define VEU_IRQ		54
#define VOU_IRQ		55
#define VIO_IPR_ADDR	INTC_IPRE
#define VIO_IPR_POS	 2
#define VIO_PRIORITY	 2

/* MFI (Multi Functional Interface) */
#define MFI_IRQ		56
#define MFI_IPR_ADDR	INTC_IPRE
#define MFI_IPR_POS	 1
#define MFI_PRIORITY	 2

/* VPU (Video Processing Unit) */
#define VPU_IRQ		60
#define VPU_IPR_ADDR	INTC_IPRE
#define VPU_IPR_POS	 0
#define VPU_PRIORITY	 2

/* 3DG */
#define TDG_IRQ		63
#define TDG_IPR_ADDR	INTC_IPRJ
#define TDG_IPR_POS	 2
#define TDG_PRIORITY	 2

/* DMAC(1) */
#define DMTE0_IRQ	48
#define DMTE1_IRQ	49
#define DMTE2_IRQ	50
#define DMTE3_IRQ	51
#define DMA1_IPR_ADDR	INTC_IPRE
#define DMA1_IPR_POS	3
#define DMA1_PRIORITY	7

/* DMAC(2) */
#define DMTE4_IRQ	76
#define DMTE5_IRQ	77
#define DMA2_IPR_ADDR	INTC_IPRF
#define DMA2_IPR_POS	2
#define DMA2_PRIORITY	7

/* SCIF0 */
#define SCIF_ERI_IRQ	80
#define SCIF_RXI_IRQ	81
#define SCIF_BRI_IRQ	82
#define SCIF_TXI_IRQ	83
#define SCIF_IPR_ADDR	INTC_IPRG
#define SCIF_IPR_POS	3
#define SCIF_PRIORITY	3

/* SIOF0 */
#define SIOF0_IRQ	84
#define SIOF0_IPR_ADDR	INTC_IPRH
#define SIOF0_IPR_POS	3
#define SIOF0_PRIORITY	3

/* FLCTL (Flash Memory Controller) */
#define FLSTE_IRQ	92
#define FLTEND_IRQ	93
#define FLTRQ0_IRQ	94
#define FLTRQ1_IRQ	95
#define FLCTL_IPR_ADDR	INTC_IPRH
#define FLCTL_IPR_POS	1
#define FLCTL_PRIORITY	3

/* IIC(0) (IIC Bus Interface) */
#define IIC0_ALI_IRQ	96
#define IIC0_TACKI_IRQ	97
#define IIC0_WAITI_IRQ	98
#define IIC0_DTEI_IRQ	99
#define IIC0_IPR_ADDR	INTC_IPRH
#define IIC0_IPR_POS	0
#define IIC0_PRIORITY	3

/* IIC(1) (IIC Bus Interface) */
#define IIC1_ALI_IRQ	44
#define IIC1_TACKI_IRQ	45
#define IIC1_WAITI_IRQ	46
#define IIC1_DTEI_IRQ	47
#define IIC1_IPR_ADDR	INTC_IPRG
#define IIC1_IPR_POS	0
#define IIC1_PRIORITY	3

/* SIO0 */
#define SIO0_IRQ	88
#define SIO0_IPR_ADDR	INTC_IPRI
#define SIO0_IPR_POS	3
#define SIO0_PRIORITY	3

/* SDHI */
#define SDHI_SDHII0_IRQ	100
#define SDHI_SDHII1_IRQ	101
#define SDHI_SDHII2_IRQ	102
#define SDHI_SDHII3_IRQ	103
#define SDHI_IPR_ADDR	INTC_IPRK
#define SDHI_IPR_POS	0
#define SDHI_PRIORITY	3

/* SIU (Sound Interface Unit) */
#define SIU_IRQ		108
#define SIU_IPR_ADDR	INTC_IPRJ
#define SIU_IPR_POS	1
#define SIU_PRIORITY	3


/* ONCHIP_NR_IRQS */
#define NR_IRQS 109

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
extern void make_ipr_irq(unsigned int irq, unsigned int addr,
			 int pos,  int priority);
extern void make_imask_irq(unsigned int irq);

#define PORT_PACR	0xA4050100UL
#define PORT_PBCR	0xA4050102UL
#define PORT_PCCR	0xA4050104UL
#define PORT_PDCR	0xA4050106UL
#define PORT_PECR	0xA4050108UL
#define PORT_PFCR	0xA405010AUL
#define PORT_PGCR	0xA405010CUL
#define PORT_PHCR	0xA405010EUL
#define PORT_PJCR	0xA4050110UL
#define PORT_PKCR	0xA4050112UL
#define PORT_PLCR	0xA4050114UL
#define PORT_SCPCR	0xA4050116UL
#define PORT_PMCR	0xA4050118UL
#define PORT_PNCR	0xA405011AUL
#define PORT_PQCR	0xA405011CUL
#define PORT_PRCR	0xA405011EUL
#define PORT_PTCR	0xA405014CUL
#define PORT_PUCR	0xA405014EUL
#define PORT_PVCR	0xA4050150UL

#define PORT_PSELA	0xA4050140UL
#define PORT_PSELB	0xA4050142UL
#define PORT_PSELC	0xA4050144UL
#define PORT_PSELE	0xA4050158UL

#define PORT_HIZCRA	0xA4050146UL
#define PORT_HIZCRB	0xA4050148UL
#define PORT_DRVCR	0xA405014AUL

#define PORT_PADR  	0xA4050120UL
#define PORT_PBDR  	0xA4050122UL
#define PORT_PCDR  	0xA4050124UL
#define PORT_PDDR  	0xA4050126UL
#define PORT_PEDR  	0xA4050128UL
#define PORT_PFDR  	0xA405012AUL
#define PORT_PGDR  	0xA405012CUL
#define PORT_PHDR  	0xA405012EUL
#define PORT_PJDR  	0xA4050130UL
#define PORT_PKDR  	0xA4050132UL
#define PORT_PLDR  	0xA4050134UL
#define PORT_SCPDR  	0xA4050136UL
#define PORT_PMDR  	0xA4050138UL
#define PORT_PNDR  	0xA405013AUL
#define PORT_PQDR  	0xA405013CUL
#define PORT_PRDR  	0xA405013EUL
#define PORT_PTDR  	0xA405016CUL
#define PORT_PUDR  	0xA405016EUL
#define PORT_PVDR  	0xA4050170UL

#define IRQ0_IRQ	32
#define IRQ1_IRQ	33
#define IRQ2_IRQ	34
#define IRQ3_IRQ	35
#define IRQ4_IRQ	36
#define IRQ5_IRQ	37
#define IRQ6_IRQ	38
#define IRQ7_IRQ	39

#define INTPRI00	0xA4140010UL

#define IRQ0_IPR_ADDR	INTPRI00
#define IRQ1_IPR_ADDR	INTPRI00
#define IRQ2_IPR_ADDR	INTPRI00
#define IRQ3_IPR_ADDR	INTPRI00
#define IRQ4_IPR_ADDR	INTPRI00
#define IRQ5_IPR_ADDR	INTPRI00
#define IRQ6_IPR_ADDR	INTPRI00
#define IRQ7_IPR_ADDR	INTPRI00

#define IRQ0_IPR_POS	7
#define IRQ1_IPR_POS	6
#define IRQ2_IPR_POS	5
#define IRQ3_IPR_POS	4
#define IRQ4_IPR_POS	3
#define IRQ5_IPR_POS	2
#define IRQ6_IPR_POS	1
#define IRQ7_IPR_POS	0

#define IRQ0_PRIORITY	1
#define IRQ1_PRIORITY	1
#define IRQ2_PRIORITY	1
#define IRQ3_PRIORITY	1
#define IRQ4_PRIORITY	1
#define IRQ5_PRIORITY	1
#define IRQ6_PRIORITY	1
#define IRQ7_PRIORITY	1

extern int shmse_irq_demux(int irq);
#define __irq_demux(irq) shmse_irq_demux(irq)
#define irq_demux(irq) __irq_demux(irq)

#endif /* __ASM_SH_IRQ_SH73180_H */
