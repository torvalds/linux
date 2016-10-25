/*
 * Renesas R-Car Audio DMAC support
 *
 * Copyright (C) 2015 Renesas Electronics Corp.
 * Copyright (c) 2015 Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/delay.h>
#include <linux/of_dma.h>
#include "rsnd.h"

/*
 * Audio DMAC peri peri register
 */
#define PDMASAR		0x00
#define PDMADAR		0x04
#define PDMACHCR	0x0c

/* PDMACHCR */
#define PDMACHCR_DE		(1 << 0)


struct rsnd_dmaen {
	struct dma_chan		*chan;
};

struct rsnd_dmapp {
	int			dmapp_id;
	u32			chcr;
};

struct rsnd_dma {
	struct rsnd_mod		mod;
	dma_addr_t		src_addr;
	dma_addr_t		dst_addr;
	union {
		struct rsnd_dmaen en;
		struct rsnd_dmapp pp;
	} dma;
};

struct rsnd_dma_ctrl {
	void __iomem *base;
	int dmaen_num;
	int dmapp_num;
};

#define rsnd_priv_to_dmac(p)	((struct rsnd_dma_ctrl *)(p)->dma)
#define rsnd_mod_to_dma(_mod) container_of((_mod), struct rsnd_dma, mod)
#define rsnd_dma_to_dmaen(dma)	(&(dma)->dma.en)
#define rsnd_dma_to_dmapp(dma)	(&(dma)->dma.pp)

/*
 *		Audio DMAC
 */
static void __rsnd_dmaen_complete(struct rsnd_mod *mod,
				  struct rsnd_dai_stream *io)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	bool elapsed = false;
	unsigned long flags;

	/*
	 * Renesas sound Gen1 needs 1 DMAC,
	 * Gen2 needs 2 DMAC.
	 * In Gen2 case, it are Audio-DMAC, and Audio-DMAC-peri-peri.
	 * But, Audio-DMAC-peri-peri doesn't have interrupt,
	 * and this driver is assuming that here.
	 *
	 * If Audio-DMAC-peri-peri has interrpt,
	 * rsnd_dai_pointer_update() will be called twice,
	 * ant it will breaks io->byte_pos
	 */
	spin_lock_irqsave(&priv->lock, flags);

	if (rsnd_io_is_working(io))
		elapsed = rsnd_dai_pointer_update(io, io->byte_per_period);

	spin_unlock_irqrestore(&priv->lock, flags);

	if (elapsed)
		rsnd_dai_period_elapsed(io);
}

static void rsnd_dmaen_complete(void *data)
{
	struct rsnd_mod *mod = data;

	rsnd_mod_interrupt(mod, __rsnd_dmaen_complete);
}

static int rsnd_dmaen_stop(struct rsnd_mod *mod,
			   struct rsnd_dai_stream *io,
			   struct rsnd_priv *priv)
{
	struct rsnd_dma *dma = rsnd_mod_to_dma(mod);
	struct rsnd_dmaen *dmaen = rsnd_dma_to_dmaen(dma);

	dmaengine_terminate_all(dmaen->chan);

	return 0;
}

static int rsnd_dmaen_start(struct rsnd_mod *mod,
			    struct rsnd_dai_stream *io,
			    struct rsnd_priv *priv)
{
	struct rsnd_dma *dma = rsnd_mod_to_dma(mod);
	struct rsnd_dmaen *dmaen = rsnd_dma_to_dmaen(dma);
	struct snd_pcm_substream *substream = io->substream;
	struct device *dev = rsnd_priv_to_dev(priv);
	struct dma_async_tx_descriptor *desc;
	int is_play = rsnd_io_is_play(io);

	desc = dmaengine_prep_dma_cyclic(dmaen->chan,
					 substream->runtime->dma_addr,
					 snd_pcm_lib_buffer_bytes(substream),
					 snd_pcm_lib_period_bytes(substream),
					 is_play ? DMA_MEM_TO_DEV : DMA_DEV_TO_MEM,
					 DMA_PREP_INTERRUPT | DMA_CTRL_ACK);

