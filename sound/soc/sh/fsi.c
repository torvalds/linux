/*
 * Fifo-attached Serial Interface (FSI) support for SH7724
 *
 * Copyright (C) 2009 Renesas Solutions Corp.
 * Kuninori Morimoto <morimoto.kuninori@renesas.com>
 *
 * Based on ssi.c
 * Copyright (c) 2007 Manuel Lauss <mano@roarinelk.homelinux.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/scatterlist.h>
#include <linux/sh_dma.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <sound/sh_fsi.h>

/* PortA/PortB register */
#define REG_DO_FMT	0x0000
#define REG_DOFF_CTL	0x0004
#define REG_DOFF_ST	0x0008
#define REG_DI_FMT	0x000C
#define REG_DIFF_CTL	0x0010
#define REG_DIFF_ST	0x0014
#define REG_CKG1	0x0018
#define REG_CKG2	0x001C
#define REG_DIDT	0x0020
#define REG_DODT	0x0024
#define REG_MUTE_ST	0x0028
#define REG_OUT_DMAC	0x002C
#define REG_OUT_SEL	0x0030
#define REG_IN_DMAC	0x0038

/* master register */
#define MST_CLK_RST	0x0210
#define MST_SOFT_RST	0x0214
#define MST_FIFO_SZ	0x0218

/* core register (depend on FSI version) */
#define A_MST_CTLR	0x0180
#define B_MST_CTLR	0x01A0
#define CPU_INT_ST	0x01F4
#define CPU_IEMSK	0x01F8
#define CPU_IMSK	0x01FC
#define INT_ST		0x0200
#define IEMSK		0x0204
#define IMSK		0x0208

/* DO_FMT */
/* DI_FMT */
#define CR_BWS_MASK	(0x3 << 20) /* FSI2 */
#define CR_BWS_24	(0x0 << 20) /* FSI2 */
#define CR_BWS_16	(0x1 << 20) /* FSI2 */
#define CR_BWS_20	(0x2 << 20) /* FSI2 */

#define CR_DTMD_PCM		(0x0 << 8) /* FSI2 */
#define CR_DTMD_SPDIF_PCM	(0x1 << 8) /* FSI2 */
#define CR_DTMD_SPDIF_STREAM	(0x2 << 8) /* FSI2 */

#define CR_MONO		(0x0 << 4)
#define CR_MONO_D	(0x1 << 4)
#define CR_PCM		(0x2 << 4)
#define CR_I2S		(0x3 << 4)
#define CR_TDM		(0x4 << 4)
#define CR_TDM_D	(0x5 << 4)

/* OUT_DMAC */
/* IN_DMAC */
#define VDMD_MASK	(0x3 << 4)
#define VDMD_FRONT	(0x0 << 4) /* Package in front */
#define VDMD_BACK	(0x1 << 4) /* Package in back */
#define VDMD_STREAM	(0x2 << 4) /* Stream mode(16bit * 2) */

#define DMA_ON		(0x1 << 0)

/* DOFF_CTL */
/* DIFF_CTL */
#define IRQ_HALF	0x00100000
#define FIFO_CLR	0x00000001

/* DOFF_ST */
#define ERR_OVER	0x00000010
#define ERR_UNDER	0x00000001
#define ST_ERR		(ERR_OVER | ERR_UNDER)

/* CKG1 */
#define ACKMD_MASK	0x00007000
#define BPFMD_MASK	0x00000700
#define DIMD		(1 << 4)
#define DOMD		(1 << 0)

/* A/B MST_CTLR */
#define BP	(1 << 4)	/* Fix the signal of Biphase output */
#define SE	(1 << 0)	/* Fix the master clock */

/* CLK_RST */
#define CRB	(1 << 4)
#define CRA	(1 << 0)

/* IO SHIFT / MACRO */
#define BI_SHIFT	12
#define BO_SHIFT	8
#define AI_SHIFT	4
#define AO_SHIFT	0
#define AB_IO(param, shift)	(param << shift)

/* SOFT_RST */
#define PBSR		(1 << 12) /* Port B Software Reset */
#define PASR		(1 <<  8) /* Port A Software Reset */
#define IR		(1 <<  4) /* Interrupt Reset */
#define FSISR		(1 <<  0) /* Software Reset */

/* OUT_SEL (FSI2) */
#define DMMD		(1 << 4) /* SPDIF output timing 0: Biphase only */
				 /*			1: Biphase and serial */

/* FIFO_SZ */
#define FIFO_SZ_MASK	0x7

#define FSI_RATES SNDRV_PCM_RATE_8000_96000

#define FSI_FMTS (SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S16_LE)

/*
 * bus options
 *
 * 0x000000BA
 *
 * A : sample widtht 16bit setting
 * B : sample widtht 24bit setting
 */

#define SHIFT_16DATA		0
#define SHIFT_24DATA		4

#define PACKAGE_24BITBUS_BACK		0
#define PACKAGE_24BITBUS_FRONT		1
#define PACKAGE_16BITBUS_STREAM		2

#define BUSOP_SET(s, a)	((a) << SHIFT_ ## s ## DATA)
#define BUSOP_GET(s, a)	(((a) >> SHIFT_ ## s ## DATA) & 0xF)

/*
 * FSI driver use below type name for variable
 *
 * xxx_num	: number of data
 * xxx_pos	: position of data
 * xxx_capa	: capacity of data
 */

/*
 *	period/frame/sample image
 *
 * ex) PCM (2ch)
 *
 * period pos					   period pos
 *   [n]					     [n + 1]
 *   |<-------------------- period--------------------->|
 * ==|============================================ ... =|==
 *   |							|
 *   ||<-----  frame ----->|<------ frame ----->|  ...	|
 *   |+--------------------+--------------------+- ...	|
 *   ||[ sample ][ sample ]|[ sample ][ sample ]|  ...	|
 *   |+--------------------+--------------------+- ...	|
 * ==|============================================ ... =|==
 */

/*
 *	FSI FIFO image
 *
 *	|	     |
 *	|	     |
 *	| [ sample ] |
 *	| [ sample ] |
 *	| [ sample ] |
 *	| [ sample ] |
 *		--> go to codecs
 */

/*
 *	FSI clock
 *
 * FSIxCLK [CPG] (ick) ------->	|
 *				|-> FSI_DIV (div)-> FSI2
 * FSIxCK [external] (xck) --->	|
 */

/*
 *		struct
 */

struct fsi_stream_handler;
struct fsi_stream {

	/*
	 * these are initialized by fsi_stream_init()
	 */
	struct snd_pcm_substream *substream;
	int fifo_sample_capa;	/* sample capacity of FSI FIFO */
	int buff_sample_capa;	/* sample capacity of ALSA buffer */
	int buff_sample_pos;	/* sample position of ALSA buffer */
	int period_samples;	/* sample number / 1 period */
	int period_pos;		/* current period position */
	int sample_width;	/* sample width */
	int uerr_num;
	int oerr_num;

	/*
	 * bus options
	 */
	u32 bus_option;

	/*
	 * thse are initialized by fsi_handler_init()
	 */
	struct fsi_stream_handler *handler;
	struct fsi_priv		*priv;

	/*
	 * these are for DMAEngine
	 */
	struct dma_chan		*chan;
	int			dma_id;
};

struct fsi_clk {
	/* see [FSI clock] */
	struct clk *own;
	struct clk *xck;
	struct clk *ick;
	struct clk *div;
	int (*set_rate)(struct device *dev,
			struct fsi_priv *fsi);

	unsigned long rate;
	unsigned int count;
};

struct fsi_priv {
	void __iomem *base;
	phys_addr_t phys;
	struct fsi_master *master;

	struct fsi_stream playback;
	struct fsi_stream capture;

	struct fsi_clk clock;

	u32 fmt;

