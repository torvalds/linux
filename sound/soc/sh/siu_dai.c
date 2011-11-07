/*
 * siu_dai.c - ALSA SoC driver for Renesas SH7343, SH7722 SIU peripheral.
 *
 * Copyright (C) 2009-2010 Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 * Copyright (C) 2006 Carlos Munoz <carlos@kenati.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/module.h>

#include <asm/clock.h>
#include <asm/siu.h>

#include <sound/control.h>
#include <sound/soc.h>

#include "siu.h"

/* Board specifics */
#if defined(CONFIG_CPU_SUBTYPE_SH7722)
# define SIU_MAX_VOLUME		0x1000
#else
# define SIU_MAX_VOLUME		0x7fff
#endif

#define PRAM_SIZE	0x2000
#define XRAM_SIZE	0x800
#define YRAM_SIZE	0x800

#define XRAM_OFFSET	0x4000
#define YRAM_OFFSET	0x6000
#define REG_OFFSET	0xc000

#define PLAYBACK_ENABLED	1
#define CAPTURE_ENABLED		2

#define VOLUME_CAPTURE		0
#define VOLUME_PLAYBACK		1
#define DFLT_VOLUME_LEVEL	0x08000800

/*
 * SPDIF is only available on port A and on some SIU implementations it is only
 * available for input. Due to the lack of hardware to test it, SPDIF is left
 * disabled in this driver version
 */
struct format_flag {
	u32	i2s;
	u32	pcm;
	u32	spdif;
	u32	mask;
};

struct port_flag {
	struct format_flag	playback;
	struct format_flag	capture;
};

struct siu_info *siu_i2s_data;

static struct port_flag siu_flags[SIU_PORT_NUM] = {
	[SIU_PORT_A] = {
		.playback = {
			.i2s	= 0x50000000,
			.pcm	= 0x40000000,
			.spdif	= 0x80000000,	/* not on all SIU versions */
			.mask	= 0xd0000000,
		},
		.capture = {
			.i2s	= 0x05000000,
			.pcm	= 0x04000000,
			.spdif	= 0x08000000,
			.mask	= 0x0d000000,
		},
	},
	[SIU_PORT_B] = {
		.playback = {
			.i2s	= 0x00500000,
			.pcm	= 0x00400000,
			.spdif	= 0,		/* impossible - turn off */
			.mask	= 0x00500000,
		},
		.capture = {
			.i2s	= 0x00050000,
			.pcm	= 0x00040000,
			.spdif	= 0,		/* impossible - turn off */
			.mask	= 0x00050000,
		},
	},
};

static void siu_dai_start(struct siu_port *port_info)
{
	struct siu_info *info = siu_i2s_data;
	u32 __iomem *base = info->reg;

	dev_dbg(port_info->pcm->card->dev, "%s\n", __func__);

	/* Turn on SIU clock */
	pm_runtime_get_sync(info->dev);

	/* Issue software reset to siu */
	siu_write32(base + SIU_SRCTL, 0);

	/* Wait for the reset to take effect */
	udelay(1);

	port_info->stfifo = 0;
	port_info->trdat = 0;

	/* portA, portB, SIU operate */
	siu_write32(base + SIU_SRCTL, 0x301);

	/* portA=256fs, portB=256fs */
	siu_write32(base + SIU_CKCTL, 0x40400000);

	/* portA's BRG does not divide SIUCKA */
	siu_write32(base + SIU_BRGASEL, 0);
	siu_write32(base + SIU_BRRA, 0);

	/* portB's BRG divides SIUCKB by half */
	siu_write32(base + SIU_BRGBSEL, 1);
	siu_write32(base + SIU_BRRB, 0);

	siu_write32(base + SIU_IFCTL, 0x44440000);

	/* portA: 32 bit/fs, master; portB: 32 bit/fs, master */
	siu_write32(base + SIU_SFORM, 0x0c0c0000);

	/*
	 * Volume levels: looks like the DSP firmware implements volume controls
	 * differently from what's described in the datasheet
	 */
	siu_write32(base + SIU_SBDVCA, port_info->playback.volume);
	siu_write32(base + SIU_SBDVCB, port_info->capture.volume);
}

