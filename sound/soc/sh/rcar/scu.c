/*
 * Renesas R-Car SCU support
 *
 * Copyright (C) 2013 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include "rsnd.h"

struct rsnd_scu {
	struct rsnd_scu_platform_info *info; /* rcar_snd.h */
	struct rsnd_mod mod;
	struct clk *clk;
};

#define RSND_SCU_NAME_SIZE 16

/*
 * ADINR
 */
#define OTBL_24		(0 << 16)
#define OTBL_22		(2 << 16)
#define OTBL_20		(4 << 16)
#define OTBL_18		(6 << 16)
#define OTBL_16		(8 << 16)

#define rsnd_scu_mode_flags(p) ((p)->info->flags)
#define rsnd_scu_convert_rate(p) ((p)->info->convert_rate)
#define rsnd_mod_to_scu(_mod)				\
	container_of((_mod), struct rsnd_scu, mod)
#define rsnd_scu_hpbif_is_enable(scu)	\
	(rsnd_scu_mode_flags(scu) & RSND_SCU_USE_HPBIF)
#define rsnd_scu_dma_available(scu) \
	rsnd_dma_available(rsnd_mod_to_dma(&(scu)->mod))

#define for_each_rsnd_scu(pos, priv, i)				\
	for ((i) = 0;						\
	     ((i) < rsnd_scu_nr(priv)) &&			\
	     ((pos) = (struct rsnd_scu *)(priv)->scu + i);	\
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
 * scu.c is caring...
 *
 * Gen1
 *
 * [mem] -> [SRU] -> [SSI]
 *        |--------|
 *
 * Gen2
 *
 * [mem] -> [SCU] -> [SSIU] -> [SSI]
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
 * struct rsnd_scu_platform_info {
 *	u32 flags;
 *	u32 convert_rate;
 * }
 *
 * rsnd_scu_hpbif_is_enable() will be true
 * if flags had RSND_SCU_USE_HPBIF,
 * and it controls whether SSIU is used or not.
 *
 * rsnd_scu_convert_rate() indicates
 * above convert_rate, and it controls
 * whether SRC is used or not.
 *
 * ex) doesn't use SRC
 * struct rsnd_scu_platform_info info = {
 *	.flags = 0,
 *	.convert_rate = 0,
 * };
 *
 * ex) uses SRC
 * struct rsnd_scu_platform_info info = {
 *	.flags = RSND_SCU_USE_HPBIF,
 *	.convert_rate = 48000,
 * };
 *
 * ex) uses SRC bypass mode
 * struct rsnd_scu_platform_info info = {
 *	.flags = RSND_SCU_USE_HPBIF,
 *	.convert_rate = 0,
 * };
 *
 */

/*
 *		Gen1/Gen2 common functions
 */
static int rsnd_scu_ssi_mode_init(struct rsnd_mod *mod,
				  struct rsnd_dai *rdai,
				  struct rsnd_dai_stream *io)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct rsnd_scu *scu = rsnd_mod_to_scu(mod);
	struct rsnd_mod *ssi_mod = rsnd_io_to_mod_ssi(io);
	int ssi_id = rsnd_mod_id(ssi_mod);
	u32 convert_rate = rsnd_scu_convert_rate(scu);

	if (convert_rate && !rsnd_dai_is_clk_master(rdai)) {
		struct device *dev = rsnd_priv_to_dev(priv);

		dev_err(dev, "rsnd should be clk master when you rate convert\n");
		return -EINVAL;
	}

	/*
	 * SSI_MODE0
	 */
	rsnd_mod_bset(mod, SSI_MODE0, (1 << ssi_id),
		      rsnd_scu_hpbif_is_enable(scu) ? 0 : (1 << ssi_id));

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
			rsnd_mod_bset(mod, SSI_MODE1,
				      0x3 << shift,
				      rsnd_dai_is_clk_master(rdai) ?
				      0x2 << shift : 0x1 << shift);
	}

	return 0;
}

int rsnd_scu_enable_ssi_irq(struct rsnd_mod *ssi_mod,
			    struct rsnd_dai *rdai,
			    struct rsnd_dai_stream *io)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(ssi_mod);

	/* enable PIO interrupt if Gen2 */
	if (rsnd_is_gen2(priv))
		rsnd_mod_write(ssi_mod, INT_ENABLE, 0x0f000000);

	return 0;
}

unsigned int rsnd_scu_get_ssi_rate(struct rsnd_priv *priv,
				   struct rsnd_dai_stream *io,
				   struct snd_pcm_runtime *runtime)
{
	struct rsnd_scu *scu;
	unsigned int rate;

