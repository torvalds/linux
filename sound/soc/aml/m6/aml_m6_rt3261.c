/*
 * aml_m6_rt3261.c  --  SoC audio for AML M6
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/jack.h>
#include <sound/rt3261.h>

#include <asm/mach-types.h>
#include <mach/hardware.h>

#include <linux/switch.h>


#include "../codecs/rt3261/rt3261.h"
#include "aml_dai.h"
#include "aml_pcm.h"
#include "aml_audio_hw.h"

#define HP_DET                  1

struct rt3261_private_data {
    int bias_level;
    int clock_en;
#if HP_DET
    int timer_en;
    int detect_flag;
    struct timer_list timer;
    struct work_struct work;
    struct mutex lock;
    struct snd_soc_jack jack;
    void* data;
    struct switch_dev sdev; // for android
#endif
};

#define DEBUG	1
//#undef DEBUG
#ifdef DEBUG
#define dprintk(msg...) printk(msg)
#else
#define dprintk(msg...) while(0){}
#endif

static struct rt3261_platform_data *rt3261_snd_pdata = NULL;
static struct rt3261_private_data* rt3261_snd_priv = NULL;

static void rt3261_dev_init(void)
{
    if (rt3261_snd_pdata->device_init) {
        rt3261_snd_pdata->device_init();
    }
}

static void rt3261_dev_uninit(void)
{
    if (rt3261_snd_pdata->device_uninit) {
        rt3261_snd_pdata->device_uninit();
    }
}

static void rt3261_set_clock(int enable)
{
    /* set clock gating */
    rt3261_snd_priv->clock_en = enable;

    return ;
}

static void rt3261_set_output(struct snd_soc_codec *codec)
{
    struct snd_soc_dapm_context *dapm = &codec->dapm;
#if 0
    if (rt3261_snd_pdata->spk_output != RT3261_SPK_STEREO) {
        if (rt3261_snd_pdata->spk_output == RT3261_SPK_RIGHT) {
            snd_soc_dapm_nc_pin(dapm, "SPOL");

            snd_soc_update_bits(codec, RT3261_SPK_MONO_OUT_CTRL,
                0xf000,
                RT3261_M_SPKVOL_L_TO_SPOL_MIXER | RT3261_M_SPKVOL_R_TO_SPOL_MIXER);
        } else {
            snd_soc_dapm_nc_pin(dapm, "SPOR");

            snd_soc_update_bits(codec, RT3261_SPK_MONO_OUT_CTRL,
                0xf000,
                RT3261_M_SPKVOL_L_TO_SPOR_MIXER | RT3261_M_SPKVOL_R_TO_SPOR_MIXER);
        }

        snd_soc_update_bits(codec, RT3261_SPK_MONO_HP_OUT_CTRL,
            RT3261_SPK_L_MUX_SEL_MASK | RT3261_SPK_R_MUX_SEL_MASK | RT3261_HP_L_MUX_SEL_MASK | RT3261_HP_R_MUX_SEL_MASK,
            RT3261_SPK_L_MUX_SEL_SPKMIXER_L | RT3261_SPK_R_MUX_SEL_SPKMIXER_R | RT3261_HP_L_MUX_SEL_HPVOL_L | RT3261_HP_R_MUX_SEL_HPVOL_R);
    } else {
        snd_soc_update_bits(codec, RT3261_SPK_MONO_OUT_CTRL,
            0xf000,
            RT3261_M_SPKVOL_R_TO_SPOL_MIXER | RT3261_M_SPKVOL_L_TO_SPOR_MIXER);

        snd_soc_update_bits(codec, RT3261_SPK_MONO_HP_OUT_CTRL,
            RT3261_SPK_L_MUX_SEL_MASK | RT3261_SPK_R_MUX_SEL_MASK | RT3261_HP_L_MUX_SEL_MASK | RT3261_HP_R_MUX_SEL_MASK,
            RT3261_SPK_L_MUX_SEL_SPKMIXER_L | RT3261_SPK_R_MUX_SEL_SPKMIXER_R | RT3261_HP_L_MUX_SEL_HPVOL_L | RT3261_HP_R_MUX_SEL_HPVOL_R);
    }
#endif
}