	if (!desc) {
		dev_err(dev, "dmaengine_prep_slave_sg() fail\n");
		return -EIO;
	}

	desc->callback		= rsnd_dmaen_complete;
	desc->callback_param	= rsnd_mod_get(dma);

	if (dmaengine_submit(desc) < 0) {
		dev_err(dev, "dmaengine_submit() fail\n");
		return -EIO;
	}

	dma_async_issue_pending(dmaen->chan);

	return 0;
}

struct dma_chan *rsnd_dma_request_channel(struct device_node *of_node,
					  struct rsnd_mod *mod, char *name)
{
	struct dma_chan *chan;
	struct device_node *np;
	int i = 0;

	for_each_child_of_node(of_node, np) {
		if (i == rsnd_mod_id(mod))
			break;
		i++;
	}

	chan = of_dma_request_slave_channel(np, name);

	of_node_put(np);
	of_node_put(of_node);

	return chan;
}

static struct dma_chan *rsnd_dmaen_request_channel(struct rsnd_dai_stream *io,
						   struct rsnd_mod *mod_from,
						   struct rsnd_mod *mod_to)
{
	if ((!mod_from && !mod_to) ||
	    (mod_from && mod_to))
		return NULL;

	if (mod_from)
		return rsnd_mod_dma_req(io, mod_from);
	else
		return rsnd_mod_dma_req(io, mod_to);
}

static int rsnd_dmaen_remove(struct rsnd_mod *mod,
			      struct rsnd_dai_stream *io,
			      struct rsnd_priv *priv)
{
	struct rsnd_dma *dma = rsnd_mod_to_dma(mod);
	struct rsnd_dmaen *dmaen = rsnd_dma_to_dmaen(dma);

	if (dmaen->chan)
		dma_release_channel(dmaen->chan);

	dmaen->chan = NULL;

	return 0;
}

static int rsnd_dmaen_attach(struct rsnd_dai_stream *io,
			   struct rsnd_dma *dma,
			   struct rsnd_mod *mod_from, struct rsnd_mod *mod_to)
{
	struct rsnd_mod *mod = rsnd_mod_get(dma);
	struct rsnd_dmaen *dmaen = rsnd_dma_to_dmaen(dma);
	struct rsnd_priv *priv = rsnd_io_to_priv(io);
	struct rsnd_dma_ctrl *dmac = rsnd_priv_to_dmac(priv);
	struct device *dev = rsnd_priv_to_dev(priv);
	struct dma_slave_config cfg = {};
	int is_play = rsnd_io_is_play(io);
	int ret;

	if (dmaen->chan) {
		dev_err(dev, "it already has dma channel\n");
		return -EIO;
	}

	dmaen->chan = rsnd_dmaen_request_channel(io, mod_from, mod_to);

	if (IS_ERR_OR_NULL(dmaen->chan)) {
		dmaen->chan = NULL;
		dev_err(dev, "can't get dma channel\n");
		goto rsnd_dma_channel_err;
	}

	cfg.direction	= is_play ? DMA_MEM_TO_DEV : DMA_DEV_TO_MEM;
	cfg.src_addr	= dma->src_addr;
	cfg.dst_addr	= dma->dst_addr;
	cfg.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	cfg.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;

	dev_dbg(dev, "%s[%d] %pad -> %pad\n",
		rsnd_mod_name(mod), rsnd_mod_id(mod),
		&cfg.src_addr, &cfg.dst_addr);

	ret = dmaengine_slave_config(dmaen->chan, &cfg);
	if (ret < 0)
		goto rsnd_dma_attach_err;

	dmac->dmaen_num++;

	return 0;

rsnd_dma_attach_err:
	rsnd_dmaen_remove(mod, io, priv);
rsnd_dma_channel_err:

	/*
	 * DMA failed. try to PIO mode
	 * see
	 *	rsnd_ssi_fallback()
	 *	rsnd_rdai_continuance_probe()
	 */
	return -EAGAIN;
}