	int chan_num:16;
	unsigned int clk_master:1;
	unsigned int clk_cpg:1;
	unsigned int spdif:1;
	unsigned int enable_stream:1;
	unsigned int bit_clk_inv:1;
	unsigned int lr_clk_inv:1;
};

struct fsi_stream_handler {
	int (*init)(struct fsi_priv *fsi, struct fsi_stream *io);
	int (*quit)(struct fsi_priv *fsi, struct fsi_stream *io);
	int (*probe)(struct fsi_priv *fsi, struct fsi_stream *io, struct device *dev);
	int (*transfer)(struct fsi_priv *fsi, struct fsi_stream *io);
	int (*remove)(struct fsi_priv *fsi, struct fsi_stream *io);
	int (*start_stop)(struct fsi_priv *fsi, struct fsi_stream *io,
			   int enable);
};
#define fsi_stream_handler_call(io, func, args...)	\
	(!(io) ? -ENODEV :				\
	 !((io)->handler->func) ? 0 :			\
	 (io)->handler->func(args))

struct fsi_core {
	int ver;

	u32 int_st;
	u32 iemsk;
	u32 imsk;
	u32 a_mclk;
	u32 b_mclk;
};

struct fsi_master {
	void __iomem *base;
	struct fsi_priv fsia;
	struct fsi_priv fsib;
	const struct fsi_core *core;
	spinlock_t lock;
};

static int fsi_stream_is_play(struct fsi_priv *fsi, struct fsi_stream *io);

/*
 *		basic read write function
 */

static void __fsi_reg_write(u32 __iomem *reg, u32 data)
{
	/* valid data area is 24bit */
	data &= 0x00ffffff;

	__raw_writel(data, reg);
}

static u32 __fsi_reg_read(u32 __iomem *reg)
{
	return __raw_readl(reg);
}

static void __fsi_reg_mask_set(u32 __iomem *reg, u32 mask, u32 data)
{
	u32 val = __fsi_reg_read(reg);

	val &= ~mask;
	val |= data & mask;

	__fsi_reg_write(reg, val);
}

