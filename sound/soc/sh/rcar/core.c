// SPDX-License-Identifier: GPL-2.0
//
// Renesas R-Car SRU/SCU/SSIU/SSI support
//
// Copyright (C) 2013 Renesas Solutions Corp.
// Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
//
// Based on fsi.c
// Kuninori Morimoto <morimoto.kuninori@renesas.com>

/*
 * Renesas R-Car sound device structure
 *
 * Gen1
 *
 * SRU		: Sound Routing Unit
 *  - SRC	: Sampling Rate Converter
 *  - CMD
 *    - CTU	: Channel Count Conversion Unit
 *    - MIX	: Mixer
 *    - DVC	: Digital Volume and Mute Function
 *  - SSI	: Serial Sound Interface
 *
 * Gen2
 *
 * SCU		: Sampling Rate Converter Unit
 *  - SRC	: Sampling Rate Converter
 *  - CMD
 *   - CTU	: Channel Count Conversion Unit
 *   - MIX	: Mixer
 *   - DVC	: Digital Volume and Mute Function
 * SSIU		: Serial Sound Interface Unit
 *  - SSI	: Serial Sound Interface
 */

/*
 *	driver data Image
 *
 * rsnd_priv
 *   |
 *   | ** this depends on Gen1/Gen2
 *   |
 *   +- gen
 *   |
 *   | ** these depend on data path
 *   | ** gen and platform data control it
 *   |
 *   +- rdai[0]
 *   |   |		 sru     ssiu      ssi
 *   |   +- playback -> [mod] -> [mod] -> [mod] -> ...
 *   |   |
 *   |   |		 sru     ssiu      ssi
 *   |   +- capture  -> [mod] -> [mod] -> [mod] -> ...
 *   |
 *   +- rdai[1]
 *   |   |		 sru     ssiu      ssi
 *   |   +- playback -> [mod] -> [mod] -> [mod] -> ...
 *   |   |
 *   |   |		 sru     ssiu      ssi
 *   |   +- capture  -> [mod] -> [mod] -> [mod] -> ...
 *   ...
 *   |
 *   | ** these control ssi
 *   |
 *   +- ssi
 *   |  |
 *   |  +- ssi[0]
 *   |  +- ssi[1]
 *   |  +- ssi[2]
 *   |  ...
 *   |
 *   | ** these control src
 *   |
 *   +- src
 *      |
 *      +- src[0]
 *      +- src[1]
 *      +- src[2]
 *      ...
 *
 *
 * for_each_rsnd_dai(xx, priv, xx)
 *  rdai[0] => rdai[1] => rdai[2] => ...
 *
 * for_each_rsnd_mod(xx, rdai, xx)
 *  [mod] => [mod] => [mod] => ...
 *
 * rsnd_dai_call(xxx, fn )
 *  [mod]->fn() -> [mod]->fn() -> [mod]->fn()...
 *
 */

/*
 * you can enable below define if you don't need
 * DAI status debug message when debugging
 * see rsnd_dbg_dai_call()
 *
 * #define RSND_DEBUG_NO_DAI_CALL 1
 */

#include <linux/pm_runtime.h>
#include "rsnd.h"

#define RSND_RATES SNDRV_PCM_RATE_8000_192000
#define RSND_FMTS (SNDRV_PCM_FMTBIT_S8 |\
		   SNDRV_PCM_FMTBIT_S16_LE |\
		   SNDRV_PCM_FMTBIT_S24_LE)

static const struct of_device_id rsnd_of_match[] = {
	{ .compatible = "renesas,rcar_sound-gen1", .data = (void *)RSND_GEN1 },
	{ .compatible = "renesas,rcar_sound-gen2", .data = (void *)RSND_GEN2 },
	{ .compatible = "renesas,rcar_sound-gen3", .data = (void *)RSND_GEN3 },
	{},
};
MODULE_DEVICE_TABLE(of, rsnd_of_match);

/*
 *	rsnd_mod functions
 */
void rsnd_mod_make_sure(struct rsnd_mod *mod, enum rsnd_mod_type type)
{
	if (mod->type != type) {
		struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
		struct device *dev = rsnd_priv_to_dev(priv);

		dev_warn(dev, "%s is not your expected module\n",
			 rsnd_mod_name(mod));
	}
}

struct dma_chan *rsnd_mod_dma_req(struct rsnd_dai_stream *io,
				  struct rsnd_mod *mod)
{
	if (!mod || !mod->ops || !mod->ops->dma_req)
		return NULL;

	return mod->ops->dma_req(io, mod);
}

#define MOD_NAME_NUM   5
#define MOD_NAME_SIZE 16
char *rsnd_mod_name(struct rsnd_mod *mod)
{
	static char names[MOD_NAME_NUM][MOD_NAME_SIZE];
	static int num;
	char *name = names[num];

	num++;
	if (num >= MOD_NAME_NUM)
		num = 0;

	/*
	 * Let's use same char to avoid pointlessness memory
	 * Thus, rsnd_mod_name() should be used immediately
	 * Don't keep pointer
	 */
	if ((mod)->ops->id_sub) {
		snprintf(name, MOD_NAME_SIZE, "%s[%d%d]",
			 mod->ops->name,
			 rsnd_mod_id(mod),
			 rsnd_mod_id_sub(mod));
	} else {
		snprintf(name, MOD_NAME_SIZE, "%s[%d]",
			 mod->ops->name,
			 rsnd_mod_id(mod));
	}

	return name;
}

u32 *rsnd_mod_get_status(struct rsnd_mod *mod,
			 struct rsnd_dai_stream *io,
			 enum rsnd_mod_type type)
{
	return &mod->status;
}

int rsnd_mod_id_raw(struct rsnd_mod *mod)
{
	return mod->id;
}

int rsnd_mod_id(struct rsnd_mod *mod)
{
	if ((mod)->ops->id)
		return (mod)->ops->id(mod);

	return rsnd_mod_id_raw(mod);
}

int rsnd_mod_id_sub(struct rsnd_mod *mod)
{
	if ((mod)->ops->id_sub)
		return (mod)->ops->id_sub(mod);

	return 0;
}

int rsnd_mod_init(struct rsnd_priv *priv,
		  struct rsnd_mod *mod,
		  struct rsnd_mod_ops *ops,
		  struct clk *clk,
		  enum rsnd_mod_type type,
		  int id)
{
	int ret = clk_prepare(clk);

	if (ret)
		return ret;

	mod->id		= id;
	mod->ops	= ops;
	mod->type	= type;
	mod->clk	= clk;
	mod->priv	= priv;

	return ret;
}

void rsnd_mod_quit(struct rsnd_mod *mod)
{
	clk_unprepare(mod->clk);
	mod->clk = NULL;
}

void rsnd_mod_interrupt(struct rsnd_mod *mod,
			void (*callback)(struct rsnd_mod *mod,
					 struct rsnd_dai_stream *io))
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct rsnd_dai_stream *io;
	struct rsnd_dai *rdai;
	int i;

	for_each_rsnd_dai(rdai, priv, i) {
		io = &rdai->playback;
		if (mod == io->mod[mod->type])
			callback(mod, io);

		io = &rdai->capture;
		if (mod == io->mod[mod->type])
			callback(mod, io);
	}
}

