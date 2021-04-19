/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * hdmi-codec.h - HDMI Codec driver API
 *
 * Copyright (C) 2014 Texas Instruments Incorporated - https://www.ti.com
 *
 * Author: Jyri Sarha <jsarha@ti.com>
 */

#ifndef __HDMI_CODEC_H__
#define __HDMI_CODEC_H__

#include <linux/of_graph.h>
#include <linux/hdmi.h>
#include <drm/drm_edid.h>
#include <sound/asoundef.h>
#include <sound/soc.h>
#include <uapi/sound/asound.h>

/*
 * Protocol between ASoC cpu-dai and HDMI-encoder
 */
struct hdmi_codec_daifmt {
	enum {
		HDMI_I2S,
		HDMI_RIGHT_J,
		HDMI_LEFT_J,
		HDMI_DSP_A,
		HDMI_DSP_B,
		HDMI_AC97,
		HDMI_SPDIF,
	} fmt;
	unsigned int bit_clk_inv:1;
	unsigned int frame_clk_inv:1;
	unsigned int bit_clk_master:1;
	unsigned int frame_clk_master:1;
	/* bit_fmt could be standard PCM format or
	 * IEC958 encoded format. ALSA IEC958 plugin will pass
	 * IEC958_SUBFRAME format to the underneath driver.
	 */
	snd_pcm_format_t bit_fmt;
};

/*
 * HDMI audio parameters
 */
struct hdmi_codec_params {
	struct hdmi_audio_infoframe cea;
	struct snd_aes_iec958 iec;
	int sample_rate;
	int sample_width;
	int channels;
};

typedef void (*hdmi_codec_plugged_cb)(struct device *dev,
				      bool plugged);

struct hdmi_codec_pdata;
struct hdmi_codec_ops {
	/*
	 * Called when ASoC starts an audio stream setup.
	 * Optional
	 */
	int (*audio_startup)(struct device *dev, void *data);

	/*
	 * Configures HDMI-encoder for audio stream.
	 * Mandatory
	 */
	int (*hw_params)(struct device *dev, void *data,
			 struct hdmi_codec_daifmt *fmt,
			 struct hdmi_codec_params *hparms);

	/*
	 * Shuts down the audio stream.
	 * Mandatory
	 */
	void (*audio_shutdown)(struct device *dev, void *data);

	/*
	 * Mute/unmute HDMI audio stream.
	 * Optional
	 */
	int (*mute_stream)(struct device *dev, void *data,
			   bool enable, int direction);

	/*
	 * Provides EDID-Like-Data from connected HDMI device.
	 * Optional
	 */
	int (*get_eld)(struct device *dev, void *data,
		       uint8_t *buf, size_t len);

	/*
	 * Getting DAI ID
	 * Optional
	 */
	int (*get_dai_id)(struct snd_soc_component *comment,
			  struct device_node *endpoint);

	/*
	 * Hook callback function to handle connector plug event.
	 * Optional
	 */
	int (*hook_plugged_cb)(struct device *dev, void *data,
			       hdmi_codec_plugged_cb fn,
			       struct device *codec_dev);

	/* bit field */
	unsigned int no_capture_mute:1;
};

/* HDMI codec initalization data */
struct hdmi_codec_pdata {
	const struct hdmi_codec_ops *ops;
	uint i2s:1;
	uint spdif:1;
	int max_i2s_channels;
	void *data;
};

struct snd_soc_component;
struct snd_soc_jack;

#define HDMI_CODEC_DRV_NAME "hdmi-audio-codec"

#endif /* __HDMI_CODEC_H__ */
