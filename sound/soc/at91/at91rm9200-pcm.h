/*
 * at91rm9200-pcm.h - ALSA PCM interface for the Atmel AT91RM9200 chip
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

/*
 * Registers and status bits that are required by the PCM driver.
 */
struct at91rm9200_ssc_regs {
	void __iomem 	*cr;		/* SSC control */
	void __iomem	*ier;		/* SSC interrupt enable */
	void __iomem	*idr;		/* SSC interrupt disable */
};

struct at91rm9200_pdc_regs {
	void __iomem	*xpr;		/* PDC recv/trans pointer */
	void __iomem	*xcr;		/* PDC recv/trans counter */
	void __iomem	*xnpr;		/* PDC next recv/trans pointer */
	void __iomem	*xncr;		/* PDC next recv/trans counter */
	void __iomem	*ptcr;		/* PDC transfer control */
};

struct at91rm9200_ssc_mask {
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
typedef struct {
	char *name;			/* stream identifier */
	int pdc_xfer_size;		/* PDC counter increment in bytes */
	struct at91rm9200_ssc_regs *ssc; /* SSC register addresses */
	struct at91rm9200_pdc_regs *pdc; /* PDC receive/transmit registers */
	struct at91rm9200_ssc_mask *mask;/* SSC & PDC status bits */
	snd_pcm_substream_t *substream;
	void (*dma_intr_handler)(u32, snd_pcm_substream_t *);
} at91rm9200_pcm_dma_params_t;

extern struct snd_soc_cpu_dai at91rm9200_i2s_dai[3];
extern struct snd_soc_platform at91rm9200_soc_platform;


/*
 * SSC I/O helpers.
 * E.g., at91_ssc_write(AT91_SSC(1) + AT91_SSC_CR, AT91_SSC_RXEN);
 */
#define AT91_SSC(x) (((x)==0) ? AT91_VA_BASE_SSC0 :\
	 ((x)==1) ? AT91_VA_BASE_SSC1 : ((x)==2) ? AT91_VA_BASE_SSC2 : NULL)
#define at91_ssc_read(a)	((unsigned long) __raw_readl(a))
#define at91_ssc_write(a,v)	__raw_writel((v),(a))
