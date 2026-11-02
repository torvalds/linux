/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sn624x-sdca.h -- SN624X SDCA ALSA SoC audio driver header
 *
 * Copyright(c) 2025 Senary Semiconductor Corp.
 */

#ifndef __SN624X_H__
#define __SN624X_H__

#include <linux/mutex.h>
#include <linux/soundwire/sdw.h>
#include <linux/workqueue.h>
#include <sound/sdca_function.h>
#include <sound/soc.h>

struct sn624x_sdca_priv {
	struct regmap *regmap;
	struct snd_soc_component *component;
	struct sdw_slave *slave;
	/* Parsed DisCo functions (init tables / defaults from ACPI) */
	struct sdca_function_data *jack_func;
	struct sdca_function_data *mic_func;
	struct sdca_function_data *amp_func;
	bool hw_init;
	bool first_hw_init;
	struct snd_soc_jack *hs_jack;
	struct delayed_work jack_detect_work;
	struct mutex disable_irq_lock;
	bool disable_irq;
	/* Long-lived runtime PM ref while jack detection is enabled */
	bool jack_rpm;
	int jack_type;
	int jack_type_last;
	unsigned int scp_sdca_stat1;
	unsigned int scp_sdca_stat2;
};

/* SoundWire data port numbers (slave prop source/sink port masks) */
#define SN624X_PORT_JACK_PLAYBACK		1
#define SN624X_PORT_SPEAKER_PLAYBACK		2
#define SN624X_PORT_JACK_CAPTURE		4
#define SN624X_PORT_DMIC_CAPTURE		5

/* Order matches sn624x_sdca_dai[] .id */
enum sn624x_dai_id {
	SN624X_DAI_JACK = 0,
	SN624X_DAI_SPEAKER = 1,
	SN624X_DAI_DMIC = 2,
};

/*
 * SDCA Function Numbers (from platform ACPI / device topology).
 * Address with SDW_SDCA_CTL(fun, ent, ctl, ch) from sdw_registers.h.
 */
#define SN624X_FUNC_NUM_JACK_CODEC		0x01
#define SN624X_FUNC_NUM_MIC_ARRAY		0x02
#define SN624X_FUNC_NUM_SPEAKER_AMP		0x04

/* Entity instance IDs (from platform ACPI / SN624X topology) */
#define SN624X_SDCA_ENT_ENTITY0			0x00
#define SN624X_SDCA_ENT_CS01			0x01
#define SN624X_SDCA_ENT_CS02			0x02
#define SN624X_SDCA_ENT_PDE03			0x03
#define SN624X_SDCA_ENT_PDE04			0x04
#define SN624X_SDCA_ENT_CRU05			0x05
#define SN624X_SDCA_ENT_USER_FU10		0x10
#define SN624X_SDCA_ENT_USER_FU14		0x14
#define SN624X_SDCA_ENT_USER_FU1E		0x1e
#define SN624X_UAJ_ENT_BLOCK			0x20
#define SN624X_SDCA_ENT_GE35			0x1F
/* UAJ power domains from ssdt26.dsl AF01 entity list (PDE 34 / PDE 47) */
#define SN624X_UAJ_ENT_PDE47			SN624X_SDCA_ENT_PDE03
#define SN624X_UAJ_ENT_PDE34			SN624X_SDCA_ENT_PDE04

/* Control selectors (SDCA; same values as sound/sdca_function.h) */
/* PDE power state selectors — use standard SDCA defines from sdca_function.h */
#define SN624X_SDCA_CTL_FU_MUTE			0x01
#define SN624X_SDCA_CTL_FU_VOLUME		0x02
#define SN624X_SDCA_CTL_SAMPLE_FREQ_INDEX	0x10
#define SN624X_SDCA_CTL_CLUSTER_INDEX		0x10
#define SN624X_SDCA_CTL_FUNCTION_STATUS		0x10	/* Entity 0 */
#define SN624X_SDCA_CTL_SELECTED_MODE		0x01
#define SN624X_SDCA_CTL_DETECTED_MODE		0x02

