/* sound/soc/samsung/i2s.c
 *
 * ALSA SoC Audio Layer - Samsung I2S Controller driver
 *
 * Copyright (c) 2010 Samsung Electronics Co. Ltd.
 *	Jaswinder Singh <jassisinghbrar@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/pm_runtime.h>

#include <sound/soc.h>
#include <sound/pcm_params.h>

#include <linux/platform_data/asoc-s3c.h>

#include "dma.h"
#include "idma.h"
#include "i2s.h"
#include "i2s-regs.h"

#define msecs_to_loops(t) (loops_per_jiffy / 1000 * HZ * t)

enum samsung_dai_type {
	TYPE_PRI,
	TYPE_SEC,
};

struct samsung_i2s_variant_regs {
	unsigned int	bfs_off;
	unsigned int	rfs_off;
	unsigned int	sdf_off;
	unsigned int	txr_off;
	unsigned int	rclksrc_off;
	unsigned int	mss_off;
	unsigned int	cdclkcon_off;
	unsigned int	lrp_off;
	unsigned int	bfs_mask;
	unsigned int	rfs_mask;
	unsigned int	ftx0cnt_off;
};

struct samsung_i2s_dai_data {
	int dai_type;
	u32 quirks;
	const struct samsung_i2s_variant_regs *i2s_variant_regs;
};

struct i2s_dai {
	/* Platform device for this DAI */
	struct platform_device *pdev;
	/* IOREMAP'd SFRs */
	void __iomem	*addr;
	/* Physical base address of SFRs */
	u32	base;
	/* Rate of RCLK source clock */
	unsigned long rclk_srcrate;
	/* Frame Clock */
	unsigned frmclk;
	/*
	 * Specifically requested RCLK,BCLK by MACHINE Driver.
	 * 0 indicates CPU driver is free to choose any value.
	 */
	unsigned rfs, bfs;
	/* I2S Controller's core clock */
	struct clk *clk;
	/* Clock for generating I2S signals */
	struct clk *op_clk;
	/* Pointer to the Primary_Fifo if this is Sec_Fifo, NULL otherwise */
	struct i2s_dai *pri_dai;
	/* Pointer to the Secondary_Fifo if it has one, NULL otherwise */
	struct i2s_dai *sec_dai;
#define DAI_OPENED	(1 << 0) /* Dai is opened */
#define DAI_MANAGER	(1 << 1) /* Dai is the manager */
	unsigned mode;
	/* CDCLK pin direction: 0  - input, 1 - output */
	unsigned int cdclk_out:1;
	/* Driver for this DAI */
	struct snd_soc_dai_driver i2s_dai_drv;
	/* DMA parameters */
	struct s3c_dma_params dma_playback;
	struct s3c_dma_params dma_capture;
	struct s3c_dma_params idma_playback;
	u32	quirks;
	u32	suspend_i2smod;
	u32	suspend_i2scon;
	u32	suspend_i2spsr;
	unsigned long gpios[7];	/* i2s gpio line numbers */
	const struct samsung_i2s_variant_regs *variant_regs;
};

/* Lock for cross i/f checks */
static DEFINE_SPINLOCK(lock);

/* If this is the 'overlay' stereo DAI */
static inline bool is_secondary(struct i2s_dai *i2s)
{
	return i2s->pri_dai ? true : false;
}

/* If operating in SoC-Slave mode */
static inline bool is_slave(struct i2s_dai *i2s)
{
	u32 mod = readl(i2s->addr + I2SMOD);
	return (mod & (1 << i2s->variant_regs->mss_off)) ? true : false;
}

/* If this interface of the controller is transmitting data */
static inline bool tx_active(struct i2s_dai *i2s)
{
	u32 active;

	if (!i2s)
		return false;

	active = readl(i2s->addr + I2SCON);

	if (is_secondary(i2s))
		active &= CON_TXSDMA_ACTIVE;
	else
		active &= CON_TXDMA_ACTIVE;

	return active ? true : false;
}

/* If the other interface of the controller is transmitting data */
static inline bool other_tx_active(struct i2s_dai *i2s)
{
	struct i2s_dai *other = i2s->pri_dai ? : i2s->sec_dai;

	return tx_active(other);
}

/* If any interface of the controller is transmitting data */
static inline bool any_tx_active(struct i2s_dai *i2s)
{
	return tx_active(i2s) || other_tx_active(i2s);
}

/* If this interface of the controller is receiving data */
static inline bool rx_active(struct i2s_dai *i2s)
{
	u32 active;

	if (!i2s)
		return false;

	active = readl(i2s->addr + I2SCON) & CON_RXDMA_ACTIVE;

	return active ? true : false;
}

/* If the other interface of the controller is receiving data */
static inline bool other_rx_active(struct i2s_dai *i2s)
{
	struct i2s_dai *other = i2s->pri_dai ? : i2s->sec_dai;

	return rx_active(other);
}

/* If any interface of the controller is receiving data */
static inline bool any_rx_active(struct i2s_dai *i2s)
{
	return rx_active(i2s) || other_rx_active(i2s);
}

/* If the other DAI is transmitting or receiving data */
static inline bool other_active(struct i2s_dai *i2s)
{
	return other_rx_active(i2s) || other_tx_active(i2s);
}

/* If this DAI is transmitting or receiving data */
static inline bool this_active(struct i2s_dai *i2s)
{
	return tx_active(i2s) || rx_active(i2s);
}

/* If the controller is active anyway */
static inline bool any_active(struct i2s_dai *i2s)
{
	return this_active(i2s) || other_active(i2s);
}

static inline struct i2s_dai *to_info(struct snd_soc_dai *dai)
{
	return snd_soc_dai_get_drvdata(dai);
}

static inline bool is_opened(struct i2s_dai *i2s)
{
	if (i2s && (i2s->mode & DAI_OPENED))
		return true;
	else
		return false;
}

static inline bool is_manager(struct i2s_dai *i2s)
{
	if (is_opened(i2s) && (i2s->mode & DAI_MANAGER))
		return true;
	else
		return false;
}