int rsnd_io_is_working(struct rsnd_dai_stream *io)
{
	/* see rsnd_dai_stream_init/quit() */
	if (io->substream)
		return snd_pcm_running(io->substream);

	return 0;
}

int rsnd_runtime_channel_original_with_params(struct rsnd_dai_stream *io,
					      struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = rsnd_io_to_runtime(io);

	/*
	 * params will be added when refine
	 * see
	 *	__rsnd_soc_hw_rule_rate()
	 *	__rsnd_soc_hw_rule_channels()
	 */
	if (params)
		return params_channels(params);
	else
		return runtime->channels;
}

int rsnd_runtime_channel_after_ctu_with_params(struct rsnd_dai_stream *io,
					       struct snd_pcm_hw_params *params)
{
	int chan = rsnd_runtime_channel_original_with_params(io, params);
	struct rsnd_mod *ctu_mod = rsnd_io_to_mod_ctu(io);

	if (ctu_mod) {
		u32 converted_chan = rsnd_io_converted_chan(io);

		/*
		 * !! Note !!
		 *
		 * converted_chan will be used for CTU,
		 * or TDM Split mode.
		 * User shouldn't use CTU with TDM Split mode.
		 */
		if (rsnd_runtime_is_tdm_split(io)) {
			struct device *dev = rsnd_priv_to_dev(rsnd_io_to_priv(io));

			dev_err(dev, "CTU and TDM Split should be used\n");
		}

		if (converted_chan)
			return converted_chan;
	}

	return chan;
}

int rsnd_runtime_channel_for_ssi_with_params(struct rsnd_dai_stream *io,
					     struct snd_pcm_hw_params *params)
{
	struct rsnd_dai *rdai = rsnd_io_to_rdai(io);
	int chan = rsnd_io_is_play(io) ?
		rsnd_runtime_channel_after_ctu_with_params(io, params) :
		rsnd_runtime_channel_original_with_params(io, params);

	/* Use Multi SSI */
	if (rsnd_runtime_is_multi_ssi(io))
		chan /= rsnd_rdai_ssi_lane_get(rdai);

	/* TDM Extend Mode needs 8ch */
	if (chan == 6)
		chan = 8;

	return chan;
}

int rsnd_runtime_is_multi_ssi(struct rsnd_dai_stream *io)
{
	struct rsnd_dai *rdai = rsnd_io_to_rdai(io);
	int lane = rsnd_rdai_ssi_lane_get(rdai);
	int chan = rsnd_io_is_play(io) ?
		rsnd_runtime_channel_after_ctu(io) :
		rsnd_runtime_channel_original(io);

	return (chan > 2) && (lane > 1);
}

int rsnd_runtime_is_tdm(struct rsnd_dai_stream *io)
{
	return rsnd_runtime_channel_for_ssi(io) >= 6;
}

int rsnd_runtime_is_tdm_split(struct rsnd_dai_stream *io)
{
	return !!rsnd_flags_has(io, RSND_STREAM_TDM_SPLIT);
}

/*
 *	ADINR function
 */
u32 rsnd_get_adinr_bit(struct rsnd_mod *mod, struct rsnd_dai_stream *io)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct snd_pcm_runtime *runtime = rsnd_io_to_runtime(io);
	struct device *dev = rsnd_priv_to_dev(priv);

	switch (snd_pcm_format_width(runtime->format)) {
	case 8:
		return 16 << 16;
	case 16:
		return 8 << 16;
	case 24:
		return 0 << 16;
	}

	dev_warn(dev, "not supported sample bits\n");

	return 0;
}

/*
 *	DALIGN function
 */
u32 rsnd_get_dalign(struct rsnd_mod *mod, struct rsnd_dai_stream *io)
{
	struct rsnd_mod *ssiu = rsnd_io_to_mod_ssiu(io);
	struct rsnd_mod *target;
	struct snd_pcm_runtime *runtime = rsnd_io_to_runtime(io);

	/*
	 * *Hardware* L/R and *Software* L/R are inverted for 16bit data.
	 *	    31..16 15...0
	 *	HW: [L ch] [R ch]
	 *	SW: [R ch] [L ch]
	 * We need to care about inversion timing to control
	 * Playback/Capture correctly.
	 * The point is [DVC] needs *Hardware* L/R, [MEM] needs *Software* L/R
	 *
	 * sL/R : software L/R
	 * hL/R : hardware L/R
	 * (*)  : conversion timing
	 *
	 * Playback
	 *	     sL/R (*) hL/R     hL/R     hL/R      hL/R     hL/R
	 *	[MEM] -> [SRC] -> [DVC] -> [CMD] -> [SSIU] -> [SSI] -> codec
	 *
	 * Capture
	 *	     hL/R     hL/R      hL/R     hL/R     hL/R (*) sL/R
	 *	codec -> [SSI] -> [SSIU] -> [SRC] -> [DVC] -> [CMD] -> [MEM]
	 */
	if (rsnd_io_is_play(io)) {
		struct rsnd_mod *src = rsnd_io_to_mod_src(io);

		target = src ? src : ssiu;
	} else {
		struct rsnd_mod *cmd = rsnd_io_to_mod_cmd(io);

		target = cmd ? cmd : ssiu;
	}

	/* Non target mod or non 16bit needs normal DALIGN */
	if ((snd_pcm_format_width(runtime->format) != 16) ||
	    (mod != target))
		return 0x76543210;
	/* Target mod needs inverted DALIGN when 16bit */
	else
		return 0x67452301;
}

u32 rsnd_get_busif_shift(struct rsnd_dai_stream *io, struct rsnd_mod *mod)
{
	enum rsnd_mod_type playback_mods[] = {
		RSND_MOD_SRC,
		RSND_MOD_CMD,
		RSND_MOD_SSIU,
	};
	enum rsnd_mod_type capture_mods[] = {
		RSND_MOD_CMD,
		RSND_MOD_SRC,
		RSND_MOD_SSIU,
	};
	struct snd_pcm_runtime *runtime = rsnd_io_to_runtime(io);
	struct rsnd_mod *tmod = NULL;
	enum rsnd_mod_type *mods =
		rsnd_io_is_play(io) ?
		playback_mods : capture_mods;
	int i;

	/*
	 * This is needed for 24bit data
	 * We need to shift 8bit
	 *
	 * Linux 24bit data is located as 0x00******
	 * HW    24bit data is located as 0x******00
	 *
	 */
	if (snd_pcm_format_width(runtime->format) != 24)
		return 0;

	for (i = 0; i < ARRAY_SIZE(playback_mods); i++) {
		tmod = rsnd_io_to_mod(io, mods[i]);
		if (tmod)
			break;
	}

	if (tmod != mod)
		return 0;

	if (rsnd_io_is_play(io))
		return  (0 << 20) | /* shift to Left */
			(8 << 16);  /* 8bit */
	else
		return  (1 << 20) | /* shift to Right */
			(8 << 16);  /* 8bit */
}

/*
 *	rsnd_dai functions
 */
