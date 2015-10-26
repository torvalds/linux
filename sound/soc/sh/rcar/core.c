/*
 * Renesas R-Car SRU/SCU/SSIU/SSI support
 *
 * Copyright (C) 2013 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * Based on fsi.c
 * Kuninori Morimoto <morimoto.kuninori@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

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
#include <linux/pm_runtime.h>
#include "rsnd.h"

#define RSND_RATES SNDRV_PCM_RATE_8000_96000
#define RSND_FMTS (SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S16_LE)

static const struct rsnd_of_data rsnd_of_data_gen1 = {
	.flags = RSND_GEN1,
};

static const struct rsnd_of_data rsnd_of_data_gen2 = {
	.flags = RSND_GEN2,
};

static const struct of_device_id rsnd_of_match[] = {
	{ .compatible = "renesas,rcar_sound-gen1", .data = &rsnd_of_data_gen1 },
	{ .compatible = "renesas,rcar_sound-gen2", .data = &rsnd_of_data_gen2 },
	{ .compatible = "renesas,rcar_sound-gen3", .data = &rsnd_of_data_gen2 }, /* gen2 compatible */
	{},
};
MODULE_DEVICE_TABLE(of, rsnd_of_match);

/*
 *	rsnd_platform functions
 */
#define rsnd_platform_call(priv, dai, func, param...)	\
	(!(priv->info->func) ? 0 :		\
	 priv->info->func(param))

#define rsnd_is_enable_path(io, name) \
	((io)->info ? (io)->info->name : NULL)
#define rsnd_info_id(priv, io, name) \
	((io)->info->name - priv->info->name##_info)

void rsnd_mod_make_sure(struct rsnd_mod *mod, enum rsnd_mod_type type)
{
	if (mod->type != type) {
		struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
		struct device *dev = rsnd_priv_to_dev(priv);

		dev_warn(dev, "%s[%d] is not your expected module\n",
			 rsnd_mod_name(mod), rsnd_mod_id(mod));
	}
}

/*
 *	rsnd_mod functions
 */
char *rsnd_mod_name(struct rsnd_mod *mod)
{
	if (!mod || !mod->ops)
		return "unknown";

	return mod->ops->name;
}

struct dma_chan *rsnd_mod_dma_req(struct rsnd_dai_stream *io,
				  struct rsnd_mod *mod)
{
	if (!mod || !mod->ops || !mod->ops->dma_req)
		return NULL;

	return mod->ops->dma_req(io, mod);
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
	if (mod->clk)
		clk_unprepare(mod->clk);
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
	return !!io->substream;
}

/*
 *	ADINR function
 */
u32 rsnd_get_adinr_bit(struct rsnd_mod *mod, struct rsnd_dai_stream *io)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct snd_pcm_runtime *runtime = rsnd_io_to_runtime(io);
	struct device *dev = rsnd_priv_to_dev(priv);
	u32 adinr = runtime->channels;

	switch (runtime->sample_bits) {
	case 16:
		adinr |= (8 << 16);
		break;
	case 32:
		adinr |= (0 << 16);
		break;
	default:
		dev_warn(dev, "not supported sample bits\n");
		return 0;
	}

	return adinr;
}

u32 rsnd_get_adinr_chan(struct rsnd_mod *mod, struct rsnd_dai_stream *io)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct snd_pcm_runtime *runtime = rsnd_io_to_runtime(io);
	struct device *dev = rsnd_priv_to_dev(priv);
	u32 chan = runtime->channels;

	switch (chan) {
	case 1:
	case 2:
	case 4:
	case 6:
	case 8:
		break;
	default:
		dev_warn(dev, "not supported channel\n");
		chan = 0;
		break;
	}

	return chan;
}

/*
 *	DALIGN function
 */