	scu = rsnd_mod_to_scu(rsnd_io_to_mod_scu(io));

	/*
	 * return convert rate if SRC is used,
	 * otherwise, return runtime->rate as usual
	 */
	rate = rsnd_scu_convert_rate(scu);
	if (!rate)
		rate = runtime->rate;

	return rate;
}

static int rsnd_scu_set_convert_rate(struct rsnd_mod *mod,
				     struct rsnd_dai *rdai,
				     struct rsnd_dai_stream *io)
{
	struct snd_pcm_runtime *runtime = rsnd_io_to_runtime(io);
	struct rsnd_scu *scu = rsnd_mod_to_scu(mod);
	u32 convert_rate = rsnd_scu_convert_rate(scu);
	u32 adinr = runtime->channels;
	u32 fsrate = 0;

	if (convert_rate)
		fsrate = 0x0400000 / convert_rate * runtime->rate;

	/* set/clear soft reset */
	rsnd_mod_write(mod, SRC_SWRSR, 0);
	rsnd_mod_write(mod, SRC_SWRSR, 1);

	/*
	 * Initialize the operation of the SRC internal circuits
	 * see rsnd_scu_start()
	 */
	rsnd_mod_write(mod, SRC_SRCIR, 1);

	/* Set channel number and output bit length */
	switch (runtime->sample_bits) {
	case 16:
		adinr |= OTBL_16;
		break;
	case 32:
		adinr |= OTBL_24;
		break;
	default:
		return -EIO;
	}
	rsnd_mod_write(mod, SRC_ADINR, adinr);

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

static int rsnd_scu_init(struct rsnd_mod *mod,
			 struct rsnd_dai *rdai,
			 struct rsnd_dai_stream *io)
{
	struct rsnd_scu *scu = rsnd_mod_to_scu(mod);
	int ret;

	clk_enable(scu->clk);

	ret = rsnd_scu_ssi_mode_init(mod, rdai, io);
	if (ret < 0)
		return ret;

	return 0;
}

static int rsnd_scu_quit(struct rsnd_mod *mod,
			 struct rsnd_dai *rdai,
			 struct rsnd_dai_stream *io)
{
	struct rsnd_scu *scu = rsnd_mod_to_scu(mod);

	clk_disable(scu->clk);

	return 0;
}

static int rsnd_scu_start(struct rsnd_mod *mod,
			  struct rsnd_dai *rdai,
			  struct rsnd_dai_stream *io)
{
	struct rsnd_scu *scu = rsnd_mod_to_scu(mod);

	/*
	 * Cancel the initialization and operate the SRC function
	 * see rsnd_scu_set_convert_rate()
	 */
	rsnd_mod_write(mod, SRC_SRCIR, 0);

	if (rsnd_scu_convert_rate(scu))
		rsnd_mod_write(mod, SRC_ROUTE_MODE0, 1);

	return 0;
}


static int rsnd_scu_stop(struct rsnd_mod *mod,
			 struct rsnd_dai *rdai,
			 struct rsnd_dai_stream *io)
{
	struct rsnd_scu *scu = rsnd_mod_to_scu(mod);

	if (rsnd_scu_convert_rate(scu))
		rsnd_mod_write(mod, SRC_ROUTE_MODE0, 0);

	return 0;
}

static struct rsnd_mod_ops rsnd_scu_non_ops = {
	.name	= "scu (non)",
};

/*
 *		Gen1 functions
 */
static int rsnd_src_set_route_gen1(struct rsnd_mod *mod,
				   struct rsnd_dai *rdai,
				   struct rsnd_dai_stream *io)
{
	struct scu_route_config {
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
	val = rsnd_dai_is_play(rdai, io) ? 0x1 : 0x2;
	val = val		<< routes[id].shift;
	mask = routes[id].mask	<< routes[id].shift;

	rsnd_mod_bset(mod, SRC_ROUTE_SEL, mask, val);

	return 0;
}

static int rsnd_scu_set_convert_timing_gen1(struct rsnd_mod *mod,
					    struct rsnd_dai *rdai,
					    struct rsnd_dai_stream *io)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct rsnd_scu *scu = rsnd_mod_to_scu(mod);
	struct snd_pcm_runtime *runtime = rsnd_io_to_runtime(io);
	u32 convert_rate = rsnd_scu_convert_rate(scu);
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

static int rsnd_scu_set_convert_rate_gen1(struct rsnd_mod *mod,
					  struct rsnd_dai *rdai,
					  struct rsnd_dai_stream *io)
{
	int ret;

	ret = rsnd_scu_set_convert_rate(mod, rdai, io);
	if (ret < 0)
		return ret;

