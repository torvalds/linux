#ifndef __ASM_SH_IRQ_H
#define __ASM_SH_IRQ_H

/*
 *
 * linux/include/asm-sh/irq.h
 *
 * Copyright (C) 1999  Niibe Yutaka & Takeshi Yaegashi
 * Copyright (C) 2000  Kazumoto Kojima
 * Copyright (C) 2003  Paul Mundt
 *
 */

#include <linux/config.h>
#include <asm/machvec.h>
#include <asm/ptrace.h>		/* for pt_regs */

#if defined(CONFIG_SH_HP6XX) || \
    defined(CONFIG_SH_RTS7751R2D) || \
    defined(CONFIG_SH_HS7751RVOIP) || \
    defined(CONFIG_SH_HS7751RVOIP) || \
    defined(CONFIG_SH_SH03) || \
    defined(CONFIG_SH_R7780RP) || \
    defined(CONFIG_SH_LANDISK)
#include <asm/mach/ide.h>
#endif

#ifndef CONFIG_CPU_SUBTYPE_SH7780

#define INTC_DMAC0_MSK	0

#if defined(CONFIG_CPU_SH3)
#define INTC_IPRA	0xfffffee2UL
#define INTC_IPRB	0xfffffee4UL
#elif defined(CONFIG_CPU_SH4)
#define INTC_IPRA	0xffd00004UL
#define INTC_IPRB	0xffd00008UL
#define INTC_IPRC	0xffd0000cUL
#define INTC_IPRD	0xffd00010UL
#endif

#ifdef CONFIG_IDE
# ifndef IRQ_CFCARD
#  define IRQ_CFCARD	14
# endif
# ifndef IRQ_PCMCIA
#  define IRQ_PCMCIA	15
# endif
#endif

#define TIMER_IRQ	16
#define TIMER_IPR_ADDR	INTC_IPRA
#define TIMER_IPR_POS	 3
#define TIMER_PRIORITY	 2

#define TIMER1_IRQ	17
#define TIMER1_IPR_ADDR	INTC_IPRA
#define TIMER1_IPR_POS	 2
#define TIMER1_PRIORITY	 4

#define RTC_IRQ		22
#define RTC_IPR_ADDR	INTC_IPRA
#define RTC_IPR_POS	 0
#define RTC_PRIORITY	TIMER_PRIORITY

#if defined(CONFIG_CPU_SH3)
#define DMTE0_IRQ	48
#define DMTE1_IRQ	49
#define DMTE2_IRQ	50
#define DMTE3_IRQ	51
#define DMA_IPR_ADDR	INTC_IPRE
#define DMA_IPR_POS	3
#define DMA_PRIORITY	7
#if defined(CONFIG_CPU_SUBTYPE_SH7300)
/* TMU2 */
#define TIMER2_IRQ      18
#define TIMER2_IPR_ADDR INTC_IPRA
#define TIMER2_IPR_POS   1
#define TIMER2_PRIORITY  2

/* WDT */
#define WDT_IRQ		27
#define WDT_IPR_ADDR	INTC_IPRB
#define WDT_IPR_POS	 3
#define WDT_PRIORITY	 2

/* SIM (SIM Card Module) */
#define SIM_ERI_IRQ	23
#define SIM_RXI_IRQ	24
#define SIM_TXI_IRQ	25
#define SIM_TEND_IRQ	26
#define SIM_IPR_ADDR	INTC_IPRB
#define SIM_IPR_POS	 1
#define SIM_PRIORITY	 2

/* VIO (Video I/O) */
#define VIO_IRQ		52
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

/* KEY (Key Scan Interface) */
#define KEY_IRQ		79
#define KEY_IPR_ADDR	INTC_IPRF
#define KEY_IPR_POS	 3
#define KEY_PRIORITY	 2

/* CMT (Compare Match Timer) */
#define CMT_IRQ		104
#define CMT_IPR_ADDR	INTC_IPRF
#define CMT_IPR_POS	 0
#define CMT_PRIORITY	 2

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

/* IIC (IIC Bus Interface) */
#define IIC_ALI_IRQ	96
#define IIC_TACKI_IRQ	97
#define IIC_WAITI_IRQ	98
#define IIC_DTEI_IRQ	99
#define IIC_IPR_ADDR	INTC_IPRH
#define IIC_IPR_POS	0
#define IIC_PRIORITY	3