/* Read RCLK of I2S (in multiples of LRCLK) */
static inline unsigned get_rfs(struct i2s_dai *i2s)
{
	u32 rfs;
	rfs = readl(i2s->addr + I2SMOD) >> i2s->variant_regs->rfs_off;
	rfs &= i2s->variant_regs->rfs_mask;

	switch (rfs) {
	case 7: return 192;
	case 6: return 96;
	case 5: return 128;
	case 4: return 64;
	case 3:	return 768;
	case 2: return 384;
	case 1:	return 512;
	default: return 256;
	}
}

/* Write RCLK of I2S (in multiples of LRCLK) */
static inline void set_rfs(struct i2s_dai *i2s, unsigned rfs)
{
	u32 mod = readl(i2s->addr + I2SMOD);
	int rfs_shift = i2s->variant_regs->rfs_off;

	mod &= ~(i2s->variant_regs->rfs_mask << rfs_shift);

	switch (rfs) {
	case 192:
		mod |= (EXYNOS7_MOD_RCLK_192FS << rfs_shift);
		break;
	case 96:
		mod |= (EXYNOS7_MOD_RCLK_96FS << rfs_shift);
		break;
	case 128:
		mod |= (EXYNOS7_MOD_RCLK_128FS << rfs_shift);
		break;
	case 64:
		mod |= (EXYNOS7_MOD_RCLK_64FS << rfs_shift);
		break;
	case 768:
		mod |= (MOD_RCLK_768FS << rfs_shift);
		break;
	case 512:
		mod |= (MOD_RCLK_512FS << rfs_shift);
		break;
	case 384:
		mod |= (MOD_RCLK_384FS << rfs_shift);
		break;
	default:
		mod |= (MOD_RCLK_256FS << rfs_shift);
		break;
	}

	writel(mod, i2s->addr + I2SMOD);
}

/* Read Bit-Clock of I2S (in multiples of LRCLK) */
static inline unsigned get_bfs(struct i2s_dai *i2s)
{
	u32 bfs;
	bfs = readl(i2s->addr + I2SMOD) >> i2s->variant_regs->bfs_off;
	bfs &= i2s->variant_regs->bfs_mask;

	switch (bfs) {
	case 8: return 256;
	case 7: return 192;
	case 6: return 128;
	case 5: return 96;
	case 4: return 64;
	case 3: return 24;
	case 2: return 16;
	case 1:	return 48;
	default: return 32;
	}
}

/* Write Bit-Clock of I2S (in multiples of LRCLK) */
static inline void set_bfs(struct i2s_dai *i2s, unsigned bfs)
{
	u32 mod = readl(i2s->addr + I2SMOD);
	int tdm = i2s->quirks & QUIRK_SUPPORTS_TDM;
	int bfs_shift = i2s->variant_regs->bfs_off;

	/* Non-TDM I2S controllers do not support BCLK > 48 * FS */
	if (!tdm && bfs > 48) {
		dev_err(&i2s->pdev->dev, "Unsupported BCLK divider\n");
		return;
	}

	mod &= ~(i2s->variant_regs->bfs_mask << bfs_shift);

	switch (bfs) {
	case 48:
		mod |= (MOD_BCLK_48FS << bfs_shift);
		break;
	case 32:
		mod |= (MOD_BCLK_32FS << bfs_shift);
		break;
	case 24:
		mod |= (MOD_BCLK_24FS << bfs_shift);
		break;
	case 16:
		mod |= (MOD_BCLK_16FS << bfs_shift);
		break;
	case 64:
		mod |= (EXYNOS5420_MOD_BCLK_64FS << bfs_shift);
		break;
	case 96:
		mod |= (EXYNOS5420_MOD_BCLK_96FS << bfs_shift);
		break;
	case 128:
		mod |= (EXYNOS5420_MOD_BCLK_128FS << bfs_shift);
		break;
	case 192:
		mod |= (EXYNOS5420_MOD_BCLK_192FS << bfs_shift);
		break;
	case 256:
		mod |= (EXYNOS5420_MOD_BCLK_256FS << bfs_shift);
		break;
	default:
		dev_err(&i2s->pdev->dev, "Wrong BCLK Divider!\n");
		return;
	}

	writel(mod, i2s->addr + I2SMOD);
}

/* Sample-Size */
static inline int get_blc(struct i2s_dai *i2s)
{
	int blc = readl(i2s->addr + I2SMOD);

	blc = (blc >> 13) & 0x3;

	switch (blc) {
	case 2: return 24;
	case 1:	return 8;
	default: return 16;
	}
}

/* TX Channel Control */
static void i2s_txctrl(struct i2s_dai *i2s, int on)
{
	void __iomem *addr = i2s->addr;
	int txr_off = i2s->variant_regs->txr_off;
	u32 con = readl(addr + I2SCON);
	u32 mod = readl(addr + I2SMOD) & ~(3 << txr_off);

	if (on) {
		con |= CON_ACTIVE;
		con &= ~CON_TXCH_PAUSE;

		if (is_secondary(i2s)) {
			con |= CON_TXSDMA_ACTIVE;
			con &= ~CON_TXSDMA_PAUSE;
		} else {
			con |= CON_TXDMA_ACTIVE;
			con &= ~CON_TXDMA_PAUSE;
		}

		if (any_rx_active(i2s))
			mod |= 2 << txr_off;
		else
			mod |= 0 << txr_off;
	} else {
		if (is_secondary(i2s)) {
			con |=  CON_TXSDMA_PAUSE;
			con &= ~CON_TXSDMA_ACTIVE;
		} else {
			con |=  CON_TXDMA_PAUSE;
			con &= ~CON_TXDMA_ACTIVE;
		}

		if (other_tx_active(i2s)) {
			writel(con, addr + I2SCON);
			return;
		}

		con |=  CON_TXCH_PAUSE;

		if (any_rx_active(i2s))
			mod |= 1 << txr_off;
		else
			con &= ~CON_ACTIVE;
	}

	writel(mod, addr + I2SMOD);
	writel(con, addr + I2SCON);
}

