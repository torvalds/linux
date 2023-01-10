// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * kirkwood-i2s.c
 *
 * (c) 2010 Arnaud Patard <apatard@mandriva.com>
 * (c) 2010 Arnaud Patard <arnaud.patard@rtp-net.org>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/mbus.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <linux/platform_data/asoc-kirkwood.h>
#include <linux/of.h>

#include "kirkwood.h"

#define KIRKWOOD_I2S_FORMATS \
	(SNDRV_PCM_FMTBIT_S16_LE | \
	 SNDRV_PCM_FMTBIT_S24_LE | \
	 SNDRV_PCM_FMTBIT_S32_LE)

#define KIRKWOOD_SPDIF_FORMATS \
	(SNDRV_PCM_FMTBIT_S16_LE | \
	 SNDRV_PCM_FMTBIT_S24_LE)

/* These registers are relative to the second register region -
 * audio pll configuration.
 */
#define A38X_PLL_CONF_REG0			0x0
#define     A38X_PLL_FB_CLK_DIV_OFFSET		10
#define     A38X_PLL_FB_CLK_DIV_MASK		0x7fc00
#define A38X_PLL_CONF_REG1			0x4
#define     A38X_PLL_FREQ_OFFSET_MASK		0xffff
#define     A38X_PLL_FREQ_OFFSET_VALID		BIT(16)
#define     A38X_PLL_SW_RESET			BIT(31)
#define A38X_PLL_CONF_REG2			0x8
#define     A38X_PLL_AUDIO_POSTDIV_MASK		0x7f

/* Bit below belongs to SoC control register corresponding to the third
 * register region.
 */
#define A38X_SPDIF_MODE_ENABLE			BIT(27)

static int armada_38x_i2s_init_quirk(struct platform_device *pdev,
				     struct kirkwood_dma_data *priv,
				     struct snd_soc_dai_driver *dai_drv)
{
	struct device_node *np = pdev->dev.of_node;
	u32 reg_val;
	int i;

	priv->pll_config = devm_platform_ioremap_resource_byname(pdev, "pll_regs");
	if (IS_ERR(priv->pll_config))
		return -ENOMEM;

	priv->soc_control = devm_platform_ioremap_resource_byname(pdev, "soc_ctrl");
	if (IS_ERR(priv->soc_control))
		return -ENOMEM;

	/* Select one of exceptive modes: I2S or S/PDIF */
	reg_val = readl(priv->soc_control);
	if (of_property_read_bool(np, "spdif-mode")) {
		reg_val |= A38X_SPDIF_MODE_ENABLE;
		dev_info(&pdev->dev, "using S/PDIF mode\n");
	} else {
		reg_val &= ~A38X_SPDIF_MODE_ENABLE;
		dev_info(&pdev->dev, "using I2S mode\n");
	}
	writel(reg_val, priv->soc_control);

	/* Update available rates of mclk's fs */
	for (i = 0; i < 2; i++) {
		dai_drv[i].playback.rates |= SNDRV_PCM_RATE_192000;
		dai_drv[i].capture.rates |= SNDRV_PCM_RATE_192000;
	}

	return 0;
}

static inline void armada_38x_set_pll(void __iomem *base, unsigned long rate)
{
	u32 reg_val;
	u16 freq_offset = 0x22b0;
	u8 audio_postdiv, fb_clk_div = 0x1d;

	/* Set frequency offset value to not valid and enable PLL reset */
	reg_val = readl(base + A38X_PLL_CONF_REG1);
	reg_val &= ~A38X_PLL_FREQ_OFFSET_VALID;
	reg_val &= ~A38X_PLL_SW_RESET;
	writel(reg_val, base + A38X_PLL_CONF_REG1);

	udelay(1);

	/* Update PLL parameters */
	switch (rate) {
	default:
	case 44100:
		freq_offset = 0x735;
		fb_clk_div = 0x1b;
		audio_postdiv = 0xc;
		break;
	case 48000:
		audio_postdiv = 0xc;
		break;
	case 96000:
		audio_postdiv = 0x6;
		break;
	case 192000:
		audio_postdiv = 0x3;
		break;
	}

	reg_val = readl(base + A38X_PLL_CONF_REG0);
	reg_val &= ~A38X_PLL_FB_CLK_DIV_MASK;
	reg_val |= (fb_clk_div << A38X_PLL_FB_CLK_DIV_OFFSET);
	writel(reg_val, base + A38X_PLL_CONF_REG0);

	reg_val = readl(base + A38X_PLL_CONF_REG2);
	reg_val &= ~A38X_PLL_AUDIO_POSTDIV_MASK;
	reg_val |= audio_postdiv;
	writel(reg_val, base + A38X_PLL_CONF_REG2);

	reg_val = readl(base + A38X_PLL_CONF_REG1);
	reg_val &= ~A38X_PLL_FREQ_OFFSET_MASK;
	reg_val |= freq_offset;
	writel(reg_val, base + A38X_PLL_CONF_REG1);

	udelay(1);

	/* Disable reset */
	reg_val |= A38X_PLL_SW_RESET;
	writel(reg_val, base + A38X_PLL_CONF_REG1);

	/* Wait 50us for PLL to lock */
	udelay(50);

	/* Restore frequency offset value validity */
	reg_val |= A38X_PLL_FREQ_OFFSET_VALID;
	writel(reg_val, base + A38X_PLL_CONF_REG1);
}

