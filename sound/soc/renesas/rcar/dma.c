// SPDX-License-Identifier: GPL-2.0
//
// Renesas R-Car Audio DMAC support
//
// Copyright (C) 2015 Renesas Electronics Corp.
// Copyright (c) 2015 Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>

#include <linux/delay.h>
#include <linux/of_dma.h>
#include <sound/dmaengine_pcm.h>
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
	struct rsnd_mod		*mod_from;
	struct rsnd_mod		*mod_to;
	dma_addr_t		src_addr;
	dma_addr_t		dst_addr;
	union {
		struct rsnd_dmaen en;
		struct rsnd_dmapp pp;
	} dma;
};

struct rsnd_dma_ctrl {
	void __iomem *ppbase;
	phys_addr_t ppres;
	int dmaen_num;
	int dmapp_num;
};

#define rsnd_priv_to_dmac(p)	((struct rsnd_dma_ctrl *)(p)->dma)
#define rsnd_mod_to_dma(_mod) container_of((_mod), struct rsnd_dma, mod)
#define rsnd_dma_to_dmaen(dma)	(&(dma)->dma.en)
#define rsnd_dma_to_dmapp(dma)	(&(dma)->dma.pp)

/* for DEBUG */
static struct rsnd_mod_ops mem_ops = {
	.name = "mem",
};

static struct rsnd_mod mem = {
};

/*
 *		Audio DMAC
 */
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

static int rsnd_dmaen_stop(struct rsnd_mod *mod,
			   struct rsnd_dai_stream *io,
			   struct rsnd_priv *priv)
{
	return snd_dmaengine_pcm_trigger(io->substream, SNDRV_PCM_TRIGGER_STOP);
}

static int rsnd_dmaen_cleanup(struct rsnd_mod *mod,
			      struct rsnd_dai_stream *io,
			      struct rsnd_priv *priv)
{
	struct rsnd_dma *dma = rsnd_mod_to_dma(mod);
	struct rsnd_dmaen *dmaen = rsnd_dma_to_dmaen(dma);

	/*
	 * DMAEngine release uses mutex lock.
	 * Thus, it shouldn't be called under spinlock.
	 * Let's call it under prepare
	 */
	if (dmaen->chan)
		snd_dmaengine_pcm_close_release_chan(io->substream);

	dmaen->chan = NULL;

	return 0;
}

static int rsnd_dmaen_prepare(struct rsnd_mod *mod,
			      struct rsnd_dai_stream *io,
			      struct rsnd_priv *priv)
{
	struct rsnd_dma *dma = rsnd_mod_to_dma(mod);
	struct rsnd_dmaen *dmaen = rsnd_dma_to_dmaen(dma);
	struct device *dev = rsnd_priv_to_dev(priv);

	/* maybe suspended */
	if (dmaen->chan)
		return 0;

	/*
	 * DMAEngine request uses mutex lock.
	 * Thus, it shouldn't be called under spinlock.
	 * Let's call it under prepare
	 */
	dmaen->chan = rsnd_dmaen_request_channel(io,
						 dma->mod_from,
						 dma->mod_to);
	if (IS_ERR_OR_NULL(dmaen->chan)) {
		dmaen->chan = NULL;
		dev_err(dev, "can't get dma channel\n");
		return -EIO;
	}

	return snd_dmaengine_pcm_open(io->substream, dmaen->chan);
}

static int rsnd_dmaen_start(struct rsnd_mod *mod,
			    struct rsnd_dai_stream *io,
			    struct rsnd_priv *priv)
{
	struct rsnd_dma *dma = rsnd_mod_to_dma(mod);
	struct rsnd_dmaen *dmaen = rsnd_dma_to_dmaen(dma);
	struct device *dev = rsnd_priv_to_dev(priv);
	struct dma_slave_config cfg = {};
	enum dma_slave_buswidth buswidth = DMA_SLAVE_BUSWIDTH_4_BYTES;
	int ret;

