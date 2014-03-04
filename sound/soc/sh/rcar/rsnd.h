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
	RSND_REG_SSI_MODE0,
	RSND_REG_SSI_MODE1,
	RSND_REG_SRC_BUSIF_MODE,
	RSND_REG_SRC_ROUTE_MODE0,
	RSND_REG_SRC_SWRSR,
	RSND_REG_SRC_SRCIR,
	RSND_REG_SRC_ADINR,
	RSND_REG_SRC_IFSCR,
	RSND_REG_SRC_IFSVR,
	RSND_REG_SRC_SRCCR,

	/* ADG */
	RSND_REG_BRRA,
	RSND_REG_BRRB,
	RSND_REG_SSICKR,
	RSND_REG_AUDIO_CLK_SEL0,
	RSND_REG_AUDIO_CLK_SEL1,

	/* SSI */
	RSND_REG_SSICR,
	RSND_REG_SSISR,
	RSND_REG_SSITDR,
	RSND_REG_SSIRDR,
	RSND_REG_SSIWSR,

	/* SHARE see below */
	RSND_REG_SHARE01,
	RSND_REG_SHARE02,
	RSND_REG_SHARE03,
	RSND_REG_SHARE04,
	RSND_REG_SHARE05,
	RSND_REG_SHARE06,
	RSND_REG_SHARE07,
	RSND_REG_SHARE08,
	RSND_REG_SHARE09,
	RSND_REG_SHARE10,
	RSND_REG_SHARE11,
	RSND_REG_SHARE12,
	RSND_REG_SHARE13,
	RSND_REG_SHARE14,
	RSND_REG_SHARE15,
	RSND_REG_SHARE16,
	RSND_REG_SHARE17,
	RSND_REG_SHARE18,
	RSND_REG_SHARE19,

	RSND_REG_MAX,
};

/* Gen1 only */
#define RSND_REG_SRC_ROUTE_SEL		RSND_REG_SHARE01
#define RSND_REG_SRC_TMG_SEL0		RSND_REG_SHARE02
#define RSND_REG_SRC_TMG_SEL1		RSND_REG_SHARE03
#define RSND_REG_SRC_TMG_SEL2		RSND_REG_SHARE04
#define RSND_REG_SRC_ROUTE_CTRL		RSND_REG_SHARE05
#define RSND_REG_SRC_MNFSR		RSND_REG_SHARE06
#define RSND_REG_AUDIO_CLK_SEL3		RSND_REG_SHARE07
#define RSND_REG_AUDIO_CLK_SEL4		RSND_REG_SHARE08
#define RSND_REG_AUDIO_CLK_SEL5		RSND_REG_SHARE09

/* Gen2 only */
#define RSND_REG_SRC_CTRL		RSND_REG_SHARE01
#define RSND_REG_SSI_CTRL		RSND_REG_SHARE02
#define RSND_REG_SSI_BUSIF_MODE		RSND_REG_SHARE03
#define RSND_REG_SSI_BUSIF_ADINR	RSND_REG_SHARE04
#define RSND_REG_INT_ENABLE		RSND_REG_SHARE05
#define RSND_REG_SRC_BSDSR		RSND_REG_SHARE06
#define RSND_REG_SRC_BSISR		RSND_REG_SHARE07
#define RSND_REG_DIV_EN			RSND_REG_SHARE08
#define RSND_REG_SRCIN_TIMSEL0		RSND_REG_SHARE09
#define RSND_REG_SRCIN_TIMSEL1		RSND_REG_SHARE10
#define RSND_REG_SRCIN_TIMSEL2		RSND_REG_SHARE11
#define RSND_REG_SRCIN_TIMSEL3		RSND_REG_SHARE12
#define RSND_REG_SRCIN_TIMSEL4		RSND_REG_SHARE13
#define RSND_REG_SRCOUT_TIMSEL0		RSND_REG_SHARE14
#define RSND_REG_SRCOUT_TIMSEL1		RSND_REG_SHARE15
#define RSND_REG_SRCOUT_TIMSEL2		RSND_REG_SHARE16
#define RSND_REG_SRCOUT_TIMSEL3		RSND_REG_SHARE17
#define RSND_REG_SRCOUT_TIMSEL4		RSND_REG_SHARE18
#define RSND_REG_AUDIO_CLK_SEL2		RSND_REG_SHARE19

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
	struct sh_dmae_slave	slave;
	struct work_struct	work;
	struct dma_chan		*chan;
	enum dma_data_direction dir;

	int submit_loop;
	int offset; /* it cares A/B plane */
};

void rsnd_dma_start(struct rsnd_dma *dma);
void rsnd_dma_stop(struct rsnd_dma *dma);
int rsnd_dma_available(struct rsnd_dma *dma);
int rsnd_dma_init(struct rsnd_priv *priv, struct rsnd_dma *dma,
	int is_play, int id);
void  rsnd_dma_quit(struct rsnd_priv *priv,
		    struct rsnd_dma *dma);


/*
 *	R-Car sound mod
 */
enum rsnd_mod_type {
	RSND_MOD_SCU = 0,
	RSND_MOD_SSI,
	RSND_MOD_MAX,
};