static int kirkwood_i2s_set_fmt(struct snd_soc_dai *cpu_dai,
		unsigned int fmt)
{
	struct kirkwood_dma_data *priv = snd_soc_dai_get_drvdata(cpu_dai);
	unsigned long mask;
	unsigned long value;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_RIGHT_J:
		mask = KIRKWOOD_I2S_CTL_RJ;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		mask = KIRKWOOD_I2S_CTL_LJ;
		break;
	case SND_SOC_DAIFMT_I2S:
		mask = KIRKWOOD_I2S_CTL_I2S;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * Set same format for playback and record
	 * This avoids some troubles.
	 */
	value = readl(priv->io+KIRKWOOD_I2S_PLAYCTL);
	value &= ~KIRKWOOD_I2S_CTL_JUST_MASK;
	value |= mask;
	writel(value, priv->io+KIRKWOOD_I2S_PLAYCTL);

	value = readl(priv->io+KIRKWOOD_I2S_RECCTL);
	value &= ~KIRKWOOD_I2S_CTL_JUST_MASK;
	value |= mask;
	writel(value, priv->io+KIRKWOOD_I2S_RECCTL);

	return 0;
}

static inline void kirkwood_set_dco(void __iomem *io, unsigned long rate)
{
	unsigned long value;

	value = KIRKWOOD_DCO_CTL_OFFSET_0;
	switch (rate) {
	default:
	case 44100:
		value |= KIRKWOOD_DCO_CTL_FREQ_11;
		break;
	case 48000:
		value |= KIRKWOOD_DCO_CTL_FREQ_12;
		break;
	case 96000:
		value |= KIRKWOOD_DCO_CTL_FREQ_24;
		break;
	}
	writel(value, io + KIRKWOOD_DCO_CTL);

	/* wait for dco locked */
	do {
		cpu_relax();
		value = readl(io + KIRKWOOD_DCO_SPCR_STATUS);
		value &= KIRKWOOD_DCO_SPCR_STATUS_DCO_LOCK;
	} while (value == 0);
}

static void kirkwood_set_rate(struct snd_soc_dai *dai,
	struct kirkwood_dma_data *priv, unsigned long rate)
{
	uint32_t clks_ctrl;

	if (IS_ERR(priv->extclk)) {
		/* use internal dco for the supported rates
		 * defined in kirkwood_i2s_dai */
		dev_dbg(dai->dev, "%s: dco set rate = %lu\n",
			__func__, rate);
		if (priv->pll_config)
			armada_38x_set_pll(priv->pll_config, rate);
		else
			kirkwood_set_dco(priv->io, rate);

		clks_ctrl = KIRKWOOD_MCLK_SOURCE_DCO;
	} else {
		/* use the external clock for the other rates
		 * defined in kirkwood_i2s_dai_extclk */
		dev_dbg(dai->dev, "%s: extclk set rate = %lu -> %lu\n",
			__func__, rate, 256 * rate);
		clk_set_rate(priv->extclk, 256 * rate);

		clks_ctrl = KIRKWOOD_MCLK_SOURCE_EXTCLK;
	}
	writel(clks_ctrl, priv->io + KIRKWOOD_CLOCKS_CTRL);
}

static int kirkwood_i2s_startup(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct kirkwood_dma_data *priv = snd_soc_dai_get_drvdata(dai);

	snd_soc_dai_set_dma_data(dai, substream, priv);
	return 0;
}

