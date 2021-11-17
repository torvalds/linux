/* SPDX-License-Identifier: GPL-2.0
 *
 * linux/sound/soc-dai.h -- ALSA SoC Layer
 *
 * Copyright:	2005-2008 Wolfson Microelectronics. PLC.
 *
 * Digital Audio Interface (DAI) API.
 */

#ifndef __LINUX_SND_SOC_DAI_H
#define __LINUX_SND_SOC_DAI_H


#include <linux/list.h>
#include <sound/asoc.h>

struct snd_pcm_substream;
struct snd_soc_dapm_widget;
struct snd_compr_stream;

/*
 * DAI hardware audio formats.
 *
 * Describes the physical PCM data formating and clocking. Add new formats
 * to the end.
 */
#define SND_SOC_DAIFMT_I2S		SND_SOC_DAI_FORMAT_I2S
#define SND_SOC_DAIFMT_RIGHT_J		SND_SOC_DAI_FORMAT_RIGHT_J
#define SND_SOC_DAIFMT_LEFT_J		SND_SOC_DAI_FORMAT_LEFT_J
#define SND_SOC_DAIFMT_DSP_A		SND_SOC_DAI_FORMAT_DSP_A
#define SND_SOC_DAIFMT_DSP_B		SND_SOC_DAI_FORMAT_DSP_B
#define SND_SOC_DAIFMT_AC97		SND_SOC_DAI_FORMAT_AC97
#define SND_SOC_DAIFMT_PDM		SND_SOC_DAI_FORMAT_PDM

/* left and right justified also known as MSB and LSB respectively */
#define SND_SOC_DAIFMT_MSB		SND_SOC_DAIFMT_LEFT_J
#define SND_SOC_DAIFMT_LSB		SND_SOC_DAIFMT_RIGHT_J

/* Describes the possible PCM format */
/*
 * use SND_SOC_DAI_FORMAT_xx as eash shift.
 * see
 *	snd_soc_runtime_get_dai_fmt()
 */
#define SND_SOC_POSSIBLE_DAIFMT_FORMAT_SHIFT	0
#define SND_SOC_POSSIBLE_DAIFMT_FORMAT_MASK	(0xFFFF << SND_SOC_POSSIBLE_DAIFMT_FORMAT_SHIFT)
#define SND_SOC_POSSIBLE_DAIFMT_I2S		(1 << SND_SOC_DAI_FORMAT_I2S)
#define SND_SOC_POSSIBLE_DAIFMT_RIGHT_J		(1 << SND_SOC_DAI_FORMAT_RIGHT_J)
#define SND_SOC_POSSIBLE_DAIFMT_LEFT_J		(1 << SND_SOC_DAI_FORMAT_LEFT_J)
#define SND_SOC_POSSIBLE_DAIFMT_DSP_A		(1 << SND_SOC_DAI_FORMAT_DSP_A)
#define SND_SOC_POSSIBLE_DAIFMT_DSP_B		(1 << SND_SOC_DAI_FORMAT_DSP_B)
#define SND_SOC_POSSIBLE_DAIFMT_AC97		(1 << SND_SOC_DAI_FORMAT_AC97)
#define SND_SOC_POSSIBLE_DAIFMT_PDM		(1 << SND_SOC_DAI_FORMAT_PDM)

/*
 * DAI Clock gating.
 *
 * DAI bit clocks can be gated (disabled) when the DAI is not
 * sending or receiving PCM data in a frame. This can be used to save power.
 */
#define SND_SOC_DAIFMT_CONT		(1 << 4) /* continuous clock */
#define SND_SOC_DAIFMT_GATED		(0 << 4) /* clock is gated */

/* Describes the possible PCM format */
/*
 * define GATED -> CONT. GATED will be selected if both are selected.
 * see
 *	snd_soc_runtime_get_dai_fmt()
 */
