/*$_FOR_ROCKCHIP_RBOX_$*/
/*$_rbox_$_modify_$_huangzhibao for spdif output*/

/* sound/soc/rockchip/spdif.c
 *
 * ALSA SoC Audio Layer - rockchip S/PDIF Controller driver
 *
 * Copyright (c) 2010 rockchip Electronics Co. Ltd
 *		http://www.rockchip.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/version.h>

#include <asm/dma.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <asm/io.h>

#include <mach/board.h>
#include <mach/hardware.h>
#include <mach/io.h>
#include <mach/gpio.h>
#include <mach/iomux.h>

#if defined (CONFIG_ARCH_RK29)
#include <mach/rk29-dma-pl330.h>
#endif

#if defined (CONFIG_ARCH_RK30)
#include <mach/dma-pl330.h>
#endif

#if defined (CONFIG_ARCH_RK3188)
#include <mach/dma-pl330.h>
#endif

#include "rk29_pcm.h"

#if 0
#define RK_SPDIF_DBG(x...) printk(KERN_INFO "rk_spdif:"x)
#else
#define RK_SPDIF_DBG(x...) do { } while (0)
#endif


/* Registers */
#define CFGR				0x00
#define SDBLR				0x04
#define DMACR				0x08
#define INTCR				0x0C
#define INTSR			  0x10
#define XFER				0x18
#define SMPDR				0x20

#define DATA_OUTBUF			0x20   
 
#define CFGR_MASK			0x0ffffff
#define CFGR_VALID_DATA_16bit       (00)
#define CFGR_VALID_DATA_20bit       (01)
#define CFGR_VALID_DATA_24bit       (10)
#define CFGR_VALID_DATA_MASK        (11)

#define CFGR_HALFWORD_TX_ENABLE     (0x1 << 2)
#define CFGR_HALFWORD_TX_DISABLE    (0x0 << 2)
#define CFGR_HALFWORD_TX_MASK       (0x1 << 2)  

#define CFGR_CLK_RATE_MASK          (0xFF<<16)                 

#define CFGR_JUSTIFIED_RIGHT        (0<<3)
#define CFGR_JUSTIFIED_LEFT         (1<<3)
#define CFGR_JUSTIFIED_MASK         (1<<3)

#define XFER_TRAN_STOP        (0)
#define XFER_TRAN_START       (1)
#define XFER_MASK             (1)

#define DMACR_TRAN_DMA_DISABLE   (0<<5)
#define DMACR_TRAN_DMA_ENABLE   (1<<5)
#define DMACR_TRAN_DMA_CTL_MASK   (1<<5)

#define DMACR_TRAN_DATA_LEVEL       0x10
#define DMACR_TRAN_DATA_LEVEL_MASK  0x1F

#define DMACR_TRAN_DMA_MASK   (0x3F)


 
struct rockchip_spdif_info {
	spinlock_t	lock;
	struct device	*dev;
	void __iomem	*regs;
	unsigned long	clk_rate;
	struct clk	*hclk;
	struct clk	*clk;
	u32		saved_clkcon;
	u32		saved_con;
	u32		saved_cstas;
	struct rockchip_pcm_dma_params	*dma_playback;
};

static struct rk29_dma_client spdif_dma_client_out = {
	.name		= "SPDIF Stereo out"
};

static struct rockchip_pcm_dma_params spdif_stereo_out;

static struct rockchip_spdif_info spdif_info;

static inline struct rockchip_spdif_info *to_info(struct snd_soc_dai *cpu_dai)
{
	return snd_soc_dai_get_drvdata(cpu_dai);
}

static void spdif_snd_txctrl(struct rockchip_spdif_info *spdif, int on)
{
	void __iomem *regs = spdif->regs;
	u32 opr,xfer;

	RK_SPDIF_DBG( "Entered %s\n", __func__);

	xfer = readl(regs + XFER) & XFER_MASK;
	opr  = readl(regs + DMACR) & DMACR_TRAN_DMA_MASK & (~DMACR_TRAN_DMA_CTL_MASK);
	
	if (on){
		xfer |= XFER_TRAN_START;
		opr |= DMACR_TRAN_DMA_ENABLE;
		writel(xfer, regs + XFER);
		writel(opr|0x10, regs + DMACR);
		RK_SPDIF_DBG("on xfer=0x%x,opr=0x%x\n",readl(regs + XFER),readl(regs + DMACR));
  }	else{
  	xfer &= ~XFER_TRAN_START;
  	opr  &= ~DMACR_TRAN_DMA_ENABLE; 
		writel(xfer, regs + XFER);
		writel(opr|0x10, regs + DMACR);
  }
}

static int spdif_set_syclk(struct snd_soc_dai *cpu_dai,
				int clk_id, unsigned int freq, int dir)
{
	struct rockchip_spdif_info *spdif = to_info(cpu_dai);
	u32 clkcon;

	RK_SPDIF_DBG("Entered %s\n", __func__);

	spdif->clk_rate = freq;

	return 0;
}

