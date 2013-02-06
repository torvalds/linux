/*
 * idma.c  --  I2S0's Internal Dma driver
 *
 * Copyright (c) 2010 Samsung Electronics Co. Ltd
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include <mach/map.h>
#include <plat/cpu.h>

#include "i2s.h"
#include "idma.h"
#include "dma.h"

/*#define ENABLE_REG_LOG*/
#define ENABLE_TRNCNT_WA

#define ST_RUNNING		(1<<0)
#define ST_OPENED		(1<<1)

static const struct snd_pcm_hardware idma_hardware = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
		    SNDRV_PCM_INFO_BLOCK_TRANSFER |
		    SNDRV_PCM_INFO_MMAP |
		    SNDRV_PCM_INFO_MMAP_VALID |
		    SNDRV_PCM_INFO_PAUSE |
		    SNDRV_PCM_INFO_RESUME,
	.formats = SNDRV_PCM_FMTBIT_S16_LE |
		    SNDRV_PCM_FMTBIT_U16_LE |
		    SNDRV_PCM_FMTBIT_S24_LE |
		    SNDRV_PCM_FMTBIT_U24_LE |
		    SNDRV_PCM_FMTBIT_U8 |
		    SNDRV_PCM_FMTBIT_S8,
	.channels_min = 1,
	.channels_max = 2,
	.buffer_bytes_max = LP_TXBUFF_MAX,
	.period_bytes_min = 1024,
	.period_bytes_max = PAGE_SIZE * 2,
	.periods_min = 2,
	.periods_max = 128,
	.fifo_size = 32,
};

struct idma_ctrl {
	spinlock_t	lock;
	int		state;
	dma_addr_t	start;
	dma_addr_t	pos;
	dma_addr_t	end;
	dma_addr_t	period;
	dma_addr_t	periodsz;
	void		*token;
	void		(*cb)(void *dt, int bytes_xfer);
};

static struct idma_info {
	spinlock_t	lock;
	void		__iomem	*regs;
	int		trigger_stat;
#ifdef ENABLE_TRNCNT_WA
	bool		trncnt_wa_enabled;
#endif
} idma;

static void idma_getpos(dma_addr_t *src, struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct idma_ctrl *prtd = runtime->private_data;

	*src = prtd->start + (readl(idma.regs + I2STRNCNT) & 0xffffff) * 4;

#ifdef ENABLE_TRNCNT_WA
	if (idma.trncnt_wa_enabled && (idma.trigger_stat == LPAM_DMA_START)) {
		*src -= 4;
		if (*src < prtd->start)
			*src = prtd->end - 4;
	}
#endif
}

static int idma_enqueue(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct idma_ctrl *prtd = substream->runtime->private_data;
	u32 val = prtd->start;
	unsigned long flags;

	spin_lock_irqsave(&prtd->lock, flags);
	prtd->token = (void *) substream;
	spin_unlock_irqrestore(&prtd->lock, flags);

	/* Start address0 of I2S internal DMA operation. */
	writel(val, idma.regs + I2SSTR0);

	/* Internal DMA Level0 Interrupt Address */
	val = prtd->start + prtd->periodsz;
	writel(val, idma.regs + I2SLVL0ADDR);

	/*
	 * Transfer block size for I2S internal DMA.
	 * Should decide transfer size before start dma operation
	 */
	val = readl(idma.regs + I2SSIZE);
	val &= ~(I2SSIZE_TRNMSK << I2SSIZE_SHIFT);

	val |= (((runtime->dma_bytes >> 2) &
			I2SSIZE_TRNMSK) << I2SSIZE_SHIFT);
	writel(val, idma.regs + I2SSIZE);

	return 0;
}

static void idma_setcallbk(struct snd_pcm_substream *substream,
				void (*cb)(void *, int))
{
	struct idma_ctrl *prtd = substream->runtime->private_data;
	unsigned long flags;

	spin_lock_irqsave(&prtd->lock, flags);
	prtd->cb = cb;
	spin_unlock_irqrestore(&prtd->lock, flags);

	pr_debug("%s:%d dma_period=%x\n", __func__, __LINE__, prtd->periodsz);
}

static void idma_ctrl(int op)
{
	u32 val = readl(idma.regs + I2SAHB);
	unsigned long flags;

	spin_lock_irqsave(&idma.lock, flags);

	switch (op) {
	case LPAM_DMA_START:
		val |= (AHB_INTENLVL0 | AHB_DMAEN);
		break;
	case LPAM_DMA_STOP:
		val &= ~(AHB_INTENLVL0 | AHB_DMAEN);
		break;
	default:
		spin_unlock(&idma.lock);
		return;
	}

	writel(val, idma.regs + I2SAHB);
	spin_unlock_irqrestore(&idma.lock, flags);
}

