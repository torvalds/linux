/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2023 Intel Corporation.
 */

#ifndef __SOF_INTEL_BOARD_HELPERS_H
#define __SOF_INTEL_BOARD_HELPERS_H

#include <sound/soc.h>
#include "sof_hdmi_common.h"
#include "sof_ssp_common.h"

enum {
	SOF_LINK_NONE = 0,
	SOF_LINK_CODEC,
	SOF_LINK_DMIC01,
	SOF_LINK_DMIC16K,
	SOF_LINK_IDISP_HDMI,
	SOF_LINK_AMP,
	SOF_LINK_BT_OFFLOAD,
	SOF_LINK_HDMI_IN,
};

#define SOF_LINK_ORDER_MASK	(0xF)
#define SOF_LINK_ORDER_SHIFT	(4)

#define SOF_LINK_ORDER(k1, k2, k3, k4, k5, k6, k7) \
	((((k1) & SOF_LINK_ORDER_MASK) << (SOF_LINK_ORDER_SHIFT * 0)) | \
	 (((k2) & SOF_LINK_ORDER_MASK) << (SOF_LINK_ORDER_SHIFT * 1)) | \
	 (((k3) & SOF_LINK_ORDER_MASK) << (SOF_LINK_ORDER_SHIFT * 2)) | \
	 (((k4) & SOF_LINK_ORDER_MASK) << (SOF_LINK_ORDER_SHIFT * 3)) | \
	 (((k5) & SOF_LINK_ORDER_MASK) << (SOF_LINK_ORDER_SHIFT * 4)) | \
	 (((k6) & SOF_LINK_ORDER_MASK) << (SOF_LINK_ORDER_SHIFT * 5)) | \
	 (((k7) & SOF_LINK_ORDER_MASK) << (SOF_LINK_ORDER_SHIFT * 6)))

/*
 * sof_rt5682_private: private data for rt5682 machine driver
 *
 * @mclk: mclk clock data
 * @is_legacy_cpu: true for BYT/CHT boards
 */
struct sof_rt5682_private {
	struct clk *mclk;
	bool is_legacy_cpu;
};

/*
 * sof_card_private: common data for machine drivers
 *
 * @headset_jack: headset jack data
 * @hdmi: init data for hdmi dai link
 * @codec_type: type of headset codec
 * @amp_type: type of speaker amplifier
 * @dmic_be_num: number of Intel PCH DMIC BE link
 * @hdmi_num: number of Intel HDMI BE link
 * @ssp_codec: ssp port number of headphone BE link
 * @ssp_amp: ssp port number of speaker BE link
 * @ssp_bt: ssp port number of BT offload BE link
 * @ssp_mask_hdmi_in: ssp port mask of HDMI-IN BE link
 * @bt_offload_present: true to create BT offload BE link
 * @codec_link: pointer to headset codec dai link
 * @amp_link: pointer to speaker amplifier dai link
 * @link_order_overwrite: custom DAI link order
 * @rt5682: private data for rt5682 machine driver
 */
struct sof_card_private {
	struct snd_soc_jack headset_jack;
	struct sof_hdmi_private hdmi;

	enum sof_ssp_codec codec_type;
	enum sof_ssp_codec amp_type;

	int dmic_be_num;
	int hdmi_num;

	int ssp_codec;
	int ssp_amp;
	int ssp_bt;
	unsigned long ssp_mask_hdmi_in;

	bool bt_offload_present;

	struct snd_soc_dai_link *codec_link;
	struct snd_soc_dai_link *amp_link;

	unsigned long link_order_overwrite;

	union {
		struct sof_rt5682_private rt5682;
	};
};

enum sof_dmic_be_type {
	SOF_DMIC_01,
	SOF_DMIC_16K,
};

int sof_intel_board_card_late_probe(struct snd_soc_card *card);
int sof_intel_board_set_dai_link(struct device *dev, struct snd_soc_card *card,
				 struct sof_card_private *ctx);

int sof_intel_board_set_codec_link(struct device *dev,
				   struct snd_soc_dai_link *link, int be_id,
				   enum sof_ssp_codec codec_type, int ssp_codec);
int sof_intel_board_set_dmic_link(struct device *dev,
				  struct snd_soc_dai_link *link, int be_id,
				  enum sof_dmic_be_type be_type);
int sof_intel_board_set_intel_hdmi_link(struct device *dev,
					struct snd_soc_dai_link *link, int be_id,
					int hdmi_id, bool idisp_codec);
int sof_intel_board_set_ssp_amp_link(struct device *dev,
				     struct snd_soc_dai_link *link, int be_id,
				     enum sof_ssp_codec amp_type, int ssp_amp);
int sof_intel_board_set_bt_link(struct device *dev,
				struct snd_soc_dai_link *link, int be_id,
				int ssp_bt);
int sof_intel_board_set_hdmi_in_link(struct device *dev,
				     struct snd_soc_dai_link *link, int be_id,
				     int ssp_hdmi);

struct snd_soc_dai *get_codec_dai_by_name(struct snd_soc_pcm_runtime *rtd,
					  const char * const dai_name[], int num_dais);

#endif /* __SOF_INTEL_BOARD_HELPERS_H */