#define fsi_reg_write(p, r, d)\
	__fsi_reg_write((p->base + REG_##r), d)

#define fsi_reg_read(p, r)\
	__fsi_reg_read((p->base + REG_##r))

#define fsi_reg_mask_set(p, r, m, d)\
	__fsi_reg_mask_set((p->base + REG_##r), m, d)

#define fsi_master_read(p, r) _fsi_master_read(p, MST_##r)
#define fsi_core_read(p, r)   _fsi_master_read(p, p->core->r)
static u32 _fsi_master_read(struct fsi_master *master, u32 reg)
{
	u32 ret;
	unsigned long flags;

	spin_lock_irqsave(&master->lock, flags);
	ret = __fsi_reg_read(master->base + reg);
	spin_unlock_irqrestore(&master->lock, flags);

	return ret;
}

#define fsi_master_mask_set(p, r, m, d) _fsi_master_mask_set(p, MST_##r, m, d)
#define fsi_core_mask_set(p, r, m, d)  _fsi_master_mask_set(p, p->core->r, m, d)
static void _fsi_master_mask_set(struct fsi_master *master,
			       u32 reg, u32 mask, u32 data)
{
	unsigned long flags;

	spin_lock_irqsave(&master->lock, flags);
	__fsi_reg_mask_set(master->base + reg, mask, data);
	spin_unlock_irqrestore(&master->lock, flags);
}

/*
 *		basic function
 */
static int fsi_version(struct fsi_master *master)
{
	return master->core->ver;
}

static struct fsi_master *fsi_get_master(struct fsi_priv *fsi)
{
	return fsi->master;
}

static int fsi_is_clk_master(struct fsi_priv *fsi)
{
	return fsi->clk_master;
}

static int fsi_is_port_a(struct fsi_priv *fsi)
{
	return fsi->master->base == fsi->base;
}

static int fsi_is_spdif(struct fsi_priv *fsi)
{
	return fsi->spdif;
}

static int fsi_is_enable_stream(struct fsi_priv *fsi)
{
	return fsi->enable_stream;
}

static int fsi_is_play(struct snd_pcm_substream *substream)
{
	return substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
}

static struct snd_soc_dai *fsi_get_dai(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;

	return  rtd->cpu_dai;
}

static struct fsi_priv *fsi_get_priv_frm_dai(struct snd_soc_dai *dai)
{
	struct fsi_master *master = snd_soc_dai_get_drvdata(dai);

	if (dai->id == 0)
		return &master->fsia;
	else
		return &master->fsib;
}

static struct fsi_priv *fsi_get_priv(struct snd_pcm_substream *substream)
{
	return fsi_get_priv_frm_dai(fsi_get_dai(substream));
}

static u32 fsi_get_port_shift(struct fsi_priv *fsi, struct fsi_stream *io)
{
	int is_play = fsi_stream_is_play(fsi, io);
	int is_porta = fsi_is_port_a(fsi);
	u32 shift;

	if (is_porta)
		shift = is_play ? AO_SHIFT : AI_SHIFT;
	else
		shift = is_play ? BO_SHIFT : BI_SHIFT;

	return shift;
}

static int fsi_frame2sample(struct fsi_priv *fsi, int frames)
{
	return frames * fsi->chan_num;
}

static int fsi_sample2frame(struct fsi_priv *fsi, int samples)
{
	return samples / fsi->chan_num;
}

static int fsi_get_current_fifo_samples(struct fsi_priv *fsi,
					struct fsi_stream *io)
{
	int is_play = fsi_stream_is_play(fsi, io);
	u32 status;
	int frames;

	status = is_play ?
		fsi_reg_read(fsi, DOFF_ST) :
		fsi_reg_read(fsi, DIFF_ST);

	frames = 0x1ff & (status >> 8);

	return fsi_frame2sample(fsi, frames);
}

static void fsi_count_fifo_err(struct fsi_priv *fsi)
{
	u32 ostatus = fsi_reg_read(fsi, DOFF_ST);
	u32 istatus = fsi_reg_read(fsi, DIFF_ST);

	if (ostatus & ERR_OVER)
		fsi->playback.oerr_num++;

	if (ostatus & ERR_UNDER)
		fsi->playback.uerr_num++;

	if (istatus & ERR_OVER)
		fsi->capture.oerr_num++;

	if (istatus & ERR_UNDER)
		fsi->capture.uerr_num++;

	fsi_reg_write(fsi, DOFF_ST, 0);
	fsi_reg_write(fsi, DIFF_ST, 0);
}

/*
 *		fsi_stream_xx() function
 */
static inline int fsi_stream_is_play(struct fsi_priv *fsi,
				     struct fsi_stream *io)
{
	return &fsi->playback == io;
}

static inline struct fsi_stream *fsi_stream_get(struct fsi_priv *fsi,
					struct snd_pcm_substream *substream)
{
	return fsi_is_play(substream) ? &fsi->playback : &fsi->capture;
}

static int fsi_stream_is_working(struct fsi_priv *fsi,
				 struct fsi_stream *io)
{
	struct fsi_master *master = fsi_get_master(fsi);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&master->lock, flags);
	ret = !!(io->substream && io->substream->runtime);
	spin_unlock_irqrestore(&master->lock, flags);

	return ret;
}

static struct fsi_priv *fsi_stream_to_priv(struct fsi_stream *io)
{
	return io->priv;
}

static void fsi_stream_init(struct fsi_priv *fsi,
			    struct fsi_stream *io,
			    struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct fsi_master *master = fsi_get_master(fsi);
	unsigned long flags;

	spin_lock_irqsave(&master->lock, flags);
	io->substream	= substream;
	io->buff_sample_capa	= fsi_frame2sample(fsi, runtime->buffer_size);
	io->buff_sample_pos	= 0;
	io->period_samples	= fsi_frame2sample(fsi, runtime->period_size);
	io->period_pos		= 0;
	io->sample_width	= samples_to_bytes(runtime, 1);
	io->bus_option		= 0;
	io->oerr_num	= -1; /* ignore 1st err */
	io->uerr_num	= -1; /* ignore 1st err */
	fsi_stream_handler_call(io, init, fsi, io);
	spin_unlock_irqrestore(&master->lock, flags);
}

static void fsi_stream_quit(struct fsi_priv *fsi, struct fsi_stream *io)
{
	struct snd_soc_dai *dai = fsi_get_dai(io->substream);
	struct fsi_master *master = fsi_get_master(fsi);
	unsigned long flags;

	spin_lock_irqsave(&master->lock, flags);

	if (io->oerr_num > 0)
		dev_err(dai->dev, "over_run = %d\n", io->oerr_num);

	if (io->uerr_num > 0)
		dev_err(dai->dev, "under_run = %d\n", io->uerr_num);

	fsi_stream_handler_call(io, quit, fsi, io);
	io->substream	= NULL;
	io->buff_sample_capa	= 0;
	io->buff_sample_pos	= 0;
	io->period_samples	= 0;
	io->period_pos		= 0;
	io->sample_width	= 0;
	io->bus_option		= 0;
	io->oerr_num	= 0;
	io->uerr_num	= 0;
	spin_unlock_irqrestore(&master->lock, flags);
}

static int fsi_stream_transfer(struct fsi_stream *io)
{
	struct fsi_priv *fsi = fsi_stream_to_priv(io);
	if (!fsi)
		return -EIO;

	return fsi_stream_handler_call(io, transfer, fsi, io);
}

#define fsi_stream_start(fsi, io)\
	fsi_stream_handler_call(io, start_stop, fsi, io, 1)

#define fsi_stream_stop(fsi, io)\
	fsi_stream_handler_call(io, start_stop, fsi, io, 0)

static int fsi_stream_probe(struct fsi_priv *fsi, struct device *dev)
{
	struct fsi_stream *io;
	int ret1, ret2;

	io = &fsi->playback;
	ret1 = fsi_stream_handler_call(io, probe, fsi, io, dev);

	io = &fsi->capture;
	ret2 = fsi_stream_handler_call(io, probe, fsi, io, dev);

	if (ret1 < 0)
		return ret1;
	if (ret2 < 0)
		return ret2;

	return 0;
}

static int fsi_stream_remove(struct fsi_priv *fsi)
{
	struct fsi_stream *io;
	int ret1, ret2;

	io = &fsi->playback;
	ret1 = fsi_stream_handler_call(io, remove, fsi, io);

	io = &fsi->capture;
	ret2 = fsi_stream_handler_call(io, remove, fsi, io);

	if (ret1 < 0)
		return ret1;
	if (ret2 < 0)
		return ret2;

	return 0;
}

/*
 *	format/bus/dma setting
 */
static void fsi_format_bus_setup(struct fsi_priv *fsi, struct fsi_stream *io,
				 u32 bus, struct device *dev)
{
	struct fsi_master *master = fsi_get_master(fsi);
	int is_play = fsi_stream_is_play(fsi, io);
	u32 fmt = fsi->fmt;

	if (fsi_version(master) >= 2) {
		u32 dma = 0;

		/*
		 * FSI2 needs DMA/Bus setting
		 */
		switch (bus) {
		case PACKAGE_24BITBUS_FRONT:
			fmt |= CR_BWS_24;
			dma |= VDMD_FRONT;
			dev_dbg(dev, "24bit bus / package in front\n");
			break;
		case PACKAGE_16BITBUS_STREAM:
			fmt |= CR_BWS_16;
			dma |= VDMD_STREAM;
			dev_dbg(dev, "16bit bus / stream mode\n");
			break;
		case PACKAGE_24BITBUS_BACK:
		default:
			fmt |= CR_BWS_24;
			dma |= VDMD_BACK;
			dev_dbg(dev, "24bit bus / package in back\n");
			break;
		}

		if (is_play)
			fsi_reg_write(fsi, OUT_DMAC,	dma);
		else
			fsi_reg_write(fsi, IN_DMAC,	dma);
	}

	if (is_play)
		fsi_reg_write(fsi, DO_FMT, fmt);
	else
		fsi_reg_write(fsi, DI_FMT, fmt);
}

/*
 *		irq function
 */

static void fsi_irq_enable(struct fsi_priv *fsi, struct fsi_stream *io)
{
	u32 data = AB_IO(1, fsi_get_port_shift(fsi, io));
	struct fsi_master *master = fsi_get_master(fsi);

	fsi_core_mask_set(master, imsk,  data, data);
	fsi_core_mask_set(master, iemsk, data, data);
}

static void fsi_irq_disable(struct fsi_priv *fsi, struct fsi_stream *io)
{
	u32 data = AB_IO(1, fsi_get_port_shift(fsi, io));
	struct fsi_master *master = fsi_get_master(fsi);

	fsi_core_mask_set(master, imsk,  data, 0);
	fsi_core_mask_set(master, iemsk, data, 0);
}

static u32 fsi_irq_get_status(struct fsi_master *master)
{
	return fsi_core_read(master, int_st);
}

static void fsi_irq_clear_status(struct fsi_priv *fsi)
{
	u32 data = 0;
	struct fsi_master *master = fsi_get_master(fsi);

	data |= AB_IO(1, fsi_get_port_shift(fsi, &fsi->playback));
	data |= AB_IO(1, fsi_get_port_shift(fsi, &fsi->capture));

	/* clear interrupt factor */
	fsi_core_mask_set(master, int_st, data, 0);
}

/*
 *		SPDIF master clock function
 *
 * These functions are used later FSI2
 */
static void fsi_spdif_clk_ctrl(struct fsi_priv *fsi, int enable)
{
	struct fsi_master *master = fsi_get_master(fsi);
	u32 mask, val;

	mask = BP | SE;
	val = enable ? mask : 0;

	fsi_is_port_a(fsi) ?
		fsi_core_mask_set(master, a_mclk, mask, val) :
		fsi_core_mask_set(master, b_mclk, mask, val);
}

/*
 *		clock function
 */
static int fsi_clk_init(struct device *dev,
			struct fsi_priv *fsi,
			int xck,
			int ick,
			int div,
			int (*set_rate)(struct device *dev,
					struct fsi_priv *fsi))
{
	struct fsi_clk *clock = &fsi->clock;
	int is_porta = fsi_is_port_a(fsi);

	clock->xck	= NULL;
	clock->ick	= NULL;
	clock->div	= NULL;
	clock->rate	= 0;
	clock->count	= 0;
	clock->set_rate	= set_rate;

	clock->own = devm_clk_get(dev, NULL);
	if (IS_ERR(clock->own))
		return -EINVAL;

	/* external clock */
	if (xck) {
		clock->xck = devm_clk_get(dev, is_porta ? "xcka" : "xckb");
		if (IS_ERR(clock->xck)) {
			dev_err(dev, "can't get xck clock\n");
			return -EINVAL;
		}
		if (clock->xck == clock->own) {
			dev_err(dev, "cpu doesn't support xck clock\n");
			return -EINVAL;
		}
	}

	/* FSIACLK/FSIBCLK */
	if (ick) {
		clock->ick = devm_clk_get(dev,  is_porta ? "icka" : "ickb");
		if (IS_ERR(clock->ick)) {
			dev_err(dev, "can't get ick clock\n");
			return -EINVAL;
		}
		if (clock->ick == clock->own) {
			dev_err(dev, "cpu doesn't support ick clock\n");
			return -EINVAL;
		}
	}

	/* FSI-DIV */
	if (div) {
		clock->div = devm_clk_get(dev,  is_porta ? "diva" : "divb");
		if (IS_ERR(clock->div)) {
			dev_err(dev, "can't get div clock\n");
			return -EINVAL;
		}
		if (clock->div == clock->own) {
			dev_err(dev, "cpu doens't support div clock\n");
			return -EINVAL;
		}
	}

	return 0;
}

#define fsi_clk_invalid(fsi) fsi_clk_valid(fsi, 0)
static void fsi_clk_valid(struct fsi_priv *fsi, unsigned long rate)
{
	fsi->clock.rate = rate;
}

static int fsi_clk_is_valid(struct fsi_priv *fsi)
{
	return	fsi->clock.set_rate &&
		fsi->clock.rate;
}

static int fsi_clk_enable(struct device *dev,
			  struct fsi_priv *fsi)
{
	struct fsi_clk *clock = &fsi->clock;
	int ret = -EINVAL;

	if (!fsi_clk_is_valid(fsi))
		return ret;

	if (0 == clock->count) {
		ret = clock->set_rate(dev, fsi);
		if (ret < 0) {
			fsi_clk_invalid(fsi);
			return ret;
		}

		clk_enable(clock->xck);
		clk_enable(clock->ick);
		clk_enable(clock->div);

		clock->count++;
	}

	return ret;
}

static int fsi_clk_disable(struct device *dev,
			    struct fsi_priv *fsi)
{
	struct fsi_clk *clock = &fsi->clock;

	if (!fsi_clk_is_valid(fsi))
		return -EINVAL;

	if (1 == clock->count--) {
		clk_disable(clock->xck);
		clk_disable(clock->ick);
		clk_disable(clock->div);
	}

	return 0;
}

static int fsi_clk_set_ackbpf(struct device *dev,
			      struct fsi_priv *fsi,
			      int ackmd, int bpfmd)
{
	u32 data = 0;

	/* check ackmd/bpfmd relationship */
	if (bpfmd > ackmd) {
		dev_err(dev, "unsupported rate (%d/%d)\n", ackmd, bpfmd);
		return -EINVAL;
	}

	/*  ACKMD */
	switch (ackmd) {
	case 512:
		data |= (0x0 << 12);
		break;
	case 256:
		data |= (0x1 << 12);
		break;
	case 128:
		data |= (0x2 << 12);
		break;
	case 64:
		data |= (0x3 << 12);
		break;
	case 32:
		data |= (0x4 << 12);
		break;
	default:
		dev_err(dev, "unsupported ackmd (%d)\n", ackmd);
		return -EINVAL;
	}

	/* BPFMD */
	switch (bpfmd) {
	case 32:
		data |= (0x0 << 8);
		break;
	case 64:
		data |= (0x1 << 8);
		break;
	case 128:
		data |= (0x2 << 8);
		break;
	case 256:
		data |= (0x3 << 8);
		break;
	case 512:
		data |= (0x4 << 8);
		break;
	case 16:
		data |= (0x7 << 8);
		break;
	default:
		dev_err(dev, "unsupported bpfmd (%d)\n", bpfmd);
		return -EINVAL;
	}

	dev_dbg(dev, "ACKMD/BPFMD = %d/%d\n", ackmd, bpfmd);

	fsi_reg_mask_set(fsi, CKG1, (ACKMD_MASK | BPFMD_MASK) , data);
	udelay(10);

	return 0;
}

static int fsi_clk_set_rate_external(struct device *dev,
				     struct fsi_priv *fsi)
{
	struct clk *xck = fsi->clock.xck;
	struct clk *ick = fsi->clock.ick;
	unsigned long rate = fsi->clock.rate;
	unsigned long xrate;
	int ackmd, bpfmd;
	int ret = 0;

	/* check clock rate */
	xrate = clk_get_rate(xck);
	if (xrate % rate) {
		dev_err(dev, "unsupported clock rate\n");
		return -EINVAL;
	}

	clk_set_parent(ick, xck);
	clk_set_rate(ick, xrate);

	bpfmd = fsi->chan_num * 32;
	ackmd = xrate / rate;

	dev_dbg(dev, "external/rate = %ld/%ld\n", xrate, rate);

	ret = fsi_clk_set_ackbpf(dev, fsi, ackmd, bpfmd);
	if (ret < 0)
		dev_err(dev, "%s failed", __func__);

	return ret;
}

static int fsi_clk_set_rate_cpg(struct device *dev,
				struct fsi_priv *fsi)
{
	struct clk *ick = fsi->clock.ick;
	struct clk *div = fsi->clock.div;
	unsigned long rate = fsi->clock.rate;
	unsigned long target = 0; /* 12288000 or 11289600 */
	unsigned long actual, cout;
	unsigned long diff, min;
	unsigned long best_cout, best_act;
	int adj;
	int ackmd, bpfmd;
	int ret = -EINVAL;

	if (!(12288000 % rate))
		target = 12288000;
	if (!(11289600 % rate))
		target = 11289600;
	if (!target) {
		dev_err(dev, "unsupported rate\n");
		return ret;
	}

	bpfmd = fsi->chan_num * 32;
	ackmd = target / rate;
	ret = fsi_clk_set_ackbpf(dev, fsi, ackmd, bpfmd);
	if (ret < 0) {
		dev_err(dev, "%s failed", __func__);
		return ret;
	}

	/*
	 * The clock flow is
	 *
	 * [CPG] = cout => [FSI_DIV] = audio => [FSI] => [codec]
	 *
	 * But, it needs to find best match of CPG and FSI_DIV
	 * combination, since it is difficult to generate correct
	 * frequency of audio clock from ick clock only.
	 * Because ick is created from its parent clock.
	 *
	 * target	= rate x [512/256/128/64]fs
	 * cout		= round(target x adjustment)
	 * actual	= cout / adjustment (by FSI-DIV) ~= target
	 * audio	= actual
	 */
	min = ~0;
	best_cout = 0;
	best_act = 0;
	for (adj = 1; adj < 0xffff; adj++) {

		cout = target * adj;
		if (cout > 100000000) /* max clock = 100MHz */
			break;

		/* cout/actual audio clock */
		cout	= clk_round_rate(ick, cout);
		actual	= cout / adj;

		/* find best frequency */
		diff = abs(actual - target);
		if (diff < min) {
			min		= diff;
			best_cout	= cout;
			best_act	= actual;
		}
	}

	ret = clk_set_rate(ick, best_cout);
	if (ret < 0) {
		dev_err(dev, "ick clock failed\n");
		return -EIO;
	}

	ret = clk_set_rate(div, clk_round_rate(div, best_act));
	if (ret < 0) {
		dev_err(dev, "div clock failed\n");
		return -EIO;
	}

	dev_dbg(dev, "ick/div = %ld/%ld\n",
		clk_get_rate(ick), clk_get_rate(div));

	return ret;
}

static void fsi_pointer_update(struct fsi_stream *io, int size)
{
	io->buff_sample_pos += size;

	if (io->buff_sample_pos >=
	    io->period_samples * (io->period_pos + 1)) {
		struct snd_pcm_substream *substream = io->substream;
		struct snd_pcm_runtime *runtime = substream->runtime;

		io->period_pos++;

		if (io->period_pos >= runtime->periods) {
			io->buff_sample_pos = 0;
			io->period_pos = 0;
		}

		snd_pcm_period_elapsed(substream);
	}
}

/*
 *		pio data transfer handler
 */
static void fsi_pio_push16(struct fsi_priv *fsi, u8 *_buf, int samples)
{
	int i;

	if (fsi_is_enable_stream(fsi)) {
		/*
		 * stream mode
		 * see
		 *	fsi_pio_push_init()
		 */
		u32 *buf = (u32 *)_buf;

		for (i = 0; i < samples / 2; i++)
			fsi_reg_write(fsi, DODT, buf[i]);
	} else {
		/* normal mode */
		u16 *buf = (u16 *)_buf;

		for (i = 0; i < samples; i++)
			fsi_reg_write(fsi, DODT, ((u32)*(buf + i) << 8));
	}
}

static void fsi_pio_pop16(struct fsi_priv *fsi, u8 *_buf, int samples)
{
	u16 *buf = (u16 *)_buf;
	int i;

	for (i = 0; i < samples; i++)
		*(buf + i) = (u16)(fsi_reg_read(fsi, DIDT) >> 8);
}

static void fsi_pio_push32(struct fsi_priv *fsi, u8 *_buf, int samples)
{
	u32 *buf = (u32 *)_buf;
	int i;

	for (i = 0; i < samples; i++)
		fsi_reg_write(fsi, DODT, *(buf + i));
}

static void fsi_pio_pop32(struct fsi_priv *fsi, u8 *_buf, int samples)
{
	u32 *buf = (u32 *)_buf;
	int i;

	for (i = 0; i < samples; i++)
		*(buf + i) = fsi_reg_read(fsi, DIDT);
}

static u8 *fsi_pio_get_area(struct fsi_priv *fsi, struct fsi_stream *io)
{
	struct snd_pcm_runtime *runtime = io->substream->runtime;

	return runtime->dma_area +
		samples_to_bytes(runtime, io->buff_sample_pos);
}

static int fsi_pio_transfer(struct fsi_priv *fsi, struct fsi_stream *io,
		void (*run16)(struct fsi_priv *fsi, u8 *buf, int samples),
		void (*run32)(struct fsi_priv *fsi, u8 *buf, int samples),
		int samples)
{
	u8 *buf;

	if (!fsi_stream_is_working(fsi, io))
		return -EINVAL;

	buf = fsi_pio_get_area(fsi, io);

	switch (io->sample_width) {
	case 2:
		run16(fsi, buf, samples);
		break;
	case 4:
		run32(fsi, buf, samples);
		break;
	default:
		return -EINVAL;
	}

	fsi_pointer_update(io, samples);

	return 0;
}

static int fsi_pio_pop(struct fsi_priv *fsi, struct fsi_stream *io)
{
	int sample_residues;	/* samples in FSI fifo */
	int sample_space;	/* ALSA free samples space */
	int samples;

	sample_residues	= fsi_get_current_fifo_samples(fsi, io);
	sample_space	= io->buff_sample_capa - io->buff_sample_pos;

	samples = min(sample_residues, sample_space);

	return fsi_pio_transfer(fsi, io,
				  fsi_pio_pop16,
				  fsi_pio_pop32,
				  samples);
}

static int fsi_pio_push(struct fsi_priv *fsi, struct fsi_stream *io)
{
	int sample_residues;	/* ALSA residue samples */
	int sample_space;	/* FSI fifo free samples space */
	int samples;

	sample_residues	= io->buff_sample_capa - io->buff_sample_pos;
	sample_space	= io->fifo_sample_capa -
		fsi_get_current_fifo_samples(fsi, io);

	samples = min(sample_residues, sample_space);

	return fsi_pio_transfer(fsi, io,
				  fsi_pio_push16,
				  fsi_pio_push32,
				  samples);
}

static int fsi_pio_start_stop(struct fsi_priv *fsi, struct fsi_stream *io,
			       int enable)
{
	struct fsi_master *master = fsi_get_master(fsi);
	u32 clk  = fsi_is_port_a(fsi) ? CRA  : CRB;

	if (enable)
		fsi_irq_enable(fsi, io);
	else
		fsi_irq_disable(fsi, io);

	if (fsi_is_clk_master(fsi))
		fsi_master_mask_set(master, CLK_RST, clk, (enable) ? clk : 0);

	return 0;
}

static int fsi_pio_push_init(struct fsi_priv *fsi, struct fsi_stream *io)
{
	/*
	 * we can use 16bit stream mode
	 * when "playback" and "16bit data"
	 * and platform allows "stream mode"
	 * see
	 *	fsi_pio_push16()
	 */
	if (fsi_is_enable_stream(fsi))
		io->bus_option = BUSOP_SET(24, PACKAGE_24BITBUS_BACK) |
				 BUSOP_SET(16, PACKAGE_16BITBUS_STREAM);
	else
		io->bus_option = BUSOP_SET(24, PACKAGE_24BITBUS_BACK) |
				 BUSOP_SET(16, PACKAGE_24BITBUS_BACK);
	return 0;
}

static int fsi_pio_pop_init(struct fsi_priv *fsi, struct fsi_stream *io)
{
	/*
	 * always 24bit bus, package back when "capture"
	 */
	io->bus_option = BUSOP_SET(24, PACKAGE_24BITBUS_BACK) |
			 BUSOP_SET(16, PACKAGE_24BITBUS_BACK);
	return 0;
}

static struct fsi_stream_handler fsi_pio_push_handler = {
	.init		= fsi_pio_push_init,
	.transfer	= fsi_pio_push,
	.start_stop	= fsi_pio_start_stop,
};

static struct fsi_stream_handler fsi_pio_pop_handler = {
	.init		= fsi_pio_pop_init,
	.transfer	= fsi_pio_pop,
	.start_stop	= fsi_pio_start_stop,
};

static irqreturn_t fsi_interrupt(int irq, void *data)
{
	struct fsi_master *master = data;
	u32 int_st = fsi_irq_get_status(master);

	/* clear irq status */
	fsi_master_mask_set(master, SOFT_RST, IR, 0);
	fsi_master_mask_set(master, SOFT_RST, IR, IR);

	if (int_st & AB_IO(1, AO_SHIFT))
		fsi_stream_transfer(&master->fsia.playback);
	if (int_st & AB_IO(1, BO_SHIFT))
		fsi_stream_transfer(&master->fsib.playback);
	if (int_st & AB_IO(1, AI_SHIFT))
		fsi_stream_transfer(&master->fsia.capture);
	if (int_st & AB_IO(1, BI_SHIFT))
		fsi_stream_transfer(&master->fsib.capture);

	fsi_count_fifo_err(&master->fsia);
	fsi_count_fifo_err(&master->fsib);

	fsi_irq_clear_status(&master->fsia);
	fsi_irq_clear_status(&master->fsib);

	return IRQ_HANDLED;
}

/*
 *		dma data transfer handler
 */
static int fsi_dma_init(struct fsi_priv *fsi, struct fsi_stream *io)
{
	/*
	 * 24bit data : 24bit bus / package in back
	 * 16bit data : 16bit bus / stream mode
	 */
	io->bus_option = BUSOP_SET(24, PACKAGE_24BITBUS_BACK) |
			 BUSOP_SET(16, PACKAGE_16BITBUS_STREAM);

	return 0;
}

static void fsi_dma_complete(void *data)
{
	struct fsi_stream *io = (struct fsi_stream *)data;
	struct fsi_priv *fsi = fsi_stream_to_priv(io);

	fsi_pointer_update(io, io->period_samples);

	fsi_count_fifo_err(fsi);
}

static int fsi_dma_transfer(struct fsi_priv *fsi, struct fsi_stream *io)
{
	struct snd_soc_dai *dai = fsi_get_dai(io->substream);
	struct snd_pcm_substream *substream = io->substream;
	struct dma_async_tx_descriptor *desc;
	int is_play = fsi_stream_is_play(fsi, io);
	enum dma_transfer_direction dir;
	int ret = -EIO;

	if (is_play)
		dir = DMA_MEM_TO_DEV;
	else
		dir = DMA_DEV_TO_MEM;

	desc = dmaengine_prep_dma_cyclic(io->chan,
					 substream->runtime->dma_addr,
					 snd_pcm_lib_buffer_bytes(substream),
					 snd_pcm_lib_period_bytes(substream),
					 dir,
					 DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc) {
		dev_err(dai->dev, "dmaengine_prep_dma_cyclic() fail\n");
		goto fsi_dma_transfer_err;
	}

	desc->callback		= fsi_dma_complete;
	desc->callback_param	= io;

	if (dmaengine_submit(desc) < 0) {
		dev_err(dai->dev, "tx_submit() fail\n");
		goto fsi_dma_transfer_err;
	}

	dma_async_issue_pending(io->chan);

	/*
	 * FIXME
	 *
	 * In DMAEngine case, codec and FSI cannot be started simultaneously
	 * since FSI is using the scheduler work queue.
	 * Therefore, in capture case, probably FSI FIFO will have got
	 * overflow error in this point.
	 * in that case, DMA cannot start transfer until error was cleared.
	 */
	if (!is_play) {
		if (ERR_OVER & fsi_reg_read(fsi, DIFF_ST)) {
			fsi_reg_mask_set(fsi, DIFF_CTL, FIFO_CLR, FIFO_CLR);
			fsi_reg_write(fsi, DIFF_ST, 0);
		}
	}

	ret = 0;

fsi_dma_transfer_err:
	return ret;
}

static int fsi_dma_push_start_stop(struct fsi_priv *fsi, struct fsi_stream *io,
				 int start)
{
	struct fsi_master *master = fsi_get_master(fsi);
	u32 clk  = fsi_is_port_a(fsi) ? CRA  : CRB;
	u32 enable = start ? DMA_ON : 0;

	fsi_reg_mask_set(fsi, OUT_DMAC, DMA_ON, enable);

	dmaengine_terminate_all(io->chan);

	if (fsi_is_clk_master(fsi))
		fsi_master_mask_set(master, CLK_RST, clk, (enable) ? clk : 0);

	return 0;
}

static int fsi_dma_probe(struct fsi_priv *fsi, struct fsi_stream *io, struct device *dev)
{
	dma_cap_mask_t mask;
	int is_play = fsi_stream_is_play(fsi, io);

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	io->chan = dma_request_slave_channel_compat(mask,
				shdma_chan_filter, (void *)io->dma_id,
				dev, is_play ? "tx" : "rx");
	if (io->chan) {
		struct dma_slave_config cfg = {};
		int ret;

		if (is_play) {
			cfg.dst_addr		= fsi->phys + REG_DODT;
			cfg.dst_addr_width	= DMA_SLAVE_BUSWIDTH_4_BYTES;
			cfg.direction		= DMA_MEM_TO_DEV;
		} else {
			cfg.src_addr		= fsi->phys + REG_DIDT;
			cfg.src_addr_width	= DMA_SLAVE_BUSWIDTH_4_BYTES;
			cfg.direction		= DMA_DEV_TO_MEM;
		}

		ret = dmaengine_slave_config(io->chan, &cfg);
		if (ret < 0) {
			dma_release_channel(io->chan);
			io->chan = NULL;
		}
	}

	if (!io->chan) {

		/* switch to PIO handler */
		if (is_play)
			fsi->playback.handler	= &fsi_pio_push_handler;
		else
			fsi->capture.handler	= &fsi_pio_pop_handler;

		dev_info(dev, "switch handler (dma => pio)\n");

		/* probe again */
		return fsi_stream_probe(fsi, dev);
	}

	return 0;
}

static int fsi_dma_remove(struct fsi_priv *fsi, struct fsi_stream *io)
{
	fsi_stream_stop(fsi, io);

	if (io->chan)
		dma_release_channel(io->chan);

	io->chan = NULL;
	return 0;
}

static struct fsi_stream_handler fsi_dma_push_handler = {
	.init		= fsi_dma_init,
	.probe		= fsi_dma_probe,
	.transfer	= fsi_dma_transfer,
	.remove		= fsi_dma_remove,
	.start_stop	= fsi_dma_push_start_stop,
};

/*
 *		dai ops
 */
static void fsi_fifo_init(struct fsi_priv *fsi,
			  struct fsi_stream *io,
			  struct device *dev)
{
	struct fsi_master *master = fsi_get_master(fsi);
	int is_play = fsi_stream_is_play(fsi, io);
	u32 shift, i;
	int frame_capa;

	/* get on-chip RAM capacity */
	shift = fsi_master_read(master, FIFO_SZ);
	shift >>= fsi_get_port_shift(fsi, io);
	shift &= FIFO_SZ_MASK;
	frame_capa = 256 << shift;
	dev_dbg(dev, "fifo = %d words\n", frame_capa);

	/*
	 * The maximum number of sample data varies depending
	 * on the number of channels selected for the format.
	 *
	 * FIFOs are used in 4-channel units in 3-channel mode
	 * and in 8-channel units in 5- to 7-channel mode
	 * meaning that more FIFOs than the required size of DPRAM
	 * are used.
	 *
	 * ex) if 256 words of DP-RAM is connected
	 * 1 channel:  256 (256 x 1 = 256)
	 * 2 channels: 128 (128 x 2 = 256)
	 * 3 channels:  64 ( 64 x 3 = 192)
	 * 4 channels:  64 ( 64 x 4 = 256)
	 * 5 channels:  32 ( 32 x 5 = 160)
	 * 6 channels:  32 ( 32 x 6 = 192)
	 * 7 channels:  32 ( 32 x 7 = 224)
	 * 8 channels:  32 ( 32 x 8 = 256)
	 */
	for (i = 1; i < fsi->chan_num; i <<= 1)
		frame_capa >>= 1;
	dev_dbg(dev, "%d channel %d store\n",
		fsi->chan_num, frame_capa);

	io->fifo_sample_capa = fsi_frame2sample(fsi, frame_capa);

	/*
	 * set interrupt generation factor
	 * clear FIFO
	 */
	if (is_play) {
		fsi_reg_write(fsi,	DOFF_CTL, IRQ_HALF);
		fsi_reg_mask_set(fsi,	DOFF_CTL, FIFO_CLR, FIFO_CLR);
	} else {
		fsi_reg_write(fsi,	DIFF_CTL, IRQ_HALF);
		fsi_reg_mask_set(fsi,	DIFF_CTL, FIFO_CLR, FIFO_CLR);
	}
}

static int fsi_hw_startup(struct fsi_priv *fsi,
			  struct fsi_stream *io,
			  struct device *dev)
{
	u32 data = 0;

	/* clock setting */
	if (fsi_is_clk_master(fsi))
		data = DIMD | DOMD;

	fsi_reg_mask_set(fsi, CKG1, (DIMD | DOMD), data);

	/* clock inversion (CKG2) */
	data = 0;
	if (fsi->bit_clk_inv)
		data |= (1 << 0);
	if (fsi->lr_clk_inv)
		data |= (1 << 4);
	if (fsi_is_clk_master(fsi))
		data <<= 8;
	fsi_reg_write(fsi, CKG2, data);

	/* spdif ? */
	if (fsi_is_spdif(fsi)) {
		fsi_spdif_clk_ctrl(fsi, 1);
		fsi_reg_mask_set(fsi, OUT_SEL, DMMD, DMMD);
	}

	/*
	 * get bus settings
	 */
	data = 0;
	switch (io->sample_width) {
	case 2:
		data = BUSOP_GET(16, io->bus_option);
		break;
	case 4:
		data = BUSOP_GET(24, io->bus_option);
		break;
	}
	fsi_format_bus_setup(fsi, io, data, dev);

	/* irq clear */
	fsi_irq_disable(fsi, io);
	fsi_irq_clear_status(fsi);

	/* fifo init */
	fsi_fifo_init(fsi, io, dev);

	/* start master clock */
	if (fsi_is_clk_master(fsi))
		return fsi_clk_enable(dev, fsi);

	return 0;
}

static int fsi_hw_shutdown(struct fsi_priv *fsi,
			    struct device *dev)
{
	/* stop master clock */
	if (fsi_is_clk_master(fsi))
		return fsi_clk_disable(dev, fsi);

	return 0;
}

static int fsi_dai_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	struct fsi_priv *fsi = fsi_get_priv(substream);

	fsi_clk_invalid(fsi);

	return 0;
}

static void fsi_dai_shutdown(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct fsi_priv *fsi = fsi_get_priv(substream);

	fsi_clk_invalid(fsi);
}

static int fsi_dai_trigger(struct snd_pcm_substream *substream, int cmd,
			   struct snd_soc_dai *dai)
{
	struct fsi_priv *fsi = fsi_get_priv(substream);
	struct fsi_stream *io = fsi_stream_get(fsi, substream);
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		fsi_stream_init(fsi, io, substream);
		if (!ret)
			ret = fsi_hw_startup(fsi, io, dai->dev);
		if (!ret)
			ret = fsi_stream_start(fsi, io);
		if (!ret)
			ret = fsi_stream_transfer(io);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		if (!ret)
			ret = fsi_hw_shutdown(fsi, dai->dev);
		fsi_stream_stop(fsi, io);
		fsi_stream_quit(fsi, io);
		break;
	}

	return ret;
}

static int fsi_set_fmt_dai(struct fsi_priv *fsi, unsigned int fmt)
{
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		fsi->fmt = CR_I2S;
		fsi->chan_num = 2;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		fsi->fmt = CR_PCM;
		fsi->chan_num = 2;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int fsi_set_fmt_spdif(struct fsi_priv *fsi)
{
	struct fsi_master *master = fsi_get_master(fsi);

	if (fsi_version(master) < 2)
		return -EINVAL;

	fsi->fmt = CR_DTMD_SPDIF_PCM | CR_PCM;
	fsi->chan_num = 2;

	return 0;
}

static int fsi_dai_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct fsi_priv *fsi = fsi_get_priv_frm_dai(dai);
	int ret;

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		fsi->clk_master = 1; /* codec is slave, cpu is master */
		break;
	default:
		return -EINVAL;
	}

	/* set clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_IF:
		fsi->bit_clk_inv = 0;
		fsi->lr_clk_inv = 1;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		fsi->bit_clk_inv = 1;
		fsi->lr_clk_inv = 0;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		fsi->bit_clk_inv = 1;
		fsi->lr_clk_inv = 1;
		break;
	case SND_SOC_DAIFMT_NB_NF:
	default:
		fsi->bit_clk_inv = 0;
		fsi->lr_clk_inv = 0;
		break;
	}

	if (fsi_is_clk_master(fsi)) {
		if (fsi->clk_cpg)
			fsi_clk_init(dai->dev, fsi, 0, 1, 1,
				     fsi_clk_set_rate_cpg);
		else
			fsi_clk_init(dai->dev, fsi, 1, 1, 0,
				     fsi_clk_set_rate_external);
	}

	/* set format */
	if (fsi_is_spdif(fsi))
		ret = fsi_set_fmt_spdif(fsi);
	else
		ret = fsi_set_fmt_dai(fsi, fmt & SND_SOC_DAIFMT_FORMAT_MASK);

	return ret;
}

static int fsi_dai_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct fsi_priv *fsi = fsi_get_priv(substream);

	if (fsi_is_clk_master(fsi))
		fsi_clk_valid(fsi, params_rate(params));

	return 0;
}

static const struct snd_soc_dai_ops fsi_dai_ops = {
	.startup	= fsi_dai_startup,
	.shutdown	= fsi_dai_shutdown,
	.trigger	= fsi_dai_trigger,
	.set_fmt	= fsi_dai_set_fmt,
	.hw_params	= fsi_dai_hw_params,
};

/*
 *		pcm ops
 */

static struct snd_pcm_hardware fsi_pcm_hardware = {
	.info =		SNDRV_PCM_INFO_INTERLEAVED	|
			SNDRV_PCM_INFO_MMAP		|
			SNDRV_PCM_INFO_MMAP_VALID,
	.buffer_bytes_max	= 64 * 1024,
	.period_bytes_min	= 32,
	.period_bytes_max	= 8192,
	.periods_min		= 1,
	.periods_max		= 32,
	.fifo_size		= 256,
};

static int fsi_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int ret = 0;

	snd_soc_set_runtime_hwparams(substream, &fsi_pcm_hardware);

	ret = snd_pcm_hw_constraint_integer(runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);

	return ret;
}

