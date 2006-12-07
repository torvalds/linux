#ifndef __ASM_ARCH_OMAP24XX_H
#define __ASM_ARCH_OMAP24XX_H

/*
 * Please place only base defines here and put the rest in device
 * specific headers. Note also that some of these defines are needed
 * for omap1 to compile without adding ifdefs.
 */

#define L4_24XX_BASE		0x48000000
#define L3_24XX_BASE		0x68000000

/* interrupt controller */
#define OMAP24XX_IC_BASE	(L4_24XX_BASE + 0xfe000)
#define VA_IC_BASE		IO_ADDRESS(OMAP24XX_IC_BASE)
#define OMAP24XX_IVA_INTC_BASE	0x40000000
#define IRQ_SIR_IRQ		0x0040

#define OMAP24XX_32KSYNCT_BASE	(L4_24XX_BASE + 0x4000)
#define OMAP24XX_PRCM_BASE	(L4_24XX_BASE + 0x8000)
#define OMAP24XX_SDRC_BASE	(L3_24XX_BASE + 0x9000)

/* DSP SS */
#define OMAP24XX_DSP_BASE	0x58000000
#define OMAP24XX_DSP_MEM_BASE	(OMAP24XX_DSP_BASE + 0x0)
#define OMAP24XX_DSP_IPI_BASE	(OMAP24XX_DSP_BASE + 0x1000000)
#define OMAP24XX_DSP_MMU_BASE	(OMAP24XX_DSP_BASE + 0x2000000)

/* Mailbox */
#define OMAP24XX_MAILBOX_BASE	(L4_24XX_BASE + 0x94000)

#endif /* __ASM_ARCH_OMAP24XX_H */

