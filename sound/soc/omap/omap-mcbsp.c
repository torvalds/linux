/*
 * omap-mcbsp.c  --  OMAP ALSA SoC DAI driver using McBSP port
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * Contact: Jarkko Nikula <jarkko.nikula@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>

#include <mach/control.h>
#include <mach/dma.h>
#include <mach/mcbsp.h>
#include "omap-mcbsp.h"
#include "omap-pcm.h"

#define OMAP_MCBSP_RATES	(SNDRV_PCM_RATE_44100 | \
				 SNDRV_PCM_RATE_48000 | \
				 SNDRV_PCM_RATE_KNOT)

struct omap_mcbsp_data {
	unsigned int			bus_id;
	struct omap_mcbsp_reg_cfg	regs;
	/*
	 * Flags indicating is the bus already activated and configured by
	 * another substream
	 */
	int				active;
	int				configured;
};

#define to_mcbsp(priv)	container_of((priv), struct omap_mcbsp_data, bus_id)

static struct omap_mcbsp_data mcbsp_data[NUM_LINKS];

/*
 * Stream DMA parameters. DMA request line and port address are set runtime
 * since they are different between OMAP1 and later OMAPs
 */
static struct omap_pcm_dma_data omap_mcbsp_dai_dma_params[NUM_LINKS][2] = {
{
	{ .name		= "I2S PCM Stereo out", },
	{ .name		= "I2S PCM Stereo in", },
},
};

#if defined(CONFIG_ARCH_OMAP15XX) || defined(CONFIG_ARCH_OMAP16XX)
static const int omap1_dma_reqs[][2] = {
	{ OMAP_DMA_MCBSP1_TX, OMAP_DMA_MCBSP1_RX },
	{ OMAP_DMA_MCBSP2_TX, OMAP_DMA_MCBSP2_RX },
	{ OMAP_DMA_MCBSP3_TX, OMAP_DMA_MCBSP3_RX },
};
static const unsigned long omap1_mcbsp_port[][2] = {
	{ OMAP1510_MCBSP1_BASE + OMAP_MCBSP_REG_DXR1,
	  OMAP1510_MCBSP1_BASE + OMAP_MCBSP_REG_DRR1 },
	{ OMAP1510_MCBSP2_BASE + OMAP_MCBSP_REG_DXR1,
	  OMAP1510_MCBSP2_BASE + OMAP_MCBSP_REG_DRR1 },
	{ OMAP1510_MCBSP3_BASE + OMAP_MCBSP_REG_DXR1,
	  OMAP1510_MCBSP3_BASE + OMAP_MCBSP_REG_DRR1 },
};
#else
static const int omap1_dma_reqs[][2] = {};
static const unsigned long omap1_mcbsp_port[][2] = {};
#endif
#if defined(CONFIG_ARCH_OMAP2420)
static const int omap2420_dma_reqs[][2] = {
	{ OMAP24XX_DMA_MCBSP1_TX, OMAP24XX_DMA_MCBSP1_RX },
	{ OMAP24XX_DMA_MCBSP2_TX, OMAP24XX_DMA_MCBSP2_RX },
};
static const unsigned long omap2420_mcbsp_port[][2] = {
	{ OMAP24XX_MCBSP1_BASE + OMAP_MCBSP_REG_DXR1,
	  OMAP24XX_MCBSP1_BASE + OMAP_MCBSP_REG_DRR1 },
	{ OMAP24XX_MCBSP2_BASE + OMAP_MCBSP_REG_DXR1,
	  OMAP24XX_MCBSP2_BASE + OMAP_MCBSP_REG_DRR1 },
};
#else
static const int omap2420_dma_reqs[][2] = {};
static const unsigned long omap2420_mcbsp_port[][2] = {};
#endif

static int omap_mcbsp_dai_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	struct omap_mcbsp_data *mcbsp_data = to_mcbsp(cpu_dai->private_data);
	int err = 0;

	if (!cpu_dai->active)
		err = omap_mcbsp_request(mcbsp_data->bus_id);

	return err;
}

static void omap_mcbsp_dai_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	struct omap_mcbsp_data *mcbsp_data = to_mcbsp(cpu_dai->private_data);

	if (!cpu_dai->active) {
		omap_mcbsp_free(mcbsp_data->bus_id);
		mcbsp_data->configured = 0;
	}
}

