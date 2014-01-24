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

#define rsnd_scu_mode_flags(p) ((p)->info->flags)
#define rsnd_scu_convert_rate(p) ((p)->info->convert_rate)

#define RSND_SCU_NAME_SIZE 16

/*
 * ADINR
 */
#define OTBL_24		(0 << 16)
#define OTBL_22		(2 << 16)
#define OTBL_20		(4 << 16)
#define OTBL_18		(6 << 16)
#define OTBL_16		(8 << 16)

/*
 *		image of SRC (Sampling Rate Converter)
 *
 * 96kHz   <-> +-----+	48kHz	+-----+	 48kHz	+-------+
 * 48kHz   <-> | SRC | <------>	| SSI |	<----->	| codec |
 * 44.1kHz <-> +-----+		+-----+		+-------+
 * ...
 *
 */

#define rsnd_mod_to_scu(_mod)	\
	container_of((_mod), struct rsnd_scu, mod)

#define for_each_rsnd_scu(pos, priv, i)					\
	for ((i) = 0;							\
	     ((i) < rsnd_scu_nr(priv)) &&				\
		     ((pos) = (struct rsnd_scu *)(priv)->scu + i);	\
	     i++)

static int rsnd_scu_ssi_mode_init(struct rsnd_mod *mod,
				  struct rsnd_dai *rdai,
				  struct rsnd_dai_stream *io)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	int id = rsnd_mod_id(mod);

	/*
	 * SSI_MODE0
	 */
	rsnd_mod_bset(mod, SSI_MODE0, (1 << id),
		      rsnd_scu_hpbif_is_enable(mod) ? 0 : (1 << id));

	/*
	 * SSI_MODE1
	 */
	if (rsnd_ssi_is_pin_sharing(rsnd_ssi_mod_get(priv, id))) {
		int shift = -1;
		switch (id) {
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

/* Gen1 only */
static int rsnd_src_set_route_if_gen1(
			      struct rsnd_mod *mod,
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
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct rsnd_scu *scu = rsnd_mod_to_scu(mod);
	u32 mask;
	u32 val;
	int shift;
	int id;

	/*
	 * Gen1 only
	 */
	if (!rsnd_is_gen1(priv))
		return 0;

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
	if (rsnd_scu_convert_rate(scu))	/* use ADG */
		val = 0;
	else if (8 == id)		/* use SSI WS, but SRU8 is special */
		val = id << shift;
	else				/* use SSI WS */
		val = (id + 1) << shift;

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

unsigned int rsnd_scu_get_ssi_rate(struct rsnd_priv *priv,
				   struct rsnd_mod *ssi_mod,
				   struct snd_pcm_runtime *runtime)
{
	struct rsnd_scu *scu;
	unsigned int rate;

	/* this function is assuming SSI id = SCU id here */
	scu = rsnd_mod_to_scu(rsnd_scu_mod_get(priv, rsnd_mod_id(ssi_mod)));

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
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct snd_pcm_runtime *runtime = rsnd_io_to_runtime(io);
	struct rsnd_scu *scu = rsnd_mod_to_scu(mod);
	u32 convert_rate = rsnd_scu_convert_rate(scu);
	u32 adinr = runtime->channels;

	/* set/clear soft reset */
	rsnd_mod_write(mod, SRC_SWRSR, 0);
	rsnd_mod_write(mod, SRC_SWRSR, 1);

	/* Initialize the operation of the SRC internal circuits */
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

	if (convert_rate) {
		u32 fsrate = 0x0400000 / convert_rate * runtime->rate;
		int ret;

		/* Enable the initial value of IFS */
		rsnd_mod_write(mod, SRC_IFSCR, 1);

		/* Set initial value of IFS */
		rsnd_mod_write(mod, SRC_IFSVR, fsrate);

		/* Select SRC mode (fixed value) */
		rsnd_mod_write(mod, SRC_SRCCR, 0x00010110);

		/* Set the restriction value of the FS ratio (98%) */
		rsnd_mod_write(mod, SRC_MNFSR, fsrate / 100 * 98);

		if (rsnd_is_gen1(priv)) {
			/* no SRC_BFSSR settings, since SRC_SRCCR::BUFMD is 0 */
		}

		/* set convert clock */
		ret = rsnd_adg_set_convert_clk(priv, mod,
					       runtime->rate,
					       convert_rate);
		if (ret < 0)
			return ret;
	}

	/* Cancel the initialization and operate the SRC function */
	rsnd_mod_write(mod, SRC_SRCIR, 0);

	/* use DMA transfer */
	rsnd_mod_write(mod, SRC_BUSIF_MODE, 1);

	return 0;
}

bool rsnd_scu_hpbif_is_enable(struct rsnd_mod *mod)
{
	struct rsnd_scu *scu = rsnd_mod_to_scu(mod);
	u32 flags = rsnd_scu_mode_flags(scu);

	return !!(flags & RSND_SCU_USE_HPBIF);
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

	ret = rsnd_src_set_route_if_gen1(mod, rdai, io);
	if (ret < 0)
		return ret;

	ret = rsnd_scu_set_convert_rate(mod, rdai, io);
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
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct rsnd_scu *scu = rsnd_mod_to_scu(mod);
	int id = rsnd_mod_id(mod);

	if (rsnd_is_gen1(priv))
		rsnd_mod_bset(mod, SRC_ROUTE_CTRL, (1 << id), (1 << id));

	if (rsnd_scu_convert_rate(scu))
		rsnd_mod_write(mod, SRC_ROUTE_MODE0, 1);

	return 0;
}

static int rsnd_scu_stop(struct rsnd_mod *mod,
			  struct rsnd_dai *rdai,
			  struct rsnd_dai_stream *io)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct rsnd_scu *scu = rsnd_mod_to_scu(mod);
	int id = rsnd_mod_id(mod);

	if (rsnd_is_gen1(priv))
		rsnd_mod_bset(mod, SRC_ROUTE_CTRL, (1 << id), 0);

	if (rsnd_scu_convert_rate(scu))
		rsnd_mod_write(mod, SRC_ROUTE_MODE0, 0);

	return 0;
}

static struct rsnd_mod_ops rsnd_scu_ops = {
	.name	= "scu",
	.init	= rsnd_scu_init,
	.quit	= rsnd_scu_quit,
	.start	= rsnd_scu_start,
	.stop	= rsnd_scu_stop,
};

static struct rsnd_mod_ops rsnd_scu_non_ops = {
	.name	= "scu (non)",
	.init	= rsnd_scu_ssi_mode_init,
};

struct rsnd_mod *rsnd_scu_mod_get(struct rsnd_priv *priv, int id)
{
	if (WARN_ON(id < 0 || id >= rsnd_scu_nr(priv)))
		id = 0;

	return &((struct rsnd_scu *)(priv->scu) + id)->mod;
}

int rsnd_scu_probe(struct platform_device *pdev,
		   struct rcar_snd_info *info,
		   struct rsnd_priv *priv)
{
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
		if (rsnd_scu_hpbif_is_enable(&scu->mod))
			ops = &rsnd_scu_ops;

		rsnd_mod_init(priv, &scu->mod, ops, i);

		dev_dbg(dev, "SCU%d probed\n", i);
	}
	dev_dbg(dev, "scu probed\n");

	return 0;
}

void rsnd_scu_remove(struct platform_device *pdev,
		     struct rsnd_priv *priv)
{
}
