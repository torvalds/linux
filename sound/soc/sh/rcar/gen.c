/*
 * Renesas R-Car Gen1 SRU/SSI support
 *
 * Copyright (C) 2013 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include "rsnd.h"

struct rsnd_gen_ops {
	int (*path_init)(struct rsnd_priv *priv,
			 struct rsnd_dai *rdai,
			 struct rsnd_dai_stream *io);
	int (*path_exit)(struct rsnd_priv *priv,
			 struct rsnd_dai *rdai,
			 struct rsnd_dai_stream *io);
};

struct rsnd_gen_reg_map {
	int index;	/* -1 : not supported */
	u32 offset_id;	/* offset of ssi0, ssi1, ssi2... */
	u32 offset_adr;	/* offset of SSICR, SSISR, ... */
};

struct rsnd_gen {
	void __iomem *base[RSND_BASE_MAX];

	struct rsnd_gen_reg_map reg_map[RSND_REG_MAX];
	struct rsnd_gen_ops *ops;
};

#define rsnd_priv_to_gen(p)	((struct rsnd_gen *)(p)->gen)

#define rsnd_is_gen1(s)		((s)->info->flags & RSND_GEN1)
#define rsnd_is_gen2(s)		((s)->info->flags & RSND_GEN2)

/*
 *		Gen2
 *		will be filled in the future
 */

/*
 *		Gen1
 */
static int rsnd_gen1_path_init(struct rsnd_priv *priv,
			       struct rsnd_dai *rdai,
			       struct rsnd_dai_stream *io)
{
	struct rsnd_mod *mod;
	int ret;
	int id;

	/*
	 * Gen1 is created by SRU/SSI, and this SRU is base module of
	 * Gen2's SCU/SSIU/SSI. (Gen2 SCU/SSIU came from SRU)
	 *
	 * Easy image is..
	 *	Gen1 SRU = Gen2 SCU + SSIU + etc
	 *
	 * Gen2 SCU path is very flexible, but, Gen1 SRU (SCU parts) is
	 * using fixed path.
	 *
	 * Then, SSI id = SCU id here
	 */

	/* get SSI's ID */
	mod = rsnd_ssi_mod_get_frm_dai(priv,
				       rsnd_dai_id(priv, rdai),
				       rsnd_dai_is_play(rdai, io));
	id = rsnd_mod_id(mod);

	/* SSI */
	mod = rsnd_ssi_mod_get(priv, id);
	ret = rsnd_dai_connect(rdai, mod, io);
	if (ret < 0)
		return ret;

	/* SCU */
	mod = rsnd_scu_mod_get(priv, id);
	ret = rsnd_dai_connect(rdai, mod, io);

	return ret;
}

static int rsnd_gen1_path_exit(struct rsnd_priv *priv,
			       struct rsnd_dai *rdai,
			       struct rsnd_dai_stream *io)
{
	struct rsnd_mod *mod, *n;
	int ret = 0;

	/*
	 * remove all mod from rdai
	 */
	for_each_rsnd_mod(mod, n, io)
		ret |= rsnd_dai_disconnect(mod);

	return ret;
}

static struct rsnd_gen_ops rsnd_gen1_ops = {
	.path_init	= rsnd_gen1_path_init,
	.path_exit	= rsnd_gen1_path_exit,
};

