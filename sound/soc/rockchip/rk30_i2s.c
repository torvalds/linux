/*
 * rk30_i2s.c  --  ALSA SoC ROCKCHIP IIS Audio Layer Platform driver
 *
 * Driver for rockchip iis audio
 *
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/version.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <asm/dma.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>
#include <asm/io.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#define CLK_SET_lATER

#include "rk_pcm.h"
#include "rk_i2s.h"

#if 0
#define I2S_DBG(x...) printk(KERN_INFO x)
#else
#define I2S_DBG(x...) do { } while (0)
#endif

#define pheadi2s  ((pI2S_REG)(i2s->regs))

#define MAX_I2S 3

static DEFINE_SPINLOCK(lock);

struct rk30_i2s_info {
	void __iomem	*regs;

	struct clk *i2s_clk;// i2s clk ,is bclk lrck
	struct clk *i2s_mclk;//i2s mclk,rk32xx can different i2s clk.
	struct clk *i2s_hclk;
	struct snd_dmaengine_dai_dma_data capture_dma_data;
	struct snd_dmaengine_dai_dma_data playback_dma_data;

	bool i2s_tx_status;//active = true;
	bool i2s_rx_status;
#ifdef CLK_SET_lATER
	struct delayed_work clk_delayed_work;
#endif	
};

#define I2S_CLR_ERROR_COUNT 10// check I2S_CLR reg 
static struct rk30_i2s_info *rk30_i2s;

#if defined (CONFIG_RK_HDMI) && defined (CONFIG_SND_RK_SOC_HDMI_I2S)
extern int hdmi_get_hotplug(void);
#else
#define hdmi_get_hotplug() 0
#endif

static inline struct rk30_i2s_info *to_info(struct snd_soc_dai *dai)
{
	return snd_soc_dai_get_drvdata(dai);
}

/* 
 *Turn on or off the transmission path. 
 */
static void rockchip_snd_txctrl(struct rk30_i2s_info *i2s, int on)
{
	u32 opr, xfer, clr;
	unsigned long flags;
	bool is_need_delay = false;
	int clr_error_count = I2S_CLR_ERROR_COUNT;

	spin_lock_irqsave(&lock, flags);

	opr  = readl(&(pheadi2s->I2S_DMACR));
	xfer = readl(&(pheadi2s->I2S_XFER));
	clr  = readl(&(pheadi2s->I2S_CLR));

	I2S_DBG("rockchip_snd_txctrl: %s\n", on ? "on" : "off");

	if (on) {
		if ((opr & I2S_TRAN_DMA_ENABLE) == 0) {
			opr  |= I2S_TRAN_DMA_ENABLE;
			writel(opr, &(pheadi2s->I2S_DMACR));
		}

		if ((xfer & I2S_TX_TRAN_START) == 0 || (xfer & I2S_RX_TRAN_START) == 0) {
			xfer |= I2S_TX_TRAN_START;
			xfer |= I2S_RX_TRAN_START;
			writel(xfer, &(pheadi2s->I2S_XFER));
		}

		i2s->i2s_tx_status = 1;

	} else { //stop tx
		i2s->i2s_tx_status = 0;
		opr  &= ~I2S_TRAN_DMA_ENABLE;
		writel(opr, &(pheadi2s->I2S_DMACR));

		if (i2s->i2s_rx_status == 0 && hdmi_get_hotplug() == 0) {
			xfer &= ~I2S_TX_TRAN_START;
			xfer &= ~I2S_RX_TRAN_START;
			writel(xfer, &(pheadi2s->I2S_XFER));	

			clr |= I2S_TX_CLEAR;
			clr |= I2S_RX_CLEAR;
			writel(clr, &(pheadi2s->I2S_CLR));

			is_need_delay = true;

			I2S_DBG("rockchip_snd_txctrl: stop xfer\n");
		}
	}

	spin_unlock_irqrestore(&lock, flags);

	if (is_need_delay){
		while(readl(&(pheadi2s->I2S_CLR)) && clr_error_count){
			udelay(1);
			clr_error_count --;
			if(clr_error_count == 0)
				printk("%s: i2s clr reg warning =%d",__FUNCTION__,readl(&(pheadi2s->I2S_CLR)));
		}
	}	
}

