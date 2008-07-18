/*
 * Cirrus Logic CS4270 ALSA SoC Codec Driver
 *
 * Author: Timur Tabi <timur@freescale.com>
 *
 * Copyright 2007 Freescale Semiconductor, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifndef _CS4270_H
#define _CS4270_H

/*
 * The ASoC codec DAI structure for the CS4270.  Assign this structure to
 * the .codec_dai field of your machine driver's snd_soc_dai_link structure.
 */
extern struct snd_soc_dai cs4270_dai;

/*
 * The ASoC codec device structure for the CS4270.  Assign this structure
 * to the .codec_dev field of your machine driver's snd_soc_device
 * structure.
 */
extern struct snd_soc_codec_device soc_codec_device_cs4270;

#endif