static int kirkwood_i2s_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct kirkwood_dma_data *priv = snd_soc_dai_get_drvdata(dai);
	uint32_t ctl_play, ctl_rec;
	unsigned int i2s_reg;
	unsigned long i2s_value;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		i2s_reg = KIRKWOOD_I2S_PLAYCTL;
	} else {
		i2s_reg = KIRKWOOD_I2S_RECCTL;
	}

	kirkwood_set_rate(dai, priv, params_rate(params));

	i2s_value = readl(priv->io+i2s_reg);
	i2s_value &= ~KIRKWOOD_I2S_CTL_SIZE_MASK;

	/*
	 * Size settings in play/rec i2s control regs and play/rec control
	 * regs must be the same.
	 */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		i2s_value |= KIRKWOOD_I2S_CTL_SIZE_16;
		ctl_play = KIRKWOOD_PLAYCTL_SIZE_16_C |
			   KIRKWOOD_PLAYCTL_I2S_EN |
			   KIRKWOOD_PLAYCTL_SPDIF_EN;
		ctl_rec = KIRKWOOD_RECCTL_SIZE_16_C |
			  KIRKWOOD_RECCTL_I2S_EN |
			  KIRKWOOD_RECCTL_SPDIF_EN;
		break;
	/*
	 * doesn't work... S20_3LE != kirkwood 20bit format ?
	 *
	case SNDRV_PCM_FORMAT_S20_3LE:
		i2s_value |= KIRKWOOD_I2S_CTL_SIZE_20;
		ctl_play = KIRKWOOD_PLAYCTL_SIZE_20 |
			   KIRKWOOD_PLAYCTL_I2S_EN;
		ctl_rec = KIRKWOOD_RECCTL_SIZE_20 |
			  KIRKWOOD_RECCTL_I2S_EN;
		break;
	*/
	case SNDRV_PCM_FORMAT_S24_LE:
		i2s_value |= KIRKWOOD_I2S_CTL_SIZE_24;
		ctl_play = KIRKWOOD_PLAYCTL_SIZE_24 |
			   KIRKWOOD_PLAYCTL_I2S_EN |
			   KIRKWOOD_PLAYCTL_SPDIF_EN;
		ctl_rec = KIRKWOOD_RECCTL_SIZE_24 |
			  KIRKWOOD_RECCTL_I2S_EN |
			  KIRKWOOD_RECCTL_SPDIF_EN;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		i2s_value |= KIRKWOOD_I2S_CTL_SIZE_32;
		ctl_play = KIRKWOOD_PLAYCTL_SIZE_32 |
			   KIRKWOOD_PLAYCTL_I2S_EN;
		ctl_rec = KIRKWOOD_RECCTL_SIZE_32 |
			  KIRKWOOD_RECCTL_I2S_EN;
		break;
	default:
		return -EINVAL;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (params_channels(params) == 1)
			ctl_play |= KIRKWOOD_PLAYCTL_MONO_BOTH;
		else
			ctl_play |= KIRKWOOD_PLAYCTL_MONO_OFF;

		priv->ctl_play &= ~(KIRKWOOD_PLAYCTL_MONO_MASK |
				    KIRKWOOD_PLAYCTL_ENABLE_MASK |
				    KIRKWOOD_PLAYCTL_SIZE_MASK);
		priv->ctl_play |= ctl_play;
	} else {
		priv->ctl_rec &= ~(KIRKWOOD_RECCTL_ENABLE_MASK |
				   KIRKWOOD_RECCTL_SIZE_MASK);
		priv->ctl_rec |= ctl_rec;
	}

	writel(i2s_value, priv->io+i2s_reg);

	return 0;
}

static unsigned kirkwood_i2s_play_mute(unsigned ctl)
{
	if (!(ctl & KIRKWOOD_PLAYCTL_I2S_EN))
		ctl |= KIRKWOOD_PLAYCTL_I2S_MUTE;
	if (!(ctl & KIRKWOOD_PLAYCTL_SPDIF_EN))
		ctl |= KIRKWOOD_PLAYCTL_SPDIF_MUTE;
	return ctl;
}