static void rt3261_set_input(struct snd_soc_codec *codec)
{
#if 0
    if (rt3261_snd_pdata->mic_input == RT3261_MIC_SINGLEENDED) {
        /* single-ended input mode */
        snd_soc_update_bits(codec, RT3261_MIC_CTRL_1,
            RT3261_MIC1_DIFF_INPUT_CTRL,
            0);
    } else {
        /* differential input mode */
        snd_soc_update_bits(codec, RT3261_MIC_CTRL_1,
            RT3261_MIC1_DIFF_INPUT_CTRL,
            RT3261_MIC1_DIFF_INPUT_CTRL);
    }
#endif
}

#if HP_DET
static int rt3261_detect_hp(void)
{
    int flag = -1;

    if (rt3261_snd_pdata->hp_detect)
    {
        flag = rt3261_snd_pdata->hp_detect();
    }

    return flag;
}

static void rt3261_start_timer(unsigned long delay)
{
    rt3261_snd_priv->timer.expires = jiffies + delay;
    rt3261_snd_priv->timer.data = (unsigned long)rt3261_snd_priv;
    rt3261_snd_priv->detect_flag = -1;
    add_timer(&rt3261_snd_priv->timer);
    rt3261_snd_priv->timer_en = 1;
}

static void rt3261_stop_timer(void)
{
    del_timer_sync(&rt3261_snd_priv->timer);
    cancel_work_sync(&rt3261_snd_priv->work);
    rt3261_snd_priv->timer_en = 0;
    rt3261_snd_priv->detect_flag = -1;
}

static void rt3261_work_func(struct work_struct *work)
{
    struct rt3261_private_data *pdata = NULL;
    struct snd_soc_codec *codec = NULL;
    int jack_type = 0;
    int flag = -1;
	int status = SND_JACK_HEADPHONE;

    pdata = container_of(work, struct rt3261_private_data, work);
    codec = (struct snd_soc_codec *)pdata->data;

    flag = rt3261_detect_hp();
    if(pdata->detect_flag != flag) {
        if (flag == 1) {
	    jack_type = rt3261_headset_detect(codec, 1);
            dprintk(KERN_INFO "rt3261 hp pluged jack_type: %d\n", jack_type);
            snd_soc_jack_report(&pdata->jack, status, SND_JACK_HEADPHONE);
            switch_set_state(&pdata->sdev, 1); 
        } else {
            dprintk(KERN_INFO "rt3261 hp unpluged\n");
	    rt3261_headset_detect(codec, 0);
            snd_soc_jack_report(&pdata->jack, 0, SND_JACK_HEADPHONE);
            switch_set_state(&pdata->sdev, 0);
        }

        pdata->detect_flag = flag;
    }
}


static void rt3261_timer_func(unsigned long data)
{
    struct rt3261_private_data *pdata = (struct rt3261_private_data *)data;
    unsigned long delay = msecs_to_jiffies(200);

    schedule_work(&pdata->work);
    mod_timer(&pdata->timer, jiffies + delay);
}
#endif