static int fsi_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *hw_params)
{
	return snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(hw_params));
}

static int fsi_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
}

static snd_pcm_uframes_t fsi_pointer(struct snd_pcm_substream *substream)
{
	struct fsi_priv *fsi = fsi_get_priv(substream);
	struct fsi_stream *io = fsi_stream_get(fsi, substream);

	return fsi_sample2frame(fsi, io->buff_sample_pos);
}

static struct snd_pcm_ops fsi_pcm_ops = {
	.open		= fsi_pcm_open,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= fsi_hw_params,
	.hw_free	= fsi_hw_free,
	.pointer	= fsi_pointer,
};

/*
 *		snd_soc_platform
 */

#define PREALLOC_BUFFER		(32 * 1024)
#define PREALLOC_BUFFER_MAX	(32 * 1024)

static int fsi_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	return snd_pcm_lib_preallocate_pages_for_all(
		rtd->pcm,
		SNDRV_DMA_TYPE_DEV,
		rtd->card->snd_card->dev,
		PREALLOC_BUFFER, PREALLOC_BUFFER_MAX);
}

/*
 *		alsa struct
 */

static struct snd_soc_dai_driver fsi_soc_dai[] = {
	{
		.name			= "fsia-dai",
		.playback = {
			.rates		= FSI_RATES,
			.formats	= FSI_FMTS,
			.channels_min	= 2,
			.channels_max	= 2,
		},
		.capture = {
			.rates		= FSI_RATES,
			.formats	= FSI_FMTS,
			.channels_min	= 2,
			.channels_max	= 2,
		},
		.ops = &fsi_dai_ops,
	},
	{
		.name			= "fsib-dai",
		.playback = {
			.rates		= FSI_RATES,
			.formats	= FSI_FMTS,
			.channels_min	= 2,
			.channels_max	= 2,
		},
		.capture = {
			.rates		= FSI_RATES,
			.formats	= FSI_FMTS,
			.channels_min	= 2,
			.channels_max	= 2,
		},
		.ops = &fsi_dai_ops,
	},
};

