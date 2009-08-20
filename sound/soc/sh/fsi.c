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
#include <linux/clk.h>
#include <linux/io.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <sound/sh_fsi.h>
#include <asm/atomic.h>
#include <asm/dma.h>
#include <asm/dma-sh.h>

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

	int fifo_max;
	int chan;
	int dma_chan;

	int byte_offset;
	int period_len;
	int buffer_len;
	int periods;
};

struct fsi_master {
	void __iomem *base;
	int irq;
	struct clk *clk;
	struct fsi_priv fsia;
	struct fsi_priv fsib;
	struct sh_fsi_platform_info *info;
};

static struct fsi_master *master;

/************************************************************************


		basic read write function


************************************************************************/
static int __fsi_reg_write(u32 reg, u32 data)
{
	/* valid data area is 24bit */
	data &= 0x00ffffff;

	return ctrl_outl(data, reg);
}

static u32 __fsi_reg_read(u32 reg)
{
	return ctrl_inl(reg);
}

static int __fsi_reg_mask_set(u32 reg, u32 mask, u32 data)
{
	u32 val = __fsi_reg_read(reg);

	val &= ~mask;
	val |= data & mask;

	return __fsi_reg_write(reg, val);
}

static int fsi_reg_write(struct fsi_priv *fsi, u32 reg, u32 data)
{
	if (reg > REG_END)
		return -1;

	return __fsi_reg_write((u32)(fsi->base + reg), data);
}

static u32 fsi_reg_read(struct fsi_priv *fsi, u32 reg)
{
	if (reg > REG_END)
		return 0;

	return __fsi_reg_read((u32)(fsi->base + reg));
}

static int fsi_reg_mask_set(struct fsi_priv *fsi, u32 reg, u32 mask, u32 data)
{
	if (reg > REG_END)
		return -1;

	return __fsi_reg_mask_set((u32)(fsi->base + reg), mask, data);
}

static int fsi_master_write(u32 reg, u32 data)
{
	if ((reg < MREG_START) ||
	    (reg > MREG_END))
		return -1;

	return __fsi_reg_write((u32)(master->base + reg), data);
}

static u32 fsi_master_read(u32 reg)
{
	if ((reg < MREG_START) ||
	    (reg > MREG_END))
		return 0;

	return __fsi_reg_read((u32)(master->base + reg));
}

static int fsi_master_mask_set(u32 reg, u32 mask, u32 data)
{
	if ((reg < MREG_START) ||
	    (reg > MREG_END))
		return -1;

	return __fsi_reg_mask_set((u32)(master->base + reg), mask, data);
}

/************************************************************************


		basic function


************************************************************************/
static struct fsi_priv *fsi_get(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd;
	struct fsi_priv *fsi = NULL;

	if (!substream || !master)
		return NULL;

	rtd = substream->private_data;
	switch (rtd->dai->cpu_dai->id) {
	case 0:
		fsi = &master->fsia;
		break;
	case 1:
		fsi = &master->fsib;
		break;
	}

	return fsi;
}

static int fsi_is_port_a(struct fsi_priv *fsi)
{
	/* return
	 * 1 : port a
	 * 0 : port b
	 */

	if (fsi == &master->fsia)
		return 1;

	return 0;
}

