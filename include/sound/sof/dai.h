/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2018 Intel Corporation. All rights reserved.
 */

#ifndef __INCLUDE_SOUND_SOF_DAI_H__
#define __INCLUDE_SOUND_SOF_DAI_H__

#include <sound/sof/header.h>
#include <sound/sof/dai-intel.h>
#include <sound/sof/dai-imx.h>
#include <sound/sof/dai-amd.h>
#include <sound/sof/dai-mediatek.h>

/*
 * DAI Configuration.
 *
 * Each different DAI type will have it's own structure and IPC cmd.
 */

#define SOF_DAI_FMT_I2S		1 /**< I2S mode */
#define SOF_DAI_FMT_RIGHT_J	2 /**< Right Justified mode */
#define SOF_DAI_FMT_LEFT_J	3 /**< Left Justified mode */
#define SOF_DAI_FMT_DSP_A	4 /**< L data MSB after FRM LRC */
#define SOF_DAI_FMT_DSP_B	5 /**< L data MSB during FRM LRC */
#define SOF_DAI_FMT_PDM		6 /**< Pulse density modulation */

#define SOF_DAI_FMT_CONT	(1 << 4) /**< continuous clock */
#define SOF_DAI_FMT_GATED	(0 << 4) /**< clock is gated */

#define SOF_DAI_FMT_NB_NF	(0 << 8) /**< normal bit clock + frame */
#define SOF_DAI_FMT_NB_IF	(2 << 8) /**< normal BCLK + inv FRM */
#define SOF_DAI_FMT_IB_NF	(3 << 8) /**< invert BCLK + nor FRM */
#define SOF_DAI_FMT_IB_IF	(4 << 8) /**< invert BCLK + FRM */

#define SOF_DAI_FMT_CBP_CFP	(0 << 12) /**< codec bclk provider & frame provider */
#define SOF_DAI_FMT_CBC_CFP	(2 << 12) /**< codec bclk consumer & frame provider */
#define SOF_DAI_FMT_CBP_CFC	(3 << 12) /**< codec bclk provider & frame consumer */
#define SOF_DAI_FMT_CBC_CFC	(4 << 12) /**< codec bclk consumer & frame consumer */

/* keep old definitions for backwards compatibility */
#define SOF_DAI_FMT_CBM_CFM	SOF_DAI_FMT_CBP_CFP
#define SOF_DAI_FMT_CBS_CFM	SOF_DAI_FMT_CBC_CFP
#define SOF_DAI_FMT_CBM_CFS	SOF_DAI_FMT_CBP_CFC
#define SOF_DAI_FMT_CBS_CFS	SOF_DAI_FMT_CBC_CFC

#define SOF_DAI_FMT_FORMAT_MASK		0x000f
#define SOF_DAI_FMT_CLOCK_MASK		0x00f0
#define SOF_DAI_FMT_INV_MASK		0x0f00
#define SOF_DAI_FMT_CLOCK_PROVIDER_MASK	0xf000

/*
 * DAI_CONFIG flags. The 4 LSB bits are used for the commands, HW_PARAMS, HW_FREE and PAUSE
 * representing when the IPC is sent. The 4 MSB bits are used to add quirks along with the above
 * commands.
 */
#define SOF_DAI_CONFIG_FLAGS_CMD_MASK	0xF
#define SOF_DAI_CONFIG_FLAGS_NONE	0 /**< DAI_CONFIG sent without stage information */
#define SOF_DAI_CONFIG_FLAGS_HW_PARAMS	BIT(0) /**< DAI_CONFIG sent during hw_params stage */
#define SOF_DAI_CONFIG_FLAGS_HW_FREE	BIT(1) /**< DAI_CONFIG sent during hw_free stage */
/**< DAI_CONFIG sent during pause trigger. Only available ABI 3.20 onwards */
#define SOF_DAI_CONFIG_FLAGS_PAUSE	BIT(2)
#define SOF_DAI_CONFIG_FLAGS_QUIRK_SHIFT 4
#define SOF_DAI_CONFIG_FLAGS_QUIRK_MASK  (0xF << SOF_DAI_CONFIG_FLAGS_QUIRK_SHIFT)
/*
 * This should be used along with the SOF_DAI_CONFIG_FLAGS_HW_PARAMS to indicate that pipeline
 * stop/pause and DAI DMA stop/pause should happen in two steps. This change is only available
 * ABI 3.20 onwards.
 */
#define SOF_DAI_CONFIG_FLAGS_2_STEP_STOP BIT(0)

/** \brief Types of DAI */
enum sof_ipc_dai_type {
	SOF_DAI_INTEL_NONE = 0,		/**< None */
	SOF_DAI_INTEL_SSP,		/**< Intel SSP */
	SOF_DAI_INTEL_DMIC,		/**< Intel DMIC */
	SOF_DAI_INTEL_HDA,		/**< Intel HD/A */
	SOF_DAI_INTEL_ALH,		/**< Intel ALH  */
	SOF_DAI_IMX_SAI,		/**< i.MX SAI */
	SOF_DAI_IMX_ESAI,		/**< i.MX ESAI */
	SOF_DAI_AMD_BT,			/**< AMD ACP BT*/
	SOF_DAI_AMD_SP,			/**< AMD ACP SP */
	SOF_DAI_AMD_DMIC,		/**< AMD ACP DMIC */
	SOF_DAI_MEDIATEK_AFE,		/**< Mediatek AFE */
	SOF_DAI_AMD_HS,			/**< Amd HS */
	SOF_DAI_AMD_SP_VIRTUAL,		/**< AMD ACP SP VIRTUAL */
	SOF_DAI_AMD_HS_VIRTUAL,		/**< AMD ACP HS VIRTUAL */
	SOF_DAI_IMX_MICFIL,		/** < i.MX MICFIL PDM */
	SOF_DAI_AMD_SDW,		/**< AMD ACP SDW */
};

/* general purpose DAI configuration */
struct sof_ipc_dai_config {
	struct sof_ipc_cmd_hdr hdr;
	uint32_t type;		/**< DAI type - enum sof_ipc_dai_type */
	uint32_t dai_index;	/**< index of this type dai */

	/* physical protocol and clocking */
	uint16_t format;	/**< SOF_DAI_FMT_ */
	uint8_t group_id;	/**< group ID, 0 means no group (ABI 3.17) */
	uint8_t flags;		/**< SOF_DAI_CONFIG_FLAGS_ (ABI 3.19) */

	/* reserved for future use */
	uint32_t reserved[8];

	/* HW specific data */
	union {
		struct sof_ipc_dai_ssp_params ssp;
		struct sof_ipc_dai_dmic_params dmic;
		struct sof_ipc_dai_hda_params hda;
		struct sof_ipc_dai_alh_params alh;
		struct sof_ipc_dai_esai_params esai;
		struct sof_ipc_dai_sai_params sai;
		struct sof_ipc_dai_acp_params acpbt;
		struct sof_ipc_dai_acp_params acpsp;
		struct sof_ipc_dai_acpdmic_params acpdmic;
		struct sof_ipc_dai_acp_params acphs;
		struct sof_ipc_dai_mtk_afe_params afe;
		struct sof_ipc_dai_micfil_params micfil;
		struct sof_ipc_dai_acp_sdw_params acp_sdw;
	};
} __packed;

struct sof_dai_private_data {
	struct sof_ipc_comp_dai *comp_dai;
	struct sof_ipc_dai_config *dai_config;
};

#endif
