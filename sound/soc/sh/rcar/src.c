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
	struct rsnd_src_platform_info *info; /* rcar_snd.h */
	struct rsnd_mod mod;
	struct rsnd_kctrl_cfg_s sen;  /* sync convert enable */
	struct rsnd_kctrl_cfg_s sync; /* sync convert */
	u32 convert_rate; /* sampling rate convert */
	int err;
};

#define RSND_SRC_NAME_SIZE 16

#define rsnd_src_nr(priv) ((priv)->src_nr)
#define rsnd_enable_sync_convert(src) ((src)->sen.val)
#define rsnd_src_of_node(priv) \
	of_get_child_by_name(rsnd_priv_to_dev(priv)->of_node, "rcar_sound,src")

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

/*
 *	How to use SRC bypass mode for debugging
 *
 * SRC has bypass mode, and it is useful for debugging.
 * In Gen2 case,
 * SRCm_MODE controls whether SRC is used or not
 * SSI_MODE0 controls whether SSIU which receives SRC data
 * is used or not.
 * Both SRCm_MODE/SSI_MODE0 settings are needed if you use SRC,
 * but SRC bypass mode needs SSI_MODE0 only.
 *
 * This driver request
 * struct rsnd_src_platform_info {
 *	u32 convert_rate;
 *	int dma_id;
 * }
 *
 * rsnd_src_convert_rate() indicates
 * above convert_rate, and it controls
 * whether SRC is used or not.
 *
 * ex) doesn't use SRC
 * static struct rsnd_dai_platform_info rsnd_dai = {
 *	.playback = { .ssi = &rsnd_ssi[0], },
 * };
 *
 * ex) uses SRC
 * static struct rsnd_src_platform_info rsnd_src[] = {
 *	RSND_SCU(48000, 0),
 *	...
 * };
 * static struct rsnd_dai_platform_info rsnd_dai = {
 *	.playback = { .ssi = &rsnd_ssi[0], .src = &rsnd_src[0] },
 * };
 *
 * ex) uses SRC bypass mode
 * static struct rsnd_src_platform_info rsnd_src[] = {
 *	RSND_SCU(0, 0),
 *	...
 * };
 * static struct rsnd_dai_platform_info rsnd_dai = {
 *	.playback = { .ssi = &rsnd_ssi[0], .src = &rsnd_src[0] },
 * };
 *
 */

/*
 *		Gen1/Gen2 common functions
 */
static void rsnd_src_soft_reset(struct rsnd_mod *mod)
{
	rsnd_mod_write(mod, SRC_SWRSR, 0);
	rsnd_mod_write(mod, SRC_SWRSR, 1);
}


