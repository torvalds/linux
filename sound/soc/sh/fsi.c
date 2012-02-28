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
#include <linux/pm_runtime.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <sound/soc.h>
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

typedef int (*set_rate_func)(struct device *dev, int is_porta, int rate, int enable);

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
 *		struct
 */

struct fsi_stream {
	struct snd_pcm_substream *substream;

	int fifo_sample_capa;	/* sample capacity of FSI FIFO */
	int buff_sample_capa;	/* sample capacity of ALSA buffer */
	int buff_sample_pos;	/* sample position of ALSA buffer */
	int period_samples;	/* sample number / 1 period */
	int period_pos;		/* current period position */

	int uerr_num;
	int oerr_num;
};

struct fsi_priv {
	void __iomem *base;
	struct fsi_master *master;

	struct fsi_stream playback;
	struct fsi_stream capture;

	u32 do_fmt;
	u32 di_fmt;

	int chan_num:16;
	int clk_master:1;
	int spdif:1;

	long rate;
};

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
	int irq;
	struct fsi_priv fsia;
	struct fsi_priv fsib;
	struct fsi_core *core;
	struct sh_fsi_platform_info *info;
	spinlock_t lock;
};

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

static set_rate_func fsi_get_info_set_rate(struct fsi_master *master)
{
	if (!master->info)
		return NULL;

	return master->info->set_rate;
}

static u32 fsi_get_info_flags(struct fsi_priv *fsi)
{
	int is_porta = fsi_is_port_a(fsi);
	struct fsi_master *master = fsi_get_master(fsi);

	if (!master->info)
		return 0;

	return is_porta ? master->info->porta_flags :
		master->info->portb_flags;
}

static inline int fsi_stream_is_play(int stream)
{
	return stream == SNDRV_PCM_STREAM_PLAYBACK;
}

static inline int fsi_is_play(struct snd_pcm_substream *substream)
{
	return fsi_stream_is_play(substream->stream);
}

static inline struct fsi_stream *fsi_get_stream(struct fsi_priv *fsi,
						int is_play)
{
	return is_play ? &fsi->playback : &fsi->capture;
}

static u32 fsi_get_port_shift(struct fsi_priv *fsi, int is_play)
{
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

static int fsi_stream_is_working(struct fsi_priv *fsi,
				  int is_play)
{
	struct fsi_stream *io = fsi_get_stream(fsi, is_play);
	struct fsi_master *master = fsi_get_master(fsi);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&master->lock, flags);
	ret = !!io->substream;
	spin_unlock_irqrestore(&master->lock, flags);

	return ret;
}

static void fsi_stream_push(struct fsi_priv *fsi,
			    int is_play,
			    struct snd_pcm_substream *substream)
{
	struct fsi_stream *io = fsi_get_stream(fsi, is_play);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct fsi_master *master = fsi_get_master(fsi);
	unsigned long flags;

	spin_lock_irqsave(&master->lock, flags);
	io->substream	= substream;
	io->buff_sample_capa	= fsi_frame2sample(fsi, runtime->buffer_size);
	io->buff_sample_pos	= 0;
	io->period_samples	= fsi_frame2sample(fsi, runtime->period_size);
	io->period_pos		= 0;
	io->oerr_num	= -1; /* ignore 1st err */
	io->uerr_num	= -1; /* ignore 1st err */
	spin_unlock_irqrestore(&master->lock, flags);
}

static void fsi_stream_pop(struct fsi_priv *fsi, int is_play)
{
	struct fsi_stream *io = fsi_get_stream(fsi, is_play);
	struct snd_soc_dai *dai = fsi_get_dai(io->substream);
	struct fsi_master *master = fsi_get_master(fsi);
	unsigned long flags;

	spin_lock_irqsave(&master->lock, flags);

	if (io->oerr_num > 0)
		dev_err(dai->dev, "over_run = %d\n", io->oerr_num);

	if (io->uerr_num > 0)
		dev_err(dai->dev, "under_run = %d\n", io->uerr_num);

	io->substream	= NULL;
	io->buff_sample_capa	= 0;
	io->buff_sample_pos	= 0;
	io->period_samples	= 0;
	io->period_pos		= 0;
	io->oerr_num	= 0;
	io->uerr_num	= 0;
	spin_unlock_irqrestore(&master->lock, flags);
}