static int kirkwood_i2s_play_trigger(struct snd_pcm_substream *substream,
				int cmd, struct snd_soc_dai *dai)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct kirkwood_dma_data *priv = snd_soc_dai_get_drvdata(dai);
	uint32_t ctl, value;

	ctl = readl(priv->io + KIRKWOOD_PLAYCTL);
	if ((ctl & KIRKWOOD_PLAYCTL_ENABLE_MASK) == 0) {
		unsigned timeout = 5000;
		/*
		 * The Armada510 spec says that if we enter pause mode, the
		 * busy bit must be read back as clear _twice_.  Make sure
		 * we respect that otherwise we get DMA underruns.
		 */
		do {
			value = ctl;
			ctl = readl(priv->io + KIRKWOOD_PLAYCTL);
			if (!((ctl | value) & KIRKWOOD_PLAYCTL_PLAY_BUSY))
				break;
			udelay(1);
		} while (timeout--);

		if ((ctl | value) & KIRKWOOD_PLAYCTL_PLAY_BUSY)
			dev_notice(dai->dev, "timed out waiting for busy to deassert: %08x\n",
				   ctl);
	}

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		/* configure */
		ctl = priv->ctl_play;
		if (dai->id == 0)
			ctl &= ~KIRKWOOD_PLAYCTL_SPDIF_EN;	/* i2s */
		else
			ctl &= ~KIRKWOOD_PLAYCTL_I2S_EN;	/* spdif */
		ctl = kirkwood_i2s_play_mute(ctl);
		value = ctl & ~KIRKWOOD_PLAYCTL_ENABLE_MASK;
		writel(value, priv->io + KIRKWOOD_PLAYCTL);

		/* enable interrupts */
		if (!runtime->no_period_wakeup) {
			value = readl(priv->io + KIRKWOOD_INT_MASK);
			value |= KIRKWOOD_INT_CAUSE_PLAY_BYTES;
			writel(value, priv->io + KIRKWOOD_INT_MASK);
		}

		/* enable playback */
		writel(ctl, priv->io + KIRKWOOD_PLAYCTL);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
		/* stop audio, disable interrupts */
		ctl |= KIRKWOOD_PLAYCTL_PAUSE | KIRKWOOD_PLAYCTL_I2S_MUTE |
				KIRKWOOD_PLAYCTL_SPDIF_MUTE;
		writel(ctl, priv->io + KIRKWOOD_PLAYCTL);

		value = readl(priv->io + KIRKWOOD_INT_MASK);
		value &= ~KIRKWOOD_INT_CAUSE_PLAY_BYTES;
		writel(value, priv->io + KIRKWOOD_INT_MASK);

		/* disable all playbacks */
		ctl &= ~KIRKWOOD_PLAYCTL_ENABLE_MASK;
		writel(ctl, priv->io + KIRKWOOD_PLAYCTL);
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		ctl |= KIRKWOOD_PLAYCTL_PAUSE | KIRKWOOD_PLAYCTL_I2S_MUTE |
				KIRKWOOD_PLAYCTL_SPDIF_MUTE;
		writel(ctl, priv->io + KIRKWOOD_PLAYCTL);
		break;

	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		ctl &= ~(KIRKWOOD_PLAYCTL_PAUSE | KIRKWOOD_PLAYCTL_I2S_MUTE |
				KIRKWOOD_PLAYCTL_SPDIF_MUTE);
		ctl = kirkwood_i2s_play_mute(ctl);
		writel(ctl, priv->io + KIRKWOOD_PLAYCTL);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int kirkwood_i2s_rec_trigger(struct snd_pcm_substream *substream,
				int cmd, struct snd_soc_dai *dai)
{
	struct kirkwood_dma_data *priv = snd_soc_dai_get_drvdata(dai);
	uint32_t ctl, value;

	value = readl(priv->io + KIRKWOOD_RECCTL);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		/* configure */
		ctl = priv->ctl_rec;
		if (dai->id == 0)
			ctl &= ~KIRKWOOD_RECCTL_SPDIF_EN;	/* i2s */
		else
			ctl &= ~KIRKWOOD_RECCTL_I2S_EN;		/* spdif */

		value = ctl & ~KIRKWOOD_RECCTL_ENABLE_MASK;
		writel(value, priv->io + KIRKWOOD_RECCTL);

		/* enable interrupts */
		value = readl(priv->io + KIRKWOOD_INT_MASK);
		value |= KIRKWOOD_INT_CAUSE_REC_BYTES;
		writel(value, priv->io + KIRKWOOD_INT_MASK);

		/* enable record */
		writel(ctl, priv->io + KIRKWOOD_RECCTL);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
		/* stop audio, disable interrupts */
		value = readl(priv->io + KIRKWOOD_RECCTL);
		value |= KIRKWOOD_RECCTL_PAUSE | KIRKWOOD_RECCTL_MUTE;
		writel(value, priv->io + KIRKWOOD_RECCTL);

		value = readl(priv->io + KIRKWOOD_INT_MASK);
		value &= ~KIRKWOOD_INT_CAUSE_REC_BYTES;
		writel(value, priv->io + KIRKWOOD_INT_MASK);

		/* disable all records */
		value = readl(priv->io + KIRKWOOD_RECCTL);
		value &= ~KIRKWOOD_RECCTL_ENABLE_MASK;
		writel(value, priv->io + KIRKWOOD_RECCTL);
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		value = readl(priv->io + KIRKWOOD_RECCTL);
		value |= KIRKWOOD_RECCTL_PAUSE | KIRKWOOD_RECCTL_MUTE;
		writel(value, priv->io + KIRKWOOD_RECCTL);
		break;

	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		value = readl(priv->io + KIRKWOOD_RECCTL);
		value &= ~(KIRKWOOD_RECCTL_PAUSE | KIRKWOOD_RECCTL_MUTE);
		writel(value, priv->io + KIRKWOOD_RECCTL);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int kirkwood_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
			       struct snd_soc_dai *dai)
{
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return kirkwood_i2s_play_trigger(substream, cmd, dai);
	else
		return kirkwood_i2s_rec_trigger(substream, cmd, dai);

