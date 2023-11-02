// SPDX-License-Identifier: GPL-2.0
//
// Freescale SSI ALSA SoC Digital Audio Interface (DAI) driver
//
// Author: Timur Tabi <timur@freescale.com>
//
// Copyright 2007-2010 Freescale Semiconductor, Inc.
//
// Some notes why imx-pcm-fiq is used instead of DMA on some boards:
//
// The i.MX SSI core has some nasty limitations in AC97 mode. While most
// sane processor vendors have a FIFO per AC97 slot, the i.MX has only
// one FIFO which combines all valid receive slots. We cannot even select
// which slots we want to receive. The WM9712 with which this driver
// was developed with always sends GPIO status data in slot 12 which
// we receive in our (PCM-) data stream. The only chance we have is to
// manually skip this data in the FIQ handler. With sampling rates different
// from 48000Hz not every frame has valid receive data, so the ratio
// between pcm data and GPIO status data changes. Our FIQ handler is not
// able to handle this, hence this driver only works with 48000Hz sampling
// rate.
// Reading and writing AC97 registers is another challenge. The core
// provides us status bits when the read register is updated with *another*
// value. When we read the same register two times (and the register still
// contains the same value) these status bits are not set. We work
// around this by not polling these bits but only wait a fixed delay.

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
#include <linux/dma/imx-dma.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

#include "fsl_ssi.h"
#include "imx-pcm.h"

/* Define RX and TX to index ssi->regvals array; Can be 0 or 1 only */
#define RX 0
#define TX 1

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

/*
 * In AC97 mode, TXDIR bit is forced to 0 and TFDIR bit is forced to 1:
 *  - SSI inputs external bit clock and outputs frame sync clock -- CBM_CFS
 *  - Also have NB_NF to mark these two clocks will not be inverted
 */
#define FSLSSI_AC97_DAIFMT \
	(SND_SOC_DAIFMT_AC97 | \
	 SND_SOC_DAIFMT_BC_FP | \
	 SND_SOC_DAIFMT_NB_NF)

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
 * struct fsl_ssi - per-SSI private data
 * @regs: Pointer to the regmap registers
 * @irq: IRQ of this SSI
 * @cpu_dai_drv: CPU DAI driver for this device
 * @dai_fmt: DAI configuration this device is currently used with
 * @streams: Mask of current active streams: BIT(TX) and BIT(RX)
 * @i2s_net: I2S and Network mode configurations of SCR register
 *           (this is the initial settings based on the DAI format)
 * @synchronous: Use synchronous mode - both of TX and RX use STCK and SFCK
 * @use_dma: DMA is used or FIQ with stream filter
 * @use_dual_fifo: DMA with support for dual FIFO mode
 * @use_dyna_fifo: DMA with support for multi FIFO script
 * @has_ipg_clk_name: If "ipg" is in the clock name list of device tree
 * @fifo_depth: Depth of the SSI FIFOs
 * @slot_width: Width of each DAI slot
 * @slots: Number of slots
 * @regvals: Specific RX/TX register settings
 * @clk: Clock source to access register
 * @baudclk: Clock source to generate bit and frame-sync clocks
 * @baudclk_streams: Active streams that are using baudclk
 * @regcache_sfcsr: Cache sfcsr register value during suspend and resume
 * @regcache_sacnt: Cache sacnt register value during suspend and resume
 * @dma_params_tx: DMA transmit parameters
 * @dma_params_rx: DMA receive parameters
 * @ssi_phys: physical address of the SSI registers
 * @fiq_params: FIQ stream filtering parameters
 * @card_pdev: Platform_device pointer to register a sound card for PowerPC or
 *             to register a CODEC platform device for AC97
 * @card_name: Platform_device name to register a sound card for PowerPC or
 *             to register a CODEC platform device for AC97
 * @card_idx: The index of SSI to register a sound card for PowerPC or
 *            to register a CODEC platform device for AC97
 * @dbg_stats: Debugging statistics
 * @soc: SoC specific data
 * @dev: Pointer to &pdev->dev
 * @fifo_watermark: The FIFO watermark setting. Notifies DMA when there are
 *                  @fifo_watermark or fewer words in TX fifo or
 *                  @fifo_watermark or more empty words in RX fifo.
 * @dma_maxburst: Max number of words to transfer in one go. So far,
 *                this is always the same as fifo_watermark.
 * @ac97_reg_lock: Mutex lock to serialize AC97 register access operations
 * @audio_config: configure for dma multi fifo script
 */
struct fsl_ssi {
	struct regmap *regs;
	int irq;
	struct snd_soc_dai_driver cpu_dai_drv;

	unsigned int dai_fmt;
	u8 streams;
	u8 i2s_net;
	bool synchronous;
	bool use_dma;
	bool use_dual_fifo;
	bool use_dyna_fifo;
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

