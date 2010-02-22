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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/pm_runtime.h>
#include <linux/io.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <sound/sh_fsi.h>
#include <asm/atomic.h>

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
#define REG_END		MUTE_ST

#define INT_ST		0x0200
#define IEMSK		0x0204
#define IMSK		0x0208
#define MUTE		0x020C
#define CLK_RST		0x0210
#define SOFT_RST	0x0214
#define MREG_START	INT_ST
#define MREG_END	SOFT_RST

/* DO_FMT */
/* DI_FMT */
#define CR_FMT(param) ((param) << 4)
# define CR_MONO	0x0
# define CR_MONO_D	0x1
# define CR_PCM		0x2
# define CR_I2S		0x3
# define CR_TDM		0x4
# define CR_TDM_D	0x5

/* DOFF_CTL */
/* DIFF_CTL */
#define IRQ_HALF	0x00100000
#define FIFO_CLR	0x00000001

/* DOFF_ST */
#define ERR_OVER	0x00000010
#define ERR_UNDER	0x00000001
#define ST_ERR		(ERR_OVER | ERR_UNDER)

/* CLK_RST */
#define B_CLK		0x00000010
#define A_CLK		0x00000001

/* INT_ST */
#define INT_B_IN	(1 << 12)
#define INT_B_OUT	(1 << 8)
#define INT_A_IN	(1 << 4)
#define INT_A_OUT	(1 << 0)

#define FSI_RATES SNDRV_PCM_RATE_8000_96000

#define FSI_FMTS (SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S16_LE)

/************************************************************************


		struct


************************************************************************/
struct fsi_priv {
	void __iomem *base;
	struct snd_pcm_substream *substream;
	struct fsi_master *master;

	int fifo_max;
	int chan;

	int byte_offset;
	int period_len;
	int buffer_len;
	int periods;
};

struct fsi_master {
	void __iomem *base;
	int irq;
	struct fsi_priv fsia;
	struct fsi_priv fsib;
	struct sh_fsi_platform_info *info;
	spinlock_t lock;
};

/************************************************************************


		basic read write function


************************************************************************/
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
	if (reg > REG_END)
		return;

	__fsi_reg_write((u32)(fsi->base + reg), data);
}

static u32 fsi_reg_read(struct fsi_priv *fsi, u32 reg)
{
	if (reg > REG_END)
		return 0;

	return __fsi_reg_read((u32)(fsi->base + reg));
}

static void fsi_reg_mask_set(struct fsi_priv *fsi, u32 reg, u32 mask, u32 data)
{
	if (reg > REG_END)
		return;

	__fsi_reg_mask_set((u32)(fsi->base + reg), mask, data);
}

static void fsi_master_write(struct fsi_master *master, u32 reg, u32 data)
{
	unsigned long flags;

	if ((reg < MREG_START) ||
	    (reg > MREG_END))
		return;

	spin_lock_irqsave(&master->lock, flags);
	__fsi_reg_write((u32)(master->base + reg), data);
	spin_unlock_irqrestore(&master->lock, flags);
}

static u32 fsi_master_read(struct fsi_master *master, u32 reg)
{
	u32 ret;
	unsigned long flags;

	if ((reg < MREG_START) ||
	    (reg > MREG_END))
		return 0;

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
	    (reg > MREG_END))
		return;

	spin_lock_irqsave(&master->lock, flags);
	__fsi_reg_mask_set((u32)(master->base + reg), mask, data);
	spin_unlock_irqrestore(&master->lock, flags);
}

/************************************************************************


		basic function


************************************************************************/
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
	struct snd_soc_dai_link *machine = rtd->dai;

	return  machine->cpu_dai;
}

static struct fsi_priv *fsi_get_priv(struct snd_pcm_substream *substream)
{
	struct snd_soc_dai *dai = fsi_get_dai(substream);

	return dai->private_data;
}

