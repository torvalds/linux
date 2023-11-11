/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020 The Linux Foundation. All rights reserved.
 *
 * lpass_hdmi.h - Definitions for the QTi LPASS HDMI
 */

#ifndef __LPASS_HDMI_H__
#define __LPASS_HDMI_H__

#include <linux/regmap.h>

#define LPASS_HDMITX_LEGACY_DISABLE		0x0
#define LPASS_HDMITX_LEGACY_ENABLE		0x1
#define LPASS_DP_AUDIO_BITWIDTH16		0x0
#define LPASS_DP_AUDIO_BITWIDTH24		0xb
#define LPASS_DATA_FORMAT_SHIFT			0x1
#define LPASS_FREQ_BIT_SHIFT			24
#define LPASS_DATA_FORMAT_LINEAR		0x0
#define LPASS_DATA_FORMAT_NON_LINEAR	0x1
#define LPASS_SAMPLING_FREQ32			0x3
#define LPASS_SAMPLING_FREQ44			0x0
#define LPASS_SAMPLING_FREQ48			0x2
#define LPASS_TX_CTL_RESET				0x1
#define LPASS_TX_CTL_CLEAR				0x0
#define LPASS_SSTREAM_ENABLE			1
#define LPASS_SSTREAM_DISABLE			0
#define LPASS_LAYOUT_SP_DEFAULT			0xf
#define LPASS_SSTREAM_DEFAULT_ENABLE	1
#define LPASS_SSTREAM_DEFAULT_DISABLE	0
#define LPASS_MUTE_ENABLE				1
#define LPASS_MUTE_DISABLE				0
#define LPASS_META_DEFAULT_VAL			0
#define HW_MODE							1
#define SW_MODE							0
#define LEGACY_LPASS_LPAIF				1
#define LEGACY_LPASS_HDMI				0
#define REPLACE_VBIT					0x1
#define LINEAR_PCM_DATA					0x0
#define NON_LINEAR_PCM_DATA				0x1
#define HDMITX_PARITY_CALC_EN			0x1
#define HDMITX_PARITY_CALC_DIS			0x0
#define LPASS_DATA_FORMAT_MASK			GENMASK(1, 1)
#define LPASS_WORDLENGTH_MASK			GENMASK(3, 0)
#define LPASS_FREQ_BIT_MASK				GENMASK(27, 24)

#define LPASS_HDMI_TX_CTL_ADDR(v)		(v->hdmi_tx_ctl_addr)
#define LPASS_HDMI_TX_LEGACY_ADDR(v)	(v->hdmi_legacy_addr)
#define LPASS_HDMI_TX_VBIT_CTL_ADDR(v)	(v->hdmi_vbit_addr)
#define LPASS_HDMI_TX_PARITY_ADDR(v)	(v->hdmi_parity_addr)
#define LPASS_HDMI_TX_DP_ADDR(v)		(v->hdmi_DP_addr)
#define LPASS_HDMI_TX_SSTREAM_ADDR(v)	(v->hdmi_sstream_addr)

#define LPASS_HDMI_TX_CH_LSB_ADDR(v, port) \
		(v->hdmi_ch_lsb_addr + v->ch_stride * (port))
#define LPASS_HDMI_TX_CH_MSB_ADDR(v, port) \
		(v->hdmi_ch_msb_addr + v->ch_stride * (port))
#define LPASS_HDMI_TX_DMA_ADDR(v, port) \
		(v->hdmi_dmactl_addr + v->hdmi_dma_stride * (port))

struct lpass_sstream_ctl {
	struct regmap_field *sstream_en;
	struct regmap_field *dma_sel;
	struct regmap_field *auto_bbit_en;
	struct regmap_field *layout;
	struct regmap_field *layout_sp;
	struct regmap_field *set_sp_on_en;
	struct regmap_field *dp_audio;
	struct regmap_field *dp_staffing_en;
	struct regmap_field *dp_sp_b_hw_en;
};

struct lpass_dp_metadata_ctl {
	struct regmap_field *mute;
	struct regmap_field *as_sdp_cc;
	struct regmap_field *as_sdp_ct;
	struct regmap_field *aif_db4;
	struct regmap_field *frequency;
	struct regmap_field *mst_index;
	struct regmap_field *dptx_index;
};

struct lpass_hdmi_tx_ctl {
	struct regmap_field *soft_reset;
	struct regmap_field *force_reset;
};

struct lpass_hdmitx_dmactl {
	struct regmap_field *use_hw_chs;
	struct regmap_field *use_hw_usr;
	struct regmap_field *hw_chs_sel;
	struct regmap_field *hw_usr_sel;
};

struct lpass_vbit_ctrl {
		struct regmap_field *replace_vbit;
		struct regmap_field *vbit_stream;
};

extern const struct snd_soc_dai_ops asoc_qcom_lpass_hdmi_dai_ops;

#endif /* __LPASS_HDMI_H__ */
