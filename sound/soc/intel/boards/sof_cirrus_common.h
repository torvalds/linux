/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This file defines data structures used in Machine Driver for Intel
 * platforms with Cirrus Logic Codecs.
 *
 * Copyright 2022 Intel Corporation.
 */
#ifndef __SOF_CIRRUS_COMMON_H
#define __SOF_CIRRUS_COMMON_H

#include <sound/soc.h>
#include <sound/soc-acpi-intel-ssp-common.h>

/*
 * Cirrus Logic CS35L41/CS35L53
 */
#define CS35L41_CODEC_DAI	"cs35l41-pcm"
#define CS35L41_DEV0_NAME	"i2c-" CS35L41_ACPI_HID ":00"
#define CS35L41_DEV1_NAME	"i2c-" CS35L41_ACPI_HID ":01"
#define CS35L41_DEV2_NAME	"i2c-" CS35L41_ACPI_HID ":02"
#define CS35L41_DEV3_NAME	"i2c-" CS35L41_ACPI_HID ":03"

void cs35l41_set_dai_link(struct snd_soc_dai_link *link);
void cs35l41_set_codec_conf(struct snd_soc_card *card);

#endif /* __SOF_CIRRUS_COMMON_H */