static void siu_dai_stop(struct siu_port *port_info)
{
	struct siu_info *info = siu_i2s_data;
	u32 __iomem *base = info->reg;

	/* SIU software reset */
	siu_write32(base + SIU_SRCTL, 0);

	/* Turn off SIU clock */
	pm_runtime_put_sync(info->dev);
}

static void siu_dai_spbAselect(struct siu_port *port_info)
{
	struct siu_info *info = siu_i2s_data;
	struct siu_firmware *fw = &info->fw;
	u32 *ydef = fw->yram0;
	u32 idx;

	/* path A use */
	if (!info->port_id)
		idx = 1;		/* portA */
	else
		idx = 2;		/* portB */

	ydef[0] = (fw->spbpar[idx].ab1a << 16) |
		(fw->spbpar[idx].ab0a << 8) |
		(fw->spbpar[idx].dir << 7) | 3;
	ydef[1] = fw->yram0[1];	/* 0x03000300 */
	ydef[2] = (16 / 2) << 24;
	ydef[3] = fw->yram0[3];	/* 0 */
	ydef[4] = fw->yram0[4];	/* 0 */
	ydef[7] = fw->spbpar[idx].event;
	port_info->stfifo |= fw->spbpar[idx].stfifo;
	port_info->trdat |= fw->spbpar[idx].trdat;
}

static void siu_dai_spbBselect(struct siu_port *port_info)
{
	struct siu_info *info = siu_i2s_data;
	struct siu_firmware *fw = &info->fw;
	u32 *ydef = fw->yram0;
	u32 idx;

	/* path B use */
	if (!info->port_id)
		idx = 7;		/* portA */
	else
		idx = 8;		/* portB */

	ydef[5] = (fw->spbpar[idx].ab1a << 16) |
		(fw->spbpar[idx].ab0a << 8) | 1;
	ydef[6] = fw->spbpar[idx].event;
	port_info->stfifo |= fw->spbpar[idx].stfifo;
	port_info->trdat |= fw->spbpar[idx].trdat;
}

static void siu_dai_open(struct siu_stream *siu_stream)
{
	struct siu_info *info = siu_i2s_data;
	u32 __iomem *base = info->reg;
	u32 srctl, ifctl;

	srctl = siu_read32(base + SIU_SRCTL);
	ifctl = siu_read32(base + SIU_IFCTL);

	switch (info->port_id) {
	case SIU_PORT_A:
		/* portA operates */
		srctl |= 0x200;
		ifctl &= ~0xc2;
		break;
	case SIU_PORT_B:
		/* portB operates */
		srctl |= 0x100;
		ifctl &= ~0x31;
		break;
	}

	siu_write32(base + SIU_SRCTL, srctl);
	/* Unmute and configure portA */
	siu_write32(base + SIU_IFCTL, ifctl);
}

/*
 * At the moment only fixed Left-upper, Left-lower, Right-upper, Right-lower
 * packing is supported
 */
static void siu_dai_pcmdatapack(struct siu_stream *siu_stream)
{
	struct siu_info *info = siu_i2s_data;
	u32 __iomem *base = info->reg;
	u32 dpak;

	dpak = siu_read32(base + SIU_DPAK);

	switch (info->port_id) {
	case SIU_PORT_A:
		dpak &= ~0xc0000000;
		break;
	case SIU_PORT_B:
		dpak &= ~0x00c00000;
		break;
	}

	siu_write32(base + SIU_DPAK, dpak);
}

