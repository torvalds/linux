/*
 * Freescale SSI ALSA SoC Digital Audio Interface (DAI) driver
 *
 * Author: Timur Tabi <timur@freescale.com>
 *
 * Copyright 2007-2010 Freescale Semiconductor, Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 *
 * Some notes why imx-pcm-fiq is used instead of DMA on some boards:
 *
 * The i.MX SSI core has some nasty limitations in AC97 mode. While most
 * sane processor vendors have a FIFO per AC97 slot, the i.MX has only
 * one FIFO which combines all valid receive slots. We cannot even select
 * which slots we want to receive. The WM9712 with which this driver
 * was developed with always sends GPIO status data in slot 12 which
 * we receive in our (PCM-) data stream. The only chance we have is to
 * manually skip this data in the FIQ handler. With sampling rates different
 * from 48000Hz not every frame has valid receive data, so the ratio
 * between pcm data and GPIO status data changes. Our FIQ handler is not
 * able to handle this, hence this driver only works with 48000Hz sampling
 * rate.
 * Reading and writing AC97 registers is another challenge. The core
 * provides us status bits when the read register is updated with *another*
 * value. When we read the same register two times (and the register still
 * contains the same value) these status bits are not set. We work
 * around this by not polling these bits but only wait a fixed delay.
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/ctype.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

#include "fsl_ssi.h"
#include "imx-pcm.h"

/**
 * FSLSSI_I2S_FORMATS: audio formats supported by the SSI
 *
 * The SSI has a limitation in that the samples must be in the same byte
 * order as the host CPU.  This is because when multiple bytes are written
 * to the STX register, the bytes and bits must be written in the same
 * order.  The STX is a shift register, so all the bits need to be aligned
 * (bit-endianness must match byte-endianness).  Processors typically write
 * the bits within a byte in the same order that the bytes of a word are
 * written in.  So if the host CPU is big-endian, then only big-endian
 * samples will be written to STX properly.
 */
#ifdef __BIG_ENDIAN
#define FSLSSI_I2S_FORMATS \
	(SNDRV_PCM_FMTBIT_S8 | \
	 SNDRV_PCM_FMTBIT_S16_BE | \
	 SNDRV_PCM_FMTBIT_S18_3BE | \
	 SNDRV_PCM_FMTBIT_S20_3BE | \
	 SNDRV_PCM_FMTBIT_S24_3BE | \
	 SNDRV_PCM_FMTBIT_S24_BE)
#else
#define FSLSSI_I2S_FORMATS \
	(SNDRV_PCM_FMTBIT_S8 | \
	 SNDRV_PCM_FMTBIT_S16_LE | \
	 SNDRV_PCM_FMTBIT_S18_3LE | \
	 SNDRV_PCM_FMTBIT_S20_3LE | \
	 SNDRV_PCM_FMTBIT_S24_3LE | \
	 SNDRV_PCM_FMTBIT_S24_LE)
#endif

#define FSLSSI_SIER_DBG_RX_FLAGS \
	(SSI_SIER_RFF0_EN | \
	 SSI_SIER_RLS_EN | \
	 SSI_SIER_RFS_EN | \
	 SSI_SIER_ROE0_EN | \
	 SSI_SIER_RFRC_EN)
#define FSLSSI_SIER_DBG_TX_FLAGS \
	(SSI_SIER_TFE0_EN | \
	 SSI_SIER_TLS_EN | \
	 SSI_SIER_TFS_EN | \
	 SSI_SIER_TUE0_EN | \
	 SSI_SIER_TFRC_EN)

enum fsl_ssi_type {
	FSL_SSI_MCP8610,
	FSL_SSI_MX21,
	FSL_SSI_MX35,
	FSL_SSI_MX51,
};

struct fsl_ssi_regvals {
	u32 sier;
	u32 srcr;
	u32 stcr;
	u32 scr;
};

static bool fsl_ssi_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case REG_SSI_SACCEN:
	case REG_SSI_SACCDIS:
		return false;
	default:
		return true;
	}
}

static bool fsl_ssi_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case REG_SSI_STX0:
	case REG_SSI_STX1:
	case REG_SSI_SRX0:
	case REG_SSI_SRX1:
	case REG_SSI_SISR:
	case REG_SSI_SFCSR:
	case REG_SSI_SACNT:
	case REG_SSI_SACADD:
	case REG_SSI_SACDAT:
	case REG_SSI_SATAG:
	case REG_SSI_SACCST:
	case REG_SSI_SOR:
		return true;
	default:
		return false;
	}
}

static bool fsl_ssi_precious_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case REG_SSI_SRX0:
	case REG_SSI_SRX1:
	case REG_SSI_SISR:
	case REG_SSI_SACADD:
	case REG_SSI_SACDAT:
	case REG_SSI_SATAG:
		return true;
	default:
		return false;
	}
}

static bool fsl_ssi_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case REG_SSI_SRX0:
	case REG_SSI_SRX1:
	case REG_SSI_SACCST:
		return false;
	default:
		return true;
	}
}

static const struct regmap_config fsl_ssi_regconfig = {
	.max_register = REG_SSI_SACCDIS,
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
	.num_reg_defaults_raw = REG_SSI_SACCDIS / sizeof(uint32_t) + 1,
	.readable_reg = fsl_ssi_readable_reg,
	.volatile_reg = fsl_ssi_volatile_reg,
	.precious_reg = fsl_ssi_precious_reg,
	.writeable_reg = fsl_ssi_writeable_reg,
	.cache_type = REGCACHE_FLAT,
};

struct fsl_ssi_soc_data {
	bool imx;
	bool imx21regs; /* imx21-class SSI - no SACC{ST,EN,DIS} regs */
	bool offline_config;
	u32 sisr_write_mask;
};

/**
 * fsl_ssi: per-SSI private data
 *
 * @regs: Pointer to the regmap registers
 * @irq: IRQ of this SSI
 * @cpu_dai_drv: CPU DAI driver for this device
 *
 * @dai_fmt: DAI configuration this device is currently used with
 * @i2s_net: I2S and Network mode configurations of SCR register
 * @use_dma: DMA is used or FIQ with stream filter
 * @use_dual_fifo: DMA with support for dual FIFO mode
 * @has_ipg_clk_name: If "ipg" is in the clock name list of device tree
 * @fifo_depth: Depth of the SSI FIFOs
 * @slot_width: Width of each DAI slot
 * @slots: Number of slots
 * @regvals: Specific RX/TX register settings
 *
 * @clk: Clock source to access register
 * @baudclk: Clock source to generate bit and frame-sync clocks
 * @baudclk_streams: Active streams that are using baudclk
 *
 * @regcache_sfcsr: Cache sfcsr register value during suspend and resume
 * @regcache_sacnt: Cache sacnt register value during suspend and resume
 *
 * @dma_params_tx: DMA transmit parameters
 * @dma_params_rx: DMA receive parameters
 * @ssi_phys: physical address of the SSI registers
 *
 * @fiq_params: FIQ stream filtering parameters
 *
 * @pdev: Pointer to pdev when using fsl-ssi as sound card (ppc only)
 *        TODO: Should be replaced with simple-sound-card
 *
 * @dbg_stats: Debugging statistics
 *
 * @soc: SoC specific data
 * @dev: Pointer to &pdev->dev
 *
 * @fifo_watermark: The FIFO watermark setting. Notifies DMA when there are
 *                  @fifo_watermark or fewer words in TX fifo or
 *                  @fifo_watermark or more empty words in RX fifo.
 * @dma_maxburst: Max number of words to transfer in one go. So far,
 *                this is always the same as fifo_watermark.
 *
 * @ac97_reg_lock: Mutex lock to serialize AC97 register access operations
 */