/* RX Channel Control */
static void i2s_rxctrl(struct i2s_dai *i2s, int on)
{
	void __iomem *addr = i2s->addr;
	int txr_off = i2s->variant_regs->txr_off;
	u32 con = readl(addr + I2SCON);
	u32 mod = readl(addr + I2SMOD) & ~(3 << txr_off);

	if (on) {
		con |= CON_RXDMA_ACTIVE | CON_ACTIVE;
		con &= ~(CON_RXDMA_PAUSE | CON_RXCH_PAUSE);

		if (any_tx_active(i2s))
			mod |= 2 << txr_off;
		else
			mod |= 1 << txr_off;
	} else {
		con |=  CON_RXDMA_PAUSE | CON_RXCH_PAUSE;
		con &= ~CON_RXDMA_ACTIVE;

		if (any_tx_active(i2s))
			mod |= 0 << txr_off;
		else
			con &= ~CON_ACTIVE;
	}

	writel(mod, addr + I2SMOD);
	writel(con, addr + I2SCON);
}

/* Flush FIFO of an interface */
static inline void i2s_fifo(struct i2s_dai *i2s, u32 flush)
{
	void __iomem *fic;
	u32 val;

	if (!i2s)
		return;

	if (is_secondary(i2s))
		fic = i2s->addr + I2SFICS;
	else
		fic = i2s->addr + I2SFIC;

	/* Flush the FIFO */
	writel(readl(fic) | flush, fic);

	/* Be patient */
	val = msecs_to_loops(1) / 1000; /* 1 usec */
	while (--val)
		cpu_relax();

	writel(readl(fic) & ~flush, fic);
}

static int i2s_set_sysclk(struct snd_soc_dai *dai,
	  int clk_id, unsigned int rfs, int dir)
{
	struct i2s_dai *i2s = to_info(dai);
	struct i2s_dai *other = i2s->pri_dai ? : i2s->sec_dai;
	u32 mod = readl(i2s->addr + I2SMOD);
	const struct samsung_i2s_variant_regs *i2s_regs = i2s->variant_regs;
	unsigned int cdcon_mask = 1 << i2s_regs->cdclkcon_off;
	unsigned int rsrc_mask = 1 << i2s_regs->rclksrc_off;

	switch (clk_id) {
	case SAMSUNG_I2S_OPCLK:
		mod &= ~MOD_OPCLK_MASK;
		mod |= dir;
		break;
	case SAMSUNG_I2S_CDCLK:
		/* Shouldn't matter in GATING(CLOCK_IN) mode */
		if (dir == SND_SOC_CLOCK_IN)
			rfs = 0;

		if ((rfs && other && other->rfs && (other->rfs != rfs)) ||
				(any_active(i2s) &&
				(((dir == SND_SOC_CLOCK_IN)
					&& !(mod & cdcon_mask)) ||
				((dir == SND_SOC_CLOCK_OUT)
					&& (mod & cdcon_mask))))) {
			dev_err(&i2s->pdev->dev,
				"%s:%d Other DAI busy\n", __func__, __LINE__);
			return -EAGAIN;
		}

		if (dir == SND_SOC_CLOCK_IN)
			mod |= 1 << i2s_regs->cdclkcon_off;
		else
			mod &= ~(1 << i2s_regs->cdclkcon_off);

		i2s->rfs = rfs;
		break;

	case SAMSUNG_I2S_RCLKSRC_0: /* clock corrsponding to IISMOD[10] := 0 */
	case SAMSUNG_I2S_RCLKSRC_1: /* clock corrsponding to IISMOD[10] := 1 */
		if ((i2s->quirks & QUIRK_NO_MUXPSR)
				|| (clk_id == SAMSUNG_I2S_RCLKSRC_0))
			clk_id = 0;
		else
			clk_id = 1;

		if (!any_active(i2s)) {
			if (i2s->op_clk && !IS_ERR(i2s->op_clk)) {
				if ((clk_id && !(mod & rsrc_mask)) ||
					(!clk_id && (mod & rsrc_mask))) {
					clk_disable_unprepare(i2s->op_clk);
					clk_put(i2s->op_clk);
				} else {
					i2s->rclk_srcrate =
						clk_get_rate(i2s->op_clk);
					return 0;
				}
			}

			if (clk_id)
				i2s->op_clk = clk_get(&i2s->pdev->dev,
						"i2s_opclk1");
			else
				i2s->op_clk = clk_get(&i2s->pdev->dev,
						"i2s_opclk0");

			if (WARN_ON(IS_ERR(i2s->op_clk)))
				return PTR_ERR(i2s->op_clk);

			clk_prepare_enable(i2s->op_clk);
			i2s->rclk_srcrate = clk_get_rate(i2s->op_clk);

			/* Over-ride the other's */
			if (other) {
				other->op_clk = i2s->op_clk;
				other->rclk_srcrate = i2s->rclk_srcrate;
			}
		} else if ((!clk_id && (mod & rsrc_mask))
				|| (clk_id && !(mod & rsrc_mask))) {
			dev_err(&i2s->pdev->dev,
				"%s:%d Other DAI busy\n", __func__, __LINE__);
			return -EAGAIN;
		} else {
			/* Call can't be on the active DAI */
			i2s->op_clk = other->op_clk;
			i2s->rclk_srcrate = other->rclk_srcrate;
			return 0;
		}

		if (clk_id == 0)
			mod &= ~(1 << i2s_regs->rclksrc_off);
		else
			mod |= 1 << i2s_regs->rclksrc_off;

		break;
	default:
		dev_err(&i2s->pdev->dev, "We don't serve that!\n");
		return -EINVAL;
	}

	writel(mod, i2s->addr + I2SMOD);

	return 0;
}

