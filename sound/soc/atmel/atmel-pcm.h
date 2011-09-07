/*
 * at91-pcm.h - ALSA PCM interface for the Atmel AT91 SoC.
 *
 *  Copyright (C) 2005 SAN People
 *  Copyright (C) 2008 Atmel
 *
 * Authors: Sedji Gaouaou <sedji.gaouaou@atmel.com>
 *
 * Based on at91-pcm. by:
 * Frank Mandarino <fmandarino@endrelia.com>
 * Copyright 2006 Endrelia Technologies Inc.
 *
 * Based on pxa2xx-pcm.c by:
 *
 * Author:	Nicolas Pitre
 * Created:	Nov 30, 2004
 * Copyright:	(C) 2004 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _ATMEL_PCM_H
#define _ATMEL_PCM_H

#include <linux/atmel-ssc.h>

/*
 * Registers and status bits that are required by the PCM driver.
 */
struct atmel_pdc_regs {
	unsigned int	xpr;		/* PDC recv/trans pointer */
	unsigned int	xcr;		/* PDC recv/trans counter */
	unsigned int	xnpr;		/* PDC next recv/trans pointer */
	unsigned int	xncr;		/* PDC next recv/trans counter */
	unsigned int	ptcr;		/* PDC transfer control */
};

struct atmel_ssc_mask {
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
 * by the interface.  The dma_intr_handler() pointer is set by the PCM
 * driver and called by the interface SSC interrupt handler if it is
 * non-NULL.
 */
struct atmel_pcm_dma_params {
	char *name;			/* stream identifier */
	int pdc_xfer_size;		/* PDC counter increment in bytes */
	struct ssc_device *ssc;		/* SSC device for stream */
	struct atmel_pdc_regs *pdc;	/* PDC receive or transmit registers */
	struct atmel_ssc_mask *mask;	/* SSC & PDC status bits */
	struct snd_pcm_substream *substream;
	void (*dma_intr_handler)(u32, struct snd_pcm_substream *);
};

/*
 * SSC register access (since ssc_writel() / ssc_readl() require literal name)
 */
#define ssc_readx(base, reg)            (__raw_readl((base) + (reg)))
#define ssc_writex(base, reg, value)    __raw_writel((value), (base) + (reg))

#endif /* _ATMEL_PCM_H */