struct fsl_ssi {
	struct regmap *regs;
	int irq;
	struct snd_soc_dai_driver cpu_dai_drv;

	unsigned int dai_fmt;
	u8 i2s_net;
	bool use_dma;
	bool use_dual_fifo;
	bool has_ipg_clk_name;
	unsigned int fifo_depth;
	unsigned int slot_width;
	unsigned int slots;
	struct fsl_ssi_regvals regvals[2];

	struct clk *clk;
	struct clk *baudclk;
	unsigned int baudclk_streams;

	u32 regcache_sfcsr;
	u32 regcache_sacnt;

	struct snd_dmaengine_dai_dma_data dma_params_tx;
	struct snd_dmaengine_dai_dma_data dma_params_rx;
	dma_addr_t ssi_phys;

	struct imx_pcm_fiq_params fiq_params;

	struct platform_device *pdev;

	struct fsl_ssi_dbg dbg_stats;

	const struct fsl_ssi_soc_data *soc;
	struct device *dev;

	u32 fifo_watermark;
	u32 dma_maxburst;

	struct mutex ac97_reg_lock;
};

/*
 * SoC specific data
 *
 * Notes:
 * 1) SSI in earlier SoCS has critical bits in control registers that
 *    cannot be changed after SSI starts running -- a software reset
 *    (set SSIEN to 0) is required to change their values. So adding
 *    an offline_config flag for these SoCs.
 * 2) SDMA is available since imx35. However, imx35 does not support
 *    DMA bits changing when SSI is running, so set offline_config.
 * 3) imx51 and later versions support register configurations when
 *    SSI is running (SSIEN); For these versions, DMA needs to be
 *    configured before SSI sends DMA request to avoid an undefined
 *    DMA request on the SDMA side.
 */

static struct fsl_ssi_soc_data fsl_ssi_mpc8610 = {
	.imx = false,
	.offline_config = true,
	.sisr_write_mask = SSI_SISR_RFRC | SSI_SISR_TFRC |
			   SSI_SISR_ROE0 | SSI_SISR_ROE1 |
			   SSI_SISR_TUE0 | SSI_SISR_TUE1,
};

static struct fsl_ssi_soc_data fsl_ssi_imx21 = {
	.imx = true,
	.imx21regs = true,
	.offline_config = true,
	.sisr_write_mask = 0,
};

static struct fsl_ssi_soc_data fsl_ssi_imx35 = {
	.imx = true,
	.offline_config = true,
	.sisr_write_mask = SSI_SISR_RFRC | SSI_SISR_TFRC |
			   SSI_SISR_ROE0 | SSI_SISR_ROE1 |
			   SSI_SISR_TUE0 | SSI_SISR_TUE1,
};

static struct fsl_ssi_soc_data fsl_ssi_imx51 = {
	.imx = true,
	.offline_config = false,
	.sisr_write_mask = SSI_SISR_ROE0 | SSI_SISR_ROE1 |
			   SSI_SISR_TUE0 | SSI_SISR_TUE1,
};

static const struct of_device_id fsl_ssi_ids[] = {
	{ .compatible = "fsl,mpc8610-ssi", .data = &fsl_ssi_mpc8610 },
	{ .compatible = "fsl,imx51-ssi", .data = &fsl_ssi_imx51 },
	{ .compatible = "fsl,imx35-ssi", .data = &fsl_ssi_imx35 },
	{ .compatible = "fsl,imx21-ssi", .data = &fsl_ssi_imx21 },
	{}
};
MODULE_DEVICE_TABLE(of, fsl_ssi_ids);

static bool fsl_ssi_is_ac97(struct fsl_ssi *ssi)
{
	return (ssi->dai_fmt & SND_SOC_DAIFMT_FORMAT_MASK) ==
		SND_SOC_DAIFMT_AC97;
}

static bool fsl_ssi_is_i2s_master(struct fsl_ssi *ssi)
{
	return (ssi->dai_fmt & SND_SOC_DAIFMT_MASTER_MASK) ==
		SND_SOC_DAIFMT_CBS_CFS;
}

static bool fsl_ssi_is_i2s_cbm_cfs(struct fsl_ssi *ssi)
{
	return (ssi->dai_fmt & SND_SOC_DAIFMT_MASTER_MASK) ==
		SND_SOC_DAIFMT_CBM_CFS;
}

/**
 * Interrupt handler to gather states
 */
static irqreturn_t fsl_ssi_isr(int irq, void *dev_id)
{
	struct fsl_ssi *ssi = dev_id;
	struct regmap *regs = ssi->regs;
	__be32 sisr;
	__be32 sisr2;

	regmap_read(regs, REG_SSI_SISR, &sisr);

	sisr2 = sisr & ssi->soc->sisr_write_mask;
	/* Clear the bits that we set */
	if (sisr2)
		regmap_write(regs, REG_SSI_SISR, sisr2);

	fsl_ssi_dbg_isr(&ssi->dbg_stats, sisr);

	return IRQ_HANDLED;
}

/**
 * Enable or disable all rx/tx config flags at once
 */
static void fsl_ssi_rxtx_config(struct fsl_ssi *ssi, bool enable)
{
	struct regmap *regs = ssi->regs;
	struct fsl_ssi_regvals *vals = ssi->regvals;

	if (enable) {
		regmap_update_bits(regs, REG_SSI_SIER,
				   vals[RX].sier | vals[TX].sier,
				   vals[RX].sier | vals[TX].sier);
		regmap_update_bits(regs, REG_SSI_SRCR,
				   vals[RX].srcr | vals[TX].srcr,
				   vals[RX].srcr | vals[TX].srcr);
		regmap_update_bits(regs, REG_SSI_STCR,
				   vals[RX].stcr | vals[TX].stcr,
				   vals[RX].stcr | vals[TX].stcr);
	} else {
		regmap_update_bits(regs, REG_SSI_SRCR,
				   vals[RX].srcr | vals[TX].srcr, 0);
		regmap_update_bits(regs, REG_SSI_STCR,
				   vals[RX].stcr | vals[TX].stcr, 0);
		regmap_update_bits(regs, REG_SSI_SIER,
				   vals[RX].sier | vals[TX].sier, 0);
	}
}

/**
 * Clear remaining data in the FIFO to avoid dirty data or channel slipping
 */
static void fsl_ssi_fifo_clear(struct fsl_ssi *ssi, bool is_rx)
{
	bool tx = !is_rx;

	regmap_update_bits(ssi->regs, REG_SSI_SOR,
			   SSI_SOR_xX_CLR(tx), SSI_SOR_xX_CLR(tx));
}

/**
 * Calculate the bits that have to be disabled for the current stream that is
 * getting disabled. This keeps the bits enabled that are necessary for the
 * second stream to work if 'stream_active' is true.
 *
 * Detailed calculation:
 * These are the values that need to be active after disabling. For non-active
 * second stream, this is 0:
 *	vals_stream * !!stream_active
 *
 * The following computes the overall differences between the setup for the
 * to-disable stream and the active stream, a simple XOR:
 *	vals_disable ^ (vals_stream * !!(stream_active))
 *
 * The full expression adds a mask on all values we care about
 */