#define SND_SOC_POSSIBLE_DAIFMT_CLOCK_SHIFT	16
#define SND_SOC_POSSIBLE_DAIFMT_CLOCK_MASK	(0xFFFF	<< SND_SOC_POSSIBLE_DAIFMT_CLOCK_SHIFT)
#define SND_SOC_POSSIBLE_DAIFMT_GATED		(0x1ULL	<< SND_SOC_POSSIBLE_DAIFMT_CLOCK_SHIFT)
#define SND_SOC_POSSIBLE_DAIFMT_CONT		(0x2ULL	<< SND_SOC_POSSIBLE_DAIFMT_CLOCK_SHIFT)

/*
 * DAI hardware signal polarity.
 *
 * Specifies whether the DAI can also support inverted clocks for the specified
 * format.
 *
 * BCLK:
 * - "normal" polarity means signal is available at rising edge of BCLK
 * - "inverted" polarity means signal is available at falling edge of BCLK
 *
 * FSYNC "normal" polarity depends on the frame format:
 * - I2S: frame consists of left then right channel data. Left channel starts
 *      with falling FSYNC edge, right channel starts with rising FSYNC edge.
 * - Left/Right Justified: frame consists of left then right channel data.
 *      Left channel starts with rising FSYNC edge, right channel starts with
 *      falling FSYNC edge.
 * - DSP A/B: Frame starts with rising FSYNC edge.
 * - AC97: Frame starts with rising FSYNC edge.
 *
 * "Negative" FSYNC polarity is the one opposite of "normal" polarity.
 */
#define SND_SOC_DAIFMT_NB_NF		(0 << 8) /* normal bit clock + frame */
#define SND_SOC_DAIFMT_NB_IF		(2 << 8) /* normal BCLK + inv FRM */
#define SND_SOC_DAIFMT_IB_NF		(3 << 8) /* invert BCLK + nor FRM */
#define SND_SOC_DAIFMT_IB_IF		(4 << 8) /* invert BCLK + FRM */

/* Describes the possible PCM format */
#define SND_SOC_POSSIBLE_DAIFMT_INV_SHIFT	32
#define SND_SOC_POSSIBLE_DAIFMT_INV_MASK	(0xFFFFULL << SND_SOC_POSSIBLE_DAIFMT_INV_SHIFT)
#define SND_SOC_POSSIBLE_DAIFMT_NB_NF		(0x1ULL    << SND_SOC_POSSIBLE_DAIFMT_INV_SHIFT)
#define SND_SOC_POSSIBLE_DAIFMT_NB_IF		(0x2ULL    << SND_SOC_POSSIBLE_DAIFMT_INV_SHIFT)
#define SND_SOC_POSSIBLE_DAIFMT_IB_NF		(0x4ULL    << SND_SOC_POSSIBLE_DAIFMT_INV_SHIFT)
#define SND_SOC_POSSIBLE_DAIFMT_IB_IF		(0x8ULL    << SND_SOC_POSSIBLE_DAIFMT_INV_SHIFT)

/*
 * DAI hardware clock providers/consumers
 *
 * This is wrt the codec, the inverse is true for the interface
 * i.e. if the codec is clk and FRM provider then the interface is
 * clk and frame consumer.
 */
#define SND_SOC_DAIFMT_CBP_CFP		(1 << 12) /* codec clk provider & frame provider */
#define SND_SOC_DAIFMT_CBC_CFP		(2 << 12) /* codec clk consumer & frame provider */
#define SND_SOC_DAIFMT_CBP_CFC		(3 << 12) /* codec clk provider & frame consumer */
#define SND_SOC_DAIFMT_CBC_CFC		(4 << 12) /* codec clk consumer & frame consumer */

/* previous definitions kept for backwards-compatibility, do not use in new contributions */
#define SND_SOC_DAIFMT_CBM_CFM		SND_SOC_DAIFMT_CBP_CFP
#define SND_SOC_DAIFMT_CBS_CFM		SND_SOC_DAIFMT_CBC_CFP
#define SND_SOC_DAIFMT_CBM_CFS		SND_SOC_DAIFMT_CBP_CFC
#define SND_SOC_DAIFMT_CBS_CFS		SND_SOC_DAIFMT_CBC_CFC

