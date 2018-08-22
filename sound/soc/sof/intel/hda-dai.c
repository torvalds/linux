// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Authors: Keyon Jie <yang.jie@linux.intel.com>
//

#include <sound/pcm_params.h>
#include "../sof-priv.h"
#include "hda.h"

#define SKL_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE | \
	SNDRV_PCM_FMTBIT_S32_LE)

/*
 * common dai driver for skl+ platforms.
 * some products who use this DAI array only physically have a subset of
 * the DAIs, but no harm is done here by adding the whole set.
 */
struct snd_soc_dai_driver skl_dai[] = {
{
	.name = "SSP0 Pin",
	.playback = SOF_DAI_STREAM("ssp0 Tx", 1, 8,
				   SNDRV_PCM_RATE_8000_192000, SKL_FORMATS),
	.capture = SOF_DAI_STREAM("ssp0 Rx", 1, 8,
				  SNDRV_PCM_RATE_8000_192000, SKL_FORMATS),
},
{
	.name = "SSP1 Pin",
	.playback = SOF_DAI_STREAM("ssp1 Tx", 1, 8,
				   SNDRV_PCM_RATE_8000_192000, SKL_FORMATS),
	.capture = SOF_DAI_STREAM("ssp1 Rx", 1, 8,
				  SNDRV_PCM_RATE_8000_192000, SKL_FORMATS),
},
{
	.name = "SSP2 Pin",
	.playback = SOF_DAI_STREAM("ssp2 Tx", 1, 8,
				   SNDRV_PCM_RATE_8000_192000, SKL_FORMATS),
	.capture = SOF_DAI_STREAM("ssp2 Rx", 1, 8,
				  SNDRV_PCM_RATE_8000_192000, SKL_FORMATS),
},
{
	.name = "SSP3 Pin",
	.playback = SOF_DAI_STREAM("ssp3 Tx", 1, 8,
				   SNDRV_PCM_RATE_8000_192000, SKL_FORMATS),
	.capture = SOF_DAI_STREAM("ssp3 Rx", 1, 8,
				  SNDRV_PCM_RATE_8000_192000, SKL_FORMATS),
},
{
	.name = "SSP4 Pin",
	.playback = SOF_DAI_STREAM("ssp4 Tx", 1, 8,
				   SNDRV_PCM_RATE_8000_192000, SKL_FORMATS),
	.capture = SOF_DAI_STREAM("ssp4 Rx", 1, 8,
				  SNDRV_PCM_RATE_8000_192000, SKL_FORMATS),
},
{
	.name = "SSP5 Pin",
	.playback = SOF_DAI_STREAM("ssp5 Tx", 1, 8,
				   SNDRV_PCM_RATE_8000_192000, SKL_FORMATS),
	.capture = SOF_DAI_STREAM("ssp5 Rx", 1, 8,
				  SNDRV_PCM_RATE_8000_192000, SKL_FORMATS),
},
{
	.name = "DMIC01 Pin",
	.capture = SOF_DAI_STREAM("DMIC01 Rx", 1, 4,
				  SNDRV_PCM_RATE_8000_192000, SKL_FORMATS),
},
{
	.name = "DMIC16k Pin",
	.capture = SOF_DAI_STREAM("DMIC16k Rx", 1, 4,
				  SNDRV_PCM_RATE_16000, SKL_FORMATS),
},
{
	.name = "iDisp1 Pin",
	.playback = SOF_DAI_STREAM("iDisp1 Tx", 1, 8,
				   SNDRV_PCM_RATE_8000_192000, SKL_FORMATS),
},
{
	.name = "iDisp2 Pin",
	.playback = SOF_DAI_STREAM("iDisp2 Tx", 1, 8,
				   SNDRV_PCM_RATE_8000_192000, SKL_FORMATS),
},
{
	.name = "iDisp3 Pin",
	.playback = SOF_DAI_STREAM("iDisp3 Tx", 1, 8,
				   SNDRV_PCM_RATE_8000_192000, SKL_FORMATS),
},
{
	.name = "Analog Codec DAI",
	.playback = SOF_DAI_STREAM("Analog Codec Playback", 1, 16,
				   SNDRV_PCM_RATE_8000_192000, SKL_FORMATS),
	.capture = SOF_DAI_STREAM("Analog Codec Capture", 1, 16,
				   SNDRV_PCM_RATE_8000_192000, SKL_FORMATS),
},
{
	.name = "Digital Codec DAI",
	.playback = SOF_DAI_STREAM("Digital Codec Playback", 1, 16,
				   SNDRV_PCM_RATE_8000_192000, SKL_FORMATS),
	.capture = SOF_DAI_STREAM("Digital Codec Capture", 1, 16,
				   SNDRV_PCM_RATE_8000_192000, SKL_FORMATS),
},
{
	.name = "Alt Analog Codec DAI",
	.playback = SOF_DAI_STREAM("Alt Analog Codec Playback", 1, 16,
				   SNDRV_PCM_RATE_8000_192000, SKL_FORMATS),
	.capture = SOF_DAI_STREAM("Alt Analog Codec Capture", 1, 16,
				   SNDRV_PCM_RATE_8000_192000, SKL_FORMATS),
},
};