#define fsl_ssi_disable_val(vals_disable, vals_stream, stream_active) \
	((vals_disable) & \
	 ((vals_disable) ^ ((vals_stream) * (u32)!!(stream_active))))

/**
 * Enable or disable SSI configuration.
 */
static void fsl_ssi_config(struct fsl_ssi *ssi, bool enable,
			   struct fsl_ssi_regvals *vals)
{
	struct regmap *regs = ssi->regs;
	struct fsl_ssi_regvals *avals;
	int nr_active_streams;
	u32 scr;
	int keep_active;

	regmap_read(regs, REG_SSI_SCR, &scr);

	nr_active_streams = !!(scr & SSI_SCR_TE) + !!(scr & SSI_SCR_RE);

	if (nr_active_streams - 1 > 0)
		keep_active = 1;
	else
		keep_active = 0;

	/* Get the opposite direction to keep its values untouched */
	if (&ssi->regvals[RX] == vals)
		avals = &ssi->regvals[TX];
	else
		avals = &ssi->regvals[RX];

	if (!enable) {
		/*
		 * To keep the other stream safe, exclude shared bits between
		 * both streams, and get safe bits to disable current stream
		 */
		u32 scr = fsl_ssi_disable_val(vals->scr, avals->scr,
					      keep_active);
		/* Safely disable SCR register for the stream */
		regmap_update_bits(regs, REG_SSI_SCR, scr, 0);
	}

	/*
	 * For cases where online configuration is not supported,
	 * 1) Enable all necessary bits of both streams when 1st stream starts
	 *    even if the opposite stream will not start
	 * 2) Disable all remaining bits of both streams when last stream ends
	 */
	if (ssi->soc->offline_config) {
		if ((enable && !nr_active_streams) || (!enable && !keep_active))
			fsl_ssi_rxtx_config(ssi, enable);

		goto config_done;
	}

	/* Online configure single direction while SSI is running */
	if (enable) {
		fsl_ssi_fifo_clear(ssi, vals->scr & SSI_SCR_RE);

		regmap_update_bits(regs, REG_SSI_SRCR, vals->srcr, vals->srcr);
		regmap_update_bits(regs, REG_SSI_STCR, vals->stcr, vals->stcr);
		regmap_update_bits(regs, REG_SSI_SIER, vals->sier, vals->sier);
	} else {
		u32 sier;
		u32 srcr;
		u32 stcr;

		/*
		 * To keep the other stream safe, exclude shared bits between
		 * both streams, and get safe bits to disable current stream
		 */
		sier = fsl_ssi_disable_val(vals->sier, avals->sier,
					   keep_active);
		srcr = fsl_ssi_disable_val(vals->srcr, avals->srcr,
					   keep_active);
		stcr = fsl_ssi_disable_val(vals->stcr, avals->stcr,
					   keep_active);

		/* Safely disable other control registers for the stream */
		regmap_update_bits(regs, REG_SSI_SRCR, srcr, 0);
		regmap_update_bits(regs, REG_SSI_STCR, stcr, 0);
		regmap_update_bits(regs, REG_SSI_SIER, sier, 0);
	}

config_done:
	/* Enabling of subunits is done after configuration */
	if (enable) {
		/*
		 * Start DMA before setting TE to avoid FIFO underrun
		 * which may cause a channel slip or a channel swap
		 *
		 * TODO: FIQ cases might also need this upon testing
		 */
		if (ssi->use_dma && (vals->scr & SSI_SCR_TE)) {
			int i;
			int max_loop = 100;

			/* Enable SSI first to send TX DMA request */
			regmap_update_bits(regs, REG_SSI_SCR,
					   SSI_SCR_SSIEN, SSI_SCR_SSIEN);

			/* Busy wait until TX FIFO not empty -- DMA working */
			for (i = 0; i < max_loop; i++) {
				u32 sfcsr;
				regmap_read(regs, REG_SSI_SFCSR, &sfcsr);
				if (SSI_SFCSR_TFCNT0(sfcsr))
					break;
			}
			if (i == max_loop) {
				dev_err(ssi->dev,
					"Timeout waiting TX FIFO filling\n");
			}
		}
		/* Enable all remaining bits */
		regmap_update_bits(regs, REG_SSI_SCR, vals->scr, vals->scr);
	}
}

static void fsl_ssi_rx_config(struct fsl_ssi *ssi, bool enable)
{
	fsl_ssi_config(ssi, enable, &ssi->regvals[RX]);
}

static void fsl_ssi_tx_ac97_saccst_setup(struct fsl_ssi *ssi)
{
	struct regmap *regs = ssi->regs;

	/* no SACC{ST,EN,DIS} regs on imx21-class SSI */
	if (!ssi->soc->imx21regs) {
		/* Disable all channel slots */
		regmap_write(regs, REG_SSI_SACCDIS, 0xff);
		/* Enable slots 3 & 4 -- PCM Playback Left & Right channels */
		regmap_write(regs, REG_SSI_SACCEN, 0x300);
	}
}

static void fsl_ssi_tx_config(struct fsl_ssi *ssi, bool enable)
{
	/*
	 * SACCST might be modified via AC Link by a CODEC if it sends
	 * extra bits in their SLOTREQ requests, which'll accidentally
	 * send valid data to slots other than normal playback slots.
	 *
	 * To be safe, configure SACCST right before TX starts.
	 */
	if (enable && fsl_ssi_is_ac97(ssi))
		fsl_ssi_tx_ac97_saccst_setup(ssi);

	fsl_ssi_config(ssi, enable, &ssi->regvals[TX]);
}

/**
 * Cache critical bits of SIER, SRCR, STCR and SCR to later set them safely
 */
static void fsl_ssi_setup_regvals(struct fsl_ssi *ssi)
{
	struct fsl_ssi_regvals *vals = ssi->regvals;

	vals[RX].sier = SSI_SIER_RFF0_EN;
	vals[RX].srcr = SSI_SRCR_RFEN0;
	vals[RX].scr = 0;
	vals[TX].sier = SSI_SIER_TFE0_EN;
	vals[TX].stcr = SSI_STCR_TFEN0;
	vals[TX].scr = 0;

	/* AC97 has already enabled SSIEN, RE and TE, so ignore them */
	if (!fsl_ssi_is_ac97(ssi)) {
		vals[RX].scr = SSI_SCR_SSIEN | SSI_SCR_RE;
		vals[TX].scr = SSI_SCR_SSIEN | SSI_SCR_TE;
	}

	if (ssi->use_dma) {
		vals[RX].sier |= SSI_SIER_RDMAE;
		vals[TX].sier |= SSI_SIER_TDMAE;
	} else {
		vals[RX].sier |= SSI_SIER_RIE;
		vals[TX].sier |= SSI_SIER_TIE;
	}

	vals[RX].sier |= FSLSSI_SIER_DBG_RX_FLAGS;
	vals[TX].sier |= FSLSSI_SIER_DBG_TX_FLAGS;
}

