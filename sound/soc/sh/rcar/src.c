/*
 * Renesas R-Car SRC support
 *
 * Copyright (C) 2013 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include "rsnd.h"

#define SRC_NAME "src"

/* SRCx_STATUS */
#define OUF_SRCO	((1 << 12) | (1 << 13))
#define OUF_SRCI	((1 <<  9) | (1 <<  8))

/* SCU_SYSTEM_STATUS0/1 */
#define OUF_SRC(id)	((1 << (id + 16)) | (1 << id))

struct rsnd_src {
	struct rsnd_mod mod;
	struct rsnd_mod *dma;
	struct rsnd_kctrl_cfg_s sen;  /* sync convert enable */
	struct rsnd_kctrl_cfg_s sync; /* sync convert */
	u32 convert_rate; /* sampling rate convert */
	int irq;
};

#define RSND_SRC_NAME_SIZE 16

#define rsnd_src_get(priv, id) ((struct rsnd_src *)(priv->src) + id)
#define rsnd_src_to_dma(src) ((src)->dma)
#define rsnd_src_nr(priv) ((priv)->src_nr)
#define rsnd_src_sync_is_enabled(mod) (rsnd_mod_to_src(mod)->sen.val)

#define rsnd_mod_to_src(_mod)				\
	container_of((_mod), struct rsnd_src, mod)

#define for_each_rsnd_src(pos, priv, i)				\
	for ((i) = 0;						\
	     ((i) < rsnd_src_nr(priv)) &&			\
	     ((pos) = (struct rsnd_src *)(priv)->src + i);	\
	     i++)


/*
 *		image of SRC (Sampling Rate Converter)
 *
 * 96kHz   <-> +-----+	48kHz	+-----+	 48kHz	+-------+
 * 48kHz   <-> | SRC | <------>	| SSI |	<----->	| codec |
 * 44.1kHz <-> +-----+		+-----+		+-------+
 * ...
 *
 */

/*
 * src.c is caring...
 *
 * Gen1
 *
 * [mem] -> [SRU] -> [SSI]
 *        |--------|
 *
 * Gen2
 *
 * [mem] -> [SRC] -> [SSIU] -> [SSI]
 *        |-----------------|
 */

static void rsnd_src_activation(struct rsnd_mod *mod)
{
	rsnd_mod_write(mod, SRC_SWRSR, 0);
	rsnd_mod_write(mod, SRC_SWRSR, 1);
}

static void rsnd_src_halt(struct rsnd_mod *mod)
{
	rsnd_mod_write(mod, SRC_SRCIR, 1);
	rsnd_mod_write(mod, SRC_SWRSR, 0);
}

static struct dma_chan *rsnd_src_dma_req(struct rsnd_dai_stream *io,
					 struct rsnd_mod *mod)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	int is_play = rsnd_io_is_play(io);

	return rsnd_dma_request_channel(rsnd_src_of_node(priv),
					mod,
					is_play ? "rx" : "tx");
}

static u32 rsnd_src_convert_rate(struct rsnd_dai_stream *io,
				 struct rsnd_mod *mod)
{
	struct snd_pcm_runtime *runtime = rsnd_io_to_runtime(io);
	struct rsnd_src *src = rsnd_mod_to_src(mod);
	u32 convert_rate;

	if (!runtime)
		return 0;

	if (!rsnd_src_sync_is_enabled(mod))
		return src->convert_rate;

	convert_rate = src->sync.val;

	if (!convert_rate)
		convert_rate = src->convert_rate;

	if (!convert_rate)
		convert_rate = runtime->rate;

	return convert_rate;
}

unsigned int rsnd_src_get_rate(struct rsnd_priv *priv,
			       struct rsnd_dai_stream *io,
			       int is_in)
{
	struct rsnd_mod *src_mod = rsnd_io_to_mod_src(io);
	struct snd_pcm_runtime *runtime = rsnd_io_to_runtime(io);
	unsigned int rate = 0;
	int is_play = rsnd_io_is_play(io);

	/*
	 *
	 * Playback
	 * runtime_rate -> [SRC] -> convert_rate
	 *
	 * Capture
	 * convert_rate -> [SRC] -> runtime_rate
	 */

	if (is_play == is_in)
		return runtime->rate;

	/*
	 * return convert rate if SRC is used,
	 * otherwise, return runtime->rate as usual
	 */
	if (src_mod)
		rate = rsnd_src_convert_rate(io, src_mod);

	if (!rate)
		rate = runtime->rate;

	return rate;
}