static int i2s_set_fmt(struct snd_soc_dai *dai,
	unsigned int fmt)
{
	struct i2s_dai *i2s = to_info(dai);
	u32 mod = readl(i2s->addr + I2SMOD);
	int lrp_shift, sdf_shift, sdf_mask, lrp_rlow, mod_slave;
	u32 tmp = 0;

	lrp_shift = i2s->variant_regs->lrp_off;
	sdf_shift = i2s->variant_regs->sdf_off;
	mod_slave = 1 << i2s->variant_regs->mss_off;

	sdf_mask = MOD_SDF_MASK << sdf_shift;
	lrp_rlow = MOD_LR_RLOW << lrp_shift;

	/* Format is priority */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_RIGHT_J:
		tmp |= lrp_rlow;
		tmp |= (MOD_SDF_MSB << sdf_shift);
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		tmp |= lrp_rlow;
		tmp |= (MOD_SDF_LSB << sdf_shift);
		break;
	case SND_SOC_DAIFMT_I2S:
		tmp |= (MOD_SDF_IIS << sdf_shift);
		break;
	default:
		dev_err(&i2s->pdev->dev, "Format not supported\n");
		return -EINVAL;
	}

	/*
	 * INV flag is relative to the FORMAT flag - if set it simply
	 * flips the polarity specified by the Standard
	 */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_NB_IF:
		if (tmp & lrp_rlow)
			tmp &= ~lrp_rlow;
		else
			tmp |= lrp_rlow;
		break;
	default:
		dev_err(&i2s->pdev->dev, "Polarity not supported\n");
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		tmp |= mod_slave;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		/* Set default source clock in Master mode */
		if (i2s->rclk_srcrate == 0)
			i2s_set_sysclk(dai, SAMSUNG_I2S_RCLKSRC_0,
							0, SND_SOC_CLOCK_IN);
		break;
	default:
		dev_err(&i2s->pdev->dev, "master/slave format not supported\n");
		return -EINVAL;
	}

	/*
	 * Don't change the I2S mode if any controller is active on this
	 * channel.
	 */
	if (any_active(i2s) &&
		((mod & (sdf_mask | lrp_rlow | mod_slave)) != tmp)) {
		dev_err(&i2s->pdev->dev,
				"%s:%d Other DAI busy\n", __func__, __LINE__);
		return -EAGAIN;
	}

	mod &= ~(sdf_mask | lrp_rlow | mod_slave);
	mod |= tmp;
	writel(mod, i2s->addr + I2SMOD);

	return 0;
}

static int i2s_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct i2s_dai *i2s = to_info(dai);
	u32 mod = readl(i2s->addr + I2SMOD);

	if (!is_secondary(i2s))
		mod &= ~(MOD_DC2_EN | MOD_DC1_EN);

	switch (params_channels(params)) {
	case 6:
		mod |= MOD_DC2_EN;
	case 4:
		mod |= MOD_DC1_EN;
		break;
	case 2:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			i2s->dma_playback.dma_size = 4;
		else
			i2s->dma_capture.dma_size = 4;
		break;
	case 1:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			i2s->dma_playback.dma_size = 2;
		else
			i2s->dma_capture.dma_size = 2;

		break;
	default:
		dev_err(&i2s->pdev->dev, "%d channels not supported\n",
				params_channels(params));
		return -EINVAL;
	}

	if (is_secondary(i2s))
		mod &= ~MOD_BLCS_MASK;
	else
		mod &= ~MOD_BLCP_MASK;

	if (is_manager(i2s))
		mod &= ~MOD_BLC_MASK;

	switch (params_width(params)) {
	case 8:
		if (is_secondary(i2s))
			mod |= MOD_BLCS_8BIT;
		else
			mod |= MOD_BLCP_8BIT;
		if (is_manager(i2s))
			mod |= MOD_BLC_8BIT;
		break;
	case 16:
		if (is_secondary(i2s))
			mod |= MOD_BLCS_16BIT;
		else
			mod |= MOD_BLCP_16BIT;
		if (is_manager(i2s))
			mod |= MOD_BLC_16BIT;
		break;
	case 24:
		if (is_secondary(i2s))
			mod |= MOD_BLCS_24BIT;
		else
			mod |= MOD_BLCP_24BIT;
		if (is_manager(i2s))
			mod |= MOD_BLC_24BIT;
		break;
	default:
		dev_err(&i2s->pdev->dev, "Format(%d) not supported\n",
				params_format(params));
		return -EINVAL;
	}
	writel(mod, i2s->addr + I2SMOD);

	samsung_asoc_init_dma_data(dai, &i2s->dma_playback, &i2s->dma_capture);

	i2s->frmclk = params_rate(params);

	return 0;
}

/* We set constraints on the substream acc to the version of I2S */
static int i2s_startup(struct snd_pcm_substream *substream,
	  struct snd_soc_dai *dai)
{
	struct i2s_dai *i2s = to_info(dai);
	struct i2s_dai *other = i2s->pri_dai ? : i2s->sec_dai;
	unsigned long flags;

	spin_lock_irqsave(&lock, flags);

	i2s->mode |= DAI_OPENED;

	if (is_manager(other))
		i2s->mode &= ~DAI_MANAGER;
	else
		i2s->mode |= DAI_MANAGER;

	if (!any_active(i2s) && (i2s->quirks & QUIRK_NEED_RSTCLR))
		writel(CON_RSTCLR, i2s->addr + I2SCON);

	spin_unlock_irqrestore(&lock, flags);

	if (!is_opened(other) && i2s->cdclk_out)
		i2s_set_sysclk(dai, SAMSUNG_I2S_CDCLK,
				0, SND_SOC_CLOCK_OUT);
	return 0;
}

static void i2s_shutdown(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	struct i2s_dai *i2s = to_info(dai);
	struct i2s_dai *other = i2s->pri_dai ? : i2s->sec_dai;
	unsigned long flags;
	const struct samsung_i2s_variant_regs *i2s_regs = i2s->variant_regs;

	spin_lock_irqsave(&lock, flags);

	i2s->mode &= ~DAI_OPENED;
	i2s->mode &= ~DAI_MANAGER;

	if (is_opened(other)) {
		other->mode |= DAI_MANAGER;
	} else {
		u32 mod = readl(i2s->addr + I2SMOD);
		i2s->cdclk_out = !(mod & (1 << i2s_regs->cdclkcon_off));
		if (other)
			other->cdclk_out = i2s->cdclk_out;
	}
	/* Reset any constraint on RFS and BFS */
	i2s->rfs = 0;
	i2s->bfs = 0;

	spin_unlock_irqrestore(&lock, flags);

	/* Gate CDCLK by default */
	if (!is_opened(other))
		i2s_set_sysclk(dai, SAMSUNG_I2S_CDCLK,
				0, SND_SOC_CLOCK_IN);
}