static void fsl_ssi_setup_ac97(struct fsl_ssi *ssi)
{
	struct regmap *regs = ssi->regs;

	/* Setup the clock control register */
	regmap_write(regs, REG_SSI_STCCR, SSI_SxCCR_WL(17) | SSI_SxCCR_DC(13));
	regmap_write(regs, REG_SSI_SRCCR, SSI_SxCCR_WL(17) | SSI_SxCCR_DC(13));

	/* Enable AC97 mode and startup the SSI */
	regmap_write(regs, REG_SSI_SACNT, SSI_SACNT_AC97EN | SSI_SACNT_FV);

	/* AC97 has to communicate with codec before starting a stream */
	regmap_update_bits(regs, REG_SSI_SCR,
			   SSI_SCR_SSIEN | SSI_SCR_TE | SSI_SCR_RE,
			   SSI_SCR_SSIEN | SSI_SCR_TE | SSI_SCR_RE);

	regmap_write(regs, REG_SSI_SOR, SSI_SOR_WAIT(3));
}

static int fsl_ssi_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct fsl_ssi *ssi = snd_soc_dai_get_drvdata(rtd->cpu_dai);
	int ret;

	ret = clk_prepare_enable(ssi->clk);
	if (ret)
		return ret;

	/*
	 * When using dual fifo mode, it is safer to ensure an even period
	 * size. If appearing to an odd number while DMA always starts its
	 * task from fifo0, fifo1 would be neglected at the end of each
	 * period. But SSI would still access fifo1 with an invalid data.
	 */
	if (ssi->use_dual_fifo)
		snd_pcm_hw_constraint_step(substream->runtime, 0,
					   SNDRV_PCM_HW_PARAM_PERIOD_SIZE, 2);

	return 0;
}

static void fsl_ssi_shutdown(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct fsl_ssi *ssi = snd_soc_dai_get_drvdata(rtd->cpu_dai);

	clk_disable_unprepare(ssi->clk);
}

/**
 * Configure Digital Audio Interface bit clock
 *
 * Note: This function can be only called when using SSI as DAI master
 *
 * Quick instruction for parameters:
 * freq: Output BCLK frequency = samplerate * slots * slot_width
 *       (In 2-channel I2S Master mode, slot_width is fixed 32)
 */
static int fsl_ssi_set_bclk(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai,
			    struct snd_pcm_hw_params *hw_params)
{
	bool tx2, tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	struct fsl_ssi *ssi = snd_soc_dai_get_drvdata(dai);
	struct regmap *regs = ssi->regs;
	int synchronous = ssi->cpu_dai_drv.symmetric_rates, ret;
	u32 pm = 999, div2, psr, stccr, mask, afreq, factor, i;
	unsigned long clkrate, baudrate, tmprate;
	unsigned int slots = params_channels(hw_params);
	unsigned int slot_width = 32;
	u64 sub, savesub = 100000;
	unsigned int freq;
	bool baudclk_is_used;

	/* Override slots and slot_width if being specifically set... */
	if (ssi->slots)
		slots = ssi->slots;
	/* ...but keep 32 bits if slots is 2 -- I2S Master mode */
	if (ssi->slot_width && slots != 2)
		slot_width = ssi->slot_width;

	/* Generate bit clock based on the slot number and slot width */
	freq = slots * slot_width * params_rate(hw_params);

	/* Don't apply it to any non-baudclk circumstance */
	if (IS_ERR(ssi->baudclk))
		return -EINVAL;

	/*
	 * Hardware limitation: The bclk rate must be
	 * never greater than 1/5 IPG clock rate
	 */
	if (freq * 5 > clk_get_rate(ssi->clk)) {
		dev_err(dai->dev, "bitclk > ipgclk / 5\n");
		return -EINVAL;
	}

	baudclk_is_used = ssi->baudclk_streams & ~(BIT(substream->stream));

	/* It should be already enough to divide clock by setting pm alone */
	psr = 0;
	div2 = 0;

	factor = (div2 + 1) * (7 * psr + 1) * 2;

	for (i = 0; i < 255; i++) {
		tmprate = freq * factor * (i + 1);

		if (baudclk_is_used)
			clkrate = clk_get_rate(ssi->baudclk);
		else
			clkrate = clk_round_rate(ssi->baudclk, tmprate);

		clkrate /= factor;
		afreq = clkrate / (i + 1);

		if (freq == afreq)
			sub = 0;
		else if (freq / afreq == 1)
			sub = freq - afreq;
		else if (afreq / freq == 1)
			sub = afreq - freq;
		else
			continue;

		/* Calculate the fraction */
		sub *= 100000;
		do_div(sub, freq);

		if (sub < savesub && !(i == 0 && psr == 0 && div2 == 0)) {
			baudrate = tmprate;
			savesub = sub;
			pm = i;
		}

		/* We are lucky */
		if (savesub == 0)
			break;
	}

	/* No proper pm found if it is still remaining the initial value */
	if (pm == 999) {
		dev_err(dai->dev, "failed to handle the required sysclk\n");
		return -EINVAL;
	}

	stccr = SSI_SxCCR_PM(pm + 1) | (div2 ? SSI_SxCCR_DIV2 : 0) |
		(psr ? SSI_SxCCR_PSR : 0);
	mask = SSI_SxCCR_PM_MASK | SSI_SxCCR_DIV2 | SSI_SxCCR_PSR;

	/* STCCR is used for RX in synchronous mode */
	tx2 = tx || synchronous;
	regmap_update_bits(regs, REG_SSI_SxCCR(tx2), mask, stccr);

	if (!baudclk_is_used) {
		ret = clk_set_rate(ssi->baudclk, baudrate);
		if (ret) {
			dev_err(dai->dev, "failed to set baudclk rate\n");
			return -EINVAL;
		}
	}

	return 0;
}

/**
 * Configure SSI based on PCM hardware parameters
 *
 * Notes:
 * 1) SxCCR.WL bits are critical bits that require SSI to be temporarily
 *    disabled on offline_config SoCs. Even for online configurable SoCs
 *    running in synchronous mode (both TX and RX use STCCR), it is not
 *    safe to re-configure them when both two streams start running.
 * 2) SxCCR.PM, SxCCR.DIV2 and SxCCR.PSR bits will be configured in the
 *    fsl_ssi_set_bclk() if SSI is the DAI clock master.
 */
static int fsl_ssi_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *hw_params,
			     struct snd_soc_dai *dai)
{
	bool tx2, tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	struct fsl_ssi *ssi = snd_soc_dai_get_drvdata(dai);
	struct regmap *regs = ssi->regs;
	unsigned int channels = params_channels(hw_params);
	unsigned int sample_size = params_width(hw_params);
	u32 wl = SSI_SxCCR_WL(sample_size);
	int ret;
	u32 scr;
	int enabled;

	regmap_read(regs, REG_SSI_SCR, &scr);
	enabled = scr & SSI_SCR_SSIEN;

	/*
	 * SSI is properly configured if it is enabled and running in
	 * the synchronous mode; Note that AC97 mode is an exception
	 * that should set separate configurations for STCCR and SRCCR
	 * despite running in the synchronous mode.
	 */
	if (enabled && ssi->cpu_dai_drv.symmetric_rates)
		return 0;

	if (fsl_ssi_is_i2s_master(ssi)) {
		ret = fsl_ssi_set_bclk(substream, dai, hw_params);
		if (ret)
			return ret;

		/* Do not enable the clock if it is already enabled */
		if (!(ssi->baudclk_streams & BIT(substream->stream))) {
			ret = clk_prepare_enable(ssi->baudclk);
			if (ret)
				return ret;

			ssi->baudclk_streams |= BIT(substream->stream);
		}
	}