struct rsnd_mod *rsnd_mod_next(int *iterator,
			       struct rsnd_dai_stream *io,
			       enum rsnd_mod_type *array,
			       int array_size)
{
	struct rsnd_mod *mod;
	enum rsnd_mod_type type;
	int max = array ? array_size : RSND_MOD_MAX;

	for (; *iterator < max; (*iterator)++) {
		type = (array) ? array[*iterator] : *iterator;
		mod = rsnd_io_to_mod(io, type);
		if (mod)
			return mod;
	}

	return NULL;
}

static enum rsnd_mod_type rsnd_mod_sequence[][RSND_MOD_MAX] = {
	{
		/* CAPTURE */
		RSND_MOD_AUDMAPP,
		RSND_MOD_AUDMA,
		RSND_MOD_DVC,
		RSND_MOD_MIX,
		RSND_MOD_CTU,
		RSND_MOD_CMD,
		RSND_MOD_SRC,
		RSND_MOD_SSIU,
		RSND_MOD_SSIM3,
		RSND_MOD_SSIM2,
		RSND_MOD_SSIM1,
		RSND_MOD_SSIP,
		RSND_MOD_SSI,
	}, {
		/* PLAYBACK */
		RSND_MOD_AUDMAPP,
		RSND_MOD_AUDMA,
		RSND_MOD_SSIM3,
		RSND_MOD_SSIM2,
		RSND_MOD_SSIM1,
		RSND_MOD_SSIP,
		RSND_MOD_SSI,
		RSND_MOD_SSIU,
		RSND_MOD_DVC,
		RSND_MOD_MIX,
		RSND_MOD_CTU,
		RSND_MOD_CMD,
		RSND_MOD_SRC,
	},
};

static int rsnd_status_update(u32 *status,
			      int shift, int add, int timing)
{
	u32 mask	= 0xF << shift;
	u8 val		= (*status >> shift) & 0xF;
	u8 next_val	= (val + add) & 0xF;
	int func_call	= (val == timing);

	if (next_val == 0xF) /* underflow case */
		func_call = 0;
	else
		*status = (*status & ~mask) + (next_val << shift);

	return func_call;
}

#define rsnd_dai_call(fn, io, param...)					\
({									\
	struct device *dev = rsnd_priv_to_dev(rsnd_io_to_priv(io));	\
	struct rsnd_mod *mod;						\
	int is_play = rsnd_io_is_play(io);				\
	int ret = 0, i;							\
	enum rsnd_mod_type *types = rsnd_mod_sequence[is_play];		\
	for_each_rsnd_mod_arrays(i, mod, io, types, RSND_MOD_MAX) {	\
		int tmp = 0;						\
		u32 *status = mod->ops->get_status(mod, io, types[i]);	\
		int func_call = rsnd_status_update(status,		\
						__rsnd_mod_shift_##fn,	\
						__rsnd_mod_add_##fn,	\
						__rsnd_mod_call_##fn);	\
		rsnd_dbg_dai_call(dev, "%s\t0x%08x %s\n",		\
			rsnd_mod_name(mod), *status,	\
			(func_call && (mod)->ops->fn) ? #fn : "");	\
		if (func_call && (mod)->ops->fn)			\
			tmp = (mod)->ops->fn(mod, io, param);		\
		if (tmp && (tmp != -EPROBE_DEFER))			\
			dev_err(dev, "%s : %s error %d\n",		\
				rsnd_mod_name(mod), #fn, tmp);		\
		ret |= tmp;						\
	}								\
	ret;								\
})

int rsnd_dai_connect(struct rsnd_mod *mod,
		     struct rsnd_dai_stream *io,
		     enum rsnd_mod_type type)
{
	struct rsnd_priv *priv;
	struct device *dev;

	if (!mod)
		return -EIO;

	if (io->mod[type] == mod)
		return 0;

	if (io->mod[type])
		return -EINVAL;

	priv = rsnd_mod_to_priv(mod);
	dev = rsnd_priv_to_dev(priv);

	io->mod[type] = mod;

	dev_dbg(dev, "%s is connected to io (%s)\n",
		rsnd_mod_name(mod),
		rsnd_io_is_play(io) ? "Playback" : "Capture");

	return 0;
}

static void rsnd_dai_disconnect(struct rsnd_mod *mod,
				struct rsnd_dai_stream *io,
				enum rsnd_mod_type type)
{
	io->mod[type] = NULL;
}

int rsnd_rdai_channels_ctrl(struct rsnd_dai *rdai,
			    int max_channels)
{
	if (max_channels > 0)
		rdai->max_channels = max_channels;

	return rdai->max_channels;
}

int rsnd_rdai_ssi_lane_ctrl(struct rsnd_dai *rdai,
			    int ssi_lane)
{
	if (ssi_lane > 0)
		rdai->ssi_lane = ssi_lane;

	return rdai->ssi_lane;
}

int rsnd_rdai_width_ctrl(struct rsnd_dai *rdai, int width)
{
	if (width > 0)
		rdai->chan_width = width;

	return rdai->chan_width;
}

struct rsnd_dai *rsnd_rdai_get(struct rsnd_priv *priv, int id)
{
	if ((id < 0) || (id >= rsnd_rdai_nr(priv)))
		return NULL;

	return priv->rdai + id;
}

static struct snd_soc_dai_driver
*rsnd_daidrv_get(struct rsnd_priv *priv, int id)
{
	if ((id < 0) || (id >= rsnd_rdai_nr(priv)))
		return NULL;

	return priv->daidrv + id;
}

#define rsnd_dai_to_priv(dai) snd_soc_dai_get_drvdata(dai)
static struct rsnd_dai *rsnd_dai_to_rdai(struct snd_soc_dai *dai)
{
	struct rsnd_priv *priv = rsnd_dai_to_priv(dai);

	return rsnd_rdai_get(priv, dai->id);
}

/*
 *	rsnd_soc_dai functions
 */
void rsnd_dai_period_elapsed(struct rsnd_dai_stream *io)
{
	struct snd_pcm_substream *substream = io->substream;

	/*
	 * this function should be called...
	 *
	 * - if rsnd_dai_pointer_update() returns true
	 * - without spin lock
	 */

	snd_pcm_period_elapsed(substream);
}

static void rsnd_dai_stream_init(struct rsnd_dai_stream *io,
				struct snd_pcm_substream *substream)
{
	io->substream		= substream;
}

static void rsnd_dai_stream_quit(struct rsnd_dai_stream *io)
{
	io->substream		= NULL;
}

static
struct snd_soc_dai *rsnd_substream_to_dai(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;

	return  rtd->cpu_dai;
}

static
struct rsnd_dai_stream *rsnd_rdai_to_io(struct rsnd_dai *rdai,
					struct snd_pcm_substream *substream)
{
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return &rdai->playback;
	else
		return &rdai->capture;
}

static int rsnd_soc_dai_trigger(struct snd_pcm_substream *substream, int cmd,
			    struct snd_soc_dai *dai)
{
	struct rsnd_priv *priv = rsnd_dai_to_priv(dai);
	struct rsnd_dai *rdai = rsnd_dai_to_rdai(dai);
	struct rsnd_dai_stream *io = rsnd_rdai_to_io(rdai, substream);
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		ret = rsnd_dai_call(init, io, priv);
		if (ret < 0)
			goto dai_trigger_end;

		ret = rsnd_dai_call(start, io, priv);
		if (ret < 0)
			goto dai_trigger_end;

		ret = rsnd_dai_call(irq, io, priv, 1);
		if (ret < 0)
			goto dai_trigger_end;

		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		ret = rsnd_dai_call(irq, io, priv, 0);

		ret |= rsnd_dai_call(stop, io, priv);

		ret |= rsnd_dai_call(quit, io, priv);

		break;
	default:
		ret = -EINVAL;
	}

dai_trigger_end:
	spin_unlock_irqrestore(&priv->lock, flags);

	return ret;
}

static int rsnd_soc_dai_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct rsnd_dai *rdai = rsnd_dai_to_rdai(dai);

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		rdai->clk_master = 0;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		rdai->clk_master = 1; /* codec is slave, cpu is master */
		break;
	default:
		return -EINVAL;
	}

	/* set format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		rdai->sys_delay = 0;
		rdai->data_alignment = 0;
		rdai->frm_clk_inv = 0;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
	case SND_SOC_DAIFMT_DSP_B:
		rdai->sys_delay = 1;
		rdai->data_alignment = 0;
		rdai->frm_clk_inv = 1;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		rdai->sys_delay = 1;
		rdai->data_alignment = 1;
		rdai->frm_clk_inv = 1;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		rdai->sys_delay = 0;
		rdai->data_alignment = 0;
		rdai->frm_clk_inv = 1;
		break;
	}

	/* set clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_IF:
		rdai->frm_clk_inv = !rdai->frm_clk_inv;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		rdai->bit_clk_inv = !rdai->bit_clk_inv;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		rdai->bit_clk_inv = !rdai->bit_clk_inv;
		rdai->frm_clk_inv = !rdai->frm_clk_inv;
		break;
	case SND_SOC_DAIFMT_NB_NF:
	default:
		break;
	}

	return 0;
}

static int rsnd_soc_set_dai_tdm_slot(struct snd_soc_dai *dai,
				     u32 tx_mask, u32 rx_mask,
				     int slots, int slot_width)
{
	struct rsnd_priv *priv = rsnd_dai_to_priv(dai);
	struct rsnd_dai *rdai = rsnd_dai_to_rdai(dai);
	struct device *dev = rsnd_priv_to_dev(priv);

	switch (slot_width) {
	case 16:
	case 24:
	case 32:
		break;
	default:
		/* use default */
		slot_width = 32;
	}

	switch (slots) {
	case 2:
		/* TDM Split Mode */
	case 6:
	case 8:
		/* TDM Extend Mode */
		rsnd_rdai_channels_set(rdai, slots);
		rsnd_rdai_ssi_lane_set(rdai, 1);
		rsnd_rdai_width_set(rdai, slot_width);
		break;
	default:
		dev_err(dev, "unsupported TDM slots (%d)\n", slots);
		return -EINVAL;
	}

	return 0;
}

