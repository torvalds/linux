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
static int rsnd_gen1_probe(struct platform_device *pdev,
			   struct rcar_snd_info *info,
			   struct rsnd_priv *priv)
{
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