static u32 fsi_get_info_flags(struct fsi_priv *fsi)
{
	int is_porta = fsi_is_port_a(fsi);
	struct fsi_master *master = fsi_get_master(fsi);

	return is_porta ? master->info->porta_flags :
		master->info->portb_flags;
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

static u32 fsi_port_ab_io_bit(struct fsi_priv *fsi, int is_play)
{
	int is_porta = fsi_is_port_a(fsi);
	u32 data;

	if (is_porta)
		data = is_play ? (1 << 0) : (1 << 4);
	else
		data = is_play ? (1 << 8) : (1 << 12);

	return data;
}

static void fsi_stream_push(struct fsi_priv *fsi,
			    struct snd_pcm_substream *substream,
			    u32 buffer_len,
			    u32 period_len)
{
	fsi->substream		= substream;
	fsi->buffer_len		= buffer_len;
	fsi->period_len		= period_len;
	fsi->byte_offset	= 0;
	fsi->periods		= 0;
}

static void fsi_stream_pop(struct fsi_priv *fsi)
{
	fsi->substream		= NULL;
	fsi->buffer_len		= 0;
	fsi->period_len		= 0;
	fsi->byte_offset	= 0;
	fsi->periods		= 0;
}

static int fsi_get_fifo_residue(struct fsi_priv *fsi, int is_play)
{
	u32 status;
	u32 reg = is_play ? DOFF_ST : DIFF_ST;
	int residue;

	status = fsi_reg_read(fsi, reg);
	residue = 0x1ff & (status >> 8);
	residue *= fsi->chan;

	return residue;
}

/************************************************************************


		ctrl function


************************************************************************/
static void fsi_irq_enable(struct fsi_priv *fsi, int is_play)
{
	u32 data = fsi_port_ab_io_bit(fsi, is_play);
	struct fsi_master *master = fsi_get_master(fsi);

	fsi_master_mask_set(master, IMSK,  data, data);
	fsi_master_mask_set(master, IEMSK, data, data);
}

static void fsi_irq_disable(struct fsi_priv *fsi, int is_play)
{
	u32 data = fsi_port_ab_io_bit(fsi, is_play);
	struct fsi_master *master = fsi_get_master(fsi);

	fsi_master_mask_set(master, IMSK,  data, 0);
	fsi_master_mask_set(master, IEMSK, data, 0);
}

static void fsi_clk_ctrl(struct fsi_priv *fsi, int enable)
{
	u32 val = fsi_is_port_a(fsi) ? (1 << 0) : (1 << 4);
	struct fsi_master *master = fsi_get_master(fsi);

	if (enable)
		fsi_master_mask_set(master, CLK_RST, val, val);
	else
		fsi_master_mask_set(master, CLK_RST, val, 0);
}

static void fsi_irq_init(struct fsi_priv *fsi, int is_play)
{
	u32 data;
	u32 ctrl;

	data = fsi_port_ab_io_bit(fsi, is_play);
	ctrl = is_play ? DOFF_CTL : DIFF_CTL;

	/* set IMSK */
	fsi_irq_disable(fsi, is_play);

	/* set interrupt generation factor */
	fsi_reg_write(fsi, ctrl, IRQ_HALF);

	/* clear FIFO */
	fsi_reg_mask_set(fsi, ctrl, FIFO_CLR, FIFO_CLR);

	/* clear interrupt factor */
	fsi_master_mask_set(fsi_get_master(fsi), INT_ST, data, 0);
}

static void fsi_soft_all_reset(struct fsi_master *master)
{
	u32 status = fsi_master_read(master, SOFT_RST);

	/* port AB reset */
	status &= 0x000000ff;
	fsi_master_write(master, SOFT_RST, status);
	mdelay(10);

	/* soft reset */
	status &= 0x000000f0;
	fsi_master_write(master, SOFT_RST, status);
	status |= 0x00000001;
	fsi_master_write(master, SOFT_RST, status);
	mdelay(10);
}

/* playback interrupt */
static int fsi_data_push(struct fsi_priv *fsi, int startup)
{
	struct snd_pcm_runtime *runtime;
	struct snd_pcm_substream *substream = NULL;
	u32 status;
	int send;
	int fifo_free;
	int width;
	u8 *start;
	int i, over_period;

	if (!fsi			||
	    !fsi->substream		||
	    !fsi->substream->runtime)
		return -EINVAL;

	over_period	= 0;
	substream	= fsi->substream;
	runtime		= substream->runtime;

	/* FSI FIFO has limit.
	 * So, this driver can not send periods data at a time
	 */
	if (fsi->byte_offset >=
	    fsi->period_len * (fsi->periods + 1)) {

		over_period = 1;
		fsi->periods = (fsi->periods + 1) % runtime->periods;

		if (0 == fsi->periods)
			fsi->byte_offset = 0;
	}

	/* get 1 channel data width */
	width = frames_to_bytes(runtime, 1) / fsi->chan;

	/* get send size for alsa */
	send = (fsi->buffer_len - fsi->byte_offset) / width;

	/*  get FIFO free size */
	fifo_free = (fsi->fifo_max * fsi->chan) - fsi_get_fifo_residue(fsi, 1);

	/* size check */
	if (fifo_free < send)
		send = fifo_free;

	start = runtime->dma_area;
	start += fsi->byte_offset;

	switch (width) {
	case 2:
		for (i = 0; i < send; i++)
			fsi_reg_write(fsi, DODT,
				      ((u32)*((u16 *)start + i) << 8));
		break;
	case 4:
		for (i = 0; i < send; i++)
			fsi_reg_write(fsi, DODT, *((u32 *)start + i));
		break;
	default:
		return -EINVAL;
	}

	fsi->byte_offset += send * width;

	status = fsi_reg_read(fsi, DOFF_ST);
	if (!startup) {
		struct snd_soc_dai *dai = fsi_get_dai(substream);

		if (status & ERR_OVER)
			dev_err(dai->dev, "over run\n");
		if (status & ERR_UNDER)
			dev_err(dai->dev, "under run\n");
	}
	fsi_reg_write(fsi, DOFF_ST, 0);

	fsi_irq_enable(fsi, 1);

	if (over_period)
		snd_pcm_period_elapsed(substream);

	return 0;
}

static int fsi_data_pop(struct fsi_priv *fsi, int startup)
{
	struct snd_pcm_runtime *runtime;
	struct snd_pcm_substream *substream = NULL;
	u32 status;
	int free;
	int fifo_fill;
	int width;
	u8 *start;
	int i, over_period;

	if (!fsi			||
	    !fsi->substream		||
	    !fsi->substream->runtime)
		return -EINVAL;

	over_period	= 0;
	substream	= fsi->substream;
	runtime		= substream->runtime;

	/* FSI FIFO has limit.
	 * So, this driver can not send periods data at a time
	 */
	if (fsi->byte_offset >=
	    fsi->period_len * (fsi->periods + 1)) {

		over_period = 1;
		fsi->periods = (fsi->periods + 1) % runtime->periods;

		if (0 == fsi->periods)
			fsi->byte_offset = 0;
	}

	/* get 1 channel data width */
	width = frames_to_bytes(runtime, 1) / fsi->chan;

	/* get free space for alsa */
	free = (fsi->buffer_len - fsi->byte_offset) / width;

	/* get recv size */
	fifo_fill = fsi_get_fifo_residue(fsi, 0);

	if (free < fifo_fill)
		fifo_fill = free;

	start = runtime->dma_area;
	start += fsi->byte_offset;

	switch (width) {
	case 2:
		for (i = 0; i < fifo_fill; i++)
			*((u16 *)start + i) =
				(u16)(fsi_reg_read(fsi, DIDT) >> 8);
		break;
	case 4:
		for (i = 0; i < fifo_fill; i++)
			*((u32 *)start + i) = fsi_reg_read(fsi, DIDT);
		break;
	default:
		return -EINVAL;
	}

	fsi->byte_offset += fifo_fill * width;

	status = fsi_reg_read(fsi, DIFF_ST);
	if (!startup) {
		struct snd_soc_dai *dai = fsi_get_dai(substream);

		if (status & ERR_OVER)
			dev_err(dai->dev, "over run\n");
		if (status & ERR_UNDER)
			dev_err(dai->dev, "under run\n");
	}
	fsi_reg_write(fsi, DIFF_ST, 0);

	fsi_irq_enable(fsi, 0);

	if (over_period)
		snd_pcm_period_elapsed(substream);

	return 0;
}

static irqreturn_t fsi_interrupt(int irq, void *data)
{
	struct fsi_master *master = data;
	u32 status = fsi_master_read(master, SOFT_RST) & ~0x00000010;
	u32 int_st = fsi_master_read(master, INT_ST);

	/* clear irq status */
	fsi_master_write(master, SOFT_RST, status);
	fsi_master_write(master, SOFT_RST, status | 0x00000010);

	if (int_st & INT_A_OUT)
		fsi_data_push(&master->fsia, 0);
	if (int_st & INT_B_OUT)
		fsi_data_push(&master->fsib, 0);
	if (int_st & INT_A_IN)
		fsi_data_pop(&master->fsia, 0);
	if (int_st & INT_B_IN)
		fsi_data_pop(&master->fsib, 0);

	fsi_master_write(master, INT_ST, 0x0000000);

	return IRQ_HANDLED;
}

/************************************************************************


		dai ops


************************************************************************/
static int fsi_dai_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	struct fsi_priv *fsi = fsi_get_priv(substream);
	const char *msg;
	u32 flags = fsi_get_info_flags(fsi);
	u32 fmt;
	u32 reg;
	u32 data;
	int is_play = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);
	int is_master;
	int ret = 0;

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
	switch (SH_FSI_INVERSION_MASK & flags) {
	case SH_FSI_LRM_INV:
		data = 1 << 12;
		break;
	case SH_FSI_BRM_INV:
		data = 1 << 8;
		break;
	case SH_FSI_LRS_INV:
		data = 1 << 4;
		break;
	case SH_FSI_BRS_INV:
		data = 1 << 0;
		break;
	}
	fsi_reg_write(fsi, CKG2, data);

	/* do fmt, di fmt */
	data = 0;
	reg = is_play ? DO_FMT : DI_FMT;
	fmt = is_play ? SH_FSI_GET_OFMT(flags) : SH_FSI_GET_IFMT(flags);
	switch (fmt) {
	case SH_FSI_FMT_MONO:
		msg = "MONO";
		data = CR_FMT(CR_MONO);
		fsi->chan = 1;
		break;
	case SH_FSI_FMT_MONO_DELAY:
		msg = "MONO Delay";
		data = CR_FMT(CR_MONO_D);
		fsi->chan = 1;
		break;
	case SH_FSI_FMT_PCM:
		msg = "PCM";
		data = CR_FMT(CR_PCM);
		fsi->chan = 2;
		break;
	case SH_FSI_FMT_I2S:
		msg = "I2S";
		data = CR_FMT(CR_I2S);
		fsi->chan = 2;
		break;
	case SH_FSI_FMT_TDM:
		msg = "TDM";
		data = CR_FMT(CR_TDM) | (fsi->chan - 1);
		fsi->chan = is_play ?
			SH_FSI_GET_CH_O(flags) : SH_FSI_GET_CH_I(flags);
		break;
	case SH_FSI_FMT_TDM_DELAY:
		msg = "TDM Delay";
		data = CR_FMT(CR_TDM_D) | (fsi->chan - 1);
		fsi->chan = is_play ?
			SH_FSI_GET_CH_O(flags) : SH_FSI_GET_CH_I(flags);
		break;
	default:
		dev_err(dai->dev, "unknown format.\n");
		return -EINVAL;
	}

	switch (fsi->chan) {
	case 1:
		fsi->fifo_max = 256;
		break;
	case 2:
		fsi->fifo_max = 128;
		break;
	case 3:
	case 4:
		fsi->fifo_max = 64;
		break;
	case 5:
	case 6:
	case 7:
	case 8:
		fsi->fifo_max = 32;
		break;
	default:
		dev_err(dai->dev, "channel size error.\n");
		return -EINVAL;
	}

	fsi_reg_write(fsi, reg, data);

	/*
	 * clear clk reset if master mode
	 */
	if (is_master)
		fsi_clk_ctrl(fsi, 1);

	/* irq setting */
	fsi_irq_init(fsi, is_play);

	return ret;
}

