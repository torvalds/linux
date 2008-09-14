/*
 * at91-pcm.h - ALSA PCM interface for the Atmel AT91 SoC
 *
 * Author:	Frank Mandarino <fmandarino@endrelia.com>
 *		Endrelia Technologies Inc.
 * Created:	Mar 3, 2006
 *
 * Based on pxa2xx-pcm.h by:
 *
 * Author:	Nicolas Pitre
 * Created:	Nov 30, 2004
 * Copyright:	MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _AT91_PCM_H
#define _AT91_PCM_H

#include <mach/hardware.h>

struct at91_ssc_periph {
	void __iomem	*base;
	u32		pid;
};

/*
 * Registers and status bits that are required by the PCM driver.
 */
struct at91_pdc_regs {
	unsigned int	xpr;		/* PDC recv/trans pointer */
	unsigned int	xcr;		/* PDC recv/trans counter */
	unsigned int	xnpr;		/* PDC next recv/trans pointer */
	unsigned int	xncr;		/* PDC next recv/trans counter */
	unsigned int	ptcr;		/* PDC transfer control */
};

struct at91_ssc_mask {
	u32	ssc_enable;		/* SSC recv/trans enable */
	u32	ssc_disable;		/* SSC recv/trans disable */
	u32	ssc_endx;		/* SSC ENDTX or ENDRX */
	u32	ssc_endbuf;		/* SSC TXBUFE or RXBUFF */
	u32	pdc_enable;		/* PDC recv/trans enable */
	u32	pdc_disable;		/* PDC recv/trans disable */
};

/*
 * This structure, shared between the PCM driver and the interface,
 * contains all information required by the PCM driver to perform the
 * PDC DMA operation.  All fields except dma_intr_handler() are initialized
 * by the interface.  The dms_intr_handler() pointer is set by the PCM
 * driver and called by the interface SSC interrupt handler if it is
 * non-NULL.
 */
struct at91_pcm_dma_params {
	char *name;			/* stream identifier */
	int pdc_xfer_size;		/* PDC counter increment in bytes */
	void __iomem *ssc_base;		/* SSC base address */
	struct at91_pdc_regs *pdc; /* PDC receive or transmit registers */
	struct at91_ssc_mask *mask;/* SSC & PDC status bits */
	struct snd_pcm_substream *substream;
	void (*dma_intr_handler)(u32, struct snd_pcm_substream *);
};

extern struct snd_soc_platform at91_soc_platform;

#define at91_ssc_read(a)	((unsigned long) __raw_readl(a))
#define at91_ssc_write(a,v)	__raw_writel((v),(a))

#endif /* _AT91_PCM_H */