/*
 * Control Number / channel for multi-channel FU controls (mute/volume).
 * Single-value controls (PDE power, rate, function status, …) pass 0
 * directly to SDW_SDCA_CTL — no CH_0 alias.
 */
#define SN624X_SDCA_CH_L			0x01
#define SN624X_SDCA_CH_R			0x02

/* GE35 Selected_Mode / Detected_Mode (SDCA Table 153, UAJ §5.5) */
#define SN624X_GE35_MODE_HEADPHONE		0x03
#define SN624X_GE35_MODE_HEADSET		0x05

/* Vendor UAJ SCP windows */
#define SN624X_UAJ_CTL_INPUT_DELAY		0x30
#define SN624X_UAJ_CTL_CHARGE_PUMP		0x31
#define SN624X_UAJ_CTL_JACK_DETECT		0x32

/*
 * Vendor SCP addresses used only when ACPI
 * mipi-sdca-function-initialization-table is missing (temporary fallback).
 * Remove once platform DisCo ships a complete init table.
 */
#define SN624X_UAJ_CTL_IT33_MIC_BIAS		0x40400418
#define SN624X_UAJ_CTL_IT31_MIC_BIAS		0x40400318
#define SN624X_UAJ_CTL_FU41_CH1_MUTE		0x40400A09
#define SN624X_UAJ_CTL_FU41_CH2_MUTE		0x40400A0A
#define SN624X_UAJ_CTL_FU31_CH0_MUTE		0x40400C08
#define SN624X_UAJ_CTL_FU32_CH0_MUTE		0x40400C88
#define SN624X_UAJ_CTL_FU33_CH0_MUTE		0x40400D08
#define SN624X_UAJ_CTL_FU36_CH1_MUTE		0x40400F09
#define SN624X_UAJ_CTL_FU36_CH2_MUTE		0x40400F0A
#define SN624X_UAJ_CTL_FU31_CH0_GAIN_HIGH	0x40402C58
#define SN624X_UAJ_CTL_FU31_CH0_GAIN_LOW	0x40400C58
#define SN624X_UAJ_CTL_FU32_CH0_GAIN_HIGH	0x40402CD8
#define SN624X_UAJ_CTL_FU32_CH0_GAIN_LOW	0x40400CD8
#define SN624X_UAJ_CTL_FU33_CH0_GAIN_HIGH	0x40402D58
#define SN624X_UAJ_CTL_FU33_CH0_GAIN_LOW	0x40400D58
#define SN624X_UAJ_CTL_INPUT_DELAY_TIME		0x00002124
#define SN624X_UAJ_CTL_PORTA_CHARGE_PUMP	0x00002086
#define SN624X_UAJ_DMIC_PPU11			0x40880380

/* Jack poll interval (ms) */
#define SN624X_JACK_POLL_MS			1000

/* µs settle times for headphone <-> speaker route switch during active PCM */
#define SN624X_ROUTE_AFTER_MUTE_US_MIN		5000
#define SN624X_ROUTE_AFTER_MUTE_US_MAX		8000
#define SN624X_ROUTE_AFTER_D3_US_MIN		10000
#define SN624X_ROUTE_AFTER_D3_US_MAX		15000
#define SN624X_ROUTE_BEFORE_ACTIVE_US_MIN	10000
#define SN624X_ROUTE_BEFORE_ACTIVE_US_MAX	15000

/* SDCA SampleFreqIndex (same encoding as other SDCA codecs, e.g. rt722) */
#define SN624X_SDCA_RATE_44100HZ		0x08
#define SN624X_SDCA_RATE_48000HZ		0x09
#define SN624X_SDCA_RATE_96000HZ		0x0b
#define SN624X_SDCA_RATE_192000HZ		0x0d

#define SN624X_STEREO_RATES (SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 | \
			SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_192000)
#define SN624X_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE)

int sn624x_sdca_init(struct device *dev, struct regmap *regmap,
		      struct sdw_slave *slave);

#endif /* __SN624X_H__ */