	/*
	 * in case of monaural data writing or reading through Audio-DMAC
	 * data is always in Left Justified format, so both src and dst
	 * DMA Bus width need to be set equal to physical data width.
	 */
	if (rsnd_runtime_channel_original(io) == 1) {
		struct snd_pcm_runtime *runtime = rsnd_io_to_runtime(io);
		int bits = snd_pcm_format_physical_width(runtime->format);

		switch (bits) {
		case 8:
			buswidth = DMA_SLAVE_BUSWIDTH_1_BYTE;
			break;
		case 16:
			buswidth = DMA_SLAVE_BUSWIDTH_2_BYTES;
			break;
		case 32:
			buswidth = DMA_SLAVE_BUSWIDTH_4_BYTES;
			break;
		default:
			dev_err(dev, "invalid format width %d\n", bits);
			return -EINVAL;
		}
	}

	cfg.direction	= snd_pcm_substream_to_dma_direction(io->substream);
	cfg.src_addr	= dma->src_addr;
	cfg.dst_addr	= dma->dst_addr;
	cfg.src_addr_width = buswidth;
	cfg.dst_addr_width = buswidth;

	dev_dbg(dev, "%s %pad -> %pad\n",
		rsnd_mod_name(mod),
		&cfg.src_addr, &cfg.dst_addr);

	ret = dmaengine_slave_config(dmaen->chan, &cfg);
	if (ret < 0)
		return ret;

	return snd_dmaengine_pcm_trigger(io->substream, SNDRV_PCM_TRIGGER_START);
}

struct dma_chan *rsnd_dma_request_channel(struct device_node *of_node, char *name,
					  struct rsnd_mod *mod, char *x)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct device *dev = rsnd_priv_to_dev(priv);
	struct dma_chan *chan = NULL;
	int i = 0;

	for_each_child_of_node_scoped(of_node, np) {
		i = rsnd_node_fixed_index(dev, np, name, i);
		if (i < 0) {
			chan = NULL;
			break;
		}

		if (i == rsnd_mod_id_raw(mod) && (!chan))
			chan = of_dma_request_slave_channel(np, x);
		i++;
	}

	/* It should call of_node_put(), since, it is rsnd_xxx_of_node() */
	of_node_put(of_node);

	return chan;
}

static int rsnd_dmaen_attach(struct rsnd_dai_stream *io,
			   struct rsnd_dma *dma,
			   struct rsnd_mod *mod_from, struct rsnd_mod *mod_to)
{
	struct rsnd_priv *priv = rsnd_io_to_priv(io);
	struct rsnd_dma_ctrl *dmac = rsnd_priv_to_dmac(priv);
	struct dma_chan *chan;

	/* try to get DMAEngine channel */
	chan = rsnd_dmaen_request_channel(io, mod_from, mod_to);
	if (IS_ERR_OR_NULL(chan)) {
		/* Let's follow when -EPROBE_DEFER case */
		if (PTR_ERR(chan) == -EPROBE_DEFER)
			return PTR_ERR(chan);

		/*
		 * DMA failed. try to PIO mode
		 * see
		 *	rsnd_ssi_fallback()
		 *	rsnd_rdai_continuance_probe()
		 */
		return -EAGAIN;
	}

	/*
	 * use it for IPMMU if needed
	 * see
	 *	rsnd_preallocate_pages()
	 */
	io->dmac_dev = chan->device->dev;

	dma_release_channel(chan);

	dmac->dmaen_num++;

	return 0;
}

static int rsnd_dmaen_pointer(struct rsnd_mod *mod,
			      struct rsnd_dai_stream *io,
			      snd_pcm_uframes_t *pointer)
{
	*pointer = snd_dmaengine_pcm_pointer(io->substream);

	return 0;
}

static struct rsnd_mod_ops rsnd_dmaen_ops = {
	.name		= "audmac",
	.prepare	= rsnd_dmaen_prepare,
	.cleanup	= rsnd_dmaen_cleanup,
	.start		= rsnd_dmaen_start,
	.stop		= rsnd_dmaen_stop,
	.pointer	= rsnd_dmaen_pointer,
	.get_status	= rsnd_mod_get_status,
};