static int fsi_get_current_fifo_samples(struct fsi_priv *fsi, int is_play)
{
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
 *		dma function
 */

static u8 *fsi_dma_get_area(struct fsi_priv *fsi, int stream)
{
	int is_play = fsi_stream_is_play(stream);
	struct fsi_stream *io = fsi_get_stream(fsi, is_play);
	struct snd_pcm_runtime *runtime = io->substream->runtime;

	return runtime->dma_area +
		samples_to_bytes(runtime, io->buff_sample_pos);
}

static void fsi_dma_soft_push16(struct fsi_priv *fsi, int num)
{
	u16 *start;
	int i;

	start  = (u16 *)fsi_dma_get_area(fsi, SNDRV_PCM_STREAM_PLAYBACK);

	for (i = 0; i < num; i++)
		fsi_reg_write(fsi, DODT, ((u32)*(start + i) << 8));
}

static void fsi_dma_soft_pop16(struct fsi_priv *fsi, int num)
{
	u16 *start;
	int i;

	start  = (u16 *)fsi_dma_get_area(fsi, SNDRV_PCM_STREAM_CAPTURE);


	for (i = 0; i < num; i++)
		*(start + i) = (u16)(fsi_reg_read(fsi, DIDT) >> 8);
}

static void fsi_dma_soft_push32(struct fsi_priv *fsi, int num)
{
	u32 *start;
	int i;

	start  = (u32 *)fsi_dma_get_area(fsi, SNDRV_PCM_STREAM_PLAYBACK);


	for (i = 0; i < num; i++)
		fsi_reg_write(fsi, DODT, *(start + i));
}

static void fsi_dma_soft_pop32(struct fsi_priv *fsi, int num)
{
	u32 *start;
	int i;

	start  = (u32 *)fsi_dma_get_area(fsi, SNDRV_PCM_STREAM_CAPTURE);

	for (i = 0; i < num; i++)
		*(start + i) = fsi_reg_read(fsi, DIDT);
}

/*
 *		irq function
 */

static void fsi_irq_enable(struct fsi_priv *fsi, int is_play)
{
	u32 data = AB_IO(1, fsi_get_port_shift(fsi, is_play));
	struct fsi_master *master = fsi_get_master(fsi);

	fsi_core_mask_set(master, imsk,  data, data);
	fsi_core_mask_set(master, iemsk, data, data);
}

static void fsi_irq_disable(struct fsi_priv *fsi, int is_play)
{
	u32 data = AB_IO(1, fsi_get_port_shift(fsi, is_play));
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

	data |= AB_IO(1, fsi_get_port_shift(fsi, 0));
	data |= AB_IO(1, fsi_get_port_shift(fsi, 1));

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

	if (master->core->ver < 2) {
		pr_err("fsi: register access err (%s)\n", __func__);
		return;
	}

	mask = BP | SE;
	val = enable ? mask : 0;

	fsi_is_port_a(fsi) ?
		fsi_core_mask_set(master, a_mclk, mask, val) :
		fsi_core_mask_set(master, b_mclk, mask, val);
}

/*
 *		clock function
 */
static int fsi_set_master_clk(struct device *dev, struct fsi_priv *fsi,
			      long rate, int enable)
{
	struct fsi_master *master = fsi_get_master(fsi);
	set_rate_func set_rate = fsi_get_info_set_rate(master);
	int fsi_ver = master->core->ver;
	int ret;

	ret = set_rate(dev, fsi_is_port_a(fsi), rate, enable);
	if (ret < 0) /* error */
		return ret;

	if (!enable)
		return 0;

	if (ret > 0) {
		u32 data = 0;

		switch (ret & SH_FSI_ACKMD_MASK) {
		default:
			/* FALL THROUGH */
		case SH_FSI_ACKMD_512:
			data |= (0x0 << 12);
			break;
		case SH_FSI_ACKMD_256:
			data |= (0x1 << 12);
			break;
		case SH_FSI_ACKMD_128:
			data |= (0x2 << 12);
			break;
		case SH_FSI_ACKMD_64:
			data |= (0x3 << 12);
			break;
		case SH_FSI_ACKMD_32:
			if (fsi_ver < 2)
				dev_err(dev, "unsupported ACKMD\n");
			else
				data |= (0x4 << 12);
			break;
		}

		switch (ret & SH_FSI_BPFMD_MASK) {
		default:
			/* FALL THROUGH */
		case SH_FSI_BPFMD_32:
			data |= (0x0 << 8);
			break;
		case SH_FSI_BPFMD_64:
			data |= (0x1 << 8);
			break;
		case SH_FSI_BPFMD_128:
			data |= (0x2 << 8);
			break;
		case SH_FSI_BPFMD_256:
			data |= (0x3 << 8);
			break;
		case SH_FSI_BPFMD_512:
			data |= (0x4 << 8);
			break;
		case SH_FSI_BPFMD_16:
			if (fsi_ver < 2)
				dev_err(dev, "unsupported ACKMD\n");
			else
				data |= (0x7 << 8);
			break;
		}

		fsi_reg_mask_set(fsi, CKG1, (ACKMD_MASK | BPFMD_MASK) , data);
		udelay(10);
		ret = 0;
	}

	return ret;
}

#define fsi_port_start(f, i)	__fsi_port_clk_ctrl(f, i, 1)
#define fsi_port_stop(f, i)	__fsi_port_clk_ctrl(f, i, 0)
static void __fsi_port_clk_ctrl(struct fsi_priv *fsi, int is_play, int enable)
{
	struct fsi_master *master = fsi_get_master(fsi);
	u32 clk  = fsi_is_port_a(fsi) ? CRA  : CRB;

	if (enable)
		fsi_irq_enable(fsi, is_play);
	else
		fsi_irq_disable(fsi, is_play);

	if (fsi_is_clk_master(fsi))
		fsi_master_mask_set(master, CLK_RST, clk, (enable) ? clk : 0);
}

/*
 *		ctrl function
 */
static void fsi_fifo_init(struct fsi_priv *fsi,
			  int is_play,
			  struct device *dev)
{
	struct fsi_master *master = fsi_get_master(fsi);
	struct fsi_stream *io = fsi_get_stream(fsi, is_play);
	u32 shift, i;
	int frame_capa;

	/* get on-chip RAM capacity */
	shift = fsi_master_read(master, FIFO_SZ);
	shift >>= fsi_get_port_shift(fsi, is_play);
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

static int fsi_fifo_data_ctrl(struct fsi_priv *fsi, int stream)
{
	struct snd_pcm_runtime *runtime;
	struct snd_pcm_substream *substream = NULL;
	int is_play = fsi_stream_is_play(stream);
	struct fsi_stream *io = fsi_get_stream(fsi, is_play);
	int sample_residues;
	int sample_width;
	int samples;
	int samples_max;
	int over_period;
	void (*fn)(struct fsi_priv *fsi, int size);

	if (!fsi			||
	    !io->substream		||
	    !io->substream->runtime)
		return -EINVAL;

	over_period	= 0;
	substream	= io->substream;
	runtime		= substream->runtime;

	/* FSI FIFO has limit.
	 * So, this driver can not send periods data at a time
	 */
	if (io->buff_sample_pos >=
	    io->period_samples * (io->period_pos + 1)) {

		over_period = 1;
		io->period_pos = (io->period_pos + 1) % runtime->periods;

		if (0 == io->period_pos)
			io->buff_sample_pos = 0;
	}

	/* get 1 sample data width */
	sample_width = samples_to_bytes(runtime, 1);

	/* get number of residue samples */
	sample_residues = io->buff_sample_capa - io->buff_sample_pos;

	if (is_play) {
		/*
		 * for play-back
		 *
		 * samples_max	: number of FSI fifo free samples space
		 * samples	: number of ALSA residue samples
		 */
		samples_max  = io->fifo_sample_capa;
		samples_max -= fsi_get_current_fifo_samples(fsi, is_play);

		samples = sample_residues;

		switch (sample_width) {
		case 2:
			fn = fsi_dma_soft_push16;
			break;
		case 4:
			fn = fsi_dma_soft_push32;
			break;
		default:
			return -EINVAL;
		}
	} else {
		/*
		 * for capture
		 *
		 * samples_max	: number of ALSA free samples space
		 * samples	: number of samples in FSI fifo
		 */
		samples_max = sample_residues;
		samples     = fsi_get_current_fifo_samples(fsi, is_play);

		switch (sample_width) {
		case 2:
			fn = fsi_dma_soft_pop16;
			break;
		case 4:
			fn = fsi_dma_soft_pop32;
			break;
		default:
			return -EINVAL;
		}
	}

	samples = min(samples, samples_max);

	fn(fsi, samples);

	/* update buff_sample_pos */
	io->buff_sample_pos += samples;

	if (over_period)
		snd_pcm_period_elapsed(substream);

	return 0;
}

static int fsi_data_pop(struct fsi_priv *fsi)
{
	return fsi_fifo_data_ctrl(fsi, SNDRV_PCM_STREAM_CAPTURE);
}

static int fsi_data_push(struct fsi_priv *fsi)
{
	return fsi_fifo_data_ctrl(fsi, SNDRV_PCM_STREAM_PLAYBACK);
}

static irqreturn_t fsi_interrupt(int irq, void *data)
{
	struct fsi_master *master = data;
	u32 int_st = fsi_irq_get_status(master);

	/* clear irq status */
	fsi_master_mask_set(master, SOFT_RST, IR, 0);
	fsi_master_mask_set(master, SOFT_RST, IR, IR);

	if (int_st & AB_IO(1, AO_SHIFT))
		fsi_data_push(&master->fsia);
	if (int_st & AB_IO(1, BO_SHIFT))
		fsi_data_push(&master->fsib);
	if (int_st & AB_IO(1, AI_SHIFT))
		fsi_data_pop(&master->fsia);
	if (int_st & AB_IO(1, BI_SHIFT))
		fsi_data_pop(&master->fsib);

	fsi_count_fifo_err(&master->fsia);
	fsi_count_fifo_err(&master->fsib);

	fsi_irq_clear_status(&master->fsia);
	fsi_irq_clear_status(&master->fsib);

	return IRQ_HANDLED;
}

/*
 *		dai ops
 */

static int fsi_hw_startup(struct fsi_priv *fsi,
			  int is_play,
			  struct device *dev)
{
	struct fsi_master *master = fsi_get_master(fsi);
	int fsi_ver = master->core->ver;
	u32 flags = fsi_get_info_flags(fsi);
	u32 data = 0;

	/* clock setting */
	if (fsi_is_clk_master(fsi))
		data = DIMD | DOMD;

	fsi_reg_mask_set(fsi, CKG1, (DIMD | DOMD), data);

	/* clock inversion (CKG2) */
	data = 0;
	if (SH_FSI_LRM_INV & flags)
		data |= 1 << 12;
	if (SH_FSI_BRM_INV & flags)
		data |= 1 << 8;
	if (SH_FSI_LRS_INV & flags)
		data |= 1 << 4;
	if (SH_FSI_BRS_INV & flags)
		data |= 1 << 0;

	fsi_reg_write(fsi, CKG2, data);

	/* set format */
	fsi_reg_write(fsi, DO_FMT, fsi->do_fmt);
	fsi_reg_write(fsi, DI_FMT, fsi->di_fmt);

	/* spdif ? */
	if (fsi_is_spdif(fsi)) {
		fsi_spdif_clk_ctrl(fsi, 1);
		fsi_reg_mask_set(fsi, OUT_SEL, DMMD, DMMD);
	}

	/*
	 * FIXME
	 *
	 * FSI driver assumed that data package is in-back.
	 * FSI2 chip can select it.
	 */
	if (fsi_ver >= 2) {
		fsi_reg_write(fsi, OUT_DMAC,	(1 << 4));
		fsi_reg_write(fsi, IN_DMAC,	(1 << 4));
	}

	/* irq clear */
	fsi_irq_disable(fsi, is_play);
	fsi_irq_clear_status(fsi);

	/* fifo init */
	fsi_fifo_init(fsi, is_play, dev);

	return 0;
}

static void fsi_hw_shutdown(struct fsi_priv *fsi,
			    int is_play,
			    struct device *dev)
{
	if (fsi_is_clk_master(fsi))
		fsi_set_master_clk(dev, fsi, fsi->rate, 0);
}

static int fsi_dai_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	struct fsi_priv *fsi = fsi_get_priv(substream);
	int is_play = fsi_is_play(substream);

	return fsi_hw_startup(fsi, is_play, dai->dev);
}

static void fsi_dai_shutdown(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct fsi_priv *fsi = fsi_get_priv(substream);
	int is_play = fsi_is_play(substream);

	fsi_hw_shutdown(fsi, is_play, dai->dev);
	fsi->rate = 0;
}

static int fsi_dai_trigger(struct snd_pcm_substream *substream, int cmd,
			   struct snd_soc_dai *dai)
{
	struct fsi_priv *fsi = fsi_get_priv(substream);
	int is_play = fsi_is_play(substream);
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		fsi_stream_push(fsi, is_play, substream);
		ret = is_play ? fsi_data_push(fsi) : fsi_data_pop(fsi);
		fsi_port_start(fsi, is_play);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		fsi_port_stop(fsi, is_play);
		fsi_stream_pop(fsi, is_play);
		break;
	}

	return ret;
}