static struct snd_soc_platform_driver fsi_soc_platform = {
	.ops		= &fsi_pcm_ops,
	.pcm_new	= fsi_pcm_new,
};

static const struct snd_soc_component_driver fsi_soc_component = {
	.name		= "fsi",
};

/*
 *		platform function
 */
static void fsi_of_parse(char *name,
			 struct device_node *np,
			 struct sh_fsi_port_info *info,
			 struct device *dev)
{
	int i;
	char prop[128];
	unsigned long flags = 0;
	struct {
		char *name;
		unsigned int val;
	} of_parse_property[] = {
		{ "spdif-connection",		SH_FSI_FMT_SPDIF },
		{ "stream-mode-support",	SH_FSI_ENABLE_STREAM_MODE },
		{ "use-internal-clock",		SH_FSI_CLK_CPG },
	};

	for (i = 0; i < ARRAY_SIZE(of_parse_property); i++) {
		sprintf(prop, "%s,%s", name, of_parse_property[i].name);
		if (of_get_property(np, prop, NULL))
			flags |= of_parse_property[i].val;
	}
	info->flags = flags;

	dev_dbg(dev, "%s flags : %lx\n", name, info->flags);
}

static void fsi_port_info_init(struct fsi_priv *fsi,
			       struct sh_fsi_port_info *info)
{
	if (info->flags & SH_FSI_FMT_SPDIF)
		fsi->spdif = 1;