static void fsi_dai_shutdown(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct fsi_priv *fsi = fsi_get_priv(substream);
	int is_play = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;

	fsi_irq_disable(fsi, is_play);
	fsi_clk_ctrl(fsi, 0);

	pm_runtime_put_sync(dai->dev);
}

static int fsi_dai_trigger(struct snd_pcm_substream *substream, int cmd,
			   struct snd_soc_dai *dai)
{
	struct fsi_priv *fsi = fsi_get_priv(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int is_play = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		fsi_stream_push(fsi, substream,
				frames_to_bytes(runtime, runtime->buffer_size),
				frames_to_bytes(runtime, runtime->period_size));
		ret = is_play ? fsi_data_push(fsi, 1) : fsi_data_pop(fsi, 1);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		fsi_irq_disable(fsi, is_play);
		fsi_stream_pop(fsi);
		break;
	}

	return ret;
}

static struct snd_soc_dai_ops fsi_dai_ops = {
	.startup	= fsi_dai_startup,
	.shutdown	= fsi_dai_shutdown,
	.trigger	= fsi_dai_trigger,
};

/************************************************************************


		pcm ops


************************************************************************/
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
	long location;

	location = (fsi->byte_offset - 1);
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

/************************************************************************


		snd_soc_platform


************************************************************************/
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