static int config_setup(struct i2s_dai *i2s)
{
	struct i2s_dai *other = i2s->pri_dai ? : i2s->sec_dai;
	unsigned rfs, bfs, blc;
	u32 psr;

	blc = get_blc(i2s);

	bfs = i2s->bfs;

	if (!bfs && other)
		bfs = other->bfs;

	/* Select least possible multiple(2) if no constraint set */
	if (!bfs)
		bfs = blc * 2;

	rfs = i2s->rfs;

	if (!rfs && other)
		rfs = other->rfs;

	if ((rfs == 256 || rfs == 512) && (blc == 24)) {
		dev_err(&i2s->pdev->dev,
			"%d-RFS not supported for 24-blc\n", rfs);
		return -EINVAL;
	}

	if (!rfs) {
		if (bfs == 16 || bfs == 32)
			rfs = 256;
		else
			rfs = 384;
	}

	/* If already setup and running */
	if (any_active(i2s) && (get_rfs(i2s) != rfs || get_bfs(i2s) != bfs)) {
		dev_err(&i2s->pdev->dev,
				"%s:%d Other DAI busy\n", __func__, __LINE__);
		return -EAGAIN;
	}

	set_bfs(i2s, bfs);
	set_rfs(i2s, rfs);

	/* Don't bother with PSR in Slave mode */
	if (is_slave(i2s))
		return 0;

	if (!(i2s->quirks & QUIRK_NO_MUXPSR)) {
		psr = i2s->rclk_srcrate / i2s->frmclk / rfs;
		writel(((psr - 1) << 8) | PSR_PSREN, i2s->addr + I2SPSR);
		dev_dbg(&i2s->pdev->dev,
			"RCLK_SRC=%luHz PSR=%u, RCLK=%dfs, BCLK=%dfs\n",
				i2s->rclk_srcrate, psr, rfs, bfs);
	}

	return 0;
}

static int i2s_trigger(struct snd_pcm_substream *substream,
	int cmd, struct snd_soc_dai *dai)
{
	int capture = (substream->stream == SNDRV_PCM_STREAM_CAPTURE);
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct i2s_dai *i2s = to_info(rtd->cpu_dai);
	unsigned long flags;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		local_irq_save(flags);

		if (config_setup(i2s)) {
			local_irq_restore(flags);
			return -EINVAL;
		}

		if (capture)
			i2s_rxctrl(i2s, 1);
		else
			i2s_txctrl(i2s, 1);

		local_irq_restore(flags);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		local_irq_save(flags);

		if (capture) {
			i2s_rxctrl(i2s, 0);
			i2s_fifo(i2s, FIC_RXFLUSH);
		} else {
			i2s_txctrl(i2s, 0);
			i2s_fifo(i2s, FIC_TXFLUSH);
		}

		local_irq_restore(flags);
		break;
	}

	return 0;
}

static int i2s_set_clkdiv(struct snd_soc_dai *dai,
	int div_id, int div)
{
	struct i2s_dai *i2s = to_info(dai);
	struct i2s_dai *other = i2s->pri_dai ? : i2s->sec_dai;

	switch (div_id) {
	case SAMSUNG_I2S_DIV_BCLK:
		if ((any_active(i2s) && div && (get_bfs(i2s) != div))
			|| (other && other->bfs && (other->bfs != div))) {
			dev_err(&i2s->pdev->dev,
				"%s:%d Other DAI busy\n", __func__, __LINE__);
			return -EAGAIN;
		}
		i2s->bfs = div;
		break;
	default:
		dev_err(&i2s->pdev->dev,
			"Invalid clock divider(%d)\n", div_id);
		return -EINVAL;
	}

	return 0;
}

static snd_pcm_sframes_t
i2s_delay(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct i2s_dai *i2s = to_info(dai);
	u32 reg = readl(i2s->addr + I2SFIC);
	snd_pcm_sframes_t delay;
	const struct samsung_i2s_variant_regs *i2s_regs = i2s->variant_regs;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		delay = FIC_RXCOUNT(reg);
	else if (is_secondary(i2s))
		delay = FICS_TXCOUNT(readl(i2s->addr + I2SFICS));
	else
		delay = (reg >> i2s_regs->ftx0cnt_off) & 0x7f;

	return delay;
}

#ifdef CONFIG_PM
static int i2s_suspend(struct snd_soc_dai *dai)
{
	struct i2s_dai *i2s = to_info(dai);

	i2s->suspend_i2smod = readl(i2s->addr + I2SMOD);
	i2s->suspend_i2scon = readl(i2s->addr + I2SCON);
	i2s->suspend_i2spsr = readl(i2s->addr + I2SPSR);

	return 0;
}

static int i2s_resume(struct snd_soc_dai *dai)
{
	struct i2s_dai *i2s = to_info(dai);

	writel(i2s->suspend_i2scon, i2s->addr + I2SCON);
	writel(i2s->suspend_i2smod, i2s->addr + I2SMOD);
	writel(i2s->suspend_i2spsr, i2s->addr + I2SPSR);

	return 0;
}
#else
#define i2s_suspend NULL
#define i2s_resume  NULL
#endif