static int spdif_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct rockchip_spdif_info *spdif = to_info(rtd->cpu_dai);
	unsigned long flags;

	RK_SPDIF_DBG( "Entered %s\n", __func__);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		spin_lock_irqsave(&spdif->lock, flags);
		spdif_snd_txctrl(spdif, 1);
		spin_unlock_irqrestore(&spdif->lock, flags);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		spin_lock_irqsave(&spdif->lock, flags);
		spdif_snd_txctrl(spdif, 0);
		spin_unlock_irqrestore(&spdif->lock, flags);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}


static int spdif_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *socdai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct rockchip_spdif_info *spdif = to_info(rtd->cpu_dai);
	void __iomem *regs = spdif->regs;
	struct rockchip_pcm_dma_params *dma_data;
	unsigned long flags;
	int i, cfgr, dmac;

	RK_SPDIF_DBG("Entered %s\n", __func__);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dma_data = spdif->dma_playback;
	else {
		printk("spdif:Capture is not supported\n");
		return -EINVAL;
	}

	snd_soc_dai_set_dma_data(rtd->cpu_dai, substream, dma_data);
	spin_lock_irqsave(&spdif->lock, flags);
	
	cfgr = readl(regs + CFGR) & CFGR_VALID_DATA_MASK;
	
  cfgr &= ~CFGR_VALID_DATA_MASK;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		cfgr |= CFGR_VALID_DATA_16bit;
		break;
	case SNDRV_PCM_FMTBIT_S20_3LE :
		cfgr |= CFGR_VALID_DATA_20bit;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		cfgr |= CFGR_VALID_DATA_24bit;
		break;			
	default:
		goto err;
	}
	
	cfgr &= ~CFGR_HALFWORD_TX_MASK;
	cfgr |= CFGR_HALFWORD_TX_ENABLE;
	
	cfgr &= ~CFGR_CLK_RATE_MASK;
	cfgr |= (1<<16);
	
	cfgr &= ~CFGR_JUSTIFIED_MASK;
	cfgr |= CFGR_JUSTIFIED_RIGHT;
	
	writel(cfgr, regs + CFGR);
  
  dmac = readl(regs + DMACR) & DMACR_TRAN_DMA_MASK & (~DMACR_TRAN_DATA_LEVEL_MASK);
  dmac |= 0x10;
  writel(dmac, regs + DMACR);
  
	spin_unlock_irqrestore(&spdif->lock, flags);

	return 0;
err:
	spin_unlock_irqrestore(&spdif->lock, flags);
	return -EINVAL;
}

static void spdif_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct rockchip_spdif_info *spdif = to_info(rtd->cpu_dai);
	void __iomem *regs = spdif->regs;
	u32 con, clkcon;

	RK_SPDIF_DBG( "spdif:Entered %s\n", __func__);

}

#ifdef CONFIG_PM
static int spdif_suspend(struct snd_soc_dai *cpu_dai)
{
	struct rockchip_spdif_info *spdif = to_info(cpu_dai);
	u32 con = spdif->saved_con;

	RK_SPDIF_DBG( "spdif:Entered %s\n", __func__);

	return 0;
}

static int spdif_resume(struct snd_soc_dai *cpu_dai)
{
	struct rockchip_spdif_info *spdif = to_info(cpu_dai);

	RK_SPDIF_DBG( "spdif:Entered %s\n", __func__);

	return 0;
}
#else
#define spdif_suspend NULL
#define spdif_resume NULL
#endif

static struct snd_soc_dai_ops spdif_dai_ops = {
	.set_sysclk	= spdif_set_syclk,
	.trigger	= spdif_trigger,
	.hw_params	= spdif_hw_params,
	.shutdown	= spdif_shutdown,
};

struct snd_soc_dai_driver rockchip_spdif_dai = {
	.name = "rk-spdif",
	.playback = {
		.stream_name = "SPDIF Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = (SNDRV_PCM_RATE_32000 |
				SNDRV_PCM_RATE_44100 |
				SNDRV_PCM_RATE_48000 |
				SNDRV_PCM_RATE_96000),
		.formats = SNDRV_PCM_FMTBIT_S16_LE|
		SNDRV_PCM_FMTBIT_S20_3LE|
		SNDRV_PCM_FMTBIT_S24_LE, },
	.ops = &spdif_dai_ops,
	.suspend = spdif_suspend,
	.resume = spdif_resume,
};


