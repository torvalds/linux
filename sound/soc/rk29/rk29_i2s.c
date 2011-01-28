/*
 * rk29_i2s.c  --  ALSA SoC ROCKCHIP IIS Audio Layer Platform driver
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

#include <asm/dma.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <asm/io.h>

#include <mach/hardware.h>
#include <mach/board.h>
#include <mach/rk29_iomap.h>
#include <mach/rk29-dma-pl330.h>
#include <mach/iomux.h>

#include "rk29_pcm.h"
#include "rk29_i2s.h"


#if 0
#define I2S_DBG(x...) printk(KERN_INFO x)
#else
#define I2S_DBG(x...) do { } while (0)
#endif

#define pheadi2s  ((pI2S_REG)(i2s->regs))

#define MAX_I2S         2

struct rk29_i2s_info {
	struct device	*dev;
	void __iomem	*regs;
        
        u32     feature;

	struct clk	*iis_clk;
	struct clk	*iis_pclk;

        unsigned char   master;

        struct rockchip_pcm_dma_params  *dma_playback;
        struct rockchip_pcm_dma_params  *dma_capture;

	u32		 suspend_iismod;
	u32		 suspend_iiscon;
	u32		 suspend_iispsr;
};

static struct rk29_dma_client rk29_dma_client_out = {
	.name = "I2S PCM Stereo Out"
};

static struct rk29_dma_client rk29_dma_client_in = {
	.name = "I2S PCM Stereo In"
};

static inline struct rk29_i2s_info *to_info(struct snd_soc_dai *cpu_dai)
{
	return cpu_dai->private_data;
}

static struct rockchip_pcm_dma_params rk29_i2s_pcm_stereo_out[MAX_I2S];
static struct rockchip_pcm_dma_params rk29_i2s_pcm_stereo_in[MAX_I2S];
static struct rk29_i2s_info rk29_i2s[MAX_I2S];

struct snd_soc_dai rk29_i2s_dai[MAX_I2S];
EXPORT_SYMBOL_GPL(rk29_i2s_dai);

/*
static struct rockchip_pcm_dma_params rockchip_i2s_pcm_stereo_out[MAX_I2S] = {
        [0] = {
	        .client		= &rk29_dma_client_out,
	        .channel	= DMACH_I2S_2CH_TX, ///0,  //DMACH_I2S_OUT,
	        .dma_addr	= RK29_I2S_2CH_PHYS + I2S_TXR_BUFF,
	        .dma_size	= 4,
        },
        [1] = {
	        .client		= &rk29_dma_client_out,
	        .channel	= DMACH_I2S_8CH_TX, ///0,  //DMACH_I2S_OUT,
	        .dma_addr	= RK29_I2S_8CH_PHYS + I2S_TXR_BUFF,
	        .dma_size	= 4,
        },
};

static struct rockchip_pcm_dma_params rockchip_i2s_pcm_stereo_in[MAX_I2S] = {
        [0] = {
	        .client		= &rk29_dma_client_in,
	        .channel	= DMACH_I2S_2CH_RX,  ///1,  //DMACH_I2S_IN,
	        .dma_addr	= RK29_I2S_2CH_PHYS + I2S_RXR_BUFF,
	        .dma_size	= 4,
        },
        [1] = {
	        .client		= &rk29_dma_client_in,
	        .channel	= DMACH_I2S_8CH_RX,  ///1,  //DMACH_I2S_IN,
	        .dma_addr	= RK29_I2S_8CH_PHYS + I2S_RXR_BUFF,
	        .dma_size	= 4,
        },
};
*/




/* 
 *Turn on or off the transmission path. 
 */
static void rockchip_snd_txctrl(struct rk29_i2s_info *i2s, int on)
{
        u32 opr,xfer,fifosts;
    
        I2S_DBG("Enter %s, %d >>>>>>>>>>>\n", __func__, __LINE__);
        opr  = readl(&(pheadi2s->I2S_DMACR));
        xfer = readl(&(pheadi2s->I2S_XFER));
        opr  &= ~I2S_TRAN_DMA_ENABLE;
        xfer &= ~I2S_TX_TRAN_START;       
        if (on) 
        {                
                writel(opr, &(pheadi2s->I2S_DMACR));
                writel(xfer, &(pheadi2s->I2S_XFER));
                udelay(5);

                opr  |= I2S_TRAN_DMA_ENABLE;
                xfer |= I2S_TX_TRAN_START;
                writel(opr, &(pheadi2s->I2S_DMACR));
                writel(xfer, &(pheadi2s->I2S_XFER));
        }
        else
        {
                writel(opr, &(pheadi2s->I2S_DMACR));
                writel(xfer, &(pheadi2s->I2S_XFER));
        } 
}

