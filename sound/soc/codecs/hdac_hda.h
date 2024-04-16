/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(c) 2015-18 Intel Corporation.
 */

#ifndef __HDAC_HDA_H__
#define __HDAC_HDA_H__

enum {
	HDAC_ANALOG_DAI_ID = 0,
	HDAC_DIGITAL_DAI_ID,
	HDAC_ALT_ANALOG_DAI_ID,
	HDAC_HDMI_0_DAI_ID,
	HDAC_HDMI_1_DAI_ID,
	HDAC_HDMI_2_DAI_ID,
	HDAC_HDMI_3_DAI_ID,
	HDAC_DAI_ID_NUM
};

struct hdac_hda_pcm {
	int stream_tag[2];
	unsigned int format_val[2];
};

struct hdac_hda_priv {
	struct hda_codec *codec;
	struct hdac_hda_pcm pcm[HDAC_DAI_ID_NUM];
	bool need_display_power;
};

struct hdac_ext_bus_ops *snd_soc_hdac_hda_get_ops(void);

#endif /* __HDAC_HDA_H__ */