static int siu_dai_spbstart(struct siu_port *port_info)
{
	struct siu_info *info = siu_i2s_data;
	u32 __iomem *base = info->reg;
	struct siu_firmware *fw = &info->fw;
	u32 *ydef = fw->yram0;
	int cnt;
	u32 __iomem *add;
	u32 *ptr;

	/* Load SPB Program in PRAM */
	ptr = fw->pram0;
	add = info->pram;
	for (cnt = 0; cnt < PRAM0_SIZE; cnt++, add++, ptr++)
		siu_write32(add, *ptr);

	ptr = fw->pram1;
	add = info->pram + (0x0100 / sizeof(u32));
	for (cnt = 0; cnt < PRAM1_SIZE; cnt++, add++, ptr++)
		siu_write32(add, *ptr);

	/* XRAM initialization */
	add = info->xram;
	for (cnt = 0; cnt < XRAM0_SIZE + XRAM1_SIZE + XRAM2_SIZE; cnt++, add++)
		siu_write32(add, 0);

	/* YRAM variable area initialization */
	add = info->yram;
	for (cnt = 0; cnt < YRAM_DEF_SIZE; cnt++, add++)
		siu_write32(add, ydef[cnt]);

	/* YRAM FIR coefficient area initialization */
	add = info->yram + (0x0200 / sizeof(u32));
	for (cnt = 0; cnt < YRAM_FIR_SIZE; cnt++, add++)
		siu_write32(add, fw->yram_fir_coeff[cnt]);

	/* YRAM IIR coefficient area initialization */
	add = info->yram + (0x0600 / sizeof(u32));
	for (cnt = 0; cnt < YRAM_IIR_SIZE; cnt++, add++)
		siu_write32(add, 0);

	siu_write32(base + SIU_TRDAT, port_info->trdat);
	port_info->trdat = 0x0;


	/* SPB start condition: software */
	siu_write32(base + SIU_SBACTIV, 0);
	/* Start SPB */
	siu_write32(base + SIU_SBCTL, 0xc0000000);
	/* Wait for program to halt */
	cnt = 0x10000;
	while (--cnt && siu_read32(base + SIU_SBCTL) != 0x80000000)
		cpu_relax();

	if (!cnt)
		return -EBUSY;

	/* SPB program start address setting */
	siu_write32(base + SIU_SBPSET, 0x00400000);
	/* SPB hardware start(FIFOCTL source) */
	siu_write32(base + SIU_SBACTIV, 0xc0000000);

	return 0;
}

static void siu_dai_spbstop(struct siu_port *port_info)
{
	struct siu_info *info = siu_i2s_data;
	u32 __iomem *base = info->reg;

	siu_write32(base + SIU_SBACTIV, 0);
	/* SPB stop */
	siu_write32(base + SIU_SBCTL, 0);

	port_info->stfifo = 0;
}

/*		API functions		*/

/* Playback and capture hardware properties are identical */
static struct snd_pcm_hardware siu_dai_pcm_hw = {
	.info			= SNDRV_PCM_INFO_INTERLEAVED,
	.formats		= SNDRV_PCM_FMTBIT_S16,
	.rates			= SNDRV_PCM_RATE_8000_48000,
	.rate_min		= 8000,
	.rate_max		= 48000,
	.channels_min		= 2,
	.channels_max		= 2,
	.buffer_bytes_max	= SIU_BUFFER_BYTES_MAX,
	.period_bytes_min	= SIU_PERIOD_BYTES_MIN,
	.period_bytes_max	= SIU_PERIOD_BYTES_MAX,
	.periods_min		= SIU_PERIODS_MIN,
	.periods_max		= SIU_PERIODS_MAX,
};

static int siu_dai_info_volume(struct snd_kcontrol *kctrl,
			       struct snd_ctl_elem_info *uinfo)
{
	struct siu_port *port_info = snd_kcontrol_chip(kctrl);

	dev_dbg(port_info->pcm->card->dev, "%s\n", __func__);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = SIU_MAX_VOLUME;