static void idma_done(void *id, int bytes_xfer)
{
	struct snd_pcm_substream *substream = id;
	struct idma_ctrl *prtd = substream->runtime->private_data;

	if (prtd && (prtd->state & ST_RUNNING))
		snd_pcm_period_elapsed(substream);
}

static int idma_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct idma_ctrl *prtd = substream->runtime->private_data;
	u32 ahb = readl(idma.regs + I2SAHB);

	pr_debug("Entered %s\n", __func__);

	ahb |= (AHB_DMARLD | AHB_INTMASK);
	writel(ahb, idma.regs + I2SAHB);

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);
	runtime->dma_bytes = params_buffer_bytes(params);
	memset(runtime->dma_area, 0, runtime->dma_bytes);

	prtd->start = prtd->pos = runtime->dma_addr;
	prtd->period = params_periods(params);
	prtd->periodsz = params_period_bytes(params);
	prtd->end = prtd->start + runtime->dma_bytes;

	idma_setcallbk(substream, idma_done);

	pr_info("I:%s:DmaAddr=@%x Total=%d PrdSz=%d #Prds=%d dma_area=0x%x\n",
		(substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ? "P" : "C",
		prtd->start, runtime->dma_bytes, prtd->periodsz,
		prtd->period, (unsigned int)runtime->dma_area);

	return 0;
}

static int idma_hw_free(struct snd_pcm_substream *substream)
{
	pr_debug("Entered %s\n", __func__);

	snd_pcm_set_runtime_buffer(substream, NULL);

	return 0;
}

static int idma_prepare(struct snd_pcm_substream *substream)
{
	struct idma_ctrl *prtd = substream->runtime->private_data;

	pr_debug("Entered %s\n", __func__);

	prtd->pos = prtd->start;

	/* flush the DMA channel */
	idma_ctrl(LPAM_DMA_STOP);
	idma_enqueue(substream);

	return 0;
}

static int idma_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct idma_ctrl *prtd = substream->runtime->private_data;
	int ret = 0;
	unsigned long flags;

	pr_debug("Entered %s\n", __func__);

	spin_lock_irqsave(&prtd->lock, flags);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		prtd->state |= ST_RUNNING;
		idma.trigger_stat = LPAM_DMA_START;
		idma_ctrl(LPAM_DMA_START);
		break;

	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		prtd->state &= ~ST_RUNNING;
		idma.trigger_stat = LPAM_DMA_STOP;
		idma_ctrl(LPAM_DMA_STOP);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	spin_unlock_irqrestore(&prtd->lock, flags);

	return ret;
}

static snd_pcm_uframes_t
	idma_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct idma_ctrl *prtd = runtime->private_data;
	dma_addr_t src;
	unsigned long res, flags;

	spin_lock_irqsave(&prtd->lock, flags);

	idma_getpos(&src, substream);
	res = src - prtd->start;

	spin_unlock_irqrestore(&prtd->lock, flags);

	if (res >= snd_pcm_lib_buffer_bytes(substream)) {
		if (res == snd_pcm_lib_buffer_bytes(substream))
			res = 0;
	}

	return bytes_to_frames(substream->runtime, res);
}

static int idma_mmap(struct snd_pcm_substream *substream,
	struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned long size, offset;
	int ret;

	pr_debug("Entered %s\n", __func__);

	/* From snd_pcm_lib_mmap_iomem */
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_flags |= VM_IO;
	size = vma->vm_end - vma->vm_start;
	offset = vma->vm_pgoff << PAGE_SHIFT;
	ret = io_remap_pfn_range(vma, vma->vm_start,
			(runtime->dma_addr + offset) >> PAGE_SHIFT,
			size, vma->vm_page_prot);

	return ret;
}

