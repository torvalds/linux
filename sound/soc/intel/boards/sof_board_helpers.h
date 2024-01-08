/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2023 Intel Corporation.
 */

#ifndef __SOF_INTEL_BOARD_HELPERS_H
#define __SOF_INTEL_BOARD_HELPERS_H

#include <sound/soc.h>
#include "sof_hdmi_common.h"
#include "sof_ssp_common.h"

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
 * @rt5682: private data for rt5682 machine driver
 */
struct sof_card_private {
	struct snd_soc_jack headset_jack;
	struct sof_hdmi_private hdmi;

	enum sof_ssp_codec codec_type;
	enum sof_ssp_codec amp_type;

	int dmic_be_num;
	int hdmi_num;

	union {
		struct sof_rt5682_private rt5682;
	};
};

enum sof_dmic_be_type {
	SOF_DMIC_01,
	SOF_DMIC_16K,
};

int sof_intel_board_card_late_probe(struct snd_soc_card *card);

int sof_intel_board_set_dmic_link(struct device *dev,
				  struct snd_soc_dai_link *link, int be_id,
				  enum sof_dmic_be_type be_type);
int sof_intel_board_set_intel_hdmi_link(struct device *dev,
					struct snd_soc_dai_link *link, int be_id,
					int hdmi_id, bool idisp_codec);

#endif /* __SOF_INTEL_BOARD_HELPERS_H */
