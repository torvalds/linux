// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020, Linaro Limited

#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <dt-bindings/sound/qcom,q6afe.h>
#include "q6dsp-lpass-ports.h"

#define Q6AFE_TDM_PB_DAI(pre, num, did) {				\
		.playback = {						\
			.stream_name = pre" TDM"#num" Playback",	\
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |\
				SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_48000 |\
				SNDRV_PCM_RATE_176400,			\
			.formats = SNDRV_PCM_FMTBIT_S16_LE |		\
				   SNDRV_PCM_FMTBIT_S24_LE |		\
				   SNDRV_PCM_FMTBIT_S32_LE,		\
			.channels_min = 1,				\
			.channels_max = 8,				\
			.rate_min = 8000,				\
			.rate_max = 176400,				\
		},							\
		.name = #did,						\
		.id = did,						\
	}

#define Q6AFE_TDM_CAP_DAI(pre, num, did) {				\
		.capture = {						\
			.stream_name = pre" TDM"#num" Capture",		\
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |\
				SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_48000 |\
				SNDRV_PCM_RATE_176400,			\
			.formats = SNDRV_PCM_FMTBIT_S16_LE |		\
				   SNDRV_PCM_FMTBIT_S24_LE |		\
				   SNDRV_PCM_FMTBIT_S32_LE,		\
			.channels_min = 1,				\
			.channels_max = 8,				\
			.rate_min = 8000,				\
			.rate_max = 176400,				\
		},							\
		.name = #did,						\
		.id = did,						\
	}

#define Q6AFE_CDC_DMA_RX_DAI(did) {				\
		.playback = {						\
			.stream_name = #did" Playback",	\
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |\
				SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_48000 |\
				SNDRV_PCM_RATE_176400,			\
			.formats = SNDRV_PCM_FMTBIT_S16_LE |		\
				   SNDRV_PCM_FMTBIT_S24_LE |		\
				   SNDRV_PCM_FMTBIT_S32_LE,		\
			.channels_min = 1,				\
			.channels_max = 8,				\
			.rate_min = 8000,				\
			.rate_max = 176400,				\
		},							\
		.name = #did,						\
		.id = did,						\
	}

#define Q6AFE_CDC_DMA_TX_DAI(did) {				\
		.capture = {						\
			.stream_name = #did" Capture",		\
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |\
				SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_48000 |\
				SNDRV_PCM_RATE_176400,			\
			.formats = SNDRV_PCM_FMTBIT_S16_LE |		\
				   SNDRV_PCM_FMTBIT_S24_LE |		\
				   SNDRV_PCM_FMTBIT_S32_LE,		\
			.channels_min = 1,				\
			.channels_max = 8,				\
			.rate_min = 8000,				\
			.rate_max = 176400,				\
		},							\
		.name = #did,						\
		.id = did,						\
	}

#define Q6AFE_DP_RX_DAI(did) {						\
		.playback = {						\
			.stream_name = #did" Playback",			\
			.rates = SNDRV_PCM_RATE_48000 |			\
				SNDRV_PCM_RATE_96000 |			\
				SNDRV_PCM_RATE_192000,			\
			.formats = SNDRV_PCM_FMTBIT_S16_LE |		\
				   SNDRV_PCM_FMTBIT_S24_LE,		\
			.channels_min = 2,				\
			.channels_max = 8,				\
			.rate_min = 48000,				\
			.rate_max = 192000,				\
		},							\
		.name = #did,						\
		.id = did,						\
	}