	return 0;
}

static int siu_dai_get_volume(struct snd_kcontrol *kctrl,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct siu_port *port_info = snd_kcontrol_chip(kctrl);
	struct device *dev = port_info->pcm->card->dev;
	u32 vol;

	dev_dbg(dev, "%s\n", __func__);

	switch (kctrl->private_value) {
	case VOLUME_PLAYBACK:
		/* Playback is always on port 0 */
		vol = port_info->playback.volume;
		ucontrol->value.integer.value[0] = vol & 0xffff;
		ucontrol->value.integer.value[1] = vol >> 16 & 0xffff;
		break;
	case VOLUME_CAPTURE:
		/* Capture is always on port 1 */
		vol = port_info->capture.volume;
		ucontrol->value.integer.value[0] = vol & 0xffff;
		ucontrol->value.integer.value[1] = vol >> 16 & 0xffff;
		break;
	default:
		dev_err(dev, "%s() invalid private_value=%ld\n",
			__func__, kctrl->private_value);
		return -EINVAL;
	}

	return 0;
}

static int siu_dai_put_volume(struct snd_kcontrol *kctrl,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct siu_port *port_info = snd_kcontrol_chip(kctrl);
	struct device *dev = port_info->pcm->card->dev;
	struct siu_info *info = siu_i2s_data;
	u32 __iomem *base = info->reg;
	u32 new_vol;
	u32 cur_vol;

	dev_dbg(dev, "%s\n", __func__);

	if (ucontrol->value.integer.value[0] < 0 ||
	    ucontrol->value.integer.value[0] > SIU_MAX_VOLUME ||
	    ucontrol->value.integer.value[1] < 0 ||
	    ucontrol->value.integer.value[1] > SIU_MAX_VOLUME)
		return -EINVAL;

	new_vol = ucontrol->value.integer.value[0] |
		ucontrol->value.integer.value[1] << 16;

	/* See comment above - DSP firmware implementation */
	switch (kctrl->private_value) {
	case VOLUME_PLAYBACK:
		/* Playback is always on port 0 */
		cur_vol = port_info->playback.volume;
		siu_write32(base + SIU_SBDVCA, new_vol);
		port_info->playback.volume = new_vol;
		break;
	case VOLUME_CAPTURE:
		/* Capture is always on port 1 */
		cur_vol = port_info->capture.volume;
		siu_write32(base + SIU_SBDVCB, new_vol);
		port_info->capture.volume = new_vol;
		break;
	default:
		dev_err(dev, "%s() invalid private_value=%ld\n",
			__func__, kctrl->private_value);
		return -EINVAL;
	}

	if (cur_vol != new_vol)
		return 1;

	return 0;
}

static struct snd_kcontrol_new playback_controls = {
	.iface		= SNDRV_CTL_ELEM_IFACE_MIXER,
	.name		= "PCM Playback Volume",
	.index		= 0,
	.info		= siu_dai_info_volume,
	.get		= siu_dai_get_volume,
	.put		= siu_dai_put_volume,
	.private_value	= VOLUME_PLAYBACK,
};

static struct snd_kcontrol_new capture_controls = {
	.iface		= SNDRV_CTL_ELEM_IFACE_MIXER,
	.name		= "PCM Capture Volume",
	.index		= 0,
	.info		= siu_dai_info_volume,
	.get		= siu_dai_get_volume,
	.put		= siu_dai_put_volume,
	.private_value	= VOLUME_CAPTURE,
};