static int rsnd_src_hw_params(struct rsnd_mod *mod,
			      struct rsnd_dai_stream *io,
			      struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *fe_params)
{
	struct rsnd_src *src = rsnd_mod_to_src(mod);
	struct snd_soc_pcm_runtime *fe = substream->private_data;

	/*
	 * SRC assumes that it is used under DPCM if user want to use
	 * sampling rate convert. Then, SRC should be FE.
	 * And then, this function will be called *after* BE settings.
	 * this means, each BE already has fixuped hw_params.
	 * see
	 *	dpcm_fe_dai_hw_params()
	 *	dpcm_be_dai_hw_params()
	 */
	if (fe->dai_link->dynamic) {
		int stream = substream->stream;
		struct snd_soc_dpcm *dpcm;
		struct snd_pcm_hw_params *be_params;

		list_for_each_entry(dpcm, &fe->dpcm[stream].be_clients, list_be) {
			be_params = &dpcm->hw_params;

			if (params_rate(fe_params) != params_rate(be_params))
				src->convert_rate = params_rate(be_params);
		}
	}

	return 0;
}

static void rsnd_src_set_convert_rate(struct rsnd_dai_stream *io,
				      struct rsnd_mod *mod)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct device *dev = rsnd_priv_to_dev(priv);
	struct snd_pcm_runtime *runtime = rsnd_io_to_runtime(io);
	u32 fin, fout;
	u32 ifscr, fsrate, adinr;
	u32 cr, route;
	u32 bsdsr, bsisr;
	uint ratio;

	if (!runtime)
		return;

	fin  = rsnd_src_get_in_rate(priv, io);
	fout = rsnd_src_get_out_rate(priv, io);

	/* 6 - 1/6 are very enough ratio for SRC_BSDSR */
	if (fin == fout)
		ratio = 0;
	else if (fin > fout)
		ratio = 100 * fin / fout;
	else
		ratio = 100 * fout / fin;

	if (ratio > 600) {
		dev_err(dev, "FSO/FSI ratio error\n");
		return;
	}

	/*
	 *	SRC_ADINR
	 */
	adinr = rsnd_get_adinr_bit(mod, io) |
		rsnd_runtime_channel_original(io);

	/*
	 *	SRC_IFSCR / SRC_IFSVR
	 */
	ifscr = 0;
	fsrate = 0;
	if (fin != fout) {
		u64 n;

		ifscr = 1;
		n = (u64)0x0400000 * fin;
		do_div(n, fout);
		fsrate = n;
	}

	/*
	 *	SRC_SRCCR / SRC_ROUTE_MODE0
	 */
	cr	= 0x00011110;
	route	= 0x0;
	if (fin != fout) {
		route	= 0x1;

		if (rsnd_src_sync_is_enabled(mod)) {
			cr |= 0x1;
			route |= rsnd_io_is_play(io) ?
				(0x1 << 24) : (0x1 << 25);
		}
	}

	/*
	 * SRC_BSDSR / SRC_BSISR
	 */
	switch (rsnd_mod_id(mod)) {
	case 5:
	case 6:
	case 7:
	case 8:
		bsdsr = 0x02400000; /* 6 - 1/6 */
		bsisr = 0x00100060; /* 6 - 1/6 */
		break;
	default:
		bsdsr = 0x01800000; /* 6 - 1/6 */
		bsisr = 0x00100060 ;/* 6 - 1/6 */
		break;
	}

	rsnd_mod_write(mod, SRC_ROUTE_MODE0, route);

	rsnd_mod_write(mod, SRC_SRCIR, 1);	/* initialize */
	rsnd_mod_write(mod, SRC_ADINR, adinr);
	rsnd_mod_write(mod, SRC_IFSCR, ifscr);
	rsnd_mod_write(mod, SRC_IFSVR, fsrate);
	rsnd_mod_write(mod, SRC_SRCCR, cr);
	rsnd_mod_write(mod, SRC_BSDSR, bsdsr);
	rsnd_mod_write(mod, SRC_BSISR, bsisr);
	rsnd_mod_write(mod, SRC_SRCIR, 0);	/* cancel initialize */

	rsnd_mod_write(mod, SRC_I_BUSIF_MODE, 1);
	rsnd_mod_write(mod, SRC_O_BUSIF_MODE, 1);
	rsnd_mod_write(mod, SRC_BUSIF_DALIGN, rsnd_get_dalign(mod, io));

	rsnd_adg_set_src_timesel_gen2(mod, io, fin, fout);
}