static u32 fsi_get_info_flags(struct fsi_priv *fsi)
{
	int is_porta = fsi_is_port_a(fsi);

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

static int fsi_get_residue(struct fsi_priv *fsi, int is_play)
{
	int residue;
	int width;
	struct snd_pcm_runtime *runtime;

	runtime = fsi->substream->runtime;

	/* get 1 channel data width */
	width = frames_to_bytes(runtime, 1) / fsi->chan;

	if (2 == width)
		residue = fsi_get_fifo_residue(fsi, is_play);
	else
		residue = get_dma_residue(fsi->dma_chan);

	return residue;
}

/************************************************************************


		basic dma function


************************************************************************/
#define PORTA_DMA 0
#define PORTB_DMA 1

static int fsi_get_dma_chan(void)
{
	if (0 != request_dma(PORTA_DMA, "fsia"))
		return -EIO;

	if (0 != request_dma(PORTB_DMA, "fsib")) {
		free_dma(PORTA_DMA);
		return -EIO;
	}

	master->fsia.dma_chan = PORTA_DMA;
	master->fsib.dma_chan = PORTB_DMA;

	return 0;
}

static void fsi_free_dma_chan(void)
{
	dma_wait_for_completion(PORTA_DMA);
	dma_wait_for_completion(PORTB_DMA);
	free_dma(PORTA_DMA);
	free_dma(PORTB_DMA);

	master->fsia.dma_chan = -1;
	master->fsib.dma_chan = -1;
}

/************************************************************************


		ctrl function


************************************************************************/
static void fsi_irq_enable(struct fsi_priv *fsi, int is_play)
{
	u32 data = fsi_port_ab_io_bit(fsi, is_play);

	fsi_master_mask_set(IMSK,  data, data);
	fsi_master_mask_set(IEMSK, data, data);
}

static void fsi_irq_disable(struct fsi_priv *fsi, int is_play)
{
	u32 data = fsi_port_ab_io_bit(fsi, is_play);

	fsi_master_mask_set(IMSK,  data, 0);
	fsi_master_mask_set(IEMSK, data, 0);
}

static void fsi_clk_ctrl(struct fsi_priv *fsi, int enable)
{
	u32 val = fsi_is_port_a(fsi) ? (1 << 0) : (1 << 4);

	if (enable)
		fsi_master_mask_set(CLK_RST, val, val);
	else
		fsi_master_mask_set(CLK_RST, val, 0);
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
	fsi_master_mask_set(INT_ST, data, 0);
}

static void fsi_soft_all_reset(void)
{
	u32 status = fsi_master_read(SOFT_RST);

	/* port AB reset */
	status &= 0x000000ff;
	fsi_master_write(SOFT_RST, status);
	mdelay(10);

	/* soft reset */
	status &= 0x000000f0;
	fsi_master_write(SOFT_RST, status);
	status |= 0x00000001;
	fsi_master_write(SOFT_RST, status);
	mdelay(10);
}

static void fsi_16data_push(struct fsi_priv *fsi,
			   struct snd_pcm_runtime *runtime,
			   int send)
{
	u16 *dma_start;
	u32 snd;
	int i;

	/* get dma start position for FSI */
	dma_start = (u16 *)runtime->dma_area;
	dma_start += fsi->byte_offset / 2;

	/*
	 * soft dma
	 * FSI can not use DMA when 16bpp
	 */
	for (i = 0; i < send; i++) {
		snd = (u32)dma_start[i];
		fsi_reg_write(fsi, DODT, snd << 8);
	}
}

static void fsi_32data_push(struct fsi_priv *fsi,
			   struct snd_pcm_runtime *runtime,
			   int send)
{
	u32 *dma_start;

	/* get dma start position for FSI */
	dma_start = (u32 *)runtime->dma_area;
	dma_start += fsi->byte_offset / 4;

	dma_wait_for_completion(fsi->dma_chan);
	dma_configure_channel(fsi->dma_chan, (SM_INC|0x400|TS_32|TM_BUR));
	dma_write(fsi->dma_chan, (u32)dma_start,
		  (u32)(fsi->base + DODT), send * 4);
}

/* playback interrupt */
static int fsi_data_push(struct fsi_priv *fsi)
{
	struct snd_pcm_runtime *runtime;
	struct snd_pcm_substream *substream = NULL;
	int send;
	int fifo_free;
	int width;

	if (!fsi			||
	    !fsi->substream		||
	    !fsi->substream->runtime)
		return -EINVAL;

	runtime = fsi->substream->runtime;

	/* FSI FIFO has limit.
	 * So, this driver can not send periods data at a time
	 */
	if (fsi->byte_offset >=
	    fsi->period_len * (fsi->periods + 1)) {

		substream = fsi->substream;
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

	if (2 == width)
		fsi_16data_push(fsi, runtime, send);
	else if (4 == width)
		fsi_32data_push(fsi, runtime, send);
	else
		return -EINVAL;

	fsi->byte_offset += send * width;

	fsi_irq_enable(fsi, 1);

	if (substream)
		snd_pcm_period_elapsed(substream);

	return 0;
}

static irqreturn_t fsi_interrupt(int irq, void *data)
{
	u32 status = fsi_master_read(SOFT_RST) & ~0x00000010;
	u32 int_st = fsi_master_read(INT_ST);

	/* clear irq status */
	fsi_master_write(SOFT_RST, status);
	fsi_master_write(SOFT_RST, status | 0x00000010);

	if (int_st & INT_A_OUT)
		fsi_data_push(&master->fsia);
	if (int_st & INT_B_OUT)
		fsi_data_push(&master->fsib);

	fsi_master_write(INT_ST, 0x0000000);

	return IRQ_HANDLED;
}

/************************************************************************


		dai ops


************************************************************************/
static int fsi_dai_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	struct fsi_priv *fsi = fsi_get(substream);
	const char *msg;
	u32 flags = fsi_get_info_flags(fsi);
	u32 fmt;
	u32 reg;
	u32 data;
	int is_play = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);
	int is_master;
	int ret = 0;

	clk_enable(master->clk);

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
	dev_dbg(dai->dev, "use %s format (%d channel) use %d DMAC\n",
		msg, fsi->chan, fsi->dma_chan);

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
	struct fsi_priv *fsi = fsi_get(substream);
	int is_play = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;

	fsi_irq_disable(fsi, is_play);
	fsi_clk_ctrl(fsi, 0);

	clk_disable(master->clk);
}

