/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2020 Intel Corporation.
 */

/*
 * This file defines data structures used in Machine Driver for Intel
 * platforms with Maxim Codecs.
 */
#ifndef __SOF_MAXIM_COMMON_H
#define __SOF_MAXIM_COMMON_H

#include <sound/soc.h>

#define MAX_98373_CODEC_DAI	"max98373-aif1"
#define MAX_98373_DEV0_NAME	"i2c-MX98373:00"
#define MAX_98373_DEV1_NAME	"i2c-MX98373:01"

extern struct snd_soc_dai_link_component max_98373_components[2];
extern struct snd_soc_ops max_98373_ops;

int max98373_spk_codec_init(struct snd_soc_pcm_runtime *rtd);
void sof_max98373_codec_conf(struct snd_soc_card *card);
#endif /* __SOF_MAXIM_COMMON_H */