/* SIO0 */
#define SIO0_IRQ	88
#define SIO0_IPR_ADDR	INTC_IPRI
#define SIO0_IPR_POS	3
#define SIO0_PRIORITY	3

/* SIU (Sound Interface Unit) */
#define SIU_IRQ		108
#define SIU_IPR_ADDR	INTC_IPRJ
#define SIU_IPR_POS	1
#define SIU_PRIORITY	3

#endif
#elif defined(CONFIG_CPU_SH4)
#define DMTE0_IRQ	34
#define DMTE1_IRQ	35
#define DMTE2_IRQ	36
#define DMTE3_IRQ	37
#define DMTE4_IRQ	44	/* 7751R only */
#define DMTE5_IRQ	45	/* 7751R only */
#define DMTE6_IRQ	46	/* 7751R only */
#define DMTE7_IRQ	47	/* 7751R only */
#define DMAE_IRQ	38
#define DMA_IPR_ADDR	INTC_IPRC
#define DMA_IPR_POS	2
#define DMA_PRIORITY	7
#endif

#if defined (CONFIG_CPU_SUBTYPE_SH7707) || defined (CONFIG_CPU_SUBTYPE_SH7708) || \
    defined (CONFIG_CPU_SUBTYPE_SH7709) || defined (CONFIG_CPU_SUBTYPE_SH7750) || \
    defined (CONFIG_CPU_SUBTYPE_SH7751)
#define SCI_ERI_IRQ	23
#define SCI_RXI_IRQ	24
#define SCI_TXI_IRQ	25
#define SCI_IPR_ADDR	INTC_IPRB
#define SCI_IPR_POS	1
#define SCI_PRIORITY	3
#endif

#if defined(CONFIG_CPU_SUBTYPE_SH7300)
#define SCIF0_IRQ	80
#define SCIF0_IPR_ADDR	INTC_IPRG
#define SCIF0_IPR_POS	3
#define SCIF0_PRIORITY	3
#elif defined(CONFIG_CPU_SUBTYPE_SH7705) || \
      defined(CONFIG_CPU_SUBTYPE_SH7707) || \
      defined(CONFIG_CPU_SUBTYPE_SH7709)
#define SCIF_ERI_IRQ	56
#define SCIF_RXI_IRQ	57
#define SCIF_BRI_IRQ	58
#define SCIF_TXI_IRQ	59
#define SCIF_IPR_ADDR	INTC_IPRE
#define SCIF_IPR_POS	1
#define SCIF_PRIORITY	3

#define IRDA_ERI_IRQ	52
#define IRDA_RXI_IRQ	53
#define IRDA_BRI_IRQ	54
#define IRDA_TXI_IRQ	55
#define IRDA_IPR_ADDR	INTC_IPRE
#define IRDA_IPR_POS	2
#define IRDA_PRIORITY	3
#elif defined(CONFIG_CPU_SUBTYPE_SH7750) || defined(CONFIG_CPU_SUBTYPE_SH7751) || \
      defined(CONFIG_CPU_SUBTYPE_ST40STB1) || defined(CONFIG_CPU_SUBTYPE_SH4_202)
#define SCIF_ERI_IRQ	40
#define SCIF_RXI_IRQ	41
#define SCIF_BRI_IRQ	42
#define SCIF_TXI_IRQ	43
#define SCIF_IPR_ADDR	INTC_IPRC
#define SCIF_IPR_POS	1
#define SCIF_PRIORITY	3
#if defined(CONFIG_CPU_SUBTYPE_ST40STB1)
#define SCIF1_ERI_IRQ	23
#define SCIF1_RXI_IRQ	24
#define SCIF1_BRI_IRQ	25
#define SCIF1_TXI_IRQ	26
#define SCIF1_IPR_ADDR	INTC_IPRB
#define SCIF1_IPR_POS	1
#define SCIF1_PRIORITY	3
#endif /* ST40STB1 */

#endif /* 775x / SH4-202 / ST40STB1 */

/* NR_IRQS is made from three components:
 *   1. ONCHIP_NR_IRQS - number of IRLS + on-chip peripherial modules
 *   2. PINT_NR_IRQS   - number of PINT interrupts
 *   3. OFFCHIP_NR_IRQS - numbe of IRQs from off-chip peripherial modules
 */

