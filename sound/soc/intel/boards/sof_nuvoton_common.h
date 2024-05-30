/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This file defines data structures used in Machine Driver for Intel
 * platforms with Nuvoton Codecs.
 *
 * Copyright 2023 Intel Corporation.
 */
#ifndef __SOF_NUVOTON_COMMON_H
#define __SOF_NUVOTON_COMMON_H

#include <sound/soc.h>
#include <sound/soc-acpi-intel-ssp-common.h>

/*
 * Nuvoton NAU8318
 */
#define NAU8318_CODEC_DAI	"nau8315-hifi"
#define NAU8318_DEV0_NAME	"i2c-" NAU8318_ACPI_HID ":00"

void nau8318_set_dai_link(struct snd_soc_dai_link *link);

#endif /* __SOF_NUVOTON_COMMON_H */