static void rockchip_snd_rxctrl(struct rk29_i2s_info *i2s, int on)
{
        u32 opr,xfer,fifosts;
          
        I2S_DBG("Enter %s, %d >>>>>>>>>>>\n", __func__, __LINE__);

        opr  = readl(&(pheadi2s->I2S_DMACR));
        xfer = readl(&(pheadi2s->I2S_XFER));
        
        opr  &= ~I2S_RECE_DMA_ENABLE;
        xfer &= ~I2S_RX_TRAN_START;
        
        if (on) 
        {                
                writel(opr, &(pheadi2s->I2S_DMACR));
                writel(xfer, &(pheadi2s->I2S_XFER));
                udelay(5);

                opr  |= I2S_RECE_DMA_ENABLE;
                xfer |= I2S_RX_TRAN_START;
                writel(opr, &(pheadi2s->I2S_DMACR));
                writel(xfer, &(pheadi2s->I2S_XFER));
        }
        else
        {
                writel(opr, &(pheadi2s->I2S_DMACR));
                writel(xfer, &(pheadi2s->I2S_XFER));
        }  
}

/*
 * Set Rockchip I2S DAI format
 */
static int rockchip_i2s_set_fmt(struct snd_soc_dai *cpu_dai,
		unsigned int fmt)
{
        struct rk29_i2s_info *i2s = to_info(cpu_dai);	
        u32 tx_ctl,rx_ctl;

        I2S_DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);

        tx_ctl = readl(&(pheadi2s->I2S_TXCR));

        switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
                case SND_SOC_DAIFMT_CBM_CFM:  	
                        tx_ctl &= ~I2S_MODE_MASK;  
                        tx_ctl |= I2S_MASTER_MODE;
                        break;
                case SND_SOC_DAIFMT_CBS_CFS:
                        tx_ctl &= ~I2S_MODE_MASK;   
                        tx_ctl |= I2S_SLAVE_MODE;
                        break;
                default:
                        I2S_DBG("unknwon master/slave format\n");
                        return -EINVAL;
        }       

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
                        return -EINVAL;
        }
        I2S_DBG("Enter::%s----%d, I2S_TXCR=0x%X\n",__FUNCTION__,__LINE__,tx_ctl);
        writel(tx_ctl, &(pheadi2s->I2S_TXCR));
        rx_ctl = tx_ctl & 0x00007FFF;
        writel(rx_ctl, &(pheadi2s->I2S_RXCR));
        return 0;
}

static int rockchip_i2s_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params, struct snd_soc_dai *socdai)
{
        struct snd_soc_pcm_runtime *rtd = substream->private_data;
        struct snd_soc_dai_link *dai = rtd->dai;
        struct rk29_i2s_info *i2s = to_info(dai->cpu_dai);
	u32 iismod;
	  
        I2S_DBG("Enter %s, %d >>>>>>>>>>>\n", __func__, __LINE__);
	/*by Vincent Hsiung for EQ Vol Change*/
	#define HW_PARAMS_FLAG_EQVOL_ON 0x21
	#define HW_PARAMS_FLAG_EQVOL_OFF 0x22
        if ((params->flags == HW_PARAMS_FLAG_EQVOL_ON)||(params->flags == HW_PARAMS_FLAG_EQVOL_OFF))
    	{
    		return 0;
    	}
           
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
                dai->cpu_dai->dma_data = i2s->dma_playback;
	else
                dai->cpu_dai->dma_data = i2s->dma_capture;
                
	/* Working copies of register */
	iismod = readl(&(pheadi2s->I2S_TXCR));
        //iismod &= (~((1<<5)-1));
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
        #if defined (CONFIG_SND_RK29_CODEC_SOC_SLAVE) 
        iismod |= I2S_MASTER_MODE;
        #endif

        #if defined (CONFIG_SND_RK29_CODEC_SOC_MASTER) 
        iismod |= I2S_SLAVE_MODE;
        #endif

        writel((16<<24) |(16<<18)|(16<<12)|(16<<6)|16, &(pheadi2s->I2S_FIFOLR));
        writel((16<<16) | 16, &(pheadi2s->I2S_DMACR));

	I2S_DBG("Enter %s, %d I2S_TXCR=0x%08X\n", __func__, __LINE__, iismod);  
	writel(iismod, &(pheadi2s->I2S_TXCR));
        iismod = iismod & 0x00007FFF;
        writel(iismod, &(pheadi2s->I2S_RXCR));        
        return 0;
}