static struct rsnd_mod_ops rsnd_dmaen_ops = {
	.name	= "audmac",
	.start	= rsnd_dmaen_start,
	.stop	= rsnd_dmaen_stop,
	.remove	= rsnd_dmaen_remove,
};

/*
 *		Audio DMAC peri peri
 */
static const u8 gen2_id_table_ssiu[] = {
	0x00, /* SSI00 */
	0x04, /* SSI10 */
	0x08, /* SSI20 */
	0x0c, /* SSI3  */
	0x0d, /* SSI4  */
	0x0e, /* SSI5  */
	0x0f, /* SSI6  */
	0x10, /* SSI7  */
	0x11, /* SSI8  */
	0x12, /* SSI90 */
};
static const u8 gen2_id_table_scu[] = {
	0x2d, /* SCU_SRCI0 */
	0x2e, /* SCU_SRCI1 */
	0x2f, /* SCU_SRCI2 */
	0x30, /* SCU_SRCI3 */
	0x31, /* SCU_SRCI4 */
	0x32, /* SCU_SRCI5 */
	0x33, /* SCU_SRCI6 */
	0x34, /* SCU_SRCI7 */
	0x35, /* SCU_SRCI8 */
	0x36, /* SCU_SRCI9 */
};
static const u8 gen2_id_table_cmd[] = {
	0x37, /* SCU_CMD0 */
	0x38, /* SCU_CMD1 */
};

static u32 rsnd_dmapp_get_id(struct rsnd_dai_stream *io,
			     struct rsnd_mod *mod)
{
	struct rsnd_mod *ssi = rsnd_io_to_mod_ssi(io);
	struct rsnd_mod *src = rsnd_io_to_mod_src(io);
	struct rsnd_mod *dvc = rsnd_io_to_mod_dvc(io);
	const u8 *entry = NULL;
	int id = rsnd_mod_id(mod);
	int size = 0;

	if (mod == ssi) {
		entry = gen2_id_table_ssiu;
		size = ARRAY_SIZE(gen2_id_table_ssiu);
	} else if (mod == src) {
		entry = gen2_id_table_scu;
		size = ARRAY_SIZE(gen2_id_table_scu);
	} else if (mod == dvc) {
		entry = gen2_id_table_cmd;
		size = ARRAY_SIZE(gen2_id_table_cmd);
	}

	if ((!entry) || (size <= id)) {
		struct device *dev = rsnd_priv_to_dev(rsnd_io_to_priv(io));

		dev_err(dev, "unknown connection (%s[%d])\n",
			rsnd_mod_name(mod), rsnd_mod_id(mod));

		/* use non-prohibited SRS number as error */
		return 0x00; /* SSI00 */
	}

	return entry[id];
}

static u32 rsnd_dmapp_get_chcr(struct rsnd_dai_stream *io,
			       struct rsnd_mod *mod_from,
			       struct rsnd_mod *mod_to)
{
	return	(rsnd_dmapp_get_id(io, mod_from) << 24) +
		(rsnd_dmapp_get_id(io, mod_to) << 16);
}

#define rsnd_dmapp_addr(dmac, dma, reg) \
	(dmac->base + 0x20 + reg + \
	 (0x10 * rsnd_dma_to_dmapp(dma)->dmapp_id))
static void rsnd_dmapp_write(struct rsnd_dma *dma, u32 data, u32 reg)
{
	struct rsnd_mod *mod = rsnd_mod_get(dma);
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct rsnd_dma_ctrl *dmac = rsnd_priv_to_dmac(priv);
	struct device *dev = rsnd_priv_to_dev(priv);

	dev_dbg(dev, "w %p : %08x\n", rsnd_dmapp_addr(dmac, dma, reg), data);

	iowrite32(data, rsnd_dmapp_addr(dmac, dma, reg));
}