static int fsi_set_fmt_dai(struct fsi_priv *fsi, unsigned int fmt)
{
	u32 data = 0;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		data = CR_I2S;
		fsi->chan_num = 2;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		data = CR_PCM;
		fsi->chan_num = 2;
		break;
	default:
		return -EINVAL;
	}

	fsi->do_fmt = data;
	fsi->di_fmt = data;

	return 0;
}

static int fsi_set_fmt_spdif(struct fsi_priv *fsi)
{
	struct fsi_master *master = fsi_get_master(fsi);
	u32 data = 0;

	if (master->core->ver < 2)
		return -EINVAL;

	data = CR_BWS_16 | CR_DTMD_SPDIF_PCM | CR_PCM;
	fsi->chan_num = 2;
	fsi->spdif = 1;

	fsi->do_fmt = data;
	fsi->di_fmt = data;

	return 0;
}

static int fsi_dai_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct fsi_priv *fsi = fsi_get_priv_frm_dai(dai);
	struct fsi_master *master = fsi_get_master(fsi);
	set_rate_func set_rate = fsi_get_info_set_rate(master);
	u32 flags = fsi_get_info_flags(fsi);
	int ret;

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		fsi->clk_master = 1;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	if (fsi_is_clk_master(fsi) && !set_rate) {
		dev_err(dai->dev, "platform doesn't have set_rate\n");
		return -EINVAL;
	}

	/* set format */
	switch (flags & SH_FSI_FMT_MASK) {
	case SH_FSI_FMT_DAI:
		ret = fsi_set_fmt_dai(fsi, fmt & SND_SOC_DAIFMT_FORMAT_MASK);
		break;
	case SH_FSI_FMT_SPDIF:
		ret = fsi_set_fmt_spdif(fsi);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int fsi_dai_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct fsi_priv *fsi = fsi_get_priv(substream);
	long rate = params_rate(params);
	int ret;

	if (!fsi_is_clk_master(fsi))
		return 0;

	ret = fsi_set_master_clk(dai->dev, fsi, rate, 1);
	if (ret < 0)
		return ret;

	fsi->rate = rate;

	return ret;
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
			SNDRV_PCM_INFO_MMAP_VALID	|
			SNDRV_PCM_INFO_PAUSE,
	.formats		= FSI_FMTS,
	.rates			= FSI_RATES,
	.rate_min		= 8000,
	.rate_max		= 192000,
	.channels_min		= 1,
	.channels_max		= 2,
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
	struct fsi_stream *io = fsi_get_stream(fsi, fsi_is_play(substream));

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

static void fsi_pcm_free(struct snd_pcm *pcm)
{
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static int fsi_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_pcm *pcm = rtd->pcm;

	/*
	 * dont use SNDRV_DMA_TYPE_DEV, since it will oops the SH kernel
	 * in MMAP mode (i.e. aplay -M)
	 */
	return snd_pcm_lib_preallocate_pages_for_all(
		pcm,
		SNDRV_DMA_TYPE_CONTINUOUS,
		snd_dma_continuous_data(GFP_KERNEL),
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
			.channels_min	= 1,
			.channels_max	= 8,
		},
		.capture = {
			.rates		= FSI_RATES,
			.formats	= FSI_FMTS,
			.channels_min	= 1,
			.channels_max	= 8,
		},
		.ops = &fsi_dai_ops,
	},
	{
		.name			= "fsib-dai",
		.playback = {
			.rates		= FSI_RATES,
			.formats	= FSI_FMTS,
			.channels_min	= 1,
			.channels_max	= 8,
		},
		.capture = {
			.rates		= FSI_RATES,
			.formats	= FSI_FMTS,
			.channels_min	= 1,
			.channels_max	= 8,
		},
		.ops = &fsi_dai_ops,
	},
};

