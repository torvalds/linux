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

struct rsnd_dma_ctrl {
	void __iomem *base;
	int dmapp_num;
};

#define rsnd_priv_to_dmac(p)	((struct rsnd_dma_ctrl *)(p)->dma)

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

static void rsnd_dmaen_stop(struct rsnd_dai_stream *io, struct rsnd_dma *dma)
{
	struct rsnd_dmaen *dmaen = rsnd_dma_to_dmaen(dma);

	dmaengine_terminate_all(dmaen->chan);
}

static void rsnd_dmaen_start(struct rsnd_dai_stream *io, struct rsnd_dma *dma)
{
	struct rsnd_dmaen *dmaen = rsnd_dma_to_dmaen(dma);
	struct rsnd_mod *mod = rsnd_dma_to_mod(dma);
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
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
		return;
	}

	desc->callback		= rsnd_dmaen_complete;
	desc->callback_param	= mod;

	if (dmaengine_submit(desc) < 0) {
		dev_err(dev, "dmaengine_submit() fail\n");
		return;
	}

	dma_async_issue_pending(dmaen->chan);
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

static int rsnd_dmaen_init(struct rsnd_dai_stream *io,
			   struct rsnd_dma *dma, int id,
			   struct rsnd_mod *mod_from, struct rsnd_mod *mod_to)
{
	struct rsnd_dmaen *dmaen = rsnd_dma_to_dmaen(dma);
	struct rsnd_priv *priv = rsnd_io_to_priv(io);
	struct device *dev = rsnd_priv_to_dev(priv);
	struct dma_slave_config cfg = {};
	int is_play = rsnd_io_is_play(io);
	int ret;

	if (dmaen->chan) {
		dev_err(dev, "it already has dma channel\n");
		return -EIO;
	}

	if (dev->of_node) {
		dmaen->chan = rsnd_dmaen_request_channel(io, mod_from, mod_to);
	} else {
		dma_cap_mask_t mask;

		dma_cap_zero(mask);
		dma_cap_set(DMA_SLAVE, mask);

		dmaen->chan = dma_request_channel(mask, shdma_chan_filter,
						  (void *)id);
	}
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

	dev_dbg(dev, "dma : %pad -> %pad\n",
		&cfg.src_addr, &cfg.dst_addr);

	ret = dmaengine_slave_config(dmaen->chan, &cfg);
	if (ret < 0)
		goto rsnd_dma_init_err;

	return 0;

rsnd_dma_init_err:
	rsnd_dma_quit(io, dma);
rsnd_dma_channel_err:

	/*
	 * DMA failed. try to PIO mode
	 * see
	 *	rsnd_ssi_fallback()
	 *	rsnd_rdai_continuance_probe()
	 */
	return -EAGAIN;
}

static void rsnd_dmaen_quit(struct rsnd_dai_stream *io, struct rsnd_dma *dma)
{
	struct rsnd_dmaen *dmaen = rsnd_dma_to_dmaen(dma);

	if (dmaen->chan)
		dma_release_channel(dmaen->chan);

	dmaen->chan = NULL;
}

static struct rsnd_dma_ops rsnd_dmaen_ops = {
	.start	= rsnd_dmaen_start,
	.stop	= rsnd_dmaen_stop,
	.init	= rsnd_dmaen_init,
	.quit	= rsnd_dmaen_quit,
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

	if (!entry)
		return 0xFF;

	if (size <= id)
		return 0xFF;

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
	struct rsnd_mod *mod = rsnd_dma_to_mod(dma);
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct rsnd_dma_ctrl *dmac = rsnd_priv_to_dmac(priv);
	struct device *dev = rsnd_priv_to_dev(priv);

	dev_dbg(dev, "w %p : %08x\n", rsnd_dmapp_addr(dmac, dma, reg), data);

	iowrite32(data, rsnd_dmapp_addr(dmac, dma, reg));
}

static u32 rsnd_dmapp_read(struct rsnd_dma *dma, u32 reg)
{
	struct rsnd_mod *mod = rsnd_dma_to_mod(dma);
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct rsnd_dma_ctrl *dmac = rsnd_priv_to_dmac(priv);

	return ioread32(rsnd_dmapp_addr(dmac, dma, reg));
}

static void rsnd_dmapp_stop(struct rsnd_dai_stream *io, struct rsnd_dma *dma)
{
	int i;

	rsnd_dmapp_write(dma, 0, PDMACHCR);

	for (i = 0; i < 1024; i++) {
		if (0 == rsnd_dmapp_read(dma, PDMACHCR))
			return;
		udelay(1);
	}
}

static void rsnd_dmapp_start(struct rsnd_dai_stream *io, struct rsnd_dma *dma)
{
	struct rsnd_dmapp *dmapp = rsnd_dma_to_dmapp(dma);

	rsnd_dmapp_write(dma, dma->src_addr,	PDMASAR);
	rsnd_dmapp_write(dma, dma->dst_addr,	PDMADAR);
	rsnd_dmapp_write(dma, dmapp->chcr,	PDMACHCR);
}

static int rsnd_dmapp_init(struct rsnd_dai_stream *io,
			   struct rsnd_dma *dma, int id,
			   struct rsnd_mod *mod_from, struct rsnd_mod *mod_to)
{
	struct rsnd_dmapp *dmapp = rsnd_dma_to_dmapp(dma);
	struct rsnd_priv *priv = rsnd_io_to_priv(io);
	struct rsnd_dma_ctrl *dmac = rsnd_priv_to_dmac(priv);
	struct device *dev = rsnd_priv_to_dev(priv);