/************************************************************************


		alsa struct


************************************************************************/
struct snd_soc_dai fsi_soc_dai[] = {
	{
		.name			= "FSIA",
		.id			= 0,
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
		.name			= "FSIB",
		.id			= 1,
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
EXPORT_SYMBOL_GPL(fsi_soc_dai);

struct snd_soc_platform fsi_soc_platform = {
	.name		= "fsi-pcm",
	.pcm_ops 	= &fsi_pcm_ops,
	.pcm_new	= fsi_pcm_new,
	.pcm_free	= fsi_pcm_free,
};
EXPORT_SYMBOL_GPL(fsi_soc_platform);

/************************************************************************


		platform function


************************************************************************/
static int fsi_probe(struct platform_device *pdev)
{
	struct fsi_master *master;
	struct resource *res;
	unsigned int irq;
	int ret;

	if (0 != pdev->id) {
		dev_err(&pdev->dev, "current fsi support id 0 only now\n");
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

	master->irq		= irq;
	master->info		= pdev->dev.platform_data;
	master->fsia.base	= master->base;
	master->fsia.master	= master;
	master->fsib.base	= master->base + 0x40;
	master->fsib.master	= master;
	spin_lock_init(&master->lock);

	pm_runtime_enable(&pdev->dev);
	pm_runtime_resume(&pdev->dev);

	fsi_soc_dai[0].dev		= &pdev->dev;
	fsi_soc_dai[0].private_data	= &master->fsia;
	fsi_soc_dai[1].dev		= &pdev->dev;
	fsi_soc_dai[1].private_data	= &master->fsib;

	fsi_soft_all_reset(master);

	ret = request_irq(irq, &fsi_interrupt, IRQF_DISABLED, "fsi", master);
	if (ret) {
		dev_err(&pdev->dev, "irq request err\n");
		goto exit_iounmap;
	}

	ret = snd_soc_register_platform(&fsi_soc_platform);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot snd soc register\n");
		goto exit_free_irq;
	}

	return snd_soc_register_dais(fsi_soc_dai, ARRAY_SIZE(fsi_soc_dai));

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

	master = fsi_get_master(fsi_soc_dai[0].private_data);

	snd_soc_unregister_dais(fsi_soc_dai, ARRAY_SIZE(fsi_soc_dai));
	snd_soc_unregister_platform(&fsi_soc_platform);

	pm_runtime_disable(&pdev->dev);

	free_irq(master->irq, master);

	iounmap(master->base);
	kfree(master);

	fsi_soc_dai[0].dev		= NULL;
	fsi_soc_dai[0].private_data	= NULL;
	fsi_soc_dai[1].dev		= NULL;
	fsi_soc_dai[1].private_data	= NULL;

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

static struct platform_driver fsi_driver = {
	.driver 	= {
		.name	= "sh_fsi",
		.pm	= &fsi_pm_ops,
	},
	.probe		= fsi_probe,
	.remove		= fsi_remove,
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