static u32 rsnd_dmapp_read(struct rsnd_dma *dma, u32 reg)
{
	struct rsnd_mod *mod = rsnd_mod_get(dma);
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct rsnd_dma_ctrl *dmac = rsnd_priv_to_dmac(priv);

	return ioread32(rsnd_dmapp_addr(dmac, dma, reg));
}

static int rsnd_dmapp_stop(struct rsnd_mod *mod,
			   struct rsnd_dai_stream *io,
			   struct rsnd_priv *priv)
{
	struct rsnd_dma *dma = rsnd_mod_to_dma(mod);
	int i;

	rsnd_dmapp_write(dma, 0, PDMACHCR);

	for (i = 0; i < 1024; i++) {
		if (0 == rsnd_dmapp_read(dma, PDMACHCR))
			return 0;
		udelay(1);
	}

	return -EIO;
}

static int rsnd_dmapp_start(struct rsnd_mod *mod,
			    struct rsnd_dai_stream *io,
			    struct rsnd_priv *priv)
{
	struct rsnd_dma *dma = rsnd_mod_to_dma(mod);
	struct rsnd_dmapp *dmapp = rsnd_dma_to_dmapp(dma);

	rsnd_dmapp_write(dma, dma->src_addr,	PDMASAR);
	rsnd_dmapp_write(dma, dma->dst_addr,	PDMADAR);
	rsnd_dmapp_write(dma, dmapp->chcr,	PDMACHCR);

	return 0;
}

static int rsnd_dmapp_attach(struct rsnd_dai_stream *io,
			     struct rsnd_dma *dma,
			     struct rsnd_mod *mod_from, struct rsnd_mod *mod_to)
{
	struct rsnd_dmapp *dmapp = rsnd_dma_to_dmapp(dma);
	struct rsnd_priv *priv = rsnd_io_to_priv(io);
	struct rsnd_dma_ctrl *dmac = rsnd_priv_to_dmac(priv);
	struct device *dev = rsnd_priv_to_dev(priv);

	dmapp->dmapp_id = dmac->dmapp_num;
	dmapp->chcr = rsnd_dmapp_get_chcr(io, mod_from, mod_to) | PDMACHCR_DE;

	dmac->dmapp_num++;

	dev_dbg(dev, "id/src/dst/chcr = %d/%pad/%pad/%08x\n",
		dmapp->dmapp_id, &dma->src_addr, &dma->dst_addr, dmapp->chcr);

	return 0;
}

static struct rsnd_mod_ops rsnd_dmapp_ops = {
	.name	= "audmac-pp",
	.start	= rsnd_dmapp_start,
	.stop	= rsnd_dmapp_stop,
	.quit	= rsnd_dmapp_stop,
};

/*
 *		Common DMAC Interface
 */

/*
 *	DMA read/write register offset
 *
 *	RSND_xxx_I_N	for Audio DMAC input
 *	RSND_xxx_O_N	for Audio DMAC output
 *	RSND_xxx_I_P	for Audio DMAC peri peri input
 *	RSND_xxx_O_P	for Audio DMAC peri peri output
 *
 *	ex) R-Car H2 case
 *	      mod        / DMAC in    / DMAC out   / DMAC PP in / DMAC pp out
 *	SSI : 0xec541000 / 0xec241008 / 0xec24100c
 *	SSIU: 0xec541000 / 0xec100000 / 0xec100000 / 0xec400000 / 0xec400000
 *	SCU : 0xec500000 / 0xec000000 / 0xec004000 / 0xec300000 / 0xec304000
 *	CMD : 0xec500000 /            / 0xec008000                0xec308000
 */
#define RDMA_SSI_I_N(addr, i)	(addr ##_reg - 0x00300000 + (0x40 * i) + 0x8)
#define RDMA_SSI_O_N(addr, i)	(addr ##_reg - 0x00300000 + (0x40 * i) + 0xc)

#define RDMA_SSIU_I_N(addr, i)	(addr ##_reg - 0x00441000 + (0x1000 * i))
#define RDMA_SSIU_O_N(addr, i)	(addr ##_reg - 0x00441000 + (0x1000 * i))