static __devinit int spdif_probe(struct platform_device *pdev)
{
	struct s3c_audio_pdata *spdif_pdata;
	struct resource *mem_res, *dma_res;
	struct rockchip_spdif_info *spdif;
	int ret;
  
	spdif_pdata = pdev->dev.platform_data;

	RK_SPDIF_DBG("Entered %s\n", __func__);
	
#if defined  (CONFIG_ARCH_RK29)
    rk29_mux_api_set(GPIO4A7_SPDIFTX_NAME, GPIO4L_SPDIF_TX);
#endif

#if defined (CONFIG_ARCH_RK30)    
    #if defined (CONFIG_ARCH_RK3066B)
    iomux_set(SPDIF_TX);
    #else
    rk30_mux_api_set(GPIO1B2_SPDIFTX_NAME, GPIO1B_SPDIF_TX);
    #endif
#elif defined (CONFIG_ARCH_RK3188)
    iomux_set(SPDIF_TX);
#endif


	dma_res = platform_get_resource_byname(pdev, IORESOURCE_DMA, "spdif_dma");
	if (!dma_res) {
		printk("spdif:Unable to get dma resource.\n");
		return -ENXIO;
	}

	mem_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "spdif_base");
	if (!mem_res) {
		printk("spdif:Unable to get register resource.\n");
		return -ENXIO;
	}

	spdif = &spdif_info;
	spdif->dev = &pdev->dev;

	spin_lock_init(&spdif->lock);
	
	spdif->clk = clk_get(&pdev->dev, "spdif");
	if (IS_ERR(spdif->clk)) {
		printk("spdif:failed to get internal source clock\n");
		ret = -ENOENT;
		goto err1;
	}
	clk_enable(spdif->clk);
	clk_set_rate(spdif->clk, 11289600);
	
#if 1// defined (CONFIG_ARCH_RK30)	
	spdif->hclk = clk_get(&pdev->dev, "hclk_spdif");
	if (IS_ERR(spdif->hclk)) {
		printk("spdif:failed to get spdif hclk\n");
		ret = -ENOENT;
		goto err0;
	}
	clk_enable(spdif->hclk);
	clk_set_rate(spdif->hclk, 11289600);
#endif

	/* Request S/PDIF Register's memory region */
	if (!request_mem_region(mem_res->start,
				resource_size(mem_res), "rockchip-spdif")) {
		printk("spdif:Unable to request register region\n");
		ret = -EBUSY;
		goto err2;
	}

	spdif->regs = ioremap(mem_res->start, mem_res->end - mem_res->start + 1);
	if (spdif->regs == NULL) {
		printk("spdif:Cannot ioremap registers\n");
		ret = -ENXIO;
		goto err3;
	}

	dev_set_drvdata(&pdev->dev, spdif);
	
	ret = snd_soc_register_dai(&pdev->dev, &rockchip_spdif_dai);
	if (ret != 0) {
		printk("spdif:fail to register dai\n");
		goto err4;
	}

	spdif_stereo_out.dma_size = 4;
	spdif_stereo_out.client = &spdif_dma_client_out;
	spdif_stereo_out.dma_addr = mem_res->start + DATA_OUTBUF;
	spdif_stereo_out.channel = dma_res->start;

	spdif->dma_playback = &spdif_stereo_out;
#ifdef CONFIG_SND_DMA_EVENT_STATIC
	WARN_ON(rk29_dma_request(spdif_stereo_out.channel, spdif_stereo_out.client, NULL));
#endif

	RK_SPDIF_DBG("spdif:spdif probe ok!\n");
	
	return 0;

err4:
	iounmap(spdif->regs);
err3:
	release_mem_region(mem_res->start, resource_size(mem_res));
err2:
	clk_disable(spdif->clk);
	clk_put(spdif->clk);
err1:
	clk_disable(spdif->hclk);
	clk_put(spdif->hclk);
#if  1//defined (CONFIG_ARCH_RK30)	
err0:
#endif
	return ret;
}

static __devexit int spdif_remove(struct platform_device *pdev)
{
	struct rockchip_spdif_info *spdif = &spdif_info;
	struct resource *mem_res;
	
	RK_SPDIF_DBG("Entered %s\n", __func__);
	
	snd_soc_unregister_dai(&pdev->dev);

	iounmap(spdif->regs);

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (mem_res)
		release_mem_region(mem_res->start, resource_size(mem_res));

	clk_disable(spdif->clk);
	clk_put(spdif->clk);
	clk_disable(spdif->hclk);
	clk_put(spdif->hclk);

	return 0;
}


static struct platform_driver rockchip_spdif_driver = {
	.probe	= spdif_probe,
	.remove	= spdif_remove,
	.driver	= {
		.name	= "rk-spdif",
		.owner	= THIS_MODULE,
	},
};


static int __init spdif_init(void)
{
	RK_SPDIF_DBG("Entered %s\n", __func__);
	return platform_driver_register(&rockchip_spdif_driver);
}
module_init(spdif_init);

static void __exit spdif_exit(void)
{
	RK_SPDIF_DBG("Entered %s\n", __func__);
	platform_driver_unregister(&rockchip_spdif_driver);
}
module_exit(spdif_exit);

MODULE_AUTHOR("Seungwhan Youn, <sw.youn@rockchip.com>");
MODULE_DESCRIPTION("rockchip S/PDIF Controller Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rockchip-spdif");
