/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license. When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2021 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Author: Ajit Kumar Pandey <AjitKumar.Pandey@amd.com>
 */
#ifndef __ACP_MACH_H
#define __ACP_MACH_H

#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm_params.h>
#include <sound/soc-dapm.h>
#include <linux/input.h>
#include <linux/module.h>
#include <sound/soc.h>

enum be_id {
	HEADSET_BE_ID = 0,
	AMP_BE_ID,
	DMIC_BE_ID,
};

enum cpu_endpoints {
	NONE = 0,
	I2S_SP,
	I2S_BT,
	DMIC,
};

enum codec_endpoints {
	DUMMY = 0,
	RT5682,
	RT1019,
	MAX98360A,
	RT5682S,
};

struct acp_card_drvdata {
	unsigned int hs_cpu_id;
	unsigned int amp_cpu_id;
	unsigned int dmic_cpu_id;
	unsigned int hs_codec_id;
	unsigned int amp_codec_id;
	unsigned int dmic_codec_id;
	unsigned int dai_fmt;
	struct clk *wclk;
	struct clk *bclk;
};

int acp_sofdsp_dai_links_create(struct snd_soc_card *card);
int acp_legacy_dai_links_create(struct snd_soc_card *card);

#endif