#ifdef ENABLE_REG_LOG
u32 iis_reg_addr[8] = {
	I2SCON, I2SMOD, I2SFIC, I2SFICS, I2SAHB, I2SSTR0, I2SSIZE, I2SLVL0ADDR
};
struct {
	u32 before;
	u32 after;
} iis_reg_log[16][8];
int iis_reg_idx = 0;
#endif
static irqreturn_t iis_irq(int irqno, void *dev_id)
{
	struct idma_ctrl *prtd = (struct idma_ctrl *)dev_id;
	u32 iiscon = readl(idma.regs + I2SCON);
	u32 iisahb = readl(idma.regs + I2SAHB);
	u32 addr = 0;
	u32 val = 0;

#ifdef ENABLE_REG_LOG
	for (val = 0; val < 8; val++) {
		addr = iis_reg_addr[val];
		iis_reg_log[iis_reg_idx][val].before = readl(idma.regs + addr);
	}
#endif

	/* Check RX Overflow INT */
	if (iiscon & CON_FRXOFSTATUS) {
		pr_err("RX overflow occurs!! I2SCON[0x%08x])\n", iiscon);
		iiscon |= CON_FRXOFSTATUS;
		writel(iiscon, idma.regs + I2SCON);
	}

	/* Check TX_P Underrun INT */
	if (iiscon & CON_FTXURSTATUS) {
		pr_err("Tx_P underrun occurs!! I2SCON[0x%08x])\n", iiscon);
		iiscon |= CON_FTXURSTATUS;
		writel(iiscon, idma.regs + I2SCON);
	}

	/* Check TX_S Underrun INT */
	if (iiscon & CON_FTXSURSTAT) {
		pr_err("Tx_S underrun occurs!! I2SCON[0x%08x])\n", iiscon);
		iiscon |= CON_FTXSURSTAT;
		writel(iiscon, idma.regs + I2SCON);
	}

	/* Check I2SAHB Level[0~3] INT */
	if (iisahb & AHB_LVL0INT)
		val = AHB_CLRLVL0INT;
	else if (iisahb & AHB_LVL1INT)
		val = AHB_CLRLVL1INT;
	else if (iisahb & AHB_LVL2INT)
		val = AHB_CLRLVL2INT;
	else if (iisahb & AHB_LVL3INT)
		val = AHB_CLRLVL3INT;

	if (val) {
		iisahb |= val;
		writel(iisahb, idma.regs + I2SAHB);

		addr = readl(idma.regs + I2SLVL0ADDR);
		addr += prtd->periodsz;

		if (addr >= prtd->end)
			addr = prtd->start;

		writel(addr, idma.regs + I2SLVL0ADDR);

		/* Finished dma transfer ? */
		if (iisahb & AHB_LVLINTMASK) {
			if (prtd->cb)
				prtd->cb(prtd->token, prtd->periodsz);
		}
	}

#ifdef ENABLE_REG_LOG
	for (val = 0; val < 8; val++) {
		addr = iis_reg_addr[val];
		iis_reg_log[iis_reg_idx][val].after = readl(idma.regs + addr);
	}
	iis_reg_idx++;
	iis_reg_idx &= 0x0F;
#endif

	return IRQ_HANDLED;
}

static int idma_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct idma_ctrl *prtd;
	int ret;

	pr_debug("Entered %s\n", __func__);

	snd_soc_set_runtime_hwparams(substream, &idma_hardware);

	/* Clear AHB register */
	writel(0, idma.regs + I2SAHB);

	prtd = kzalloc(sizeof(struct idma_ctrl), GFP_KERNEL);
	if (prtd == NULL)
		return -ENOMEM;

	ret = request_irq(IRQ_I2S0, iis_irq, 0, "i2s", prtd);
	if (ret < 0) {
		pr_err("fail to claim i2s irq , ret = %d\n", ret);
		kfree(prtd);
		return ret;
	}

	spin_lock_init(&prtd->lock);

	runtime->private_data = prtd;

	return 0;
}

static int idma_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct idma_ctrl *prtd = runtime->private_data;

	pr_debug("Entered %s, prtd = %p\n", __func__, prtd);

	free_irq(IRQ_I2S0, prtd);

	if (!prtd)
		pr_err("idma_close called with prtd == NULL\n");

	kfree(prtd);

	return 0;
}

static struct snd_pcm_ops idma_ops = {
	.open		= idma_open,
	.close		= idma_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.trigger	= idma_trigger,
	.pointer	= idma_pointer,
	.mmap		= idma_mmap,
	.hw_params	= idma_hw_params,
	.hw_free	= idma_hw_free,
	.prepare	= idma_prepare,
};

static void idma_free(struct snd_pcm *pcm)
{
	struct snd_pcm_substream *substream;
	struct snd_dma_buffer *buf;

	pr_debug("Entered %s\n", __func__);

	substream = pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream;
	if (!substream)
		return;

	buf = &substream->dma_buffer;
	if (!buf->area)
		return;

	iounmap(buf->area);

	buf->area = NULL;
	buf->addr = 0;
}