static int omap_mcbsp_dai_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	struct omap_mcbsp_data *mcbsp_data = to_mcbsp(cpu_dai->private_data);
	int err = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (!mcbsp_data->active++)
			omap_mcbsp_start(mcbsp_data->bus_id);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (!--mcbsp_data->active)
			omap_mcbsp_stop(mcbsp_data->bus_id);
		break;
	default:
		err = -EINVAL;
	}

	return err;
}

static int omap_mcbsp_dai_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	struct omap_mcbsp_data *mcbsp_data = to_mcbsp(cpu_dai->private_data);
	struct omap_mcbsp_reg_cfg *regs = &mcbsp_data->regs;
	int dma, bus_id = mcbsp_data->bus_id, id = cpu_dai->id;
	unsigned long port;

	if (cpu_class_is_omap1()) {
		dma = omap1_dma_reqs[bus_id][substream->stream];
		port = omap1_mcbsp_port[bus_id][substream->stream];
	} else if (cpu_is_omap2420()) {
		dma = omap2420_dma_reqs[bus_id][substream->stream];
		port = omap2420_mcbsp_port[bus_id][substream->stream];
	} else {
		/*
		 * TODO: Add support for 2430 and 3430
		 */
		return -ENODEV;
	}
	omap_mcbsp_dai_dma_params[id][substream->stream].dma_req = dma;
	omap_mcbsp_dai_dma_params[id][substream->stream].port_addr = port;
	cpu_dai->dma_data = &omap_mcbsp_dai_dma_params[id][substream->stream];

	if (mcbsp_data->configured) {
		/* McBSP already configured by another stream */
		return 0;
	}

	switch (params_channels(params)) {
	case 2:
		/* Set 1 word per (McBPSP) frame and use dual-phase frames */
		regs->rcr2	|= RFRLEN2(1 - 1) | RPHASE;
		regs->rcr1	|= RFRLEN1(1 - 1);
		regs->xcr2	|= XFRLEN2(1 - 1) | XPHASE;
		regs->xcr1	|= XFRLEN1(1 - 1);
		break;
	default:
		/* Unsupported number of channels */
		return -EINVAL;
	}

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		/* Set word lengths */
		regs->rcr2	|= RWDLEN2(OMAP_MCBSP_WORD_16);
		regs->rcr1	|= RWDLEN1(OMAP_MCBSP_WORD_16);
		regs->xcr2	|= XWDLEN2(OMAP_MCBSP_WORD_16);
		regs->xcr1	|= XWDLEN1(OMAP_MCBSP_WORD_16);
		/* Set FS period and length in terms of bit clock periods */
		regs->srgr2	|= FPER(16 * 2 - 1);
		regs->srgr1	|= FWID(16 - 1);
		break;
	default:
		/* Unsupported PCM format */
		return -EINVAL;
	}

	omap_mcbsp_config(bus_id, &mcbsp_data->regs);
	mcbsp_data->configured = 1;

	return 0;
}

/*
 * This must be called before _set_clkdiv and _set_sysclk since McBSP register
 * cache is initialized here
 */
static int omap_mcbsp_dai_set_dai_fmt(struct snd_soc_dai *cpu_dai,
				      unsigned int fmt)
{
	struct omap_mcbsp_data *mcbsp_data = to_mcbsp(cpu_dai->private_data);
	struct omap_mcbsp_reg_cfg *regs = &mcbsp_data->regs;

	if (mcbsp_data->configured)
		return 0;

	memset(regs, 0, sizeof(*regs));
	/* Generic McBSP register settings */
	regs->spcr2	|= XINTM(3) | FREE;
	regs->spcr1	|= RINTM(3);
	regs->rcr2	|= RFIG;
	regs->xcr2	|= XFIG;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		/* 1-bit data delay */
		regs->rcr2	|= RDATDLY(1);
		regs->xcr2	|= XDATDLY(1);
		break;
	default:
		/* Unsupported data format */
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		/* McBSP master. Set FS and bit clocks as outputs */
		regs->pcr0	|= FSXM | FSRM |
				   CLKXM | CLKRM;
		/* Sample rate generator drives the FS */
		regs->srgr2	|= FSGM;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		/* McBSP slave */
		break;
	default:
		/* Unsupported master/slave configuration */
		return -EINVAL;
	}

	/* Set bit clock (CLKX/CLKR) and FS polarities */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		/*
		 * Normal BCLK + FS.
		 * FS active low. TX data driven on falling edge of bit clock
		 * and RX data sampled on rising edge of bit clock.
		 */
		regs->pcr0	|= FSXP | FSRP |
				   CLKXP | CLKRP;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		regs->pcr0	|= CLKXP | CLKRP;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		regs->pcr0	|= FSXP | FSRP;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int omap_mcbsp_dai_set_clkdiv(struct snd_soc_dai *cpu_dai,
				     int div_id, int div)
{
	struct omap_mcbsp_data *mcbsp_data = to_mcbsp(cpu_dai->private_data);
	struct omap_mcbsp_reg_cfg *regs = &mcbsp_data->regs;

	if (div_id != OMAP_MCBSP_CLKGDV)
		return -ENODEV;

	regs->srgr1	|= CLKGDV(div - 1);

	return 0;
}