static struct snd_soc_platform_driver fsi_soc_platform = {
	.ops		= &fsi_pcm_ops,
	.pcm_new	= fsi_pcm_new,
	.pcm_free	= fsi_pcm_free,
};

/*
 *		platform function
 */

static int fsi_probe(struct platform_device *pdev)
{
	struct fsi_master *master;
	const struct platform_device_id	*id_entry;
	struct resource *res;
	unsigned int irq;
	int ret;

	id_entry = pdev->id_entry;
	if (!id_entry) {
		dev_err(&pdev->dev, "unknown fsi device\n");
		return -ENODEV;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (!res || (int)irq <= 0) {
		dev_err(&pdev->dev, "Not enough FSI platform resources.\n");
		ret = -ENODEV;
		goto exit;
	}

	master = kzalloc(sizeof(*master), GFP_KERNEL);
	if (!master) {
		dev_err(&pdev->dev, "Could not allocate master\n");
		ret = -ENOMEM;
		goto exit;
	}

	master->base = ioremap_nocache(res->start, resource_size(res));
	if (!master->base) {
		ret = -ENXIO;
		dev_err(&pdev->dev, "Unable to ioremap FSI registers.\n");
		goto exit_kfree;
	}

	/* master setting */
	master->irq		= irq;
	master->info		= pdev->dev.platform_data;
	master->core		= (struct fsi_core *)id_entry->driver_data;
	spin_lock_init(&master->lock);

	/* FSI A setting */
	master->fsia.base	= master->base;
	master->fsia.master	= master;

	/* FSI B setting */
	master->fsib.base	= master->base + 0x40;
	master->fsib.master	= master;

	pm_runtime_enable(&pdev->dev);
	dev_set_drvdata(&pdev->dev, master);

	ret = request_irq(irq, &fsi_interrupt, 0,
			  id_entry->name, master);
	if (ret) {
		dev_err(&pdev->dev, "irq request err\n");
		goto exit_iounmap;
	}

	ret = snd_soc_register_platform(&pdev->dev, &fsi_soc_platform);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot snd soc register\n");
		goto exit_free_irq;
	}

	ret = snd_soc_register_dais(&pdev->dev, fsi_soc_dai,
				    ARRAY_SIZE(fsi_soc_dai));
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot snd dai register\n");
		goto exit_snd_soc;
	}

	return ret;

