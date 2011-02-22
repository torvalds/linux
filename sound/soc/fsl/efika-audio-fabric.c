/*
 * Efika driver for the PSC of the Freescale MPC52xx
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

#include "mpc5200_dma.h"
#include "mpc5200_psc_ac97.h"
#include "../codecs/stac9766.h"

#define DRV_NAME "efika-audio-fabric"

static struct snd_soc_card card;

static struct snd_soc_dai_link efika_fabric_dai[] = {
{
	.name = "AC97",
	.stream_name = "AC97 Analog",
	.codec_dai_name = "stac9766-hifi-analog",
	.cpu_dai_name = "mpc5200-psc-ac97.0",
	.platform_name = "mpc5200-pcm-audio",
	.codec_name = "stac9766-codec",
},
{
	.name = "AC97",
	.stream_name = "AC97 IEC958",
	.codec_dai_name = "stac9766-hifi-IEC958",
	.cpu_dai_name = "mpc5200-psc-ac97.1",
	.platform_name = "mpc5200-pcm-audio",
	.codec_name = "stac9766-codec",
},
};

static __init int efika_fabric_init(void)
{
	struct platform_device *pdev;
	int rc;

	if (!of_machine_is_compatible("bplan,efika"))
		return -ENODEV;

	card.name = "Efika";
	card.dai_link = efika_fabric_dai;
	card.num_links = ARRAY_SIZE(efika_fabric_dai);


	pdev = platform_device_alloc("soc-audio", 1);
	if (!pdev) {
		pr_err("efika_fabric_init: platform_device_alloc() failed\n");
		return -ENODEV;
	}

	platform_set_drvdata(pdev, &card);

	rc = platform_device_add(pdev);
	if (rc) {
		pr_err("efika_fabric_init: platform_device_add() failed\n");
		platform_device_put(pdev);
		return -ENODEV;
	}
	return 0;
}

module_init(efika_fabric_init);


MODULE_AUTHOR("Jon Smirl <jonsmirl@gmail.com>");
MODULE_DESCRIPTION(DRV_NAME ": mpc5200 Efika fabric driver");
MODULE_LICENSE("GPL");

