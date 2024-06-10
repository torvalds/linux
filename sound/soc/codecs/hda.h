/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(c) 2021-2022 Intel Corporation
 *
 * Author: Cezary Rojewski <cezary.rojewski@intel.com>
 */

#ifndef SND_SOC_CODECS_HDA_H
#define SND_SOC_CODECS_HDA_H

#define hda_codec_is_display(codec) \
	((((codec)->core.vendor_id >> 16) & 0xFFFF) == 0x8086)

extern const struct snd_soc_dai_ops snd_soc_hda_codec_dai_ops;

extern const struct hdac_ext_bus_ops soc_hda_ext_bus_ops;
int hda_codec_probe_complete(struct hda_codec *codec);

#endif