exit_snd_soc:
	snd_soc_unregister_platform(&pdev->dev);
exit_free_irq:
	free_irq(irq, master);
exit_iounmap:
	iounmap(master->base);
	pm_runtime_disable(&pdev->dev);
exit_kfree:
	kfree(master);
	master = NULL;
exit:
	return ret;
}

static int fsi_remove(struct platform_device *pdev)
{
	struct fsi_master *master;

	master = dev_get_drvdata(&pdev->dev);

	free_irq(master->irq, master);
	pm_runtime_disable(&pdev->dev);

	snd_soc_unregister_dais(&pdev->dev, ARRAY_SIZE(fsi_soc_dai));
	snd_soc_unregister_platform(&pdev->dev);

	iounmap(master->base);
	kfree(master);

	return 0;
}

static void __fsi_suspend(struct fsi_priv *fsi,
			  int is_play,
			  struct device *dev)
{
	if (!fsi_stream_is_working(fsi, is_play))
		return;

	fsi_port_stop(fsi, is_play);
	fsi_hw_shutdown(fsi, is_play, dev);
}

static void __fsi_resume(struct fsi_priv *fsi,
			 int is_play,
			 struct device *dev)
{
	if (!fsi_stream_is_working(fsi, is_play))
		return;

	fsi_hw_startup(fsi, is_play, dev);

