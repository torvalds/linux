/*
 * odroid_audio.c  --  SoC audio for AML M8
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
#include <linux/delay.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/jack.h>

#include <asm/mach-types.h>
#include <mach/hardware.h>

#include <linux/switch.h>
#include <linux/amlogic/saradc.h>

#include "aml_i2s_dai.h"
#include "aml_i2s.h"
#include "odroid_audio.h"
#include "aml_audio_hw.h"
#include "../../codecs/aml_m8_codec.h"
#include <mach/register.h>

#ifdef CONFIG_USE_OF
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/amlogic/aml_gpio_consumer.h>
#include <linux/amlogic/aml_audio_codec_probe.h>
#include <linux/of_gpio.h>
#include <mach/pinmux.h>
#include <plat/io.h>
#endif

#ifdef CONFIG_MESON_TRUSTZONE
#include <mach/meson-secure.h>
#endif

#define DRV_NAME "odroid_snd"

static int odroid_hw_params(struct snd_pcm_substream *substream,
    struct snd_pcm_hw_params *params)
{
    struct snd_soc_pcm_runtime *rtd = substream->private_data;
    struct snd_soc_dai *codec_dai = rtd->codec_dai;
    struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
    int ret;

    printk(KERN_INFO "enter %s rate=%d, format=%d \n", __func__, params_rate(params), params_format(params));

    /* set codec DAI configuration */
    ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
        SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
    if (ret < 0) {
        printk(KERN_ERR "%s: set codec dai fmt failed!\n", __func__);
        return ret;
    }

    /* set cpu DAI configuration */
    ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
        SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
    if (ret < 0) {
        printk(KERN_ERR "%s: set cpu dai fmt failed!\n", __func__);
        return ret;
    }

    /* set cpu DAI clock */
    ret = snd_soc_dai_set_sysclk(cpu_dai, 0, params_rate(params) * 256, SND_SOC_CLOCK_OUT);
    if (ret < 0) {
        printk(KERN_ERR "%s: set cpu dai sysclk failed (rate: %d)!\n", __func__, params_rate(params));
        return ret;
    }
    return 0;
}

static struct snd_soc_ops odroid_ops = {
    .hw_params = odroid_hw_params,
};

static int odroid_set_bias_level(struct snd_soc_card *card,
        struct snd_soc_dapm_context *dapm, enum snd_soc_bias_level level)
{
    int ret = 0;
    struct odroid_audio_private_data * p_odroid_audio;

    p_odroid_audio = snd_soc_card_get_drvdata(card);
    if (p_odroid_audio->bias_level == (int)level)
        return 0;

    p_odroid_audio->bias_level = (int)level;
    return ret;
}

#ifdef CONFIG_PM_SLEEP
static int aml_suspend_pre(struct snd_soc_card *card)
{
    printk(KERN_DEBUG "enter %s\n", __func__);
    return 0;
}

static int i2s_gpio_set(struct snd_soc_card *card)
{
    struct odroid_audio_private_data *p_odroid_audio;
    const char *str=NULL;
    int ret;
    

    p_odroid_audio = snd_soc_card_get_drvdata(card);
    if(p_odroid_audio->pin_ctl)
        devm_pinctrl_put(p_odroid_audio->pin_ctl);
    ret = of_property_read_string(card->dev->of_node, "I2S_MCLK", &str);
    if (ret < 0) {
        printk("I2S_MCLK: faild to get gpio I2S_MCLK!\n");
    }else{
        p_odroid_audio->gpio_i2s_m = amlogic_gpio_name_map_num(str);
        amlogic_gpio_request_one(p_odroid_audio->gpio_i2s_m,GPIOF_OUT_INIT_LOW,"low_mclk");
        amlogic_set_value(p_odroid_audio->gpio_i2s_m, 0, "low_mclk");
    }

    ret = of_property_read_string(card->dev->of_node, "I2S_SCLK", &str);
    if (ret < 0) {
        printk("I2S_SCLK: faild to get gpio I2S_SCLK!\n");
    }else{
        p_odroid_audio->gpio_i2s_s = amlogic_gpio_name_map_num(str);
        amlogic_gpio_request_one(p_odroid_audio->gpio_i2s_s,GPIOF_OUT_INIT_LOW,"low_sclk");
        amlogic_set_value(p_odroid_audio->gpio_i2s_s, 0, "low_sclk");
    }

    ret = of_property_read_string(card->dev->of_node, "I2S_LRCLK", &str);
    if (ret < 0) {
        printk("I2S_LRCLK: faild to get gpio I2S_LRCLK!\n");
    }else{
        p_odroid_audio->gpio_i2s_r = amlogic_gpio_name_map_num(str);
        amlogic_gpio_request_one(p_odroid_audio->gpio_i2s_r,GPIOF_OUT_INIT_LOW,"low_lrclk");
        amlogic_set_value(p_odroid_audio->gpio_i2s_r, 0, "low_lrclk");
    }

    ret = of_property_read_string(card->dev->of_node, "I2S_ODAT", &str);
    if (ret < 0) {
        printk("I2S_ODAT: faild to get gpio I2S_ODAT!\n");
    }else{
        p_odroid_audio->gpio_i2s_o = amlogic_gpio_name_map_num(str);
        amlogic_gpio_request_one(p_odroid_audio->gpio_i2s_o,GPIOF_OUT_INIT_LOW,"low_odata");
        amlogic_set_value(p_odroid_audio->gpio_i2s_o, 0, "low_odata");
    }
    return 0;
}
static int aml_suspend_post(struct snd_soc_card *card)
{
    printk(KERN_DEBUG "enter %s\n", __func__);
    i2s_gpio_set(card);
    return 0;
}