/* 1. ONCHIP_NR_IRQS */
#if defined(CONFIG_CPU_SUBTYPE_SH7604)
# define ONCHIP_NR_IRQS 24	// Actually 21
#elif defined(CONFIG_CPU_SUBTYPE_SH7707)
# define ONCHIP_NR_IRQS 64
# define PINT_NR_IRQS   16
#elif defined(CONFIG_CPU_SUBTYPE_SH7708)
# define ONCHIP_NR_IRQS 32
#elif defined(CONFIG_CPU_SUBTYPE_SH7709) || \
      defined(CONFIG_CPU_SUBTYPE_SH7705)
# define ONCHIP_NR_IRQS 64	// Actually 61
# define PINT_NR_IRQS   16
#elif defined(CONFIG_CPU_SUBTYPE_SH7750)
# define ONCHIP_NR_IRQS 48	// Actually 44
#elif defined(CONFIG_CPU_SUBTYPE_SH7751)
# define ONCHIP_NR_IRQS 72
#elif defined(CONFIG_CPU_SUBTYPE_SH7760)
# define ONCHIP_NR_IRQS 112	/* XXX */
#elif defined(CONFIG_CPU_SUBTYPE_SH4_202)
# define ONCHIP_NR_IRQS 72
#elif defined(CONFIG_CPU_SUBTYPE_ST40STB1)
# define ONCHIP_NR_IRQS 144
#elif defined(CONFIG_CPU_SUBTYPE_SH7300)
# define ONCHIP_NR_IRQS 109
#elif defined(CONFIG_SH_UNKNOWN)	/* Most be last */
# define ONCHIP_NR_IRQS 144
#endif

/* 2. PINT_NR_IRQS */
#ifdef CONFIG_SH_UNKNOWN
# define PINT_NR_IRQS 16
#else
# ifndef PINT_NR_IRQS
#  define PINT_NR_IRQS 0
# endif
#endif

#if PINT_NR_IRQS > 0
# define PINT_IRQ_BASE  ONCHIP_NR_IRQS
#endif

/* 3. OFFCHIP_NR_IRQS */
#if defined(CONFIG_HD64461)
# define OFFCHIP_NR_IRQS 18
#elif defined (CONFIG_SH_BIGSUR) /* must be before CONFIG_HD64465 */
# define OFFCHIP_NR_IRQS 48
#elif defined(CONFIG_HD64465)
# define OFFCHIP_NR_IRQS 16
#elif defined (CONFIG_SH_EC3104)
# define OFFCHIP_NR_IRQS 16
#elif defined (CONFIG_SH_DREAMCAST)
# define OFFCHIP_NR_IRQS 96
#elif defined (CONFIG_SH_TITAN)
# define OFFCHIP_NR_IRQS 4
#elif defined(CONFIG_SH_UNKNOWN)
# define OFFCHIP_NR_IRQS 16	/* Must also be last */
#else
# define OFFCHIP_NR_IRQS 0
#endif

#if OFFCHIP_NR_IRQS > 0
# define OFFCHIP_IRQ_BASE (ONCHIP_NR_IRQS + PINT_NR_IRQS)
#endif

/* NR_IRQS. 1+2+3 */
#define NR_IRQS (ONCHIP_NR_IRQS + PINT_NR_IRQS + OFFCHIP_NR_IRQS)

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

#if defined(CONFIG_CPU_SUBTYPE_SH7300)
#undef INTC_IPRA
#undef INTC_IPRB
#define INTC_IPRA  	0xA414FEE2UL
#define INTC_IPRB  	0xA414FEE4UL
#define INTC_IPRC  	0xA4140016UL
#define INTC_IPRD  	0xA4140018UL
#define INTC_IPRE  	0xA414001AUL
#define INTC_IPRF  	0xA4080000UL
#define INTC_IPRG  	0xA4080002UL
#define INTC_IPRH  	0xA4080004UL
#define INTC_IPRI  	0xA4080006UL
#define INTC_IPRJ  	0xA4080008UL

#define INTC_IMR0	0xA4080040UL
#define INTC_IMR1	0xA4080042UL
#define INTC_IMR2	0xA4080044UL
#define INTC_IMR3	0xA4080046UL
#define INTC_IMR4	0xA4080048UL
#define INTC_IMR5	0xA408004AUL
#define INTC_IMR6	0xA408004CUL
#define INTC_IMR7	0xA408004EUL
#define INTC_IMR8	0xA4080050UL
#define INTC_IMR9	0xA4080052UL
#define INTC_IMR10	0xA4080054UL