	struct platform_device *card_pdev;
	char card_name[32];
	u32 card_idx;

	struct fsl_ssi_dbg dbg_stats;

	const struct fsl_ssi_soc_data *soc;
	struct device *dev;

	u32 fifo_watermark;
	u32 dma_maxburst;

	struct mutex ac97_reg_lock;
	struct sdma_peripheral_config audio_config[2];
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

static bool fsl_ssi_is_i2s_clock_provider(struct fsl_ssi *ssi)
{
	return (ssi->dai_fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) ==
		SND_SOC_DAIFMT_BP_FP;
}

static bool fsl_ssi_is_i2s_bc_fp(struct fsl_ssi *ssi)
{
	return (ssi->dai_fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) ==
		SND_SOC_DAIFMT_BC_FP;
}

/**
 * fsl_ssi_isr - Interrupt handler to gather states
 * @irq: irq number
 * @dev_id: context
 */
static irqreturn_t fsl_ssi_isr(int irq, void *dev_id)
{
	struct fsl_ssi *ssi = dev_id;
	struct regmap *regs = ssi->regs;
	u32 sisr, sisr2;

	regmap_read(regs, REG_SSI_SISR, &sisr);

	sisr2 = sisr & ssi->soc->sisr_write_mask;
	/* Clear the bits that we set */
	if (sisr2)
		regmap_write(regs, REG_SSI_SISR, sisr2);

	fsl_ssi_dbg_isr(&ssi->dbg_stats, sisr);

	return IRQ_HANDLED;
}

/**
 * fsl_ssi_config_enable - Set SCR, SIER, STCR and SRCR registers with
 * cached values in regvals
 * @ssi: SSI context
 * @tx: direction
 *
 * Notes:
 * 1) For offline_config SoCs, enable all necessary bits of both streams
 *    when 1st stream starts, even if the opposite stream will not start
 * 2) It also clears FIFO before setting regvals; SOR is safe to set online
 */
static void fsl_ssi_config_enable(struct fsl_ssi *ssi, bool tx)
{
	struct fsl_ssi_regvals *vals = ssi->regvals;
	int dir = tx ? TX : RX;
	u32 sier, srcr, stcr;

	/* Clear dirty data in the FIFO; It also prevents channel slipping */
	regmap_update_bits(ssi->regs, REG_SSI_SOR,
			   SSI_SOR_xX_CLR(tx), SSI_SOR_xX_CLR(tx));

	/*
	 * On offline_config SoCs, SxCR and SIER are already configured when
	 * the previous stream started. So skip all SxCR and SIER settings
	 * to prevent online reconfigurations, then jump to set SCR directly
	 */
	if (ssi->soc->offline_config && ssi->streams)
		goto enable_scr;

	if (ssi->soc->offline_config) {
		/*
		 * Online reconfiguration not supported, so enable all bits for
		 * both streams at once to avoid necessity of reconfigurations
		 */
		srcr = vals[RX].srcr | vals[TX].srcr;
		stcr = vals[RX].stcr | vals[TX].stcr;
		sier = vals[RX].sier | vals[TX].sier;
	} else {
		/* Otherwise, only set bits for the current stream */
		srcr = vals[dir].srcr;
		stcr = vals[dir].stcr;
		sier = vals[dir].sier;
	}

	/* Configure SRCR, STCR and SIER at once */
	regmap_update_bits(ssi->regs, REG_SSI_SRCR, srcr, srcr);
	regmap_update_bits(ssi->regs, REG_SSI_STCR, stcr, stcr);
	regmap_update_bits(ssi->regs, REG_SSI_SIER, sier, sier);

enable_scr:
	/*
	 * Start DMA before setting TE to avoid FIFO underrun
	 * which may cause a channel slip or a channel swap
	 *
	 * TODO: FIQ cases might also need this upon testing
	 */
	if (ssi->use_dma && tx) {
		int try = 100;
		u32 sfcsr;

		/* Enable SSI first to send TX DMA request */
		regmap_update_bits(ssi->regs, REG_SSI_SCR,
				   SSI_SCR_SSIEN, SSI_SCR_SSIEN);

		/* Busy wait until TX FIFO not empty -- DMA working */
		do {
			regmap_read(ssi->regs, REG_SSI_SFCSR, &sfcsr);
			if (SSI_SFCSR_TFCNT0(sfcsr))
				break;
		} while (--try);

		/* FIFO still empty -- something might be wrong */
		if (!SSI_SFCSR_TFCNT0(sfcsr))
			dev_warn(ssi->dev, "Timeout waiting TX FIFO filling\n");
	}
	/* Enable all remaining bits in SCR */
	regmap_update_bits(ssi->regs, REG_SSI_SCR,
			   vals[dir].scr, vals[dir].scr);

	/* Log the enabled stream to the mask */
	ssi->streams |= BIT(dir);
}

/*
 * Exclude bits that are used by the opposite stream
 *
 * When both streams are active, disabling some bits for the current stream
 * might break the other stream if these bits are used by it.
 *
 * @vals : regvals of the current stream
 * @avals: regvals of the opposite stream
 * @aactive: active state of the opposite stream
 *
 *  1) XOR vals and avals to get the differences if the other stream is active;
 *     Otherwise, return current vals if the other stream is not active
 *  2) AND the result of 1) with the current vals
 */
#define _ssi_xor_shared_bits(vals, avals, aactive) \
	((vals) ^ ((avals) * (aactive)))

#define ssi_excl_shared_bits(vals, avals, aactive) \
	((vals) & _ssi_xor_shared_bits(vals, avals, aactive))

/**
 * fsl_ssi_config_disable - Unset SCR, SIER, STCR and SRCR registers
 * with cached values in regvals
 * @ssi: SSI context
 * @tx: direction
 *
 * Notes:
 * 1) For offline_config SoCs, to avoid online reconfigurations, disable all
 *    bits of both streams at once when the last stream is abort to end
 * 2) It also clears FIFO after unsetting regvals; SOR is safe to set online
 */
static void fsl_ssi_config_disable(struct fsl_ssi *ssi, bool tx)
{
	struct fsl_ssi_regvals *vals, *avals;
	u32 sier, srcr, stcr, scr;
	int adir = tx ? RX : TX;
	int dir = tx ? TX : RX;
	bool aactive;

	/* Check if the opposite stream is active */
	aactive = ssi->streams & BIT(adir);

	vals = &ssi->regvals[dir];

	/* Get regvals of the opposite stream to keep opposite stream safe */
	avals = &ssi->regvals[adir];

	/*
	 * To keep the other stream safe, exclude shared bits between
	 * both streams, and get safe bits to disable current stream
	 */
	scr = ssi_excl_shared_bits(vals->scr, avals->scr, aactive);

	/* Disable safe bits of SCR register for the current stream */
	regmap_update_bits(ssi->regs, REG_SSI_SCR, scr, 0);

	/* Log the disabled stream to the mask */
	ssi->streams &= ~BIT(dir);

	/*
	 * On offline_config SoCs, if the other stream is active, skip
	 * SxCR and SIER settings to prevent online reconfigurations
	 */
	if (ssi->soc->offline_config && aactive)
		goto fifo_clear;

	if (ssi->soc->offline_config) {
		/* Now there is only current stream active, disable all bits */
		srcr = vals->srcr | avals->srcr;
		stcr = vals->stcr | avals->stcr;
		sier = vals->sier | avals->sier;
	} else {
		/*
		 * To keep the other stream safe, exclude shared bits between
		 * both streams, and get safe bits to disable current stream
		 */
		sier = ssi_excl_shared_bits(vals->sier, avals->sier, aactive);
		srcr = ssi_excl_shared_bits(vals->srcr, avals->srcr, aactive);
		stcr = ssi_excl_shared_bits(vals->stcr, avals->stcr, aactive);
	}

	/* Clear configurations of SRCR, STCR and SIER at once */
	regmap_update_bits(ssi->regs, REG_SSI_SRCR, srcr, 0);
	regmap_update_bits(ssi->regs, REG_SSI_STCR, stcr, 0);
	regmap_update_bits(ssi->regs, REG_SSI_SIER, sier, 0);

fifo_clear:
	/* Clear remaining data in the FIFO */
	regmap_update_bits(ssi->regs, REG_SSI_SOR,
			   SSI_SOR_xX_CLR(tx), SSI_SOR_xX_CLR(tx));
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

/**
 * fsl_ssi_setup_regvals - Cache critical bits of SIER, SRCR, STCR and
 * SCR to later set them safely
 * @ssi: SSI context
 */
static void fsl_ssi_setup_regvals(struct fsl_ssi *ssi)
{
	struct fsl_ssi_regvals *vals = ssi->regvals;

	vals[RX].sier = SSI_SIER_RFF0_EN | FSLSSI_SIER_DBG_RX_FLAGS;
	vals[RX].srcr = SSI_SRCR_RFEN0;
	vals[RX].scr = SSI_SCR_SSIEN | SSI_SCR_RE;
	vals[TX].sier = SSI_SIER_TFE0_EN | FSLSSI_SIER_DBG_TX_FLAGS;
	vals[TX].stcr = SSI_STCR_TFEN0;
	vals[TX].scr = SSI_SCR_SSIEN | SSI_SCR_TE;

	/* AC97 has already enabled SSIEN, RE and TE, so ignore them */
	if (fsl_ssi_is_ac97(ssi))
		vals[RX].scr = vals[TX].scr = 0;

	if (ssi->use_dual_fifo) {
		vals[RX].srcr |= SSI_SRCR_RFEN1;
		vals[TX].stcr |= SSI_STCR_TFEN1;
	}

	if (ssi->use_dma) {
		vals[RX].sier |= SSI_SIER_RDMAE;
		vals[TX].sier |= SSI_SIER_TDMAE;
	} else {
		vals[RX].sier |= SSI_SIER_RIE;
		vals[TX].sier |= SSI_SIER_TIE;
	}
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
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct fsl_ssi *ssi = snd_soc_dai_get_drvdata(asoc_rtd_to_cpu(rtd, 0));
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
	if (ssi->use_dual_fifo || ssi->use_dyna_fifo)
		snd_pcm_hw_constraint_step(substream->runtime, 0,
					   SNDRV_PCM_HW_PARAM_PERIOD_SIZE, 2);

	return 0;
}

static void fsl_ssi_shutdown(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct fsl_ssi *ssi = snd_soc_dai_get_drvdata(asoc_rtd_to_cpu(rtd, 0));

	clk_disable_unprepare(ssi->clk);
}

/**
 * fsl_ssi_set_bclk - Configure Digital Audio Interface bit clock
 * @substream: ASoC substream
 * @dai: pointer to DAI
 * @hw_params: pointers to hw_params
 *
 * Notes: This function can be only called when using SSI as DAI master
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
	u32 pm = 999, div2, psr, stccr, mask, afreq, factor, i;
	unsigned long clkrate, baudrate, tmprate;
	unsigned int channels = params_channels(hw_params);
	unsigned int slot_width = params_width(hw_params);
	unsigned int slots = 2;
	u64 sub, savesub = 100000;
	unsigned int freq;
	bool baudclk_is_used;
	int ret;

	/* Override slots and slot_width if being specifically set... */
	if (ssi->slots)
		slots = ssi->slots;
	if (ssi->slot_width)
		slot_width = ssi->slot_width;

	/* ...but force 32 bits for stereo audio using I2S Master Mode */
	if (channels == 2 &&
	    (ssi->i2s_net & SSI_SCR_I2S_MODE_MASK) == SSI_SCR_I2S_MODE_MASTER)
		slot_width = 32;

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

		if (sub < savesub && !(i == 0)) {
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

	stccr = SSI_SxCCR_PM(pm + 1);
	mask = SSI_SxCCR_PM_MASK | SSI_SxCCR_DIV2 | SSI_SxCCR_PSR;

	/* STCCR is used for RX in synchronous mode */
	tx2 = tx || ssi->synchronous;
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
 * fsl_ssi_hw_params - Configure SSI based on PCM hardware parameters
 * @substream: ASoC substream
 * @hw_params: pointers to hw_params
 * @dai: pointer to DAI
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
	struct fsl_ssi_regvals *vals = ssi->regvals;
	struct regmap *regs = ssi->regs;
	unsigned int channels = params_channels(hw_params);
	unsigned int sample_size = params_width(hw_params);
	u32 wl = SSI_SxCCR_WL(sample_size);
	int ret;

	if (fsl_ssi_is_i2s_clock_provider(ssi)) {
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

	/*
	 * SSI is properly configured if it is enabled and running in
	 * the synchronous mode; Note that AC97 mode is an exception
	 * that should set separate configurations for STCCR and SRCCR
	 * despite running in the synchronous mode.
	 */
	if (ssi->streams && ssi->synchronous)
		return 0;

	if (!fsl_ssi_is_ac97(ssi)) {
		/*
		 * Keep the ssi->i2s_net intact while having a local variable
		 * to override settings for special use cases. Otherwise, the
		 * ssi->i2s_net will lose the settings for regular use cases.
		 */
		u8 i2s_net = ssi->i2s_net;

		/* Normal + Network mode to send 16-bit data in 32-bit frames */
		if (fsl_ssi_is_i2s_bc_fp(ssi) && sample_size == 16)
			i2s_net = SSI_SCR_I2S_MODE_NORMAL | SSI_SCR_NET;

		/* Use Normal mode to send mono data at 1st slot of 2 slots */
		if (channels == 1)
			i2s_net = SSI_SCR_I2S_MODE_NORMAL;

		regmap_update_bits(regs, REG_SSI_SCR,
				   SSI_SCR_I2S_NET_MASK, i2s_net);
	}

	/* In synchronous mode, the SSI uses STCCR for capture */
	tx2 = tx || ssi->synchronous;
	regmap_update_bits(regs, REG_SSI_SxCCR(tx2), SSI_SxCCR_WL_MASK, wl);

	if (ssi->use_dyna_fifo) {
		if (channels == 1) {
			ssi->audio_config[0].n_fifos_dst = 1;
			ssi->audio_config[1].n_fifos_src = 1;
			vals[RX].srcr &= ~SSI_SRCR_RFEN1;
			vals[TX].stcr &= ~SSI_STCR_TFEN1;
			vals[RX].scr  &= ~SSI_SCR_TCH_EN;
			vals[TX].scr  &= ~SSI_SCR_TCH_EN;
		} else {
			ssi->audio_config[0].n_fifos_dst = 2;
			ssi->audio_config[1].n_fifos_src = 2;
			vals[RX].srcr |= SSI_SRCR_RFEN1;
			vals[TX].stcr |= SSI_STCR_TFEN1;
			vals[RX].scr  |= SSI_SCR_TCH_EN;
			vals[TX].scr  |= SSI_SCR_TCH_EN;
		}
		ssi->dma_params_tx.peripheral_config = &ssi->audio_config[0];
		ssi->dma_params_tx.peripheral_size = sizeof(ssi->audio_config[0]);
		ssi->dma_params_rx.peripheral_config = &ssi->audio_config[1];
		ssi->dma_params_rx.peripheral_size = sizeof(ssi->audio_config[1]);
	}

	return 0;
}

static int fsl_ssi_hw_free(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct fsl_ssi *ssi = snd_soc_dai_get_drvdata(asoc_rtd_to_cpu(rtd, 0));

	if (fsl_ssi_is_i2s_clock_provider(ssi) &&
	    ssi->baudclk_streams & BIT(substream->stream)) {
		clk_disable_unprepare(ssi->baudclk);
		ssi->baudclk_streams &= ~BIT(substream->stream);
	}

	return 0;
}

static int _fsl_ssi_set_dai_fmt(struct fsl_ssi *ssi, unsigned int fmt)
{
	u32 strcr = 0, scr = 0, stcr, srcr, mask;
	unsigned int slots;

	ssi->dai_fmt = fmt;

	/* Synchronize frame sync clock for TE to avoid data slipping */
	scr |= SSI_SCR_SYNC_TX_FS;

	/* Set to default shifting settings: LSB_ALIGNED */
	strcr |= SSI_STCR_TXBIT0;

	/* Use Network mode as default */
	ssi->i2s_net = SSI_SCR_NET;
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
		case SND_SOC_DAIFMT_BP_FP:
			if (IS_ERR(ssi->baudclk)) {
				dev_err(ssi->dev,
					"missing baudclk for master mode\n");
				return -EINVAL;
			}
			fallthrough;
		case SND_SOC_DAIFMT_BC_FP:
			ssi->i2s_net |= SSI_SCR_I2S_MODE_MASTER;
			break;
		case SND_SOC_DAIFMT_BC_FC:
			ssi->i2s_net |= SSI_SCR_I2S_MODE_SLAVE;
			break;
		default:
			return -EINVAL;
		}

		slots = ssi->slots ? : 2;
		regmap_update_bits(ssi->regs, REG_SSI_STCCR,
				   SSI_SxCCR_DC_MASK, SSI_SxCCR_DC(slots));
		regmap_update_bits(ssi->regs, REG_SSI_SRCCR,
				   SSI_SxCCR_DC_MASK, SSI_SxCCR_DC(slots));

		/* Data on rising edge of bclk, frame low, 1clk before data */
		strcr |= SSI_STCR_TFSI | SSI_STCR_TSCKP | SSI_STCR_TEFS;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		/* Data on rising edge of bclk, frame high */
		strcr |= SSI_STCR_TSCKP;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		/* Data on rising edge of bclk, frame high, 1clk before data */
		strcr |= SSI_STCR_TFSL | SSI_STCR_TSCKP | SSI_STCR_TEFS;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		/* Data on rising edge of bclk, frame high */
		strcr |= SSI_STCR_TFSL | SSI_STCR_TSCKP;
		break;
	case SND_SOC_DAIFMT_AC97:
		/* Data on falling edge of bclk, frame high, 1clk before data */
		strcr |= SSI_STCR_TEFS;
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

	/* DAI clock provider masks */
	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_BP_FP:
		/* Output bit and frame sync clocks */
		strcr |= SSI_STCR_TFDIR | SSI_STCR_TXDIR;
		scr |= SSI_SCR_SYS_CLK_EN;
		break;
	case SND_SOC_DAIFMT_BC_FC:
		/* Input bit or frame sync clocks */
		break;
	case SND_SOC_DAIFMT_BC_FP:
		/* Input bit clock but output frame sync clock */
		strcr |= SSI_STCR_TFDIR;
		break;
	default:
		return -EINVAL;
	}

	stcr = strcr;
	srcr = strcr;

	/* Set SYN mode and clear RXDIR bit when using SYN or AC97 mode */
	if (ssi->synchronous || fsl_ssi_is_ac97(ssi)) {
		srcr &= ~SSI_SRCR_RXDIR;
		scr |= SSI_SCR_SYN;
	}

	mask = SSI_STCR_TFDIR | SSI_STCR_TXDIR | SSI_STCR_TSCKP |
	       SSI_STCR_TFSL | SSI_STCR_TFSI | SSI_STCR_TEFS | SSI_STCR_TXBIT0;

	regmap_update_bits(ssi->regs, REG_SSI_STCR, mask, stcr);
	regmap_update_bits(ssi->regs, REG_SSI_SRCR, mask, srcr);

	mask = SSI_SCR_SYNC_TX_FS | SSI_SCR_I2S_MODE_MASK |
	       SSI_SCR_SYS_CLK_EN | SSI_SCR_SYN;
	regmap_update_bits(ssi->regs, REG_SSI_SCR, mask, scr);

	return 0;
}

/**
 * fsl_ssi_set_dai_fmt - Configure Digital Audio Interface (DAI) Format
 * @dai: pointer to DAI
 * @fmt: format mask
 */
static int fsl_ssi_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct fsl_ssi *ssi = snd_soc_dai_get_drvdata(dai);