static unsigned int rsnd_soc_hw_channels_list[] = {
	2, 6, 8,
};

static unsigned int rsnd_soc_hw_rate_list[] = {
	  8000,
	 11025,
	 16000,
	 22050,
	 32000,
	 44100,
	 48000,
	 64000,
	 88200,
	 96000,
	176400,
	192000,
};

static int rsnd_soc_hw_rule(struct rsnd_dai *rdai,
			    unsigned int *list, int list_num,
			    struct snd_interval *baseline, struct snd_interval *iv)
{
	struct snd_interval p;
	unsigned int rate;
	int i;

	snd_interval_any(&p);
	p.min = UINT_MAX;
	p.max = 0;

	for (i = 0; i < list_num; i++) {

		if (!snd_interval_test(iv, list[i]))
			continue;

		rate = rsnd_ssi_clk_query(rdai,
					  baseline->min, list[i], NULL);
		if (rate > 0) {
			p.min = min(p.min, list[i]);
			p.max = max(p.max, list[i]);
		}

		rate = rsnd_ssi_clk_query(rdai,
					  baseline->max, list[i], NULL);
		if (rate > 0) {
			p.min = min(p.min, list[i]);
			p.max = max(p.max, list[i]);
		}
	}

	return snd_interval_refine(iv, &p);
}

static int rsnd_soc_hw_rule_rate(struct snd_pcm_hw_params *params,
				 struct snd_pcm_hw_rule *rule)
{
	struct snd_interval *ic_ = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval *ir = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval ic;
	struct rsnd_dai_stream *io = rule->private;
	struct rsnd_dai *rdai = rsnd_io_to_rdai(io);

	/*
	 * possible sampling rate limitation is same as
	 * 2ch if it supports multi ssi
	 * and same as 8ch if TDM 6ch (see rsnd_ssi_config_init())
	 */
	ic = *ic_;
	ic.min =
	ic.max = rsnd_runtime_channel_for_ssi_with_params(io, params);

	return rsnd_soc_hw_rule(rdai, rsnd_soc_hw_rate_list,
				ARRAY_SIZE(rsnd_soc_hw_rate_list),
				&ic, ir);
}

static int rsnd_soc_hw_rule_channels(struct snd_pcm_hw_params *params,
				     struct snd_pcm_hw_rule *rule)
{
	struct snd_interval *ic_ = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval *ir = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval ic;
	struct rsnd_dai_stream *io = rule->private;
	struct rsnd_dai *rdai = rsnd_io_to_rdai(io);

	/*
	 * possible sampling rate limitation is same as
	 * 2ch if it supports multi ssi
	 * and same as 8ch if TDM 6ch (see rsnd_ssi_config_init())
	 */
	ic = *ic_;
	ic.min =
	ic.max = rsnd_runtime_channel_for_ssi_with_params(io, params);

	return rsnd_soc_hw_rule(rdai, rsnd_soc_hw_channels_list,
				ARRAY_SIZE(rsnd_soc_hw_channels_list),
				ir, &ic);
}

static const struct snd_pcm_hardware rsnd_pcm_hardware = {
	.info =		SNDRV_PCM_INFO_INTERLEAVED	|
			SNDRV_PCM_INFO_MMAP		|
			SNDRV_PCM_INFO_MMAP_VALID,
	.buffer_bytes_max	= 64 * 1024,
	.period_bytes_min	= 32,
	.period_bytes_max	= 8192,
	.periods_min		= 1,
	.periods_max		= 32,
	.fifo_size		= 256,
};

