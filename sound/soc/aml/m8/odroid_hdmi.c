/*
 * aml_m8.c  --  SoC audio for AML M8
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
#include "aml_m8.h"
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

#define DRV_NAME "aml_snd_m8"

extern struct device *spdif_dev;

struct aml_audio_private_data *p_audio;

static int aml_set_bias_level(struct snd_soc_card *card,
        struct snd_soc_dapm_context *dapm, enum snd_soc_bias_level level)
{
    int ret = 0;
    struct aml_audio_private_data * p_aml_audio;
    int hp_state;

    p_aml_audio = snd_soc_card_get_drvdata(card);
	hp_state = p_aml_audio->detect_flag;

    if (p_aml_audio->bias_level == (int)level)
        return 0;

    p_aml_audio->bias_level = (int)level;

    return ret;
}

#ifdef CONFIG_PM_SLEEP
static int aml_suspend_pre(struct snd_soc_card *card)
{
    printk(KERN_DEBUG "enter %s\n", __func__);
    return 0;
}

static int aml_suspend_post(struct snd_soc_card *card)
{
    printk(KERN_DEBUG "enter %s\n", __func__);
    return 0;
}

static int aml_resume_pre(struct snd_soc_card *card)
{
    printk(KERN_DEBUG "enter %s\n", __func__);
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

static struct snd_soc_dai_link aml_codec_dai_link[] = {
    {
        .name = "AML-SPDIF",
        .stream_name = "SPDIF PCM",
        .cpu_dai_name = "aml-spdif-dai.0",
        .codec_dai_name = "dit-hifi",
        .platform_name = "aml-i2s.0",
        .codec_name = "spdif-dit.0",
    },
};

static struct snd_soc_card aml_snd_soc_card = {
    .driver_name = "SOC-Audio",
    .dai_link = &aml_codec_dai_link[0],
    .num_links = ARRAY_SIZE(aml_codec_dai_link),
    .set_bias_level = aml_set_bias_level,
#ifdef CONFIG_PM_SLEEP
	.suspend_pre    = aml_suspend_pre,
	.suspend_post   = aml_suspend_post,
	.resume_pre     = aml_resume_pre,
	.resume_post    = aml_resume_post,
#endif
};

static int aml_m8_audio_probe(struct platform_device *pdev)
{
    struct snd_soc_card *card = &aml_snd_soc_card;
    struct aml_audio_private_data *p_aml_audio;
    int ret = 0;

#ifdef CONFIG_USE_OF
    p_aml_audio = devm_kzalloc(&pdev->dev,
            sizeof(struct aml_audio_private_data), GFP_KERNEL);
    if (!p_aml_audio) {
        dev_err(&pdev->dev, "Can't allocate aml_audio_private_data\n");
        ret = -ENOMEM;
        goto err;
    }

    card->dev = &pdev->dev;
    platform_set_drvdata(pdev, card);
    snd_soc_card_set_drvdata(card, p_aml_audio);
    if (!(pdev->dev.of_node)) {
        dev_err(&pdev->dev, "Must be instantiated using device tree\n");
        ret = -EINVAL;
        goto err;
    }

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
    kfree(p_aml_audio);
    return ret;
}

static int aml_m8_audio_remove(struct platform_device *pdev)
{
    int ret = 0;
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct aml_audio_private_data *p_aml_audio;

	p_aml_audio = snd_soc_card_get_drvdata(card);
	snd_soc_unregister_card(card);
    kfree(p_aml_audio);
    return ret;
}

#ifdef CONFIG_USE_OF
static const struct of_device_id amlogic_audio_dt_match[]={
	{ .compatible = "sound_card, aml_snd_m8", },
	{},
};
#else
#define amlogic_audio_dt_match NULL
#endif

static struct platform_driver aml_m8_audio_driver = {
    .probe  = aml_m8_audio_probe,
    .remove = aml_m8_audio_remove,
    .driver = {
        .name = DRV_NAME,
        .owner = THIS_MODULE,
        .pm = &snd_soc_pm_ops,
        .of_match_table = amlogic_audio_dt_match,
    },
};

static int __init aml_m8_audio_init(void)
{
    return platform_driver_register(&aml_m8_audio_driver);
}

static void __exit aml_m8_audio_exit(void)
{
    platform_driver_unregister(&aml_m8_audio_driver);
}

#ifdef CONFIG_DEFERRED_MODULE_INIT
deferred_module_init(aml_m8_audio_init);
#else
module_init(aml_m8_audio_init);
#endif
module_exit(aml_m8_audio_exit);

/* Module information */
MODULE_AUTHOR("AMLogic, Inc.");
MODULE_DESCRIPTION("AML_M8 audio machine Asoc driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_DEVICE_TABLE(of, amlogic_audio_dt_match);