int siu_init_port(int port, struct siu_port **port_info, struct snd_card *card)
{
	struct device *dev = card->dev;
	struct snd_kcontrol *kctrl;
	int ret;

	*port_info = kzalloc(sizeof(**port_info), GFP_KERNEL);
	if (!*port_info)
		return -ENOMEM;

	dev_dbg(dev, "%s: port #%d@%p\n", __func__, port, *port_info);

	(*port_info)->playback.volume = DFLT_VOLUME_LEVEL;
	(*port_info)->capture.volume = DFLT_VOLUME_LEVEL;

	/*
	 * Add mixer support. The SPB is used to change the volume. Both
	 * ports use the same SPB. Therefore, we only register one
	 * control instance since it will be used by both channels.
	 * In error case we continue without controls.
	 */
	kctrl = snd_ctl_new1(&playback_controls, *port_info);
	ret = snd_ctl_add(card, kctrl);
	if (ret < 0)
		dev_err(dev,
			"failed to add playback controls %p port=%d err=%d\n",
			kctrl, port, ret);

	kctrl = snd_ctl_new1(&capture_controls, *port_info);
	ret = snd_ctl_add(card, kctrl);
	if (ret < 0)
		dev_err(dev,
			"failed to add capture controls %p port=%d err=%d\n",
			kctrl, port, ret);

	return 0;
}

void siu_free_port(struct siu_port *port_info)
{
	kfree(port_info);
}

static int siu_dai_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	struct siu_info *info = snd_soc_dai_get_drvdata(dai);
	struct snd_pcm_runtime *rt = substream->runtime;
	struct siu_port	*port_info = siu_port_info(substream);
	int ret;

	dev_dbg(substream->pcm->card->dev, "%s: port=%d@%p\n", __func__,
		info->port_id, port_info);

	snd_soc_set_runtime_hwparams(substream, &siu_dai_pcm_hw);

	ret = snd_pcm_hw_constraint_integer(rt, SNDRV_PCM_HW_PARAM_PERIODS);
	if (unlikely(ret < 0))
		return ret;

	siu_dai_start(port_info);

	return 0;
}

static void siu_dai_shutdown(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct siu_info *info = snd_soc_dai_get_drvdata(dai);
	struct siu_port	*port_info = siu_port_info(substream);

	dev_dbg(substream->pcm->card->dev, "%s: port=%d@%p\n", __func__,
		info->port_id, port_info);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		port_info->play_cap &= ~PLAYBACK_ENABLED;
	else
		port_info->play_cap &= ~CAPTURE_ENABLED;

	/* Stop the siu if the other stream is not using it */
	if (!port_info->play_cap) {
		/* during stmread or stmwrite ? */
		BUG_ON(port_info->playback.rw_flg || port_info->capture.rw_flg);
		siu_dai_spbstop(port_info);
		siu_dai_stop(port_info);
	}
}

/* PCM part of siu_dai_playback_prepare() / siu_dai_capture_prepare() */
static int siu_dai_prepare(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	struct siu_info *info = snd_soc_dai_get_drvdata(dai);
	struct snd_pcm_runtime *rt = substream->runtime;
	struct siu_port *port_info = siu_port_info(substream);
	struct siu_stream *siu_stream;
	int self, ret;

	dev_dbg(substream->pcm->card->dev,
		"%s: port %d, active streams %lx, %d channels\n",
		__func__, info->port_id, port_info->play_cap, rt->channels);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		self = PLAYBACK_ENABLED;
		siu_stream = &port_info->playback;
	} else {
		self = CAPTURE_ENABLED;
		siu_stream = &port_info->capture;
	}

	/* Set up the siu if not already done */
	if (!port_info->play_cap) {
		siu_stream->rw_flg = 0;	/* stream-data transfer flag */

		siu_dai_spbAselect(port_info);
		siu_dai_spbBselect(port_info);

		siu_dai_open(siu_stream);

		siu_dai_pcmdatapack(siu_stream);

		ret = siu_dai_spbstart(port_info);
		if (ret < 0)
			goto fail;
	} else {
		ret = 0;
	}

	port_info->play_cap |= self;

fail:
	return ret;
}

/*
 * SIU can set bus format to I2S / PCM / SPDIF independently for playback and
 * capture, however, the current API sets the bus format globally for a DAI.
 */