	/* AC97 configured DAIFMT earlier in the probe() */
	if (fsl_ssi_is_ac97(ssi))
		return 0;

	return _fsl_ssi_set_dai_fmt(ssi, fmt);
}

/**
 * fsl_ssi_set_dai_tdm_slot - Set TDM slot number and slot width
 * @dai: pointer to DAI
 * @tx_mask: mask for TX
 * @rx_mask: mask for RX
 * @slots: number of slots
 * @slot_width: number of bits per slot
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
	if (ssi->i2s_net && slots < 2) {
		dev_err(dai->dev, "slot number should be >= 2 in I2S or NET\n");
		return -EINVAL;
	}

	regmap_update_bits(regs, REG_SSI_STCCR,
			   SSI_SxCCR_DC_MASK, SSI_SxCCR_DC(slots));
	regmap_update_bits(regs, REG_SSI_SRCCR,
			   SSI_SxCCR_DC_MASK, SSI_SxCCR_DC(slots));

	/* Save the SCR register value */
	regmap_read(regs, REG_SSI_SCR, &val);
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
 * fsl_ssi_trigger - Start or stop SSI and corresponding DMA transaction.
 * @substream: ASoC substream
 * @cmd: trigger command
 * @dai: pointer to DAI
 *
 * The DMA channel is in external master start and pause mode, which
 * means the SSI completely controls the flow of data.
 */
