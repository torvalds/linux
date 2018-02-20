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
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/sh_dma.h>
#include <linux/workqueue.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>

#include "rcar_snd.h"

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
	RSND_REG_SCU_SYS_STATUS0,
	RSND_REG_SCU_SYS_INT_EN0,
	RSND_REG_CMD_ROUTE_SLCT,
	RSND_REG_CTU_CTUIR,
	RSND_REG_CTU_ADINR,
	RSND_REG_MIX_SWRSR,
	RSND_REG_MIX_MIXIR,
	RSND_REG_MIX_ADINR,
	RSND_REG_MIX_MIXMR,
	RSND_REG_MIX_MVPDR,
	RSND_REG_MIX_MDBAR,
	RSND_REG_MIX_MDBBR,
	RSND_REG_MIX_MDBCR,
	RSND_REG_MIX_MDBDR,
	RSND_REG_MIX_MDBER,
	RSND_REG_DVC_SWRSR,
	RSND_REG_DVC_DVUIR,
	RSND_REG_DVC_ADINR,
	RSND_REG_DVC_DVUCR,
	RSND_REG_DVC_ZCMCR,
	RSND_REG_DVC_VOL0R,
	RSND_REG_DVC_VOL1R,
	RSND_REG_DVC_DVUER,

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
	RSND_REG_SHARE20,
	RSND_REG_SHARE21,
	RSND_REG_SHARE22,
	RSND_REG_SHARE23,
	RSND_REG_SHARE24,
	RSND_REG_SHARE25,
	RSND_REG_SHARE26,
	RSND_REG_SHARE27,
	RSND_REG_SHARE28,
	RSND_REG_SHARE29,

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
#define RSND_REG_SSI_INT_ENABLE		RSND_REG_SHARE05
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
#define RSND_REG_CMD_CTRL		RSND_REG_SHARE20
#define RSND_REG_CMDOUT_TIMSEL		RSND_REG_SHARE21
#define RSND_REG_SSI_BUSIF_DALIGN	RSND_REG_SHARE22
#define RSND_REG_DVC_VRCTR		RSND_REG_SHARE23
#define RSND_REG_DVC_VRPDR		RSND_REG_SHARE24
#define RSND_REG_DVC_VRDBR		RSND_REG_SHARE25
#define RSND_REG_SCU_SYS_STATUS1	RSND_REG_SHARE26
#define RSND_REG_SCU_SYS_INT_EN1	RSND_REG_SHARE27
#define RSND_REG_SRC_INT_ENABLE0	RSND_REG_SHARE28
#define RSND_REG_SRC_BUSIF_DALIGN	RSND_REG_SHARE29