#define RDMA_SSIU_I_P(addr, i)	(addr ##_reg - 0x00141000 + (0x1000 * i))
#define RDMA_SSIU_O_P(addr, i)	(addr ##_reg - 0x00141000 + (0x1000 * i))

#define RDMA_SRC_I_N(addr, i)	(addr ##_reg - 0x00500000 + (0x400 * i))
#define RDMA_SRC_O_N(addr, i)	(addr ##_reg - 0x004fc000 + (0x400 * i))

#define RDMA_SRC_I_P(addr, i)	(addr ##_reg - 0x00200000 + (0x400 * i))
#define RDMA_SRC_O_P(addr, i)	(addr ##_reg - 0x001fc000 + (0x400 * i))

#define RDMA_CMD_O_N(addr, i)	(addr ##_reg - 0x004f8000 + (0x400 * i))
#define RDMA_CMD_O_P(addr, i)	(addr ##_reg - 0x001f8000 + (0x400 * i))

static dma_addr_t
rsnd_gen2_dma_addr(struct rsnd_dai_stream *io,
		   struct rsnd_mod *mod,
		   int is_play, int is_from)
{
	struct rsnd_priv *priv = rsnd_io_to_priv(io);
	struct device *dev = rsnd_priv_to_dev(priv);
	phys_addr_t ssi_reg = rsnd_gen_get_phy_addr(priv, RSND_GEN2_SSI);
	phys_addr_t src_reg = rsnd_gen_get_phy_addr(priv, RSND_GEN2_SCU);
	int is_ssi = !!(rsnd_io_to_mod_ssi(io) == mod);
	int use_src = !!rsnd_io_to_mod_src(io);
	int use_cmd = !!rsnd_io_to_mod_dvc(io) ||
		      !!rsnd_io_to_mod_mix(io) ||
		      !!rsnd_io_to_mod_ctu(io);
	int id = rsnd_mod_id(mod);
	struct dma_addr {
		dma_addr_t out_addr;
		dma_addr_t in_addr;
	} dma_addrs[3][2][3] = {
		/* SRC */
		{{{ 0,				0 },
		  /* Capture */
		  { RDMA_SRC_O_N(src, id),	RDMA_SRC_I_P(src, id) },
		  { RDMA_CMD_O_N(src, id),	RDMA_SRC_I_P(src, id) } },
		 /* Playback */
		 {{ 0,				0, },
		  { RDMA_SRC_O_P(src, id),	RDMA_SRC_I_N(src, id) },
		  { RDMA_CMD_O_P(src, id),	RDMA_SRC_I_N(src, id) } }
		},
		/* SSI */
		/* Capture */
		{{{ RDMA_SSI_O_N(ssi, id),	0 },
		  { RDMA_SSIU_O_P(ssi, id),	0 },
		  { RDMA_SSIU_O_P(ssi, id),	0 } },
		 /* Playback */
		 {{ 0,				RDMA_SSI_I_N(ssi, id) },
		  { 0,				RDMA_SSIU_I_P(ssi, id) },
		  { 0,				RDMA_SSIU_I_P(ssi, id) } }
		},
		/* SSIU */
		/* Capture */
		{{{ RDMA_SSIU_O_N(ssi, id),	0 },
		  { RDMA_SSIU_O_P(ssi, id),	0 },
		  { RDMA_SSIU_O_P(ssi, id),	0 } },
		 /* Playback */
		 {{ 0,				RDMA_SSIU_I_N(ssi, id) },
		  { 0,				RDMA_SSIU_I_P(ssi, id) },
		  { 0,				RDMA_SSIU_I_P(ssi, id) } } },
	};

	/* it shouldn't happen */
	if (use_cmd && !use_src)
		dev_err(dev, "DVC is selected without SRC\n");

	/* use SSIU or SSI ? */
	if (is_ssi && rsnd_ssi_use_busif(io))
		is_ssi++;

	return (is_from) ?
		dma_addrs[is_ssi][is_play][use_src + use_cmd].out_addr :
		dma_addrs[is_ssi][is_play][use_src + use_cmd].in_addr;
}