static int fsi_dai_trigger(struct snd_pcm_substream *substream, int cmd,
			   struct snd_soc_dai *dai)
{
	struct fsi_priv *fsi = fsi_get(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int is_play = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	int ret = 0;

	/* capture not supported */
	if (!is_play)
		return -ENODEV;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		fsi_stream_push(fsi, substream,
				frames_to_bytes(runtime, runtime->buffer_size),
				frames_to_bytes(runtime, runtime->period_size));
		ret = fsi_data_push(fsi);
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
	struct fsi_priv *fsi = fsi_get(substream);
	int is_play = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	long location;

	location = (fsi->byte_offset - 1) - fsi_get_residue(fsi, is_play);
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
		/* capture not supported */
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
		/* capture not supported */
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
	struct resource *res;
	char clk_name[8];
	unsigned int irq;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (!res || !irq) {
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
	master->fsib.base	= master->base + 0x40;

	master->fsia.dma_chan = -1;
	master->fsib.dma_chan = -1;

	ret = fsi_get_dma_chan();
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot get dma api\n");
		goto exit_iounmap;
	}

	/* FSI is based on SPU mstp */
	snprintf(clk_name, sizeof(clk_name), "spu%d", pdev->id);
	master->clk = clk_get(NULL, clk_name);
	if (IS_ERR(master->clk)) {
		dev_err(&pdev->dev, "cannot get %s mstp\n", clk_name);
		ret = -EIO;
		goto exit_free_dma;
	}

	fsi_soc_dai[0].dev		= &pdev->dev;
	fsi_soc_dai[1].dev		= &pdev->dev;

	fsi_soft_all_reset();

	ret = request_irq(irq, &fsi_interrupt, IRQF_DISABLED, "fsi", master);
	if (ret) {
		dev_err(&pdev->dev, "irq request err\n");
		goto exit_free_dma;
	}

	ret = snd_soc_register_platform(&fsi_soc_platform);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot snd soc register\n");
		goto exit_free_irq;
	}

	return snd_soc_register_dais(fsi_soc_dai, ARRAY_SIZE(fsi_soc_dai));

exit_free_irq:
	free_irq(irq, master);
exit_free_dma:
	fsi_free_dma_chan();
exit_iounmap:
	iounmap(master->base);
exit_kfree:
	kfree(master);
	master = NULL;
exit:
	return ret;
}

static int fsi_remove(struct platform_device *pdev)
{
	snd_soc_unregister_dais(fsi_soc_dai, ARRAY_SIZE(fsi_soc_dai));
	snd_soc_unregister_platform(&fsi_soc_platform);

	clk_put(master->clk);

	fsi_free_dma_chan();

	free_irq(master->irq, master);

	iounmap(master->base);
	kfree(master);
	master = NULL;
	return 0;
}

static struct platform_driver fsi_driver = {
	.driver 	= {
		.name	= "sh_fsi",
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