#define rsnd_src_initialize_lock(mod)	__rsnd_src_initialize_lock(mod, 1)
#define rsnd_src_initialize_unlock(mod)	__rsnd_src_initialize_lock(mod, 0)
static void __rsnd_src_initialize_lock(struct rsnd_mod *mod, u32 enable)
{
	rsnd_mod_write(mod, SRC_SRCIR, enable);
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

int rsnd_src_ssiu_start(struct rsnd_mod *ssi_mod,
			struct rsnd_dai_stream *io,
			int use_busif)
{
	struct rsnd_dai *rdai = rsnd_io_to_rdai(io);
	int ssi_id = rsnd_mod_id(ssi_mod);

	/*
	 * SSI_MODE0
	 */
	rsnd_mod_bset(ssi_mod, SSI_MODE0, (1 << ssi_id),
		      !use_busif << ssi_id);

	/*
	 * SSI_MODE1
	 */
	if (rsnd_ssi_is_pin_sharing(ssi_mod)) {
		int shift = -1;
		switch (ssi_id) {
		case 1:
			shift = 0;
			break;
		case 2:
			shift = 2;
			break;
		case 4:
			shift = 16;
			break;
		}

		if (shift >= 0)
			rsnd_mod_bset(ssi_mod, SSI_MODE1,
				      0x3 << shift,
				      rsnd_rdai_is_clk_master(rdai) ?
				      0x2 << shift : 0x1 << shift);
	}

	/*
	 * DMA settings for SSIU
	 */
	if (use_busif) {
		u32 val = rsnd_get_dalign(ssi_mod, io);

		rsnd_mod_write(ssi_mod, SSI_BUSIF_ADINR,
			       rsnd_get_adinr_bit(ssi_mod, io));
		rsnd_mod_write(ssi_mod, SSI_BUSIF_MODE,  1);
		rsnd_mod_write(ssi_mod, SSI_CTRL, 0x1);

		rsnd_mod_write(ssi_mod, SSI_BUSIF_DALIGN, val);
	}

	return 0;
}

int rsnd_src_ssiu_stop(struct rsnd_mod *ssi_mod,
		       struct rsnd_dai_stream *io)
{
	/*
	 * DMA settings for SSIU
	 */
	rsnd_mod_write(ssi_mod, SSI_CTRL, 0);

	return 0;
}

int rsnd_src_ssi_irq_enable(struct rsnd_mod *ssi_mod)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(ssi_mod);

	if (rsnd_is_gen1(priv))
		return 0;

	/* enable SSI interrupt if Gen2 */
	rsnd_mod_write(ssi_mod, SSI_INT_ENABLE,
		       rsnd_ssi_is_dma_mode(ssi_mod) ?
		       0x0e000000 : 0x0f000000);

	return 0;
}

int rsnd_src_ssi_irq_disable(struct rsnd_mod *ssi_mod)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(ssi_mod);

	if (rsnd_is_gen1(priv))
		return 0;

	/* disable SSI interrupt if Gen2 */
	rsnd_mod_write(ssi_mod, SSI_INT_ENABLE, 0x00000000);

	return 0;
}

static u32 rsnd_src_convert_rate(struct rsnd_dai_stream *io,
				 struct rsnd_src *src)
{
	struct snd_pcm_runtime *runtime = rsnd_io_to_runtime(io);
	u32 convert_rate;

	if (!runtime)
		return 0;

	if (!rsnd_enable_sync_convert(src))
		return src->convert_rate;

	convert_rate = src->sync.val;

	if (!convert_rate)
		convert_rate = src->convert_rate;

	if (!convert_rate)
		convert_rate = runtime->rate;

	return convert_rate;
}

unsigned int rsnd_src_get_ssi_rate(struct rsnd_priv *priv,
				   struct rsnd_dai_stream *io,
				   struct snd_pcm_runtime *runtime)
{
	struct rsnd_mod *src_mod = rsnd_io_to_mod_src(io);
	struct rsnd_src *src;
	unsigned int rate = 0;

	if (src_mod) {
		src = rsnd_mod_to_src(src_mod);

		/*
		 * return convert rate if SRC is used,
		 * otherwise, return runtime->rate as usual
		 */
		rate = rsnd_src_convert_rate(io, src);
	}

	if (!rate)
		rate = runtime->rate;

	return rate;
}

static int rsnd_src_set_convert_rate(struct rsnd_mod *mod,
				     struct rsnd_dai_stream *io)
{
	struct snd_pcm_runtime *runtime = rsnd_io_to_runtime(io);
	struct rsnd_src *src = rsnd_mod_to_src(mod);
	u32 convert_rate = rsnd_src_convert_rate(io, src);
	u32 fsrate = 0;

	if (convert_rate)
		fsrate = 0x0400000 / convert_rate * runtime->rate;

	/* Set channel number and output bit length */
	rsnd_mod_write(mod, SRC_ADINR, rsnd_get_adinr_bit(mod, io));

	/* Enable the initial value of IFS */
	if (fsrate) {
		rsnd_mod_write(mod, SRC_IFSCR, 1);

		/* Set initial value of IFS */
		rsnd_mod_write(mod, SRC_IFSVR, fsrate);
	}