	if (fsi_is_clk_master(fsi) && fsi->rate)
		fsi_set_master_clk(dev, fsi, fsi->rate, 1);

	fsi_port_start(fsi, is_play);

}

static int fsi_suspend(struct device *dev)
{
	struct fsi_master *master = dev_get_drvdata(dev);
	struct fsi_priv *fsia = &master->fsia;
	struct fsi_priv *fsib = &master->fsib;

	__fsi_suspend(fsia, 1, dev);
	__fsi_suspend(fsia, 0, dev);

	__fsi_suspend(fsib, 1, dev);
	__fsi_suspend(fsib, 0, dev);

	return 0;
}

static int fsi_resume(struct device *dev)
{
	struct fsi_master *master = dev_get_drvdata(dev);
	struct fsi_priv *fsia = &master->fsia;
	struct fsi_priv *fsib = &master->fsib;

	__fsi_resume(fsia, 1, dev);
	__fsi_resume(fsia, 0, dev);

	__fsi_resume(fsib, 1, dev);
	__fsi_resume(fsib, 0, dev);

	return 0;
}

static int fsi_runtime_nop(struct device *dev)
{
	/* Runtime PM callback shared between ->runtime_suspend()
	 * and ->runtime_resume(). Simply returns success.
	 *
	 * This driver re-initializes all registers after
	 * pm_runtime_get_sync() anyway so there is no need
	 * to save and restore registers here.
	 */
	return 0;
}

