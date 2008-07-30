/* sound/soc/at32/at32-pcm.h
 * ASoC PCM interface for Atmel AT32 SoC
 *
 * Copyright (C) 2008 Long Range Systems
 *    Geoffrey Wossum <gwossum@acm.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __SOUND_SOC_AT32_AT32_PCM_H
#define __SOUND_SOC_AT32_AT32_PCM_H __FILE__

#include <linux/atmel-ssc.h>


/*
 * Registers and status bits that are required by the PCM driver
 * TODO: Is ptcr really used?
 */
struct at32_pdc_regs {
	u32 xpr;		/* PDC RX/TX pointer */
	u32 xcr;		/* PDC RX/TX counter */
	u32 xnpr;		/* PDC next RX/TX pointer */
	u32 xncr;		/* PDC next RX/TX counter */
	u32 ptcr;		/* PDC transfer control */
};



/*
 * SSC mask info
 */
struct at32_ssc_mask {
	u32 ssc_enable;		/* SSC RX/TX enable */
	u32 ssc_disable;	/* SSC RX/TX disable */
	u32 ssc_endx;		/* SSC ENDTX or ENDRX */
	u32 ssc_endbuf;		/* SSC TXBUFF or RXBUFF */
	u32 pdc_enable;		/* PDC RX/TX enable */
	u32 pdc_disable;	/* PDC RX/TX disable */
};



/*
 * This structure, shared between the PCM driver and the interface,
 * contains all information required by the PCM driver to perform the
 * PDC DMA operation.  All fields except dma_intr_handler() are initialized
 * by the interface.  The dms_intr_handler() pointer is set by the PCM
 * driver and called by the interface SSC interrupt handler if it is
 * non-NULL.
 */
struct at32_pcm_dma_params {
	char *name;		/* stream identifier */
	int pdc_xfer_size;	/* PDC counter increment in bytes */
	struct ssc_device *ssc;	/* SSC device for stream */
	struct at32_pdc_regs *pdc;	/* PDC register info */
	struct at32_ssc_mask *mask;	/* SSC mask info */
	struct snd_pcm_substream *substream;
	void (*dma_intr_handler) (u32, struct snd_pcm_substream *);
};



/*
 * The AT32 ASoC platform driver
 */
extern struct snd_soc_platform at32_soc_platform;



/*
 * SSC register access (since ssc_writel() / ssc_readl() require literal name)
 */
#define ssc_readx(base, reg)            (__raw_readl((base) + (reg)))
#define ssc_writex(base, reg, value)    __raw_writel((value), (base) + (reg))

#endif /* __SOUND_SOC_AT32_AT32_PCM_H */