static dma_addr_t rsnd_dma_addr(struct rsnd_dai_stream *io,
				struct rsnd_mod *mod,
				int is_play, int is_from)
{
	struct rsnd_priv *priv = rsnd_io_to_priv(io);

	/*
	 * gen1 uses default DMA addr
	 */
	if (rsnd_is_gen1(priv))
		return 0;

	if (!mod)
		return 0;

	return rsnd_gen2_dma_addr(io, mod, is_play, is_from);
}

#define MOD_MAX (RSND_MOD_MAX + 1) /* +Memory */
static void rsnd_dma_of_path(struct rsnd_mod *this,
			     struct rsnd_dai_stream *io,
			     int is_play,
			     struct rsnd_mod **mod_from,
			     struct rsnd_mod **mod_to)
{
	struct rsnd_mod *ssi = rsnd_io_to_mod_ssi(io);
	struct rsnd_mod *src = rsnd_io_to_mod_src(io);
	struct rsnd_mod *ctu = rsnd_io_to_mod_ctu(io);
	struct rsnd_mod *mix = rsnd_io_to_mod_mix(io);
	struct rsnd_mod *dvc = rsnd_io_to_mod_dvc(io);
	struct rsnd_mod *mod[MOD_MAX];
	struct rsnd_mod *mod_start, *mod_end;
	struct rsnd_priv *priv = rsnd_mod_to_priv(this);
	struct device *dev = rsnd_priv_to_dev(priv);
	int nr, i, idx;

	if (!ssi)
		return;

	nr = 0;
	for (i = 0; i < MOD_MAX; i++) {
		mod[i] = NULL;
		nr += !!rsnd_io_to_mod(io, i);
	}

	/*
	 * [S] -*-> [E]
	 * [S] -*-> SRC -o-> [E]
	 * [S] -*-> SRC -> DVC -o-> [E]
	 * [S] -*-> SRC -> CTU -> MIX -> DVC -o-> [E]
	 *
	 * playback	[S] = mem
	 *		[E] = SSI
	 *
	 * capture	[S] = SSI
	 *		[E] = mem
	 *
	 * -*->		Audio DMAC
	 * -o->		Audio DMAC peri peri
	 */
	mod_start	= (is_play) ? NULL : ssi;
	mod_end		= (is_play) ? ssi  : NULL;

	idx = 0;
	mod[idx++] = mod_start;
	for (i = 1; i < nr; i++) {
		if (src) {
			mod[idx++] = src;
			src = NULL;
		} else if (ctu) {
			mod[idx++] = ctu;
			ctu = NULL;
		} else if (mix) {
			mod[idx++] = mix;
			mix = NULL;
		} else if (dvc) {
			mod[idx++] = dvc;
			dvc = NULL;
		}
	}
	mod[idx] = mod_end;

	/*
	 *		| SSI | SRC |
	 * -------------+-----+-----+
	 *  is_play	|  o  |  *  |
	 * !is_play	|  *  |  o  |
	 */
	if ((this == ssi) == (is_play)) {
		*mod_from	= mod[idx - 1];
		*mod_to		= mod[idx];
	} else {
		*mod_from	= mod[0];
		*mod_to		= mod[1];
	}

	dev_dbg(dev, "module connection (this is %s[%d])\n",
		rsnd_mod_name(this), rsnd_mod_id(this));
	for (i = 0; i <= idx; i++) {
		dev_dbg(dev, "  %s[%d]%s\n",
		       rsnd_mod_name(mod[i]), rsnd_mod_id(mod[i]),
		       (mod[i] == *mod_from) ? " from" :
		       (mod[i] == *mod_to)   ? " to" : "");
	}
}