	if (info->flags & SH_FSI_CLK_CPG)
		fsi->clk_cpg = 1;

	if (info->flags & SH_FSI_ENABLE_STREAM_MODE)
		fsi->enable_stream = 1;
}

static void fsi_handler_init(struct fsi_priv *fsi,
			     struct sh_fsi_port_info *info)
{
	fsi->playback.handler	= &fsi_pio_push_handler; /* default PIO */
	fsi->playback.priv	= fsi;
	fsi->capture.handler	= &fsi_pio_pop_handler;  /* default PIO */
	fsi->capture.priv	= fsi;

	if (info->tx_id) {
		fsi->playback.dma_id  = info->tx_id;
		fsi->playback.handler = &fsi_dma_push_handler;
	}
}

static const struct fsi_core fsi1_core = {
	.ver	= 1,

	/* Interrupt */
	.int_st	= INT_ST,
	.iemsk	= IEMSK,
	.imsk	= IMSK,
};

static const struct fsi_core fsi2_core = {
	.ver	= 2,

	/* Interrupt */
	.int_st	= CPU_INT_ST,
	.iemsk	= CPU_IEMSK,
	.imsk	= CPU_IMSK,
	.a_mclk	= A_MST_CTLR,
	.b_mclk	= B_MST_CTLR,
};

