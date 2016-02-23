/*
 * siu.h - ALSA SoC driver for Renesas SH7343, SH7722 SIU peripheral.
 *
 * Copyright (C) 2009-2010 Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 * Copyright (C) 2006 Carlos Munoz <carlos@kenati.com>
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef SIU_H
#define SIU_H

/* Common kernel and user-space firmware-building defines and types */

#define YRAM0_SIZE		(0x0040 / 4)		/* 16 */
#define YRAM1_SIZE		(0x0080 / 4)		/* 32 */
#define YRAM2_SIZE		(0x0040 / 4)		/* 16 */
#define YRAM3_SIZE		(0x0080 / 4)		/* 32 */
#define YRAM4_SIZE		(0x0080 / 4)		/* 32 */
#define YRAM_DEF_SIZE		(YRAM0_SIZE + YRAM1_SIZE + YRAM2_SIZE + \
				 YRAM3_SIZE + YRAM4_SIZE)
#define YRAM_FIR_SIZE		(0x0400 / 4)		/* 256 */
#define YRAM_IIR_SIZE		(0x0200 / 4)		/* 128 */

#define XRAM0_SIZE		(0x0400 / 4)		/* 256 */
#define XRAM1_SIZE		(0x0200 / 4)		/* 128 */
#define XRAM2_SIZE		(0x0200 / 4)		/* 128 */

/* PRAM program array size */
#define PRAM0_SIZE		(0x0100 / 4)		/* 64 */
#define PRAM1_SIZE		((0x2000 - 0x0100) / 4)	/* 1984 */

#include <linux/types.h>

struct siu_spb_param {
	__u32	ab1a;	/* input FIFO address */
	__u32	ab0a;	/* output FIFO address */
	__u32	dir;	/* 0=the ather except CPUOUTPUT, 1=CPUINPUT */
	__u32	event;	/* SPB program starting conditions */
	__u32	stfifo;	/* STFIFO register setting value */
	__u32	trdat;	/* TRDAT register setting value */
};

struct siu_firmware {
	__u32			yram_fir_coeff[YRAM_FIR_SIZE];
	__u32			pram0[PRAM0_SIZE];
	__u32			pram1[PRAM1_SIZE];
	__u32			yram0[YRAM0_SIZE];
	__u32			yram1[YRAM1_SIZE];
	__u32			yram2[YRAM2_SIZE];
	__u32			yram3[YRAM3_SIZE];
	__u32			yram4[YRAM4_SIZE];
	__u32			spbpar_num;
	struct siu_spb_param	spbpar[32];
};

#ifdef __KERNEL__

#include <linux/dmaengine.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/sh_dma.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#define SIU_PERIOD_BYTES_MAX	8192		/* DMA transfer/period size */
#define SIU_PERIOD_BYTES_MIN	256		/* DMA transfer/period size */
#define SIU_PERIODS_MAX		64		/* Max periods in buffer */
#define SIU_PERIODS_MIN		4		/* Min periods in buffer */
#define SIU_BUFFER_BYTES_MAX	(SIU_PERIOD_BYTES_MAX * SIU_PERIODS_MAX)

/* SIU ports: only one can be used at a time */
enum {
	SIU_PORT_A,
	SIU_PORT_B,
	SIU_PORT_NUM,
};

/* SIU clock configuration */
enum {
	SIU_CLKA_PLL,
	SIU_CLKA_EXT,
	SIU_CLKB_PLL,
	SIU_CLKB_EXT
};

struct device;
struct siu_info {
	struct device		*dev;
	int			port_id;
	u32 __iomem		*pram;
	u32 __iomem		*xram;
	u32 __iomem		*yram;
	u32 __iomem		*reg;
	struct siu_firmware	fw;
};

struct siu_stream {
	struct tasklet_struct		tasklet;
	struct snd_pcm_substream	*substream;
	snd_pcm_format_t		format;
	size_t				buf_bytes;
	size_t				period_bytes;
	int				cur_period;	/* Period currently in dma */
	u32				volume;
	snd_pcm_sframes_t		xfer_cnt;	/* Number of frames */
	u8				rw_flg;		/* transfer status */
	/* DMA status */
	struct dma_chan			*chan;		/* DMA channel */
	struct dma_async_tx_descriptor	*tx_desc;
	dma_cookie_t			cookie;
	struct sh_dmae_slave		param;
};

struct siu_port {
	unsigned long		play_cap;	/* Used to track full duplex */
	struct snd_pcm		*pcm;
	struct siu_stream	playback;
	struct siu_stream	capture;
	u32			stfifo;		/* STFIFO value from firmware */
	u32			trdat;		/* TRDAT value from firmware */
};

extern struct siu_port *siu_ports[SIU_PORT_NUM];

static inline struct siu_port *siu_port_info(struct snd_pcm_substream *substream)
{
	struct platform_device *pdev =
		to_platform_device(substream->pcm->card->dev);
	return siu_ports[pdev->id];
}

/* Register access */
static inline void siu_write32(u32 __iomem *addr, u32 val)
{
	__raw_writel(val, addr);
}

static inline u32 siu_read32(u32 __iomem *addr)
{
	return __raw_readl(addr);
}

/* SIU registers */
#define SIU_IFCTL	(0x000 / sizeof(u32))
#define SIU_SRCTL	(0x004 / sizeof(u32))
#define SIU_SFORM	(0x008 / sizeof(u32))
#define SIU_CKCTL	(0x00c / sizeof(u32))
#define SIU_TRDAT	(0x010 / sizeof(u32))
#define SIU_STFIFO	(0x014 / sizeof(u32))
#define SIU_DPAK	(0x01c / sizeof(u32))
#define SIU_CKREV	(0x020 / sizeof(u32))
#define SIU_EVNTC	(0x028 / sizeof(u32))
#define SIU_SBCTL	(0x040 / sizeof(u32))
#define SIU_SBPSET	(0x044 / sizeof(u32))
#define SIU_SBFSTS	(0x068 / sizeof(u32))
#define SIU_SBDVCA	(0x06c / sizeof(u32))
#define SIU_SBDVCB	(0x070 / sizeof(u32))
#define SIU_SBACTIV	(0x074 / sizeof(u32))
#define SIU_DMAIA	(0x090 / sizeof(u32))
#define SIU_DMAIB	(0x094 / sizeof(u32))
#define SIU_DMAOA	(0x098 / sizeof(u32))
#define SIU_DMAOB	(0x09c / sizeof(u32))
#define SIU_DMAML	(0x0a0 / sizeof(u32))
#define SIU_SPSTS	(0x0cc / sizeof(u32))
#define SIU_SPCTL	(0x0d0 / sizeof(u32))
#define SIU_BRGASEL	(0x100 / sizeof(u32))
#define SIU_BRRA	(0x104 / sizeof(u32))
#define SIU_BRGBSEL	(0x108 / sizeof(u32))
#define SIU_BRRB	(0x10c / sizeof(u32))

extern struct snd_soc_platform_driver siu_platform;
extern struct siu_info *siu_i2s_data;

int siu_init_port(int port, struct siu_port **port_info, struct snd_card *card);
void siu_free_port(struct siu_port *port_info);

#endif

#endif /* SIU_H */