	if (!fsl_ssi_is_ac97(ssi)) {
		u8 i2s_net;
		/* Normal + Network mode to send 16-bit data in 32-bit frames */
		if (fsl_ssi_is_i2s_cbm_cfs(ssi) && sample_size == 16)
			i2s_net = SSI_SCR_I2S_MODE_NORMAL | SSI_SCR_NET;
		else
			i2s_net = ssi->i2s_net;

		regmap_update_bits(regs, REG_SSI_SCR,
				   SSI_SCR_I2S_NET_MASK,
				   channels == 1 ? 0 : i2s_net);
	}

	/* In synchronous mode, the SSI uses STCCR for capture */
	tx2 = tx || ssi->cpu_dai_drv.symmetric_rates;
	regmap_update_bits(regs, REG_SSI_SxCCR(tx2), SSI_SxCCR_WL_MASK, wl);

	return 0;
}

static int fsl_ssi_hw_free(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct fsl_ssi *ssi = snd_soc_dai_get_drvdata(rtd->cpu_dai);

	if (fsl_ssi_is_i2s_master(ssi) &&
	    ssi->baudclk_streams & BIT(substream->stream)) {
		clk_disable_unprepare(ssi->baudclk);
		ssi->baudclk_streams &= ~BIT(substream->stream);
	}

	return 0;
}

static int _fsl_ssi_set_dai_fmt(struct device *dev,
				struct fsl_ssi *ssi, unsigned int fmt)
{
	struct regmap *regs = ssi->regs;
	u32 strcr = 0, stcr, srcr, scr, mask;
	u8 wm;

	ssi->dai_fmt = fmt;

	if (fsl_ssi_is_i2s_master(ssi) && IS_ERR(ssi->baudclk)) {
		dev_err(dev, "missing baudclk for master mode\n");
		return -EINVAL;
	}

	fsl_ssi_setup_regvals(ssi);

	regmap_read(regs, REG_SSI_SCR, &scr);
	scr &= ~(SSI_SCR_SYN | SSI_SCR_I2S_MODE_MASK);
	/* Synchronize frame sync clock for TE to avoid data slipping */
	scr |= SSI_SCR_SYNC_TX_FS;

	mask = SSI_STCR_TXBIT0 | SSI_STCR_TFDIR | SSI_STCR_TXDIR |
	       SSI_STCR_TSCKP | SSI_STCR_TFSI | SSI_STCR_TFSL | SSI_STCR_TEFS;
	regmap_read(regs, REG_SSI_STCR, &stcr);
	regmap_read(regs, REG_SSI_SRCR, &srcr);
	stcr &= ~mask;
	srcr &= ~mask;

	/* Use Network mode as default */
	ssi->i2s_net = SSI_SCR_NET;
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		regmap_update_bits(regs, REG_SSI_STCCR,
				   SSI_SxCCR_DC_MASK, SSI_SxCCR_DC(2));
		regmap_update_bits(regs, REG_SSI_SRCCR,
				   SSI_SxCCR_DC_MASK, SSI_SxCCR_DC(2));
		switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
		case SND_SOC_DAIFMT_CBM_CFS:
		case SND_SOC_DAIFMT_CBS_CFS:
			ssi->i2s_net |= SSI_SCR_I2S_MODE_MASTER;
			break;
		case SND_SOC_DAIFMT_CBM_CFM:
			ssi->i2s_net |= SSI_SCR_I2S_MODE_SLAVE;
			break;
		default:
			return -EINVAL;
		}

		/* Data on rising edge of bclk, frame low, 1clk before data */
		strcr |= SSI_STCR_TFSI | SSI_STCR_TSCKP |
			 SSI_STCR_TXBIT0 | SSI_STCR_TEFS;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		/* Data on rising edge of bclk, frame high */
		strcr |= SSI_STCR_TXBIT0 | SSI_STCR_TSCKP;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		/* Data on rising edge of bclk, frame high, 1clk before data */
		strcr |= SSI_STCR_TFSL | SSI_STCR_TSCKP |
			 SSI_STCR_TXBIT0 | SSI_STCR_TEFS;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		/* Data on rising edge of bclk, frame high */
		strcr |= SSI_STCR_TFSL | SSI_STCR_TSCKP | SSI_STCR_TXBIT0;
		break;
	case SND_SOC_DAIFMT_AC97:
		/* Data on falling edge of bclk, frame high, 1clk before data */
		ssi->i2s_net |= SSI_SCR_I2S_MODE_NORMAL;
		break;
	default:
		return -EINVAL;
	}
	scr |= ssi->i2s_net;

	/* DAI clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		/* Nothing to do for both normal cases */
		break;
	case SND_SOC_DAIFMT_IB_NF:
		/* Invert bit clock */
		strcr ^= SSI_STCR_TSCKP;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		/* Invert frame clock */
		strcr ^= SSI_STCR_TFSI;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		/* Invert both clocks */
		strcr ^= SSI_STCR_TSCKP;
		strcr ^= SSI_STCR_TFSI;
		break;
	default:
		return -EINVAL;
	}

	/* DAI clock master masks */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		/* Output bit and frame sync clocks */
		strcr |= SSI_STCR_TFDIR | SSI_STCR_TXDIR;
		scr |= SSI_SCR_SYS_CLK_EN;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		/* Input bit or frame sync clocks */
		scr &= ~SSI_SCR_SYS_CLK_EN;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
		/* Input bit clock but output frame sync clock */
		strcr &= ~SSI_STCR_TXDIR;
		strcr |= SSI_STCR_TFDIR;
		scr &= ~SSI_SCR_SYS_CLK_EN;
		break;
	default:
		if (!fsl_ssi_is_ac97(ssi))
			return -EINVAL;
	}

	stcr |= strcr;
	srcr |= strcr;

	/* Set SYN mode and clear RXDIR bit when using SYN or AC97 mode */
	if (ssi->cpu_dai_drv.symmetric_rates || fsl_ssi_is_ac97(ssi)) {
		srcr &= ~SSI_SRCR_RXDIR;
		scr |= SSI_SCR_SYN;
	}

	regmap_write(regs, REG_SSI_STCR, stcr);
	regmap_write(regs, REG_SSI_SRCR, srcr);
	regmap_write(regs, REG_SSI_SCR, scr);

	wm = ssi->fifo_watermark;

	regmap_write(regs, REG_SSI_SFCSR,
		     SSI_SFCSR_TFWM0(wm) | SSI_SFCSR_RFWM0(wm) |
		     SSI_SFCSR_TFWM1(wm) | SSI_SFCSR_RFWM1(wm));

	if (ssi->use_dual_fifo) {
		regmap_update_bits(regs, REG_SSI_SRCR,
				   SSI_SRCR_RFEN1, SSI_SRCR_RFEN1);
		regmap_update_bits(regs, REG_SSI_STCR,
				   SSI_STCR_TFEN1, SSI_STCR_TFEN1);
		regmap_update_bits(regs, REG_SSI_SCR,
				   SSI_SCR_TCH_EN, SSI_SCR_TCH_EN);
	}

	if ((fmt & SND_SOC_DAIFMT_FORMAT_MASK) == SND_SOC_DAIFMT_AC97)
		fsl_ssi_setup_ac97(ssi);

	return 0;
}

/**
 * Configure Digital Audio Interface (DAI) Format
 */