static void rockchip_snd_rxctrl(struct rk30_i2s_info *i2s, int on)
{
	u32 opr, xfer, clr;
	unsigned long flags;
	bool is_need_delay = false;
	int clr_error_count = I2S_CLR_ERROR_COUNT;

	spin_lock_irqsave(&lock, flags);

	opr  = readl(&(pheadi2s->I2S_DMACR));
	xfer = readl(&(pheadi2s->I2S_XFER));
	clr  = readl(&(pheadi2s->I2S_CLR));

	I2S_DBG("rockchip_snd_rxctrl: %s\n", on ? "on" : "off");

	if (on) {
		if ((opr & I2S_RECE_DMA_ENABLE) == 0) {
			opr  |= I2S_RECE_DMA_ENABLE;
			writel(opr, &(pheadi2s->I2S_DMACR));
		}

		if ((xfer & I2S_TX_TRAN_START)==0 || (xfer & I2S_RX_TRAN_START) == 0) {
			xfer |= I2S_RX_TRAN_START;
			xfer |= I2S_TX_TRAN_START;
			writel(xfer, &(pheadi2s->I2S_XFER));
		}

		i2s->i2s_rx_status = 1;
	} else {
		i2s->i2s_rx_status = 0;

		opr  &= ~I2S_RECE_DMA_ENABLE;
		writel(opr, &(pheadi2s->I2S_DMACR));

		if (i2s->i2s_tx_status == 0 && hdmi_get_hotplug() == 0) {
			xfer &= ~I2S_RX_TRAN_START;
			xfer &= ~I2S_TX_TRAN_START;
			writel(xfer, &(pheadi2s->I2S_XFER));

			clr |= I2S_RX_CLEAR;
			clr |= I2S_TX_CLEAR;
			writel(clr, &(pheadi2s->I2S_CLR));

			is_need_delay = true;

			I2S_DBG("rockchip_snd_rxctrl: stop xfer\n");
		}
	}

	spin_unlock_irqrestore(&lock, flags);

	if (is_need_delay){
		while(readl(&(pheadi2s->I2S_CLR)) && clr_error_count){
			udelay(1);
			clr_error_count --;
			if(clr_error_count == 0)
				printk("%s: i2s clr reg warning =%d",__FUNCTION__,readl(&(pheadi2s->I2S_CLR)));
		}
	}
}

/*
 * Set Rockchip I2S DAI format
 */
static int rockchip_i2s_set_fmt(struct snd_soc_dai *cpu_dai,
						unsigned int fmt)
{
	struct rk30_i2s_info *i2s = to_info(cpu_dai);
	u32 tx_ctl,rx_ctl;
	u32 iis_ckr_value;//clock generation register
	unsigned long flags;
	int ret = 0;

	I2S_DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);

	spin_lock_irqsave(&lock, flags);

	tx_ctl = readl(&(pheadi2s->I2S_TXCR));
	iis_ckr_value = readl(&(pheadi2s->I2S_CKR));

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		//Codec is master, so set cpu slave.
		iis_ckr_value &= ~I2S_MODE_MASK;
		iis_ckr_value |= I2S_SLAVE_MODE;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		//Codec is slave, so set cpu master.
		iis_ckr_value &= ~I2S_MODE_MASK;
		iis_ckr_value |= I2S_MASTER_MODE;
		break;
	default:
		I2S_DBG("unknwon master/slave format\n");
		ret = -EINVAL;
		goto out_;
	}

	writel(iis_ckr_value, &(pheadi2s->I2S_CKR));

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_RIGHT_J:
		tx_ctl &= ~I2S_BUS_MODE_MASK;    //I2S Bus Mode
		tx_ctl |= I2S_BUS_MODE_RSJM;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		tx_ctl &= ~I2S_BUS_MODE_MASK;    //I2S Bus Mode
		tx_ctl |= I2S_BUS_MODE_LSJM;
		break;
	case SND_SOC_DAIFMT_I2S:
		tx_ctl &= ~I2S_BUS_MODE_MASK;    //I2S Bus Mode
		tx_ctl |= I2S_BUS_MODE_NOR;
		break;
	default:
		I2S_DBG("Unknown data format\n");
		ret = -EINVAL;
		goto out_;
	}

	I2S_DBG("Enter::%s----%d, I2S_TXCR=0x%X\n",__FUNCTION__,__LINE__,tx_ctl);

	writel(tx_ctl, &(pheadi2s->I2S_TXCR));

	rx_ctl = tx_ctl & 0x00007FFF;
	writel(rx_ctl, &(pheadi2s->I2S_RXCR));

