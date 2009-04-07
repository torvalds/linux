/*
 * linux/sound/soc-dai.h -- ALSA SoC Layer
 *
 * Copyright:	2005-2008 Wolfson Microelectronics. PLC.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Digital Audio Interface (DAI) API.
 */

#ifndef __LINUX_SND_SOC_DAI_H
#define __LINUX_SND_SOC_DAI_H


#include <linux/list.h>

struct snd_pcm_substream;

/*
 * DAI hardware audio formats.
 *
 * Describes the physical PCM data formating and clocking. Add new formats
 * to the end.
 */
#define SND_SOC_DAIFMT_I2S		0 /* I2S mode */
#define SND_SOC_DAIFMT_RIGHT_J		1 /* Right Justified mode */
#define SND_SOC_DAIFMT_LEFT_J		2 /* Left Justified mode */
#define SND_SOC_DAIFMT_DSP_A		3 /* L data msb after FRM LRC */
#define SND_SOC_DAIFMT_DSP_B		4 /* L data msb during FRM LRC */
#define SND_SOC_DAIFMT_AC97		5 /* AC97 */

/* left and right justified also known as MSB and LSB respectively */
#define SND_SOC_DAIFMT_MSB		SND_SOC_DAIFMT_LEFT_J
#define SND_SOC_DAIFMT_LSB		SND_SOC_DAIFMT_RIGHT_J

/*
 * DAI Clock gating.
 *
 * DAI bit clocks can be be gated (disabled) when not the DAI is not
 * sending or receiving PCM data in a frame. This can be used to save power.
 */
#define SND_SOC_DAIFMT_CONT		(0 << 4) /* continuous clock */
#define SND_SOC_DAIFMT_GATED		(1 << 4) /* clock is gated */

/*
 * DAI Left/Right Clocks.
 *
 * Specifies whether the DAI can support different samples for similtanious
 * playback and capture. This usually requires a seperate physical frame
 * clock for playback and capture.
 */
#define SND_SOC_DAIFMT_SYNC		(0 << 5) /* Tx FRM = Rx FRM */
#define SND_SOC_DAIFMT_ASYNC		(1 << 5) /* Tx FRM ~ Rx FRM */

/*
 * TDM
 *
 * Time Division Multiplexing. Allows PCM data to be multplexed with other
 * data on the DAI.
 */
#define SND_SOC_DAIFMT_TDM		(1 << 6)

/*
 * DAI hardware signal inversions.
 *
 * Specifies whether the DAI can also support inverted clocks for the specified
 * format.
 */
#define SND_SOC_DAIFMT_NB_NF		(0 << 8) /* normal bit clock + frame */
#define SND_SOC_DAIFMT_NB_IF		(1 << 8) /* normal bclk + inv frm */
#define SND_SOC_DAIFMT_IB_NF		(2 << 8) /* invert bclk + nor frm */
#define SND_SOC_DAIFMT_IB_IF		(3 << 8) /* invert bclk + frm */

/*
 * DAI hardware clock masters.
 *
 * This is wrt the codec, the inverse is true for the interface
 * i.e. if the codec is clk and frm master then the interface is
 * clk and frame slave.
 */
#define SND_SOC_DAIFMT_CBM_CFM		(0 << 12) /* codec clk & frm master */
#define SND_SOC_DAIFMT_CBS_CFM		(1 << 12) /* codec clk slave & frm master */
#define SND_SOC_DAIFMT_CBM_CFS		(2 << 12) /* codec clk master & frame slave */
#define SND_SOC_DAIFMT_CBS_CFS		(3 << 12) /* codec clk & frm slave */

#define SND_SOC_DAIFMT_FORMAT_MASK	0x000f
#define SND_SOC_DAIFMT_CLOCK_MASK	0x00f0
#define SND_SOC_DAIFMT_INV_MASK		0x0f00
#define SND_SOC_DAIFMT_MASTER_MASK	0xf000

/*
 * Master Clock Directions
 */
#define SND_SOC_CLOCK_IN		0
#define SND_SOC_CLOCK_OUT		1

struct snd_soc_dai_ops;
struct snd_soc_dai;
struct snd_ac97_bus_ops;

/* Digital Audio Interface registration */
int snd_soc_register_dai(struct snd_soc_dai *dai);
void snd_soc_unregister_dai(struct snd_soc_dai *dai);
int snd_soc_register_dais(struct snd_soc_dai *dai, size_t count);
void snd_soc_unregister_dais(struct snd_soc_dai *dai, size_t count);

