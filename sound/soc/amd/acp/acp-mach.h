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

#define TDM_CHANNELS	8

#define ACP_OPS(priv, cb)	((priv)->ops.cb)

#define acp_get_drvdata(card) ((struct acp_card_drvdata *)(card)->drvdata)

enum be_id {
	HEADSET_BE_ID = 0,
	AMP_BE_ID,
	DMIC_BE_ID,
};

enum cpu_endpoints {
	NONE = 0,
	I2S_HS,
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
	NAU8825,
	NAU8821,
	MAX98388,
	ES83XX,
};

enum platform_end_point {
	RENOIR = 0,
	REMBRANDT,
};

struct acp_mach_ops {
	int (*probe)(struct snd_soc_card *card);
	int (*configure_link)(struct snd_soc_card *card, struct snd_soc_dai_link *dai_link);
	int (*configure_widgets)(struct snd_soc_card *card);
	int (*suspend_pre)(struct snd_soc_card *card);
	int (*resume_post)(struct snd_soc_card *card);
};

struct acp_card_drvdata {
	unsigned int hs_cpu_id;
	unsigned int amp_cpu_id;
	unsigned int dmic_cpu_id;
	unsigned int hs_codec_id;
	unsigned int amp_codec_id;
	unsigned int dmic_codec_id;
	unsigned int dai_fmt;
	unsigned int platform;
	struct clk *wclk;
	struct clk *bclk;
	struct acp_mach_ops ops;
	struct snd_soc_acpi_mach *acpi_mach;
	void *mach_priv;
	bool soc_mclk;
	bool tdm_mode;
};

int acp_sofdsp_dai_links_create(struct snd_soc_card *card);
int acp_legacy_dai_links_create(struct snd_soc_card *card);
extern const struct dmi_system_id acp_quirk_table[];

static inline int acp_ops_probe(struct snd_soc_card *card)
{
	int ret = 1;
	struct acp_card_drvdata *priv = acp_get_drvdata(card);

	if (ACP_OPS(priv, probe))
		ret = ACP_OPS(priv, probe)(card);
	return ret;
}

static inline int acp_ops_configure_link(struct snd_soc_card *card,
					 struct snd_soc_dai_link *dai_link)
{
	int ret = 1;
	struct acp_card_drvdata *priv = acp_get_drvdata(card);

	if (ACP_OPS(priv, configure_link))
		ret = ACP_OPS(priv, configure_link)(card, dai_link);
	return ret;
}

static inline int acp_ops_configure_widgets(struct snd_soc_card *card)
{
	int ret = 1;
	struct acp_card_drvdata *priv = acp_get_drvdata(card);

	if (ACP_OPS(priv, configure_widgets))
		ret = ACP_OPS(priv, configure_widgets)(card);
	return ret;
}

static inline int acp_ops_suspend_pre(struct snd_soc_card *card)
{
	int ret = 1;
	struct acp_card_drvdata *priv = acp_get_drvdata(card);

	if (ACP_OPS(priv, suspend_pre))
		ret = ACP_OPS(priv, suspend_pre)(card);
	return ret;
}

static inline int acp_ops_resume_post(struct snd_soc_card *card)
{
	int ret = 1;
	struct acp_card_drvdata *priv = acp_get_drvdata(card);

	if (ACP_OPS(priv, resume_post))
		ret = ACP_OPS(priv, resume_post)(card);
	return ret;
}

#endif