#define INTC_IMCR0	0xA4080060UL
#define INTC_IMCR1	0xA4080062UL
#define INTC_IMCR2	0xA4080064UL
#define INTC_IMCR3	0xA4080066UL
#define INTC_IMCR4	0xA4080068UL
#define INTC_IMCR5	0xA408006AUL
#define INTC_IMCR6	0xA408006CUL
#define INTC_IMCR7	0xA408006EUL
#define INTC_IMCR8	0xA4080070UL
#define INTC_IMCR9	0xA4080072UL
#define INTC_IMCR10	0xA4080074UL

#define INTC_ICR0	0xA414FEE0UL
#define INTC_ICR1	0xA4140010UL

#define INTC_IRR0	0xA4140004UL

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

#define PORT_PSELA	0xA4050140UL
#define PORT_PSELB	0xA4050142UL
#define PORT_PSELC	0xA4050144UL

#define PORT_HIZCRA	0xA4050146UL
#define PORT_HIZCRB	0xA4050148UL
#define PORT_DRVCR	0xA4050150UL

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

#define IRQ0_IRQ	32
#define IRQ1_IRQ	33
#define IRQ2_IRQ	34
#define IRQ3_IRQ	35
#define IRQ4_IRQ	36
#define IRQ5_IRQ	37

#define IRQ0_IPR_ADDR	INTC_IPRC
#define IRQ1_IPR_ADDR	INTC_IPRC
#define IRQ2_IPR_ADDR	INTC_IPRC
#define IRQ3_IPR_ADDR	INTC_IPRC
#define IRQ4_IPR_ADDR	INTC_IPRD
#define IRQ5_IPR_ADDR	INTC_IPRD

#define IRQ0_IPR_POS	0
#define IRQ1_IPR_POS	1
#define IRQ2_IPR_POS	2
#define IRQ3_IPR_POS	3
#define IRQ4_IPR_POS	0
#define IRQ5_IPR_POS	1

#define IRQ0_PRIORITY	1
#define IRQ1_PRIORITY	1
#define IRQ2_PRIORITY	1
#define IRQ3_PRIORITY	1
#define IRQ4_PRIORITY	1
#define IRQ5_PRIORITY	1

extern int ipr_irq_demux(int irq);
#define __irq_demux(irq) ipr_irq_demux(irq)

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
#elif defined(CONFIG_CPU_SUBTYPE_SH7705) || \
      defined(CONFIG_CPU_SUBTYPE_SH7707) || \
      defined(CONFIG_CPU_SUBTYPE_SH7709)
#define INTC_IRR0	0xa4000004UL
#define INTC_IRR1	0xa4000006UL
#define INTC_IRR2	0xa4000008UL

#define INTC_ICR0	0xfffffee0UL
#define INTC_ICR1	0xa4000010UL
#define INTC_ICR2	0xa4000012UL
#define INTC_INTER	0xa4000014UL

#define INTC_IPRC	0xa4000016UL
#define INTC_IPRD	0xa4000018UL
#define INTC_IPRE	0xa400001aUL
#if defined(CONFIG_CPU_SUBTYPE_SH7707)
#define INTC_IPRF	0xa400001cUL
#elif defined(CONFIG_CPU_SUBTYPE_SH7705)
#define INTC_IPRF	0xa4080000UL
#define INTC_IPRG	0xa4080002UL
#define INTC_IPRH	0xa4080004UL
#endif

#define PORT_PACR	0xa4000100UL
#define PORT_PBCR	0xa4000102UL
#define PORT_PCCR	0xa4000104UL
#define PORT_PFCR	0xa400010aUL
#define PORT_PADR  	0xa4000120UL
#define PORT_PBDR  	0xa4000122UL
#define PORT_PCDR  	0xa4000124UL
#define PORT_PFDR  	0xa400012aUL

#define IRQ0_IRQ	32
#define IRQ1_IRQ	33
#define IRQ2_IRQ	34
#define IRQ3_IRQ	35
#define IRQ4_IRQ	36
#define IRQ5_IRQ	37