static int fsl_ssi_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct fsl_ssi *ssi = snd_soc_dai_get_drvdata(dai);

	/* AC97 configured DAIFMT earlier in the probe() */
	if (fsl_ssi_is_ac97(ssi))
		return 0;

	return _fsl_ssi_set_dai_fmt(dai->dev, ssi, fmt);
}

/**
 * Set TDM slot number and slot width
 */
static int fsl_ssi_set_dai_tdm_slot(struct snd_soc_dai *dai, u32 tx_mask,
				    u32 rx_mask, int slots, int slot_width)
{
	struct fsl_ssi *ssi = snd_soc_dai_get_drvdata(dai);
	struct regmap *regs = ssi->regs;
	u32 val;

	/* The word length should be 8, 10, 12, 16, 18, 20, 22 or 24 */
	if (slot_width & 1 || slot_width < 8 || slot_width > 24) {
		dev_err(dai->dev, "invalid slot width: %d\n", slot_width);
		return -EINVAL;
	}

	/* The slot number should be >= 2 if using Network mode or I2S mode */
	regmap_read(regs, REG_SSI_SCR, &val);
	val &= SSI_SCR_I2S_MODE_MASK | SSI_SCR_NET;
	if (val && slots < 2) {
		dev_err(dai->dev, "slot number should be >= 2 in I2S or NET\n");
		return -EINVAL;
	}

	regmap_update_bits(regs, REG_SSI_STCCR,
			   SSI_SxCCR_DC_MASK, SSI_SxCCR_DC(slots));
	regmap_update_bits(regs, REG_SSI_SRCCR,
			   SSI_SxCCR_DC_MASK, SSI_SxCCR_DC(slots));

	/* Save SSIEN bit of the SCR register */
	regmap_read(regs, REG_SSI_SCR, &val);
	val &= SSI_SCR_SSIEN;
	/* Temporarily enable SSI to allow SxMSKs to be configurable */
	regmap_update_bits(regs, REG_SSI_SCR, SSI_SCR_SSIEN, SSI_SCR_SSIEN);

	regmap_write(regs, REG_SSI_STMSK, ~tx_mask);
	regmap_write(regs, REG_SSI_SRMSK, ~rx_mask);

	/* Restore the value of SSIEN bit */
	regmap_update_bits(regs, REG_SSI_SCR, SSI_SCR_SSIEN, val);

	ssi->slot_width = slot_width;
	ssi->slots = slots;

	return 0;
}

/**
 * Start or stop SSI and corresponding DMA transaction.
 *
 * The DMA channel is in external master start and pause mode, which
 * means the SSI completely controls the flow of data.
 */
static int fsl_ssi_trigger(struct snd_pcm_substream *substream, int cmd,
			   struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct fsl_ssi *ssi = snd_soc_dai_get_drvdata(rtd->cpu_dai);
	struct regmap *regs = ssi->regs;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			fsl_ssi_tx_config(ssi, true);
		else
			fsl_ssi_rx_config(ssi, true);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			fsl_ssi_tx_config(ssi, false);
		else
			fsl_ssi_rx_config(ssi, false);
		break;

	default:
		return -EINVAL;
	}

	/* Clear corresponding FIFO */
	if (fsl_ssi_is_ac97(ssi)) {
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			regmap_write(regs, REG_SSI_SOR, SSI_SOR_TX_CLR);
		else
			regmap_write(regs, REG_SSI_SOR, SSI_SOR_RX_CLR);
	}

	return 0;
}

static int fsl_ssi_dai_probe(struct snd_soc_dai *dai)
{
	struct fsl_ssi *ssi = snd_soc_dai_get_drvdata(dai);

	if (ssi->soc->imx && ssi->use_dma) {
		dai->playback_dma_data = &ssi->dma_params_tx;
		dai->capture_dma_data = &ssi->dma_params_rx;
	}

	return 0;
}

static const struct snd_soc_dai_ops fsl_ssi_dai_ops = {
	.startup = fsl_ssi_startup,
	.shutdown = fsl_ssi_shutdown,
	.hw_params = fsl_ssi_hw_params,
	.hw_free = fsl_ssi_hw_free,
	.set_fmt = fsl_ssi_set_dai_fmt,
	.set_tdm_slot = fsl_ssi_set_dai_tdm_slot,
	.trigger = fsl_ssi_trigger,
};

static struct snd_soc_dai_driver fsl_ssi_dai_template = {
	.probe = fsl_ssi_dai_probe,
	.playback = {
		.stream_name = "CPU-Playback",
		.channels_min = 1,
		.channels_max = 32,
		.rates = SNDRV_PCM_RATE_CONTINUOUS,
		.formats = FSLSSI_I2S_FORMATS,
	},
	.capture = {
		.stream_name = "CPU-Capture",
		.channels_min = 1,
		.channels_max = 32,
		.rates = SNDRV_PCM_RATE_CONTINUOUS,
		.formats = FSLSSI_I2S_FORMATS,
	},
	.ops = &fsl_ssi_dai_ops,
};

static const struct snd_soc_component_driver fsl_ssi_component = {
	.name = "fsl-ssi",
};

static struct snd_soc_dai_driver fsl_ssi_ac97_dai = {
	.bus_control = true,
	.probe = fsl_ssi_dai_probe,
	.playback = {
		.stream_name = "AC97 Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FMTBIT_S16 | SNDRV_PCM_FMTBIT_S20,
	},
	.capture = {
		.stream_name = "AC97 Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		/* 16-bit capture is broken (errata ERR003778) */
		.formats = SNDRV_PCM_FMTBIT_S20,
	},
	.ops = &fsl_ssi_dai_ops,
};

static struct fsl_ssi *fsl_ac97_data;

static void fsl_ssi_ac97_write(struct snd_ac97 *ac97, unsigned short reg,
			       unsigned short val)
{
	struct regmap *regs = fsl_ac97_data->regs;
	unsigned int lreg;
	unsigned int lval;
	int ret;

	if (reg > 0x7f)
		return;

	mutex_lock(&fsl_ac97_data->ac97_reg_lock);

	ret = clk_prepare_enable(fsl_ac97_data->clk);
	if (ret) {
		pr_err("ac97 write clk_prepare_enable failed: %d\n",
			ret);
		goto ret_unlock;
	}

	lreg = reg <<  12;
	regmap_write(regs, REG_SSI_SACADD, lreg);

	lval = val << 4;
	regmap_write(regs, REG_SSI_SACDAT, lval);

	regmap_update_bits(regs, REG_SSI_SACNT,
			   SSI_SACNT_RDWR_MASK, SSI_SACNT_WR);
	udelay(100);

	clk_disable_unprepare(fsl_ac97_data->clk);

ret_unlock:
	mutex_unlock(&fsl_ac97_data->ac97_reg_lock);
}

