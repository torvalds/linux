/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2023 Intel Corporation.
 */

#ifndef __SOF_HDMI_COMMON_H
#define __SOF_HDMI_COMMON_H

#include <sound/soc.h>

#define IDISP_CODEC_MASK	0x4

/*
 * sof_hdmi_private: data for Intel HDMI dai link (idisp) initialization
 *
 * @hdmi_comp: ASoC component of idisp codec
 * @idisp_codec: true to indicate idisp codec is present
 */
struct sof_hdmi_private {
	struct snd_soc_component *hdmi_comp;
	bool idisp_codec;
};

#endif /* __SOF_HDMI_COMMON_H */