	return 0;
}

static int kirkwood_i2s_init(struct kirkwood_dma_data *priv)
{
	unsigned long value;
	unsigned int reg_data;

	/* put system in a "safe" state : */
	/* disable audio interrupts */
	writel(0xffffffff, priv->io + KIRKWOOD_INT_CAUSE);
	writel(0, priv->io + KIRKWOOD_INT_MASK);

	reg_data = readl(priv->io + 0x1200);
	reg_data &= (~(0x333FF8));
	reg_data |= 0x111D18;
	writel(reg_data, priv->io + 0x1200);

	msleep(500);

	reg_data = readl(priv->io + 0x1200);
	reg_data &= (~(0x333FF8));
	reg_data |= 0x111D18;
	writel(reg_data, priv->io + 0x1200);

	/* disable playback/record */
	value = readl(priv->io + KIRKWOOD_PLAYCTL);
	value &= ~KIRKWOOD_PLAYCTL_ENABLE_MASK;
	writel(value, priv->io + KIRKWOOD_PLAYCTL);

	value = readl(priv->io + KIRKWOOD_RECCTL);
	value &= ~KIRKWOOD_RECCTL_ENABLE_MASK;
	writel(value, priv->io + KIRKWOOD_RECCTL);

	return 0;

}

static const struct snd_soc_dai_ops kirkwood_i2s_dai_ops = {
	.startup	= kirkwood_i2s_startup,
	.trigger	= kirkwood_i2s_trigger,
	.hw_params      = kirkwood_i2s_hw_params,
	.set_fmt        = kirkwood_i2s_set_fmt,
};

static struct snd_soc_dai_driver kirkwood_i2s_dai[2] = {
    {
	.name = "i2s",
	.id = 0,
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |
				SNDRV_PCM_RATE_96000,
		.formats = KIRKWOOD_I2S_FORMATS,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |
				SNDRV_PCM_RATE_96000,
		.formats = KIRKWOOD_I2S_FORMATS,
	},
	.ops = &kirkwood_i2s_dai_ops,
    },
    {
	.name = "spdif",
	.id = 1,
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |
				SNDRV_PCM_RATE_96000,
		.formats = KIRKWOOD_SPDIF_FORMATS,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |
				SNDRV_PCM_RATE_96000,
		.formats = KIRKWOOD_SPDIF_FORMATS,
	},
	.ops = &kirkwood_i2s_dai_ops,
    },
};

static struct snd_soc_dai_driver kirkwood_i2s_dai_extclk[2] = {
    {
	.name = "i2s",
	.id = 0,
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_CONTINUOUS,
		.rate_min = 5512,
		.rate_max = 192000,
		.formats = KIRKWOOD_I2S_FORMATS,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_CONTINUOUS,
		.rate_min = 5512,
		.rate_max = 192000,
		.formats = KIRKWOOD_I2S_FORMATS,
	},
	.ops = &kirkwood_i2s_dai_ops,
    },
    {
	.name = "spdif",
	.id = 1,
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_CONTINUOUS,
		.rate_min = 5512,
		.rate_max = 192000,
		.formats = KIRKWOOD_SPDIF_FORMATS,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_CONTINUOUS,
		.rate_min = 5512,
		.rate_max = 192000,
		.formats = KIRKWOOD_SPDIF_FORMATS,
	},
	.ops = &kirkwood_i2s_dai_ops,
    },
};