struct rsnd_mod_ops {
	char *name;
	int (*probe)(struct rsnd_mod *mod,
		     struct rsnd_dai *rdai,
		     struct rsnd_dai_stream *io);
	int (*remove)(struct rsnd_mod *mod,
		      struct rsnd_dai *rdai,
		      struct rsnd_dai_stream *io);
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

struct rsnd_dai_stream;
struct rsnd_mod {
	int id;
	enum rsnd_mod_type type;
	struct rsnd_priv *priv;
	struct rsnd_mod_ops *ops;
	struct rsnd_dma dma;
	struct rsnd_dai_stream *io;
};

#define rsnd_mod_to_priv(mod) ((mod)->priv)
#define rsnd_mod_to_dma(mod) (&(mod)->dma)
#define rsnd_dma_to_mod(_dma) container_of((_dma), struct rsnd_mod, dma)
#define rsnd_mod_to_io(mod) ((mod)->io)
#define rsnd_mod_id(mod) ((mod)->id)

void rsnd_mod_init(struct rsnd_priv *priv,
		   struct rsnd_mod *mod,
		   struct rsnd_mod_ops *ops,
		   enum rsnd_mod_type type,
		   int id);
char *rsnd_mod_name(struct rsnd_mod *mod);

/*
 *	R-Car sound DAI
 */
#define RSND_DAI_NAME_SIZE	16
struct rsnd_dai_stream {
	struct snd_pcm_substream *substream;
	struct rsnd_mod *mod[RSND_MOD_MAX];
	struct rsnd_dai_path_info *info; /* rcar_snd.h */
	int byte_pos;
	int period_pos;
	int byte_per_period;
	int next_period_byte;
};
#define rsnd_io_to_mod_ssi(io)	((io)->mod[RSND_MOD_SSI])
#define rsnd_io_to_mod_scu(io)	((io)->mod[RSND_MOD_SCU])

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

#define rsnd_rdai_nr(priv) ((priv)->rdai_nr)
#define for_each_rsnd_dai(rdai, priv, i)		\
	for (i = 0;					\
	     (i < rsnd_rdai_nr(priv)) &&		\
	     ((rdai) = rsnd_dai_get(priv, i));		\
	     i++)

struct rsnd_dai *rsnd_dai_get(struct rsnd_priv *priv, int id);
int rsnd_dai_is_play(struct rsnd_dai *rdai, struct rsnd_dai_stream *io);
int rsnd_dai_id(struct rsnd_priv *priv, struct rsnd_dai *rdai);
#define rsnd_dai_get_platform_info(rdai) ((rdai)->info)
#define rsnd_io_to_runtime(io) ((io)->substream->runtime)

void rsnd_dai_pointer_update(struct rsnd_dai_stream *io, int cnt);
int rsnd_dai_pointer_offset(struct rsnd_dai_stream *io, int additional);
#define rsnd_dai_is_clk_master(rdai) ((rdai)->clk_master)

/*
 *	R-Car Gen1/Gen2
 */
int rsnd_gen_probe(struct platform_device *pdev,
		   struct rsnd_priv *priv);
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
		   struct rsnd_priv *priv);
int rsnd_adg_set_convert_clk_gen1(struct rsnd_priv *priv,
				  struct rsnd_mod *mod,
				  unsigned int src_rate,
				  unsigned int dst_rate);
int rsnd_adg_set_convert_clk_gen2(struct rsnd_mod *mod,
				  struct rsnd_dai *rdai,
				  struct rsnd_dai_stream *io,
				  unsigned int src_rate,
				  unsigned int dst_rate);
int rsnd_adg_set_convert_timing_gen2(struct rsnd_mod *mod,
				     struct rsnd_dai *rdai,
				     struct rsnd_dai_stream *io);

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
	void *ssi;
	int ssi_nr;

	/*
	 * below value will be filled on rsnd_dai_probe()
	 */
	struct snd_soc_dai_driver *daidrv;
	struct rsnd_dai *rdai;
	int rdai_nr;
};

#define rsnd_priv_to_dev(priv)	((priv)->dev)
#define rsnd_priv_to_info(priv)	((priv)->info)
#define rsnd_lock(priv, flags) spin_lock_irqsave(&priv->lock, flags)
#define rsnd_unlock(priv, flags) spin_unlock_irqrestore(&priv->lock, flags)

#define rsnd_info_is_playback(priv, type)				\
({									\
	struct rcar_snd_info *info = rsnd_priv_to_info(priv);		\
	int i, is_play = 0;						\
	for (i = 0; i < info->dai_info_nr; i++) {			\
		if (info->dai_info[i].playback.type == (type)->info) {	\
			is_play = 1;					\
			break;						\
		}							\
	}								\
	is_play;							\
})

/*
 *	R-Car SCU
 */
int rsnd_scu_probe(struct platform_device *pdev,
		   struct rsnd_priv *priv);
struct rsnd_mod *rsnd_scu_mod_get(struct rsnd_priv *priv, int id);
unsigned int rsnd_scu_get_ssi_rate(struct rsnd_priv *priv,
				   struct rsnd_dai_stream *io,
				   struct snd_pcm_runtime *runtime);
int rsnd_scu_ssi_mode_init(struct rsnd_mod *ssi_mod,
			   struct rsnd_dai *rdai,
			   struct rsnd_dai_stream *io);
int rsnd_scu_enable_ssi_irq(struct rsnd_mod *ssi_mod,
			    struct rsnd_dai *rdai,
			    struct rsnd_dai_stream *io);

#define rsnd_scu_nr(priv) ((priv)->scu_nr)

/*
 *	R-Car SSI
 */
int rsnd_ssi_probe(struct platform_device *pdev,
		   struct rsnd_priv *priv);
struct rsnd_mod *rsnd_ssi_mod_get(struct rsnd_priv *priv, int id);
struct rsnd_mod *rsnd_ssi_mod_get_frm_dai(struct rsnd_priv *priv,
					  int dai_id, int is_play);
int rsnd_ssi_is_pin_sharing(struct rsnd_mod *mod);
int rsnd_ssi_is_play(struct rsnd_mod *mod);

#endif