out_:

	spin_unlock_irqrestore(&lock, flags);

	return ret;
}

static int rockchip_i2s_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct rk30_i2s_info *i2s = to_info(dai);
	u32 iismod;
	u32 dmarc;
	unsigned long flags;

	I2S_DBG("Enter %s, %d \n", __func__, __LINE__);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dai->playback_dma_data = &i2s->playback_dma_data;
	else
		dai->capture_dma_data = &i2s->capture_dma_data;

	spin_lock_irqsave(&lock, flags);

	/* Working copies of register */
	iismod = readl(&(pheadi2s->I2S_TXCR));

	iismod &= (~((1<<5)-1));
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		iismod |= SAMPLE_DATA_8bit;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		iismod |= I2S_DATA_WIDTH(15);
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		iismod |= I2S_DATA_WIDTH(19);
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		iismod |= I2S_DATA_WIDTH(23);
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		iismod |= I2S_DATA_WIDTH(31);
		break;
	}

//	writel((16<<24) |(16<<18)|(16<<12)|(16<<6)|16, &(pheadi2s->I2S_FIFOLR));
	dmarc = readl(&(pheadi2s->I2S_DMACR));

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dmarc = ((dmarc & 0xFFFFFE00) | 16);
	else
		dmarc = ((dmarc & 0xFE00FFFF) | 16<<16);

	writel(dmarc, &(pheadi2s->I2S_DMACR));

	I2S_DBG("Enter %s, %d I2S_TXCR=0x%08X\n", __func__, __LINE__, iismod);

	writel(iismod, &(pheadi2s->I2S_TXCR));

	iismod = iismod & 0x00007FFF;
	writel(iismod, &(pheadi2s->I2S_RXCR));

	spin_unlock_irqrestore(&lock, flags);

	return 0;
}