static unsigned short fsl_ssi_ac97_read(struct snd_ac97 *ac97,
					unsigned short reg)
{
	struct regmap *regs = fsl_ac97_data->regs;
	unsigned short val = 0;
	u32 reg_val;
	unsigned int lreg;
	int ret;

	mutex_lock(&fsl_ac97_data->ac97_reg_lock);

	ret = clk_prepare_enable(fsl_ac97_data->clk);
	if (ret) {
		pr_err("ac97 read clk_prepare_enable failed: %d\n", ret);
		goto ret_unlock;
	}

	lreg = (reg & 0x7f) <<  12;
	regmap_write(regs, REG_SSI_SACADD, lreg);
	regmap_update_bits(regs, REG_SSI_SACNT,
			   SSI_SACNT_RDWR_MASK, SSI_SACNT_RD);

	udelay(100);

	regmap_read(regs, REG_SSI_SACDAT, &reg_val);
	val = (reg_val >> 4) & 0xffff;

	clk_disable_unprepare(fsl_ac97_data->clk);

ret_unlock:
	mutex_unlock(&fsl_ac97_data->ac97_reg_lock);
	return val;
}

static struct snd_ac97_bus_ops fsl_ssi_ac97_ops = {
	.read = fsl_ssi_ac97_read,
	.write = fsl_ssi_ac97_write,
};

/**
 * Make every character in a string lower-case
 */
static void make_lowercase(char *s)
{
	if (!s)
		return;
	for (; *s; s++)
		*s = tolower(*s);
}

static int fsl_ssi_imx_probe(struct platform_device *pdev,
			     struct fsl_ssi *ssi, void __iomem *iomem)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	u32 dmas[4];
	int ret;

	/* Backward compatible for a DT without ipg clock name assigned */
	if (ssi->has_ipg_clk_name)
		ssi->clk = devm_clk_get(dev, "ipg");
	else
		ssi->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(ssi->clk)) {
		ret = PTR_ERR(ssi->clk);
		dev_err(dev, "failed to get clock: %d\n", ret);
		return ret;
	}

	/* Enable the clock since regmap will not handle it in this case */
	if (!ssi->has_ipg_clk_name) {
		ret = clk_prepare_enable(ssi->clk);
		if (ret) {
			dev_err(dev, "clk_prepare_enable failed: %d\n", ret);
			return ret;
		}
	}

	/* Do not error out for slave cases that live without a baud clock */
	ssi->baudclk = devm_clk_get(dev, "baud");
	if (IS_ERR(ssi->baudclk))
		dev_dbg(dev, "failed to get baud clock: %ld\n",
			 PTR_ERR(ssi->baudclk));

	ssi->dma_params_tx.maxburst = ssi->dma_maxburst;
	ssi->dma_params_rx.maxburst = ssi->dma_maxburst;
	ssi->dma_params_tx.addr = ssi->ssi_phys + REG_SSI_STX0;
	ssi->dma_params_rx.addr = ssi->ssi_phys + REG_SSI_SRX0;

	/* Set to dual FIFO mode according to the SDMA sciprt */
	ret = of_property_read_u32_array(np, "dmas", dmas, 4);
	if (ssi->use_dma && !ret && dmas[2] == IMX_DMATYPE_SSI_DUAL) {
		ssi->use_dual_fifo = true;
		/*
		 * Use even numbers to avoid channel swap due to SDMA
		 * script design
		 */
		ssi->dma_params_tx.maxburst &= ~0x1;
		ssi->dma_params_rx.maxburst &= ~0x1;
	}

	if (!ssi->use_dma) {
		/*
		 * Some boards use an incompatible codec. Use imx-fiq-pcm-audio
		 * to get it working, as DMA is not possible in this situation.
		 */
		ssi->fiq_params.irq = ssi->irq;
		ssi->fiq_params.base = iomem;
		ssi->fiq_params.dma_params_rx = &ssi->dma_params_rx;
		ssi->fiq_params.dma_params_tx = &ssi->dma_params_tx;

		ret = imx_pcm_fiq_init(pdev, &ssi->fiq_params);
		if (ret)
			goto error_pcm;
	} else {
		ret = imx_pcm_dma_init(pdev, IMX_SSI_DMABUF_SIZE);
		if (ret)
			goto error_pcm;
	}

	return 0;

error_pcm:
	if (!ssi->has_ipg_clk_name)
		clk_disable_unprepare(ssi->clk);

	return ret;
}

static void fsl_ssi_imx_clean(struct platform_device *pdev, struct fsl_ssi *ssi)
{
	if (!ssi->use_dma)
		imx_pcm_fiq_exit(pdev);
	if (!ssi->has_ipg_clk_name)
		clk_disable_unprepare(ssi->clk);
}

