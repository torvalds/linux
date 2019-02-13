/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(c) 2015-18 Intel Corporation.
 */

#ifndef __HDAC_HDA_H__
#define __HDAC_HDA_H__

struct hdac_hda_pcm {
	int stream_tag[2];
	unsigned int format_val[2];
};

struct hdac_hda_priv {
	struct hda_codec codec;
	struct hdac_hda_pcm pcm[2];
};

#define hdac_to_hda_priv(_hdac) \
			container_of(_hdac, struct hdac_hda_priv, codec.core)
#define hdac_to_hda_codec(_hdac) container_of(_hdac, struct hda_codec, core)

struct hdac_ext_bus_ops *snd_soc_hdac_hda_get_ops(void);

#endif /* __HDAC_HDA_H__ */