static int fsl_ssi_trigger(struct snd_pcm_substream *substream, int cmd,
			   struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct fsl_ssi *ssi = snd_soc_dai_get_drvdata(asoc_rtd_to_cpu(rtd, 0));
	bool tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		/*
		 * SACCST might be modified via AC Link by a CODEC if it sends
		 * extra bits in their SLOTREQ requests, which'll accidentally
		 * send valid data to slots other than normal playback slots.
		 *
		 * To be safe, configure SACCST right before TX starts.
		 */
		if (tx && fsl_ssi_is_ac97(ssi))
			fsl_ssi_tx_ac97_saccst_setup(ssi);
		fsl_ssi_config_enable(ssi, tx);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		fsl_ssi_config_disable(ssi, tx);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int fsl_ssi_dai_probe(struct snd_soc_dai *dai)
{
	struct fsl_ssi *ssi = snd_soc_dai_get_drvdata(dai);

	if (ssi->soc->imx && ssi->use_dma)
		snd_soc_dai_init_dma_data(dai, &ssi->dma_params_tx,
					  &ssi->dma_params_rx);

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
	.legacy_dai_naming = 1,
};

static struct snd_soc_dai_driver fsl_ssi_ac97_dai = {
	.symmetric_channels = 1,
	.probe = fsl_ssi_dai_probe,
	.playback = {
		.stream_name = "CPU AC97 Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FMTBIT_S16 | SNDRV_PCM_FMTBIT_S20,
	},
	.capture = {
		.stream_name = "CPU AC97 Capture",
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
 * fsl_ssi_hw_init - Initialize SSI registers
 * @ssi: SSI context
 */
static int fsl_ssi_hw_init(struct fsl_ssi *ssi)
{
	u32 wm = ssi->fifo_watermark;

	/* Initialize regvals */
	fsl_ssi_setup_regvals(ssi);

	/* Set watermarks */
	regmap_write(ssi->regs, REG_SSI_SFCSR,
		     SSI_SFCSR_TFWM0(wm) | SSI_SFCSR_RFWM0(wm) |
		     SSI_SFCSR_TFWM1(wm) | SSI_SFCSR_RFWM1(wm));

	/* Enable Dual FIFO mode */
	if (ssi->use_dual_fifo)
		regmap_update_bits(ssi->regs, REG_SSI_SCR,
				   SSI_SCR_TCH_EN, SSI_SCR_TCH_EN);

	/* AC97 should start earlier to communicate with CODECs */
	if (fsl_ssi_is_ac97(ssi)) {
		_fsl_ssi_set_dai_fmt(ssi, ssi->dai_fmt);
		fsl_ssi_setup_ac97(ssi);
	}

	return 0;
}

/**
 * fsl_ssi_hw_clean - Clear SSI registers
 * @ssi: SSI context
 */
static void fsl_ssi_hw_clean(struct fsl_ssi *ssi)
{
	/* Disable registers for AC97 */
	if (fsl_ssi_is_ac97(ssi)) {
		/* Disable TE and RE bits first */
		regmap_update_bits(ssi->regs, REG_SSI_SCR,
				   SSI_SCR_TE | SSI_SCR_RE, 0);
		/* Disable AC97 mode */
		regmap_write(ssi->regs, REG_SSI_SACNT, 0);
		/* Unset WAIT bits */
		regmap_write(ssi->regs, REG_SSI_SOR, 0);
		/* Disable SSI -- software reset */
		regmap_update_bits(ssi->regs, REG_SSI_SCR, SSI_SCR_SSIEN, 0);
	}
}

/*
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
	struct device *dev = &pdev->dev;
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

	/* Do not error out for consumer cases that live without a baud clock */
	ssi->baudclk = devm_clk_get(dev, "baud");
	if (IS_ERR(ssi->baudclk))
		dev_dbg(dev, "failed to get baud clock: %ld\n",
			 PTR_ERR(ssi->baudclk));

	ssi->dma_params_tx.maxburst = ssi->dma_maxburst;
	ssi->dma_params_rx.maxburst = ssi->dma_maxburst;
	ssi->dma_params_tx.addr = ssi->ssi_phys + REG_SSI_STX0;
	ssi->dma_params_rx.addr = ssi->ssi_phys + REG_SSI_SRX0;

	/* Use even numbers to avoid channel swap due to SDMA script design */
	if (ssi->use_dual_fifo || ssi->use_dyna_fifo) {
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
		ret = imx_pcm_dma_init(pdev);
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

static int fsl_ssi_probe_from_dt(struct fsl_ssi *ssi)
{
	struct device *dev = ssi->dev;
	struct device_node *np = dev->of_node;
	const char *p, *sprop;
	const __be32 *iprop;
	u32 dmas[4];
	int ret;

	ret = of_property_match_string(np, "clock-names", "ipg");
	/* Get error code if not found */
	ssi->has_ipg_clk_name = ret >= 0;

	/* Check if being used in AC97 mode */
	sprop = of_get_property(np, "fsl,mode", NULL);
	if (sprop && !strcmp(sprop, "ac97-slave")) {
		ssi->dai_fmt = FSLSSI_AC97_DAIFMT;

		ret = of_property_read_u32(np, "cell-index", &ssi->card_idx);
		if (ret) {
			dev_err(dev, "failed to get SSI index property\n");
			return -EINVAL;
		}
		strcpy(ssi->card_name, "ac97-codec");
	} else if (!of_property_read_bool(np, "fsl,ssi-asynchronous")) {
		/*
		 * In synchronous mode, STCK and STFS ports are used by RX
		 * as well. So the software should limit the sample rates,
		 * sample bits and channels to be symmetric.
		 *
		 * This is exclusive with FSLSSI_AC97_FORMATS as AC97 runs
		 * in the SSI synchronous mode however it does not have to
		 * limit symmetric sample rates and sample bits.
		 */
		ssi->synchronous = true;
	}

	/* Select DMA or FIQ */
	ssi->use_dma = !of_property_read_bool(np, "fsl,fiq-stream-filter");

	/* Fetch FIFO depth; Set to 8 for older DT without this property */
	iprop = of_get_property(np, "fsl,fifo-depth", NULL);
	if (iprop)
		ssi->fifo_depth = be32_to_cpup(iprop);
	else
		ssi->fifo_depth = 8;

	/* Use dual FIFO mode depending on the support from SDMA script */
	ret = of_property_read_u32_array(np, "dmas", dmas, 4);
	if (ssi->use_dma && !ret && dmas[2] == IMX_DMATYPE_SSI_DUAL)
		ssi->use_dual_fifo = true;

	if (ssi->use_dma && !ret && dmas[2] == IMX_DMATYPE_MULTI_SAI)
		ssi->use_dyna_fifo = true;
	/*
	 * Backward compatible for older bindings by manually triggering the
	 * machine driver's probe(). Use /compatible property, including the
	 * address of CPU DAI driver structure, as the name of machine driver
	 *
	 * If card_name is set by AC97 earlier, bypass here since it uses a
	 * different name to register the device.
	 */
	if (!ssi->card_name[0] && of_get_property(np, "codec-handle", NULL)) {
		struct device_node *root = of_find_node_by_path("/");

		sprop = of_get_property(root, "compatible", NULL);
		of_node_put(root);
		/* Strip "fsl," in the compatible name if applicable */
		p = strrchr(sprop, ',');
		if (p)
			sprop = p + 1;
		snprintf(ssi->card_name, sizeof(ssi->card_name),
			 "snd-soc-%s", sprop);
		make_lowercase(ssi->card_name);
		ssi->card_idx = 0;
	}

	return 0;
}

static int fsl_ssi_probe(struct platform_device *pdev)
{
	struct regmap_config regconfig = fsl_ssi_regconfig;
	struct device *dev = &pdev->dev;
	struct fsl_ssi *ssi;
	struct resource *res;
	void __iomem *iomem;
	int ret = 0;

	ssi = devm_kzalloc(dev, sizeof(*ssi), GFP_KERNEL);
	if (!ssi)
		return -ENOMEM;

	ssi->dev = dev;
	ssi->soc = of_device_get_match_data(&pdev->dev);

	/* Probe from DT */
	ret = fsl_ssi_probe_from_dt(ssi);
	if (ret)
		return ret;

	if (fsl_ssi_is_ac97(ssi)) {
		memcpy(&ssi->cpu_dai_drv, &fsl_ssi_ac97_dai,
		       sizeof(fsl_ssi_ac97_dai));
		fsl_ac97_data = ssi;
	} else {
		memcpy(&ssi->cpu_dai_drv, &fsl_ssi_dai_template,
		       sizeof(fsl_ssi_dai_template));
	}
	ssi->cpu_dai_drv.name = dev_name(dev);

	iomem = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(iomem))
		return PTR_ERR(iomem);
	ssi->ssi_phys = res->start;

	if (ssi->soc->imx21regs) {
		/* No SACC{ST,EN,DIS} regs in imx21-class SSI */
		regconfig.max_register = REG_SSI_SRMSK;
		regconfig.num_reg_defaults_raw =
			REG_SSI_SRMSK / sizeof(uint32_t) + 1;
	}

	if (ssi->has_ipg_clk_name)
		ssi->regs = devm_regmap_init_mmio_clk(dev, "ipg", iomem,
						      &regconfig);
	else
		ssi->regs = devm_regmap_init_mmio(dev, iomem, &regconfig);
	if (IS_ERR(ssi->regs)) {
		dev_err(dev, "failed to init register map\n");
		return PTR_ERR(ssi->regs);
	}

	ssi->irq = platform_get_irq(pdev, 0);
	if (ssi->irq < 0)
		return ssi->irq;

	/* Set software limitations for synchronous mode except AC97 */
	if (ssi->synchronous && !fsl_ssi_is_ac97(ssi)) {
		ssi->cpu_dai_drv.symmetric_rate = 1;
		ssi->cpu_dai_drv.symmetric_channels = 1;
		ssi->cpu_dai_drv.symmetric_sample_bits = 1;
	}

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

	fsl_ssi_debugfs_create(&ssi->dbg_stats, dev);

	/* Initially configures SSI registers */
	fsl_ssi_hw_init(ssi);

	/* Register a platform device for older bindings or AC97 */
	if (ssi->card_name[0]) {
		struct device *parent = dev;
		/*
		 * Do not set SSI dev as the parent of AC97 CODEC device since
		 * it does not have a DT node. Otherwise ASoC core will assume
		 * CODEC has the same DT node as the SSI, so it may bypass the
		 * dai_probe() of SSI and then cause NULL DMA data pointers.
		 */
		if (fsl_ssi_is_ac97(ssi))
			parent = NULL;

		ssi->card_pdev = platform_device_register_data(parent,
				ssi->card_name, ssi->card_idx, NULL, 0);
		if (IS_ERR(ssi->card_pdev)) {
			ret = PTR_ERR(ssi->card_pdev);
			dev_err(dev, "failed to register %s: %d\n",
				ssi->card_name, ret);
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

	if (ssi->card_pdev)
		platform_device_unregister(ssi->card_pdev);

	/* Clean up SSI registers */
	fsl_ssi_hw_clean(ssi);

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
