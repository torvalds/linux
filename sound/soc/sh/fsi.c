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
#include <sound/soc.h>
#include <sound/sh_fsi.h>

#define DO_FMT		0x0000
#define DOFF_CTL	0x0004
#define DOFF_ST		0x0008
#define DI_FMT		0x000C
#define DIFF_CTL	0x0010
#define DIFF_ST		0x0014
#define CKG1		0x0018
#define CKG2		0x001C
#define DIDT		0x0020
#define DODT		0x0024
#define MUTE_ST		0x0028
#define OUT_SEL		0x0030
#define REG_END		OUT_SEL

#define A_MST_CTLR	0x0180
#define B_MST_CTLR	0x01A0
#define CPU_INT_ST	0x01F4
#define CPU_IEMSK	0x01F8
#define CPU_IMSK	0x01FC
#define INT_ST		0x0200
#define IEMSK		0x0204
#define IMSK		0x0208
#define MUTE		0x020C
#define CLK_RST		0x0210
#define SOFT_RST	0x0214
#define FIFO_SZ		0x0218
#define MREG_START	A_MST_CTLR
#define MREG_END	FIFO_SZ

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

/* A/B MST_CTLR */
#define BP	(1 << 4)	/* Fix the signal of Biphase output */
#define SE	(1 << 0)	/* Fix the master clock */

/* CLK_RST */
#define B_CLK		0x00000010
#define A_CLK		0x00000001

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
 * FSI driver use below type name for variable
 *
 * xxx_len	: data length
 * xxx_width	: data width
 * xxx_offset	: data offset
 * xxx_num	: number of data
 */

/*
 *		struct
 */

struct fsi_stream {
	struct snd_pcm_substream *substream;

	int fifo_max_num;
	int chan_num;

	int buff_offset;
	int buff_len;
	int period_len;
	int period_num;
};

struct fsi_priv {
	void __iomem *base;
	struct fsi_master *master;

	struct fsi_stream playback;
	struct fsi_stream capture;

	u32 mst_ctrl;
};

struct fsi_core {
	int ver;

	u32 int_st;
	u32 iemsk;
	u32 imsk;
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

static void __fsi_reg_write(u32 reg, u32 data)
{
	/* valid data area is 24bit */
	data &= 0x00ffffff;

	__raw_writel(data, reg);
}

static u32 __fsi_reg_read(u32 reg)
{
	return __raw_readl(reg);
}

static void __fsi_reg_mask_set(u32 reg, u32 mask, u32 data)
{
	u32 val = __fsi_reg_read(reg);

	val &= ~mask;
	val |= data & mask;

	__fsi_reg_write(reg, val);
}

static void fsi_reg_write(struct fsi_priv *fsi, u32 reg, u32 data)
{
	if (reg > REG_END) {
		pr_err("fsi: register access err (%s)\n", __func__);
		return;
	}

	__fsi_reg_write((u32)(fsi->base + reg), data);
}

static u32 fsi_reg_read(struct fsi_priv *fsi, u32 reg)
{
	if (reg > REG_END) {
		pr_err("fsi: register access err (%s)\n", __func__);
		return 0;
	}

	return __fsi_reg_read((u32)(fsi->base + reg));
}

static void fsi_reg_mask_set(struct fsi_priv *fsi, u32 reg, u32 mask, u32 data)
{
	if (reg > REG_END) {
		pr_err("fsi: register access err (%s)\n", __func__);
		return;
	}

	__fsi_reg_mask_set((u32)(fsi->base + reg), mask, data);
}

static u32 fsi_master_read(struct fsi_master *master, u32 reg)
{
	u32 ret;
	unsigned long flags;

	if ((reg < MREG_START) ||
	    (reg > MREG_END)) {
		pr_err("fsi: register access err (%s)\n", __func__);
		return 0;
	}

	spin_lock_irqsave(&master->lock, flags);
	ret = __fsi_reg_read((u32)(master->base + reg));
	spin_unlock_irqrestore(&master->lock, flags);

	return ret;
}

static void fsi_master_mask_set(struct fsi_master *master,
			       u32 reg, u32 mask, u32 data)
{
	unsigned long flags;

	if ((reg < MREG_START) ||
	    (reg > MREG_END)) {
		pr_err("fsi: register access err (%s)\n", __func__);
		return;
	}

	spin_lock_irqsave(&master->lock, flags);
	__fsi_reg_mask_set((u32)(master->base + reg), mask, data);
	spin_unlock_irqrestore(&master->lock, flags);
}

/*
 *		basic function
 */

static struct fsi_master *fsi_get_master(struct fsi_priv *fsi)
{
	return fsi->master;
}

static int fsi_is_port_a(struct fsi_priv *fsi)
{
	return fsi->master->base == fsi->base;
}

static struct snd_soc_dai *fsi_get_dai(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;

	return  rtd->cpu_dai;
}

static struct fsi_priv *fsi_get_priv(struct snd_pcm_substream *substream)
{
	struct snd_soc_dai *dai = fsi_get_dai(substream);
	struct fsi_master *master = snd_soc_dai_get_drvdata(dai);