	dmapp->dmapp_id = dmac->dmapp_num;
	dmapp->chcr = rsnd_dmapp_get_chcr(io, mod_from, mod_to) | PDMACHCR_DE;

	dmac->dmapp_num++;

	rsnd_dmapp_stop(io, dma);

	dev_dbg(dev, "id/src/dst/chcr = %d/%pad/%pad/%08x\n",
		dmapp->dmapp_id, &dma->src_addr, &dma->dst_addr, dmapp->chcr);

	return 0;
}

static struct rsnd_dma_ops rsnd_dmapp_ops = {
	.start	= rsnd_dmapp_start,
	.stop	= rsnd_dmapp_stop,
	.init	= rsnd_dmapp_init,
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
	int use_dvc = !!rsnd_io_to_mod_dvc(io);
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
	if (use_dvc && !use_src)
		dev_err(dev, "DVC is selected without SRC\n");

	/* use SSIU or SSI ? */
	if (is_ssi && rsnd_ssi_use_busif(io, mod))
		is_ssi++;

	return (is_from) ?
		dma_addrs[is_ssi][is_play][use_src + use_dvc].out_addr :
		dma_addrs[is_ssi][is_play][use_src + use_dvc].in_addr;
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

#define MOD_MAX 4 /* MEM/SSI/SRC/DVC */
static void rsnd_dma_of_path(struct rsnd_dma *dma,
			     struct rsnd_dai_stream *io,
			     int is_play,
			     struct rsnd_mod **mod_from,
			     struct rsnd_mod **mod_to)
{
	struct rsnd_mod *this = rsnd_dma_to_mod(dma);
	struct rsnd_mod *ssi = rsnd_io_to_mod_ssi(io);
	struct rsnd_mod *src = rsnd_io_to_mod_src(io);
	struct rsnd_mod *dvc = rsnd_io_to_mod_dvc(io);
	struct rsnd_mod *mod[MOD_MAX];
	int i, index;


	for (i = 0; i < MOD_MAX; i++)
		mod[i] = NULL;

	/*
	 * in play case...
	 *
	 * src -> dst
	 *
	 * mem -> SSI
	 * mem -> SRC -> SSI
	 * mem -> SRC -> DVC -> SSI
	 */
	mod[0] = NULL; /* for "mem" */
	index = 1;
	for (i = 1; i < MOD_MAX; i++) {
		if (!src) {
			mod[i] = ssi;
		} else if (!dvc) {
			mod[i] = src;
			src = NULL;
		} else {
			if ((!is_play) && (this == src))
				this = dvc;

			mod[i] = (is_play) ? src : dvc;
			i++;
			mod[i] = (is_play) ? dvc : src;
			src = NULL;
			dvc = NULL;
		}

		if (mod[i] == this)
			index = i;

		if (mod[i] == ssi)
			break;
	}

	if (is_play) {
		*mod_from = mod[index - 1];
		*mod_to   = mod[index];
	} else {
		*mod_from = mod[index];
		*mod_to   = mod[index - 1];
	}
}

void rsnd_dma_stop(struct rsnd_dai_stream *io, struct rsnd_dma *dma)
{
	dma->ops->stop(io, dma);
}

void rsnd_dma_start(struct rsnd_dai_stream *io, struct rsnd_dma *dma)
{
	dma->ops->start(io, dma);
}

void rsnd_dma_quit(struct rsnd_dai_stream *io, struct rsnd_dma *dma)
{
	struct rsnd_mod *mod = rsnd_dma_to_mod(dma);
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct rsnd_dma_ctrl *dmac = rsnd_priv_to_dmac(priv);

	if (!dmac)
		return;

	dma->ops->quit(io, dma);
}

int rsnd_dma_init(struct rsnd_dai_stream *io, struct rsnd_dma *dma, int id)
{
	struct rsnd_mod *mod_from;
	struct rsnd_mod *mod_to;
	struct rsnd_priv *priv = rsnd_io_to_priv(io);
	struct rsnd_dma_ctrl *dmac = rsnd_priv_to_dmac(priv);
	int is_play = rsnd_io_is_play(io);

	/*
	 * DMA failed. try to PIO mode
	 * see
	 *	rsnd_ssi_fallback()
	 *	rsnd_rdai_continuance_probe()
	 */
	if (!dmac)
		return -EAGAIN;

	rsnd_dma_of_path(dma, io, is_play, &mod_from, &mod_to);

	dma->src_addr = rsnd_dma_addr(io, mod_from, is_play, 1);
	dma->dst_addr = rsnd_dma_addr(io, mod_to,   is_play, 0);

	/* for Gen2 */
	if (mod_from && mod_to)
		dma->ops = &rsnd_dmapp_ops;
	else
		dma->ops = &rsnd_dmaen_ops;

	/* for Gen1, overwrite */
	if (rsnd_is_gen1(priv))
		dma->ops = &rsnd_dmaen_ops;

	return dma->ops->init(io, dma, id, mod_from, mod_to);
}

int rsnd_dma_probe(struct platform_device *pdev,
		   const struct rsnd_of_data *of_data,
		   struct rsnd_priv *priv)
{
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