static int rockchip_i2s_trigger(struct snd_pcm_substream *substream, int cmd, struct snd_soc_dai *dai)
{    
        int ret = 0;
        struct snd_soc_pcm_runtime *rtd = substream->private_data;
        struct rk29_i2s_info *i2s = to_info(rtd->dai->cpu_dai);

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
        case SNDRV_PCM_TRIGGER_STOP:
        case SNDRV_PCM_TRIGGER_SUSPEND:
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
 * Set Rockchip Clock source
 */
static int rockchip_i2s_set_sysclk(struct snd_soc_dai *cpu_dai,
	int clk_id, unsigned int freq, int dir)
{
        struct rk29_i2s_info *i2s;        

        i2s = to_info(cpu_dai);
        
        I2S_DBG("Enter:%s, %d, i2s=0x%p, freq=%d\n", __FUNCTION__, __LINE__, i2s, freq);
        /*add scu clk source and enable clk*/
        clk_set_rate(i2s->iis_clk, freq);
        return 0;
}

/*
 * Set Rockchip Clock dividers
 */
static int rockchip_i2s_set_clkdiv(struct snd_soc_dai *cpu_dai,
	int div_id, int div)
{
        struct rk29_i2s_info *i2s;
        u32    reg;

        i2s = to_info(cpu_dai);
        
        /*stereo mode MCLK/SCK=4*/  
	
        reg    = readl(&(pheadi2s->I2S_TXCKR));

        I2S_DBG("Enter:%s, %d, div_id=0x%08X, div=0x%08X\n", __FUNCTION__, __LINE__, div_id, div);
        
        /*when i2s in master mode ,must set codec pll div*/
        switch (div_id) {
        case ROCKCHIP_DIV_BCLK:
                reg &= ~I2S_TX_SCLK_DIV_MASK;
                reg |= I2S_TX_SCLK_DIV(div);
                break;
        case ROCKCHIP_DIV_MCLK:
                reg &= ~I2S_MCLK_DIV_MASK;
                reg |= I2S_MCLK_DIV(div);
                break;
        case ROCKCHIP_DIV_PRESCALER:
                
                break;
        default:
                return -EINVAL;
        }
        writel(reg, &(pheadi2s->I2S_TXCKR));
        writel(reg, &(pheadi2s->I2S_RXCKR));
        return 0;
}

static int rockchip_set_sysclk(struct snd_soc_dai *cpu_dai,
                                int clk_id, unsigned int freq, int dir)
{
        return 0;
}


/*
 * To avoid duplicating clock code, allow machine driver to
 * get the clockrate from here.
 */
u32 rockchip_i2s_get_clockrate(void)
{
        I2S_DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
	return 0;  ///clk_get_rate(s3c24xx_i2s.iis_clk);
}
EXPORT_SYMBOL_GPL(rockchip_i2s_get_clockrate);

#ifdef CONFIG_PM
int rockchip_i2s_suspend(struct snd_soc_dai *cpu_dai)
{
        I2S_DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
        //clk_disable(clk);
        return 0;
}

int rockchip_i2s_resume(struct snd_soc_dai *cpu_dai)
{
        I2S_DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
        //clk_enable(clk);
        return 0;
}		
#else
#define rockchip_i2s_suspend NULL
#define rockchip_i2s_resume NULL
#endif

#define ROCKCHIP_I2S_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |\
		            SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |\
		            SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000)

static struct snd_soc_dai_ops rockchip_i2s_dai_ops = {
	.trigger = rockchip_i2s_trigger,
	.hw_params = rockchip_i2s_hw_params,
	.set_fmt = rockchip_i2s_set_fmt,
	.set_clkdiv = rockchip_i2s_set_clkdiv,
	.set_sysclk = rockchip_i2s_set_sysclk,
};