static struct snd_soc_dai_driver q6dsp_audio_fe_dais[] = {
	{
		.playback = {
			.stream_name = "USB Playback",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |
					SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |
					SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
					SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 |
					SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE |
					SNDRV_PCM_FMTBIT_U16_LE | SNDRV_PCM_FMTBIT_U16_BE |
					SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S24_BE |
					SNDRV_PCM_FMTBIT_U24_LE | SNDRV_PCM_FMTBIT_U24_BE,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 192000,
		},
		.id = USB_RX,
		.name = "USB_RX",
	},
	{
		.playback = {
			.stream_name = "HDMI Playback",
			.rates = SNDRV_PCM_RATE_48000 |
				 SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 2,
			.channels_max = 8,
			.rate_max =     192000,
			.rate_min =	48000,
		},
		.id = HDMI_RX,
		.name = "HDMI",
	}, {
		.name = "SLIMBUS_0_RX",
		.id = SLIMBUS_0_RX,
		.playback = {
			.stream_name = "Slimbus Playback",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 192000,
		},
	}, {
		.name = "SLIMBUS_0_TX",
		.id = SLIMBUS_0_TX,
		.capture = {
			.stream_name = "Slimbus Capture",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 192000,
		},
	}, {
		.playback = {
			.stream_name = "Slimbus1 Playback",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
				 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 192000,
		},
		.name = "SLIMBUS_1_RX",
		.id = SLIMBUS_1_RX,
	}, {
		.name = "SLIMBUS_1_TX",
		.id = SLIMBUS_1_TX,
		.capture = {
			.stream_name = "Slimbus1 Capture",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 192000,
		},
	}, {
		.playback = {
			.stream_name = "Slimbus2 Playback",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 192000,
		},
		.name = "SLIMBUS_2_RX",
		.id = SLIMBUS_2_RX,

	}, {
		.name = "SLIMBUS_2_TX",
		.id = SLIMBUS_2_TX,
		.capture = {
			.stream_name = "Slimbus2 Capture",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 192000,
		},
	}, {
		.playback = {
			.stream_name = "Slimbus3 Playback",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
				 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 192000,
		},
		.name = "SLIMBUS_3_RX",
		.id = SLIMBUS_3_RX,

	}, {
		.name = "SLIMBUS_3_TX",
		.id = SLIMBUS_3_TX,
		.capture = {
			.stream_name = "Slimbus3 Capture",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 192000,
		},
	}, {
		.playback = {
			.stream_name = "Slimbus4 Playback",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
				 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 192000,
		},
		.name = "SLIMBUS_4_RX",
		.id = SLIMBUS_4_RX,

	}, {
		.name = "SLIMBUS_4_TX",
		.id = SLIMBUS_4_TX,
		.capture = {
			.stream_name = "Slimbus4 Capture",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 192000,
		},
	}, {
		.playback = {
			.stream_name = "Slimbus5 Playback",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
				 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 192000,
		},
		.name = "SLIMBUS_5_RX",
		.id = SLIMBUS_5_RX,

	}, {
		.name = "SLIMBUS_5_TX",
		.id = SLIMBUS_5_TX,
		.capture = {
			.stream_name = "Slimbus5 Capture",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 192000,
		},
	}, {
		.playback = {
			.stream_name = "Slimbus6 Playback",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
				 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 192000,
		},
		.name = "SLIMBUS_6_RX",
		.id = SLIMBUS_6_RX,

	}, {
		.name = "SLIMBUS_6_TX",
		.id = SLIMBUS_6_TX,
		.capture = {
			.stream_name = "Slimbus6 Capture",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 192000,
		},
	}, {
		.playback = {
			.stream_name = "Primary MI2S Playback",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.id = PRIMARY_MI2S_RX,
		.name = "PRI_MI2S_RX",
	}, {
		.capture = {
			.stream_name = "Primary MI2S Capture",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.id = PRIMARY_MI2S_TX,
		.name = "PRI_MI2S_TX",
	}, {
		.playback = {
			.stream_name = "Secondary MI2S Playback",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.name = "SEC_MI2S_RX",
		.id = SECONDARY_MI2S_RX,
	}, {
		.capture = {
			.stream_name = "Secondary MI2S Capture",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.id = SECONDARY_MI2S_TX,
		.name = "SEC_MI2S_TX",
	}, {
		.playback = {
			.stream_name = "Tertiary MI2S Playback",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.name = "TERT_MI2S_RX",
		.id = TERTIARY_MI2S_RX,
	}, {
		.capture = {
			.stream_name = "Tertiary MI2S Capture",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.id = TERTIARY_MI2S_TX,
		.name = "TERT_MI2S_TX",
	}, {
		.playback = {
			.stream_name = "Quaternary MI2S Playback",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.name = "QUAT_MI2S_RX",
		.id = QUATERNARY_MI2S_RX,
	}, {
		.capture = {
			.stream_name = "Quaternary MI2S Capture",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.id = QUATERNARY_MI2S_TX,
		.name = "QUAT_MI2S_TX",
	}, {
		.playback = {
			.stream_name = "Quinary MI2S Playback",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
			SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_96000 |
			SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min =     8000,
			.rate_max =     192000,
		},
		.id = QUINARY_MI2S_RX,
		.name = "QUIN_MI2S_RX",
	}, {
		.capture = {
			.stream_name = "Quinary MI2S Capture",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.id = QUINARY_MI2S_TX,
		.name = "QUIN_MI2S_TX",
	},
	Q6AFE_TDM_PB_DAI("Primary", 0, PRIMARY_TDM_RX_0),
	Q6AFE_TDM_PB_DAI("Primary", 1, PRIMARY_TDM_RX_1),
	Q6AFE_TDM_PB_DAI("Primary", 2, PRIMARY_TDM_RX_2),
	Q6AFE_TDM_PB_DAI("Primary", 3, PRIMARY_TDM_RX_3),
	Q6AFE_TDM_PB_DAI("Primary", 4, PRIMARY_TDM_RX_4),
	Q6AFE_TDM_PB_DAI("Primary", 5, PRIMARY_TDM_RX_5),
	Q6AFE_TDM_PB_DAI("Primary", 6, PRIMARY_TDM_RX_6),
	Q6AFE_TDM_PB_DAI("Primary", 7, PRIMARY_TDM_RX_7),
	Q6AFE_TDM_CAP_DAI("Primary", 0, PRIMARY_TDM_TX_0),
	Q6AFE_TDM_CAP_DAI("Primary", 1, PRIMARY_TDM_TX_1),
	Q6AFE_TDM_CAP_DAI("Primary", 2, PRIMARY_TDM_TX_2),
	Q6AFE_TDM_CAP_DAI("Primary", 3, PRIMARY_TDM_TX_3),
	Q6AFE_TDM_CAP_DAI("Primary", 4, PRIMARY_TDM_TX_4),
	Q6AFE_TDM_CAP_DAI("Primary", 5, PRIMARY_TDM_TX_5),
	Q6AFE_TDM_CAP_DAI("Primary", 6, PRIMARY_TDM_TX_6),
	Q6AFE_TDM_CAP_DAI("Primary", 7, PRIMARY_TDM_TX_7),
	Q6AFE_TDM_PB_DAI("Secondary", 0, SECONDARY_TDM_RX_0),
	Q6AFE_TDM_PB_DAI("Secondary", 1, SECONDARY_TDM_RX_1),
	Q6AFE_TDM_PB_DAI("Secondary", 2, SECONDARY_TDM_RX_2),
	Q6AFE_TDM_PB_DAI("Secondary", 3, SECONDARY_TDM_RX_3),
	Q6AFE_TDM_PB_DAI("Secondary", 4, SECONDARY_TDM_RX_4),
	Q6AFE_TDM_PB_DAI("Secondary", 5, SECONDARY_TDM_RX_5),
	Q6AFE_TDM_PB_DAI("Secondary", 6, SECONDARY_TDM_RX_6),
	Q6AFE_TDM_PB_DAI("Secondary", 7, SECONDARY_TDM_RX_7),
	Q6AFE_TDM_CAP_DAI("Secondary", 0, SECONDARY_TDM_TX_0),
	Q6AFE_TDM_CAP_DAI("Secondary", 1, SECONDARY_TDM_TX_1),
	Q6AFE_TDM_CAP_DAI("Secondary", 2, SECONDARY_TDM_TX_2),
	Q6AFE_TDM_CAP_DAI("Secondary", 3, SECONDARY_TDM_TX_3),
	Q6AFE_TDM_CAP_DAI("Secondary", 4, SECONDARY_TDM_TX_4),
	Q6AFE_TDM_CAP_DAI("Secondary", 5, SECONDARY_TDM_TX_5),
	Q6AFE_TDM_CAP_DAI("Secondary", 6, SECONDARY_TDM_TX_6),
	Q6AFE_TDM_CAP_DAI("Secondary", 7, SECONDARY_TDM_TX_7),
	Q6AFE_TDM_PB_DAI("Tertiary", 0, TERTIARY_TDM_RX_0),
	Q6AFE_TDM_PB_DAI("Tertiary", 1, TERTIARY_TDM_RX_1),
	Q6AFE_TDM_PB_DAI("Tertiary", 2, TERTIARY_TDM_RX_2),
	Q6AFE_TDM_PB_DAI("Tertiary", 3, TERTIARY_TDM_RX_3),
	Q6AFE_TDM_PB_DAI("Tertiary", 4, TERTIARY_TDM_RX_4),
	Q6AFE_TDM_PB_DAI("Tertiary", 5, TERTIARY_TDM_RX_5),
	Q6AFE_TDM_PB_DAI("Tertiary", 6, TERTIARY_TDM_RX_6),
	Q6AFE_TDM_PB_DAI("Tertiary", 7, TERTIARY_TDM_RX_7),
	Q6AFE_TDM_CAP_DAI("Tertiary", 0, TERTIARY_TDM_TX_0),
	Q6AFE_TDM_CAP_DAI("Tertiary", 1, TERTIARY_TDM_TX_1),
	Q6AFE_TDM_CAP_DAI("Tertiary", 2, TERTIARY_TDM_TX_2),
	Q6AFE_TDM_CAP_DAI("Tertiary", 3, TERTIARY_TDM_TX_3),
	Q6AFE_TDM_CAP_DAI("Tertiary", 4, TERTIARY_TDM_TX_4),
	Q6AFE_TDM_CAP_DAI("Tertiary", 5, TERTIARY_TDM_TX_5),
	Q6AFE_TDM_CAP_DAI("Tertiary", 6, TERTIARY_TDM_TX_6),
	Q6AFE_TDM_CAP_DAI("Tertiary", 7, TERTIARY_TDM_TX_7),
	Q6AFE_TDM_PB_DAI("Quaternary", 0, QUATERNARY_TDM_RX_0),
	Q6AFE_TDM_PB_DAI("Quaternary", 1, QUATERNARY_TDM_RX_1),
	Q6AFE_TDM_PB_DAI("Quaternary", 2, QUATERNARY_TDM_RX_2),
	Q6AFE_TDM_PB_DAI("Quaternary", 3, QUATERNARY_TDM_RX_3),
	Q6AFE_TDM_PB_DAI("Quaternary", 4, QUATERNARY_TDM_RX_4),
	Q6AFE_TDM_PB_DAI("Quaternary", 5, QUATERNARY_TDM_RX_5),
	Q6AFE_TDM_PB_DAI("Quaternary", 6, QUATERNARY_TDM_RX_6),
	Q6AFE_TDM_PB_DAI("Quaternary", 7, QUATERNARY_TDM_RX_7),
	Q6AFE_TDM_CAP_DAI("Quaternary", 0, QUATERNARY_TDM_TX_0),
	Q6AFE_TDM_CAP_DAI("Quaternary", 1, QUATERNARY_TDM_TX_1),
	Q6AFE_TDM_CAP_DAI("Quaternary", 2, QUATERNARY_TDM_TX_2),
	Q6AFE_TDM_CAP_DAI("Quaternary", 3, QUATERNARY_TDM_TX_3),
	Q6AFE_TDM_CAP_DAI("Quaternary", 4, QUATERNARY_TDM_TX_4),
	Q6AFE_TDM_CAP_DAI("Quaternary", 5, QUATERNARY_TDM_TX_5),
	Q6AFE_TDM_CAP_DAI("Quaternary", 6, QUATERNARY_TDM_TX_6),
	Q6AFE_TDM_CAP_DAI("Quaternary", 7, QUATERNARY_TDM_TX_7),
	Q6AFE_TDM_PB_DAI("Quinary", 0, QUINARY_TDM_RX_0),
	Q6AFE_TDM_PB_DAI("Quinary", 1, QUINARY_TDM_RX_1),
	Q6AFE_TDM_PB_DAI("Quinary", 2, QUINARY_TDM_RX_2),
	Q6AFE_TDM_PB_DAI("Quinary", 3, QUINARY_TDM_RX_3),
	Q6AFE_TDM_PB_DAI("Quinary", 4, QUINARY_TDM_RX_4),
	Q6AFE_TDM_PB_DAI("Quinary", 5, QUINARY_TDM_RX_5),
	Q6AFE_TDM_PB_DAI("Quinary", 6, QUINARY_TDM_RX_6),
	Q6AFE_TDM_PB_DAI("Quinary", 7, QUINARY_TDM_RX_7),
	Q6AFE_TDM_CAP_DAI("Quinary", 0, QUINARY_TDM_TX_0),
	Q6AFE_TDM_CAP_DAI("Quinary", 1, QUINARY_TDM_TX_1),
	Q6AFE_TDM_CAP_DAI("Quinary", 2, QUINARY_TDM_TX_2),
	Q6AFE_TDM_CAP_DAI("Quinary", 3, QUINARY_TDM_TX_3),
	Q6AFE_TDM_CAP_DAI("Quinary", 4, QUINARY_TDM_TX_4),
	Q6AFE_TDM_CAP_DAI("Quinary", 5, QUINARY_TDM_TX_5),
	Q6AFE_TDM_CAP_DAI("Quinary", 6, QUINARY_TDM_TX_6),
	Q6AFE_TDM_CAP_DAI("Quinary", 7, QUINARY_TDM_TX_7),
	Q6AFE_DP_RX_DAI(DISPLAY_PORT_RX_0),
	Q6AFE_DP_RX_DAI(DISPLAY_PORT_RX_1),
	Q6AFE_DP_RX_DAI(DISPLAY_PORT_RX_2),
	Q6AFE_DP_RX_DAI(DISPLAY_PORT_RX_3),
	Q6AFE_DP_RX_DAI(DISPLAY_PORT_RX_4),
	Q6AFE_DP_RX_DAI(DISPLAY_PORT_RX_5),
	Q6AFE_DP_RX_DAI(DISPLAY_PORT_RX_6),
	Q6AFE_DP_RX_DAI(DISPLAY_PORT_RX_7),
	Q6AFE_CDC_DMA_RX_DAI(WSA_CODEC_DMA_RX_0),
	Q6AFE_CDC_DMA_TX_DAI(WSA_CODEC_DMA_TX_0),
	Q6AFE_CDC_DMA_RX_DAI(WSA_CODEC_DMA_RX_1),
	Q6AFE_CDC_DMA_TX_DAI(WSA_CODEC_DMA_TX_1),
	Q6AFE_CDC_DMA_TX_DAI(WSA_CODEC_DMA_TX_2),
	Q6AFE_CDC_DMA_TX_DAI(VA_CODEC_DMA_TX_0),
	Q6AFE_CDC_DMA_TX_DAI(VA_CODEC_DMA_TX_1),
	Q6AFE_CDC_DMA_TX_DAI(VA_CODEC_DMA_TX_2),
	Q6AFE_CDC_DMA_RX_DAI(RX_CODEC_DMA_RX_0),
	Q6AFE_CDC_DMA_TX_DAI(TX_CODEC_DMA_TX_0),
	Q6AFE_CDC_DMA_RX_DAI(RX_CODEC_DMA_RX_1),
	Q6AFE_CDC_DMA_TX_DAI(TX_CODEC_DMA_TX_1),
	Q6AFE_CDC_DMA_RX_DAI(RX_CODEC_DMA_RX_2),
	Q6AFE_CDC_DMA_TX_DAI(TX_CODEC_DMA_TX_2),
	Q6AFE_CDC_DMA_RX_DAI(RX_CODEC_DMA_RX_3),
	Q6AFE_CDC_DMA_TX_DAI(TX_CODEC_DMA_TX_3),
	Q6AFE_CDC_DMA_RX_DAI(RX_CODEC_DMA_RX_4),
	Q6AFE_CDC_DMA_TX_DAI(TX_CODEC_DMA_TX_4),
	Q6AFE_CDC_DMA_RX_DAI(RX_CODEC_DMA_RX_5),
	Q6AFE_CDC_DMA_TX_DAI(TX_CODEC_DMA_TX_5),
	Q6AFE_CDC_DMA_RX_DAI(RX_CODEC_DMA_RX_6),
	Q6AFE_CDC_DMA_RX_DAI(RX_CODEC_DMA_RX_7),
};

int q6dsp_audio_ports_of_xlate_dai_name(struct snd_soc_component *component,
					const struct of_phandle_args *args,
					const char **dai_name)
{
	int id = args->args[0];
	int ret = -EINVAL;
	int i;

	for (i = 0; i  < ARRAY_SIZE(q6dsp_audio_fe_dais); i++) {
		if (q6dsp_audio_fe_dais[i].id == id) {
			*dai_name = q6dsp_audio_fe_dais[i].name;
			ret = 0;
			break;
		}
	}

	return ret;
}
EXPORT_SYMBOL_GPL(q6dsp_audio_ports_of_xlate_dai_name);

struct snd_soc_dai_driver *q6dsp_audio_ports_set_config(struct device *dev,
				struct q6dsp_audio_port_dai_driver_config *cfg,
				int *num_dais)
{
	int i;

	for (i = 0; i  < ARRAY_SIZE(q6dsp_audio_fe_dais); i++) {
		switch (q6dsp_audio_fe_dais[i].id) {
		case HDMI_RX:
		case DISPLAY_PORT_RX:
			q6dsp_audio_fe_dais[i].ops = cfg->q6hdmi_ops;
			break;
		case DISPLAY_PORT_RX_1 ... DISPLAY_PORT_RX_7:
			q6dsp_audio_fe_dais[i].ops = cfg->q6hdmi_ops;
			break;
		case SLIMBUS_0_RX ... SLIMBUS_6_TX:
			q6dsp_audio_fe_dais[i].ops = cfg->q6slim_ops;
			break;
		case QUINARY_MI2S_RX ... QUINARY_MI2S_TX:
		case PRIMARY_MI2S_RX ... QUATERNARY_MI2S_TX:
			q6dsp_audio_fe_dais[i].ops = cfg->q6i2s_ops;
			break;
		case PRIMARY_TDM_RX_0 ... QUINARY_TDM_TX_7:
			q6dsp_audio_fe_dais[i].ops = cfg->q6tdm_ops;
			break;
		case WSA_CODEC_DMA_RX_0 ... RX_CODEC_DMA_RX_7:
			q6dsp_audio_fe_dais[i].ops = cfg->q6dma_ops;
			break;
		case USB_RX:
			q6dsp_audio_fe_dais[i].ops = cfg->q6usb_ops;
			break;
		default:
			break;
		}
	}

	*num_dais = ARRAY_SIZE(q6dsp_audio_fe_dais);
	return q6dsp_audio_fe_dais;
}
EXPORT_SYMBOL_GPL(q6dsp_audio_ports_set_config);