#define RSND_GEN1_REG_MAP(g, s, i, oi, oa)				\
	do {								\
		(g)->reg_map[RSND_REG_##i].index  = RSND_GEN1_##s;	\
		(g)->reg_map[RSND_REG_##i].offset_id = oi;		\
		(g)->reg_map[RSND_REG_##i].offset_adr = oa;		\
	} while (0)

static void rsnd_gen1_reg_map_init(struct rsnd_gen *gen)
{
	RSND_GEN1_REG_MAP(gen, SRU,	SSI_MODE0,	0x0,	0xD0);
	RSND_GEN1_REG_MAP(gen, SRU,	SSI_MODE1,	0x0,	0xD4);

	RSND_GEN1_REG_MAP(gen, ADG,	BRRA,		0x0,	0x00);
	RSND_GEN1_REG_MAP(gen, ADG,	BRRB,		0x0,	0x04);
	RSND_GEN1_REG_MAP(gen, ADG,	SSICKR,		0x0,	0x08);
	RSND_GEN1_REG_MAP(gen, ADG,	AUDIO_CLK_SEL0,	0x0,	0x0c);
	RSND_GEN1_REG_MAP(gen, ADG,	AUDIO_CLK_SEL1,	0x0,	0x10);
	RSND_GEN1_REG_MAP(gen, ADG,	AUDIO_CLK_SEL3,	0x0,	0x18);
	RSND_GEN1_REG_MAP(gen, ADG,	AUDIO_CLK_SEL4,	0x0,	0x1c);
	RSND_GEN1_REG_MAP(gen, ADG,	AUDIO_CLK_SEL5,	0x0,	0x20);

	RSND_GEN1_REG_MAP(gen, SSI,	SSICR,		0x40,	0x00);
	RSND_GEN1_REG_MAP(gen, SSI,	SSISR,		0x40,	0x04);
	RSND_GEN1_REG_MAP(gen, SSI,	SSITDR,		0x40,	0x08);
	RSND_GEN1_REG_MAP(gen, SSI,	SSIRDR,		0x40,	0x0c);
	RSND_GEN1_REG_MAP(gen, SSI,	SSIWSR,		0x40,	0x20);
}

static int rsnd_gen1_probe(struct platform_device *pdev,
			   struct rcar_snd_info *info,
			   struct rsnd_priv *priv)
{
	struct device *dev = rsnd_priv_to_dev(priv);
	struct rsnd_gen *gen = rsnd_priv_to_gen(priv);
	struct resource *sru_res;
	struct resource *adg_res;
	struct resource *ssi_res;

	/*
	 * map address
	 */
	sru_res	= platform_get_resource(pdev, IORESOURCE_MEM, RSND_GEN1_SRU);
	adg_res = platform_get_resource(pdev, IORESOURCE_MEM, RSND_GEN1_ADG);
	ssi_res	= platform_get_resource(pdev, IORESOURCE_MEM, RSND_GEN1_SSI);

	gen->base[RSND_GEN1_SRU] = devm_ioremap_resource(dev, sru_res);
	gen->base[RSND_GEN1_ADG] = devm_ioremap_resource(dev, adg_res);
	gen->base[RSND_GEN1_SSI] = devm_ioremap_resource(dev, ssi_res);
	if (IS_ERR(gen->base[RSND_GEN1_SRU]) ||
	    IS_ERR(gen->base[RSND_GEN1_ADG]) ||
	    IS_ERR(gen->base[RSND_GEN1_SSI]))
		return -ENODEV;

	gen->ops = &rsnd_gen1_ops;
	rsnd_gen1_reg_map_init(gen);

	dev_dbg(dev, "Gen1 device probed\n");
	dev_dbg(dev, "SRU : %08x => %p\n",	sru_res->start,
						gen->base[RSND_GEN1_SRU]);
	dev_dbg(dev, "ADG : %08x => %p\n",	adg_res->start,
						gen->base[RSND_GEN1_ADG]);
	dev_dbg(dev, "SSI : %08x => %p\n",	ssi_res->start,
						gen->base[RSND_GEN1_SSI]);

	return 0;

}

static void rsnd_gen1_remove(struct platform_device *pdev,
			     struct rsnd_priv *priv)
{
}

/*
 *		Gen
 */
int rsnd_gen_path_init(struct rsnd_priv *priv,
		       struct rsnd_dai *rdai,
		       struct rsnd_dai_stream *io)
{
	struct rsnd_gen *gen = rsnd_priv_to_gen(priv);

	return gen->ops->path_init(priv, rdai, io);
}

int rsnd_gen_path_exit(struct rsnd_priv *priv,
		       struct rsnd_dai *rdai,
		       struct rsnd_dai_stream *io)
{
	struct rsnd_gen *gen = rsnd_priv_to_gen(priv);

	return gen->ops->path_exit(priv, rdai, io);
}

void __iomem *rsnd_gen_reg_get(struct rsnd_priv *priv,
			       struct rsnd_mod *mod,
			       enum rsnd_reg reg)
{
	struct rsnd_gen *gen = rsnd_priv_to_gen(priv);
	struct device *dev = rsnd_priv_to_dev(priv);
	int index;
	u32 offset_id, offset_adr;

	if (reg >= RSND_REG_MAX) {
		dev_err(dev, "rsnd_reg reg error\n");
		return NULL;
	}

	index		= gen->reg_map[reg].index;
	offset_id	= gen->reg_map[reg].offset_id;
	offset_adr	= gen->reg_map[reg].offset_adr;

	if (index < 0) {
		dev_err(dev, "unsupported reg access %d\n", reg);
		return NULL;
	}

	if (offset_id && mod)
		offset_id *= rsnd_mod_id(mod);

	/*
	 * index/offset were set on gen1/gen2
	 */

	return gen->base[index] + offset_id + offset_adr;
}

int rsnd_gen_probe(struct platform_device *pdev,
		   struct rcar_snd_info *info,
		   struct rsnd_priv *priv)
{
	struct device *dev = rsnd_priv_to_dev(priv);
	struct rsnd_gen *gen;
	int i;

	gen = devm_kzalloc(dev, sizeof(*gen), GFP_KERNEL);
	if (!gen) {
		dev_err(dev, "GEN allocate failed\n");
		return -ENOMEM;
	}

	priv->gen = gen;

	/*
	 * see
	 *	rsnd_reg_get()
	 *	rsnd_gen_probe()
	 */
	for (i = 0; i < RSND_REG_MAX; i++)
		gen->reg_map[i].index = -1;

	/*
	 *	init each module
	 */
	if (rsnd_is_gen1(priv))
		return rsnd_gen1_probe(pdev, info, priv);

	dev_err(dev, "unknown generation R-Car sound device\n");

	return -ENODEV;
}

void rsnd_gen_remove(struct platform_device *pdev,
		     struct rsnd_priv *priv)
{
	if (rsnd_is_gen1(priv))
		rsnd_gen1_remove(pdev, priv);
}