static int rt3261_prepare(struct snd_pcm_substream *substream)
{
    dprintk(KERN_INFO "enter %s stream: %s\n", __func__, (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ? "playback" : "capture");
#if HP_DET
    mutex_lock(&rt3261_snd_priv->lock);
    if (!rt3261_snd_priv->timer_en) {
        rt3261_start_timer(msecs_to_jiffies(100));
    }
    mutex_unlock(&rt3261_snd_priv->lock);
#endif
    return 0;
}

static int rt3261_hw_params(struct snd_pcm_substream *substream,
    struct snd_pcm_hw_params *params)
{
    struct snd_soc_pcm_runtime *rtd = substream->private_data;
    struct snd_soc_dai *codec_dai = rtd->codec_dai;
    struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
    int ret;

    dprintk(KERN_INFO "enter %s stream: %s rate: %d format: %d\n", __func__, (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ? "playback" : "capture", params_rate(params), params_format(params));

    /* set codec DAI configuration */
    ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
        SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
    if (ret < 0) {
        dprintk(KERN_ERR "%s: set codec dai fmt failed!\n", __func__);
        return ret;
    }

    /* set cpu DAI configuration */
    ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
        SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
    if (ret < 0) {
        dprintk(KERN_ERR "%s: set cpu dai fmt failed!\n", __func__);
        return ret;
    }

    /* set codec DAI clock */
    //ret = snd_soc_dai_set_sysclk(codec_dai, 0, params_rate(params) * 256, SND_SOC_CLOCK_IN);
    ret = snd_soc_dai_set_sysclk(codec_dai, 0, params_rate(params) * MCLKFS_RATIO, SND_SOC_CLOCK_IN);
    if (ret < 0) {
        dprintk(KERN_ERR "%s: set codec dai sysclk failed (rate: %d)!\n", __func__, params_rate(params));
        return ret;
    }

    /* set cpu DAI clock */
    //ret = snd_soc_dai_set_sysclk(cpu_dai, 0, params_rate(params) * 256, SND_SOC_CLOCK_OUT);
    ret = snd_soc_dai_set_sysclk(cpu_dai, 0, params_rate(params) * MCLKFS_RATIO, SND_SOC_CLOCK_OUT);
    if (ret < 0) {
        dprintk(KERN_ERR "%s: set cpu dai sysclk failed (rate: %d)!\n", __func__, params_rate(params));
        return ret;
    }

    return 0;
}

static int rt3261_voice_hw_params(struct snd_pcm_substream *substream,
    struct snd_pcm_hw_params *params)
{
    struct snd_soc_pcm_runtime *rtd = substream->private_data;
    struct snd_soc_dai *codec_dai = rtd->codec_dai;
    struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
    int ret;

    dprintk(KERN_INFO "enter %s stream: %s rate: %d format: %d\n", __func__, (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ? "playback" : "capture", params_rate(params), params_format(params));

    /* set codec DAI configuration */
    ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_DSP_A |
        SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
    if (ret < 0) {
        dprintk(KERN_ERR "%s: set codec dai fmt failed!\n", __func__);
        return ret;
    }

    /* set cpu DAI configuration */
    ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_DSP_A |
        SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
    if (ret < 0) {
        dprintk(KERN_ERR "%s: set cpu dai fmt failed!\n", __func__);
        return ret;
    }
    //bard 10-22 s
    //ret = snd_soc_dai_set_pll(codec_dai, 0, RT3261_PLL1_S_MCLK, 12288000, 12288000);
    ret = snd_soc_dai_set_pll(codec_dai, 0, RT3261_PLL1_S_MCLK, 24576000, 24576000);
    if (ret < 0) {
        dprintk(KERN_ERR "%s: set codec dai pll failed (rate: %d)!\n", __func__, params_rate(params));
        return ret;
    }
    //bard 10-22 e

    /* set codec DAI clock */
    #if 0 //org
    ret = snd_soc_dai_set_sysclk(codec_dai, 0, params_rate(params) * 256 * 6, SND_SOC_CLOCK_IN);
    #else //bard 10-22
    //ret = snd_soc_dai_set_sysclk(codec_dai, RT3261_SCLK_S_PLL1, params_rate(params) * 256 * 6, SND_SOC_CLOCK_IN);
    ret = snd_soc_dai_set_sysclk(codec_dai, RT3261_SCLK_S_PLL1, params_rate(params) * 256 * 12, SND_SOC_CLOCK_IN);
    #endif
    if (ret < 0) {
        dprintk(KERN_ERR "%s: set codec dai sysclk failed (rate: %d)!\n", __func__, params_rate(params));
        return ret;
    }

    /* set cpu DAI clock */
    ret = snd_soc_dai_set_sysclk(cpu_dai, 0, params_rate(params) * 256, SND_SOC_CLOCK_OUT);
    if (ret < 0) {
        dprintk(KERN_ERR "%s: set cpu dai sysclk failed (rate: %d)!\n", __func__, params_rate(params));
        return ret;
    }

    return 0;
}

static struct snd_soc_ops rt3261_soc_ops = {
    .prepare   = rt3261_prepare,
    .hw_params = rt3261_hw_params,
};

static struct snd_soc_ops rt3261_voice_ops = {
	.prepare   = rt3261_prepare,
	.hw_params = rt3261_voice_hw_params,
};

static int rt3261_set_bias_level(struct snd_soc_card *card,
			      enum snd_soc_bias_level level)
{
    int ret = 0;

    dprintk(KERN_DEBUG "enter %s level: %d\n", __func__, level);

    if (rt3261_snd_priv->bias_level == (int)level)
        return 0;

    switch (level) {
    case SND_SOC_BIAS_ON:
#if HP_DET
        mutex_lock(&rt3261_snd_priv->lock);
        if (!rt3261_snd_priv->timer_en) {
            rt3261_start_timer(msecs_to_jiffies(100));
        }
        mutex_unlock(&rt3261_snd_priv->lock);
#endif
        break;
    case SND_SOC_BIAS_PREPARE:
        /* clock enable */
        if (!rt3261_snd_priv->clock_en) {
            rt3261_set_clock(1);
        }
        break;

    case SND_SOC_BIAS_OFF:
    case SND_SOC_BIAS_STANDBY:
        /* clock disable */
        if (rt3261_snd_priv->clock_en) {
            rt3261_set_clock(0);
        }
#if HP_DET
        /* stop timer */
        mutex_lock(&rt3261_snd_priv->lock);
        if (rt3261_snd_priv->timer_en) {
            rt3261_stop_timer();
        }
        mutex_unlock(&rt3261_snd_priv->lock);
#endif
        break;
    default:
        return ret;
    }

    rt3261_snd_priv->bias_level = (int)level;

    return ret;
}

#ifdef CONFIG_PM_SLEEP
static int rt3261_suspend_pre(struct snd_soc_card *card)
{
    dprintk(KERN_DEBUG "enter %s\n", __func__);
#if HP_DET
    /* stop timer */
    mutex_lock(&rt3261_snd_priv->lock);
    if (rt3261_snd_priv->timer_en) {
        rt3261_stop_timer();
    }
    mutex_unlock(&rt3261_snd_priv->lock);
#endif
    return 0;
}

static int rt3261_suspend_post(struct snd_soc_card *card)
{
    dprintk(KERN_DEBUG "enter %s\n", __func__);
    return 0;
}

static int rt3261_resume_pre(struct snd_soc_card *card)
{
    dprintk(KERN_DEBUG "enter %s\n", __func__);
    return 0;
}

static int rt3261_resume_post(struct snd_soc_card *card)
{
    dprintk(KERN_DEBUG "enter %s\n", __func__);
    return 0;
}
#else
#define rt3261_suspend_pre  NULL
#define rt3261_suspend_post NULL
#define rt3261_resume_pre   NULL
#define rt3261_resume_post  NULL
#endif

static const struct snd_soc_dapm_widget rt3261_dapm_widgets[] = {
    SND_SOC_DAPM_SPK("Ext Spk", NULL),
    SND_SOC_DAPM_HP("HP", NULL),
};

static const struct snd_soc_dapm_route rt3261_dapm_intercon[] = {
    {"Ext Spk", NULL, "SPOL"},
    {"Ext Spk", NULL, "SPOR"},

    {"HP", NULL, "HPOL"},
    {"HP", NULL, "HPOR"},
};

#if HP_DET
static struct snd_soc_jack_pin jack_pins[] = {
    {
        .pin = "HP",
        .mask = SND_JACK_HEADPHONE,
    }
};
#endif
static int rt3261_codec_init2(struct snd_soc_pcm_runtime *rtd)
{
    return 0;
}

static int rt3261_codec_init(struct snd_soc_pcm_runtime *rtd)
{
    struct snd_soc_codec *codec = rtd->codec;
    //struct snd_soc_dai *codec_dai = rtd->codec_dai;
    struct snd_soc_dapm_context *dapm = &codec->dapm;
    int ret = 0;

    dprintk(KERN_DEBUG "enter %s rt3261_snd_pdata: %p\n", __func__, rt3261_snd_pdata);

    /* Add specific widgets */
    snd_soc_dapm_new_controls(dapm, rt3261_dapm_widgets,
                  ARRAY_SIZE(rt3261_dapm_widgets));
    /* Set up specific audio path interconnects */
    snd_soc_dapm_add_routes(dapm, rt3261_dapm_intercon, ARRAY_SIZE(rt3261_dapm_intercon));
#if 0 //we have 3g, so do not set endpoint,add jf.s
    /* Setup spk/hp/mono output */
    rt3261_set_output(codec);

    /* Setuo mic input */
    rt3261_set_input(codec);
#endif
    /* not connected */
    snd_soc_dapm_nc_pin(dapm, "MONO");
    snd_soc_dapm_nc_pin(dapm, "AUXO2");

    snd_soc_dapm_nc_pin(dapm, "DMIC");
    snd_soc_dapm_nc_pin(dapm, "AXIL");
    snd_soc_dapm_nc_pin(dapm, "AXIR");

    /* always connected */
    snd_soc_dapm_enable_pin(dapm, "Ext Spk");

    /* disable connected */
    snd_soc_dapm_disable_pin(dapm, "HP");

    snd_soc_dapm_sync(dapm);

#if HP_DET
    ret = snd_soc_jack_new(codec, "hp switch", SND_JACK_HEADPHONE, &rt3261_snd_priv->jack);
    if (ret) {
        printk(KERN_WARNING "Failed to alloc resource for hp switch\n");
    } else {
        ret = snd_soc_jack_add_pins(&rt3261_snd_priv->jack, ARRAY_SIZE(jack_pins), jack_pins);
        if (ret) {
            dprintk(KERN_WARNING "Failed to setup hp pins\n");
        }
    }
    rt3261_snd_priv->data= (void*)codec;

    init_timer(&rt3261_snd_priv->timer);
    rt3261_snd_priv->timer.function = rt3261_timer_func;
    rt3261_snd_priv->timer.data = (unsigned long)rt3261_snd_priv;

    INIT_WORK(&rt3261_snd_priv->work, rt3261_work_func);
    mutex_init(&rt3261_snd_priv->lock);
#endif

    return 0;
}

static struct snd_soc_dai_link rt3261_dai_link[] = {
    {
        .name = "RT3261",
        .stream_name = "RT3261 PCM",
        .cpu_dai_name = "aml-dai0",
        .codec_dai_name = "rt3261-aif1",
        .init = rt3261_codec_init,
        .platform_name = "aml-audio.0",
        .codec_name = "rt3261.1-001c",
        .ops = &rt3261_soc_ops,
    },
#if 1 //add jf.s
    {
        .name = "RT3261_BT_VOICE",
        .stream_name = "RT3261 BT PCM",
        .cpu_dai_name = "aml-dai0",
        .codec_dai_name = "rt3261-aif2",
        .init = rt3261_codec_init2,
        .platform_name = "aml-audio.0",
        .codec_name = "rt3261.1-001c",
        .ops = &rt3261_voice_ops,
    },
#endif
};
#define POP_TIME  10   //10ms
static struct snd_soc_card snd_soc_rt3261 = {
    .name = "AML-RT3261",
    .driver_name = "SOC-Audio",
    .dai_link = &rt3261_dai_link[0],
    .num_links = ARRAY_SIZE(rt3261_dai_link),
    .set_bias_level = rt3261_set_bias_level,
#ifdef CONFIG_PM_SLEEP
	.suspend_pre    = rt3261_suspend_pre,
	.suspend_post   = rt3261_suspend_post,
	.resume_pre     = rt3261_resume_pre,
	.resume_post    = rt3261_resume_post,
#endif
//	.pop_time 		= POP_TIME,   //add by jf.s for power up/down widgets
};

static struct platform_device *rt3261_snd_device = NULL;

static int rt3261_audio_probe(struct platform_device *pdev)
{
    int ret = 0;

    dprintk(KERN_DEBUG "enter %s\n", __func__);
    printk("rt3261, rt3261_audio_probe\n");
    rt3261_snd_pdata = pdev->dev.platform_data;
    snd_BUG_ON(!rt3261_snd_pdata);

    rt3261_snd_priv = (struct rt3261_private_data*)kzalloc(sizeof(struct rt3261_private_data), GFP_KERNEL);
    if (!rt3261_snd_priv) {
        dprintk(KERN_ERR "ASoC: Platform driver data allocation failed\n");
        return -ENOMEM;
    }

    rt3261_snd_device = platform_device_alloc("soc-audio", -1);
    if (!rt3261_snd_device) {
        dprintk(KERN_ERR "ASoC: Platform device allocation failed\n");
        ret = -ENOMEM;
        goto err;
    }

    platform_set_drvdata(rt3261_snd_device, &snd_soc_rt3261);

    ret = platform_device_add(rt3261_snd_device);
    if (ret) {
        dprintk(KERN_ERR "ASoC: Platform device allocation failed\n");
        goto err_device_add;
    }

    rt3261_snd_priv->bias_level = SND_SOC_BIAS_OFF;
    rt3261_snd_priv->clock_en = 0;

#if HP_DET
    rt3261_snd_priv->sdev.name = "h2w";//for report headphone to android
    ret = switch_dev_register(&rt3261_snd_priv->sdev);
    if (ret < 0){
        printk(KERN_ERR "ASoC: register switch dev failed\n");
        goto err;
    }
#endif


    rt3261_dev_init();

    return ret;

err_device_add:
    platform_device_put(rt3261_snd_device);

err:
    kfree(rt3261_snd_priv);

    return ret;
}

static int rt3261_audio_remove(struct platform_device *pdev)
{
    int ret = 0;

    rt3261_dev_uninit();

    platform_device_put(rt3261_snd_device);
    kfree(rt3261_snd_priv);

    rt3261_snd_device = NULL;
    rt3261_snd_priv = NULL;
    rt3261_snd_pdata = NULL;

    return ret;
}

static struct platform_driver aml_m6_rt3261_driver = {
    .probe  = rt3261_audio_probe,
    .remove = __devexit_p(rt3261_audio_remove),
    .driver = {
        .name = "aml_rt3261_audio",
        .owner = THIS_MODULE,
    },
};

static int __init aml_m6_rt3261_init(void)
{
    return platform_driver_register(&aml_m6_rt3261_driver);
}

static void __exit aml_m6_rt3261_exit(void)
{
    platform_driver_unregister(&aml_m6_rt3261_driver);
}

module_init(aml_m6_rt3261_init);
module_exit(aml_m6_rt3261_exit);

/* Module information */
MODULE_AUTHOR("AMLogic, Inc.");
MODULE_DESCRIPTION("AML RT3261 audio driver");
MODULE_LICENSE("GPL");