int rsnd_dma_attach(struct rsnd_dai_stream *io, struct rsnd_mod *mod,
		    struct rsnd_mod **dma_mod)
{
	struct rsnd_mod *mod_from = NULL;
	struct rsnd_mod *mod_to = NULL;
	struct rsnd_priv *priv = rsnd_io_to_priv(io);
	struct rsnd_dma_ctrl *dmac = rsnd_priv_to_dmac(priv);
	struct device *dev = rsnd_priv_to_dev(priv);
	struct rsnd_mod_ops *ops;
	enum rsnd_mod_type type;
	int (*attach)(struct rsnd_dai_stream *io, struct rsnd_dma *dma,
		      struct rsnd_mod *mod_from, struct rsnd_mod *mod_to);
	int is_play = rsnd_io_is_play(io);
	int ret, dma_id;

	/*
	 * DMA failed. try to PIO mode
	 * see
	 *	rsnd_ssi_fallback()
	 *	rsnd_rdai_continuance_probe()
	 */
	if (!dmac)
		return -EAGAIN;

	rsnd_dma_of_path(mod, io, is_play, &mod_from, &mod_to);

	/* for Gen2 */
	if (mod_from && mod_to) {
		ops	= &rsnd_dmapp_ops;
		attach	= rsnd_dmapp_attach;
		dma_id	= dmac->dmapp_num;
		type	= RSND_MOD_AUDMAPP;
	} else {
		ops	= &rsnd_dmaen_ops;
		attach	= rsnd_dmaen_attach;
		dma_id	= dmac->dmaen_num;
		type	= RSND_MOD_AUDMA;
	}

	/* for Gen1, overwrite */
	if (rsnd_is_gen1(priv)) {
		ops	= &rsnd_dmaen_ops;
		attach	= rsnd_dmaen_attach;
		dma_id	= dmac->dmaen_num;
		type	= RSND_MOD_AUDMA;
	}

	if (!(*dma_mod)) {
		struct rsnd_dma *dma;

		dma = devm_kzalloc(dev, sizeof(*dma), GFP_KERNEL);
		if (!dma)
			return -ENOMEM;

		*dma_mod = rsnd_mod_get(dma);

		dma->src_addr = rsnd_dma_addr(io, mod_from, is_play, 1);
		dma->dst_addr = rsnd_dma_addr(io, mod_to,   is_play, 0);

		ret = rsnd_mod_init(priv, *dma_mod, ops, NULL,
				    rsnd_mod_get_status, type, dma_id);
		if (ret < 0)
			return ret;

		dev_dbg(dev, "%s[%d] %s[%d] -> %s[%d]\n",
			rsnd_mod_name(*dma_mod), rsnd_mod_id(*dma_mod),
			rsnd_mod_name(mod_from), rsnd_mod_id(mod_from),
			rsnd_mod_name(mod_to),   rsnd_mod_id(mod_to));

		ret = attach(io, dma, mod_from, mod_to);
		if (ret < 0)
			return ret;
	}

	ret = rsnd_dai_connect(*dma_mod, io, type);
	if (ret < 0)
		return ret;

	return 0;
}

void rsnd_dma_detach(struct rsnd_mod *mod, struct rsnd_mod **dma_mod)
{
	if (*dma_mod) {
		struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
		struct device *dev = rsnd_priv_to_dev(priv);

		devm_kfree(dev, *dma_mod);
		*dma_mod = NULL;
	}
}

int rsnd_dma_probe(struct rsnd_priv *priv)
{
	struct platform_device *pdev = rsnd_priv_to_pdev(priv);
	struct device *dev = rsnd_priv_to_dev(priv);
	struct rsnd_dma_ctrl *dmac;
	struct resource *res;

	/*
	 * for Gen1
	 */
	if (rsnd_is_gen1(priv))
		return 0;

	/*
	 * for Gen2
	 */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "audmapp");
	dmac = devm_kzalloc(dev, sizeof(*dmac), GFP_KERNEL);
	if (!dmac || !res) {
		dev_err(dev, "dma allocate failed\n");
		return 0; /* it will be PIO mode */
	}

	dmac->dmapp_num = 0;
	dmac->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(dmac->base))
		return PTR_ERR(dmac->base);

	priv->dma = dmac;

	return 0;
}