	/* use DMA transfer */
	rsnd_mod_write(mod, SRC_BUSIF_MODE, 1);

	return 0;
}

static int rsnd_src_hw_params(struct rsnd_mod *mod,
			      struct rsnd_dai_stream *io,
			      struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *fe_params)
{
	struct rsnd_src *src = rsnd_mod_to_src(mod);
	struct snd_soc_pcm_runtime *fe = substream->private_data;

	/* default value (mainly for non-DT) */
	src->convert_rate = src->info->convert_rate;

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

static int rsnd_src_init(struct rsnd_mod *mod,
			 struct rsnd_priv *priv)
{
	struct rsnd_src *src = rsnd_mod_to_src(mod);

	rsnd_mod_hw_start(mod);

	rsnd_src_soft_reset(mod);

	rsnd_src_initialize_lock(mod);

	src->err = 0;

	/* reset sync convert_rate */
	src->sync.val = 0;

	return 0;
}

static int rsnd_src_quit(struct rsnd_mod *mod,
			 struct rsnd_dai_stream *io,
			 struct rsnd_priv *priv)
{
	struct rsnd_src *src = rsnd_mod_to_src(mod);
	struct device *dev = rsnd_priv_to_dev(priv);

	rsnd_mod_hw_stop(mod);

	if (src->err)
		dev_warn(dev, "%s[%d] under/over flow err = %d\n",
			 rsnd_mod_name(mod), rsnd_mod_id(mod), src->err);

	src->convert_rate = 0;

	/* reset sync convert_rate */
	src->sync.val = 0;

	return 0;
}

static int rsnd_src_start(struct rsnd_mod *mod)
{
	rsnd_src_initialize_unlock(mod);

	return 0;
}

static int rsnd_src_stop(struct rsnd_mod *mod)
{
	/* nothing to do */
	return 0;
}

/*
 *		Gen1 functions
 */
static int rsnd_src_set_route_gen1(struct rsnd_dai_stream *io,
				   struct rsnd_mod *mod)
{
	struct src_route_config {
		u32 mask;
		int shift;
	} routes[] = {
		{ 0xF,  0, }, /* 0 */
		{ 0xF,  4, }, /* 1 */
		{ 0xF,  8, }, /* 2 */
		{ 0x7, 12, }, /* 3 */
		{ 0x7, 16, }, /* 4 */
		{ 0x7, 20, }, /* 5 */
		{ 0x7, 24, }, /* 6 */
		{ 0x3, 28, }, /* 7 */
		{ 0x3, 30, }, /* 8 */
	};
	u32 mask;
	u32 val;
	int id;

	id = rsnd_mod_id(mod);
	if (id < 0 || id >= ARRAY_SIZE(routes))
		return -EIO;

	/*
	 * SRC_ROUTE_SELECT
	 */
	val = rsnd_io_is_play(io) ? 0x1 : 0x2;
	val = val		<< routes[id].shift;
	mask = routes[id].mask	<< routes[id].shift;

	rsnd_mod_bset(mod, SRC_ROUTE_SEL, mask, val);

	return 0;
}

static int rsnd_src_set_convert_timing_gen1(struct rsnd_dai_stream *io,
					    struct rsnd_mod *mod)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct rsnd_src *src = rsnd_mod_to_src(mod);
	struct snd_pcm_runtime *runtime = rsnd_io_to_runtime(io);
	u32 convert_rate = rsnd_src_convert_rate(io, src);
	u32 mask;
	u32 val;
	int shift;
	int id = rsnd_mod_id(mod);
	int ret;

	/*
	 * SRC_TIMING_SELECT
	 */
	shift	= (id % 4) * 8;
	mask	= 0x1F << shift;

	/*
	 * ADG is used as source clock if SRC was used,
	 * then, SSI WS is used as destination clock.
	 * SSI WS is used as source clock if SRC is not used
	 * (when playback, source/destination become reverse when capture)
	 */
	ret = 0;
	if (convert_rate) {
		/* use ADG */
		val = 0;
		ret = rsnd_adg_set_convert_clk_gen1(priv, mod,
						    runtime->rate,
						    convert_rate);
	} else if (8 == id) {
		/* use SSI WS, but SRU8 is special */
		val = id << shift;
	} else {
		/* use SSI WS */
		val = (id + 1) << shift;
	}

	if (ret < 0)
		return ret;

	switch (id / 4) {
	case 0:
		rsnd_mod_bset(mod, SRC_TMG_SEL0, mask, val);
		break;
	case 1:
		rsnd_mod_bset(mod, SRC_TMG_SEL1, mask, val);
		break;
	case 2:
		rsnd_mod_bset(mod, SRC_TMG_SEL2, mask, val);
		break;
	}

	return 0;
}

