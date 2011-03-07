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
#define SND_SOC_DAIFMT_DSP_A		3 /* L data MSB after FRM LRC */
#define SND_SOC_DAIFMT_DSP_B		4 /* L data MSB during FRM LRC */
#define SND_SOC_DAIFMT_AC97		5 /* AC97 */
#define SND_SOC_DAIFMT_PDM		6 /* Pulse density modulation */

/* left and right justified also known as MSB and LSB respectively */
#define SND_SOC_DAIFMT_MSB		SND_SOC_DAIFMT_LEFT_J
#define SND_SOC_DAIFMT_LSB		SND_SOC_DAIFMT_RIGHT_J

/*
 * DAI Clock gating.
 *
 * DAI bit clocks can be be gated (disabled) when the DAI is not
 * sending or receiving PCM data in a frame. This can be used to save power.
 */
#define SND_SOC_DAIFMT_CONT		(0 << 4) /* continuous clock */
#define SND_SOC_DAIFMT_GATED		(1 << 4) /* clock is gated */

/*
 * DAI hardware signal inversions.
 *
 * Specifies whether the DAI can also support inverted clocks for the specified
 * format.
 */
#define SND_SOC_DAIFMT_NB_NF		(0 << 8) /* normal bit clock + frame */
#define SND_SOC_DAIFMT_NB_IF		(1 << 8) /* normal BCLK + inv FRM */
#define SND_SOC_DAIFMT_IB_NF		(2 << 8) /* invert BCLK + nor FRM */
#define SND_SOC_DAIFMT_IB_IF		(3 << 8) /* invert BCLK + FRM */

/*
 * DAI hardware clock masters.
 *
 * This is wrt the codec, the inverse is true for the interface
 * i.e. if the codec is clk and FRM master then the interface is
 * clk and frame slave.
 */
#define SND_SOC_DAIFMT_CBM_CFM		(0 << 12) /* codec clk & FRM master */
#define SND_SOC_DAIFMT_CBS_CFM		(1 << 12) /* codec clk slave & FRM master */
#define SND_SOC_DAIFMT_CBM_CFS		(2 << 12) /* codec clk master & frame slave */
#define SND_SOC_DAIFMT_CBS_CFS		(3 << 12) /* codec clk & FRM slave */

#define SND_SOC_DAIFMT_FORMAT_MASK	0x000f
#define SND_SOC_DAIFMT_CLOCK_MASK	0x00f0
#define SND_SOC_DAIFMT_INV_MASK		0x0f00
#define SND_SOC_DAIFMT_MASTER_MASK	0xf000

/*
 * Master Clock Directions
 */
#define SND_SOC_CLOCK_IN		0
#define SND_SOC_CLOCK_OUT		1

#define SND_SOC_STD_AC97_FMTS (SNDRV_PCM_FMTBIT_S8 |\
			       SNDRV_PCM_FMTBIT_S16_LE |\
			       SNDRV_PCM_FMTBIT_S16_BE |\
			       SNDRV_PCM_FMTBIT_S20_3LE |\
			       SNDRV_PCM_FMTBIT_S20_3BE |\
			       SNDRV_PCM_FMTBIT_S24_3LE |\
			       SNDRV_PCM_FMTBIT_S24_3BE |\
                               SNDRV_PCM_FMTBIT_S32_LE |\
                               SNDRV_PCM_FMTBIT_S32_BE)

struct snd_soc_dai_driver;
struct snd_soc_dai;
struct snd_ac97_bus_ops;

/* Digital Audio Interface registration */
int snd_soc_register_dai(struct device *dev,
		struct snd_soc_dai_driver *dai_drv);
void snd_soc_unregister_dai(struct device *dev);
int snd_soc_register_dais(struct device *dev,
		struct snd_soc_dai_driver *dai_drv, size_t count);
void snd_soc_unregister_dais(struct device *dev, size_t count);

/* Digital Audio Interface clocking API.*/
int snd_soc_dai_set_sysclk(struct snd_soc_dai *dai, int clk_id,
	unsigned int freq, int dir);

int snd_soc_dai_set_clkdiv(struct snd_soc_dai *dai,
	int div_id, int div);

int snd_soc_dai_set_pll(struct snd_soc_dai *dai,
	int pll_id, int source, unsigned int freq_in, unsigned int freq_out);

/* Digital Audio interface formatting */
int snd_soc_dai_set_fmt(struct snd_soc_dai *dai, unsigned int fmt);

