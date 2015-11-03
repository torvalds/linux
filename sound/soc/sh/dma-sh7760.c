/*
 * SH7760 ("camelot") DMABRG audio DMA unit support
 *
 * Copyright (C) 2007 Manuel Lauss <mano@roarinelk.homelinux.net>
 *  licensed under the terms outlined in the file COPYING at the root
 *  of the linux kernel sources.
 *
 * The SH7760 DMABRG provides 4 dma channels (2x rec, 2x play), which
 * trigger an interrupt when one half of the programmed transfer size
 * has been xmitted.
 *
 * FIXME: little-endian only for now
 */

#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <asm/dmabrg.h>


/* registers and bits */
#define BRGATXSAR	0x00
#define BRGARXDAR	0x04
#define BRGATXTCR	0x08
#define BRGARXTCR	0x0C
#define BRGACR		0x10
#define BRGATXTCNT	0x14
#define BRGARXTCNT	0x18

#define ACR_RAR		(1 << 18)
#define ACR_RDS		(1 << 17)
#define ACR_RDE		(1 << 16)
#define ACR_TAR		(1 << 2)
#define ACR_TDS		(1 << 1)
#define ACR_TDE		(1 << 0)

/* receiver/transmitter data alignment */
#define ACR_RAM_NONE	(0 << 24)
#define ACR_RAM_4BYTE	(1 << 24)
#define ACR_RAM_2WORD	(2 << 24)
#define ACR_TAM_NONE	(0 << 8)
#define ACR_TAM_4BYTE	(1 << 8)
#define ACR_TAM_2WORD	(2 << 8)


struct camelot_pcm {
	unsigned long mmio;  /* DMABRG audio channel control reg MMIO */
	unsigned int txid;    /* ID of first DMABRG IRQ for this unit */

	struct snd_pcm_substream *tx_ss;
	unsigned long tx_period_size;
	unsigned int  tx_period;

	struct snd_pcm_substream *rx_ss;
	unsigned long rx_period_size;
	unsigned int  rx_period;

} cam_pcm_data[2] = {
	{
		.mmio	=	0xFE3C0040,
		.txid	=	DMABRGIRQ_A0TXF,
	},
	{
		.mmio	=	0xFE3C0060,
		.txid	=	DMABRGIRQ_A1TXF,
	},
};

#define BRGREG(x)	(*(unsigned long *)(cam->mmio + (x)))

/*
 * set a minimum of 16kb per period, to avoid interrupt-"storm" and
 * resulting skipping. In general, the bigger the minimum size, the
 * better for overall system performance. (The SH7760 is a puny CPU
 * with a slow SDRAM interface and poor internal bus bandwidth,
 * *especially* when the LCDC is active).  The minimum for the DMAC
 * is 8 bytes; 16kbytes are enough to get skip-free playback of a
 * 44kHz/16bit/stereo MP3 on a lightly loaded system, and maintain
 * reasonable responsiveness in MPlayer.
 */
#define DMABRG_PERIOD_MIN		16 * 1024
#define DMABRG_PERIOD_MAX		0x03fffffc
#define DMABRG_PREALLOC_BUFFER		32 * 1024
#define DMABRG_PREALLOC_BUFFER_MAX	32 * 1024

static struct snd_pcm_hardware camelot_pcm_hardware = {
	.info = (SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_BATCH),
	.buffer_bytes_max =	DMABRG_PERIOD_MAX,
	.period_bytes_min =	DMABRG_PERIOD_MIN,
	.period_bytes_max =	DMABRG_PERIOD_MAX / 2,
	.periods_min =		2,
	.periods_max =		2,
	.fifo_size =		128,
};

static void camelot_txdma(void *data)
{
	struct camelot_pcm *cam = data;
	cam->tx_period ^= 1;
	snd_pcm_period_elapsed(cam->tx_ss);
}

static void camelot_rxdma(void *data)
{
	struct camelot_pcm *cam = data;
	cam->rx_period ^= 1;
	snd_pcm_period_elapsed(cam->rx_ss);
}

static int camelot_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct camelot_pcm *cam = &cam_pcm_data[rtd->cpu_dai->id];
	int recv = substream->stream == SNDRV_PCM_STREAM_PLAYBACK ? 0:1;
	int ret, dmairq;

	snd_soc_set_runtime_hwparams(substream, &camelot_pcm_hardware);

	/* DMABRG buffer half/full events */
	dmairq = (recv) ? cam->txid + 2 : cam->txid;
	if (recv) {
		cam->rx_ss = substream;
		ret = dmabrg_request_irq(dmairq, camelot_rxdma, cam);
		if (unlikely(ret)) {
			pr_debug("audio unit %d irqs already taken!\n",
			     rtd->cpu_dai->id);
			return -EBUSY;
		}
		(void)dmabrg_request_irq(dmairq + 1,camelot_rxdma, cam);
	} else {
		cam->tx_ss = substream;
		ret = dmabrg_request_irq(dmairq, camelot_txdma, cam);
		if (unlikely(ret)) {
			pr_debug("audio unit %d irqs already taken!\n",
			     rtd->cpu_dai->id);
			return -EBUSY;
		}
		(void)dmabrg_request_irq(dmairq + 1, camelot_txdma, cam);
	}
	return 0;
}

static int camelot_pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct camelot_pcm *cam = &cam_pcm_data[rtd->cpu_dai->id];
	int recv = substream->stream == SNDRV_PCM_STREAM_PLAYBACK ? 0:1;
	int dmairq;

	dmairq = (recv) ? cam->txid + 2 : cam->txid;

	if (recv)
		cam->rx_ss = NULL;
	else
		cam->tx_ss = NULL;

	dmabrg_free_irq(dmairq + 1);
	dmabrg_free_irq(dmairq);

	return 0;
}