static int rsnd_src_set_convert_rate_gen1(struct rsnd_mod *mod,
					  struct rsnd_dai_stream *io)
{
	struct rsnd_src *src = rsnd_mod_to_src(mod);
	int ret;

	ret = rsnd_src_set_convert_rate(mod, io);
	if (ret < 0)
		return ret;

	/* Select SRC mode (fixed value) */
	rsnd_mod_write(mod, SRC_SRCCR, 0x00010110);

	/* Set the restriction value of the FS ratio (98%) */
	rsnd_mod_write(mod, SRC_MNFSR,
		       rsnd_mod_read(mod, SRC_IFSVR) / 100 * 98);

	/* Gen1/Gen2 are not compatible */
	if (rsnd_src_convert_rate(io, src))
		rsnd_mod_write(mod, SRC_ROUTE_MODE0, 1);

	/* no SRC_BFSSR settings, since SRC_SRCCR::BUFMD is 0 */

	return 0;
}

static int rsnd_src_init_gen1(struct rsnd_mod *mod,
			      struct rsnd_dai_stream *io,
			      struct rsnd_priv *priv)
{
	int ret;

	ret = rsnd_src_init(mod, priv);
	if (ret < 0)
		return ret;

	ret = rsnd_src_set_route_gen1(io, mod);
	if (ret < 0)
		return ret;

	ret = rsnd_src_set_convert_rate_gen1(mod, io);
	if (ret < 0)
		return ret;

	ret = rsnd_src_set_convert_timing_gen1(io, mod);
	if (ret < 0)
		return ret;

	return 0;
}

static int rsnd_src_start_gen1(struct rsnd_mod *mod,
			       struct rsnd_dai_stream *io,
			       struct rsnd_priv *priv)
{
	int id = rsnd_mod_id(mod);

	rsnd_mod_bset(mod, SRC_ROUTE_CTRL, (1 << id), (1 << id));

	return rsnd_src_start(mod);
}

static int rsnd_src_stop_gen1(struct rsnd_mod *mod,
			      struct rsnd_dai_stream *io,
			      struct rsnd_priv *priv)
{
	int id = rsnd_mod_id(mod);

	rsnd_mod_bset(mod, SRC_ROUTE_CTRL, (1 << id), 0);

	return rsnd_src_stop(mod);
}

static struct rsnd_mod_ops rsnd_src_gen1_ops = {
	.name	= SRC_NAME,
	.dma_req = rsnd_src_dma_req,
	.init	= rsnd_src_init_gen1,
	.quit	= rsnd_src_quit,
	.start	= rsnd_src_start_gen1,
	.stop	= rsnd_src_stop_gen1,
	.hw_params = rsnd_src_hw_params,
};

/*
 *		Gen2 functions
 */