	/* Select SRC mode (fixed value) */
	rsnd_mod_write(mod, SRC_SRCCR, 0x00010110);

	/* Set the restriction value of the FS ratio (98%) */
	rsnd_mod_write(mod, SRC_MNFSR,
		       rsnd_mod_read(mod, SRC_IFSVR) / 100 * 98);

	/* no SRC_BFSSR settings, since SRC_SRCCR::BUFMD is 0 */

	return 0;
}

static int rsnd_scu_init_gen1(struct rsnd_mod *mod,
			      struct rsnd_dai *rdai,
			      struct rsnd_dai_stream *io)
{
	int ret;

	ret = rsnd_scu_init(mod, rdai, io);
	if (ret < 0)
		return ret;

	ret = rsnd_src_set_route_gen1(mod, rdai, io);
	if (ret < 0)
		return ret;

	ret = rsnd_scu_set_convert_rate_gen1(mod, rdai, io);
	if (ret < 0)
		return ret;

	ret = rsnd_scu_set_convert_timing_gen1(mod, rdai, io);
	if (ret < 0)
		return ret;

	return 0;
}

static int rsnd_scu_start_gen1(struct rsnd_mod *mod,
			       struct rsnd_dai *rdai,
			       struct rsnd_dai_stream *io)
{
	int id = rsnd_mod_id(mod);

	rsnd_mod_bset(mod, SRC_ROUTE_CTRL, (1 << id), (1 << id));

	return rsnd_scu_start(mod, rdai, io);
}

static int rsnd_scu_stop_gen1(struct rsnd_mod *mod,
			      struct rsnd_dai *rdai,
			      struct rsnd_dai_stream *io)
{
	int id = rsnd_mod_id(mod);

	rsnd_mod_bset(mod, SRC_ROUTE_CTRL, (1 << id), 0);

	return rsnd_scu_stop(mod, rdai, io);
}

static struct rsnd_mod_ops rsnd_scu_gen1_ops = {
	.name	= "sru (gen1)",
	.init	= rsnd_scu_init_gen1,
	.quit	= rsnd_scu_quit,
	.start	= rsnd_scu_start_gen1,
	.stop	= rsnd_scu_stop_gen1,
};

static struct rsnd_mod_ops rsnd_scu_non_gen1_ops = {
	.name	= "non-sru (gen1)",
	.init	= rsnd_scu_ssi_mode_init,
};

/*
 *		Gen2 functions
 */
static int rsnd_scu_set_convert_rate_gen2(struct rsnd_mod *mod,
					  struct rsnd_dai *rdai,
					  struct rsnd_dai_stream *io)
{
	int ret;

	ret = rsnd_scu_set_convert_rate(mod, rdai, io);
	if (ret < 0)
		return ret;

	rsnd_mod_write(mod, SSI_BUSIF_ADINR, rsnd_mod_read(mod, SRC_ADINR));
	rsnd_mod_write(mod, SSI_BUSIF_MODE,  rsnd_mod_read(mod, SRC_BUSIF_MODE));

	rsnd_mod_write(mod, SRC_SRCCR, 0x00011110);

	rsnd_mod_write(mod, SRC_BSDSR, 0x01800000);
	rsnd_mod_write(mod, SRC_BSISR, 0x00100060);

	return 0;
}

static int rsnd_scu_set_convert_timing_gen2(struct rsnd_mod *mod,
					    struct rsnd_dai *rdai,
					    struct rsnd_dai_stream *io)
{
	struct snd_pcm_runtime *runtime = rsnd_io_to_runtime(io);
	struct rsnd_scu *scu = rsnd_mod_to_scu(mod);
	u32 convert_rate = rsnd_scu_convert_rate(scu);
	int ret;

	if (convert_rate)
		ret = rsnd_adg_set_convert_clk_gen2(mod, rdai, io,
						    runtime->rate,
						    convert_rate);
	else
		ret = rsnd_adg_set_convert_timing_gen2(mod, rdai, io);

	return ret;
}

static int rsnd_scu_init_gen2(struct rsnd_mod *mod,
			      struct rsnd_dai *rdai,
			      struct rsnd_dai_stream *io)
{
	int ret;

	ret = rsnd_scu_init(mod, rdai, io);
	if (ret < 0)
		return ret;

	ret = rsnd_scu_set_convert_rate_gen2(mod, rdai, io);
	if (ret < 0)
		return ret;

	ret = rsnd_scu_set_convert_timing_gen2(mod, rdai, io);
	if (ret < 0)
		return ret;

