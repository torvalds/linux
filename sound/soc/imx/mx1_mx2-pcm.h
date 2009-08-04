/*
 * mx1_mx2-pcm.h :- ASoC platform header for Freescale i.MX1x, i.MX2x
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _MXC_PCM_H
#define _MXC_PCM_H

/* AUDMUX register definitions */
#define AUDMUX_IO_BASE_ADDR	IO_ADDRESS(AUDMUX_BASE_ADDR)

#define DAM_HPCR1	(*((volatile u32 *)(AUDMUX_IO_BASE_ADDR + 0x00)))
#define DAM_HPCR2	(*((volatile u32 *)(AUDMUX_IO_BASE_ADDR + 0x04)))
#define DAM_HPCR3	(*((volatile u32 *)(AUDMUX_IO_BASE_ADDR + 0x08)))
#define DAM_PPCR1	(*((volatile u32 *)(AUDMUX_IO_BASE_ADDR + 0x10)))
#define DAM_PPCR2	(*((volatile u32 *)(AUDMUX_IO_BASE_ADDR + 0x14)))
#define DAM_PPCR3	(*((volatile u32 *)(AUDMUX_IO_BASE_ADDR + 0x1C)))

#define AUDMUX_HPCR_TFSDIR	(1 << 31)
#define AUDMUX_HPCR_TCLKDIR	(1 << 30)
#define AUDMUX_HPCR_TFCSEL(x)	(((x) & 0xff) << 26)
#define AUDMUX_HPCR_RXDSEL(x)	(((x) & 0x7) << 13)
#define AUDMUX_HPCR_SYN		(1 << 12)

#define AUDMUX_PPCR_TFSDIR	(1 << 31)
#define AUDMUX_PPCR_TCLKDIR	(1 << 30)
#define AUDMUX_PPCR_TFCSEL(x)	(((x) & 0xff) << 26)
#define AUDMUX_PPCR_RXDSEL(x)	(((x) & 0x7) << 13)
#define AUDMUX_PPCR_SYN		(1 << 12)

/* DMA information for mx1_mx2 platforms */
struct mx1_mx2_pcm_dma_params {
	char *name;			/* stream identifier */
	unsigned int transfer_type;	/* READ or WRITE DMA transfer */
	dma_addr_t per_address;		/* physical address of SSI fifo */
	int event_id;			/* fixed DMA number for SSI fifo */
	int watermark_level;		/* SSI fifo watermark level */
	int per_config;			/* DMA Config flags for peripheral */
	int mem_config;			/* DMA Config flags for RAM */
 };

/* platform data */
extern struct snd_soc_platform mx1_mx2_soc_platform;

#endif