#define rsnd_src_irq_enable_gen2(mod)  rsnd_src_irq_ctrol_gen2(mod, 1)
#define rsnd_src_irq_disable_gen2(mod) rsnd_src_irq_ctrol_gen2(mod, 0)
static void rsnd_src_irq_ctrol_gen2(struct rsnd_mod *mod, int enable)
{
	struct rsnd_src *src = rsnd_mod_to_src(mod);
	u32 sys_int_val, int_val, sys_int_mask;
	int irq = src->info->irq;
	int id = rsnd_mod_id(mod);

	sys_int_val =
	sys_int_mask = OUF_SRC(id);
	int_val = 0x3300;

	/*
	 * IRQ is not supported on non-DT
	 * see
	 *	rsnd_src_probe_gen2()
	 */
	if ((irq <= 0) || !enable) {
		sys_int_val = 0;
		int_val = 0;
	}

	/*
	 * WORKAROUND
	 *
	 * ignore over flow error when rsnd_enable_sync_convert()
	 */
	if (rsnd_enable_sync_convert(src))
		sys_int_val = sys_int_val & 0xffff;

	rsnd_mod_write(mod, SRC_INT_ENABLE0, int_val);
	rsnd_mod_bset(mod, SCU_SYS_INT_EN0, sys_int_mask, sys_int_val);
	rsnd_mod_bset(mod, SCU_SYS_INT_EN1, sys_int_mask, sys_int_val);
}

static void rsnd_src_error_clear_gen2(struct rsnd_mod *mod)
{
	u32 val = OUF_SRC(rsnd_mod_id(mod));

	rsnd_mod_bset(mod, SCU_SYS_STATUS0, val, val);
	rsnd_mod_bset(mod, SCU_SYS_STATUS1, val, val);
}

static bool rsnd_src_error_record_gen2(struct rsnd_mod *mod)
{
	struct rsnd_src *src = rsnd_mod_to_src(mod);
	u32 val0, val1;
	bool ret = false;

	val0 = val1 = OUF_SRC(rsnd_mod_id(mod));

	/*
	 * WORKAROUND
	 *
	 * ignore over flow error when rsnd_enable_sync_convert()
	 */
	if (rsnd_enable_sync_convert(src))
		val0 = val0 & 0xffff;

	if ((rsnd_mod_read(mod, SCU_SYS_STATUS0) & val0) ||
	    (rsnd_mod_read(mod, SCU_SYS_STATUS1) & val1)) {
		struct rsnd_src *src = rsnd_mod_to_src(mod);

		src->err++;
		ret = true;
	}

	/* clear error static */
	rsnd_src_error_clear_gen2(mod);

	return ret;
}

static int _rsnd_src_start_gen2(struct rsnd_mod *mod,
				struct rsnd_dai_stream *io)
{
	struct rsnd_src *src = rsnd_mod_to_src(mod);
	u32 val;

	val = rsnd_get_dalign(mod, io);

	rsnd_mod_write(mod, SRC_BUSIF_DALIGN, val);

	/*
	 * WORKAROUND
	 *
	 * Enable SRC output if you want to use sync convert together with DVC
	 */
	val = (rsnd_io_to_mod_dvc(io) && !rsnd_enable_sync_convert(src)) ?
		0x01 : 0x11;

	rsnd_mod_write(mod, SRC_CTRL, val);

	rsnd_src_error_clear_gen2(mod);

	rsnd_src_start(mod);

	rsnd_src_irq_enable_gen2(mod);

	return 0;
}

static int _rsnd_src_stop_gen2(struct rsnd_mod *mod)
{
	rsnd_src_irq_disable_gen2(mod);

	rsnd_mod_write(mod, SRC_CTRL, 0);

	rsnd_src_error_record_gen2(mod);

	return rsnd_src_stop(mod);
}