u32 rsnd_get_dalign(struct rsnd_mod *mod, struct rsnd_dai_stream *io)
{
	struct rsnd_mod *src = rsnd_io_to_mod_src(io);
	struct rsnd_mod *ssi = rsnd_io_to_mod_ssi(io);
	struct rsnd_mod *target = src ? src : ssi;
	struct snd_pcm_runtime *runtime = rsnd_io_to_runtime(io);
	u32 val = 0x76543210;
	u32 mask = ~0;

	mask <<= runtime->channels * 4;
	val = val & mask;

	switch (runtime->sample_bits) {
	case 16:
		val |= 0x67452301 & ~mask;
		break;
	case 32:
		val |= 0x76543210 & ~mask;
		break;
	}

	/*
	 * exchange channeles on SRC if possible,
	 * otherwise, R/L volume settings on DVC
	 * changes inverted channels
	 */
	if (mod == target)
		return val;
	else
		return 0x76543210;
}

/*
 *	rsnd_dai functions
 */
#define rsnd_mod_call(mod, io, func, param...)			\
({								\
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);		\
	struct device *dev = rsnd_priv_to_dev(priv);		\
	u32 mask = 0xF << __rsnd_mod_shift_##func;			\
	u8 val  = (mod->status >> __rsnd_mod_shift_##func) & 0xF;	\
	u8 add  = ((val + __rsnd_mod_add_##func) & 0xF);		\
	int ret = 0;							\
	int call = (val == __rsnd_mod_call_##func) && (mod)->ops->func;	\
	mod->status = (mod->status & ~mask) +				\
		(add << __rsnd_mod_shift_##func);			\
	dev_dbg(dev, "%s[%d]\t0x%08x %s\n",				\
		rsnd_mod_name(mod), rsnd_mod_id(mod),			\
		mod->status, call ? #func : "");			\
	if (call)							\
		ret = (mod)->ops->func(mod, io, param);			\
	ret;								\
})

#define rsnd_dai_call(fn, io, param...)				\
({								\
	struct rsnd_mod *mod;					\
	int ret = 0, i;						\
	for (i = 0; i < RSND_MOD_MAX; i++) {			\
		mod = (io)->mod[i];				\
		if (!mod)					\
			continue;				\
		ret |= rsnd_mod_call(mod, io, fn, param);	\
	}							\
	ret;							\
})

static int rsnd_dai_connect(struct rsnd_mod *mod,
			    struct rsnd_dai_stream *io)
{
	struct rsnd_priv *priv;
	struct device *dev;

	if (!mod)
		return -EIO;

	priv = rsnd_mod_to_priv(mod);
	dev = rsnd_priv_to_dev(priv);

	io->mod[mod->type] = mod;

	dev_dbg(dev, "%s[%d] is connected to io (%s)\n",
		rsnd_mod_name(mod), rsnd_mod_id(mod),
		rsnd_io_is_play(io) ? "Playback" : "Capture");

	return 0;
}

static void rsnd_dai_disconnect(struct rsnd_mod *mod,
				struct rsnd_dai_stream *io)
{
	io->mod[mod->type] = NULL;
}

struct rsnd_dai *rsnd_rdai_get(struct rsnd_priv *priv, int id)
{
	if ((id < 0) || (id >= rsnd_rdai_nr(priv)))
		return NULL;

	return priv->rdai + id;
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
int rsnd_dai_pointer_offset(struct rsnd_dai_stream *io, int additional)
{
	struct snd_pcm_substream *substream = io->substream;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int pos = io->byte_pos + additional;

	pos %= (runtime->periods * io->byte_per_period);

	return pos;
}

bool rsnd_dai_pointer_update(struct rsnd_dai_stream *io, int byte)
{
	io->byte_pos += byte;

	if (io->byte_pos >= io->next_period_byte) {
		struct snd_pcm_substream *substream = io->substream;
		struct snd_pcm_runtime *runtime = substream->runtime;

		io->period_pos++;
		io->next_period_byte += io->byte_per_period;

		if (io->period_pos >= runtime->periods) {
			io->byte_pos = 0;
			io->period_pos = 0;
			io->next_period_byte = io->byte_per_period;
		}

		return true;
	}

	return false;
}

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
	struct snd_pcm_runtime *runtime = substream->runtime;

	io->substream		= substream;
	io->byte_pos		= 0;
	io->period_pos		= 0;
	io->byte_per_period	= runtime->period_size *
				  runtime->channels *
				  samples_to_bytes(runtime, 1);
	io->next_period_byte	= io->byte_per_period;
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
	int ssi_id = rsnd_mod_id(rsnd_io_to_mod_ssi(io));
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		rsnd_dai_stream_init(io, substream);

		ret = rsnd_platform_call(priv, dai, start, ssi_id);
		if (ret < 0)
			goto dai_trigger_end;

		ret = rsnd_dai_call(init, io, priv);
		if (ret < 0)
			goto dai_trigger_end;

		ret = rsnd_dai_call(start, io, priv);
		if (ret < 0)
			goto dai_trigger_end;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		ret = rsnd_dai_call(stop, io, priv);

		ret |= rsnd_dai_call(quit, io, priv);

		ret |= rsnd_platform_call(priv, dai, stop, ssi_id);

		rsnd_dai_stream_quit(io);
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
		rdai->sys_delay = 1;
		rdai->data_alignment = 0;
		rdai->frm_clk_inv = 1;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		rdai->sys_delay = 1;
		rdai->data_alignment = 1;
		rdai->frm_clk_inv = 1;
		break;
	}

	/* set clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_IF:
		rdai->bit_clk_inv =  rdai->bit_clk_inv;
		rdai->frm_clk_inv = !rdai->frm_clk_inv;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		rdai->bit_clk_inv = !rdai->bit_clk_inv;
		rdai->frm_clk_inv =  rdai->frm_clk_inv;
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

static const struct snd_soc_dai_ops rsnd_soc_dai_ops = {
	.trigger	= rsnd_soc_dai_trigger,
	.set_fmt	= rsnd_soc_dai_set_fmt,
};

#define rsnd_path_add(priv, io, type)				\
({								\
	struct rsnd_mod *mod;					\
	int ret = 0;						\
	int id = -1;						\
								\
	if (rsnd_is_enable_path(io, type)) {			\
		id = rsnd_info_id(priv, io, type);		\
		if (id >= 0) {					\
			mod = rsnd_##type##_mod_get(priv, id);	\
			ret = rsnd_dai_connect(mod, io);	\
		}						\
	}							\
	ret;							\
})

#define rsnd_path_remove(priv, io, type)			\
{								\
	struct rsnd_mod *mod;					\
	int id = -1;						\
								\
	if (rsnd_is_enable_path(io, type)) {			\
		id = rsnd_info_id(priv, io, type);		\
		if (id >= 0) {					\
			mod = rsnd_##type##_mod_get(priv, id);	\
			rsnd_dai_disconnect(mod, io);		\
		}						\
	}							\
}

void rsnd_path_parse(struct rsnd_priv *priv,
		     struct rsnd_dai_stream *io)
{
	struct rsnd_mod *dvc = rsnd_io_to_mod_dvc(io);
	struct rsnd_mod *mix = rsnd_io_to_mod_mix(io);
	struct rsnd_mod *src = rsnd_io_to_mod_src(io);
	struct rsnd_mod *cmd;
	struct device *dev = rsnd_priv_to_dev(priv);
	u32 data;

	/* Gen1 is not supported */
	if (rsnd_is_gen1(priv))
		return;

	if (!mix && !dvc)
		return;

	if (mix) {
		struct rsnd_dai *rdai;
		int i;
		u32 path[] = {
			[0] = 0,
			[1] = 1 << 0,
			[2] = 0,
			[3] = 0,
			[4] = 0,
			[5] = 1 << 8
		};

		/*
		 * it is assuming that integrater is well understanding about
		 * data path. Here doesn't check impossible connection,
		 * like src2 + src5
		 */
		data = 0;
		for_each_rsnd_dai(rdai, priv, i) {
			io = &rdai->playback;
			if (mix == rsnd_io_to_mod_mix(io))
				data |= path[rsnd_mod_id(src)];

			io = &rdai->capture;
			if (mix == rsnd_io_to_mod_mix(io))
				data |= path[rsnd_mod_id(src)];
		}

		/*
		 * We can't use ctu = rsnd_io_ctu() here.
		 * Since, ID of dvc/mix are 0 or 1 (= same as CMD number)
		 * but ctu IDs are 0 - 7 (= CTU00 - CTU13)
		 */
		cmd = mix;
	} else {
		u32 path[] = {
			[0] = 0x30000,
			[1] = 0x30001,
			[2] = 0x40000,
			[3] = 0x10000,
			[4] = 0x20000,
			[5] = 0x40100
		};

		data = path[rsnd_mod_id(src)];

		cmd = dvc;
	}

	dev_dbg(dev, "ctu/mix path = 0x%08x", data);

	rsnd_mod_write(cmd, CMD_ROUTE_SLCT, data);

	rsnd_mod_write(cmd, CMD_CTRL, 0x10);
}

static int rsnd_path_init(struct rsnd_priv *priv,
			  struct rsnd_dai *rdai,
			  struct rsnd_dai_stream *io)
{
	int ret;

	/*
	 * Gen1 is created by SRU/SSI, and this SRU is base module of
	 * Gen2's SCU/SSIU/SSI. (Gen2 SCU/SSIU came from SRU)
	 *
	 * Easy image is..
	 *	Gen1 SRU = Gen2 SCU + SSIU + etc
	 *
	 * Gen2 SCU path is very flexible, but, Gen1 SRU (SCU parts) is
	 * using fixed path.
	 */

	/* SSI */
	ret = rsnd_path_add(priv, io, ssi);
	if (ret < 0)
		return ret;

	/* SRC */
	ret = rsnd_path_add(priv, io, src);
	if (ret < 0)
		return ret;

	/* CTU */
	ret = rsnd_path_add(priv, io, ctu);
	if (ret < 0)
		return ret;

	/* MIX */
	ret = rsnd_path_add(priv, io, mix);
	if (ret < 0)
		return ret;

	/* DVC */
	ret = rsnd_path_add(priv, io, dvc);
	if (ret < 0)
		return ret;

	return ret;
}

static void rsnd_of_parse_dai(struct platform_device *pdev,
			      const struct rsnd_of_data *of_data,
			      struct rsnd_priv *priv)
{
	struct device_node *dai_node,	*dai_np;
	struct device_node *ssi_node,	*ssi_np;
	struct device_node *src_node,	*src_np;
	struct device_node *ctu_node,	*ctu_np;
	struct device_node *mix_node,	*mix_np;
	struct device_node *dvc_node,	*dvc_np;
	struct device_node *playback, *capture;
	struct rsnd_dai_platform_info *dai_info;
	struct rcar_snd_info *info = rsnd_priv_to_info(priv);
	struct device *dev = &pdev->dev;
	int nr, i;
	int dai_i, ssi_i, src_i, ctu_i, mix_i, dvc_i;

	if (!of_data)
		return;

	dai_node = of_get_child_by_name(dev->of_node, "rcar_sound,dai");
	if (!dai_node)
		return;

	nr = of_get_child_count(dai_node);
	if (!nr)
		return;

	dai_info = devm_kzalloc(dev,
				sizeof(struct rsnd_dai_platform_info) * nr,
				GFP_KERNEL);
	if (!dai_info) {
		dev_err(dev, "dai info allocation error\n");
		return;
	}

	info->dai_info_nr	= nr;
	info->dai_info		= dai_info;

	ssi_node = of_get_child_by_name(dev->of_node, "rcar_sound,ssi");
	src_node = of_get_child_by_name(dev->of_node, "rcar_sound,src");
	ctu_node = of_get_child_by_name(dev->of_node, "rcar_sound,ctu");
	mix_node = of_get_child_by_name(dev->of_node, "rcar_sound,mix");
	dvc_node = of_get_child_by_name(dev->of_node, "rcar_sound,dvc");

#define mod_parse(name)							\
if (name##_node) {							\
	struct rsnd_##name##_platform_info *name##_info;		\
									\
	name##_i = 0;							\
	for_each_child_of_node(name##_node, name##_np) {		\
		name##_info = info->name##_info + name##_i;		\
									\
		if (name##_np == playback)				\
			dai_info->playback.name = name##_info;		\
		if (name##_np == capture)				\
			dai_info->capture.name = name##_info;		\
									\
		name##_i++;						\
	}								\
}

	/*
	 * parse all dai
	 */
	dai_i = 0;
	for_each_child_of_node(dai_node, dai_np) {
		dai_info = info->dai_info + dai_i;

		for (i = 0;; i++) {

			playback = of_parse_phandle(dai_np, "playback", i);
			capture  = of_parse_phandle(dai_np, "capture", i);

			if (!playback && !capture)
				break;

			mod_parse(ssi);
			mod_parse(src);
			mod_parse(ctu);
			mod_parse(mix);
			mod_parse(dvc);

			of_node_put(playback);
			of_node_put(capture);
		}

		dai_i++;
	}
}

static int rsnd_dai_probe(struct platform_device *pdev,
			  const struct rsnd_of_data *of_data,
			  struct rsnd_priv *priv)
{
	struct snd_soc_dai_driver *drv;
	struct rcar_snd_info *info = rsnd_priv_to_info(priv);
	struct rsnd_dai *rdai;
	struct rsnd_ssi_platform_info *pmod, *cmod;
	struct device *dev = rsnd_priv_to_dev(priv);
	int dai_nr;
	int i;

	rsnd_of_parse_dai(pdev, of_data, priv);

	dai_nr = info->dai_info_nr;
	if (!dai_nr) {
		dev_err(dev, "no dai\n");
		return -EIO;
	}

	drv  = devm_kzalloc(dev, sizeof(*drv)  * dai_nr, GFP_KERNEL);
	rdai = devm_kzalloc(dev, sizeof(*rdai) * dai_nr, GFP_KERNEL);
	if (!drv || !rdai) {
		dev_err(dev, "dai allocate failed\n");
		return -ENOMEM;
	}

	priv->rdai_nr	= dai_nr;
	priv->daidrv	= drv;
	priv->rdai	= rdai;

	for (i = 0; i < dai_nr; i++) {

		pmod = info->dai_info[i].playback.ssi;
		cmod = info->dai_info[i].capture.ssi;

		/*
		 *	init rsnd_dai
		 */
		snprintf(rdai[i].name, RSND_DAI_NAME_SIZE, "rsnd-dai.%d", i);
		rdai[i].priv = priv;

		/*
		 *	init snd_soc_dai_driver
		 */
		drv[i].name	= rdai[i].name;
		drv[i].ops	= &rsnd_soc_dai_ops;
		if (pmod) {
			snprintf(rdai[i].playback.name, RSND_DAI_NAME_SIZE,
				 "DAI%d Playback", i);

			drv[i].playback.rates		= RSND_RATES;
			drv[i].playback.formats		= RSND_FMTS;
			drv[i].playback.channels_min	= 2;
			drv[i].playback.channels_max	= 2;
			drv[i].playback.stream_name	= rdai[i].playback.name;

			rdai[i].playback.info = &info->dai_info[i].playback;
			rdai[i].playback.rdai = rdai + i;
			rsnd_path_init(priv, &rdai[i], &rdai[i].playback);
		}
		if (cmod) {
			snprintf(rdai[i].capture.name, RSND_DAI_NAME_SIZE,
				 "DAI%d Capture", i);

			drv[i].capture.rates		= RSND_RATES;
			drv[i].capture.formats		= RSND_FMTS;
			drv[i].capture.channels_min	= 2;
			drv[i].capture.channels_max	= 2;
			drv[i].capture.stream_name	= rdai[i].capture.name;

			rdai[i].capture.info = &info->dai_info[i].capture;
			rdai[i].capture.rdai = rdai + i;
			rsnd_path_init(priv, &rdai[i], &rdai[i].capture);
		}

		dev_dbg(dev, "%s (%s/%s)\n", rdai[i].name,
			pmod ? "play"    : " -- ",
			cmod ? "capture" : "  --   ");
	}

	return 0;
}

/*
 *		pcm ops
 */
static struct snd_pcm_hardware rsnd_pcm_hardware = {
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

static int rsnd_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int ret = 0;

	snd_soc_set_runtime_hwparams(substream, &rsnd_pcm_hardware);

	ret = snd_pcm_hw_constraint_integer(runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);

	return ret;
}

static int rsnd_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *hw_params)
{
	struct snd_soc_dai *dai = rsnd_substream_to_dai(substream);
	struct rsnd_dai *rdai = rsnd_dai_to_rdai(dai);
	struct rsnd_dai_stream *io = rsnd_rdai_to_io(rdai, substream);
	int ret;

	ret = rsnd_dai_call(hw_params, io, substream, hw_params);
	if (ret)
		return ret;

	return snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(hw_params));
}

static snd_pcm_uframes_t rsnd_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_dai *dai = rsnd_substream_to_dai(substream);
	struct rsnd_dai *rdai = rsnd_dai_to_rdai(dai);
	struct rsnd_dai_stream *io = rsnd_rdai_to_io(rdai, substream);

	return bytes_to_frames(runtime, io->byte_pos);
}

static struct snd_pcm_ops rsnd_pcm_ops = {
	.open		= rsnd_pcm_open,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= rsnd_hw_params,
	.hw_free	= snd_pcm_lib_free_pages,
	.pointer	= rsnd_pointer,
};

/*
 *		snd_kcontrol
 */
#define kcontrol_to_cfg(kctrl) ((struct rsnd_kctrl_cfg *)kctrl->private_value)
static int rsnd_kctrl_info(struct snd_kcontrol *kctrl,
			   struct snd_ctl_elem_info *uinfo)
{
	struct rsnd_kctrl_cfg *cfg = kcontrol_to_cfg(kctrl);

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
	struct rsnd_kctrl_cfg *cfg = kcontrol_to_cfg(kctrl);
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
	struct rsnd_mod *mod = snd_kcontrol_chip(kctrl);
	struct rsnd_kctrl_cfg *cfg = kcontrol_to_cfg(kctrl);
	int i, change = 0;

	for (i = 0; i < cfg->size; i++) {
		if (cfg->texts) {
			change |= (uc->value.enumerated.item[i] != cfg->val[i]);
			cfg->val[i] = uc->value.enumerated.item[i];
		} else {
			change |= (uc->value.integer.value[i] != cfg->val[i]);
			cfg->val[i] = uc->value.integer.value[i];
		}
	}

	if (change)
		cfg->update(cfg->io, mod);

	return change;
}

static int __rsnd_kctrl_new(struct rsnd_mod *mod,
			    struct rsnd_dai_stream *io,
			    struct snd_soc_pcm_runtime *rtd,
			    const unsigned char *name,
			    struct rsnd_kctrl_cfg *cfg,
			    void (*update)(struct rsnd_dai_stream *io,
					   struct rsnd_mod *mod))
{
	struct snd_soc_card *soc_card = rtd->card;
	struct snd_card *card = rtd->card->snd_card;
	struct snd_kcontrol *kctrl;
	struct snd_kcontrol_new knew = {
		.iface		= SNDRV_CTL_ELEM_IFACE_MIXER,
		.name		= name,
		.info		= rsnd_kctrl_info,
		.index		= rtd - soc_card->rtd,
		.get		= rsnd_kctrl_get,
		.put		= rsnd_kctrl_put,
		.private_value	= (unsigned long)cfg,
	};
	int ret;

	kctrl = snd_ctl_new1(&knew, mod);
	if (!kctrl)
		return -ENOMEM;

	ret = snd_ctl_add(card, kctrl);
	if (ret < 0) {
		snd_ctl_free_one(kctrl);
		return ret;
	}

	cfg->update = update;
	cfg->card = card;
	cfg->kctrl = kctrl;
	cfg->io = io;

	return 0;
}

void _rsnd_kctrl_remove(struct rsnd_kctrl_cfg *cfg)
{
	snd_ctl_remove(cfg->card, cfg->kctrl);
}

int rsnd_kctrl_new_m(struct rsnd_mod *mod,
		     struct rsnd_dai_stream *io,
		     struct snd_soc_pcm_runtime *rtd,
		     const unsigned char *name,
		     void (*update)(struct rsnd_dai_stream *io,
				    struct rsnd_mod *mod),
		     struct rsnd_kctrl_cfg_m *_cfg,
		     u32 max)
{
	_cfg->cfg.max	= max;
	_cfg->cfg.size	= RSND_DVC_CHANNELS;
	_cfg->cfg.val	= _cfg->val;
	return __rsnd_kctrl_new(mod, io, rtd, name, &_cfg->cfg, update);
}

int rsnd_kctrl_new_s(struct rsnd_mod *mod,
		     struct rsnd_dai_stream *io,
		     struct snd_soc_pcm_runtime *rtd,
		     const unsigned char *name,
		     void (*update)(struct rsnd_dai_stream *io,
				    struct rsnd_mod *mod),
		     struct rsnd_kctrl_cfg_s *_cfg,
		     u32 max)
{
	_cfg->cfg.max	= max;
	_cfg->cfg.size	= 1;
	_cfg->cfg.val	= &_cfg->val;
	return __rsnd_kctrl_new(mod, io, rtd, name, &_cfg->cfg, update);
}

int rsnd_kctrl_new_e(struct rsnd_mod *mod,
		     struct rsnd_dai_stream *io,
		     struct snd_soc_pcm_runtime *rtd,
		     const unsigned char *name,
		     struct rsnd_kctrl_cfg_s *_cfg,
		     void (*update)(struct rsnd_dai_stream *io,
				    struct rsnd_mod *mod),
		     const char * const *texts,
		     u32 max)
{
	_cfg->cfg.max	= max;
	_cfg->cfg.size	= 1;
	_cfg->cfg.val	= &_cfg->val;
	_cfg->cfg.texts	= texts;
	return __rsnd_kctrl_new(mod, io, rtd, name, &_cfg->cfg, update);
}

/*
 *		snd_soc_platform
 */

#define PREALLOC_BUFFER		(32 * 1024)
#define PREALLOC_BUFFER_MAX	(32 * 1024)

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

	return snd_pcm_lib_preallocate_pages_for_all(
		rtd->pcm,
		SNDRV_DMA_TYPE_DEV,
		rtd->card->snd_card->dev,
		PREALLOC_BUFFER, PREALLOC_BUFFER_MAX);
}

static struct snd_soc_platform_driver rsnd_soc_platform = {
	.ops		= &rsnd_pcm_ops,
	.pcm_new	= rsnd_pcm_new,
};

static const struct snd_soc_component_driver rsnd_soc_component = {
	.name		= "rsnd",
};

static int rsnd_rdai_continuance_probe(struct rsnd_priv *priv,
				       struct rsnd_dai_stream *io)
{
	int ret;

	ret = rsnd_dai_call(probe, io, priv);
	if (ret == -EAGAIN) {
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
		 * remove SRC/DVC from DAI,
		 */
		rsnd_path_remove(priv, io, src);
		rsnd_path_remove(priv, io, dvc);

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
	struct rcar_snd_info *info;
	struct rsnd_priv *priv;
	struct device *dev = &pdev->dev;
	struct rsnd_dai *rdai;
	const struct of_device_id *of_id = of_match_device(rsnd_of_match, dev);
	const struct rsnd_of_data *of_data;
	int (*probe_func[])(struct platform_device *pdev,
			    const struct rsnd_of_data *of_data,
			    struct rsnd_priv *priv) = {
		rsnd_gen_probe,
		rsnd_dma_probe,
		rsnd_ssi_probe,
		rsnd_src_probe,
		rsnd_ctu_probe,
		rsnd_mix_probe,
		rsnd_dvc_probe,
		rsnd_adg_probe,
		rsnd_dai_probe,
	};
	int ret, i;

	info = devm_kzalloc(&pdev->dev, sizeof(struct rcar_snd_info),
			    GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	of_data = of_id->data;

	/*
	 *	init priv data
	 */
	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(dev, "priv allocate failed\n");
		return -ENODEV;
	}

	priv->pdev	= pdev;
	priv->info	= info;
	spin_lock_init(&priv->lock);

	/*
	 *	init each module
	 */
	for (i = 0; i < ARRAY_SIZE(probe_func); i++) {
		ret = probe_func[i](pdev, of_data, priv);
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
	ret = snd_soc_register_platform(dev, &rsnd_soc_platform);
	if (ret < 0) {
		dev_err(dev, "cannot snd soc register\n");
		return ret;
	}

	ret = snd_soc_register_component(dev, &rsnd_soc_component,
					 priv->daidrv, rsnd_rdai_nr(priv));
	if (ret < 0) {
		dev_err(dev, "cannot snd dai register\n");
		goto exit_snd_soc;
	}

	pm_runtime_enable(dev);

	dev_info(dev, "probed\n");
	return ret;

exit_snd_soc:
	snd_soc_unregister_platform(dev);
exit_snd_probe:
	for_each_rsnd_dai(rdai, priv, i) {
		rsnd_dai_call(remove, &rdai->playback, priv);
		rsnd_dai_call(remove, &rdai->capture, priv);
	}

	return ret;
}

static int rsnd_remove(struct platform_device *pdev)
{
	struct rsnd_priv *priv = dev_get_drvdata(&pdev->dev);
	struct rsnd_dai *rdai;
	void (*remove_func[])(struct platform_device *pdev,
			      struct rsnd_priv *priv) = {
		rsnd_ssi_remove,
		rsnd_src_remove,
		rsnd_ctu_remove,
		rsnd_mix_remove,
		rsnd_dvc_remove,
	};
	int ret = 0, i;

	pm_runtime_disable(&pdev->dev);

	for_each_rsnd_dai(rdai, priv, i) {
		ret |= rsnd_dai_call(remove, &rdai->playback, priv);
		ret |= rsnd_dai_call(remove, &rdai->capture, priv);
	}

	for (i = 0; i < ARRAY_SIZE(remove_func); i++)
		remove_func[i](pdev, priv);

	snd_soc_unregister_component(&pdev->dev);
	snd_soc_unregister_platform(&pdev->dev);

	return ret;
}

static struct platform_driver rsnd_driver = {
	.driver	= {
		.name	= "rcar_sound",
		.of_match_table = rsnd_of_match,
	},
	.probe		= rsnd_probe,
	.remove		= rsnd_remove,
};
module_platform_driver(rsnd_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Renesas R-Car audio driver");
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
MODULE_ALIAS("platform:rcar-pcm-audio");