static int rockchip_i2s_dai_probe(struct platform_device *pdev, struct snd_soc_dai *dai)
{	
	I2S_DBG("Enter %s, %d >>>>>>>>>>>\n", __func__, __LINE__);

        switch(dai->id) {
        case 0:
                rk29_mux_api_set(GPIO2D0_I2S0CLK_MIIRXCLKIN_NAME, GPIO2H_I2S0_CLK);                
                rk29_mux_api_set(GPIO2D1_I2S0SCLK_MIICRS_NAME, GPIO2H_I2S0_SCLK);
                rk29_mux_api_set(GPIO2D2_I2S0LRCKRX_MIITXERR_NAME, GPIO2H_I2S0_LRCK_RX);
                rk29_mux_api_set(GPIO2D3_I2S0SDI_MIICOL_NAME, GPIO2H_I2S0_SDI);
                rk29_mux_api_set(GPIO2D4_I2S0SDO0_MIIRXD2_NAME, GPIO2H_I2S0_SDO0);
                rk29_mux_api_set(GPIO2D5_I2S0SDO1_MIIRXD3_NAME, GPIO2H_I2S0_SDO1);
                rk29_mux_api_set(GPIO2D6_I2S0SDO2_MIITXD2_NAME, GPIO2H_I2S0_SDO2);
                rk29_mux_api_set(GPIO2D7_I2S0SDO3_MIITXD3_NAME, GPIO2H_I2S0_SDO3);
                
                rk29_mux_api_set(GPIO4D6_I2S0LRCKTX0_NAME, GPIO4H_I2S0_LRCK_TX0);
                rk29_mux_api_set(GPIO4D7_I2S0LRCKTX1_NAME, GPIO4H_I2S0_LRCK_TX1);
                break;
        case 1:
                rk29_mux_api_set(GPIO3A0_I2S1CLK_NAME, GPIO3L_I2S1_CLK);
                rk29_mux_api_set(GPIO3A1_I2S1SCLK_NAME, GPIO3L_I2S1_SCLK);
                rk29_mux_api_set(GPIO3A2_I2S1LRCKRX_NAME, GPIO3L_I2S1_LRCK_RX);
                rk29_mux_api_set(GPIO3A3_I2S1SDI_NAME, GPIO3L_I2S1_SDI);
                rk29_mux_api_set(GPIO3A4_I2S1SDO_NAME, GPIO3L_I2S1_SDO);
                rk29_mux_api_set(GPIO3A5_I2S1LRCKTX_NAME, GPIO3L_I2S1_LRCK_TX);
                break;
        default:
                I2S_DBG("Enter:%s, %d, Error For DevId!!!", __FUNCTION__, __LINE__);
                return -EINVAL;
        }
        return 0;
}

static int rk29_i2s_probe(struct platform_device *pdev,
		    struct snd_soc_dai *dai,
		    struct rk29_i2s_info *i2s,
		    unsigned long base)
{
	struct device *dev = &pdev->dev;
        struct resource *res;

        I2S_DBG("Enter %s, %d >>>>>>>>>>>\n", __func__, __LINE__);

	i2s->dev = dev;

	/* record our i2s structure for later use in the callbacks */
	dai->private_data = i2s;

	if (!base) {
		res = platform_get_resource(pdev,
					     IORESOURCE_MEM,
					     0);
		if (!res) {
			dev_err(dev, "Unable to get register resource\n");
			return -ENXIO;
		}

		if (!request_mem_region(res->start, resource_size(res),
					"rk29_i2s")) {
			dev_err(dev, "Unable to request register region\n");
			return -EBUSY;
		}

		base = res->start;
	}

	i2s->regs = ioremap(base, (res->end - res->start) + 1); ////res));
	if (i2s->regs == NULL) {
		dev_err(dev, "cannot ioremap registers\n");
		return -ENXIO;
	}

	i2s->iis_pclk = clk_get(dev, "i2s");
	if (IS_ERR(i2s->iis_pclk)) {
		dev_err(dev, "failed to get iis_clock\n");
		iounmap(i2s->regs);
		return -ENOENT;
	}

	clk_enable(i2s->iis_pclk);

	/* Mark ourselves as in TXRX mode so we can run through our cleanup
	 * process without warnings. */
	rockchip_snd_txctrl(i2s, 0);
	rockchip_snd_rxctrl(i2s, 0);

	return 0;
}

static int rk29_i2s_register_dai(struct snd_soc_dai *dai)
{
	struct snd_soc_dai_ops *ops = dai->ops;

	ops->trigger = rockchip_i2s_trigger;
	if (!ops->hw_params)
		ops->hw_params = rockchip_i2s_hw_params;
	ops->set_fmt = rockchip_i2s_set_fmt;
	ops->set_clkdiv = rockchip_i2s_set_clkdiv;
	ops->set_sysclk = rockchip_i2s_set_sysclk;

	dai->suspend = rockchip_i2s_suspend;
	dai->resume = rockchip_i2s_resume;

	return snd_soc_register_dai(dai);
}