static int samsung_i2s_dai_probe(struct snd_soc_dai *dai)
{
	struct i2s_dai *i2s = to_info(dai);
	struct i2s_dai *other = i2s->pri_dai ? : i2s->sec_dai;
	int ret;

	if (other && other->clk) { /* If this is probe on secondary */
		samsung_asoc_init_dma_data(dai, &other->sec_dai->dma_playback,
					   NULL);
		goto probe_exit;
	}

	i2s->addr = ioremap(i2s->base, 0x100);
	if (i2s->addr == NULL) {
		dev_err(&i2s->pdev->dev, "cannot ioremap registers\n");
		return -ENXIO;
	}

	i2s->clk = clk_get(&i2s->pdev->dev, "iis");
	if (IS_ERR(i2s->clk)) {
		dev_err(&i2s->pdev->dev, "failed to get i2s_clock\n");
		iounmap(i2s->addr);
		return PTR_ERR(i2s->clk);
	}

	ret = clk_prepare_enable(i2s->clk);
	if (ret != 0) {
		dev_err(&i2s->pdev->dev, "failed to enable clock: %d\n", ret);
		return ret;
	}

	samsung_asoc_init_dma_data(dai, &i2s->dma_playback, &i2s->dma_capture);

	if (other) {
		other->addr = i2s->addr;
		other->clk = i2s->clk;
	}

	if (i2s->quirks & QUIRK_NEED_RSTCLR)
		writel(CON_RSTCLR, i2s->addr + I2SCON);

	if (i2s->quirks & QUIRK_SUPPORTS_IDMA)
		idma_reg_addr_init(i2s->addr,
					i2s->sec_dai->idma_playback.dma_addr);

probe_exit:
	/* Reset any constraint on RFS and BFS */
	i2s->rfs = 0;
	i2s->bfs = 0;
	i2s->rclk_srcrate = 0;
	i2s_txctrl(i2s, 0);
	i2s_rxctrl(i2s, 0);
	i2s_fifo(i2s, FIC_TXFLUSH);
	i2s_fifo(other, FIC_TXFLUSH);
	i2s_fifo(i2s, FIC_RXFLUSH);

	/* Gate CDCLK by default */
	if (!is_opened(other))
		i2s_set_sysclk(dai, SAMSUNG_I2S_CDCLK,
				0, SND_SOC_CLOCK_IN);

	return 0;
}

static int samsung_i2s_dai_remove(struct snd_soc_dai *dai)
{
	struct i2s_dai *i2s = snd_soc_dai_get_drvdata(dai);
	struct i2s_dai *other = i2s->pri_dai ? : i2s->sec_dai;

	if (!other || !other->clk) {

		if (i2s->quirks & QUIRK_NEED_RSTCLR)
			writel(0, i2s->addr + I2SCON);

		clk_disable_unprepare(i2s->clk);
		clk_put(i2s->clk);

		iounmap(i2s->addr);
	}

	i2s->clk = NULL;

	return 0;
}

static const struct snd_soc_dai_ops samsung_i2s_dai_ops = {
	.trigger = i2s_trigger,
	.hw_params = i2s_hw_params,
	.set_fmt = i2s_set_fmt,
	.set_clkdiv = i2s_set_clkdiv,
	.set_sysclk = i2s_set_sysclk,
	.startup = i2s_startup,
	.shutdown = i2s_shutdown,
	.delay = i2s_delay,
};

static const struct snd_soc_component_driver samsung_i2s_component = {
	.name		= "samsung-i2s",
};

#define SAMSUNG_I2S_RATES	SNDRV_PCM_RATE_8000_96000

#define SAMSUNG_I2S_FMTS	(SNDRV_PCM_FMTBIT_S8 | \
					SNDRV_PCM_FMTBIT_S16_LE | \
					SNDRV_PCM_FMTBIT_S24_LE)

static struct i2s_dai *i2s_alloc_dai(struct platform_device *pdev, bool sec)
{
	struct i2s_dai *i2s;
	int ret;

	i2s = devm_kzalloc(&pdev->dev, sizeof(struct i2s_dai), GFP_KERNEL);
	if (i2s == NULL)
		return NULL;

	i2s->pdev = pdev;
	i2s->pri_dai = NULL;
	i2s->sec_dai = NULL;
	i2s->i2s_dai_drv.symmetric_rates = 1;
	i2s->i2s_dai_drv.probe = samsung_i2s_dai_probe;
	i2s->i2s_dai_drv.remove = samsung_i2s_dai_remove;
	i2s->i2s_dai_drv.ops = &samsung_i2s_dai_ops;
	i2s->i2s_dai_drv.suspend = i2s_suspend;
	i2s->i2s_dai_drv.resume = i2s_resume;
	i2s->i2s_dai_drv.playback.channels_min = 1;
	i2s->i2s_dai_drv.playback.channels_max = 2;
	i2s->i2s_dai_drv.playback.rates = SAMSUNG_I2S_RATES;
	i2s->i2s_dai_drv.playback.formats = SAMSUNG_I2S_FMTS;

	if (!sec) {
		i2s->i2s_dai_drv.capture.channels_min = 1;
		i2s->i2s_dai_drv.capture.channels_max = 2;
		i2s->i2s_dai_drv.capture.rates = SAMSUNG_I2S_RATES;
		i2s->i2s_dai_drv.capture.formats = SAMSUNG_I2S_FMTS;
		dev_set_drvdata(&i2s->pdev->dev, i2s);
	} else {	/* Create a new platform_device for Secondary */
		i2s->pdev = platform_device_alloc("samsung-i2s-sec", -1);
		if (!i2s->pdev)
			return NULL;

		i2s->pdev->dev.parent = &pdev->dev;

		platform_set_drvdata(i2s->pdev, i2s);
		ret = platform_device_add(i2s->pdev);
		if (ret < 0)
			return NULL;
	}

	return i2s;
}

static const struct of_device_id exynos_i2s_match[];

static inline const struct samsung_i2s_dai_data *samsung_i2s_get_driver_data(
						struct platform_device *pdev)
{
#ifdef CONFIG_OF
	if (pdev->dev.of_node) {
		const struct of_device_id *match;
		match = of_match_node(exynos_i2s_match, pdev->dev.of_node);
		return match->data;
	} else
#endif
		return (struct samsung_i2s_dai_data *)
				platform_get_device_id(pdev)->driver_data;
}

