#ifndef __ASM_CPU_SH4_DMA_H
#define __ASM_CPU_SH4_DMA_H

#define SH_DMAC_BASE	0xffa00000

#define SAR	((unsigned long[]){SH_DMAC_BASE + 0x00, SH_DMAC_BASE + 0x10, \
				   SH_DMAC_BASE + 0x20, SH_DMAC_BASE + 0x30})
#define DAR	((unsigned long[]){SH_DMAC_BASE + 0x04, SH_DMAC_BASE + 0x14, \
				   SH_DMAC_BASE + 0x24, SH_DMAC_BASE + 0x34})
#define DMATCR	((unsigned long[]){SH_DMAC_BASE + 0x08, SH_DMAC_BASE + 0x18, \
				   SH_DMAC_BASE + 0x28, SH_DMAC_BASE + 0x38})
#define CHCR	((unsigned long[]){SH_DMAC_BASE + 0x0c, SH_DMAC_BASE + 0x1c, \
				   SH_DMAC_BASE + 0x2c, SH_DMAC_BASE + 0x3c})
#define DMAOR	(SH_DMAC_BASE + 0x40)

#endif /* __ASM_CPU_SH4_DMA_H */

