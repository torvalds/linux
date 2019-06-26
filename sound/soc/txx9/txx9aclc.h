/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * TXx9 SoC AC Link Controller
 */

#ifndef __TXX9ACLC_H
#define __TXX9ACLC_H

#include <linux/interrupt.h>
#include <asm/txx9/dmac.h>

#define ACCTLEN			0x00	/* control enable */
#define ACCTLDIS		0x04	/* control disable */
#define   ACCTL_ENLINK		0x00000001	/* enable/disable AC-link */
#define   ACCTL_AUDODMA		0x00000100	/* AUDODMA enable/disable */
#define   ACCTL_AUDIDMA		0x00001000	/* AUDIDMA enable/disable */
#define   ACCTL_AUDOEHLT	0x00010000	/* AUDO error halt
						   enable/disable */
#define   ACCTL_AUDIEHLT	0x00100000	/* AUDI error halt
						   enable/disable */
#define ACREGACC		0x08	/* codec register access */
#define   ACREGACC_DAT_SHIFT	0	/* data field */
#define   ACREGACC_REG_SHIFT	16	/* address field */
#define   ACREGACC_CODECID_SHIFT	24	/* CODEC ID field */
#define   ACREGACC_READ		0x80000000	/* CODEC read */
#define   ACREGACC_WRITE	0x00000000	/* CODEC write */
#define ACINTSTS		0x10	/* interrupt status */
#define ACINTMSTS		0x14	/* interrupt masked status */
#define ACINTEN			0x18	/* interrupt enable */
#define ACINTDIS		0x1c	/* interrupt disable */
#define   ACINT_CODECRDY(n)	(0x00000001 << (n))	/* CODECn ready */
#define   ACINT_REGACCRDY	0x00000010	/* ACREGACC ready */
#define   ACINT_AUDOERR		0x00000100	/* AUDO underrun error */
#define   ACINT_AUDIERR		0x00001000	/* AUDI overrun error */
#define ACDMASTS		0x80	/* DMA request status */
#define   ACDMA_AUDO		0x00000001	/* AUDODMA pending */
#define   ACDMA_AUDI		0x00000010	/* AUDIDMA pending */
#define ACAUDODAT		0xa0	/* audio out data */
#define ACAUDIDAT		0xb0	/* audio in data */
#define ACREVID			0xfc	/* revision ID */

struct txx9aclc_dmadata {
	struct resource *dma_res;
	struct txx9dmac_slave dma_slave;
	struct dma_chan *dma_chan;
	struct tasklet_struct tasklet;
	spinlock_t dma_lock;
	int stream; /* SNDRV_PCM_STREAM_PLAYBACK or SNDRV_PCM_STREAM_CAPTURE */
	struct snd_pcm_substream *substream;
	unsigned long pos;
	dma_addr_t dma_addr;
	unsigned long buffer_bytes;
	unsigned long period_bytes;
	unsigned long frag_bytes;
	int frags;
	int frag_count;
	int dmacount;
};

struct txx9aclc_plat_drvdata {
	void __iomem *base;
	u64 physbase;
};

static inline struct txx9aclc_plat_drvdata *txx9aclc_get_plat_drvdata(
	struct snd_soc_dai *dai)
{
	return dev_get_drvdata(dai->dev);
}

#endif /* __TXX9ACLC_H */
