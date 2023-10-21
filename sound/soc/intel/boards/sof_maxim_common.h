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
#include "sof_ssp_common.h"

/*
 * Maxim MAX98373
 */
#define MAX_98373_CODEC_DAI	"max98373-aif1"
#define MAX_98373_DEV0_NAME	"i2c-" MAX_98373_ACPI_HID ":00"
#define MAX_98373_DEV1_NAME	"i2c-" MAX_98373_ACPI_HID ":01"

extern struct snd_soc_dai_link_component max_98373_components[2];
extern struct snd_soc_ops max_98373_ops;
extern const struct snd_soc_dapm_route max_98373_dapm_routes[];

int max_98373_spk_codec_init(struct snd_soc_pcm_runtime *rtd);
void max_98373_set_codec_conf(struct snd_soc_card *card);
int max_98373_trigger(struct snd_pcm_substream *substream, int cmd);

/*
 * Maxim MAX98390
 */
#define MAX_98390_CODEC_DAI	"max98390-aif1"
#define MAX_98390_DEV0_NAME	"i2c-" MAX_98390_ACPI_HID ":00"
#define MAX_98390_DEV1_NAME	"i2c-" MAX_98390_ACPI_HID ":01"
#define MAX_98390_DEV2_NAME	"i2c-" MAX_98390_ACPI_HID ":02"
#define MAX_98390_DEV3_NAME	"i2c-" MAX_98390_ACPI_HID ":03"

void max_98390_dai_link(struct device *dev, struct snd_soc_dai_link *link);
void max_98390_set_codec_conf(struct device *dev, struct snd_soc_card *card);

/*
 * Maxim MAX98357A/MAX98360A
 */
#define MAX_98357A_CODEC_DAI	"HiFi"
#define MAX_98357A_DEV0_NAME	MAX_98357A_ACPI_HID ":00"
#define MAX_98360A_DEV0_NAME	MAX_98360A_ACPI_HID ":00"

void max_98357a_dai_link(struct snd_soc_dai_link *link);
void max_98360a_dai_link(struct snd_soc_dai_link *link);

#endif /* __SOF_MAXIM_COMMON_H */
