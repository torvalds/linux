/*
 * ALSA SoC SPDIF DIT driver
 *
 *  This driver is used by controllers which can operate in DIT (SPDI/F) where
 *  no codec is needed.  This file provides stub codec that can be used
 *  in these configurations. TI DaVinci Audio controller uses this driver.
 *
 * Author:      Steve Chen,  <schen@mvista.com>
 * Copyright:   (C) 2009 MontaVista Software, Inc., <source@mvista.com>
 * Copyright:   (C) 2009  Texas Instruments, India
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <sound/soc.h>
#include <sound/pcm.h>

#include "spdif_transciever.h"

MODULE_LICENSE("GPL");

#define STUB_RATES	SNDRV_PCM_RATE_8000_96000
#define STUB_FORMATS	SNDRV_PCM_FMTBIT_S16_LE

struct snd_soc_dai dit_stub_dai = {
	.name		= "DIT",
	.playback 	= {
		.stream_name	= "Playback",
		.channels_min	= 1,
		.channels_max	= 384,
		.rates		= STUB_RATES,
		.formats	= STUB_FORMATS,
	},
};
EXPORT_SYMBOL_GPL(dit_stub_dai);

static int spdif_dit_probe(struct platform_device *pdev)
{
	dit_stub_dai.dev = &pdev->dev;
	return snd_soc_register_dai(&dit_stub_dai);
}

static int spdif_dit_remove(struct platform_device *pdev)
{
	snd_soc_unregister_dai(&dit_stub_dai);
	return 0;
}

static struct platform_driver spdif_dit_driver = {
	.probe		= spdif_dit_probe,
	.remove		= spdif_dit_remove,
	.driver		= {
		.name	= "spdif-dit",
		.owner	= THIS_MODULE,
	},
};

static int __init dit_modinit(void)
{
	return platform_driver_register(&spdif_dit_driver);
}

static void __exit dit_exit(void)
{
	platform_driver_unregister(&spdif_dit_driver);
}

module_init(dit_modinit);
module_exit(dit_exit);