/* Digital Audio Interface clocking API.*/
int snd_soc_dai_set_sysclk(struct snd_soc_dai *dai, int clk_id,
	unsigned int freq, int dir);

int snd_soc_dai_set_clkdiv(struct snd_soc_dai *dai,
	int div_id, int div);

int snd_soc_dai_set_pll(struct snd_soc_dai *dai,
	int pll_id, unsigned int freq_in, unsigned int freq_out);

/* Digital Audio interface formatting */
int snd_soc_dai_set_fmt(struct snd_soc_dai *dai, unsigned int fmt);

int snd_soc_dai_set_tdm_slot(struct snd_soc_dai *dai,
	unsigned int mask, int slots);

int snd_soc_dai_set_tristate(struct snd_soc_dai *dai, int tristate);

/* Digital Audio Interface mute */
int snd_soc_dai_digital_mute(struct snd_soc_dai *dai, int mute);

/*
 * Digital Audio Interface.
 *
 * Describes the Digital Audio Interface in terms of it's ALSA, DAI and AC97
 * operations an capabilities. Codec and platfom drivers will register a this
 * structure for every DAI they have.
 *
 * This structure covers the clocking, formating and ALSA operations for each
 * interface a
 */
struct snd_soc_dai_ops {
	/*
	 * DAI clocking configuration, all optional.
	 * Called by soc_card drivers, normally in their hw_params.
	 */
	int (*set_sysclk)(struct snd_soc_dai *dai,
		int clk_id, unsigned int freq, int dir);
	int (*set_pll)(struct snd_soc_dai *dai,
		int pll_id, unsigned int freq_in, unsigned int freq_out);
	int (*set_clkdiv)(struct snd_soc_dai *dai, int div_id, int div);

	/*
	 * DAI format configuration
	 * Called by soc_card drivers, normally in their hw_params.
	 */
	int (*set_fmt)(struct snd_soc_dai *dai, unsigned int fmt);
	int (*set_tdm_slot)(struct snd_soc_dai *dai,
		unsigned int mask, int slots);
	int (*set_tristate)(struct snd_soc_dai *dai, int tristate);

	/*
	 * DAI digital mute - optional.
	 * Called by soc-core to minimise any pops.
	 */
	int (*digital_mute)(struct snd_soc_dai *dai, int mute);

	/*
	 * ALSA PCM audio operations - all optional.
	 * Called by soc-core during audio PCM operations.
	 */
	int (*startup)(struct snd_pcm_substream *,
		struct snd_soc_dai *);
	void (*shutdown)(struct snd_pcm_substream *,
		struct snd_soc_dai *);
	int (*hw_params)(struct snd_pcm_substream *,
		struct snd_pcm_hw_params *, struct snd_soc_dai *);
	int (*hw_free)(struct snd_pcm_substream *,
		struct snd_soc_dai *);
	int (*prepare)(struct snd_pcm_substream *,
		struct snd_soc_dai *);
	int (*trigger)(struct snd_pcm_substream *, int,
		struct snd_soc_dai *);
};

/*
 * Digital Audio Interface runtime data.
 *
 * Holds runtime data for a DAI.
 */
struct snd_soc_dai {
	/* DAI description */
	char *name;
	unsigned int id;
	int ac97_control;

	struct device *dev;

	/* DAI callbacks */
	int (*probe)(struct platform_device *pdev,
		     struct snd_soc_dai *dai);
	void (*remove)(struct platform_device *pdev,
		       struct snd_soc_dai *dai);
	int (*suspend)(struct snd_soc_dai *dai);
	int (*resume)(struct snd_soc_dai *dai);

	/* ops */
	struct snd_soc_dai_ops *ops;

	/* DAI capabilities */
	struct snd_soc_pcm_stream capture;
	struct snd_soc_pcm_stream playback;
	unsigned int symmetric_rates:1;

	/* DAI runtime info */
	struct snd_pcm_runtime *runtime;
	struct snd_soc_codec *codec;
	unsigned int active;
	unsigned char pop_wait:1;
	void *dma_data;

	/* DAI private data */
	void *private_data;

	/* parent codec/platform */
	union {
		struct snd_soc_codec *codec;
		struct snd_soc_platform *platform;
	};

	struct list_head list;
};

#endif