static int camelot_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *hw_params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct camelot_pcm *cam = &cam_pcm_data[rtd->cpu_dai->id];
	int recv = substream->stream == SNDRV_PCM_STREAM_PLAYBACK ? 0:1;
	int ret;

	ret = snd_pcm_lib_malloc_pages(substream,
				       params_buffer_bytes(hw_params));
	if (ret < 0)
		return ret;

	if (recv) {
		cam->rx_period_size = params_period_bytes(hw_params);
		cam->rx_period = 0;
	} else {
		cam->tx_period_size = params_period_bytes(hw_params);
		cam->tx_period = 0;
	}
	return 0;
}

static int camelot_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
}

static int camelot_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct camelot_pcm *cam = &cam_pcm_data[rtd->cpu_dai->id];

	pr_debug("PCM data: addr 0x%08ulx len %d\n",
		 (u32)runtime->dma_addr, runtime->dma_bytes);
 
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		BRGREG(BRGATXSAR) = (unsigned long)runtime->dma_area;
		BRGREG(BRGATXTCR) = runtime->dma_bytes;
	} else {
		BRGREG(BRGARXDAR) = (unsigned long)runtime->dma_area;
		BRGREG(BRGARXTCR) = runtime->dma_bytes;
	}

	return 0;
}

static inline void dmabrg_play_dma_start(struct camelot_pcm *cam)
{
	unsigned long acr = BRGREG(BRGACR) & ~(ACR_TDS | ACR_RDS);
	/* start DMABRG engine: XFER start, auto-addr-reload */
	BRGREG(BRGACR) = acr | ACR_TDE | ACR_TAR | ACR_TAM_2WORD;
}

static inline void dmabrg_play_dma_stop(struct camelot_pcm *cam)
{
	unsigned long acr = BRGREG(BRGACR) & ~(ACR_TDS | ACR_RDS);
	/* forcibly terminate data transmission */
	BRGREG(BRGACR) = acr | ACR_TDS;
}

static inline void dmabrg_rec_dma_start(struct camelot_pcm *cam)
{
	unsigned long acr = BRGREG(BRGACR) & ~(ACR_TDS | ACR_RDS);
	/* start DMABRG engine: recv start, auto-reload */
	BRGREG(BRGACR) = acr | ACR_RDE | ACR_RAR | ACR_RAM_2WORD;
}

static inline void dmabrg_rec_dma_stop(struct camelot_pcm *cam)
{
	unsigned long acr = BRGREG(BRGACR) & ~(ACR_TDS | ACR_RDS);
	/* forcibly terminate data receiver */
	BRGREG(BRGACR) = acr | ACR_RDS;
}

static int camelot_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct camelot_pcm *cam = &cam_pcm_data[rtd->cpu_dai->id];
	int recv = substream->stream == SNDRV_PCM_STREAM_PLAYBACK ? 0:1;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		if (recv)
			dmabrg_rec_dma_start(cam);
		else
			dmabrg_play_dma_start(cam);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		if (recv)
			dmabrg_rec_dma_stop(cam);
		else
			dmabrg_play_dma_stop(cam);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static snd_pcm_uframes_t camelot_pos(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct camelot_pcm *cam = &cam_pcm_data[rtd->cpu_dai->id];
	int recv = substream->stream == SNDRV_PCM_STREAM_PLAYBACK ? 0:1;
	unsigned long pos;

	/* cannot use the DMABRG pointer register: under load, by the
	 * time ALSA comes around to read the register, it is already
	 * far ahead (or worse, already done with the fragment) of the
	 * position at the time the IRQ was triggered, which results in
	 * fast-playback sound in my test application (ScummVM)
	 */
	if (recv)
		pos = cam->rx_period ? cam->rx_period_size : 0;
	else
		pos = cam->tx_period ? cam->tx_period_size : 0;

	return bytes_to_frames(runtime, pos);
}

static struct snd_pcm_ops camelot_pcm_ops = {
	.open		= camelot_pcm_open,
	.close		= camelot_pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= camelot_hw_params,
	.hw_free	= camelot_hw_free,
	.prepare	= camelot_prepare,
	.trigger	= camelot_trigger,
	.pointer	= camelot_pos,
};

static int camelot_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_pcm *pcm = rtd->pcm;

	/* dont use SNDRV_DMA_TYPE_DEV, since it will oops the SH kernel
	 * in MMAP mode (i.e. aplay -M)
	 */
	snd_pcm_lib_preallocate_pages_for_all(pcm,
		SNDRV_DMA_TYPE_CONTINUOUS,
		snd_dma_continuous_data(GFP_KERNEL),
		DMABRG_PREALLOC_BUFFER,	DMABRG_PREALLOC_BUFFER_MAX);

	return 0;
}

static struct snd_soc_platform_driver sh7760_soc_platform = {
	.ops		= &camelot_pcm_ops,
	.pcm_new	= camelot_pcm_new,
};

static int sh7760_soc_platform_probe(struct platform_device *pdev)
{
	return devm_snd_soc_register_platform(&pdev->dev, &sh7760_soc_platform);
}

static struct platform_driver sh7760_pcm_driver = {
	.driver = {
			.name = "sh7760-pcm-audio",
	},

	.probe = sh7760_soc_platform_probe,
};

module_platform_driver(sh7760_pcm_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SH7760 Audio DMA (DMABRG) driver");
MODULE_AUTHOR("Manuel Lauss <mano@roarinelk.homelinux.net>");