static int rsnd_soc_dai_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct rsnd_dai *rdai = rsnd_dai_to_rdai(dai);
	struct rsnd_dai_stream *io = rsnd_rdai_to_io(rdai, substream);
	struct snd_pcm_hw_constraint_list *constraint = &rdai->constraint;
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned int max_channels = rsnd_rdai_channels_get(rdai);
	int i;

	rsnd_dai_stream_init(io, substream);

	/*
	 * Channel Limitation
	 * It depends on Platform design
	 */
	constraint->list	= rsnd_soc_hw_channels_list;
	constraint->count	= 0;
	constraint->mask	= 0;

	for (i = 0; i < ARRAY_SIZE(rsnd_soc_hw_channels_list); i++) {
		if (rsnd_soc_hw_channels_list[i] > max_channels)
			break;
		constraint->count = i + 1;
	}

	snd_soc_set_runtime_hwparams(substream, &rsnd_pcm_hardware);

	snd_pcm_hw_constraint_list(runtime, 0,
				   SNDRV_PCM_HW_PARAM_CHANNELS, constraint);

	snd_pcm_hw_constraint_integer(runtime,
				      SNDRV_PCM_HW_PARAM_PERIODS);

	/*
	 * Sampling Rate / Channel Limitation
	 * It depends on Clock Master Mode
	 */
	if (rsnd_rdai_is_clk_master(rdai)) {
		int is_play = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;

		snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				    rsnd_soc_hw_rule_rate,
				    is_play ? &rdai->playback : &rdai->capture,
				    SNDRV_PCM_HW_PARAM_CHANNELS, -1);
		snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				    rsnd_soc_hw_rule_channels,
				    is_play ? &rdai->playback : &rdai->capture,
				    SNDRV_PCM_HW_PARAM_RATE, -1);
	}

	return 0;
}

static void rsnd_soc_dai_shutdown(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct rsnd_dai *rdai = rsnd_dai_to_rdai(dai);
	struct rsnd_priv *priv = rsnd_rdai_to_priv(rdai);
	struct rsnd_dai_stream *io = rsnd_rdai_to_io(rdai, substream);

	/*
	 * call rsnd_dai_call without spinlock
	 */
	rsnd_dai_call(cleanup, io, priv);

	rsnd_dai_stream_quit(io);
}

static int rsnd_soc_dai_prepare(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct rsnd_priv *priv = rsnd_dai_to_priv(dai);
	struct rsnd_dai *rdai = rsnd_dai_to_rdai(dai);
	struct rsnd_dai_stream *io = rsnd_rdai_to_io(rdai, substream);

	return rsnd_dai_call(prepare, io, priv);
}

static const struct snd_soc_dai_ops rsnd_soc_dai_ops = {
	.startup	= rsnd_soc_dai_startup,
	.shutdown	= rsnd_soc_dai_shutdown,
	.trigger	= rsnd_soc_dai_trigger,
	.set_fmt	= rsnd_soc_dai_set_fmt,
	.set_tdm_slot	= rsnd_soc_set_dai_tdm_slot,
	.prepare	= rsnd_soc_dai_prepare,
};

static void rsnd_parse_connect_simple(struct rsnd_priv *priv,
				      struct device_node *dai_np,
				      int dai_i, int is_play)
{
	struct device *dev = rsnd_priv_to_dev(priv);
	struct rsnd_dai *rdai = rsnd_rdai_get(priv, dai_i);
	struct rsnd_dai_stream *io = is_play ?
		&rdai->playback :
		&rdai->capture;
	struct device_node *ssiu_np = rsnd_ssiu_of_node(priv);
	struct device_node *np;
	int i, j;

	if (!ssiu_np)
		return;

	if (!rsnd_io_to_mod_ssi(io))
		return;

	/*
	 * This driver assumes that it is TDM Split mode
	 * if it includes ssiu node
	 */
	for (i = 0;; i++) {
		struct device_node *node = is_play ?
			of_parse_phandle(dai_np, "playback", i) :
			of_parse_phandle(dai_np, "capture",  i);

		if (!node)
			break;

		j = 0;
		for_each_child_of_node(ssiu_np, np) {
			if (np == node) {
				rsnd_flags_set(io, RSND_STREAM_TDM_SPLIT);
				dev_dbg(dev, "%s is part of TDM Split\n", io->name);
			}
			j++;
		}

	}
}

static void rsnd_parse_connect_graph(struct rsnd_priv *priv,
				     struct rsnd_dai_stream *io,
				     struct device_node *endpoint)
{
	struct device *dev = rsnd_priv_to_dev(priv);
	struct device_node *remote_port = of_graph_get_remote_port(endpoint);
	struct device_node *remote_node = of_graph_get_remote_port_parent(endpoint);

	if (!rsnd_io_to_mod_ssi(io))
		return;

	/* HDMI0 */
	if (strstr(remote_node->full_name, "hdmi@fead0000")) {
		rsnd_flags_set(io, RSND_STREAM_HDMI0);
		dev_dbg(dev, "%s connected to HDMI0\n", io->name);
	}

	/* HDMI1 */
	if (strstr(remote_node->full_name, "hdmi@feae0000")) {
		rsnd_flags_set(io, RSND_STREAM_HDMI1);
		dev_dbg(dev, "%s connected to HDMI1\n", io->name);
	}

	/*
	 * This driver assumes that it is TDM Split mode
	 * if remote node has multi endpoint
	 */
	if (of_get_child_count(remote_port) > 1) {
		rsnd_flags_set(io, RSND_STREAM_TDM_SPLIT);
		dev_dbg(dev, "%s is part of TDM Split\n", io->name);
	}
}

void rsnd_parse_connect_common(struct rsnd_dai *rdai,
		struct rsnd_mod* (*mod_get)(struct rsnd_priv *priv, int id),
		struct device_node *node,
		struct device_node *playback,
		struct device_node *capture)
{
	struct rsnd_priv *priv = rsnd_rdai_to_priv(rdai);
	struct device_node *np;
	struct rsnd_mod *mod;
	int i;

	if (!node)
		return;

	i = 0;
	for_each_child_of_node(node, np) {
		mod = mod_get(priv, i);
		if (np == playback)
			rsnd_dai_connect(mod, &rdai->playback, mod->type);
		if (np == capture)
			rsnd_dai_connect(mod, &rdai->capture, mod->type);
		i++;
	}

	of_node_put(node);
}

static struct device_node *rsnd_dai_of_node(struct rsnd_priv *priv,
					    int *is_graph)
{
	struct device *dev = rsnd_priv_to_dev(priv);
	struct device_node *np = dev->of_node;
	struct device_node *dai_node;
	struct device_node *ret;

	*is_graph = 0;

	/*
	 * parse both previous dai (= rcar_sound,dai), and
	 * graph dai (= ports/port)
	 */
	dai_node = of_get_child_by_name(np, RSND_NODE_DAI);
	if (dai_node) {
		ret = dai_node;
		goto of_node_compatible;
	}

	ret = np;

	dai_node = of_graph_get_next_endpoint(np, NULL);
	if (dai_node)
		goto of_node_graph;

	return NULL;

of_node_graph:
	*is_graph = 1;
of_node_compatible:
	of_node_put(dai_node);

	return ret;
}