/* Describes the possible PCM format */
#define SND_SOC_POSSIBLE_DAIFMT_CLOCK_PROVIDER_SHIFT	48
#define SND_SOC_POSSIBLE_DAIFMT_CLOCK_PROVIDER_MASK	(0xFFFFULL << SND_SOC_POSSIBLE_DAIFMT_CLOCK_PROVIDER_SHIFT)
#define SND_SOC_POSSIBLE_DAIFMT_CBP_CFP			(0x1ULL    << SND_SOC_POSSIBLE_DAIFMT_CLOCK_PROVIDER_SHIFT)
#define SND_SOC_POSSIBLE_DAIFMT_CBC_CFP			(0x2ULL    << SND_SOC_POSSIBLE_DAIFMT_CLOCK_PROVIDER_SHIFT)
#define SND_SOC_POSSIBLE_DAIFMT_CBP_CFC			(0x4ULL    << SND_SOC_POSSIBLE_DAIFMT_CLOCK_PROVIDER_SHIFT)
#define SND_SOC_POSSIBLE_DAIFMT_CBC_CFC			(0x8ULL    << SND_SOC_POSSIBLE_DAIFMT_CLOCK_PROVIDER_SHIFT)

#define SND_SOC_DAIFMT_FORMAT_MASK		0x000f
#define SND_SOC_DAIFMT_CLOCK_MASK		0x00f0
#define SND_SOC_DAIFMT_INV_MASK			0x0f00
#define SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK	0xf000

#define SND_SOC_DAIFMT_MASTER_MASK	SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK

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
			       SNDRV_PCM_FMTBIT_S20_LE |\
			       SNDRV_PCM_FMTBIT_S20_BE |\
			       SNDRV_PCM_FMTBIT_S24_3LE |\
			       SNDRV_PCM_FMTBIT_S24_3BE |\
                               SNDRV_PCM_FMTBIT_S32_LE |\
                               SNDRV_PCM_FMTBIT_S32_BE)

struct snd_soc_dai_driver;
struct snd_soc_dai;
struct snd_ac97_bus_ops;

/* Digital Audio Interface clocking API.*/
int snd_soc_dai_set_sysclk(struct snd_soc_dai *dai, int clk_id,
	unsigned int freq, int dir);

int snd_soc_dai_set_clkdiv(struct snd_soc_dai *dai,
	int div_id, int div);

int snd_soc_dai_set_pll(struct snd_soc_dai *dai,
	int pll_id, int source, unsigned int freq_in, unsigned int freq_out);

int snd_soc_dai_set_bclk_ratio(struct snd_soc_dai *dai, unsigned int ratio);

/* Digital Audio interface formatting */
int snd_soc_dai_get_fmt_max_priority(struct snd_soc_pcm_runtime *rtd);
u64 snd_soc_dai_get_fmt(struct snd_soc_dai *dai, int priority);
int snd_soc_dai_set_fmt(struct snd_soc_dai *dai, unsigned int fmt);

int snd_soc_dai_set_tdm_slot(struct snd_soc_dai *dai,
	unsigned int tx_mask, unsigned int rx_mask, int slots, int slot_width);

int snd_soc_dai_set_channel_map(struct snd_soc_dai *dai,
	unsigned int tx_num, unsigned int *tx_slot,
	unsigned int rx_num, unsigned int *rx_slot);

int snd_soc_dai_set_tristate(struct snd_soc_dai *dai, int tristate);

/* Digital Audio Interface mute */
int snd_soc_dai_digital_mute(struct snd_soc_dai *dai, int mute,
			     int direction);


int snd_soc_dai_get_channel_map(struct snd_soc_dai *dai,
		unsigned int *tx_num, unsigned int *tx_slot,
		unsigned int *rx_num, unsigned int *rx_slot);