static int rockchip_i2s_trigger(struct snd_pcm_substream *substream, int cmd, struct snd_soc_dai *dai)
{
	struct rk30_i2s_info *i2s = to_info(dai);
	int ret = 0;

	I2S_DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
			rockchip_snd_rxctrl(i2s, 1);
		else
			rockchip_snd_txctrl(i2s, 1);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
			rockchip_snd_rxctrl(i2s, 0);
		else
			rockchip_snd_txctrl(i2s, 0);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

/*
 * Set Rockchip I2S MCLK source
 */
static int rockchip_i2s_set_sysclk(struct snd_soc_dai *cpu_dai,
	int clk_id, unsigned int freq, int dir)
{
	struct rk30_i2s_info *i2s = to_info(cpu_dai);

	I2S_DBG("Enter:%s, %d, i2s=0x%p, freq=%d\n", __FUNCTION__, __LINE__, i2s, freq);

	/*add scu clk source and enable clk*/
	clk_set_rate(i2s->i2s_clk, freq);
	return 0;
}

/*
 * Set Rockchip Clock dividers
 */
static int rockchip_i2s_set_clkdiv(struct snd_soc_dai *cpu_dai,
	int div_id, int div)
{
	struct rk30_i2s_info *i2s;
	u32 reg;
	unsigned long flags;
	int ret = 0;

	i2s = to_info(cpu_dai);

	spin_lock_irqsave(&lock, flags);

	//stereo mode MCLK/SCK=4  
	reg = readl(&(pheadi2s->I2S_CKR));

	I2S_DBG("Enter:%s, %d, div_id=0x%08X, div=0x%08X\n", __FUNCTION__, __LINE__, div_id, div);
        
	//when i2s in master mode ,must set codec pll div
	switch (div_id) {
	case ROCKCHIP_DIV_BCLK:
		reg &= ~I2S_TX_SCLK_DIV_MASK;
		reg |= I2S_TX_SCLK_DIV(div);
		reg &= ~I2S_RX_SCLK_DIV_MASK;
		reg |= I2S_RX_SCLK_DIV(div);
		break;
	case ROCKCHIP_DIV_MCLK:
		reg &= ~I2S_MCLK_DIV_MASK;
		reg |= I2S_MCLK_DIV(div);
		break;
	case ROCKCHIP_DIV_PRESCALER:
		break;
	default:
		ret = -EINVAL;
		goto out_;
	}
	writel(reg, &(pheadi2s->I2S_CKR));

out_:
	spin_unlock_irqrestore(&lock, flags);

	return ret;
}

static struct snd_soc_dai_ops rockchip_i2s_dai_ops = {
	.trigger = rockchip_i2s_trigger,
	.hw_params = rockchip_i2s_hw_params,
	.set_fmt = rockchip_i2s_set_fmt,
	.set_clkdiv = rockchip_i2s_set_clkdiv,
	.set_sysclk = rockchip_i2s_set_sysclk,
};

#define ROCKCHIP_I2S_STEREO_RATES SNDRV_PCM_RATE_8000_96000
#define ROCKCHIP_I2S_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S8)

struct snd_soc_dai_driver rockchip_i2s_dai[] = {
	{
		.name = "rockchip-i2s.0",
		.id = 0,
		.playback = {
			.channels_min = 2,
			.channels_max = 8,
			.rates = ROCKCHIP_I2S_STEREO_RATES,
			.formats = ROCKCHIP_I2S_FORMATS,
		},
		.capture = {
			.channels_min = 2,
			.channels_max = 2,
			.rates = ROCKCHIP_I2S_STEREO_RATES,
			.formats = ROCKCHIP_I2S_FORMATS,
		},
		.ops = &rockchip_i2s_dai_ops,
		.symmetric_rates = 1,
	},
	{
		.name = "rockchip-i2s.1",
		.id = 1,
		.playback = {
			.channels_min = 2,
			.channels_max = 2,
			.rates = ROCKCHIP_I2S_STEREO_RATES,
			.formats = ROCKCHIP_I2S_FORMATS,
		},
		.capture = {
			.channels_min = 2,
			.channels_max = 2,
			.rates = ROCKCHIP_I2S_STEREO_RATES,
			.formats = ROCKCHIP_I2S_FORMATS,
		},
		.ops = &rockchip_i2s_dai_ops,
		.symmetric_rates = 1,
	},
};

static const struct snd_soc_component_driver rockchip_i2s_component = {
        .name           = "rockchip-i2s",
};

#ifdef CONFIG_PM
static int rockchip_i2s_suspend_noirq(struct device *dev)
{
	I2S_DBG("Enter %s, %d\n", __func__, __LINE__);

	return pinctrl_pm_select_sleep_state(dev);
}

static int rockchip_i2s_resume_noirq(struct device *dev)
{
	I2S_DBG("Enter %s, %d\n", __func__, __LINE__);

	return pinctrl_pm_select_default_state(dev);
}
#else
#define rockchip_i2s_suspend_noirq NULL
#define rockchip_i2s_resume_noirq NULL
#endif

#ifdef CLK_SET_lATER
static void set_clk_later_work(struct work_struct *work)
{
	struct rk30_i2s_info *i2s = rk30_i2s;
	clk_set_rate(i2s->i2s_clk, 11289600);
	if(!IS_ERR(i2s->i2s_mclk) )
		clk_set_rate(i2s->i2s_mclk, 11289600);
}
#endif

static int rockchip_i2s_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct rk30_i2s_info *i2s;
	struct resource *mem, *memregion;
	u32 regs_base;
	int ret;

	I2S_DBG("%s()\n", __FUNCTION__);

	ret = of_property_read_u32(node, "i2s-id", &pdev->id);
	if (ret < 0) {
		dev_err(&pdev->dev, "%s() Can not read property: id\n", __FUNCTION__);
		ret = -ENOMEM;
		goto err;
	}

	if(pdev->id >= MAX_I2S) {
		dev_err(&pdev->dev, "id %d out of range\n", pdev->id);
		ret = -ENOMEM;
		goto err;
	}

	i2s = devm_kzalloc(&pdev->dev, sizeof(struct rk30_i2s_info), GFP_KERNEL);
	if (!i2s) {
		dev_err(&pdev->dev, "Can't allocate i2s info\n");
		ret = -ENOMEM;
		goto err;
	}

	rk30_i2s = i2s;

	i2s->i2s_hclk = clk_get(&pdev->dev, "i2s_hclk");
	if(IS_ERR(i2s->i2s_hclk) ) {
                dev_err(&pdev->dev, "get i2s_hclk failed.\n");
        } else{
		clk_prepare_enable(i2s->i2s_hclk);
	}

	i2s->i2s_clk= clk_get(&pdev->dev, "i2s_clk");
	if (IS_ERR(i2s->i2s_clk)) {
		dev_err(&pdev->dev, "Can't retrieve i2s clock\n");
		ret = PTR_ERR(i2s->i2s_clk);
		goto err;
	}
#ifdef CLK_SET_lATER
	INIT_DELAYED_WORK(&i2s->clk_delayed_work, set_clk_later_work);
	schedule_delayed_work(&i2s->clk_delayed_work, msecs_to_jiffies(10));
#else
	clk_set_rate(i2s->iis_clk, 11289600);
#endif	
	clk_prepare_enable(i2s->i2s_clk);

	i2s->i2s_mclk= clk_get(&pdev->dev, "i2s_mclk");
	if(IS_ERR(i2s->i2s_mclk) ) {
		printk("This platfrom have not i2s_mclk,no need to set i2s_mclk.\n");
	}else{
	#ifdef CLK_SET_lATER
		
	#else
		clk_set_rate(i2s->i2s_mclk, 11289600);
	#endif
		clk_prepare_enable(i2s->i2s_mclk);
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		dev_err(&pdev->dev, "No memory resource\n");
		ret = -ENODEV;
		goto err_clk_put;
	}

	memregion = devm_request_mem_region(&pdev->dev, mem->start,
					    resource_size(mem), "rockchip-i2s");
	if (!memregion) {
		dev_err(&pdev->dev, "Memory region already claimed\n");
		ret = -EBUSY;
		goto err_clk_put;
	}

	i2s->regs = devm_ioremap(&pdev->dev, mem->start, resource_size(mem));
	if (!i2s->regs) {
		dev_err(&pdev->dev, "ioremap failed\n");
		ret = -ENOMEM;
		goto err_clk_put;
	}

	regs_base = mem->start;

	i2s->playback_dma_data.addr = regs_base + I2S_TXR_BUFF;
	i2s->playback_dma_data.addr_width = 4;
	i2s->playback_dma_data.maxburst = 1;

	i2s->capture_dma_data.addr = regs_base + I2S_RXR_BUFF;
	i2s->capture_dma_data.addr_width = 4;
	i2s->capture_dma_data.maxburst = 1;

	i2s->i2s_tx_status = false;
	i2s->i2s_rx_status = false;

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = rockchip_i2s_resume_noirq(&pdev->dev);
		if (ret)
			goto err_pm_disable;
	}

	//set dev name to driver->name.id for sound card register
	dev_set_name(&pdev->dev, "%s.%d", pdev->dev.driver->name, pdev->id);

	ret = snd_soc_register_component(&pdev->dev, &rockchip_i2s_component,
		&rockchip_i2s_dai[pdev->id], 1);

	if (ret) {
		dev_err(&pdev->dev, "Could not register DAI: %d\n", ret);
		ret = -ENOMEM;
		goto err_suspend;
	}

	ret = rockchip_pcm_platform_register(&pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "Could not register PCM: %d\n", ret);
		goto err_unregister_component;
	}

	/* Mark ourselves as in TXRX mode so we can run through our cleanup
	 * process without warnings. */
	rockchip_snd_txctrl(i2s, 0);
	rockchip_snd_rxctrl(i2s, 0);

	dev_set_drvdata(&pdev->dev, i2s);
	return 0;