static int preallocate_idma_buffer(struct snd_pcm *pcm, int stream)
{
	struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	struct snd_dma_buffer *buf = &substream->dma_buffer;

	pr_debug("Entered %s\n", __func__);
	buf->dev.dev = pcm->card->dev;
	buf->private_data = NULL;

	/* Assign PCM buffer pointers */
	buf->dev.type = SNDRV_DMA_TYPE_CONTINUOUS;
	buf->addr = LP_TXBUFF_ADDR;
	buf->bytes = idma_hardware.buffer_bytes_max;
	buf->area = (unsigned char *)ioremap(buf->addr, buf->bytes);
	pr_debug("%s:  VA-%p  PA-%X  %ubytes\n",
			__func__, buf->area, buf->addr, buf->bytes);

	return 0;
}

static u64 idma_mask = DMA_BIT_MASK(32);

static int idma_new(struct snd_card *card,
	struct snd_soc_dai *dai, struct snd_pcm *pcm)
{
	int ret = 0;

	pr_debug("Entered %s\n", __func__);
	if (!card->dev->dma_mask)
		card->dev->dma_mask = &idma_mask;
	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = DMA_BIT_MASK(32);

	if (dai->driver->playback.channels_min)
		ret = preallocate_idma_buffer(pcm,
				SNDRV_PCM_STREAM_PLAYBACK);

	return ret;
}

void idma_init(void *regs)
{
	spin_lock_init(&idma.lock);
	idma.regs = regs;
#if defined(ENABLE_TRNCNT_WA) && defined(CONFIG_ARCH_EXYNOS4)
	idma.trncnt_wa_enabled = (soc_is_exynos4412() || soc_is_exynos4212()) ?
				true : false;
#else
	idma.trncnt_wa_enabled = false;
#endif
}

#ifdef CONFIG_SND_SAMSUNG_RP
int idma_irq_callback(void)
{
	u32 iisahb;
	int ret = 0;

	iisahb = readl(idma.regs + I2SAHB);

	if (iisahb & AHB_LVL0INT) {
		iisahb |= AHB_CLRLVL0INT;
		ret = 1;
	}

	if (iisahb & AHB_LVL1INT) {
		iisahb |= AHB_CLRLVL1INT;
		ret = 1;
	}

	if (ret)
		writel(iisahb, idma.regs + I2SAHB);

	return ret;
}
EXPORT_SYMBOL(idma_irq_callback);

void idma_stop(void)
{
	u32 val;

	val  = readl(idma.regs + I2SAHB);
	val &= ~AHB_DMARLD;
	val |= AHB_DMA_STRADDRRST;
	val &= ~AHB_DMAEN;
	val &= ~(AHB_LVL0INT | AHB_LVL1INT);
	val |= AHB_CLRLVL0INT | AHB_CLRLVL1INT;
	writel(val, idma.regs + I2SAHB);

	writel(0, idma.regs + I2SAHB);
	writel(0x00000000, idma.regs + I2SLVL0ADDR);
	writel(0x00000000, idma.regs + I2SLVL1ADDR);
}
EXPORT_SYMBOL(idma_stop);
#endif

struct snd_soc_platform_driver samsung_asoc_idma_platform = {
	.ops		= &idma_ops,
	.pcm_new	= idma_new,
	.pcm_free	= idma_free,
};

#ifndef CONFIG_SND_SOC_SAMSUNG_USE_DMA_WRAPPER
static int __devinit
samsung_asoc_idma_platform_probe(struct platform_device *pdev)
{
	return snd_soc_register_platform(&pdev->dev, &samsung_asoc_idma_platform);
}

static int __devexit
samsung_asoc_idma_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_platform(&pdev->dev);
	return 0;
}

static struct platform_driver asoc_idma_driver = {
	.driver = {
		.name = "samsung-audio-idma",
		.owner = THIS_MODULE,
	},

	.probe = samsung_asoc_idma_platform_probe,
	.remove = __devexit_p(samsung_asoc_idma_platform_remove),
};

static int __init samsung_idma_init(void)
{
	return platform_driver_register(&asoc_idma_driver);
}
module_init(samsung_idma_init);

static void __exit samsung_idma_exit(void)
{
	platform_driver_unregister(&asoc_idma_driver);
}
module_exit(samsung_idma_exit);
#endif

MODULE_DESCRIPTION("Samsung ASoC IDMA Driver");
MODULE_LICENSE("GPL");