	return 0;
}

static int rsnd_scu_start_gen2(struct rsnd_mod *mod,
			       struct rsnd_dai *rdai,
			       struct rsnd_dai_stream *io)
{
	struct rsnd_scu *scu = rsnd_mod_to_scu(mod);

	rsnd_dma_start(rsnd_mod_to_dma(&scu->mod));

	rsnd_mod_write(mod, SSI_CTRL, 0x1);
	rsnd_mod_write(mod, SRC_CTRL, 0x11);

	return rsnd_scu_start(mod, rdai, io);
}

static int rsnd_scu_stop_gen2(struct rsnd_mod *mod,
			      struct rsnd_dai *rdai,
			      struct rsnd_dai_stream *io)
{
	struct rsnd_scu *scu = rsnd_mod_to_scu(mod);

	rsnd_mod_write(mod, SSI_CTRL, 0);
	rsnd_mod_write(mod, SRC_CTRL, 0);

	rsnd_dma_stop(rsnd_mod_to_dma(&scu->mod));

	return rsnd_scu_stop(mod, rdai, io);
}

static struct rsnd_mod_ops rsnd_scu_gen2_ops = {
	.name	= "scu (gen2)",
	.init	= rsnd_scu_init_gen2,
	.quit	= rsnd_scu_quit,
	.start	= rsnd_scu_start_gen2,
	.stop	= rsnd_scu_stop_gen2,
};

static struct rsnd_mod_ops rsnd_scu_non_gen2_ops = {
	.name	= "non-scu (gen2)",
	.init	= rsnd_scu_ssi_mode_init,
};

struct rsnd_mod *rsnd_scu_mod_get(struct rsnd_priv *priv, int id)
{
	if (WARN_ON(id < 0 || id >= rsnd_scu_nr(priv)))
		id = 0;

	return &((struct rsnd_scu *)(priv->scu) + id)->mod;
}

int rsnd_scu_probe(struct platform_device *pdev,
		   struct rsnd_priv *priv)
{
	struct rcar_snd_info *info = rsnd_priv_to_info(priv);
	struct device *dev = rsnd_priv_to_dev(priv);
	struct rsnd_scu *scu;
	struct rsnd_mod_ops *ops;
	struct clk *clk;
	char name[RSND_SCU_NAME_SIZE];
	int i, nr;

	/*
	 * init SCU
	 */
	nr	= info->scu_info_nr;
	if (!nr)
		return 0;

	scu	= devm_kzalloc(dev, sizeof(*scu) * nr, GFP_KERNEL);
	if (!scu) {
		dev_err(dev, "SCU allocate failed\n");
		return -ENOMEM;
	}

	priv->scu_nr	= nr;
	priv->scu	= scu;

	for_each_rsnd_scu(scu, priv, i) {
		snprintf(name, RSND_SCU_NAME_SIZE, "scu.%d", i);

		clk = devm_clk_get(dev, name);
		if (IS_ERR(clk))
			return PTR_ERR(clk);

		scu->info = &info->scu_info[i];
		scu->clk = clk;

		ops = &rsnd_scu_non_ops;
		if (rsnd_scu_hpbif_is_enable(scu)) {
			if (rsnd_is_gen1(priv))
				ops = &rsnd_scu_gen1_ops;
			if (rsnd_is_gen2(priv)) {
				int ret;
				int is_play;

				if (info->dai_info) {
					is_play = rsnd_info_is_playback(priv, scu);
				} else {
					struct rsnd_mod *ssi = rsnd_ssi_mod_get(priv, i);
					is_play = rsnd_ssi_is_play(ssi);
				}
				ret = rsnd_dma_init(priv,
						    rsnd_mod_to_dma(&scu->mod),
						    is_play,
						    scu->info->dma_id);
				if (ret < 0)
					return ret;

				ops = &rsnd_scu_gen2_ops;
			}
		} else {
			if (rsnd_is_gen1(priv))
				ops = &rsnd_scu_non_gen1_ops;
			if (rsnd_is_gen2(priv))
				ops = &rsnd_scu_non_gen2_ops;
		}

		rsnd_mod_init(priv, &scu->mod, ops, RSND_MOD_SCU, i);

		dev_dbg(dev, "SCU%d probed\n", i);
	}

	return 0;
}

void rsnd_scu_remove(struct platform_device *pdev,
		     struct rsnd_priv *priv)
{
	struct rsnd_scu *scu;
	int i;

	for_each_rsnd_scu(scu, priv, i) {
		if (rsnd_scu_dma_available(scu))
			rsnd_dma_quit(priv, rsnd_mod_to_dma(&scu->mod));
	}
}