static void __rsnd_src_interrupt_gen2(struct rsnd_mod *mod,
				      struct rsnd_dai_stream *io)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);

	spin_lock(&priv->lock);

	/* ignore all cases if not working */
	if (!rsnd_io_is_working(io))
		goto rsnd_src_interrupt_gen2_out;

	if (rsnd_src_error_record_gen2(mod)) {
		struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
		struct rsnd_src *src = rsnd_mod_to_src(mod);
		struct device *dev = rsnd_priv_to_dev(priv);

		dev_dbg(dev, "%s[%d] restart\n",
			rsnd_mod_name(mod), rsnd_mod_id(mod));

		_rsnd_src_stop_gen2(mod);
		if (src->err < 1024)
			_rsnd_src_start_gen2(mod, io);
		else
			dev_warn(dev, "no more SRC restart\n");
	}

rsnd_src_interrupt_gen2_out:
	spin_unlock(&priv->lock);
}

static irqreturn_t rsnd_src_interrupt_gen2(int irq, void *data)
{
	struct rsnd_mod *mod = data;

	rsnd_mod_interrupt(mod, __rsnd_src_interrupt_gen2);

	return IRQ_HANDLED;
}

static int rsnd_src_set_convert_rate_gen2(struct rsnd_mod *mod,
					  struct rsnd_dai_stream *io)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct device *dev = rsnd_priv_to_dev(priv);
	struct snd_pcm_runtime *runtime = rsnd_io_to_runtime(io);
	struct rsnd_src *src = rsnd_mod_to_src(mod);
	u32 convert_rate = rsnd_src_convert_rate(io, src);
	u32 cr, route;
	uint ratio;
	int ret;

	/* 6 - 1/6 are very enough ratio for SRC_BSDSR */
	if (!convert_rate)
		ratio = 0;
	else if (convert_rate > runtime->rate)
		ratio = 100 * convert_rate / runtime->rate;
	else
		ratio = 100 * runtime->rate / convert_rate;

	if (ratio > 600) {
		dev_err(dev, "FSO/FSI ratio error\n");
		return -EINVAL;
	}

	ret = rsnd_src_set_convert_rate(mod, io);
	if (ret < 0)
		return ret;

	cr	= 0x00011110;
	route	= 0x0;
	if (convert_rate) {
		route	= 0x1;

		if (rsnd_enable_sync_convert(src)) {
			cr |= 0x1;
			route |= rsnd_io_is_play(io) ?
				(0x1 << 24) : (0x1 << 25);
		}
	}

	rsnd_mod_write(mod, SRC_SRCCR, cr);
	rsnd_mod_write(mod, SRC_ROUTE_MODE0, route);

	switch (rsnd_mod_id(mod)) {
	case 5:
	case 6:
	case 7:
	case 8:
		rsnd_mod_write(mod, SRC_BSDSR, 0x02400000);
		break;
	default:
		rsnd_mod_write(mod, SRC_BSDSR, 0x01800000);
		break;
	}

	rsnd_mod_write(mod, SRC_BSISR, 0x00100060);

	return 0;
}

static int rsnd_src_set_convert_timing_gen2(struct rsnd_dai_stream *io,
					    struct rsnd_mod *mod)
{
	struct snd_pcm_runtime *runtime = rsnd_io_to_runtime(io);
	struct rsnd_src *src = rsnd_mod_to_src(mod);
	u32 convert_rate = rsnd_src_convert_rate(io, src);
	int ret;

	if (convert_rate)
		ret = rsnd_adg_set_convert_clk_gen2(mod, io,
						    runtime->rate,
						    convert_rate);
	else
		ret = rsnd_adg_set_convert_timing_gen2(mod, io);

	return ret;
}

static int rsnd_src_probe_gen2(struct rsnd_mod *mod,
			       struct rsnd_dai_stream *io,
			       struct rsnd_priv *priv)
{
	struct rsnd_src *src = rsnd_mod_to_src(mod);
	struct device *dev = rsnd_priv_to_dev(priv);
	int irq = src->info->irq;
	int ret;

