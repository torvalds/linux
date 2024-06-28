/* SPDX-License-Identifier: (GPL-2.0 OR MIT)
 *
 * Copyright (c) 2018 Baylibre SAS.
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 */

#ifndef _MESON_AXG_TDM_H
#define _MESON_AXG_TDM_H

#include <linux/clk.h>
#include <linux/regmap.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#define AXG_TDM_NUM_LANES	4
#define AXG_TDM_CHANNEL_MAX	128
#define AXG_TDM_RATES		(SNDRV_PCM_RATE_5512 |		\
				 SNDRV_PCM_RATE_8000_768000)
#define AXG_TDM_FORMATS		(SNDRV_PCM_FMTBIT_S8 |		\
				 SNDRV_PCM_FMTBIT_S16_LE |	\
				 SNDRV_PCM_FMTBIT_S20_LE |	\
				 SNDRV_PCM_FMTBIT_S24_LE |	\
				 SNDRV_PCM_FMTBIT_S32_LE)

struct axg_tdm_iface {
	struct clk *sclk;
	struct clk *lrclk;
	struct clk *mclk;
	unsigned long mclk_rate;

	/* format is common to all the DAIs of the iface */
	unsigned int fmt;
	unsigned int slots;
	unsigned int slot_width;

	/* For component wide symmetry */
	int rate;
};

static inline bool axg_tdm_lrclk_invert(unsigned int fmt)
{
	return ((fmt & SND_SOC_DAIFMT_FORMAT_MASK) == SND_SOC_DAIFMT_I2S) ^
		!!(fmt & (SND_SOC_DAIFMT_IB_IF | SND_SOC_DAIFMT_NB_IF));
}

static inline bool axg_tdm_sclk_invert(unsigned int fmt)
{
	return fmt & (SND_SOC_DAIFMT_IB_IF | SND_SOC_DAIFMT_IB_NF);
}

struct axg_tdm_stream {
	struct axg_tdm_iface *iface;
	struct list_head formatter_list;
	struct mutex lock;
	unsigned int channels;
	unsigned int width;
	unsigned int physical_width;
	u32 *mask;
	bool ready;

	/* For continuous clock tracking */
	bool clk_enabled;
};

struct axg_tdm_stream *axg_tdm_stream_alloc(struct axg_tdm_iface *iface);
void axg_tdm_stream_free(struct axg_tdm_stream *ts);
int axg_tdm_stream_start(struct axg_tdm_stream *ts);
void axg_tdm_stream_stop(struct axg_tdm_stream *ts);
int axg_tdm_stream_set_cont_clocks(struct axg_tdm_stream *ts,
				   unsigned int fmt);

static inline int axg_tdm_stream_reset(struct axg_tdm_stream *ts)
{
	axg_tdm_stream_stop(ts);
	return axg_tdm_stream_start(ts);
}

int axg_tdm_set_tdm_slots(struct snd_soc_dai *dai, u32 *tx_mask,
			  u32 *rx_mask, unsigned int slots,
			  unsigned int slot_width);

#endif /* _MESON_AXG_TDM_H */