static int rsnd_src_irq(struct rsnd_mod *mod,
			struct rsnd_dai_stream *io,
			struct rsnd_priv *priv,
			int enable)
{
	struct rsnd_src *src = rsnd_mod_to_src(mod);
	u32 sys_int_val, int_val, sys_int_mask;
	int irq = src->irq;
	int id = rsnd_mod_id(mod);

	sys_int_val =
	sys_int_mask = OUF_SRC(id);
	int_val = 0x3300;

	/*
	 * IRQ is not supported on non-DT
	 * see
	 *	rsnd_src_probe_()
	 */
	if ((irq <= 0) || !enable) {
		sys_int_val = 0;
		int_val = 0;
	}

	/*
	 * WORKAROUND
	 *
	 * ignore over flow error when rsnd_src_sync_is_enabled()
	 */
	if (rsnd_src_sync_is_enabled(mod))
		sys_int_val = sys_int_val & 0xffff;

	rsnd_mod_write(mod, SRC_INT_ENABLE0, int_val);
	rsnd_mod_bset(mod, SCU_SYS_INT_EN0, sys_int_mask, sys_int_val);
	rsnd_mod_bset(mod, SCU_SYS_INT_EN1, sys_int_mask, sys_int_val);

	return 0;
}

static void rsnd_src_status_clear(struct rsnd_mod *mod)
{
	u32 val = OUF_SRC(rsnd_mod_id(mod));

	rsnd_mod_bset(mod, SCU_SYS_STATUS0, val, val);
	rsnd_mod_bset(mod, SCU_SYS_STATUS1, val, val);
}

static bool rsnd_src_error_occurred(struct rsnd_mod *mod)
{
	u32 val0, val1;
	bool ret = false;

	val0 = val1 = OUF_SRC(rsnd_mod_id(mod));

	/*
	 * WORKAROUND
	 *
	 * ignore over flow error when rsnd_src_sync_is_enabled()
	 */
	if (rsnd_src_sync_is_enabled(mod))
		val0 = val0 & 0xffff;

	if ((rsnd_mod_read(mod, SCU_SYS_STATUS0) & val0) ||
	    (rsnd_mod_read(mod, SCU_SYS_STATUS1) & val1))
		ret = true;

	return ret;
}

static int rsnd_src_start(struct rsnd_mod *mod,
			  struct rsnd_dai_stream *io,
			  struct rsnd_priv *priv)
{
	u32 val;

	/*
	 * WORKAROUND
	 *
	 * Enable SRC output if you want to use sync convert together with DVC
	 */
	val = (rsnd_io_to_mod_dvc(io) && !rsnd_src_sync_is_enabled(mod)) ?
		0x01 : 0x11;

	rsnd_mod_write(mod, SRC_CTRL, val);

	return 0;
}

static int rsnd_src_stop(struct rsnd_mod *mod,
			 struct rsnd_dai_stream *io,
			 struct rsnd_priv *priv)
{
	rsnd_mod_write(mod, SRC_CTRL, 0);

	return 0;
}

static int rsnd_src_init(struct rsnd_mod *mod,
			 struct rsnd_dai_stream *io,
			 struct rsnd_priv *priv)
{
	struct rsnd_src *src = rsnd_mod_to_src(mod);

	rsnd_mod_power_on(mod);

	rsnd_src_activation(mod);

	rsnd_src_set_convert_rate(io, mod);

	rsnd_src_status_clear(mod);

	/* reset sync convert_rate */
	src->sync.val = 0;

	return 0;
}

static int rsnd_src_quit(struct rsnd_mod *mod,
			 struct rsnd_dai_stream *io,
			 struct rsnd_priv *priv)
{
	struct rsnd_src *src = rsnd_mod_to_src(mod);

	rsnd_src_halt(mod);

	rsnd_mod_power_off(mod);

	src->convert_rate = 0;

	/* reset sync convert_rate */
	src->sync.val = 0;

	return 0;
}

static void __rsnd_src_interrupt(struct rsnd_mod *mod,
				 struct rsnd_dai_stream *io)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	bool stop = false;

	spin_lock(&priv->lock);

	/* ignore all cases if not working */
	if (!rsnd_io_is_working(io))
		goto rsnd_src_interrupt_out;

	if (rsnd_src_error_occurred(mod))
		stop = true;

	rsnd_src_status_clear(mod);
rsnd_src_interrupt_out:

	spin_unlock(&priv->lock);

	if (stop)
		snd_pcm_stop_xrun(io->substream);
}

static irqreturn_t rsnd_src_interrupt(int irq, void *data)
{
	struct rsnd_mod *mod = data;

	rsnd_mod_interrupt(mod, __rsnd_src_interrupt);

	return IRQ_HANDLED;
}

static int rsnd_src_probe_(struct rsnd_mod *mod,
			   struct rsnd_dai_stream *io,
			   struct rsnd_priv *priv)
{
	struct rsnd_src *src = rsnd_mod_to_src(mod);
	struct device *dev = rsnd_priv_to_dev(priv);
	int irq = src->irq;
	int ret;

