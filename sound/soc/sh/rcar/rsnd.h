/*
 * Renesas R-Car
 *
 * Copyright (C) 2013 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef RSND_H
#define RSND_H

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/sh_dma.h>
#include <linux/workqueue.h>
#include <sound/rcar_snd.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>

/*
 *	pseudo register
 *
 * The register address offsets SRU/SCU/SSIU on Gen1/Gen2 are very different.
 * This driver uses pseudo register in order to hide it.
 * see gen1/gen2 for detail
 */
enum rsnd_reg {
	/* SRU/SCU/SSIU */
	RSND_REG_SRC_ROUTE_SEL,		/* for Gen1 */
	RSND_REG_SRC_TMG_SEL0,		/* for Gen1 */
	RSND_REG_SRC_TMG_SEL1,		/* for Gen1 */
	RSND_REG_SRC_TMG_SEL2,		/* for Gen1 */
	RSND_REG_SRC_ROUTE_CTRL,	/* for Gen1 */
	RSND_REG_SSI_MODE0,
	RSND_REG_SSI_MODE1,
	RSND_REG_INT_ENABLE,		/* for Gen2 */
	RSND_REG_SRC_BUSIF_MODE,
	RSND_REG_SRC_ROUTE_MODE0,
	RSND_REG_SRC_SWRSR,
	RSND_REG_SRC_SRCIR,
	RSND_REG_SRC_ADINR,
	RSND_REG_SRC_IFSCR,
	RSND_REG_SRC_IFSVR,
	RSND_REG_SRC_SRCCR,
	RSND_REG_SRC_MNFSR,

	/* ADG */
	RSND_REG_BRRA,
	RSND_REG_BRRB,
	RSND_REG_SSICKR,
	RSND_REG_AUDIO_CLK_SEL0,
	RSND_REG_AUDIO_CLK_SEL1,
	RSND_REG_AUDIO_CLK_SEL2,
	RSND_REG_AUDIO_CLK_SEL3,	/* for Gen1 */
	RSND_REG_AUDIO_CLK_SEL4,	/* for Gen1 */
	RSND_REG_AUDIO_CLK_SEL5,	/* for Gen1 */

	/* SSI */
	RSND_REG_SSICR,
	RSND_REG_SSISR,
	RSND_REG_SSITDR,
	RSND_REG_SSIRDR,
	RSND_REG_SSIWSR,

	RSND_REG_MAX,
};

struct rsnd_priv;
struct rsnd_mod;
struct rsnd_dai;
struct rsnd_dai_stream;

/*
 *	R-Car basic functions
 */