static const struct of_device_id fsi_of_match[] = {
	{ .compatible = "renesas,sh_fsi",	.data = &fsi1_core},
	{ .compatible = "renesas,sh_fsi2",	.data = &fsi2_core},
	{},
};
MODULE_DEVICE_TABLE(of, fsi_of_match);

static const struct platform_device_id fsi_id_table[] = {
	{ "sh_fsi",	(kernel_ulong_t)&fsi1_core },
	{},
};
MODULE_DEVICE_TABLE(platform, fsi_id_table);

static int fsi_probe(struct platform_device *pdev)
{
	struct fsi_master *master;
	struct device_node *np = pdev->dev.of_node;
	struct sh_fsi_platform_info info;
	const struct fsi_core *core;
	struct fsi_priv *fsi;
	struct resource *res;
	unsigned int irq;
	int ret;

	memset(&info, 0, sizeof(info));

	core = NULL;
	if (np) {
		const struct of_device_id *of_id;

		of_id = of_match_device(fsi_of_match, &pdev->dev);
		if (of_id) {
			core = of_id->data;
			fsi_of_parse("fsia", np, &info.port_a, &pdev->dev);
			fsi_of_parse("fsib", np, &info.port_b, &pdev->dev);
		}
	} else {
		const struct platform_device_id	*id_entry = pdev->id_entry;
		if (id_entry)
			core = (struct fsi_core *)id_entry->driver_data;

		if (pdev->dev.platform_data)
			memcpy(&info, pdev->dev.platform_data, sizeof(info));
	}

	if (!core) {
		dev_err(&pdev->dev, "unknown fsi device\n");
		return -ENODEV;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (!res || (int)irq <= 0) {
		dev_err(&pdev->dev, "Not enough FSI platform resources.\n");
		return -ENODEV;
	}

	master = devm_kzalloc(&pdev->dev, sizeof(*master), GFP_KERNEL);
	if (!master) {
		dev_err(&pdev->dev, "Could not allocate master\n");
		return -ENOMEM;
	}

	master->base = devm_ioremap_nocache(&pdev->dev,
					    res->start, resource_size(res));
	if (!master->base) {
		dev_err(&pdev->dev, "Unable to ioremap FSI registers.\n");
		return -ENXIO;
	}

	/* master setting */
	master->core		= core;
	spin_lock_init(&master->lock);

	/* FSI A setting */
	fsi		= &master->fsia;
	fsi->base	= master->base;
	fsi->phys	= res->start;
	fsi->master	= master;
	fsi_port_info_init(fsi, &info.port_a);
	fsi_handler_init(fsi, &info.port_a);
	ret = fsi_stream_probe(fsi, &pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "FSIA stream probe failed\n");
		return ret;
	}

	/* FSI B setting */
	fsi		= &master->fsib;
	fsi->base	= master->base + 0x40;
	fsi->phys	= res->start + 0x40;
	fsi->master	= master;
	fsi_port_info_init(fsi, &info.port_b);
	fsi_handler_init(fsi, &info.port_b);
	ret = fsi_stream_probe(fsi, &pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "FSIB stream probe failed\n");
		goto exit_fsia;
	}

	pm_runtime_enable(&pdev->dev);
	dev_set_drvdata(&pdev->dev, master);

	ret = devm_request_irq(&pdev->dev, irq, &fsi_interrupt, 0,
			       dev_name(&pdev->dev), master);
	if (ret) {
		dev_err(&pdev->dev, "irq request err\n");
		goto exit_fsib;
	}

	ret = snd_soc_register_platform(&pdev->dev, &fsi_soc_platform);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot snd soc register\n");
		goto exit_fsib;
	}

	ret = snd_soc_register_component(&pdev->dev, &fsi_soc_component,
				    fsi_soc_dai, ARRAY_SIZE(fsi_soc_dai));
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot snd component register\n");
		goto exit_snd_soc;
	}

	return ret;