static void __rsnd_dai_probe(struct rsnd_priv *priv,
			     struct device_node *dai_np,
			     int dai_i)
{
	struct device_node *playback, *capture;
	struct rsnd_dai_stream *io_playback;
	struct rsnd_dai_stream *io_capture;
	struct snd_soc_dai_driver *drv;
	struct rsnd_dai *rdai;
	struct device *dev = rsnd_priv_to_dev(priv);
	int io_i;

	rdai		= rsnd_rdai_get(priv, dai_i);
	drv		= rsnd_daidrv_get(priv, dai_i);
	io_playback	= &rdai->playback;
	io_capture	= &rdai->capture;

	snprintf(rdai->name, RSND_DAI_NAME_SIZE, "rsnd-dai.%d", dai_i);

	rdai->priv	= priv;
	drv->name	= rdai->name;
	drv->ops	= &rsnd_soc_dai_ops;

	snprintf(io_playback->name, RSND_DAI_NAME_SIZE,
		 "DAI%d Playback", dai_i);
	drv->playback.rates		= RSND_RATES;
	drv->playback.formats		= RSND_FMTS;
	drv->playback.channels_min	= 2;
	drv->playback.channels_max	= 8;
	drv->playback.stream_name	= io_playback->name;

	snprintf(io_capture->name, RSND_DAI_NAME_SIZE,
		 "DAI%d Capture", dai_i);
	drv->capture.rates		= RSND_RATES;
	drv->capture.formats		= RSND_FMTS;
	drv->capture.channels_min	= 2;
	drv->capture.channels_max	= 8;
	drv->capture.stream_name	= io_capture->name;

	io_playback->rdai		= rdai;
	io_capture->rdai		= rdai;
	rsnd_rdai_channels_set(rdai, 2); /* default 2ch */
	rsnd_rdai_ssi_lane_set(rdai, 1); /* default 1lane */
	rsnd_rdai_width_set(rdai, 32);   /* default 32bit width */

	for (io_i = 0;; io_i++) {
		playback = of_parse_phandle(dai_np, "playback", io_i);
		capture  = of_parse_phandle(dai_np, "capture", io_i);

		if (!playback && !capture)
			break;

		rsnd_parse_connect_ssi(rdai, playback, capture);
		rsnd_parse_connect_ssiu(rdai, playback, capture);
		rsnd_parse_connect_src(rdai, playback, capture);
		rsnd_parse_connect_ctu(rdai, playback, capture);
		rsnd_parse_connect_mix(rdai, playback, capture);
		rsnd_parse_connect_dvc(rdai, playback, capture);

		of_node_put(playback);
		of_node_put(capture);
	}

	if (rsnd_ssi_is_pin_sharing(io_capture) ||
	    rsnd_ssi_is_pin_sharing(io_playback)) {
		/* should have symmetric_rates if pin sharing */
		drv->symmetric_rates = 1;
	}

	dev_dbg(dev, "%s (%s/%s)\n", rdai->name,
		rsnd_io_to_mod_ssi(io_playback) ? "play"    : " -- ",
		rsnd_io_to_mod_ssi(io_capture) ? "capture" : "  --   ");
}

static int rsnd_dai_probe(struct rsnd_priv *priv)
{
	struct device_node *dai_node;
	struct device_node *dai_np;
	struct snd_soc_dai_driver *rdrv;
	struct device *dev = rsnd_priv_to_dev(priv);
	struct rsnd_dai *rdai;
	int nr;
	int is_graph;
	int dai_i;

	dai_node = rsnd_dai_of_node(priv, &is_graph);
	if (is_graph)
		nr = of_graph_get_endpoint_count(dai_node);
	else
		nr = of_get_child_count(dai_node);

	if (!nr)
		return -EINVAL;

	rdrv = devm_kcalloc(dev, nr, sizeof(*rdrv), GFP_KERNEL);
	rdai = devm_kcalloc(dev, nr, sizeof(*rdai), GFP_KERNEL);
	if (!rdrv || !rdai)
		return -ENOMEM;

	priv->rdai_nr	= nr;
	priv->daidrv	= rdrv;
	priv->rdai	= rdai;

	/*
	 * parse all dai
	 */
	dai_i = 0;
	if (is_graph) {
		for_each_endpoint_of_node(dai_node, dai_np) {
			__rsnd_dai_probe(priv, dai_np, dai_i);
			if (rsnd_is_gen3(priv)) {
				struct rsnd_dai *rdai = rsnd_rdai_get(priv, dai_i);

				rsnd_parse_connect_graph(priv, &rdai->playback, dai_np);
				rsnd_parse_connect_graph(priv, &rdai->capture,  dai_np);
			}
			dai_i++;
		}
	} else {
		for_each_child_of_node(dai_node, dai_np) {
			__rsnd_dai_probe(priv, dai_np, dai_i);
			if (rsnd_is_gen3(priv)) {
				rsnd_parse_connect_simple(priv, dai_np, dai_i, 1);
				rsnd_parse_connect_simple(priv, dai_np, dai_i, 0);
			}
			dai_i++;
		}
	}

	return 0;
}

/*
 *		pcm ops
 */
static int rsnd_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *hw_params)
{
	struct snd_soc_dai *dai = rsnd_substream_to_dai(substream);
	struct rsnd_dai *rdai = rsnd_dai_to_rdai(dai);
	struct rsnd_dai_stream *io = rsnd_rdai_to_io(rdai, substream);
	struct snd_soc_pcm_runtime *fe = substream->private_data;
	int ret;

	/*
	 * rsnd assumes that it might be used under DPCM if user want to use
	 * channel / rate convert. Then, rsnd should be FE.
	 * And then, this function will be called *after* BE settings.
	 * this means, each BE already has fixuped hw_params.
	 * see
	 *	dpcm_fe_dai_hw_params()
	 *	dpcm_be_dai_hw_params()
	 */
	io->converted_rate = 0;
	io->converted_chan = 0;
	if (fe->dai_link->dynamic) {
		struct rsnd_priv *priv = rsnd_io_to_priv(io);
		struct device *dev = rsnd_priv_to_dev(priv);
		struct snd_soc_dpcm *dpcm;
		struct snd_pcm_hw_params *be_params;
		int stream = substream->stream;

		for_each_dpcm_be(fe, stream, dpcm) {
			be_params = &dpcm->hw_params;
			if (params_channels(hw_params) != params_channels(be_params))
				io->converted_chan = params_channels(be_params);
			if (params_rate(hw_params) != params_rate(be_params))
				io->converted_rate = params_rate(be_params);
		}
		if (io->converted_chan)
			dev_dbg(dev, "convert channels = %d\n", io->converted_chan);
		if (io->converted_rate)
			dev_dbg(dev, "convert rate     = %d\n", io->converted_rate);
	}

	ret = rsnd_dai_call(hw_params, io, substream, hw_params);
	if (ret)
		return ret;

	return snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(hw_params));
}

static snd_pcm_uframes_t rsnd_pointer(struct snd_pcm_substream *substream)
{
	struct snd_soc_dai *dai = rsnd_substream_to_dai(substream);
	struct rsnd_dai *rdai = rsnd_dai_to_rdai(dai);
	struct rsnd_dai_stream *io = rsnd_rdai_to_io(rdai, substream);
	snd_pcm_uframes_t pointer = 0;

	rsnd_dai_call(pointer, io, &pointer);

	return pointer;
}

static const struct snd_pcm_ops rsnd_pcm_ops = {
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= rsnd_hw_params,
	.hw_free	= snd_pcm_lib_free_pages,
	.pointer	= rsnd_pointer,
};

/*
 *		snd_kcontrol
 */