#define IRQ0_IPR_ADDR	INTC_IPRC
#define IRQ1_IPR_ADDR	INTC_IPRC
#define IRQ2_IPR_ADDR	INTC_IPRC
#define IRQ3_IPR_ADDR	INTC_IPRC
#define IRQ4_IPR_ADDR	INTC_IPRD
#define IRQ5_IPR_ADDR	INTC_IPRD

#define IRQ0_IPR_POS	0
#define IRQ1_IPR_POS	1
#define IRQ2_IPR_POS	2
#define IRQ3_IPR_POS	3
#define IRQ4_IPR_POS	0
#define IRQ5_IPR_POS	1

#define IRQ0_PRIORITY	1
#define IRQ1_PRIORITY	1
#define IRQ2_PRIORITY	1
#define IRQ3_PRIORITY	1
#define IRQ4_PRIORITY	1
#define IRQ5_PRIORITY	1

#define PINT0_IRQ	40
#define PINT8_IRQ	41

#define PINT0_IPR_ADDR	INTC_IPRD
#define PINT8_IPR_ADDR	INTC_IPRD

#define PINT0_IPR_POS	3
#define PINT8_IPR_POS	2
#define PINT0_PRIORITY	2
#define PINT8_PRIORITY	2

extern int ipr_irq_demux(int irq);
#define __irq_demux(irq) ipr_irq_demux(irq)
#endif /* CONFIG_CPU_SUBTYPE_SH7707 || CONFIG_CPU_SUBTYPE_SH7709 */

#if defined(CONFIG_CPU_SUBTYPE_SH7750) || defined(CONFIG_CPU_SUBTYPE_SH7751) || \
    defined(CONFIG_CPU_SUBTYPE_ST40STB1) || defined(CONFIG_CPU_SUBTYPE_SH4_202)
#define INTC_ICR        0xffd00000
#define INTC_ICR_NMIL	(1<<15)
#define INTC_ICR_MAI	(1<<14)
#define INTC_ICR_NMIB	(1<<9)
#define INTC_ICR_NMIE	(1<<8)
#define INTC_ICR_IRLM	(1<<7)
#endif

#else
#include <asm/irq-sh7780.h>
#endif

/* SH with INTC2-style interrupts */
#ifdef CONFIG_CPU_HAS_INTC2_IRQ
#if defined(CONFIG_CPU_SUBTYPE_ST40STB1)
#define INTC2_BASE	0xfe080000
#define INTC2_FIRST_IRQ 64
#define INTC2_INTREQ_OFFSET	0x20
#define INTC2_INTMSK_OFFSET	0x40
#define INTC2_INTMSKCLR_OFFSET	0x60
#define NR_INTC2_IRQS	25
#elif defined(CONFIG_CPU_SUBTYPE_SH7760)
#define INTC2_BASE	0xfe080000
#define INTC2_FIRST_IRQ 48	/* INTEVT 0x800 */
#define INTC2_INTREQ_OFFSET	0x20
#define INTC2_INTMSK_OFFSET	0x40
#define INTC2_INTMSKCLR_OFFSET	0x60
#define NR_INTC2_IRQS	64
#elif defined(CONFIG_CPU_SUBTYPE_SH7780)
#define INTC2_BASE	0xffd40000
#define INTC2_FIRST_IRQ	22
#define INTC2_INTMSK_OFFSET	(0x38)
#define INTC2_INTMSKCLR_OFFSET	(0x3c)
#define NR_INTC2_IRQS	60
#endif

#define INTC2_INTPRI_OFFSET	0x00

void make_intc2_irq(unsigned int irq,
		    unsigned int ipr_offset, unsigned int ipr_shift,
		    unsigned int msk_offset, unsigned int msk_shift,
		    unsigned int priority);
void init_IRQ_intc2(void);
void intc2_add_clear_irq(int irq, int (*fn)(int));

#endif

static inline int generic_irq_demux(int irq)
{
	return irq;
}

#ifndef __irq_demux
#define __irq_demux(irq)	(irq)
#endif
#define irq_canonicalize(irq)	(irq)
#define irq_demux(irq)		__irq_demux(sh_mv.mv_irq_demux(irq))

#if defined(CONFIG_CPU_SUBTYPE_SH73180)
#include <asm/irq-sh73180.h>
#endif

#endif /* __ASM_SH_IRQ_H */