exit_snd_soc:
	snd_soc_unregister_platform(&pdev->dev);
exit_fsib:
	pm_runtime_disable(&pdev->dev);
	fsi_stream_remove(&master->fsib);
exit_fsia:
	fsi_stream_remove(&master->fsia);

	return ret;
}

static int fsi_remove(struct platform_device *pdev)
{
	struct fsi_master *master;

	master = dev_get_drvdata(&pdev->dev);

	pm_runtime_disable(&pdev->dev);

	snd_soc_unregister_component(&pdev->dev);
	snd_soc_unregister_platform(&pdev->dev);

	fsi_stream_remove(&master->fsia);
	fsi_stream_remove(&master->fsib);

	return 0;
}

static void __fsi_suspend(struct fsi_priv *fsi,
			  struct fsi_stream *io,
			  struct device *dev)
{
	if (!fsi_stream_is_working(fsi, io))
		return;

	fsi_stream_stop(fsi, io);
	fsi_hw_shutdown(fsi, dev);
}

static void __fsi_resume(struct fsi_priv *fsi,
			 struct fsi_stream *io,
			 struct device *dev)
{
	if (!fsi_stream_is_working(fsi, io))
		return;

	fsi_hw_startup(fsi, io, dev);
	fsi_stream_start(fsi, io);
}

static int fsi_suspend(struct device *dev)
{
	struct fsi_master *master = dev_get_drvdata(dev);
	struct fsi_priv *fsia = &master->fsia;
	struct fsi_priv *fsib = &master->fsib;

	__fsi_suspend(fsia, &fsia->playback, dev);
	__fsi_suspend(fsia, &fsia->capture, dev);

	__fsi_suspend(fsib, &fsib->playback, dev);
	__fsi_suspend(fsib, &fsib->capture, dev);

	return 0;
}

static int fsi_resume(struct device *dev)
{
	struct fsi_master *master = dev_get_drvdata(dev);
	struct fsi_priv *fsia = &master->fsia;
	struct fsi_priv *fsib = &master->fsib;

	__fsi_resume(fsia, &fsia->playback, dev);
	__fsi_resume(fsia, &fsia->capture, dev);

	__fsi_resume(fsib, &fsib->playback, dev);
	__fsi_resume(fsib, &fsib->capture, dev);

	return 0;
}

static struct dev_pm_ops fsi_pm_ops = {
	.suspend		= fsi_suspend,
	.resume			= fsi_resume,
};

static struct platform_driver fsi_driver = {
	.driver 	= {
		.name	= "fsi-pcm-audio",
		.pm	= &fsi_pm_ops,
		.of_match_table = fsi_of_match,
	},
	.probe		= fsi_probe,
	.remove		= fsi_remove,
	.id_table	= fsi_id_table,
};

module_platform_driver(fsi_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("SuperH onchip FSI audio driver");
MODULE_AUTHOR("Kuninori Morimoto <morimoto.kuninori@renesas.com>");
MODULE_ALIAS("platform:fsi-pcm-audio");