static int fsl_ssi_probe(struct platform_device *pdev)
{
	struct fsl_ssi *ssi;
	int ret = 0;
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	const struct of_device_id *of_id;
	const char *p, *sprop;
	const uint32_t *iprop;
	struct resource *res;
	void __iomem *iomem;
	char name[64];
	struct regmap_config regconfig = fsl_ssi_regconfig;

	of_id = of_match_device(fsl_ssi_ids, dev);
	if (!of_id || !of_id->data)
		return -EINVAL;

	ssi = devm_kzalloc(dev, sizeof(*ssi), GFP_KERNEL);
	if (!ssi)
		return -ENOMEM;

	ssi->soc = of_id->data;
	ssi->dev = dev;

	/* Check if being used in AC97 mode */
	sprop = of_get_property(np, "fsl,mode", NULL);
	if (sprop) {
		if (!strcmp(sprop, "ac97-slave"))
			ssi->dai_fmt = SND_SOC_DAIFMT_AC97;
	}

	/* Select DMA or FIQ */
	ssi->use_dma = !of_property_read_bool(np, "fsl,fiq-stream-filter");

	if (fsl_ssi_is_ac97(ssi)) {
		memcpy(&ssi->cpu_dai_drv, &fsl_ssi_ac97_dai,
		       sizeof(fsl_ssi_ac97_dai));
		fsl_ac97_data = ssi;
	} else {
		memcpy(&ssi->cpu_dai_drv, &fsl_ssi_dai_template,
		       sizeof(fsl_ssi_dai_template));
	}
	ssi->cpu_dai_drv.name = dev_name(dev);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	iomem = devm_ioremap_resource(dev, res);
	if (IS_ERR(iomem))
		return PTR_ERR(iomem);
	ssi->ssi_phys = res->start;

	if (ssi->soc->imx21regs) {
		/* No SACC{ST,EN,DIS} regs in imx21-class SSI */
		regconfig.max_register = REG_SSI_SRMSK;
		regconfig.num_reg_defaults_raw =
			REG_SSI_SRMSK / sizeof(uint32_t) + 1;
	}

	ret = of_property_match_string(np, "clock-names", "ipg");
	if (ret < 0) {
		ssi->has_ipg_clk_name = false;
		ssi->regs = devm_regmap_init_mmio(dev, iomem, &regconfig);
	} else {
		ssi->has_ipg_clk_name = true;
		ssi->regs = devm_regmap_init_mmio_clk(dev, "ipg", iomem,
						      &regconfig);
	}
	if (IS_ERR(ssi->regs)) {
		dev_err(dev, "failed to init register map\n");
		return PTR_ERR(ssi->regs);
	}

	ssi->irq = platform_get_irq(pdev, 0);
	if (ssi->irq < 0) {
		dev_err(dev, "no irq for node %s\n", pdev->name);
		return ssi->irq;
	}

	/* Set software limitations for synchronous mode */
	if (!of_find_property(np, "fsl,ssi-asynchronous", NULL)) {
		if (!fsl_ssi_is_ac97(ssi)) {
			ssi->cpu_dai_drv.symmetric_rates = 1;
			ssi->cpu_dai_drv.symmetric_samplebits = 1;
		}

		ssi->cpu_dai_drv.symmetric_channels = 1;
	}

	/* Fetch FIFO depth; Set to 8 for older DT without this property */
	iprop = of_get_property(np, "fsl,fifo-depth", NULL);
	if (iprop)
		ssi->fifo_depth = be32_to_cpup(iprop);
	else
		ssi->fifo_depth = 8;

	/*
	 * Configure TX and RX DMA watermarks -- when to send a DMA request
	 *
	 * Values should be tested to avoid FIFO under/over run. Set maxburst
	 * to fifo_watermark to maxiumize DMA transaction to reduce overhead.
	 */
	switch (ssi->fifo_depth) {
	case 15:
		/*
		 * Set to 8 as a balanced configuration -- When TX FIFO has 8
		 * empty slots, send a DMA request to fill these 8 slots. The
		 * remaining 7 slots should be able to allow DMA to finish the
		 * transaction before TX FIFO underruns; Same applies to RX.
		 *
		 * Tested with cases running at 48kHz @ 16 bits x 16 channels
		 */
		ssi->fifo_watermark = 8;
		ssi->dma_maxburst = 8;
		break;
	case 8:
	default:
		/* Safely use old watermark configurations for older chips */
		ssi->fifo_watermark = ssi->fifo_depth - 2;
		ssi->dma_maxburst = ssi->fifo_depth - 2;
		break;
	}

	dev_set_drvdata(dev, ssi);

	if (ssi->soc->imx) {
		ret = fsl_ssi_imx_probe(pdev, ssi, iomem);
		if (ret)
			return ret;
	}

	if (fsl_ssi_is_ac97(ssi)) {
		mutex_init(&ssi->ac97_reg_lock);
		ret = snd_soc_set_ac97_ops_of_reset(&fsl_ssi_ac97_ops, pdev);
		if (ret) {
			dev_err(dev, "failed to set AC'97 ops\n");
			goto error_ac97_ops;
		}
	}

	ret = devm_snd_soc_register_component(dev, &fsl_ssi_component,
					      &ssi->cpu_dai_drv, 1);
	if (ret) {
		dev_err(dev, "failed to register DAI: %d\n", ret);
		goto error_asoc_register;
	}

	if (ssi->use_dma) {
		ret = devm_request_irq(dev, ssi->irq, fsl_ssi_isr, 0,
				       dev_name(dev), ssi);
		if (ret < 0) {
			dev_err(dev, "failed to claim irq %u\n", ssi->irq);
			goto error_asoc_register;
		}
	}

	ret = fsl_ssi_debugfs_create(&ssi->dbg_stats, dev);
	if (ret)
		goto error_asoc_register;

	/* Bypass it if using newer DT bindings of ASoC machine drivers */
	if (!of_get_property(np, "codec-handle", NULL))
		goto done;

	/*
	 * Backward compatible for older bindings by manually triggering the
	 * machine driver's probe(). Use /compatible property, including the
	 * address of CPU DAI driver structure, as the name of machine driver.
	 */
	sprop = of_get_property(of_find_node_by_path("/"), "compatible", NULL);
	/* Sometimes the compatible name has a "fsl," prefix, so we strip it. */
	p = strrchr(sprop, ',');
	if (p)
		sprop = p + 1;
	snprintf(name, sizeof(name), "snd-soc-%s", sprop);
	make_lowercase(name);

	ssi->pdev = platform_device_register_data(dev, name, 0, NULL, 0);
	if (IS_ERR(ssi->pdev)) {
		ret = PTR_ERR(ssi->pdev);
		dev_err(dev, "failed to register platform: %d\n", ret);
		goto error_sound_card;
	}

done:
	if (ssi->dai_fmt)
		_fsl_ssi_set_dai_fmt(dev, ssi, ssi->dai_fmt);

	if (fsl_ssi_is_ac97(ssi)) {
		u32 ssi_idx;

		ret = of_property_read_u32(np, "cell-index", &ssi_idx);
		if (ret) {
			dev_err(dev, "failed to get SSI index property\n");
			goto error_sound_card;
		}

		ssi->pdev = platform_device_register_data(NULL, "ac97-codec",
							  ssi_idx, NULL, 0);
		if (IS_ERR(ssi->pdev)) {
			ret = PTR_ERR(ssi->pdev);
			dev_err(dev,
				"failed to register AC97 codec platform: %d\n",
				ret);
			goto error_sound_card;
		}
	}

	return 0;

error_sound_card:
	fsl_ssi_debugfs_remove(&ssi->dbg_stats);
error_asoc_register:
	if (fsl_ssi_is_ac97(ssi))
		snd_soc_set_ac97_ops(NULL);
error_ac97_ops:
	if (fsl_ssi_is_ac97(ssi))
		mutex_destroy(&ssi->ac97_reg_lock);

	if (ssi->soc->imx)
		fsl_ssi_imx_clean(pdev, ssi);

	return ret;
}

static int fsl_ssi_remove(struct platform_device *pdev)
{
	struct fsl_ssi *ssi = dev_get_drvdata(&pdev->dev);

	fsl_ssi_debugfs_remove(&ssi->dbg_stats);

	if (ssi->pdev)
		platform_device_unregister(ssi->pdev);

	if (ssi->soc->imx)
		fsl_ssi_imx_clean(pdev, ssi);

	if (fsl_ssi_is_ac97(ssi)) {
		snd_soc_set_ac97_ops(NULL);
		mutex_destroy(&ssi->ac97_reg_lock);
	}

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int fsl_ssi_suspend(struct device *dev)
{
	struct fsl_ssi *ssi = dev_get_drvdata(dev);
	struct regmap *regs = ssi->regs;

	regmap_read(regs, REG_SSI_SFCSR, &ssi->regcache_sfcsr);
	regmap_read(regs, REG_SSI_SACNT, &ssi->regcache_sacnt);

	regcache_cache_only(regs, true);
	regcache_mark_dirty(regs);

	return 0;
}

static int fsl_ssi_resume(struct device *dev)
{
	struct fsl_ssi *ssi = dev_get_drvdata(dev);
	struct regmap *regs = ssi->regs;

	regcache_cache_only(regs, false);

	regmap_update_bits(regs, REG_SSI_SFCSR,
			   SSI_SFCSR_RFWM1_MASK | SSI_SFCSR_TFWM1_MASK |
			   SSI_SFCSR_RFWM0_MASK | SSI_SFCSR_TFWM0_MASK,
			   ssi->regcache_sfcsr);
	regmap_write(regs, REG_SSI_SACNT, ssi->regcache_sacnt);

	return regcache_sync(regs);
}
#endif /* CONFIG_PM_SLEEP */

static const struct dev_pm_ops fsl_ssi_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(fsl_ssi_suspend, fsl_ssi_resume)
};

static struct platform_driver fsl_ssi_driver = {
	.driver = {
		.name = "fsl-ssi-dai",
		.of_match_table = fsl_ssi_ids,
		.pm = &fsl_ssi_pm,
	},
	.probe = fsl_ssi_probe,
	.remove = fsl_ssi_remove,
};

module_platform_driver(fsl_ssi_driver);

MODULE_ALIAS("platform:fsl-ssi-dai");
MODULE_AUTHOR("Timur Tabi <timur@freescale.com>");
MODULE_DESCRIPTION("Freescale Synchronous Serial Interface (SSI) ASoC Driver");
MODULE_LICENSE("GPL v2");