static int omap_mcbsp_dai_set_clks_src(struct omap_mcbsp_data *mcbsp_data,
				       int clk_id)
{
	int sel_bit;
	u16 reg;

	if (cpu_class_is_omap1()) {
		/* OMAP1's can use only external source clock */
		if (unlikely(clk_id == OMAP_MCBSP_SYSCLK_CLKS_FCLK))
			return -EINVAL;
		else
			return 0;
	}

	switch (mcbsp_data->bus_id) {
	case 0:
		reg = OMAP2_CONTROL_DEVCONF0;
		sel_bit = 2;
		break;
	case 1:
		reg = OMAP2_CONTROL_DEVCONF0;
		sel_bit = 6;
		break;
	/* TODO: Support for ports 3 - 5 in OMAP2430 and OMAP34xx */
	default:
		return -EINVAL;
	}

	if (cpu_class_is_omap2()) {
		if (clk_id == OMAP_MCBSP_SYSCLK_CLKS_FCLK) {
			omap_ctrl_writel(omap_ctrl_readl(reg) &
					 ~(1 << sel_bit), reg);
		} else {
			omap_ctrl_writel(omap_ctrl_readl(reg) |
					 (1 << sel_bit), reg);
		}
	}

	return 0;
}

static int omap_mcbsp_dai_set_dai_sysclk(struct snd_soc_dai *cpu_dai,
					 int clk_id, unsigned int freq,
					 int dir)
{
	struct omap_mcbsp_data *mcbsp_data = to_mcbsp(cpu_dai->private_data);
	struct omap_mcbsp_reg_cfg *regs = &mcbsp_data->regs;
	int err = 0;

	switch (clk_id) {
	case OMAP_MCBSP_SYSCLK_CLK:
		regs->srgr2	|= CLKSM;
		break;
	case OMAP_MCBSP_SYSCLK_CLKS_FCLK:
	case OMAP_MCBSP_SYSCLK_CLKS_EXT:
		err = omap_mcbsp_dai_set_clks_src(mcbsp_data, clk_id);
		break;

	case OMAP_MCBSP_SYSCLK_CLKX_EXT:
		regs->srgr2	|= CLKSM;
	case OMAP_MCBSP_SYSCLK_CLKR_EXT:
		regs->pcr0	|= SCLKME;
		break;
	default:
		err = -ENODEV;
	}

	return err;
}

struct snd_soc_dai omap_mcbsp_dai[NUM_LINKS] = {
{
	.name = "omap-mcbsp-dai",
	.id = 0,
	.type = SND_SOC_DAI_I2S,
	.playback = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = OMAP_MCBSP_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = OMAP_MCBSP_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = {
		.startup = omap_mcbsp_dai_startup,
		.shutdown = omap_mcbsp_dai_shutdown,
		.trigger = omap_mcbsp_dai_trigger,
		.hw_params = omap_mcbsp_dai_hw_params,
	},
	.dai_ops = {
		.set_fmt = omap_mcbsp_dai_set_dai_fmt,
		.set_clkdiv = omap_mcbsp_dai_set_clkdiv,
		.set_sysclk = omap_mcbsp_dai_set_dai_sysclk,
	},
	.private_data = &mcbsp_data[0].bus_id,
},
};
EXPORT_SYMBOL_GPL(omap_mcbsp_dai);

MODULE_AUTHOR("Jarkko Nikula <jarkko.nikula@nokia.com>");
MODULE_DESCRIPTION("OMAP I2S SoC Interface");
MODULE_LICENSE("GPL");