int snd_soc_dai_is_dummy(struct snd_soc_dai *dai);

int snd_soc_dai_hw_params(struct snd_soc_dai *dai,
			  struct snd_pcm_substream *substream,
			  struct snd_pcm_hw_params *params);
void snd_soc_dai_hw_free(struct snd_soc_dai *dai,
			 struct snd_pcm_substream *substream,
			 int rollback);
int snd_soc_dai_startup(struct snd_soc_dai *dai,
			struct snd_pcm_substream *substream);
void snd_soc_dai_shutdown(struct snd_soc_dai *dai,
			  struct snd_pcm_substream *substream, int rollback);
snd_pcm_sframes_t snd_soc_dai_delay(struct snd_soc_dai *dai,
				    struct snd_pcm_substream *substream);
void snd_soc_dai_suspend(struct snd_soc_dai *dai);
void snd_soc_dai_resume(struct snd_soc_dai *dai);
int snd_soc_dai_compress_new(struct snd_soc_dai *dai,
			     struct snd_soc_pcm_runtime *rtd, int num);
bool snd_soc_dai_stream_valid(struct snd_soc_dai *dai, int stream);
void snd_soc_dai_link_set_capabilities(struct snd_soc_dai_link *dai_link);
void snd_soc_dai_action(struct snd_soc_dai *dai,
			int stream, int action);
static inline void snd_soc_dai_activate(struct snd_soc_dai *dai,
					int stream)
{
	snd_soc_dai_action(dai, stream,  1);
}
static inline void snd_soc_dai_deactivate(struct snd_soc_dai *dai,
					  int stream)
{
	snd_soc_dai_action(dai, stream, -1);
}
int snd_soc_dai_active(struct snd_soc_dai *dai);

int snd_soc_pcm_dai_probe(struct snd_soc_pcm_runtime *rtd, int order);
int snd_soc_pcm_dai_remove(struct snd_soc_pcm_runtime *rtd, int order);
int snd_soc_pcm_dai_new(struct snd_soc_pcm_runtime *rtd);
int snd_soc_pcm_dai_prepare(struct snd_pcm_substream *substream);
int snd_soc_pcm_dai_trigger(struct snd_pcm_substream *substream, int cmd,
			    int rollback);
int snd_soc_pcm_dai_bespoke_trigger(struct snd_pcm_substream *substream,
				    int cmd);

int snd_soc_dai_compr_startup(struct snd_soc_dai *dai,
			      struct snd_compr_stream *cstream);
void snd_soc_dai_compr_shutdown(struct snd_soc_dai *dai,
				struct snd_compr_stream *cstream,
				int rollback);
int snd_soc_dai_compr_trigger(struct snd_soc_dai *dai,
			      struct snd_compr_stream *cstream, int cmd);
int snd_soc_dai_compr_set_params(struct snd_soc_dai *dai,
				 struct snd_compr_stream *cstream,
				 struct snd_compr_params *params);
int snd_soc_dai_compr_get_params(struct snd_soc_dai *dai,
				 struct snd_compr_stream *cstream,
				 struct snd_codec *params);
int snd_soc_dai_compr_ack(struct snd_soc_dai *dai,
			  struct snd_compr_stream *cstream,
			  size_t bytes);
int snd_soc_dai_compr_pointer(struct snd_soc_dai *dai,
			      struct snd_compr_stream *cstream,
			      struct snd_compr_tstamp *tstamp);
int snd_soc_dai_compr_set_metadata(struct snd_soc_dai *dai,
				   struct snd_compr_stream *cstream,
				   struct snd_compr_metadata *metadata);