static int siu_dai_set_fmt(struct snd_soc_dai *dai,
			   unsigned int fmt)
{
	struct siu_info *info = snd_soc_dai_get_drvdata(dai);
	u32 __iomem *base = info->reg;
	u32 ifctl;

	dev_dbg(dai->dev, "%s: fmt 0x%x on port %d\n",
		__func__, fmt, info->port_id);

	if (info->port_id < 0)
		return -ENODEV;

	/* Here select between I2S / PCM / SPDIF */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		ifctl = siu_flags[info->port_id].playback.i2s |
			siu_flags[info->port_id].capture.i2s;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		ifctl = siu_flags[info->port_id].playback.pcm |
			siu_flags[info->port_id].capture.pcm;
		break;
	/* SPDIF disabled - see comment at the top */
	default:
		return -EINVAL;
	}

	ifctl |= ~(siu_flags[info->port_id].playback.mask |
		   siu_flags[info->port_id].capture.mask) &
		siu_read32(base + SIU_IFCTL);
	siu_write32(base + SIU_IFCTL, ifctl);

	return 0;
}

static int siu_dai_set_sysclk(struct snd_soc_dai *dai, int clk_id,
			      unsigned int freq, int dir)
{
	struct clk *siu_clk, *parent_clk;
	char *siu_name, *parent_name;
	int ret;

	if (dir != SND_SOC_CLOCK_IN)
		return -EINVAL;

	dev_dbg(dai->dev, "%s: using clock %d\n", __func__, clk_id);

	switch (clk_id) {
	case SIU_CLKA_PLL:
		siu_name = "siua_clk";
		parent_name = "pll_clk";
		break;
	case SIU_CLKA_EXT:
		siu_name = "siua_clk";
		parent_name = "siumcka_clk";
		break;
	case SIU_CLKB_PLL:
		siu_name = "siub_clk";
		parent_name = "pll_clk";
		break;
	case SIU_CLKB_EXT:
		siu_name = "siub_clk";
		parent_name = "siumckb_clk";
		break;
	default:
		return -EINVAL;
	}

	siu_clk = clk_get(dai->dev, siu_name);
	if (IS_ERR(siu_clk)) {
		dev_err(dai->dev, "%s: cannot get a SIU clock: %ld\n", __func__,
			PTR_ERR(siu_clk));
		return PTR_ERR(siu_clk);
	}

	parent_clk = clk_get(dai->dev, parent_name);
	if (IS_ERR(parent_clk)) {
		ret = PTR_ERR(parent_clk);
		dev_err(dai->dev, "cannot get a SIU clock parent: %d\n", ret);
		goto epclkget;
	}

	ret = clk_set_parent(siu_clk, parent_clk);
	if (ret < 0) {
		dev_err(dai->dev, "cannot reparent the SIU clock: %d\n", ret);
		goto eclksetp;
	}

	ret = clk_set_rate(siu_clk, freq);
	if (ret < 0)
		dev_err(dai->dev, "cannot set SIU clock rate: %d\n", ret);

	/* TODO: when clkdev gets reference counting we'll move these to siu_dai_shutdown() */
eclksetp:
	clk_put(parent_clk);
epclkget:
	clk_put(siu_clk);

	return ret;
}

static struct snd_soc_dai_ops siu_dai_ops = {
	.startup	= siu_dai_startup,
	.shutdown	= siu_dai_shutdown,
	.prepare	= siu_dai_prepare,
	.set_sysclk	= siu_dai_set_sysclk,
	.set_fmt	= siu_dai_set_fmt,
};

static struct snd_soc_dai_driver siu_i2s_dai = {
	.name	= "siu-i2s-dai",
	.playback = {
		.channels_min = 2,
		.channels_max = 2,
		.formats = SNDRV_PCM_FMTBIT_S16,
		.rates = SNDRV_PCM_RATE_8000_48000,
	},
	.capture = {
		.channels_min = 2,
		.channels_max = 2,
		.formats = SNDRV_PCM_FMTBIT_S16,
		.rates = SNDRV_PCM_RATE_8000_48000,
	 },
	.ops = &siu_dai_ops,
};