struct rsnd_of_data;
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
#define rsnd_mod_force_write(m, r, d) \
	rsnd_force_write(rsnd_mod_to_priv(m), m, RSND_REG_##r, d)
#define rsnd_mod_bset(m, r, s, d) \
	rsnd_bset(rsnd_mod_to_priv(m), m, RSND_REG_##r, s, d)

u32 rsnd_read(struct rsnd_priv *priv, struct rsnd_mod *mod, enum rsnd_reg reg);
void rsnd_write(struct rsnd_priv *priv, struct rsnd_mod *mod,
		enum rsnd_reg reg, u32 data);
void rsnd_force_write(struct rsnd_priv *priv, struct rsnd_mod *mod,
		enum rsnd_reg reg, u32 data);
void rsnd_bset(struct rsnd_priv *priv, struct rsnd_mod *mod, enum rsnd_reg reg,
		    u32 mask, u32 data);
u32 rsnd_get_adinr_bit(struct rsnd_mod *mod, struct rsnd_dai_stream *io);
u32 rsnd_get_adinr_chan(struct rsnd_mod *mod, struct rsnd_dai_stream *io);
u32 rsnd_get_dalign(struct rsnd_mod *mod, struct rsnd_dai_stream *io);
void rsnd_path_parse(struct rsnd_priv *priv,
		     struct rsnd_dai_stream *io);

/*
 *	R-Car DMA
 */
struct rsnd_dma;

struct rsnd_dmaen {
	struct dma_chan		*chan;
};

struct rsnd_dmapp {
	int			dmapp_id;
	u32			chcr;
};

struct rsnd_dma {
	struct rsnd_dma_ops	*ops;
	dma_addr_t		src_addr;
	dma_addr_t		dst_addr;
	union {
		struct rsnd_dmaen en;
		struct rsnd_dmapp pp;
	} dma;
};
#define rsnd_dma_to_dmaen(dma)	(&(dma)->dma.en)
#define rsnd_dma_to_dmapp(dma)	(&(dma)->dma.pp)
#define rsnd_dma_to_mod(_dma) container_of((_dma), struct rsnd_mod, dma)

void rsnd_dma_start(struct rsnd_dai_stream *io, struct rsnd_dma *dma);
void rsnd_dma_stop(struct rsnd_dai_stream *io, struct rsnd_dma *dma);
int rsnd_dma_init(struct rsnd_dai_stream *io, struct rsnd_dma *dma, int id);
void rsnd_dma_quit(struct rsnd_dai_stream *io, struct rsnd_dma *dma);
int rsnd_dma_probe(struct platform_device *pdev,
		   const struct rsnd_of_data *of_data,
		   struct rsnd_priv *priv);
struct dma_chan *rsnd_dma_request_channel(struct device_node *of_node,
					  struct rsnd_mod *mod, char *name);

/*
 *	R-Car sound mod
 */
enum rsnd_mod_type {
	RSND_MOD_DVC = 0,
	RSND_MOD_MIX,
	RSND_MOD_CTU,
	RSND_MOD_SRC,
	RSND_MOD_SSIP, /* SSI parent */
	RSND_MOD_SSI,
	RSND_MOD_MAX,
};

struct rsnd_mod_ops {
	char *name;
	struct dma_chan* (*dma_req)(struct rsnd_dai_stream *io,
				    struct rsnd_mod *mod);
	int (*probe)(struct rsnd_mod *mod,
		     struct rsnd_dai_stream *io,
		     struct rsnd_priv *priv);
	int (*remove)(struct rsnd_mod *mod,
		      struct rsnd_dai_stream *io,
		      struct rsnd_priv *priv);
	int (*init)(struct rsnd_mod *mod,
		    struct rsnd_dai_stream *io,
		    struct rsnd_priv *priv);
	int (*quit)(struct rsnd_mod *mod,
		    struct rsnd_dai_stream *io,
		    struct rsnd_priv *priv);
	int (*start)(struct rsnd_mod *mod,
		     struct rsnd_dai_stream *io,
		     struct rsnd_priv *priv);
	int (*stop)(struct rsnd_mod *mod,
		    struct rsnd_dai_stream *io,
		    struct rsnd_priv *priv);
	int (*pcm_new)(struct rsnd_mod *mod,
		       struct rsnd_dai_stream *io,
		       struct snd_soc_pcm_runtime *rtd);
	int (*hw_params)(struct rsnd_mod *mod,
			 struct rsnd_dai_stream *io,
			 struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *hw_params);
	int (*fallback)(struct rsnd_mod *mod,
			struct rsnd_dai_stream *io,
			struct rsnd_priv *priv);
};

struct rsnd_dai_stream;
struct rsnd_mod {
	int id;
	enum rsnd_mod_type type;
	struct rsnd_mod_ops *ops;
	struct rsnd_dma dma;
	struct rsnd_priv *priv;
	struct clk *clk;
	u32 status;
};
/*
 * status
 *
 * 0xH0000CBA
 *
 * A	0: probe	1: remove
 * B	0: init		1: quit
 * C	0: start	1: stop
 *
 * H is always called (see __rsnd_mod_call)
 * H	0: pcm_new
 * H	0: fallback
 * H	0: hw_params
 */
#define __rsnd_mod_shift_probe		0
#define __rsnd_mod_shift_remove		0
#define __rsnd_mod_shift_init		4
#define __rsnd_mod_shift_quit		4
#define __rsnd_mod_shift_start		8
#define __rsnd_mod_shift_stop		8
#define __rsnd_mod_shift_pcm_new	28 /* always called */
#define __rsnd_mod_shift_fallback	28 /* always called */
#define __rsnd_mod_shift_hw_params	28 /* always called */

#define __rsnd_mod_add_probe		 1
#define __rsnd_mod_add_remove		-1
#define __rsnd_mod_add_init		 1
#define __rsnd_mod_add_quit		-1
#define __rsnd_mod_add_start		 1
#define __rsnd_mod_add_stop		-1
#define __rsnd_mod_add_pcm_new		0
#define __rsnd_mod_add_fallback		0
#define __rsnd_mod_add_hw_params	0

#define __rsnd_mod_call_probe		0
#define __rsnd_mod_call_remove		1
#define __rsnd_mod_call_init		0
#define __rsnd_mod_call_quit		1
#define __rsnd_mod_call_start		0
#define __rsnd_mod_call_stop		1
#define __rsnd_mod_call_pcm_new		0
#define __rsnd_mod_call_fallback	0
#define __rsnd_mod_call_hw_params	0

#define rsnd_mod_to_priv(mod) ((mod)->priv)
#define rsnd_mod_to_dma(mod) (&(mod)->dma)
#define rsnd_mod_id(mod) ((mod) ? (mod)->id : -1)
#define rsnd_mod_power_on(mod)	clk_enable((mod)->clk)
#define rsnd_mod_power_off(mod)	clk_disable((mod)->clk)
#define rsnd_mod_get(ip)	(&(ip)->mod)

int rsnd_mod_init(struct rsnd_priv *priv,
		  struct rsnd_mod *mod,
		   struct rsnd_mod_ops *ops,
		   struct clk *clk,
		   enum rsnd_mod_type type,
		   int id);
void rsnd_mod_quit(struct rsnd_mod *mod);
char *rsnd_mod_name(struct rsnd_mod *mod);
struct dma_chan *rsnd_mod_dma_req(struct rsnd_dai_stream *io,
				  struct rsnd_mod *mod);
void rsnd_mod_interrupt(struct rsnd_mod *mod,
			void (*callback)(struct rsnd_mod *mod,
					 struct rsnd_dai_stream *io));

/*
 *	R-Car sound DAI
 */
#define RSND_DAI_NAME_SIZE	16
struct rsnd_dai_stream {
	char name[RSND_DAI_NAME_SIZE];
	struct snd_pcm_substream *substream;
	struct rsnd_mod *mod[RSND_MOD_MAX];
	struct rsnd_dai_path_info *info; /* rcar_snd.h */
	struct rsnd_dai *rdai;
	int byte_pos;
	int period_pos;
	int byte_per_period;
	int next_period_byte;
};
#define rsnd_io_to_mod(io, i)	((i) < RSND_MOD_MAX ? (io)->mod[(i)] : NULL)
#define rsnd_io_to_mod_ssi(io)	rsnd_io_to_mod((io), RSND_MOD_SSI)
#define rsnd_io_to_mod_ssip(io) rsnd_io_to_mod((io), RSND_MOD_SSIP)
#define rsnd_io_to_mod_src(io)	rsnd_io_to_mod((io), RSND_MOD_SRC)
#define rsnd_io_to_mod_ctu(io)	rsnd_io_to_mod((io), RSND_MOD_CTU)
#define rsnd_io_to_mod_mix(io)	rsnd_io_to_mod((io), RSND_MOD_MIX)
#define rsnd_io_to_mod_dvc(io)	rsnd_io_to_mod((io), RSND_MOD_DVC)
#define rsnd_io_to_rdai(io)	((io)->rdai)
#define rsnd_io_to_priv(io)	(rsnd_rdai_to_priv(rsnd_io_to_rdai(io)))
#define rsnd_io_is_play(io)	(&rsnd_io_to_rdai(io)->playback == io)
#define rsnd_io_to_runtime(io) ((io)->substream ? \
				(io)->substream->runtime : NULL)
int rsnd_io_is_working(struct rsnd_dai_stream *io);

struct rsnd_dai {
	char name[RSND_DAI_NAME_SIZE];
	struct rsnd_dai_stream playback;
	struct rsnd_dai_stream capture;
	struct rsnd_priv *priv;

	unsigned int clk_master:1;
	unsigned int bit_clk_inv:1;
	unsigned int frm_clk_inv:1;
	unsigned int sys_delay:1;
	unsigned int data_alignment:1;
};

#define rsnd_rdai_nr(priv) ((priv)->rdai_nr)
#define rsnd_rdai_is_clk_master(rdai) ((rdai)->clk_master)
#define rsnd_rdai_to_priv(rdai) ((rdai)->priv)
#define for_each_rsnd_dai(rdai, priv, i)		\
	for (i = 0;					\
	     (i < rsnd_rdai_nr(priv)) &&		\
	     ((rdai) = rsnd_rdai_get(priv, i));		\
	     i++)

struct rsnd_dai *rsnd_rdai_get(struct rsnd_priv *priv, int id);

bool rsnd_dai_pointer_update(struct rsnd_dai_stream *io, int cnt);
void rsnd_dai_period_elapsed(struct rsnd_dai_stream *io);
int rsnd_dai_pointer_offset(struct rsnd_dai_stream *io, int additional);

/*
 *	R-Car Gen1/Gen2
 */
int rsnd_gen_probe(struct platform_device *pdev,
		   const struct rsnd_of_data *of_data,
		   struct rsnd_priv *priv);
void __iomem *rsnd_gen_reg_get(struct rsnd_priv *priv,
			       struct rsnd_mod *mod,
			       enum rsnd_reg reg);
phys_addr_t rsnd_gen_get_phy_addr(struct rsnd_priv *priv, int reg_id);

#define rsnd_is_gen1(s)		(((s)->info->flags & RSND_GEN_MASK) == RSND_GEN1)
#define rsnd_is_gen2(s)		(((s)->info->flags & RSND_GEN_MASK) == RSND_GEN2)

/*
 *	R-Car ADG
 */
int rsnd_adg_ssi_clk_stop(struct rsnd_mod *mod);
int rsnd_adg_ssi_clk_try_start(struct rsnd_mod *mod, unsigned int rate);
int rsnd_adg_probe(struct platform_device *pdev,
		   const struct rsnd_of_data *of_data,
		   struct rsnd_priv *priv);
int rsnd_adg_set_convert_clk_gen1(struct rsnd_priv *priv,
				  struct rsnd_mod *mod,
				  unsigned int src_rate,
				  unsigned int dst_rate);
int rsnd_adg_set_convert_clk_gen2(struct rsnd_mod *mod,
				  struct rsnd_dai_stream *io,
				  unsigned int src_rate,
				  unsigned int dst_rate);
int rsnd_adg_set_convert_timing_gen2(struct rsnd_mod *mod,
				     struct rsnd_dai_stream *io);
int rsnd_adg_set_cmd_timsel_gen2(struct rsnd_mod *mod,
				 struct rsnd_dai_stream *io);

/*
 *	R-Car sound priv
 */
struct rsnd_of_data {
	u32 flags;
};

struct rsnd_priv {

	struct platform_device *pdev;
	struct rcar_snd_info *info;
	spinlock_t lock;

	/*
	 * below value will be filled on rsnd_gen_probe()
	 */
	void *gen;

	/*
	 * below value will be filled on rsnd_adg_probe()
	 */
	void *adg;

	/*
	 * below value will be filled on rsnd_dma_probe()
	 */
	void *dma;

	/*
	 * below value will be filled on rsnd_ssi_probe()
	 */
	void *ssi;
	int ssi_nr;

	/*
	 * below value will be filled on rsnd_src_probe()
	 */
	void *src;
	int src_nr;

	/*
	 * below value will be filled on rsnd_ctu_probe()
	 */
	void *ctu;
	int ctu_nr;

	/*
	 * below value will be filled on rsnd_mix_probe()
	 */
	void *mix;
	int mix_nr;

	/*
	 * below value will be filled on rsnd_dvc_probe()
	 */
	void *dvc;
	int dvc_nr;

	/*
	 * below value will be filled on rsnd_dai_probe()
	 */
	struct snd_soc_dai_driver *daidrv;
	struct rsnd_dai *rdai;
	int rdai_nr;
};

#define rsnd_priv_to_pdev(priv)	((priv)->pdev)
#define rsnd_priv_to_dev(priv)	(&(rsnd_priv_to_pdev(priv)->dev))
#define rsnd_priv_to_info(priv)	((priv)->info)

/*
 *	rsnd_kctrl
 */
struct rsnd_kctrl_cfg {
	unsigned int max;
	unsigned int size;
	u32 *val;
	const char * const *texts;
	void (*update)(struct rsnd_dai_stream *io, struct rsnd_mod *mod);
	struct rsnd_dai_stream *io;
	struct snd_card *card;
	struct snd_kcontrol *kctrl;
};

#define RSND_DVC_CHANNELS	2
struct rsnd_kctrl_cfg_m {
	struct rsnd_kctrl_cfg cfg;
	u32 val[RSND_DVC_CHANNELS];
};

struct rsnd_kctrl_cfg_s {
	struct rsnd_kctrl_cfg cfg;
	u32 val;
};

void _rsnd_kctrl_remove(struct rsnd_kctrl_cfg *cfg);
#define rsnd_kctrl_remove(_cfg)	_rsnd_kctrl_remove(&((_cfg).cfg))

int rsnd_kctrl_new_m(struct rsnd_mod *mod,
		     struct rsnd_dai_stream *io,
		     struct snd_soc_pcm_runtime *rtd,
		     const unsigned char *name,
		     void (*update)(struct rsnd_dai_stream *io,
				    struct rsnd_mod *mod),
		     struct rsnd_kctrl_cfg_m *_cfg,
		     u32 max);
int rsnd_kctrl_new_s(struct rsnd_mod *mod,
		     struct rsnd_dai_stream *io,
		     struct snd_soc_pcm_runtime *rtd,
		     const unsigned char *name,
		     void (*update)(struct rsnd_dai_stream *io,
				    struct rsnd_mod *mod),
		     struct rsnd_kctrl_cfg_s *_cfg,
		     u32 max);
int rsnd_kctrl_new_e(struct rsnd_mod *mod,
		     struct rsnd_dai_stream *io,
		     struct snd_soc_pcm_runtime *rtd,
		     const unsigned char *name,
		     struct rsnd_kctrl_cfg_s *_cfg,
		     void (*update)(struct rsnd_dai_stream *io,
				    struct rsnd_mod *mod),
		     const char * const *texts,
		     u32 max);

/*
 *	R-Car SSI
 */
int rsnd_ssi_probe(struct platform_device *pdev,
		   const struct rsnd_of_data *of_data,
		   struct rsnd_priv *priv);
void rsnd_ssi_remove(struct platform_device *pdev,
		     struct rsnd_priv *priv);
struct rsnd_mod *rsnd_ssi_mod_get(struct rsnd_priv *priv, int id);
int rsnd_ssi_is_dma_mode(struct rsnd_mod *mod);
int rsnd_ssi_use_busif(struct rsnd_dai_stream *io);

#define rsnd_ssi_is_pin_sharing(io)	\
	__rsnd_ssi_is_pin_sharing(rsnd_io_to_mod_ssi(io))
int __rsnd_ssi_is_pin_sharing(struct rsnd_mod *mod);

/*
 *	R-Car SRC
 */
int rsnd_src_probe(struct platform_device *pdev,
		   const struct rsnd_of_data *of_data,
		   struct rsnd_priv *priv);
void rsnd_src_remove(struct platform_device *pdev,
		     struct rsnd_priv *priv);
struct rsnd_mod *rsnd_src_mod_get(struct rsnd_priv *priv, int id);
unsigned int rsnd_src_get_ssi_rate(struct rsnd_priv *priv,
				   struct rsnd_dai_stream *io,
				   struct snd_pcm_runtime *runtime);
int rsnd_src_ssiu_start(struct rsnd_mod *ssi_mod,
			struct rsnd_dai_stream *io,
			int use_busif);
int rsnd_src_ssiu_stop(struct rsnd_mod *ssi_mod,
		       struct rsnd_dai_stream *io);
int rsnd_src_ssi_irq_enable(struct rsnd_mod *ssi_mod);
int rsnd_src_ssi_irq_disable(struct rsnd_mod *ssi_mod);

/*
 *	R-Car CTU
 */
int rsnd_ctu_probe(struct platform_device *pdev,
		   const struct rsnd_of_data *of_data,
		   struct rsnd_priv *priv);

void rsnd_ctu_remove(struct platform_device *pdev,
		     struct rsnd_priv *priv);
struct rsnd_mod *rsnd_ctu_mod_get(struct rsnd_priv *priv, int id);

/*
 *	R-Car MIX
 */
int rsnd_mix_probe(struct platform_device *pdev,
		   const struct rsnd_of_data *of_data,
		   struct rsnd_priv *priv);

void rsnd_mix_remove(struct platform_device *pdev,
		     struct rsnd_priv *priv);
struct rsnd_mod *rsnd_mix_mod_get(struct rsnd_priv *priv, int id);

/*
 *	R-Car DVC
 */
int rsnd_dvc_probe(struct platform_device *pdev,
		   const struct rsnd_of_data *of_data,
		   struct rsnd_priv *priv);
void rsnd_dvc_remove(struct platform_device *pdev,
		     struct rsnd_priv *priv);
struct rsnd_mod *rsnd_dvc_mod_get(struct rsnd_priv *priv, int id);

#ifdef DEBUG
void rsnd_mod_make_sure(struct rsnd_mod *mod, enum rsnd_mod_type type);
#define rsnd_mod_confirm_ssi(mssi)	rsnd_mod_make_sure(mssi, RSND_MOD_SSI)
#define rsnd_mod_confirm_src(msrc)	rsnd_mod_make_sure(msrc, RSND_MOD_SRC)
#define rsnd_mod_confirm_dvc(mdvc)	rsnd_mod_make_sure(mdvc, RSND_MOD_DVC)
#else
#define rsnd_mod_confirm_ssi(mssi)
#define rsnd_mod_confirm_src(msrc)
#define rsnd_mod_confirm_dvc(mdvc)
#endif

#endif