#define rsnd_mod_read(m, r) \
	rsnd_read(rsnd_mod_to_priv(m), m, RSND_REG_##r)
#define rsnd_mod_write(m, r, d) \
	rsnd_write(rsnd_mod_to_priv(m), m, RSND_REG_##r, d)
#define rsnd_mod_bset(m, r, s, d) \
	rsnd_bset(rsnd_mod_to_priv(m), m, RSND_REG_##r, s, d)

u32 rsnd_read(struct rsnd_priv *priv, struct rsnd_mod *mod, enum rsnd_reg reg);
void rsnd_write(struct rsnd_priv *priv, struct rsnd_mod *mod,
		enum rsnd_reg reg, u32 data);
void rsnd_bset(struct rsnd_priv *priv, struct rsnd_mod *mod, enum rsnd_reg reg,
		    u32 mask, u32 data);

/*
 *	R-Car DMA
 */
struct rsnd_dma {
	struct rsnd_priv	*priv;
	struct sh_dmae_slave	slave;
	struct work_struct	work;
	struct dma_chan		*chan;
	enum dma_data_direction dir;
	int (*inquiry)(struct rsnd_dma *dma, dma_addr_t *buf, int *len);
	int (*complete)(struct rsnd_dma *dma);

	int submit_loop;
};

void rsnd_dma_start(struct rsnd_dma *dma);
void rsnd_dma_stop(struct rsnd_dma *dma);
int rsnd_dma_available(struct rsnd_dma *dma);
int rsnd_dma_init(struct rsnd_priv *priv, struct rsnd_dma *dma,
	int is_play, int id,
	int (*inquiry)(struct rsnd_dma *dma, dma_addr_t *buf, int *len),
	int (*complete)(struct rsnd_dma *dma));
void  rsnd_dma_quit(struct rsnd_priv *priv,
		    struct rsnd_dma *dma);


/*
 *	R-Car sound mod
 */

struct rsnd_mod_ops {
	char *name;
	int (*init)(struct rsnd_mod *mod,
		    struct rsnd_dai *rdai,
		    struct rsnd_dai_stream *io);
	int (*quit)(struct rsnd_mod *mod,
		    struct rsnd_dai *rdai,
		    struct rsnd_dai_stream *io);
	int (*start)(struct rsnd_mod *mod,
		     struct rsnd_dai *rdai,
		     struct rsnd_dai_stream *io);
	int (*stop)(struct rsnd_mod *mod,
		    struct rsnd_dai *rdai,
		    struct rsnd_dai_stream *io);
};

struct rsnd_mod {
	int id;
	struct rsnd_priv *priv;
	struct rsnd_mod_ops *ops;
	struct list_head list; /* connect to rsnd_dai playback/capture */
	struct rsnd_dma dma;
};

#define rsnd_mod_to_priv(mod) ((mod)->priv)
#define rsnd_mod_to_dma(mod) (&(mod)->dma)
#define rsnd_dma_to_mod(_dma) container_of((_dma), struct rsnd_mod, dma)
#define rsnd_mod_id(mod) ((mod)->id)
#define for_each_rsnd_mod(pos, n, io)	\
	list_for_each_entry_safe(pos, n, &(io)->head, list)
#define __rsnd_mod_call(mod, func, rdai, io)			\
({								\
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);		\
	struct device *dev = rsnd_priv_to_dev(priv);		\
	dev_dbg(dev, "%s-%d-%s\n",				\
		rsnd_mod_name(mod), rsnd_mod_id(mod), #func);	\
	(mod)->ops->func(mod, rdai, io);			\
})

#define rsnd_mod_call(mod, func, rdai, io)	\
	(!(mod) ? -ENODEV :			\
	 !((mod)->ops->func) ? 0 :		\
	 __rsnd_mod_call(mod, func, rdai, io))

void rsnd_mod_init(struct rsnd_priv *priv,
		   struct rsnd_mod *mod,
		   struct rsnd_mod_ops *ops,
		   int id);
char *rsnd_mod_name(struct rsnd_mod *mod);

/*
 *	R-Car sound DAI
 */
#define RSND_DAI_NAME_SIZE	16
struct rsnd_dai_stream {
	struct list_head head; /* head of rsnd_mod list */
	struct snd_pcm_substream *substream;
	int byte_pos;
	int period_pos;
	int byte_per_period;
	int next_period_byte;
};

struct rsnd_dai {
	char name[RSND_DAI_NAME_SIZE];
	struct rsnd_dai_platform_info *info; /* rcar_snd.h */
	struct rsnd_dai_stream playback;
	struct rsnd_dai_stream capture;

	unsigned int clk_master:1;
	unsigned int bit_clk_inv:1;
	unsigned int frm_clk_inv:1;
	unsigned int sys_delay:1;
	unsigned int data_alignment:1;
};

#define rsnd_dai_nr(priv) ((priv)->dai_nr)
#define for_each_rsnd_dai(rdai, priv, i)		\
	for (i = 0, (rdai) = rsnd_dai_get(priv, i);	\
	     i < rsnd_dai_nr(priv);			\
	     i++, (rdai) = rsnd_dai_get(priv, i))

struct rsnd_dai *rsnd_dai_get(struct rsnd_priv *priv, int id);
int rsnd_dai_disconnect(struct rsnd_mod *mod);
int rsnd_dai_connect(struct rsnd_dai *rdai, struct rsnd_mod *mod,
		     struct rsnd_dai_stream *io);
int rsnd_dai_is_play(struct rsnd_dai *rdai, struct rsnd_dai_stream *io);
int rsnd_dai_id(struct rsnd_priv *priv, struct rsnd_dai *rdai);
#define rsnd_dai_get_platform_info(rdai) ((rdai)->info)
#define rsnd_io_to_runtime(io) ((io)->substream->runtime)

void rsnd_dai_pointer_update(struct rsnd_dai_stream *io, int cnt);
int rsnd_dai_pointer_offset(struct rsnd_dai_stream *io, int additional);

/*
 *	R-Car Gen1/Gen2
 */
int rsnd_gen_probe(struct platform_device *pdev,
		   struct rcar_snd_info *info,
		   struct rsnd_priv *priv);
void rsnd_gen_remove(struct platform_device *pdev,
		     struct rsnd_priv *priv);
int rsnd_gen_path_init(struct rsnd_priv *priv,
		       struct rsnd_dai *rdai,
		       struct rsnd_dai_stream *io);
int rsnd_gen_path_exit(struct rsnd_priv *priv,
		       struct rsnd_dai *rdai,
		       struct rsnd_dai_stream *io);
void __iomem *rsnd_gen_reg_get(struct rsnd_priv *priv,
			       struct rsnd_mod *mod,
			       enum rsnd_reg reg);
#define rsnd_is_gen1(s)		(((s)->info->flags & RSND_GEN_MASK) == RSND_GEN1)
#define rsnd_is_gen2(s)		(((s)->info->flags & RSND_GEN_MASK) == RSND_GEN2)

/*
 *	R-Car ADG
 */
int rsnd_adg_ssi_clk_stop(struct rsnd_mod *mod);
int rsnd_adg_ssi_clk_try_start(struct rsnd_mod *mod, unsigned int rate);
int rsnd_adg_probe(struct platform_device *pdev,
		   struct rcar_snd_info *info,
		   struct rsnd_priv *priv);
void rsnd_adg_remove(struct platform_device *pdev,
		   struct rsnd_priv *priv);
int rsnd_adg_set_convert_clk(struct rsnd_priv *priv,
			     struct rsnd_mod *mod,
			     unsigned int src_rate,
			     unsigned int dst_rate);

/*
 *	R-Car sound priv
 */
struct rsnd_priv {

	struct device *dev;
	struct rcar_snd_info *info;
	spinlock_t lock;

	/*
	 * below value will be filled on rsnd_gen_probe()
	 */
	void *gen;

	/*
	 * below value will be filled on rsnd_scu_probe()
	 */
	void *scu;
	int scu_nr;

	/*
	 * below value will be filled on rsnd_adg_probe()
	 */
	void *adg;

	/*
	 * below value will be filled on rsnd_ssi_probe()
	 */
	void *ssiu;

	/*
	 * below value will be filled on rsnd_dai_probe()
	 */
	struct snd_soc_dai_driver *daidrv;
	struct rsnd_dai *rdai;
	int dai_nr;
};

#define rsnd_priv_to_dev(priv)	((priv)->dev)
#define rsnd_lock(priv, flags) spin_lock_irqsave(&priv->lock, flags)
#define rsnd_unlock(priv, flags) spin_unlock_irqrestore(&priv->lock, flags)

/*
 *	R-Car SCU
 */
int rsnd_scu_probe(struct platform_device *pdev,
		   struct rcar_snd_info *info,
		   struct rsnd_priv *priv);
void rsnd_scu_remove(struct platform_device *pdev,
		     struct rsnd_priv *priv);
struct rsnd_mod *rsnd_scu_mod_get(struct rsnd_priv *priv, int id);
bool rsnd_scu_hpbif_is_enable(struct rsnd_mod *mod);
unsigned int rsnd_scu_get_ssi_rate(struct rsnd_priv *priv,
				   struct rsnd_mod *ssi_mod,
				   struct snd_pcm_runtime *runtime);

#define rsnd_scu_nr(priv) ((priv)->scu_nr)

/*
 *	R-Car SSI
 */
int rsnd_ssi_probe(struct platform_device *pdev,
		   struct rcar_snd_info *info,
		   struct rsnd_priv *priv);
void rsnd_ssi_remove(struct platform_device *pdev,
		   struct rsnd_priv *priv);
struct rsnd_mod *rsnd_ssi_mod_get(struct rsnd_priv *priv, int id);
struct rsnd_mod *rsnd_ssi_mod_get_frm_dai(struct rsnd_priv *priv,
					  int dai_id, int is_play);

#endif
