/*
 * Phytec pcm030 driver for the PSC of the Freescale MPC52xx
 * configured as AC97 interface
 *
 * Copyright 2008 Jon Smirl, Digispeaker
 * Author: Jon Smirl <jonsmirl@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/dma-mapping.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/soc-of-simple.h>

#include "mpc5200_dma.h"
#include "mpc5200_psc_ac97.h"
#include "../codecs/wm9712.h"

#define DRV_NAME "pcm030-audio-fabric"

static struct snd_soc_device device;
static struct snd_soc_card card;

static struct snd_soc_dai_link pcm030_fabric_dai[] = {
{
	.name = "AC97",
	.stream_name = "AC97 Analog",
	.codec_dai = &wm9712_dai[WM9712_DAI_AC97_HIFI],
	.cpu_dai = &psc_ac97_dai[MPC5200_AC97_NORMAL],
},
{
	.name = "AC97",
	.stream_name = "AC97 IEC958",
	.codec_dai = &wm9712_dai[WM9712_DAI_AC97_AUX],
	.cpu_dai = &psc_ac97_dai[MPC5200_AC97_SPDIF],
},
};

static __init int pcm030_fabric_init(void)
{
	struct platform_device *pdev;
	int rc;

	if (!machine_is_compatible("phytec,pcm030"))
		return -ENODEV;

	card.platform = &mpc5200_audio_dma_platform;
	card.name = "pcm030";
	card.dai_link = pcm030_fabric_dai;
	card.num_links = ARRAY_SIZE(pcm030_fabric_dai);

	device.card = &card;
	device.codec_dev = &soc_codec_dev_wm9712;

	pdev = platform_device_alloc("soc-audio", 1);
	if (!pdev) {
		pr_err("pcm030_fabric_init: platform_device_alloc() failed\n");
		return -ENODEV;
	}

	platform_set_drvdata(pdev, &device);
	device.dev = &pdev->dev;

	rc = platform_device_add(pdev);
	if (rc) {
		pr_err("pcm030_fabric_init: platform_device_add() failed\n");
		return -ENODEV;
	}
	return 0;
}

module_init(pcm030_fabric_init);


MODULE_AUTHOR("Jon Smirl <jonsmirl@gmail.com>");
MODULE_DESCRIPTION(DRV_NAME ": mpc5200 pcm030 fabric driver");
MODULE_LICENSE("GPL");