	if (dai->id == 0)
		return &master->fsia;
	else
		return &master->fsib;
}

static u32 fsi_get_info_flags(struct fsi_priv *fsi)
{
	int is_porta = fsi_is_port_a(fsi);
	struct fsi_master *master = fsi_get_master(fsi);

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

static int fsi_is_master_mode(struct fsi_priv *fsi, int is_play)
{
	u32 mode;
	u32 flags = fsi_get_info_flags(fsi);

	mode = is_play ? SH_FSI_OUT_SLAVE_MODE : SH_FSI_IN_SLAVE_MODE;

	/* return
	 * 1 : master mode
	 * 0 : slave mode
	 */

	return (mode & flags) != mode;
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

static void fsi_stream_push(struct fsi_priv *fsi,
			    int is_play,
			    struct snd_pcm_substream *substream,
			    u32 buffer_len,
			    u32 period_len)
{
	struct fsi_stream *io = fsi_get_stream(fsi, is_play);

	io->substream	= substream;
	io->buff_len	= buffer_len;
	io->buff_offset	= 0;
	io->period_len	= period_len;
	io->period_num	= 0;
}

static void fsi_stream_pop(struct fsi_priv *fsi, int is_play)
{
	struct fsi_stream *io = fsi_get_stream(fsi, is_play);

	io->substream	= NULL;
	io->buff_len	= 0;
	io->buff_offset	= 0;
	io->period_len	= 0;
	io->period_num	= 0;
}

static int fsi_get_fifo_data_num(struct fsi_priv *fsi, int is_play)
{
	u32 status;
	u32 reg = is_play ? DOFF_ST : DIFF_ST;
	struct fsi_stream *io = fsi_get_stream(fsi, is_play);
	int data_num;

	status = fsi_reg_read(fsi, reg);
	data_num = 0x1ff & (status >> 8);
	data_num *= io->chan_num;

	return data_num;
}

static int fsi_len2num(int len, int width)
{
	return len / width;
}

#define fsi_num2offset(a, b) fsi_num2len(a, b)
static int fsi_num2len(int num, int width)
{
	return num * width;
}

static int fsi_get_frame_width(struct fsi_priv *fsi, int is_play)
{
	struct fsi_stream *io = fsi_get_stream(fsi, is_play);
	struct snd_pcm_substream *substream = io->substream;
	struct snd_pcm_runtime *runtime = substream->runtime;

	return frames_to_bytes(runtime, 1) / io->chan_num;
}

/*
 *		dma function
 */

static u8 *fsi_dma_get_area(struct fsi_priv *fsi, int stream)
{
	int is_play = fsi_stream_is_play(stream);
	struct fsi_stream *io = fsi_get_stream(fsi, is_play);

	return io->substream->runtime->dma_area + io->buff_offset;
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

	fsi_master_mask_set(master, master->core->imsk,  data, data);
	fsi_master_mask_set(master, master->core->iemsk, data, data);
}

static void fsi_irq_disable(struct fsi_priv *fsi, int is_play)
{
	u32 data = AB_IO(1, fsi_get_port_shift(fsi, is_play));
	struct fsi_master *master = fsi_get_master(fsi);

	fsi_master_mask_set(master, master->core->imsk,  data, 0);
	fsi_master_mask_set(master, master->core->iemsk, data, 0);
}

static u32 fsi_irq_get_status(struct fsi_master *master)
{
	return fsi_master_read(master, master->core->int_st);
}

static void fsi_irq_clear_status(struct fsi_priv *fsi)
{
	u32 data = 0;
	struct fsi_master *master = fsi_get_master(fsi);

	data |= AB_IO(1, fsi_get_port_shift(fsi, 0));
	data |= AB_IO(1, fsi_get_port_shift(fsi, 1));

	/* clear interrupt factor */
	fsi_master_mask_set(master, master->core->int_st, data, 0);
}

/*
 *		SPDIF master clock function
 *
 * These functions are used later FSI2
 */
static void fsi_spdif_clk_ctrl(struct fsi_priv *fsi, int enable)
{
	struct fsi_master *master = fsi_get_master(fsi);
	u32 val = BP | SE;

	if (master->core->ver < 2) {
		pr_err("fsi: register access err (%s)\n", __func__);
		return;
	}

	if (enable)
		fsi_master_mask_set(master, fsi->mst_ctrl, val, val);
	else
		fsi_master_mask_set(master, fsi->mst_ctrl, val, 0);
}

/*
 *		ctrl function
 */

static void fsi_clk_ctrl(struct fsi_priv *fsi, int enable)
{
	u32 val = fsi_is_port_a(fsi) ? (1 << 0) : (1 << 4);
	struct fsi_master *master = fsi_get_master(fsi);

	if (enable)
		fsi_master_mask_set(master, CLK_RST, val, val);
	else
		fsi_master_mask_set(master, CLK_RST, val, 0);
}

static void fsi_fifo_init(struct fsi_priv *fsi,
			  int is_play,
			  struct snd_soc_dai *dai)
{
	struct fsi_master *master = fsi_get_master(fsi);
	struct fsi_stream *io = fsi_get_stream(fsi, is_play);
	u32 ctrl, shift, i;

	/* get on-chip RAM capacity */
	shift = fsi_master_read(master, FIFO_SZ);
	shift >>= fsi_get_port_shift(fsi, is_play);
	shift &= FIFO_SZ_MASK;
	io->fifo_max_num = 256 << shift;
	dev_dbg(dai->dev, "fifo = %d words\n", io->fifo_max_num);

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
	for (i = 1; i < io->chan_num; i <<= 1)
		io->fifo_max_num >>= 1;
	dev_dbg(dai->dev, "%d channel %d store\n",
		io->chan_num, io->fifo_max_num);

	ctrl = is_play ? DOFF_CTL : DIFF_CTL;

	/* set interrupt generation factor */
	fsi_reg_write(fsi, ctrl, IRQ_HALF);

	/* clear FIFO */
	fsi_reg_mask_set(fsi, ctrl, FIFO_CLR, FIFO_CLR);
}

static void fsi_soft_all_reset(struct fsi_master *master)
{
	/* port AB reset */
	fsi_master_mask_set(master, SOFT_RST, PASR | PBSR, 0);
	mdelay(10);

	/* soft reset */
	fsi_master_mask_set(master, SOFT_RST, FSISR, 0);
	fsi_master_mask_set(master, SOFT_RST, FSISR, FSISR);
	mdelay(10);
}

static int fsi_fifo_data_ctrl(struct fsi_priv *fsi, int startup, int stream)
{
	struct snd_pcm_runtime *runtime;
	struct snd_pcm_substream *substream = NULL;
	int is_play = fsi_stream_is_play(stream);
	struct fsi_stream *io = fsi_get_stream(fsi, is_play);
	u32 status_reg = is_play ? DOFF_ST : DIFF_ST;
	int data_residue_num;
	int data_num;
	int data_num_max;
	int ch_width;
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
	if (io->buff_offset >=
	    fsi_num2offset(io->period_num + 1, io->period_len)) {

		over_period = 1;
		io->period_num = (io->period_num + 1) % runtime->periods;

		if (0 == io->period_num)
			io->buff_offset = 0;
	}

	/* get 1 channel data width */
	ch_width = fsi_get_frame_width(fsi, is_play);

	/* get residue data number of alsa */
	data_residue_num = fsi_len2num(io->buff_len - io->buff_offset,
				       ch_width);

	if (is_play) {
		/*
		 * for play-back
		 *
		 * data_num_max	: number of FSI fifo free space
		 * data_num	: number of ALSA residue data
		 */
		data_num_max  = io->fifo_max_num * io->chan_num;
		data_num_max -= fsi_get_fifo_data_num(fsi, is_play);

		data_num = data_residue_num;

		switch (ch_width) {
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
		 * data_num_max	: number of ALSA free space
		 * data_num	: number of data in FSI fifo
		 */
		data_num_max = data_residue_num;
		data_num     = fsi_get_fifo_data_num(fsi, is_play);

		switch (ch_width) {
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

	data_num = min(data_num, data_num_max);

	fn(fsi, data_num);

	/* update buff_offset */
	io->buff_offset += fsi_num2offset(data_num, ch_width);

	/* check fifo status */
	if (!startup) {
		struct snd_soc_dai *dai = fsi_get_dai(substream);
		u32 status = fsi_reg_read(fsi, status_reg);

		if (status & ERR_OVER)
			dev_err(dai->dev, "over run\n");
		if (status & ERR_UNDER)
			dev_err(dai->dev, "under run\n");
	}
	fsi_reg_write(fsi, status_reg, 0);

	/* re-enable irq */
	fsi_irq_enable(fsi, is_play);

	if (over_period)
		snd_pcm_period_elapsed(substream);

	return 0;
}

static int fsi_data_pop(struct fsi_priv *fsi, int startup)
{
	return fsi_fifo_data_ctrl(fsi, startup, SNDRV_PCM_STREAM_CAPTURE);
}

static int fsi_data_push(struct fsi_priv *fsi, int startup)
{
	return fsi_fifo_data_ctrl(fsi, startup, SNDRV_PCM_STREAM_PLAYBACK);
}

static irqreturn_t fsi_interrupt(int irq, void *data)
{
	struct fsi_master *master = data;
	u32 int_st = fsi_irq_get_status(master);

	/* clear irq status */
	fsi_master_mask_set(master, SOFT_RST, IR, 0);
	fsi_master_mask_set(master, SOFT_RST, IR, IR);

	if (int_st & AB_IO(1, AO_SHIFT))
		fsi_data_push(&master->fsia, 0);
	if (int_st & AB_IO(1, BO_SHIFT))
		fsi_data_push(&master->fsib, 0);
	if (int_st & AB_IO(1, AI_SHIFT))
		fsi_data_pop(&master->fsia, 0);
	if (int_st & AB_IO(1, BI_SHIFT))
		fsi_data_pop(&master->fsib, 0);

	fsi_irq_clear_status(&master->fsia);
	fsi_irq_clear_status(&master->fsib);

	return IRQ_HANDLED;
}

/*
 *		dai ops
 */

static int fsi_dai_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	struct fsi_priv *fsi = fsi_get_priv(substream);
	struct fsi_master *master = fsi_get_master(fsi);
	struct fsi_stream *io;
	u32 flags = fsi_get_info_flags(fsi);
	u32 fmt;
	u32 reg;
	u32 data;
	int is_play = fsi_is_play(substream);
	int is_master;

	io = fsi_get_stream(fsi, is_play);

	pm_runtime_get_sync(dai->dev);

	/* CKG1 */
	data = is_play ? (1 << 0) : (1 << 4);
	is_master = fsi_is_master_mode(fsi, is_play);
	if (is_master)
		fsi_reg_mask_set(fsi, CKG1, data, data);
	else
		fsi_reg_mask_set(fsi, CKG1, data, 0);

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

	/* do fmt, di fmt */
	data = 0;
	reg = is_play ? DO_FMT : DI_FMT;
	fmt = is_play ? SH_FSI_GET_OFMT(flags) : SH_FSI_GET_IFMT(flags);
	switch (fmt) {
	case SH_FSI_FMT_MONO:
		data = CR_MONO;
		io->chan_num = 1;
		break;
	case SH_FSI_FMT_MONO_DELAY:
		data = CR_MONO_D;
		io->chan_num = 1;
		break;
	case SH_FSI_FMT_PCM:
		data = CR_PCM;
		io->chan_num = 2;
		break;
	case SH_FSI_FMT_I2S:
		data = CR_I2S;
		io->chan_num = 2;
		break;
	case SH_FSI_FMT_TDM:
		io->chan_num = is_play ?
			SH_FSI_GET_CH_O(flags) : SH_FSI_GET_CH_I(flags);
		data = CR_TDM | (io->chan_num - 1);
		break;
	case SH_FSI_FMT_TDM_DELAY:
		io->chan_num = is_play ?
			SH_FSI_GET_CH_O(flags) : SH_FSI_GET_CH_I(flags);
		data = CR_TDM_D | (io->chan_num - 1);
		break;
	case SH_FSI_FMT_SPDIF:
		if (master->core->ver < 2) {
			dev_err(dai->dev, "This FSI can not use SPDIF\n");
			return -EINVAL;
		}
		data = CR_BWS_16 | CR_DTMD_SPDIF_PCM | CR_PCM;
		io->chan_num = 2;
		fsi_spdif_clk_ctrl(fsi, 1);
		fsi_reg_mask_set(fsi, OUT_SEL, DMMD, DMMD);
		break;
	default:
		dev_err(dai->dev, "unknown format.\n");
		return -EINVAL;
	}
	fsi_reg_write(fsi, reg, data);

	/* irq clear */
	fsi_irq_disable(fsi, is_play);
	fsi_irq_clear_status(fsi);

	/* fifo init */
	fsi_fifo_init(fsi, is_play, dai);

	return 0;
}

static void fsi_dai_shutdown(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct fsi_priv *fsi = fsi_get_priv(substream);
	int is_play = fsi_is_play(substream);

	fsi_irq_disable(fsi, is_play);
	fsi_clk_ctrl(fsi, 0);

	pm_runtime_put_sync(dai->dev);
}

static int fsi_dai_trigger(struct snd_pcm_substream *substream, int cmd,
			   struct snd_soc_dai *dai)
{
	struct fsi_priv *fsi = fsi_get_priv(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int is_play = fsi_is_play(substream);
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		fsi_stream_push(fsi, is_play, substream,
				frames_to_bytes(runtime, runtime->buffer_size),
				frames_to_bytes(runtime, runtime->period_size));
		ret = is_play ? fsi_data_push(fsi, 1) : fsi_data_pop(fsi, 1);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		fsi_irq_disable(fsi, is_play);
		fsi_stream_pop(fsi, is_play);
		break;
	}

	return ret;
}

static int fsi_dai_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct fsi_priv *fsi = fsi_get_priv(substream);
	struct fsi_master *master = fsi_get_master(fsi);
	int (*set_rate)(int is_porta, int rate) = master->info->set_rate;
	int fsi_ver = master->core->ver;
	int is_play = fsi_is_play(substream);
	int ret;

	/* if slave mode, set_rate is not needed */
	if (!fsi_is_master_mode(fsi, is_play))
		return 0;

	/* it is error if no set_rate */
	if (!set_rate)
		return -EIO;

	ret = set_rate(fsi_is_port_a(fsi), params_rate(params));
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
				dev_err(dai->dev, "unsupported ACKMD\n");
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
				dev_err(dai->dev, "unsupported ACKMD\n");
			else
				data |= (0x7 << 8);
			break;
		}

		fsi_reg_mask_set(fsi, CKG1, (ACKMD_MASK | BPFMD_MASK) , data);
		udelay(10);
		fsi_clk_ctrl(fsi, 1);
		ret = 0;
	}

	return ret;

}

static struct snd_soc_dai_ops fsi_dai_ops = {
	.startup	= fsi_dai_startup,
	.shutdown	= fsi_dai_shutdown,
	.trigger	= fsi_dai_trigger,
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
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct fsi_priv *fsi = fsi_get_priv(substream);
	struct fsi_stream *io = fsi_get_stream(fsi, fsi_is_play(substream));
	long location;

	location = (io->buff_offset - 1);
	if (location < 0)
		location = 0;

	return bytes_to_frames(runtime, location);
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

static int fsi_pcm_new(struct snd_card *card,
		       struct snd_soc_dai *dai,
		       struct snd_pcm *pcm)
{
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
	master->fsia.mst_ctrl	= A_MST_CTLR;

	/* FSI B setting */
	master->fsib.base	= master->base + 0x40;
	master->fsib.master	= master;
	master->fsib.mst_ctrl	= B_MST_CTLR;

	pm_runtime_enable(&pdev->dev);
	pm_runtime_resume(&pdev->dev);
	dev_set_drvdata(&pdev->dev, master);

	fsi_soft_all_reset(master);

	ret = request_irq(irq, &fsi_interrupt, IRQF_DISABLED,
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

	return snd_soc_register_dais(&pdev->dev, fsi_soc_dai, ARRAY_SIZE(fsi_soc_dai));

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

	snd_soc_unregister_dais(&pdev->dev, ARRAY_SIZE(fsi_soc_dai));
	snd_soc_unregister_platform(&pdev->dev);

	pm_runtime_disable(&pdev->dev);

	free_irq(master->irq, master);

	iounmap(master->base);
	kfree(master);

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

static int __init fsi_mobile_init(void)
{
	return platform_driver_register(&fsi_driver);
}

static void __exit fsi_mobile_exit(void)
{
	platform_driver_unregister(&fsi_driver);
}

module_init(fsi_mobile_init);
module_exit(fsi_mobile_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SuperH onchip FSI audio driver");
MODULE_AUTHOR("Kuninori Morimoto <morimoto.kuninori@renesas.com>");
