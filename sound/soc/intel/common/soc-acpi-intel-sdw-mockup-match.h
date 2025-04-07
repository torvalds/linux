/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * soc-acpi-intel-sdw-mockup-match.h - tables and support for SoundWire
 * mockup device ACPI enumeration.
 *
 * Copyright (c) 2021, Intel Corporation.
 *
 */

#ifndef _SND_SOC_ACPI_INTEL_SDW_MOCKUP_MATCH
#define _SND_SOC_ACPI_INTEL_SDW_MOCKUP_MATCH

extern const struct snd_soc_acpi_link_adr sdw_mockup_headset_1amp_mic[];
extern const struct snd_soc_acpi_link_adr sdw_mockup_headset_2amps_mic[];
extern const struct snd_soc_acpi_link_adr sdw_mockup_mic_headset_1amp[];
extern const struct snd_soc_acpi_link_adr sdw_mockup_multi_func[];

#endif