err_unregister_component:
	snd_soc_unregister_component(&pdev->dev);
err_suspend:
	if (!pm_runtime_status_suspended(&pdev->dev))
		rockchip_i2s_suspend_noirq(&pdev->dev);
err_pm_disable:
	pm_runtime_disable(&pdev->dev);
err_clk_put:
	clk_put(i2s->i2s_clk);
err:
	return ret;

}

static int rockchip_i2s_remove(struct platform_device *pdev)
{
	rockchip_pcm_platform_unregister(&pdev->dev);
	snd_soc_unregister_component(&pdev->dev);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id exynos_i2s_match[] = {
        { .compatible = "rockchip-i2s"},
        {},
};
MODULE_DEVICE_TABLE(of, exynos_i2s_match);
#endif

static const struct dev_pm_ops rockchip_i2s_pm_ops = {
	.suspend_noirq = rockchip_i2s_suspend_noirq,
	.resume_noirq  = rockchip_i2s_resume_noirq,
};

static struct platform_driver rockchip_i2s_driver = {
	.probe  = rockchip_i2s_probe,
	.remove = rockchip_i2s_remove,
	.driver = {
		.name   = "rockchip-i2s",
		.owner  = THIS_MODULE,
		.of_match_table = of_match_ptr(exynos_i2s_match),
		.pm	= &rockchip_i2s_pm_ops,
	},
};
module_platform_driver(rockchip_i2s_driver);

/* Module information */
MODULE_AUTHOR("rockchip");
MODULE_DESCRIPTION("ROCKCHIP IIS ASoC Interface");
MODULE_LICENSE("GPL");


#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
static int proc_i2s_show(struct seq_file *s, void *v)
{

	struct rk30_i2s_info *i2s = rk30_i2s;

	printk("========Show I2S reg========\n");
        
	printk("I2S_TXCR = 0x%08X\n", readl(&(pheadi2s->I2S_TXCR)));
	printk("I2S_RXCR = 0x%08X\n", readl(&(pheadi2s->I2S_RXCR)));
	printk("I2S_CKR = 0x%08X\n", readl(&(pheadi2s->I2S_CKR)));
	printk("I2S_DMACR = 0x%08X\n", readl(&(pheadi2s->I2S_DMACR)));
	printk("I2S_INTCR = 0x%08X\n", readl(&(pheadi2s->I2S_INTCR)));
	printk("I2S_INTSR = 0x%08X\n", readl(&(pheadi2s->I2S_INTSR)));
	printk("I2S_XFER = 0x%08X\n", readl(&(pheadi2s->I2S_XFER)));

	printk("========Show I2S reg========\n");
#if 0
	writel(0x0000000F, &(pheadi2s->I2S_TXCR));
	writel(0x0000000F, &(pheadi2s->I2S_RXCR));
	writel(0x00071f1F, &(pheadi2s->I2S_CKR));
	writel(0x001F0110, &(pheadi2s->I2S_DMACR));
	writel(0x00000003, &(pheadi2s->I2S_XFER));
	while(1)
	{
		writel(0x5555aaaa, &(pheadi2s->I2S_TXDR));
	}		
#endif	
	return 0;
}

static ssize_t i2s_reg_write(struct file *file,
		const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rk30_i2s_info *i2s=rk30_i2s;

	char buf[32];
	size_t buf_size;
	char *start = buf;
	unsigned long value;

	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	buf[buf_size] = 0;

	while (*start == ' ')
		start++;
	value = simple_strtoul(start, &start, 10);

	printk("test --- freq = %ld ret=%d\n",value,clk_set_rate(i2s->i2s_clk, value));
	return buf_size;
}

static int proc_i2s_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_i2s_show, NULL);
}

static const struct file_operations proc_i2s_fops = {
	.open		= proc_i2s_open,
	.read		= seq_read,
	.write = i2s_reg_write,	
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init i2s_proc_init(void)
{
	proc_create("i2s_reg", 0, NULL, &proc_i2s_fops);
	return 0;
}
late_initcall(i2s_proc_init);
#endif /* CONFIG_PROC_FS */