	if (irq > 0) {
		/*
		 * IRQ is not supported on non-DT
		 * see
		 *	rsnd_src_irq()
		 */
		ret = devm_request_irq(dev, irq,
				       rsnd_src_interrupt,
				       IRQF_SHARED,
				       dev_name(dev), mod);
		if (ret)
			return ret;
	}

	ret = rsnd_dma_attach(io, mod, &src->dma);

	return ret;
}

static int rsnd_src_pcm_new(struct rsnd_mod *mod,
			    struct rsnd_dai_stream *io,
			    struct snd_soc_pcm_runtime *rtd)
{
	struct rsnd_src *src = rsnd_mod_to_src(mod);
	int ret;

	/*
	 * enable SRC sync convert if possible
	 */

	/*
	 * It can't use SRC Synchronous convert
	 * when Capture if it uses CMD
	 */
	if (rsnd_io_to_mod_cmd(io) && !rsnd_io_is_play(io))
		return 0;

	/*
	 * enable sync convert
	 */
	ret = rsnd_kctrl_new_s(mod, io, rtd,
			       rsnd_io_is_play(io) ?
			       "SRC Out Rate Switch" :
			       "SRC In Rate Switch",
			       rsnd_src_set_convert_rate,
			       &src->sen, 1);
	if (ret < 0)
		return ret;

	ret = rsnd_kctrl_new_s(mod, io, rtd,
			       rsnd_io_is_play(io) ?
			       "SRC Out Rate" :
			       "SRC In Rate",
			       rsnd_src_set_convert_rate,
			       &src->sync, 192000);

	return ret;
}

static struct rsnd_mod_ops rsnd_src_ops = {
	.name	= SRC_NAME,
	.dma_req = rsnd_src_dma_req,
	.probe	= rsnd_src_probe_,
	.init	= rsnd_src_init,
	.quit	= rsnd_src_quit,
	.start	= rsnd_src_start,
	.stop	= rsnd_src_stop,
	.irq	= rsnd_src_irq,
	.hw_params = rsnd_src_hw_params,
	.pcm_new = rsnd_src_pcm_new,
};

struct rsnd_mod *rsnd_src_mod_get(struct rsnd_priv *priv, int id)
{
	if (WARN_ON(id < 0 || id >= rsnd_src_nr(priv)))
		id = 0;

	return rsnd_mod_get(rsnd_src_get(priv, id));
}

int rsnd_src_probe(struct rsnd_priv *priv)
{
	struct device_node *node;
	struct device_node *np;
	struct device *dev = rsnd_priv_to_dev(priv);
	struct rsnd_src *src;
	struct clk *clk;
	char name[RSND_SRC_NAME_SIZE];
	int i, nr, ret;

	/* This driver doesn't support Gen1 at this point */
	if (rsnd_is_gen1(priv))
		return 0;

	node = rsnd_src_of_node(priv);
	if (!node)
		return 0; /* not used is not error */

	nr = of_get_child_count(node);
	if (!nr) {
		ret = -EINVAL;
		goto rsnd_src_probe_done;
	}

	src	= devm_kzalloc(dev, sizeof(*src) * nr, GFP_KERNEL);
	if (!src) {
		ret = -ENOMEM;
		goto rsnd_src_probe_done;
	}

	priv->src_nr	= nr;
	priv->src	= src;

	i = 0;
	for_each_child_of_node(node, np) {
		if (!of_device_is_available(np))
			goto skip;

		src = rsnd_src_get(priv, i);

		snprintf(name, RSND_SRC_NAME_SIZE, "%s.%d",
			 SRC_NAME, i);

		src->irq = irq_of_parse_and_map(np, 0);
		if (!src->irq) {
			ret = -EINVAL;
			goto rsnd_src_probe_done;
		}

		clk = devm_clk_get(dev, name);
		if (IS_ERR(clk)) {
			ret = PTR_ERR(clk);
			goto rsnd_src_probe_done;
		}

		ret = rsnd_mod_init(priv, rsnd_mod_get(src),
				    &rsnd_src_ops, clk, rsnd_mod_get_status,
				    RSND_MOD_SRC, i);
		if (ret)
			goto rsnd_src_probe_done;

skip:
		i++;
	}

	ret = 0;

rsnd_src_probe_done:
	of_node_put(node);

	return ret;
}

void rsnd_src_remove(struct rsnd_priv *priv)
{
	struct rsnd_src *src;
	int i;

	for_each_rsnd_src(src, priv, i) {
		rsnd_mod_quit(rsnd_mod_get(src));
	}
}