int snd_soc_dai_compr_get_metadata(struct snd_soc_dai *dai,
				   struct snd_compr_stream *cstream,
				   struct snd_compr_metadata *metadata);

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
	int (*set_bclk_ratio)(struct snd_soc_dai *dai, unsigned int ratio);

	/*
	 * DAI format configuration
	 * Called by soc_card drivers, normally in their hw_params.
	 */
	int (*set_fmt)(struct snd_soc_dai *dai, unsigned int fmt);
	int (*xlate_tdm_slot_mask)(unsigned int slots,
		unsigned int *tx_mask, unsigned int *rx_mask);
	int (*set_tdm_slot)(struct snd_soc_dai *dai,
		unsigned int tx_mask, unsigned int rx_mask,
		int slots, int slot_width);
	int (*set_channel_map)(struct snd_soc_dai *dai,
		unsigned int tx_num, unsigned int *tx_slot,
		unsigned int rx_num, unsigned int *rx_slot);
	int (*get_channel_map)(struct snd_soc_dai *dai,
			unsigned int *tx_num, unsigned int *tx_slot,
			unsigned int *rx_num, unsigned int *rx_slot);
	int (*set_tristate)(struct snd_soc_dai *dai, int tristate);

	int (*set_sdw_stream)(struct snd_soc_dai *dai,
			void *stream, int direction);
	void *(*get_sdw_stream)(struct snd_soc_dai *dai, int direction);

	/*
	 * DAI digital mute - optional.
	 * Called by soc-core to minimise any pops.
	 */
	int (*mute_stream)(struct snd_soc_dai *dai, int mute, int stream);

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
	/*
	 * NOTE: Commands passed to the trigger function are not necessarily
	 * compatible with the current state of the dai. For example this
	 * sequence of commands is possible: START STOP STOP.
	 * So do not unconditionally use refcounting functions in the trigger
	 * function, e.g. clk_enable/disable.
	 */
	int (*trigger)(struct snd_pcm_substream *, int,
		struct snd_soc_dai *);
	int (*bespoke_trigger)(struct snd_pcm_substream *, int,
		struct snd_soc_dai *);
	/*
	 * For hardware based FIFO caused delay reporting.
	 * Optional.
	 */
	snd_pcm_sframes_t (*delay)(struct snd_pcm_substream *,
		struct snd_soc_dai *);

	/*
	 * Format list for auto selection.
	 * Format will be increased if priority format was
	 * not selected.
	 * see
	 *	snd_soc_dai_get_fmt()
	 */
	u64 *auto_selectable_formats;
	int num_auto_selectable_formats;

	/* bit field */
	unsigned int no_capture_mute:1;
};