#ifdef CONFIG_PM_RUNTIME
static int i2s_runtime_suspend(struct device *dev)
{
	struct i2s_dai *i2s = dev_get_drvdata(dev);

	clk_disable_unprepare(i2s->clk);

	return 0;
}

static int i2s_runtime_resume(struct device *dev)
{
	struct i2s_dai *i2s = dev_get_drvdata(dev);

	clk_prepare_enable(i2s->clk);

	return 0;
}
#endif /* CONFIG_PM_RUNTIME */

static int samsung_i2s_probe(struct platform_device *pdev)
{
	struct i2s_dai *pri_dai, *sec_dai = NULL;
	struct s3c_audio_pdata *i2s_pdata = pdev->dev.platform_data;
	struct samsung_i2s *i2s_cfg = NULL;
	struct resource *res;
	u32 regs_base, quirks = 0, idma_addr = 0;
	struct device_node *np = pdev->dev.of_node;
	const struct samsung_i2s_dai_data *i2s_dai_data;
	int ret = 0;

	/* Call during Seconday interface registration */
	i2s_dai_data = samsung_i2s_get_driver_data(pdev);

	if (i2s_dai_data->dai_type == TYPE_SEC) {
		sec_dai = dev_get_drvdata(&pdev->dev);
		if (!sec_dai) {
			dev_err(&pdev->dev, "Unable to get drvdata\n");
			return -EFAULT;
		}
		devm_snd_soc_register_component(&sec_dai->pdev->dev,
						&samsung_i2s_component,
						&sec_dai->i2s_dai_drv, 1);
		samsung_asoc_dma_platform_register(&pdev->dev);
		return 0;
	}

	pri_dai = i2s_alloc_dai(pdev, false);
	if (!pri_dai) {
		dev_err(&pdev->dev, "Unable to alloc I2S_pri\n");
		return -ENOMEM;
	}

	if (!np) {
		res = platform_get_resource(pdev, IORESOURCE_DMA, 0);
		if (!res) {
			dev_err(&pdev->dev,
				"Unable to get I2S-TX dma resource\n");
			return -ENXIO;
		}
		pri_dai->dma_playback.channel = res->start;

		res = platform_get_resource(pdev, IORESOURCE_DMA, 1);
		if (!res) {
			dev_err(&pdev->dev,
				"Unable to get I2S-RX dma resource\n");
			return -ENXIO;
		}
		pri_dai->dma_capture.channel = res->start;

		if (i2s_pdata == NULL) {
			dev_err(&pdev->dev, "Can't work without s3c_audio_pdata\n");
			return -EINVAL;
		}

		if (&i2s_pdata->type)
			i2s_cfg = &i2s_pdata->type.i2s;

		if (i2s_cfg) {
			quirks = i2s_cfg->quirks;
			idma_addr = i2s_cfg->idma_addr;
		}
	} else {
		quirks = i2s_dai_data->quirks;
		if (of_property_read_u32(np, "samsung,idma-addr",
					 &idma_addr)) {
			if (quirks & QUIRK_SUPPORTS_IDMA) {
				dev_info(&pdev->dev, "idma address is not"\
						"specified");
			}
		}
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Unable to get I2S SFR address\n");
		return -ENXIO;
	}

	if (!request_mem_region(res->start, resource_size(res),
							"samsung-i2s")) {
		dev_err(&pdev->dev, "Unable to request SFR region\n");
		return -EBUSY;
	}
	regs_base = res->start;

	pri_dai->dma_playback.dma_addr = regs_base + I2STXD;
	pri_dai->dma_capture.dma_addr = regs_base + I2SRXD;
	pri_dai->dma_playback.ch_name = "tx";
	pri_dai->dma_capture.ch_name = "rx";
	pri_dai->dma_playback.dma_size = 4;
	pri_dai->dma_capture.dma_size = 4;
	pri_dai->base = regs_base;
	pri_dai->quirks = quirks;
	pri_dai->variant_regs = i2s_dai_data->i2s_variant_regs;

	if (quirks & QUIRK_PRI_6CHAN)
		pri_dai->i2s_dai_drv.playback.channels_max = 6;

	if (quirks & QUIRK_SEC_DAI) {
		sec_dai = i2s_alloc_dai(pdev, true);
		if (!sec_dai) {
			dev_err(&pdev->dev, "Unable to alloc I2S_sec\n");
			ret = -ENOMEM;
			goto err;
		}

		sec_dai->variant_regs = pri_dai->variant_regs;
		sec_dai->dma_playback.dma_addr = regs_base + I2STXDS;
		sec_dai->dma_playback.ch_name = "tx-sec";

		if (!np) {
			res = platform_get_resource(pdev, IORESOURCE_DMA, 2);
			if (res)
				sec_dai->dma_playback.channel = res->start;
		}

		sec_dai->dma_playback.dma_size = 4;
		sec_dai->base = regs_base;
		sec_dai->quirks = quirks;
		sec_dai->idma_playback.dma_addr = idma_addr;
		sec_dai->pri_dai = pri_dai;
		pri_dai->sec_dai = sec_dai;
	}

	if (i2s_pdata && i2s_pdata->cfg_gpio && i2s_pdata->cfg_gpio(pdev)) {
		dev_err(&pdev->dev, "Unable to configure gpio\n");
		ret = -EINVAL;
		goto err;
	}

	devm_snd_soc_register_component(&pri_dai->pdev->dev,
					&samsung_i2s_component,
					&pri_dai->i2s_dai_drv, 1);

	pm_runtime_enable(&pdev->dev);

	samsung_asoc_dma_platform_register(&pdev->dev);

	return 0;
err:
	if (res)
		release_mem_region(regs_base, resource_size(res));

	return ret;
}

static int samsung_i2s_remove(struct platform_device *pdev)
{
	struct i2s_dai *i2s, *other;
	struct resource *res;

	i2s = dev_get_drvdata(&pdev->dev);
	other = i2s->pri_dai ? : i2s->sec_dai;

	if (other) {
		other->pri_dai = NULL;
		other->sec_dai = NULL;
	} else {
		pm_runtime_disable(&pdev->dev);
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (res)
			release_mem_region(res->start, resource_size(res));
	}

	i2s->pri_dai = NULL;
	i2s->sec_dai = NULL;

	return 0;
}