static int __devinit rockchip_i2s_probe(struct platform_device *pdev)
{
        struct rk29_i2s_info *i2s;
        struct snd_soc_dai *dai;
        int    ret;

        I2S_DBG("Enter %s, %d pdev->id = %d >>>>>>>>>>>\n", __func__, __LINE__, pdev->id);

        if(pdev->id >= MAX_I2S) {
                dev_err(&pdev->dev, "id %d out of range\n", pdev->id);
                return -EINVAL;        
        }

        i2s = &rk29_i2s[pdev->id];
        dai = &rk29_i2s_dai[pdev->id];
	dai->dev = &pdev->dev;
	dai->name = "rk29_i2s";
	dai->id = pdev->id;
	dai->symmetric_rates = 1;
	if(pdev->id == 0) {
        	dai->playback.channels_min = 2;
        	dai->playback.channels_max = 8;
	}else{
                dai->playback.channels_min = 2;
        	dai->playback.channels_max = 2;
	}
	dai->playback.rates = ROCKCHIP_I2S_RATES;
	dai->playback.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S24_LE;
	dai->capture.channels_min = 2;
	dai->capture.channels_max = 2;
	dai->capture.rates = SNDRV_PCM_RATE_44100;//ROCKCHIP_I2S_RATES;
	dai->capture.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S24_LE;
	dai->probe = rockchip_i2s_dai_probe; 
	dai->ops = &rockchip_i2s_dai_ops;

	//i2s->feature |= S3C_FEATURE_CDCLKCON;

	i2s->dma_capture = &rk29_i2s_pcm_stereo_in[pdev->id];
	i2s->dma_playback = &rk29_i2s_pcm_stereo_out[pdev->id];

	if (pdev->id == 1) {
		i2s->dma_capture->channel = DMACH_I2S_2CH_RX;
		i2s->dma_capture->dma_addr = RK29_I2S_2CH_PHYS + I2S_RXR_BUFF;
		i2s->dma_playback->channel = DMACH_I2S_2CH_TX;
		i2s->dma_playback->dma_addr = RK29_I2S_2CH_PHYS + I2S_TXR_BUFF;
	} else {
		i2s->dma_capture->channel = DMACH_I2S_8CH_RX;
		i2s->dma_capture->dma_addr = RK29_I2S_8CH_PHYS + I2S_RXR_BUFF;
		i2s->dma_playback->channel = DMACH_I2S_8CH_TX;
		i2s->dma_playback->dma_addr = RK29_I2S_8CH_PHYS + I2S_TXR_BUFF;
	}

	i2s->dma_capture->client = &rk29_dma_client_in;
	i2s->dma_capture->dma_size = 4;
	i2s->dma_playback->client = &rk29_dma_client_out;
	i2s->dma_playback->dma_size = 4;

	i2s->iis_clk = clk_get(&pdev->dev, "i2s");
	I2S_DBG("Enter:%s, %d, iis_clk=%d\n", __FUNCTION__, __LINE__, i2s->iis_clk);
	if (IS_ERR(i2s->iis_clk)) {
		dev_err(&pdev->dev, "failed to get i2s clk\n");
		ret = PTR_ERR(i2s->iis_clk);
		goto err;
	}

	clk_enable(i2s->iis_clk);
        clk_set_rate(i2s->iis_clk, 11289600);
	ret = rk29_i2s_probe(pdev, dai, i2s, 0);
	if (ret)
		goto err_clk;

	ret = rk29_i2s_register_dai(dai);
	if (ret != 0)
		goto err_i2sv2;

	return 0;

err_i2sv2:
	/* Not implemented for I2Sv2 core yet */
err_clk:
	clk_put(i2s->iis_clk);
err:
	return ret;
}


static int __devexit rockchip_i2s_remove(struct platform_device *pdev)
{
	snd_soc_unregister_dai(&rk29_i2s_dai);

	return 0;
}

static struct platform_driver rockchip_i2s_driver = {
	.probe  = rockchip_i2s_probe,
	.remove = __devexit_p(rockchip_i2s_remove),
	.driver = {
		.name   = "rk29_i2s",
		.owner  = THIS_MODULE,
	},
};

static int __init rockchip_i2s_init(void)
{
        I2S_DBG("Enter %s, %d >>>>>>>>>>>\n", __func__, __LINE__);
        
	return  platform_driver_register(&rockchip_i2s_driver);
}
module_init(rockchip_i2s_init);

static void __exit rockchip_i2s_exit(void)
{
	platform_driver_unregister(&rockchip_i2s_driver);
}
module_exit(rockchip_i2s_exit);

/* Module information */
MODULE_AUTHOR("rockchip");
MODULE_DESCRIPTION("ROCKCHIP IIS ASoC Interface");
MODULE_LICENSE("GPL");