static int rsnd_kctrl_info(struct snd_kcontrol *kctrl,
			   struct snd_ctl_elem_info *uinfo)
{
	struct rsnd_kctrl_cfg *cfg = snd_kcontrol_chip(kctrl);

	if (cfg->texts) {
		uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
		uinfo->count = cfg->size;
		uinfo->value.enumerated.items = cfg->max;
		if (uinfo->value.enumerated.item >= cfg->max)
			uinfo->value.enumerated.item = cfg->max - 1;
		strlcpy(uinfo->value.enumerated.name,
			cfg->texts[uinfo->value.enumerated.item],
			sizeof(uinfo->value.enumerated.name));
	} else {
		uinfo->count = cfg->size;
		uinfo->value.integer.min = 0;
		uinfo->value.integer.max = cfg->max;
		uinfo->type = (cfg->max == 1) ?
			SNDRV_CTL_ELEM_TYPE_BOOLEAN :
			SNDRV_CTL_ELEM_TYPE_INTEGER;
	}

	return 0;
}

static int rsnd_kctrl_get(struct snd_kcontrol *kctrl,
			  struct snd_ctl_elem_value *uc)
{
	struct rsnd_kctrl_cfg *cfg = snd_kcontrol_chip(kctrl);
	int i;

	for (i = 0; i < cfg->size; i++)
		if (cfg->texts)
			uc->value.enumerated.item[i] = cfg->val[i];
		else
			uc->value.integer.value[i] = cfg->val[i];

	return 0;
}

static int rsnd_kctrl_put(struct snd_kcontrol *kctrl,
			  struct snd_ctl_elem_value *uc)
{
	struct rsnd_kctrl_cfg *cfg = snd_kcontrol_chip(kctrl);
	int i, change = 0;

	if (!cfg->accept(cfg->io))
		return 0;

	for (i = 0; i < cfg->size; i++) {
		if (cfg->texts) {
			change |= (uc->value.enumerated.item[i] != cfg->val[i]);
			cfg->val[i] = uc->value.enumerated.item[i];
		} else {
			change |= (uc->value.integer.value[i] != cfg->val[i]);
			cfg->val[i] = uc->value.integer.value[i];
		}
	}

	if (change && cfg->update)
		cfg->update(cfg->io, cfg->mod);

	return change;
}

int rsnd_kctrl_accept_anytime(struct rsnd_dai_stream *io)
{
	return 1;
}

int rsnd_kctrl_accept_runtime(struct rsnd_dai_stream *io)
{
	struct snd_pcm_runtime *runtime = rsnd_io_to_runtime(io);
	struct rsnd_priv *priv = rsnd_io_to_priv(io);
	struct device *dev = rsnd_priv_to_dev(priv);

	if (!runtime) {
		dev_warn(dev, "Can't update kctrl when idle\n");
		return 0;
	}

	return 1;
}

struct rsnd_kctrl_cfg *rsnd_kctrl_init_m(struct rsnd_kctrl_cfg_m *cfg)
{
	cfg->cfg.val = cfg->val;

	return &cfg->cfg;
}

struct rsnd_kctrl_cfg *rsnd_kctrl_init_s(struct rsnd_kctrl_cfg_s *cfg)
{
	cfg->cfg.val = &cfg->val;

	return &cfg->cfg;
}

const char * const volume_ramp_rate[] = {
	"128 dB/1 step",	 /* 00000 */
	"64 dB/1 step",		 /* 00001 */
	"32 dB/1 step",		 /* 00010 */
	"16 dB/1 step",		 /* 00011 */
	"8 dB/1 step",		 /* 00100 */
	"4 dB/1 step",		 /* 00101 */
	"2 dB/1 step",		 /* 00110 */
	"1 dB/1 step",		 /* 00111 */
	"0.5 dB/1 step",	 /* 01000 */
	"0.25 dB/1 step",	 /* 01001 */
	"0.125 dB/1 step",	 /* 01010 = VOLUME_RAMP_MAX_MIX */
	"0.125 dB/2 steps",	 /* 01011 */
	"0.125 dB/4 steps",	 /* 01100 */
	"0.125 dB/8 steps",	 /* 01101 */
	"0.125 dB/16 steps",	 /* 01110 */
	"0.125 dB/32 steps",	 /* 01111 */
	"0.125 dB/64 steps",	 /* 10000 */
	"0.125 dB/128 steps",	 /* 10001 */
	"0.125 dB/256 steps",	 /* 10010 */
	"0.125 dB/512 steps",	 /* 10011 */
	"0.125 dB/1024 steps",	 /* 10100 */
	"0.125 dB/2048 steps",	 /* 10101 */
	"0.125 dB/4096 steps",	 /* 10110 */
	"0.125 dB/8192 steps",	 /* 10111 = VOLUME_RAMP_MAX_DVC */
};

int rsnd_kctrl_new(struct rsnd_mod *mod,
		   struct rsnd_dai_stream *io,
		   struct snd_soc_pcm_runtime *rtd,
		   const unsigned char *name,
		   int (*accept)(struct rsnd_dai_stream *io),
		   void (*update)(struct rsnd_dai_stream *io,
				  struct rsnd_mod *mod),
		   struct rsnd_kctrl_cfg *cfg,
		   const char * const *texts,
		   int size,
		   u32 max)
{
	struct snd_card *card = rtd->card->snd_card;
	struct snd_kcontrol *kctrl;
	struct snd_kcontrol_new knew = {
		.iface		= SNDRV_CTL_ELEM_IFACE_MIXER,
		.name		= name,
		.info		= rsnd_kctrl_info,
		.index		= rtd->num,
		.get		= rsnd_kctrl_get,
		.put		= rsnd_kctrl_put,
	};
	int ret;

	/*
	 * 1) Avoid duplicate register for DVC with MIX case
	 * 2) Allow duplicate register for MIX
	 * 3) re-register if card was rebinded
	 */
	list_for_each_entry(kctrl, &card->controls, list) {
		struct rsnd_kctrl_cfg *c = kctrl->private_data;

		if (c == cfg)
			return 0;
	}

	if (size > RSND_MAX_CHANNELS)
		return -EINVAL;

	kctrl = snd_ctl_new1(&knew, cfg);
	if (!kctrl)
		return -ENOMEM;

	ret = snd_ctl_add(card, kctrl);
	if (ret < 0)
		return ret;

	cfg->texts	= texts;
	cfg->max	= max;
	cfg->size	= size;
	cfg->accept	= accept;
	cfg->update	= update;
	cfg->card	= card;
	cfg->kctrl	= kctrl;
	cfg->io		= io;
	cfg->mod	= mod;

	return 0;
}

/*
 *		snd_soc_component
 */

#define PREALLOC_BUFFER		(32 * 1024)
#define PREALLOC_BUFFER_MAX	(32 * 1024)