static const struct samsung_i2s_variant_regs i2sv3_regs = {
	.bfs_off = 1,
	.rfs_off = 3,
	.sdf_off = 5,
	.txr_off = 8,
	.rclksrc_off = 10,
	.mss_off = 11,
	.cdclkcon_off = 12,
	.lrp_off = 7,
	.bfs_mask = 0x3,
	.rfs_mask = 0x3,
	.ftx0cnt_off = 8,
};

static const struct samsung_i2s_variant_regs i2sv6_regs = {
	.bfs_off = 0,
	.rfs_off = 4,
	.sdf_off = 6,
	.txr_off = 8,
	.rclksrc_off = 10,
	.mss_off = 11,
	.cdclkcon_off = 12,
	.lrp_off = 15,
	.bfs_mask = 0xf,
	.rfs_mask = 0x3,
	.ftx0cnt_off = 8,
};

static const struct samsung_i2s_variant_regs i2sv7_regs = {
	.bfs_off = 0,
	.rfs_off = 4,
	.sdf_off = 7,
	.txr_off = 9,
	.rclksrc_off = 11,
	.mss_off = 12,
	.cdclkcon_off = 22,
	.lrp_off = 15,
	.bfs_mask = 0xf,
	.rfs_mask = 0x7,
	.ftx0cnt_off = 0,
};

static const struct samsung_i2s_variant_regs i2sv5_i2s1_regs = {
	.bfs_off = 0,
	.rfs_off = 3,
	.sdf_off = 6,
	.txr_off = 8,
	.rclksrc_off = 10,
	.mss_off = 11,
	.cdclkcon_off = 12,
	.lrp_off = 15,
	.bfs_mask = 0x7,
	.rfs_mask = 0x7,
	.ftx0cnt_off = 8,
};

static const struct samsung_i2s_dai_data i2sv3_dai_type = {
	.dai_type = TYPE_PRI,
	.quirks = QUIRK_NO_MUXPSR,
	.i2s_variant_regs = &i2sv3_regs,
};

static const struct samsung_i2s_dai_data i2sv5_dai_type = {
	.dai_type = TYPE_PRI,
	.quirks = QUIRK_PRI_6CHAN | QUIRK_SEC_DAI | QUIRK_NEED_RSTCLR |
			QUIRK_SUPPORTS_IDMA,
	.i2s_variant_regs = &i2sv3_regs,
};

static const struct samsung_i2s_dai_data i2sv6_dai_type = {
	.dai_type = TYPE_PRI,
	.quirks = QUIRK_PRI_6CHAN | QUIRK_SEC_DAI | QUIRK_NEED_RSTCLR |
			QUIRK_SUPPORTS_TDM | QUIRK_SUPPORTS_IDMA,
	.i2s_variant_regs = &i2sv6_regs,
};

static const struct samsung_i2s_dai_data i2sv7_dai_type = {
	.dai_type = TYPE_PRI,
	.quirks = QUIRK_PRI_6CHAN | QUIRK_SEC_DAI | QUIRK_NEED_RSTCLR |
			QUIRK_SUPPORTS_TDM,
	.i2s_variant_regs = &i2sv7_regs,
};

static const struct samsung_i2s_dai_data i2sv5_dai_type_i2s1 = {
	.dai_type = TYPE_PRI,
	.quirks = QUIRK_PRI_6CHAN | QUIRK_NEED_RSTCLR,
	.i2s_variant_regs = &i2sv5_i2s1_regs,
};

static const struct samsung_i2s_dai_data samsung_dai_type_pri = {
	.dai_type = TYPE_PRI,
};

static const struct samsung_i2s_dai_data samsung_dai_type_sec = {
	.dai_type = TYPE_SEC,
};

static struct platform_device_id samsung_i2s_driver_ids[] = {
	{
		.name           = "samsung-i2s",
		.driver_data	= (kernel_ulong_t)&i2sv3_dai_type,
	}, {
		.name           = "samsung-i2s-sec",
		.driver_data    = (kernel_ulong_t)&samsung_dai_type_sec,
	}, {
		.name		= "samsung-i2sv4",
		.driver_data	= (kernel_ulong_t)&i2sv5_dai_type,
	},
	{},
};
MODULE_DEVICE_TABLE(platform, samsung_i2s_driver_ids);

#ifdef CONFIG_OF
static const struct of_device_id exynos_i2s_match[] = {
	{
		.compatible = "samsung,s3c6410-i2s",
		.data = &i2sv3_dai_type,
	}, {
		.compatible = "samsung,s5pv210-i2s",
		.data = &i2sv5_dai_type,
	}, {
		.compatible = "samsung,exynos5420-i2s",
		.data = &i2sv6_dai_type,
	}, {
		.compatible = "samsung,exynos7-i2s",
		.data = &i2sv7_dai_type,
	}, {
		.compatible = "samsung,exynos7-i2s1",
		.data = &i2sv5_dai_type_i2s1,
	},
	{},
};
MODULE_DEVICE_TABLE(of, exynos_i2s_match);
#endif

static const struct dev_pm_ops samsung_i2s_pm = {
	SET_RUNTIME_PM_OPS(i2s_runtime_suspend,
				i2s_runtime_resume, NULL)
};

static struct platform_driver samsung_i2s_driver = {
	.probe  = samsung_i2s_probe,
	.remove = samsung_i2s_remove,
	.id_table = samsung_i2s_driver_ids,
	.driver = {
		.name = "samsung-i2s",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(exynos_i2s_match),
		.pm = &samsung_i2s_pm,
	},
};

module_platform_driver(samsung_i2s_driver);

/* Module information */
MODULE_AUTHOR("Jaswinder Singh, <jassisinghbrar@gmail.com>");
MODULE_DESCRIPTION("Samsung I2S Interface");
MODULE_ALIAS("platform:samsung-i2s");
MODULE_LICENSE("GPL");