static int kirkwood_i2s_dev_probe(struct platform_device *pdev)
{
	struct kirkwood_asoc_platform_data *data = pdev->dev.platform_data;
	struct snd_soc_dai_driver *soc_dai = kirkwood_i2s_dai;
	struct kirkwood_dma_data *priv;
	struct device_node *np = pdev->dev.of_node;
	int err;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, priv);

	if (of_device_is_compatible(np, "marvell,armada-380-audio"))
		priv->io = devm_platform_ioremap_resource_byname(pdev, "i2s_regs");
	else
		priv->io = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->io))
		return PTR_ERR(priv->io);

	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq < 0)
		return priv->irq;

	if (of_device_is_compatible(np, "marvell,armada-380-audio")) {
		err = armada_38x_i2s_init_quirk(pdev, priv, soc_dai);
		if (err < 0)
			return err;
		/* Set initial pll frequency */
		armada_38x_set_pll(priv->pll_config, 44100);
	}

	if (np) {
		priv->burst = 128;		/* might be 32 or 128 */
	} else if (data) {
		priv->burst = data->burst;
	} else {
		dev_err(&pdev->dev, "no DT nor platform data ?!\n");
		return -EINVAL;
	}

	priv->clk = devm_clk_get(&pdev->dev, np ? "internal" : NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(&pdev->dev, "no clock\n");
		return PTR_ERR(priv->clk);
	}

	priv->extclk = devm_clk_get(&pdev->dev, "extclk");
	if (IS_ERR(priv->extclk)) {
		if (PTR_ERR(priv->extclk) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
	} else {
		if (clk_is_match(priv->extclk, priv->clk)) {
			devm_clk_put(&pdev->dev, priv->extclk);
			priv->extclk = ERR_PTR(-EINVAL);
		} else {
			dev_info(&pdev->dev, "found external clock\n");
			clk_prepare_enable(priv->extclk);
			soc_dai = kirkwood_i2s_dai_extclk;
		}
	}

	err = clk_prepare_enable(priv->clk);
	if (err < 0)
		return err;

	/* Some sensible defaults - this reflects the powerup values */
	priv->ctl_play = KIRKWOOD_PLAYCTL_SIZE_24;
	priv->ctl_rec = KIRKWOOD_RECCTL_SIZE_24;

	/* Select the burst size */
	if (priv->burst == 32) {
		priv->ctl_play |= KIRKWOOD_PLAYCTL_BURST_32;
		priv->ctl_rec |= KIRKWOOD_RECCTL_BURST_32;
	} else {
		priv->ctl_play |= KIRKWOOD_PLAYCTL_BURST_128;
		priv->ctl_rec |= KIRKWOOD_RECCTL_BURST_128;
	}

	err = snd_soc_register_component(&pdev->dev, &kirkwood_soc_component,
					 soc_dai, 2);
	if (err) {
		dev_err(&pdev->dev, "snd_soc_register_component failed\n");
		goto err_component;
	}

	kirkwood_i2s_init(priv);

	return 0;

 err_component:
	if (!IS_ERR(priv->extclk))
		clk_disable_unprepare(priv->extclk);
	clk_disable_unprepare(priv->clk);

	return err;
}

static int kirkwood_i2s_dev_remove(struct platform_device *pdev)
{
	struct kirkwood_dma_data *priv = dev_get_drvdata(&pdev->dev);

	snd_soc_unregister_component(&pdev->dev);
	if (!IS_ERR(priv->extclk))
		clk_disable_unprepare(priv->extclk);
	clk_disable_unprepare(priv->clk);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id mvebu_audio_of_match[] = {
	{ .compatible = "marvell,kirkwood-audio" },
	{ .compatible = "marvell,dove-audio" },
	{ .compatible = "marvell,armada370-audio" },
	{ .compatible = "marvell,armada-380-audio" },
	{ }
};
MODULE_DEVICE_TABLE(of, mvebu_audio_of_match);
#endif

static struct platform_driver kirkwood_i2s_driver = {
	.probe  = kirkwood_i2s_dev_probe,
	.remove = kirkwood_i2s_dev_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = of_match_ptr(mvebu_audio_of_match),
	},
};

module_platform_driver(kirkwood_i2s_driver);

/* Module information */
MODULE_AUTHOR("Arnaud Patard, <arnaud.patard@rtp-net.org>");
MODULE_DESCRIPTION("Kirkwood I2S SoC Interface");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:mvebu-audio");
