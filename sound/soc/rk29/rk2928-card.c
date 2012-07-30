/*
 * rk2928-card.c  --  SoC audio for RockChip RK2928
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */
#include <linux/delay.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#include "rk29_pcm.h"
#include "rk29_i2s.h"

#ifdef DEBUG
#define DBG(format, ...) \
		printk(KERN_INFO "RK2928 Card: " format "\n", ## __VA_ARGS__)
#else
#define DBG(format, ...)
#endif

static int rk2928_dai_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int pll_out = 0; 
	int div_bclk,div_mclk;
	int ret;
	  
    DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);    

    /* set cpu DAI configuration */
     #if defined (CONFIG_SND_RK29_CODEC_SOC_SLAVE) 
		DBG("Set cpu_dai master\n");    
        ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
                        SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
    #endif	
    #if defined (CONFIG_SND_RK29_CODEC_SOC_MASTER)  
	    ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
                        SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);	
		DBG("Set cpu_dai slave\n");   				
    #endif		
    if (ret < 0)
        return ret;
	
    switch(params_rate(params)) {
        case 8000:
        case 16000:
        case 24000:
        case 32000:
        case 48000:
        case 96000:
            pll_out = 12288000;
            break;
        case 11025:
        case 22050:
        case 44100:
        case 88200:
            pll_out = 11289600;
            break;
        case 176400:
			pll_out = 11289600*2;
        	break;
        case 192000:
        	pll_out = 12288000*2;
        	break;
        default:
            DBG("Enter:%s, %d, Error rate=%d\n",__FUNCTION__,__LINE__,params_rate(params));
            return -EINVAL;
            break;
	}
	DBG("Enter:%s, %d, rate=%d\n",__FUNCTION__,__LINE__,params_rate(params));

	#if defined (CONFIG_SND_RK29_CODEC_SOC_MASTER) 	
		snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
	#endif	
	
	#if defined (CONFIG_SND_RK29_CODEC_SOC_SLAVE)
		div_bclk = 63;
		div_mclk = pll_out/(params_rate(params)*64) - 1;
		
		snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
		snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_BCLK,div_bclk);
		snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, div_mclk);
	#endif
	
	return 0;
}

static struct snd_soc_ops rk2928_dai_ops = {
	.hw_params = rk2928_dai_hw_params,
};

static struct snd_soc_dai_link rk2928_dai[] = {
	{
		.name = "RK2928",
		.stream_name = "RK2928",
		.cpu_dai_name = "rk29_i2s.0",
		.platform_name = "rockchip-audio",
		.codec_name = "rk2928-codec",
		.codec_dai_name = "rk2928-codec",
		.ops = &rk2928_dai_ops,
	},
};

/* Audio machine driver */
static struct snd_soc_card snd_soc_rk2928 = {
	.name = "RK2928",
	.dai_link = rk2928_dai,
	.num_links = ARRAY_SIZE(rk2928_dai),
};

static struct platform_device *rk2928_snd_device;

static int __init rk2928_soc_init(void)
{
	int ret;

	printk(KERN_INFO "RK2928 SoC init\n");

	rk2928_snd_device = platform_device_alloc("soc-audio", -1);
	if (!rk2928_snd_device) {
		printk(KERN_ERR "Platform device allocation failed\n");
		return -ENOMEM;
	}

	platform_set_drvdata(rk2928_snd_device, &snd_soc_rk2928);

	ret = platform_device_add(rk2928_snd_device);
	if (ret)
		goto err1;

	return 0;

err1:
	printk(KERN_ERR "Unable to add platform device\n");
	platform_device_put(rk2928_snd_device);

	return ret;
}
module_init(rk2928_soc_init);

static void __exit rk2928_soc_exit(void)
{
	platform_device_unregister(rk2928_snd_device);
}
module_exit(rk2928_soc_exit);

MODULE_DESCRIPTION("ALSA SoC RK2928");
MODULE_LICENSE("GPL");