	if (irq > 0) {
		/*
		 * IRQ is not supported on non-DT
		 * see
		 *	rsnd_src_irq_enable_gen2()
		 */
		ret = devm_request_irq(dev, irq,
				       rsnd_src_interrupt_gen2,
				       IRQF_SHARED,
				       dev_name(dev), mod);
		if (ret)
			return ret;
	}

	ret = rsnd_dma_init(io,
			    rsnd_mod_to_dma(mod),
			    src->info->dma_id);

	return ret;
}

static int rsnd_src_remove_gen2(struct rsnd_mod *mod,
				struct rsnd_dai_stream *io,
				struct rsnd_priv *priv)
{
	rsnd_dma_quit(io, rsnd_mod_to_dma(mod));

	return 0;
}

static int rsnd_src_init_gen2(struct rsnd_mod *mod,
			      struct rsnd_dai_stream *io,
			      struct rsnd_priv *priv)
{
	int ret;

	ret = rsnd_src_init(mod, priv);
	if (ret < 0)
		return ret;

	ret = rsnd_src_set_convert_rate_gen2(mod, io);
	if (ret < 0)
		return ret;

	ret = rsnd_src_set_convert_timing_gen2(io, mod);
	if (ret < 0)
		return ret;

	return 0;
}

static int rsnd_src_start_gen2(struct rsnd_mod *mod,
			       struct rsnd_dai_stream *io,
			       struct rsnd_priv *priv)
{
	rsnd_dma_start(io, rsnd_mod_to_dma(mod));

	return _rsnd_src_start_gen2(mod, io);
}

static int rsnd_src_stop_gen2(struct rsnd_mod *mod,
			      struct rsnd_dai_stream *io,
			      struct rsnd_priv *priv)
{
	int ret;

	ret = _rsnd_src_stop_gen2(mod);

	rsnd_dma_stop(io, rsnd_mod_to_dma(mod));

	return ret;
}

static void rsnd_src_reconvert_update(struct rsnd_dai_stream *io,
				      struct rsnd_mod *mod)
{
	struct snd_pcm_runtime *runtime = rsnd_io_to_runtime(io);
	struct rsnd_src *src = rsnd_mod_to_src(mod);
	u32 convert_rate = rsnd_src_convert_rate(io, src);
	u32 fsrate;

	if (!runtime)
		return;

	if (!convert_rate)
		convert_rate = runtime->rate;

	fsrate = 0x0400000 / convert_rate * runtime->rate;

	/* update IFS */
	rsnd_mod_write(mod, SRC_IFSVR, fsrate);
}

static int rsnd_src_pcm_new(struct rsnd_mod *mod,
			    struct rsnd_dai_stream *io,
			    struct snd_soc_pcm_runtime *rtd)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct rsnd_dai *rdai = rsnd_io_to_rdai(io);
	struct rsnd_src *src = rsnd_mod_to_src(mod);
	int ret;

	/*
	 * enable SRC sync convert if possible
	 */

	/*
	 * Gen1 is not supported
	 */
	if (rsnd_is_gen1(priv))
		return 0;

	/*
	 * SRC sync convert needs clock master
	 */
	if (!rsnd_rdai_is_clk_master(rdai))
		return 0;

	/*
	 * enable sync convert
	 */
	ret = rsnd_kctrl_new_s(mod, io, rtd,
			       rsnd_io_is_play(io) ?
			       "SRC Out Rate Switch" :
			       "SRC In Rate Switch",
			       rsnd_src_reconvert_update,
			       &src->sen, 1);
	if (ret < 0)
		return ret;

	ret = rsnd_kctrl_new_s(mod, io, rtd,
			       rsnd_io_is_play(io) ?
			       "SRC Out Rate" :
			       "SRC In Rate",
			       rsnd_src_reconvert_update,
			       &src->sync, 192000);

	return ret;
}