static struct dev_pm_ops fsi_pm_ops = {
	.suspend		= fsi_suspend,
	.resume			= fsi_resume,
	.runtime_suspend	= fsi_runtime_nop,
	.runtime_resume		= fsi_runtime_nop,
};

static struct fsi_core fsi1_core = {
	.ver	= 1,

	/* Interrupt */
	.int_st	= INT_ST,
	.iemsk	= IEMSK,
	.imsk	= IMSK,
};

static struct fsi_core fsi2_core = {
	.ver	= 2,

	/* Interrupt */
	.int_st	= CPU_INT_ST,
	.iemsk	= CPU_IEMSK,
	.imsk	= CPU_IMSK,
	.a_mclk	= A_MST_CTLR,
	.b_mclk	= B_MST_CTLR,
};

static struct platform_device_id fsi_id_table[] = {
	{ "sh_fsi",	(kernel_ulong_t)&fsi1_core },
	{ "sh_fsi2",	(kernel_ulong_t)&fsi2_core },
	{},
};
MODULE_DEVICE_TABLE(platform, fsi_id_table);

static struct platform_driver fsi_driver = {
	.driver 	= {
		.name	= "fsi-pcm-audio",
		.pm	= &fsi_pm_ops,
	},
	.probe		= fsi_probe,
	.remove		= fsi_remove,
	.id_table	= fsi_id_table,
};

module_platform_driver(fsi_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SuperH onchip FSI audio driver");
MODULE_AUTHOR("Kuninori Morimoto <morimoto.kuninori@renesas.com>");
MODULE_ALIAS("platform:fsi-pcm-audio");