struct snd_soc_cdai_ops {
	/*
	 * for compress ops
	 */
	int (*startup)(struct snd_compr_stream *,
			struct snd_soc_dai *);
	int (*shutdown)(struct snd_compr_stream *,
			struct snd_soc_dai *);
	int (*set_params)(struct snd_compr_stream *,
			struct snd_compr_params *, struct snd_soc_dai *);
	int (*get_params)(struct snd_compr_stream *,
			struct snd_codec *, struct snd_soc_dai *);
	int (*set_metadata)(struct snd_compr_stream *,
			struct snd_compr_metadata *, struct snd_soc_dai *);
	int (*get_metadata)(struct snd_compr_stream *,
			struct snd_compr_metadata *, struct snd_soc_dai *);
	int (*trigger)(struct snd_compr_stream *, int,
			struct snd_soc_dai *);
	int (*pointer)(struct snd_compr_stream *,
			struct snd_compr_tstamp *, struct snd_soc_dai *);
	int (*ack)(struct snd_compr_stream *, size_t,
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
	unsigned int base;
	struct snd_soc_dobj dobj;

	/* DAI driver callbacks */
	int (*probe)(struct snd_soc_dai *dai);
	int (*remove)(struct snd_soc_dai *dai);
	/* compress dai */
	int (*compress_new)(struct snd_soc_pcm_runtime *rtd, int num);
	/* Optional Callback used at pcm creation*/
	int (*pcm_new)(struct snd_soc_pcm_runtime *rtd,
		       struct snd_soc_dai *dai);

	/* ops */
	const struct snd_soc_dai_ops *ops;
	const struct snd_soc_cdai_ops *cops;

	/* DAI capabilities */
	struct snd_soc_pcm_stream capture;
	struct snd_soc_pcm_stream playback;
	unsigned int symmetric_rate:1;
	unsigned int symmetric_channels:1;
	unsigned int symmetric_sample_bits:1;

	/* probe ordering - for components with runtime dependencies */
	int probe_order;
	int remove_order;
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

	/* driver ops */
	struct snd_soc_dai_driver *driver;

	/* DAI runtime info */
	unsigned int stream_active[SNDRV_PCM_STREAM_LAST + 1]; /* usage count */

	struct snd_soc_dapm_widget *playback_widget;
	struct snd_soc_dapm_widget *capture_widget;

	/* DAI DMA data */
	void *playback_dma_data;
	void *capture_dma_data;

	/* Symmetry data - only valid if symmetry is being enforced */
	unsigned int rate;
	unsigned int channels;
	unsigned int sample_bits;

	/* parent platform/codec */
	struct snd_soc_component *component;

	/* CODEC TDM slot masks and params (for fixup) */
	unsigned int tx_mask;
	unsigned int rx_mask;

	struct list_head list;

	/* function mark */
	struct snd_pcm_substream *mark_startup;
	struct snd_pcm_substream *mark_hw_params;
	struct snd_pcm_substream *mark_trigger;
	struct snd_compr_stream  *mark_compr_startup;

	/* bit field */
	unsigned int probed:1;
};

static inline struct snd_soc_pcm_stream *
snd_soc_dai_get_pcm_stream(const struct snd_soc_dai *dai, int stream)
{
	return (stream == SNDRV_PCM_STREAM_PLAYBACK) ?
		&dai->driver->playback : &dai->driver->capture;
}

static inline
struct snd_soc_dapm_widget *snd_soc_dai_get_widget(
	struct snd_soc_dai *dai, int stream)
{
	return (stream == SNDRV_PCM_STREAM_PLAYBACK) ?
		dai->playback_widget : dai->capture_widget;
}

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

static inline void snd_soc_dai_init_dma_data(struct snd_soc_dai *dai,
					     void *playback, void *capture)
{
	dai->playback_dma_data = playback;
	dai->capture_dma_data = capture;
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

/**
 * snd_soc_dai_set_sdw_stream() - Configures a DAI for SDW stream operation
 * @dai: DAI
 * @stream: STREAM
 * @direction: Stream direction(Playback/Capture)
 * SoundWire subsystem doesn't have a notion of direction and we reuse
 * the ASoC stream direction to configure sink/source ports.
 * Playback maps to source ports and Capture for sink ports.
 *
 * This should be invoked with NULL to clear the stream set previously.
 * Returns 0 on success, a negative error code otherwise.
 */
static inline int snd_soc_dai_set_sdw_stream(struct snd_soc_dai *dai,
				void *stream, int direction)
{
	if (dai->driver->ops->set_sdw_stream)
		return dai->driver->ops->set_sdw_stream(dai, stream, direction);
	else
		return -ENOTSUPP;
}

/**
 * snd_soc_dai_get_sdw_stream() - Retrieves SDW stream from DAI
 * @dai: DAI
 * @direction: Stream direction(Playback/Capture)
 *
 * This routine only retrieves that was previously configured
 * with snd_soc_dai_get_sdw_stream()
 *
 * Returns pointer to stream or an ERR_PTR value, e.g.
 * ERR_PTR(-ENOTSUPP) if callback is not supported;
 */
static inline void *snd_soc_dai_get_sdw_stream(struct snd_soc_dai *dai,
					       int direction)
{
	if (dai->driver->ops->get_sdw_stream)
		return dai->driver->ops->get_sdw_stream(dai, direction);
	else
		return ERR_PTR(-ENOTSUPP);
}

static inline unsigned int
snd_soc_dai_stream_active(struct snd_soc_dai *dai, int stream)
{
	return dai->stream_active[stream];
}

#endif