static int rsnd_preallocate_pages(struct snd_soc_pcm_runtime *rtd,
				  struct rsnd_dai_stream *io,
				  int stream)
{
	struct rsnd_priv *priv = rsnd_io_to_priv(io);
	struct device *dev = rsnd_priv_to_dev(priv);
	struct snd_pcm_substream *substream;
	int err;

	/*
	 * use Audio-DMAC dev if we can use IPMMU
	 * see
	 *	rsnd_dmaen_attach()
	 */
	if (io->dmac_dev)
		dev = io->dmac_dev;

	for (substream = rtd->pcm->streams[stream].substream;
	     substream;
	     substream = substream->next) {
		err = snd_pcm_lib_preallocate_pages(substream,
					SNDRV_DMA_TYPE_DEV,
					dev,
					PREALLOC_BUFFER, PREALLOC_BUFFER_MAX);
		if (err < 0)
			return err;
	}

	return 0;
}

static int rsnd_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *dai = rtd->cpu_dai;
	struct rsnd_dai *rdai = rsnd_dai_to_rdai(dai);
	int ret;

	ret = rsnd_dai_call(pcm_new, &rdai->playback, rtd);
	if (ret)
		return ret;

	ret = rsnd_dai_call(pcm_new, &rdai->capture, rtd);
	if (ret)
		return ret;

	ret = rsnd_preallocate_pages(rtd, &rdai->playback,
				     SNDRV_PCM_STREAM_PLAYBACK);
	if (ret)
		return ret;

	ret = rsnd_preallocate_pages(rtd, &rdai->capture,
				     SNDRV_PCM_STREAM_CAPTURE);
	if (ret)
		return ret;

	return 0;
}

static const struct snd_soc_component_driver rsnd_soc_component = {
	.ops		= &rsnd_pcm_ops,
	.pcm_new	= rsnd_pcm_new,
	.name		= "rsnd",
};

static int rsnd_rdai_continuance_probe(struct rsnd_priv *priv,
				       struct rsnd_dai_stream *io)
{
	int ret;

	ret = rsnd_dai_call(probe, io, priv);
	if (ret == -EAGAIN) {
		struct rsnd_mod *ssi_mod = rsnd_io_to_mod_ssi(io);
		struct rsnd_mod *mod;
		int i;

		/*
		 * Fallback to PIO mode
		 */

		/*
		 * call "remove" for SSI/SRC/DVC
		 * SSI will be switch to PIO mode if it was DMA mode
		 * see
		 *	rsnd_dma_init()
		 *	rsnd_ssi_fallback()
		 */
		rsnd_dai_call(remove, io, priv);

		/*
		 * remove all mod from io
		 * and, re connect ssi
		 */
		for_each_rsnd_mod(i, mod, io)
			rsnd_dai_disconnect(mod, io, i);
		rsnd_dai_connect(ssi_mod, io, RSND_MOD_SSI);

		/*
		 * fallback
		 */
		rsnd_dai_call(fallback, io, priv);

		/*
		 * retry to "probe".
		 * DAI has SSI which is PIO mode only now.
		 */
		ret = rsnd_dai_call(probe, io, priv);
	}

	return ret;
}

/*
 *	rsnd probe
 */
static int rsnd_probe(struct platform_device *pdev)
{
	struct rsnd_priv *priv;
	struct device *dev = &pdev->dev;
	struct rsnd_dai *rdai;
	int (*probe_func[])(struct rsnd_priv *priv) = {
		rsnd_gen_probe,
		rsnd_dma_probe,
		rsnd_ssi_probe,
		rsnd_ssiu_probe,
		rsnd_src_probe,
		rsnd_ctu_probe,
		rsnd_mix_probe,
		rsnd_dvc_probe,
		rsnd_cmd_probe,
		rsnd_adg_probe,
		rsnd_dai_probe,
	};
	int ret, i;

	/*
	 *	init priv data
	 */
	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENODEV;

	priv->pdev	= pdev;
	priv->flags	= (unsigned long)of_device_get_match_data(dev);
	spin_lock_init(&priv->lock);

	/*
	 *	init each module
	 */
	for (i = 0; i < ARRAY_SIZE(probe_func); i++) {
		ret = probe_func[i](priv);
		if (ret)
			return ret;
	}

	for_each_rsnd_dai(rdai, priv, i) {
		ret = rsnd_rdai_continuance_probe(priv, &rdai->playback);
		if (ret)
			goto exit_snd_probe;

		ret = rsnd_rdai_continuance_probe(priv, &rdai->capture);
		if (ret)
			goto exit_snd_probe;
	}

	dev_set_drvdata(dev, priv);

	/*
	 *	asoc register
	 */
	ret = devm_snd_soc_register_component(dev, &rsnd_soc_component,
					 priv->daidrv, rsnd_rdai_nr(priv));
	if (ret < 0) {
		dev_err(dev, "cannot snd dai register\n");
		goto exit_snd_probe;
	}

	pm_runtime_enable(dev);

	dev_info(dev, "probed\n");
	return ret;

exit_snd_probe:
	for_each_rsnd_dai(rdai, priv, i) {
		rsnd_dai_call(remove, &rdai->playback, priv);
		rsnd_dai_call(remove, &rdai->capture, priv);
	}

	/*
	 * adg is very special mod which can't use rsnd_dai_call(remove),
	 * and it registers ADG clock on probe.
	 * It should be unregister if probe failed.
	 * Mainly it is assuming -EPROBE_DEFER case
	 */
	rsnd_adg_remove(priv);

	return ret;
}

static int rsnd_remove(struct platform_device *pdev)
{
	struct rsnd_priv *priv = dev_get_drvdata(&pdev->dev);
	struct rsnd_dai *rdai;
	void (*remove_func[])(struct rsnd_priv *priv) = {
		rsnd_ssi_remove,
		rsnd_ssiu_remove,
		rsnd_src_remove,
		rsnd_ctu_remove,
		rsnd_mix_remove,
		rsnd_dvc_remove,
		rsnd_cmd_remove,
		rsnd_adg_remove,
	};
	int ret = 0, i;

	snd_soc_disconnect_sync(&pdev->dev);

	pm_runtime_disable(&pdev->dev);

	for_each_rsnd_dai(rdai, priv, i) {
		ret |= rsnd_dai_call(remove, &rdai->playback, priv);
		ret |= rsnd_dai_call(remove, &rdai->capture, priv);
	}

	for (i = 0; i < ARRAY_SIZE(remove_func); i++)
		remove_func[i](priv);

	return ret;
}

static int __maybe_unused rsnd_suspend(struct device *dev)
{
	struct rsnd_priv *priv = dev_get_drvdata(dev);

	rsnd_adg_clk_disable(priv);

	return 0;
}

static int __maybe_unused rsnd_resume(struct device *dev)
{
	struct rsnd_priv *priv = dev_get_drvdata(dev);

	rsnd_adg_clk_enable(priv);

	return 0;
}

static const struct dev_pm_ops rsnd_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(rsnd_suspend, rsnd_resume)
};

static struct platform_driver rsnd_driver = {
	.driver	= {
		.name	= "rcar_sound",
		.pm	= &rsnd_pm_ops,
		.of_match_table = rsnd_of_match,
	},
	.probe		= rsnd_probe,
	.remove		= rsnd_remove,
};
module_platform_driver(rsnd_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Renesas R-Car audio driver");
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
MODULE_ALIAS("platform:rcar-pcm-audio");
