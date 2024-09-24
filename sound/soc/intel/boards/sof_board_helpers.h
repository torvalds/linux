/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2023 Intel Corporation.
 */

#ifndef __SOF_INTEL_BOARD_HELPERS_H
#define __SOF_INTEL_BOARD_HELPERS_H

#include <sound/soc.h>
#include <sound/soc-acpi-intel-ssp-common.h>
#include "sof_hdmi_common.h"

/*
 * Common board quirks: from bit 8 to 31, LSB 8 bits reserved for machine
 *                      drivers
 */

/* SSP port number for headphone codec: 3 bits */
#define SOF_SSP_PORT_CODEC_SHIFT		8
#define SOF_SSP_PORT_CODEC_MASK			(GENMASK(10, 8))
#define SOF_SSP_PORT_CODEC(quirk)		\
	(((quirk) << SOF_SSP_PORT_CODEC_SHIFT) & SOF_SSP_PORT_CODEC_MASK)

/* SSP port number for speaker amplifier: 3 bits */
#define SOF_SSP_PORT_AMP_SHIFT			11
#define SOF_SSP_PORT_AMP_MASK			(GENMASK(13, 11))
#define SOF_SSP_PORT_AMP(quirk)			\
	(((quirk) << SOF_SSP_PORT_AMP_SHIFT) & SOF_SSP_PORT_AMP_MASK)

/* SSP port number for BT audio offload: 3 bits */
#define SOF_SSP_PORT_BT_OFFLOAD_SHIFT		14
#define SOF_SSP_PORT_BT_OFFLOAD_MASK		(GENMASK(16, 14))
#define SOF_SSP_PORT_BT_OFFLOAD(quirk)		\
	(((quirk) << SOF_SSP_PORT_BT_OFFLOAD_SHIFT) & SOF_SSP_PORT_BT_OFFLOAD_MASK)

/* SSP port mask for HDMI capture: 6 bits */
#define SOF_SSP_MASK_HDMI_CAPTURE_SHIFT		17
#define SOF_SSP_MASK_HDMI_CAPTURE_MASK		(GENMASK(22, 17))
#define SOF_SSP_MASK_HDMI_CAPTURE(quirk)	\
	(((quirk) << SOF_SSP_MASK_HDMI_CAPTURE_SHIFT) & SOF_SSP_MASK_HDMI_CAPTURE_MASK)

/* Number of idisp HDMI BE link: 3 bits */
#define SOF_NUM_IDISP_HDMI_SHIFT		23
#define SOF_NUM_IDISP_HDMI_MASK			(GENMASK(25, 23))
#define SOF_NUM_IDISP_HDMI(quirk)		\
	(((quirk) << SOF_NUM_IDISP_HDMI_SHIFT) & SOF_NUM_IDISP_HDMI_MASK)

/* Board uses BT audio offload */
#define SOF_BT_OFFLOAD_PRESENT			BIT(26)

enum {
	SOF_LINK_NONE = 0,
	SOF_LINK_CODEC,
	SOF_LINK_DMIC01,
	SOF_LINK_DMIC16K,
	SOF_LINK_IDISP_HDMI,
	SOF_LINK_AMP,
	SOF_LINK_BT_OFFLOAD,
	SOF_LINK_HDMI_IN,
	SOF_LINK_HDA,
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

#define SOF_LINK_IDS_MASK	(0xF)
#define SOF_LINK_IDS_SHIFT	(4)

#define SOF_LINK_IDS(k1, k2, k3, k4, k5, k6, k7) \
	((((k1) & SOF_LINK_IDS_MASK) << (SOF_LINK_IDS_SHIFT * 0)) | \
	 (((k2) & SOF_LINK_IDS_MASK) << (SOF_LINK_IDS_SHIFT * 1)) | \
	 (((k3) & SOF_LINK_IDS_MASK) << (SOF_LINK_IDS_SHIFT * 2)) | \
	 (((k4) & SOF_LINK_IDS_MASK) << (SOF_LINK_IDS_SHIFT * 3)) | \
	 (((k5) & SOF_LINK_IDS_MASK) << (SOF_LINK_IDS_SHIFT * 4)) | \
	 (((k6) & SOF_LINK_IDS_MASK) << (SOF_LINK_IDS_SHIFT * 5)) | \
	 (((k7) & SOF_LINK_IDS_MASK) << (SOF_LINK_IDS_SHIFT * 6)))

/*
 * sof_da7219_private: private data for da7219 machine driver
 *
 * @mclk_en: true for mclk pin is connected
 * @pll_bypass: true for PLL bypass mode
 */
struct sof_da7219_private {
	bool mclk_en;
	bool pll_bypass;
};

/*
 * sof_rt5682_private: private data for rt5682 machine driver
 *
 * @mclk: mclk clock data
 * @is_legacy_cpu: true for BYT/CHT boards
 * @mclk_en: true for mclk pin is connected
 */
struct sof_rt5682_private {
	struct clk *mclk;
	bool is_legacy_cpu;
	bool mclk_en;
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
 * @hda_codec_present: true to create HDA codec BE links
 * @codec_link: pointer to headset codec dai link
 * @amp_link: pointer to speaker amplifier dai link
 * @link_order_overwrite: custom DAI link order
 * @link_id_overwrite: custom DAI link ID
 * @da7219: private data for da7219 machine driver
 * @rt5682: private data for rt5682 machine driver
 */
struct sof_card_private {
	struct snd_soc_jack headset_jack;
	struct sof_hdmi_private hdmi;

	enum snd_soc_acpi_intel_codec codec_type;
	enum snd_soc_acpi_intel_codec amp_type;

	int dmic_be_num;
	int hdmi_num;

	int ssp_codec;
	int ssp_amp;
	int ssp_bt;
	unsigned long ssp_mask_hdmi_in;

	bool bt_offload_present;
	bool hda_codec_present;

	struct snd_soc_dai_link *codec_link;
	struct snd_soc_dai_link *amp_link;

	unsigned long link_order_overwrite;
	/*
	 * A variable stores id for all BE DAI links, use SOF_LINK_IDS macro to
	 * build the value; use DAI link array index as id if zero.
	 */
	unsigned long link_id_overwrite;

	union {
		struct sof_da7219_private da7219;
		struct sof_rt5682_private rt5682;
	};
};

int sof_intel_board_card_late_probe(struct snd_soc_card *card);
int sof_intel_board_set_dai_link(struct device *dev, struct snd_soc_card *card,
				 struct sof_card_private *ctx);
struct sof_card_private *
sof_intel_board_get_ctx(struct device *dev, unsigned long board_quirk);

#endif /* __SOF_INTEL_BOARD_HELPERS_H */