static struct rsnd_mod_ops rsnd_src_gen2_ops = {
	.name	= SRC_NAME,
	.dma_req = rsnd_src_dma_req,
	.probe	= rsnd_src_probe_gen2,
	.remove	= rsnd_src_remove_gen2,
	.init	= rsnd_src_init_gen2,
	.quit	= rsnd_src_quit,
	.start	= rsnd_src_start_gen2,
	.stop	= rsnd_src_stop_gen2,
	.hw_params = rsnd_src_hw_params,
	.pcm_new = rsnd_src_pcm_new,
};

struct rsnd_mod *rsnd_src_mod_get(struct rsnd_priv *priv, int id)
{
	if (WARN_ON(id < 0 || id >= rsnd_src_nr(priv)))
		id = 0;

	return &((struct rsnd_src *)(priv->src) + id)->mod;
}

static void rsnd_of_parse_src(struct platform_device *pdev,
			      const struct rsnd_of_data *of_data,
			      struct rsnd_priv *priv)
{
	struct device_node *src_node;
	struct device_node *np;
	struct rcar_snd_info *info = rsnd_priv_to_info(priv);
	struct rsnd_src_platform_info *src_info;
	struct device *dev = &pdev->dev;
	int nr, i;

	if (!of_data)
		return;

	src_node = rsnd_src_of_node(priv);
	if (!src_node)
		return;

	nr = of_get_child_count(src_node);
	if (!nr)
		goto rsnd_of_parse_src_end;

	src_info = devm_kzalloc(dev,
				sizeof(struct rsnd_src_platform_info) * nr,
				GFP_KERNEL);
	if (!src_info) {
		dev_err(dev, "src info allocation error\n");
		goto rsnd_of_parse_src_end;
	}

	info->src_info		= src_info;
	info->src_info_nr	= nr;

	i = 0;
	for_each_child_of_node(src_node, np) {
		src_info[i].irq = irq_of_parse_and_map(np, 0);

		i++;
	}

rsnd_of_parse_src_end:
	of_node_put(src_node);
}

int rsnd_src_probe(struct platform_device *pdev,
		   const struct rsnd_of_data *of_data,
		   struct rsnd_priv *priv)
{
	struct rcar_snd_info *info = rsnd_priv_to_info(priv);
	struct device *dev = rsnd_priv_to_dev(priv);
	struct rsnd_src *src;
	struct rsnd_mod_ops *ops;
	struct clk *clk;
	char name[RSND_SRC_NAME_SIZE];
	int i, nr, ret;

	ops = NULL;
	if (rsnd_is_gen1(priv))
		ops = &rsnd_src_gen1_ops;
	if (rsnd_is_gen2(priv))
		ops = &rsnd_src_gen2_ops;
	if (!ops) {
		dev_err(dev, "unknown Generation\n");
		return -EIO;
	}

	rsnd_of_parse_src(pdev, of_data, priv);

	/*
	 * init SRC
	 */
	nr	= info->src_info_nr;
	if (!nr)
		return 0;

	src	= devm_kzalloc(dev, sizeof(*src) * nr, GFP_KERNEL);
	if (!src)
		return -ENOMEM;

	priv->src_nr	= nr;
	priv->src	= src;

	for_each_rsnd_src(src, priv, i) {
		snprintf(name, RSND_SRC_NAME_SIZE, "%s.%d",
			 SRC_NAME, i);

		clk = devm_clk_get(dev, name);
		if (IS_ERR(clk))
			return PTR_ERR(clk);

		src->info = &info->src_info[i];

		ret = rsnd_mod_init(priv, &src->mod, ops, clk, RSND_MOD_SRC, i);
		if (ret)
			return ret;
	}

	return 0;
}

void rsnd_src_remove(struct platform_device *pdev,
		     struct rsnd_priv *priv)
{
	struct rsnd_src *src;
	int i;

	for_each_rsnd_src(src, priv, i) {
		rsnd_mod_quit(&src->mod);
	}
}