static int __devinit siu_probe(struct platform_device *pdev)
{
	const struct firmware *fw_entry;
	struct resource *res, *region;
	struct siu_info *info;
	int ret;

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	siu_i2s_data = info;
	info->dev = &pdev->dev;

	ret = request_firmware(&fw_entry, "siu_spb.bin", &pdev->dev);
	if (ret)
		goto ereqfw;

	/*
	 * Loaded firmware is "const" - read only, but we have to modify it in
	 * snd_siu_sh7343_spbAselect() and snd_siu_sh7343_spbBselect()
	 */
	memcpy(&info->fw, fw_entry->data, fw_entry->size);

	release_firmware(fw_entry);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -ENODEV;
		goto egetres;
	}

	region = request_mem_region(res->start, resource_size(res),
				    pdev->name);
	if (!region) {
		dev_err(&pdev->dev, "SIU region already claimed\n");
		ret = -EBUSY;
		goto ereqmemreg;
	}

	ret = -ENOMEM;
	info->pram = ioremap(res->start, PRAM_SIZE);
	if (!info->pram)
		goto emappram;
	info->xram = ioremap(res->start + XRAM_OFFSET, XRAM_SIZE);
	if (!info->xram)
		goto emapxram;
	info->yram = ioremap(res->start + YRAM_OFFSET, YRAM_SIZE);
	if (!info->yram)
		goto emapyram;
	info->reg = ioremap(res->start + REG_OFFSET, resource_size(res) -
			    REG_OFFSET);
	if (!info->reg)
		goto emapreg;

	dev_set_drvdata(&pdev->dev, info);

	/* register using ARRAY version so we can keep dai name */
	ret = snd_soc_register_dais(&pdev->dev, &siu_i2s_dai, 1);
	if (ret < 0)
		goto edaiinit;

	ret = snd_soc_register_platform(&pdev->dev, &siu_platform);
	if (ret < 0)
		goto esocregp;

	pm_runtime_enable(&pdev->dev);

	return ret;

esocregp:
	snd_soc_unregister_dai(&pdev->dev);
edaiinit:
	iounmap(info->reg);
emapreg:
	iounmap(info->yram);
emapyram:
	iounmap(info->xram);
emapxram:
	iounmap(info->pram);
emappram:
	release_mem_region(res->start, resource_size(res));
ereqmemreg:
egetres:
ereqfw:
	kfree(info);

	return ret;
}

static int __devexit siu_remove(struct platform_device *pdev)
{
	struct siu_info *info = dev_get_drvdata(&pdev->dev);
	struct resource *res;

	pm_runtime_disable(&pdev->dev);

	snd_soc_unregister_platform(&pdev->dev);
	snd_soc_unregister_dai(&pdev->dev);

	iounmap(info->reg);
	iounmap(info->yram);
	iounmap(info->xram);
	iounmap(info->pram);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res)
		release_mem_region(res->start, resource_size(res));
	kfree(info);

	return 0;
}

static struct platform_driver siu_driver = {
	.driver 	= {
		.owner	= THIS_MODULE,
		.name	= "siu-pcm-audio",
	},
	.probe		= siu_probe,
	.remove		= __devexit_p(siu_remove),
};

static int __init siu_init(void)
{
	return platform_driver_register(&siu_driver);
}

static void __exit siu_exit(void)
{
	platform_driver_unregister(&siu_driver);
}

module_init(siu_init)
module_exit(siu_exit)

MODULE_AUTHOR("Carlos Munoz <carlos@kenati.com>");
MODULE_DESCRIPTION("ALSA SoC SH7722 SIU driver");
MODULE_LICENSE("GPL");