static int aml_resume_pre(struct snd_soc_card *card)
{
    struct odroid_audio_private_data *p_odroid_audio;
    p_odroid_audio = snd_soc_card_get_drvdata(card);

    if(p_odroid_audio->gpio_i2s_m)
        amlogic_gpio_free(p_odroid_audio->gpio_i2s_m,"low_mclk");
    if(p_odroid_audio->gpio_i2s_s)
        amlogic_gpio_free(p_odroid_audio->gpio_i2s_s,"low_sclk");
    if(p_odroid_audio->gpio_i2s_r)
        amlogic_gpio_free(p_odroid_audio->gpio_i2s_r,"low_lrclk");
    if(p_odroid_audio->gpio_i2s_o)
        amlogic_gpio_free(p_odroid_audio->gpio_i2s_o,"low_odata");

    p_odroid_audio->pin_ctl = devm_pinctrl_get_select(card->dev, "odroid_i2s");
    return 0;
}

static int aml_resume_post(struct snd_soc_card *card)
{
    printk(KERN_DEBUG "enter %s\n", __func__);
    return 0;
}
#else
#define aml_suspend_pre  NULL
#define aml_suspend_post NULL
#define aml_resume_pre   NULL
#define aml_resume_post  NULL
#endif

static struct snd_soc_dai_link odroid_dai_link[] = {
    {
        .name = "SND_PCM5102",
        .stream_name = "PCM5102 HiFi",
        .cpu_dai_name = "aml-i2s-dai.0",
        .platform_name = "aml-i2s.0",
        .codec_name = "pcm5102.0",
        .codec_dai_name = "pcm5102",
        .ops = &odroid_ops,
    },
};

static struct snd_soc_card aml_snd_soc_card = {
    .driver_name = "SOC-Audio",
    .dai_link = &odroid_dai_link[0],
    .num_links = ARRAY_SIZE(odroid_dai_link),
    .set_bias_level = odroid_set_bias_level,
#ifdef CONFIG_PM_SLEEP
	.suspend_pre    = aml_suspend_pre,
	.suspend_post   = aml_suspend_post,
	.resume_pre     = aml_resume_pre,
	.resume_post    = aml_resume_post,
#endif
};

static int odroid_audio_probe(struct platform_device *pdev)
{
    struct snd_soc_card *card = &aml_snd_soc_card;
    struct odroid_audio_private_data *p_odroid_audio;
    int ret = 0;

#ifdef CONFIG_USE_OF
    p_odroid_audio = devm_kzalloc(&pdev->dev,
            sizeof(struct odroid_audio_private_data), GFP_KERNEL);
    if (!p_odroid_audio) {
        dev_err(&pdev->dev, "Can't allocate odroid_audio_private_data\n");
        ret = -ENOMEM;
        goto err;
    }
    card->dev = &pdev->dev;
    platform_set_drvdata(pdev, card);
    snd_soc_card_set_drvdata(card, p_odroid_audio);
    if (!(pdev->dev.of_node)) {
        dev_err(&pdev->dev, "Must be instantiated using device tree\n");
        ret = -EINVAL;
        goto err;
    }
    ret=of_property_read_string(pdev->dev.of_node,"pinctrl-names",&p_odroid_audio->pinctrl_name);
    printk(KERN_DEBUG "card->pinctrl_name:%s\n",p_odroid_audio->pinctrl_name);
    p_odroid_audio->pin_ctl = devm_pinctrl_get_select(&pdev->dev, p_odroid_audio->pinctrl_name);

    ret = snd_soc_of_parse_card_name(card, "aml,sound_card");
    if (ret)
        goto err;

    ret = snd_soc_register_card(card);
    if (ret) {
        dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n",
            ret);
        goto err;
    }
    return 0;
#endif
err:
    return ret;
}

static int odroid_audio_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct odroid_audio_private_data *p_odroid_audio;

	p_odroid_audio = snd_soc_card_get_drvdata(card);
	if(p_odroid_audio->pin_ctl)
		devm_pinctrl_put(p_odroid_audio->pin_ctl);

	snd_soc_unregister_card(card);
	return 0;
}

#ifdef CONFIG_USE_OF
static const struct of_device_id odroid_audio_dt_match[]={
	{ .compatible = "sound_card, odroid_snd", },
	{},
};
#else
#define odroid_audio_dt_match NULL
#endif

static struct platform_driver odroid_audio_driver = {
    .probe  = odroid_audio_probe,
    .remove = odroid_audio_remove,
    .driver = {
        .name = DRV_NAME,
        .owner = THIS_MODULE,
        .pm = &snd_soc_pm_ops,
        .of_match_table = odroid_audio_dt_match,
    },
};

static int __init odroid_audio_init(void)
{
    return platform_driver_register(&odroid_audio_driver);
}

static void __exit odroid_audio_exit(void)
{
    platform_driver_unregister(&odroid_audio_driver);
}

#ifdef CONFIG_DEFERRED_MODULE_INIT
deferred_module_init(odroid_audio_init);
#else
module_init(odroid_audio_init);
#endif
module_exit(odroid_audio_exit);

/* Module information */
MODULE_AUTHOR("Hardkernel, Inc.");
MODULE_DESCRIPTION("ODROID audio machine Asoc driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);