/*
 *		Audio DMAC peri peri
 */
static const u8 gen2_id_table_ssiu[] = {
	/* SSI00 ~ SSI07 */
	0x00, 0x01, 0x02, 0x03, 0x39, 0x3a, 0x3b, 0x3c,
	/* SSI10 ~ SSI17 */
	0x04, 0x05, 0x06, 0x07, 0x3d, 0x3e, 0x3f, 0x40,
	/* SSI20 ~ SSI27 */
	0x08, 0x09, 0x0a, 0x0b, 0x41, 0x42, 0x43, 0x44,
	/* SSI30 ~ SSI37 */
	0x0c, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b,
	/* SSI40 ~ SSI47 */
	0x0d, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52,
	/* SSI5 */
	0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* SSI6 */
	0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* SSI7 */
	0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* SSI8 */
	0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* SSI90 ~ SSI97 */
	0x12, 0x13, 0x14, 0x15, 0x53, 0x54, 0x55, 0x56,
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
	struct rsnd_mod *ssiu = rsnd_io_to_mod_ssiu(io);
	struct rsnd_mod *src = rsnd_io_to_mod_src(io);
	struct rsnd_mod *dvc = rsnd_io_to_mod_dvc(io);
	const u8 *entry = NULL;
	int id = 255;
	int size = 0;

	if ((mod == ssi) ||
	    (mod == ssiu)) {
		int busif = rsnd_mod_id_sub(ssiu);

		entry = gen2_id_table_ssiu;
		size = ARRAY_SIZE(gen2_id_table_ssiu);
		id = (rsnd_mod_id(mod) * 8) + busif;
	} else if (mod == src) {
		entry = gen2_id_table_scu;
		size = ARRAY_SIZE(gen2_id_table_scu);
		id = rsnd_mod_id(mod);
	} else if (mod == dvc) {
		entry = gen2_id_table_cmd;
		size = ARRAY_SIZE(gen2_id_table_cmd);
		id = rsnd_mod_id(mod);
	}

	if ((!entry) || (size <= id)) {
		struct device *dev = rsnd_priv_to_dev(rsnd_io_to_priv(io));

		dev_err(dev, "unknown connection (%s)\n", rsnd_mod_name(mod));

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
	(dmac->ppbase + 0x20 + reg + \
	 (0x10 * rsnd_dma_to_dmapp(dma)->dmapp_id))
static void rsnd_dmapp_write(struct rsnd_dma *dma, u32 data, u32 reg)
{
	struct rsnd_mod *mod = rsnd_mod_get(dma);
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct rsnd_dma_ctrl *dmac = rsnd_priv_to_dmac(priv);
	struct device *dev = rsnd_priv_to_dev(priv);

	dev_dbg(dev, "w 0x%px : %08x\n", rsnd_dmapp_addr(dmac, dma, reg), data);

	iowrite32(data, rsnd_dmapp_addr(dmac, dma, reg));
}

static u32 rsnd_dmapp_read(struct rsnd_dma *dma, u32 reg)
{
	struct rsnd_mod *mod = rsnd_mod_get(dma);
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct rsnd_dma_ctrl *dmac = rsnd_priv_to_dmac(priv);

	return ioread32(rsnd_dmapp_addr(dmac, dma, reg));
}

static void rsnd_dmapp_bset(struct rsnd_dma *dma, u32 data, u32 mask, u32 reg)
{
	struct rsnd_mod *mod = rsnd_mod_get(dma);
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct rsnd_dma_ctrl *dmac = rsnd_priv_to_dmac(priv);
	void __iomem *addr = rsnd_dmapp_addr(dmac, dma, reg);
	u32 val = ioread32(addr);

	val &= ~mask;
	val |= (data & mask);

	iowrite32(val, addr);
}

static int rsnd_dmapp_stop(struct rsnd_mod *mod,
			   struct rsnd_dai_stream *io,
			   struct rsnd_priv *priv)
{
	struct rsnd_dma *dma = rsnd_mod_to_dma(mod);
	int i;

	rsnd_dmapp_bset(dma, 0,  PDMACHCR_DE, PDMACHCR);

	for (i = 0; i < 1024; i++) {
		if (0 == (rsnd_dmapp_read(dma, PDMACHCR) & PDMACHCR_DE))
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

#ifdef CONFIG_DEBUG_FS
static void rsnd_dmapp_debug_info(struct seq_file *m,
				  struct rsnd_dai_stream *io,
				  struct rsnd_mod *mod)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct rsnd_dma_ctrl *dmac = rsnd_priv_to_dmac(priv);
	struct rsnd_dma *dma = rsnd_mod_to_dma(mod);
	struct rsnd_dmapp *dmapp = rsnd_dma_to_dmapp(dma);

	rsnd_debugfs_reg_show(m, dmac->ppres, dmac->ppbase,
			      0x20 + 0x10 * dmapp->dmapp_id, 0x10);
}
#define DEBUG_INFO .debug_info = rsnd_dmapp_debug_info
#else
#define DEBUG_INFO
#endif

static struct rsnd_mod_ops rsnd_dmapp_ops = {
	.name		= "audmac-pp",
	.start		= rsnd_dmapp_start,
	.stop		= rsnd_dmapp_stop,
	.quit		= rsnd_dmapp_stop,
	.get_status	= rsnd_mod_get_status,
	DEBUG_INFO
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

#define RDMA_SSIU_I_N(addr, i, j) (addr ##_reg - 0x00441000 + (0x1000 * (i)) + (((j) / 4) * 0xA000) + (((j) % 4) * 0x400) - (0x4000 * ((i) / 9) * ((j) / 4)))
#define RDMA_SSIU_O_N(addr, i, j) RDMA_SSIU_I_N(addr, i, j)

#define RDMA_SSIU_I_P(addr, i, j) (addr ##_reg - 0x00141000 + (0x1000 * (i)) + (((j) / 4) * 0xA000) + (((j) % 4) * 0x400) - (0x4000 * ((i) / 9) * ((j) / 4)))
#define RDMA_SSIU_O_P(addr, i, j) RDMA_SSIU_I_P(addr, i, j)

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
	phys_addr_t ssi_reg = rsnd_gen_get_phy_addr(priv, RSND_BASE_SSI);
	phys_addr_t src_reg = rsnd_gen_get_phy_addr(priv, RSND_BASE_SCU);
	int is_ssi = !!(rsnd_io_to_mod_ssi(io) == mod) ||
		     !!(rsnd_io_to_mod_ssiu(io) == mod);
	int use_src = !!rsnd_io_to_mod_src(io);
	int use_cmd = !!rsnd_io_to_mod_dvc(io) ||
		      !!rsnd_io_to_mod_mix(io) ||
		      !!rsnd_io_to_mod_ctu(io);
	int id = rsnd_mod_id(mod);
	int busif = rsnd_mod_id_sub(rsnd_io_to_mod_ssiu(io));
	struct dma_addr {
		dma_addr_t out_addr;
		dma_addr_t in_addr;
	} dma_addrs[3][2][3] = {
		/* SRC */
		/* Capture */
		{{{ 0,				0 },
		  { RDMA_SRC_O_N(src, id),	RDMA_SRC_I_P(src, id) },
		  { RDMA_CMD_O_N(src, id),	RDMA_SRC_I_P(src, id) } },
		 /* Playback */
		 {{ 0,				0, },
		  { RDMA_SRC_O_P(src, id),	RDMA_SRC_I_N(src, id) },
		  { RDMA_CMD_O_P(src, id),	RDMA_SRC_I_N(src, id) } }
		},
		/* SSI */
		/* Capture */
		{{{ RDMA_SSI_O_N(ssi, id),		0 },
		  { RDMA_SSIU_O_P(ssi, id, busif),	0 },
		  { RDMA_SSIU_O_P(ssi, id, busif),	0 } },
		 /* Playback */
		 {{ 0,			RDMA_SSI_I_N(ssi, id) },
		  { 0,			RDMA_SSIU_I_P(ssi, id, busif) },
		  { 0,			RDMA_SSIU_I_P(ssi, id, busif) } }
		},
		/* SSIU */
		/* Capture */
		{{{ RDMA_SSIU_O_N(ssi, id, busif),	0 },
		  { RDMA_SSIU_O_P(ssi, id, busif),	0 },
		  { RDMA_SSIU_O_P(ssi, id, busif),	0 } },
		 /* Playback */
		 {{ 0,			RDMA_SSIU_I_N(ssi, id, busif) },
		  { 0,			RDMA_SSIU_I_P(ssi, id, busif) },
		  { 0,			RDMA_SSIU_I_P(ssi, id, busif) } } },
	};

	/*
	 * FIXME
	 *
	 * We can't support SSI9-4/5/6/7, because its address is
	 * out of calculation rule
	 */
	if ((id == 9) && (busif >= 4))
		dev_err(dev, "This driver doesn't support SSI%d-%d, so far",
			id, busif);

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

/*
 *	Gen4 DMA read/write register offset
 *
 *	ex) R-Car V4H case
 *		  mod		/ SYS-DMAC in	/ SYS-DMAC out
 *	SSI_SDMC: 0xec400000	/ 0xec400000	/ 0xec400000
 */
#define RDMA_SSI_SDMC(addr, i)	(addr + (0x8000 * i))
static dma_addr_t
rsnd_gen4_dma_addr(struct rsnd_dai_stream *io, struct rsnd_mod *mod,
		   int is_play, int is_from)
{
	struct rsnd_priv *priv = rsnd_io_to_priv(io);
	phys_addr_t addr = rsnd_gen_get_phy_addr(priv, RSND_BASE_SDMC);
	int id = rsnd_mod_id(mod);
	int busif = rsnd_mod_id_sub(mod);

	/*
	 * SSI0 only is supported
	 */
	if (id != 0) {
		struct device *dev = rsnd_priv_to_dev(priv);

		dev_err(dev, "This driver doesn't support non SSI0");
		return -EINVAL;
	}

	return RDMA_SSI_SDMC(addr, busif);
}

static dma_addr_t rsnd_dma_addr(struct rsnd_dai_stream *io,
				struct rsnd_mod *mod,
				int is_play, int is_from)
{
	struct rsnd_priv *priv = rsnd_io_to_priv(io);

	if (!mod)
		return 0;

	/*
	 * gen1 uses default DMA addr
	 */
	if (rsnd_is_gen1(priv))
		return 0;
	else if (rsnd_is_gen4(priv))
		return rsnd_gen4_dma_addr(io, mod, is_play, is_from);
	else
		return rsnd_gen2_dma_addr(io, mod, is_play, is_from);
}

#define MOD_MAX (RSND_MOD_MAX + 1) /* +Memory */
static void rsnd_dma_of_path(struct rsnd_mod *this,
			     struct rsnd_dai_stream *io,
			     int is_play,
			     struct rsnd_mod **mod_from,
			     struct rsnd_mod **mod_to)
{
	struct rsnd_mod *ssi;
	struct rsnd_mod *src = rsnd_io_to_mod_src(io);
	struct rsnd_mod *ctu = rsnd_io_to_mod_ctu(io);
	struct rsnd_mod *mix = rsnd_io_to_mod_mix(io);
	struct rsnd_mod *dvc = rsnd_io_to_mod_dvc(io);
	struct rsnd_mod *mod[MOD_MAX];
	struct rsnd_mod *mod_start, *mod_end;
	struct rsnd_priv *priv = rsnd_mod_to_priv(this);
	struct device *dev = rsnd_priv_to_dev(priv);
	int nr, i, idx;

	/*
	 * It should use "rcar_sound,ssiu" on DT.
	 * But, we need to keep compatibility for old version.
	 *
	 * If it has "rcar_sound.ssiu", it will be used.
	 * If not, "rcar_sound.ssi" will be used.
	 * see
	 *	rsnd_ssiu_dma_req()
	 *	rsnd_ssi_dma_req()
	 */
	if (rsnd_ssiu_of_node(priv)) {
		struct rsnd_mod *ssiu = rsnd_io_to_mod_ssiu(io);

		/* use SSIU */
		ssi = ssiu;
		if (this == rsnd_io_to_mod_ssi(io))
			this = ssiu;
	} else {
		/* keep compatible, use SSI */
		ssi = rsnd_io_to_mod_ssi(io);
	}

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

	dev_dbg(dev, "module connection (this is %s)\n", rsnd_mod_name(this));
	for (i = 0; i <= idx; i++) {
		dev_dbg(dev, "  %s%s\n",
			rsnd_mod_name(mod[i] ? mod[i] : &mem),
			(mod[i] == *mod_from) ? " from" :
			(mod[i] == *mod_to)   ? " to" : "");
	}
}

static int rsnd_dma_alloc(struct rsnd_dai_stream *io, struct rsnd_mod *mod,
			  struct rsnd_mod **dma_mod)
{
	struct rsnd_mod *mod_from = NULL;
	struct rsnd_mod *mod_to = NULL;
	struct rsnd_priv *priv = rsnd_io_to_priv(io);
	struct rsnd_dma_ctrl *dmac = rsnd_priv_to_dmac(priv);
	struct device *dev = rsnd_priv_to_dev(priv);
	struct rsnd_dma *dma;
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

	/* for Gen2 or later */
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

	dma = devm_kzalloc(dev, sizeof(*dma), GFP_KERNEL);
	if (!dma)
		return -ENOMEM;

	*dma_mod = rsnd_mod_get(dma);

	ret = rsnd_mod_init(priv, *dma_mod, ops, NULL,
			    type, dma_id);
	if (ret < 0)
		return ret;

	dev_dbg(dev, "%s %s -> %s\n",
		rsnd_mod_name(*dma_mod),
		rsnd_mod_name(mod_from ? mod_from : &mem),
		rsnd_mod_name(mod_to   ? mod_to   : &mem));

	ret = attach(io, dma, mod_from, mod_to);
	if (ret < 0)
		return ret;

	dma->src_addr = rsnd_dma_addr(io, mod_from, is_play, 1);
	dma->dst_addr = rsnd_dma_addr(io, mod_to,   is_play, 0);
	dma->mod_from = mod_from;
	dma->mod_to   = mod_to;

	return 0;
}

int rsnd_dma_attach(struct rsnd_dai_stream *io, struct rsnd_mod *mod,
		    struct rsnd_mod **dma_mod)
{
	if (!(*dma_mod)) {
		int ret = rsnd_dma_alloc(io, mod, dma_mod);

		if (ret < 0)
			return ret;
	}

	return rsnd_dai_connect(*dma_mod, io, (*dma_mod)->type);
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
	 * for Gen2 or later
	 */
	dmac = devm_kzalloc(dev, sizeof(*dmac), GFP_KERNEL);
	if (!dmac) {
		dev_err(dev, "dma allocate failed\n");
		return 0; /* it will be PIO mode */
	}

	/* for Gen4 doesn't have DMA-pp */
	if (rsnd_is_gen4(priv))
		goto audmapp_end;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "audmapp");
	if (!res) {
		dev_err(dev, "lack of audmapp in DT\n");
		return 0; /* it will be PIO mode */
	}

	dmac->dmapp_num = 0;
	dmac->ppres  = res->start;
	dmac->ppbase = devm_ioremap_resource(dev, res);
	if (IS_ERR(dmac->ppbase))
		return PTR_ERR(dmac->ppbase);
audmapp_end:
	priv->dma = dmac;

	/* dummy mem mod for debug */
	return rsnd_mod_init(NULL, &mem, &mem_ops, NULL, 0, 0);
}