int snd_soc_dai_set_tdm_slot(struct snd_soc_dai *dai,
	unsigned int tx_mask, unsigned int rx_mask, int slots, int slot_width);

int snd_soc_dai_set_channel_map(struct snd_soc_dai *dai,
	unsigned int tx_num, unsigned int *tx_slot,
	unsigned int rx_num, unsigned int *rx_slot);

int snd_soc_dai_set_tristate(struct snd_soc_dai *dai, int tristate);

/* Digital Audio Interface mute */
int snd_soc_dai_digital_mute(struct snd_soc_dai *dai, int mute);

struct snd_soc_dai_ops {
	/*
	 * DAI clocking configuration, all optional.
	 * Called by soc_card drivers, normally in their hw_params.
	 */
	int (*set_sysclk)(struct snd_soc_dai *dai,
		int clk_id, unsigned int freq, int dir);
	int (*set_pll)(struct snd_soc_dai *dai, int pll_id, int source,
		unsigned int freq_in, unsigned int freq_out);
	int (*set_clkdiv)(struct snd_soc_dai *dai, int div_id, int div);

	/*
	 * DAI format configuration
	 * Called by soc_card drivers, normally in their hw_params.
	 */
	int (*set_fmt)(struct snd_soc_dai *dai, unsigned int fmt);
	int (*set_tdm_slot)(struct snd_soc_dai *dai,
		unsigned int tx_mask, unsigned int rx_mask,
		int slots, int slot_width);
	int (*set_channel_map)(struct snd_soc_dai *dai,
		unsigned int tx_num, unsigned int *tx_slot,
		unsigned int rx_num, unsigned int *rx_slot);
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
	/*
	 * For hardware based FIFO caused delay reporting.
	 * Optional.
	 */
	snd_pcm_sframes_t (*delay)(struct snd_pcm_substream *,
		struct snd_soc_dai *);
};

/*
 * Digital Audio Interface Driver.
 *
 * Describes the Digital Audio Interface in terms of its ALSA, DAI and AC97
 * operations and capabilities. Codec and platform drivers will register this
 * structure for every DAI they have.
 *
 * This structure covers the clocking, formating and ALSA operations for each
 * interface.
 */
struct snd_soc_dai_driver {
	/* DAI description */
	const char *name;
	unsigned int id;
	int ac97_control;

	/* DAI driver callbacks */
	int (*probe)(struct snd_soc_dai *dai);
	int (*remove)(struct snd_soc_dai *dai);
	int (*suspend)(struct snd_soc_dai *dai);
	int (*resume)(struct snd_soc_dai *dai);

	/* ops */
	const struct snd_soc_dai_ops *ops;

	/* DAI capabilities */
	struct snd_soc_pcm_stream capture;
	struct snd_soc_pcm_stream playback;
	unsigned int symmetric_rates:1;
};

/*
 * Digital Audio Interface runtime data.
 *
 * Holds runtime data for a DAI.
 */
struct snd_soc_dai {
	const char *name;
	int id;
	struct device *dev;
	void *ac97_pdata;	/* platform_data for the ac97 codec */

	/* driver ops */
	struct snd_soc_dai_driver *driver;

	/* DAI runtime info */
	unsigned int capture_active:1;		/* stream is in use */
	unsigned int playback_active:1;		/* stream is in use */
	unsigned int symmetric_rates:1;
	struct snd_pcm_runtime *runtime;
	unsigned int active;
	unsigned char pop_wait:1;
	unsigned char probed:1;

	/* DAI DMA data */
	void *playback_dma_data;
	void *capture_dma_data;

	/* parent platform/codec */
	union {
		struct snd_soc_platform *platform;
		struct snd_soc_codec *codec;
	};
	struct snd_soc_card *card;

	struct list_head list;
	struct list_head card_list;
};

static inline void *snd_soc_dai_get_dma_data(const struct snd_soc_dai *dai,
					     const struct snd_pcm_substream *ss)
{
	return (ss->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
		dai->playback_dma_data : dai->capture_dma_data;
}

static inline void snd_soc_dai_set_dma_data(struct snd_soc_dai *dai,
					    const struct snd_pcm_substream *ss,
					    void *data)
{
	if (ss->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dai->playback_dma_data = data;
	else
		dai->capture_dma_data = data;
}

static inline void snd_soc_dai_set_drvdata(struct snd_soc_dai *dai,
		void *data)
{
	dev_set_drvdata(dai->dev, data);
}

static inline void *snd_soc_dai_get_drvdata(struct snd_soc_dai *dai)
{
	return dev_get_drvdata(dai->dev);
}

#endif
