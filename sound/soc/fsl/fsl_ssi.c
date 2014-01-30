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
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
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

#ifdef PPC
#define read_ssi(addr)			 in_be32(addr)
#define write_ssi(val, addr)		 out_be32(addr, val)
#define write_ssi_mask(addr, clear, set) clrsetbits_be32(addr, clear, set)
#else
#define read_ssi(addr)			 readl(addr)
#define write_ssi(val, addr)		 writel(val, addr)
/*
 * FIXME: Proper locking should be added at write_ssi_mask caller level
 * to ensure this register read/modify/write sequence is race free.
 */
static inline void write_ssi_mask(u32 __iomem *addr, u32 clear, u32 set)
{
	u32 val = readl(addr);
	val = (val & ~clear) | set;
	writel(val, addr);
}
#endif

/**
 * FSLSSI_I2S_RATES: sample rates supported by the I2S
 *
 * This driver currently only supports the SSI running in I2S slave mode,
 * which means the codec determines the sample rate.  Therefore, we tell
 * ALSA that we support all rates and let the codec driver decide what rates
 * are really supported.
 */
#define FSLSSI_I2S_RATES SNDRV_PCM_RATE_CONTINUOUS

/**
 * FSLSSI_I2S_FORMATS: audio formats supported by the SSI
 *
 * This driver currently only supports the SSI running in I2S slave mode.
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
#define FSLSSI_I2S_FORMATS (SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_BE | \
	 SNDRV_PCM_FMTBIT_S18_3BE | SNDRV_PCM_FMTBIT_S20_3BE | \
	 SNDRV_PCM_FMTBIT_S24_3BE | SNDRV_PCM_FMTBIT_S24_BE)
#else
#define FSLSSI_I2S_FORMATS (SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_LE | \
	 SNDRV_PCM_FMTBIT_S18_3LE | SNDRV_PCM_FMTBIT_S20_3LE | \
	 SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S24_LE)
#endif

#define FSLSSI_SIER_DBG_RX_FLAGS (CCSR_SSI_SIER_RFF0_EN | \
		CCSR_SSI_SIER_RLS_EN | CCSR_SSI_SIER_RFS_EN | \
		CCSR_SSI_SIER_ROE0_EN | CCSR_SSI_SIER_RFRC_EN)
#define FSLSSI_SIER_DBG_TX_FLAGS (CCSR_SSI_SIER_TFE0_EN | \
		CCSR_SSI_SIER_TLS_EN | CCSR_SSI_SIER_TFS_EN | \
		CCSR_SSI_SIER_TUE0_EN | CCSR_SSI_SIER_TFRC_EN)
#define FSLSSI_SISR_MASK (FSLSSI_SIER_DBG_RX_FLAGS | FSLSSI_SIER_DBG_TX_FLAGS)


enum fsl_ssi_type {
	FSL_SSI_MCP8610,
	FSL_SSI_MX21,
	FSL_SSI_MX35,
	FSL_SSI_MX51,
};

struct fsl_ssi_reg_val {
	u32 sier;
	u32 srcr;
	u32 stcr;
	u32 scr;
};

struct fsl_ssi_rxtx_reg_val {
	struct fsl_ssi_reg_val rx;
	struct fsl_ssi_reg_val tx;
};

/**
 * fsl_ssi_private: per-SSI private data
 *
 * @ssi: pointer to the SSI's registers
 * @ssi_phys: physical address of the SSI registers
 * @irq: IRQ of this SSI
 * @playback: the number of playback streams opened
 * @capture: the number of capture streams opened
 * @cpu_dai: the CPU DAI for this device
 * @dev_attr: the sysfs device attribute structure
 * @stats: SSI statistics
 * @name: name for this device
 */
struct fsl_ssi_private {
	struct ccsr_ssi __iomem *ssi;
	dma_addr_t ssi_phys;
	unsigned int irq;
	unsigned int fifo_depth;
	struct snd_soc_dai_driver cpu_dai_drv;
	struct platform_device *pdev;

	enum fsl_ssi_type hw_type;
	bool new_binding;
	bool ssi_on_imx;
	bool imx_ac97;
	bool use_dma;
	bool baudclk_locked;
	bool irq_stats;
	bool offline_config;
	bool use_dual_fifo;
	u8 i2s_mode;
	spinlock_t baudclk_lock;
	struct clk *baudclk;
	struct clk *clk;
	struct snd_dmaengine_dai_dma_data dma_params_tx;
	struct snd_dmaengine_dai_dma_data dma_params_rx;
	struct imx_dma_data filter_data_tx;
	struct imx_dma_data filter_data_rx;
	struct imx_pcm_fiq_params fiq_params;
	/* Register values for rx/tx configuration */
	struct fsl_ssi_rxtx_reg_val rxtx_reg_val;

	struct {
		unsigned int rfrc;
		unsigned int tfrc;
		unsigned int cmdau;
		unsigned int cmddu;
		unsigned int rxt;
		unsigned int rdr1;
		unsigned int rdr0;
		unsigned int tde1;
		unsigned int tde0;
		unsigned int roe1;
		unsigned int roe0;
		unsigned int tue1;
		unsigned int tue0;
		unsigned int tfs;
		unsigned int rfs;
		unsigned int tls;
		unsigned int rls;
		unsigned int rff1;
		unsigned int rff0;
		unsigned int tfe1;
		unsigned int tfe0;
	} stats;
	struct dentry *dbg_dir;
	struct dentry *dbg_stats;

	char name[1];
};

static const struct of_device_id fsl_ssi_ids[] = {
	{ .compatible = "fsl,mpc8610-ssi", .data = (void *) FSL_SSI_MCP8610},
	{ .compatible = "fsl,imx51-ssi", .data = (void *) FSL_SSI_MX51},
	{ .compatible = "fsl,imx35-ssi", .data = (void *) FSL_SSI_MX35},
	{ .compatible = "fsl,imx21-ssi", .data = (void *) FSL_SSI_MX21},
	{}
};
MODULE_DEVICE_TABLE(of, fsl_ssi_ids);

/**
 * fsl_ssi_isr: SSI interrupt handler
 *
 * Although it's possible to use the interrupt handler to send and receive
 * data to/from the SSI, we use the DMA instead.  Programming is more
 * complicated, but the performance is much better.
 *
 * This interrupt handler is used only to gather statistics.
 *
 * @irq: IRQ of the SSI device
 * @dev_id: pointer to the ssi_private structure for this SSI device
 */
static irqreturn_t fsl_ssi_isr(int irq, void *dev_id)
{
	struct fsl_ssi_private *ssi_private = dev_id;
	struct ccsr_ssi __iomem *ssi = ssi_private->ssi;
	irqreturn_t ret = IRQ_NONE;
	__be32 sisr;
	__be32 sisr2;
	__be32 sisr_write_mask = 0;

	switch (ssi_private->hw_type) {
	case FSL_SSI_MX21:
		sisr_write_mask = 0;
		break;

	case FSL_SSI_MCP8610:
	case FSL_SSI_MX35:
		sisr_write_mask = CCSR_SSI_SISR_RFRC | CCSR_SSI_SISR_TFRC |
			CCSR_SSI_SISR_ROE0 | CCSR_SSI_SISR_ROE1 |
			CCSR_SSI_SISR_TUE0 | CCSR_SSI_SISR_TUE1;
		break;

	case FSL_SSI_MX51:
		sisr_write_mask = CCSR_SSI_SISR_ROE0 | CCSR_SSI_SISR_ROE1 |
			CCSR_SSI_SISR_TUE0 | CCSR_SSI_SISR_TUE1;
		break;
	}

	/* We got an interrupt, so read the status register to see what we
	   were interrupted for.  We mask it with the Interrupt Enable register
	   so that we only check for events that we're interested in.
	 */
	sisr = read_ssi(&ssi->sisr) & FSLSSI_SISR_MASK;

	if (sisr & CCSR_SSI_SISR_RFRC) {
		ssi_private->stats.rfrc++;
		ret = IRQ_HANDLED;
	}

	if (sisr & CCSR_SSI_SISR_TFRC) {
		ssi_private->stats.tfrc++;
		ret = IRQ_HANDLED;
	}

	if (sisr & CCSR_SSI_SISR_CMDAU) {
		ssi_private->stats.cmdau++;
		ret = IRQ_HANDLED;
	}

	if (sisr & CCSR_SSI_SISR_CMDDU) {
		ssi_private->stats.cmddu++;
		ret = IRQ_HANDLED;
	}

	if (sisr & CCSR_SSI_SISR_RXT) {
		ssi_private->stats.rxt++;
		ret = IRQ_HANDLED;
	}

	if (sisr & CCSR_SSI_SISR_RDR1) {
		ssi_private->stats.rdr1++;
		ret = IRQ_HANDLED;
	}

	if (sisr & CCSR_SSI_SISR_RDR0) {
		ssi_private->stats.rdr0++;
		ret = IRQ_HANDLED;
	}

	if (sisr & CCSR_SSI_SISR_TDE1) {
		ssi_private->stats.tde1++;
		ret = IRQ_HANDLED;
	}

	if (sisr & CCSR_SSI_SISR_TDE0) {
		ssi_private->stats.tde0++;
		ret = IRQ_HANDLED;
	}

	if (sisr & CCSR_SSI_SISR_ROE1) {
		ssi_private->stats.roe1++;
		ret = IRQ_HANDLED;
	}

	if (sisr & CCSR_SSI_SISR_ROE0) {
		ssi_private->stats.roe0++;
		ret = IRQ_HANDLED;
	}

	if (sisr & CCSR_SSI_SISR_TUE1) {
		ssi_private->stats.tue1++;
		ret = IRQ_HANDLED;
	}

	if (sisr & CCSR_SSI_SISR_TUE0) {
		ssi_private->stats.tue0++;
		ret = IRQ_HANDLED;
	}

	if (sisr & CCSR_SSI_SISR_TFS) {
		ssi_private->stats.tfs++;
		ret = IRQ_HANDLED;
	}

	if (sisr & CCSR_SSI_SISR_RFS) {
		ssi_private->stats.rfs++;
		ret = IRQ_HANDLED;
	}

	if (sisr & CCSR_SSI_SISR_TLS) {
		ssi_private->stats.tls++;
		ret = IRQ_HANDLED;
	}

	if (sisr & CCSR_SSI_SISR_RLS) {
		ssi_private->stats.rls++;
		ret = IRQ_HANDLED;
	}

	if (sisr & CCSR_SSI_SISR_RFF1) {
		ssi_private->stats.rff1++;
		ret = IRQ_HANDLED;
	}

	if (sisr & CCSR_SSI_SISR_RFF0) {
		ssi_private->stats.rff0++;
		ret = IRQ_HANDLED;
	}

	if (sisr & CCSR_SSI_SISR_TFE1) {
		ssi_private->stats.tfe1++;
		ret = IRQ_HANDLED;
	}

	if (sisr & CCSR_SSI_SISR_TFE0) {
		ssi_private->stats.tfe0++;
		ret = IRQ_HANDLED;
	}

	sisr2 = sisr & sisr_write_mask;
	/* Clear the bits that we set */
	if (sisr2)
		write_ssi(sisr2, &ssi->sisr);

	return ret;
}

#if IS_ENABLED(CONFIG_DEBUG_FS)
/* Show the statistics of a flag only if its interrupt is enabled.  The
 * compiler will optimze this code to a no-op if the interrupt is not
 * enabled.
 */
#define SIER_SHOW(flag, name) \
	do { \
		if (FSLSSI_SISR_MASK & CCSR_SSI_SIER_##flag) \
			seq_printf(s, #name "=%u\n", ssi_private->stats.name); \
	} while (0)


/**
 * fsl_sysfs_ssi_show: display SSI statistics
 *
 * Display the statistics for the current SSI device.  To avoid confusion,
 * we only show those counts that are enabled.
 */
static int fsl_ssi_stats_show(struct seq_file *s, void *unused)
{
	struct fsl_ssi_private *ssi_private = s->private;

	SIER_SHOW(RFRC_EN, rfrc);
	SIER_SHOW(TFRC_EN, tfrc);
	SIER_SHOW(CMDAU_EN, cmdau);
	SIER_SHOW(CMDDU_EN, cmddu);
	SIER_SHOW(RXT_EN, rxt);
	SIER_SHOW(RDR1_EN, rdr1);
	SIER_SHOW(RDR0_EN, rdr0);
	SIER_SHOW(TDE1_EN, tde1);
	SIER_SHOW(TDE0_EN, tde0);
	SIER_SHOW(ROE1_EN, roe1);
	SIER_SHOW(ROE0_EN, roe0);
	SIER_SHOW(TUE1_EN, tue1);
	SIER_SHOW(TUE0_EN, tue0);
	SIER_SHOW(TFS_EN, tfs);
	SIER_SHOW(RFS_EN, rfs);
	SIER_SHOW(TLS_EN, tls);
	SIER_SHOW(RLS_EN, rls);
	SIER_SHOW(RFF1_EN, rff1);
	SIER_SHOW(RFF0_EN, rff0);
	SIER_SHOW(TFE1_EN, tfe1);
	SIER_SHOW(TFE0_EN, tfe0);

	return 0;
}

static int fsl_ssi_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, fsl_ssi_stats_show, inode->i_private);
}

static const struct file_operations fsl_ssi_stats_ops = {
	.open = fsl_ssi_stats_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int fsl_ssi_debugfs_create(struct fsl_ssi_private *ssi_private,
		struct device *dev)
{
	ssi_private->dbg_dir = debugfs_create_dir(dev_name(dev), NULL);
	if (!ssi_private->dbg_dir)
		return -ENOMEM;

	ssi_private->dbg_stats = debugfs_create_file("stats", S_IRUGO,
			ssi_private->dbg_dir, ssi_private, &fsl_ssi_stats_ops);
	if (!ssi_private->dbg_stats) {
		debugfs_remove(ssi_private->dbg_dir);
		return -ENOMEM;
	}

	return 0;
}

static void fsl_ssi_debugfs_remove(struct fsl_ssi_private *ssi_private)
{
	debugfs_remove(ssi_private->dbg_stats);
	debugfs_remove(ssi_private->dbg_dir);
}

#else

static int fsl_ssi_debugfs_create(struct fsl_ssi_private *ssi_private,
		struct device *dev)
{
	return 0;
}

static void fsl_ssi_debugfs_remove(struct fsl_ssi_private *ssi_private)
{
}

#endif /* IS_ENABLED(CONFIG_DEBUG_FS) */

/*
 * Enable/Disable all rx/tx config flags at once.
 */
static void fsl_ssi_rxtx_config(struct fsl_ssi_private *ssi_private,
		bool enable)
{
	struct ccsr_ssi __iomem *ssi = ssi_private->ssi;
	struct fsl_ssi_rxtx_reg_val *vals = &ssi_private->rxtx_reg_val;

	if (enable) {
		write_ssi_mask(&ssi->sier, 0, vals->rx.sier | vals->tx.sier);
		write_ssi_mask(&ssi->srcr, 0, vals->rx.srcr | vals->tx.srcr);
		write_ssi_mask(&ssi->stcr, 0, vals->rx.stcr | vals->tx.stcr);
	} else {
		write_ssi_mask(&ssi->srcr, vals->rx.srcr | vals->tx.srcr, 0);
		write_ssi_mask(&ssi->stcr, vals->rx.stcr | vals->tx.stcr, 0);
		write_ssi_mask(&ssi->sier, vals->rx.sier | vals->tx.sier, 0);
	}
}

/*
 * Enable/Disable a ssi configuration. You have to pass either
 * ssi_private->rxtx_reg_val.rx or tx as vals parameter.
 */
static void fsl_ssi_config(struct fsl_ssi_private *ssi_private, bool enable,
		struct fsl_ssi_reg_val *vals)
{
	struct ccsr_ssi __iomem *ssi = ssi_private->ssi;
	struct fsl_ssi_reg_val *avals;
	u32 scr_val = read_ssi(&ssi->scr);
	int nr_active_streams = !!(scr_val & CCSR_SSI_SCR_TE) +
				!!(scr_val & CCSR_SSI_SCR_RE);

	/* Find the other direction values rx or tx which we do not want to
	 * modify */
	if (&ssi_private->rxtx_reg_val.rx == vals)
		avals = &ssi_private->rxtx_reg_val.tx;
	else
		avals = &ssi_private->rxtx_reg_val.rx;

	/* If vals should be disabled, start with disabling the unit */
	if (!enable) {
		u32 scr = vals->scr & (vals->scr ^ avals->scr);
		write_ssi_mask(&ssi->scr, scr, 0);
	}

	/*
	 * We are running on a SoC which does not support online SSI
	 * reconfiguration, so we have to enable all necessary flags at once
	 * even if we do not use them later (capture and playback configuration)
	 */
	if (ssi_private->offline_config) {
		if ((enable && !nr_active_streams) ||
				(!enable && nr_active_streams == 1))
			fsl_ssi_rxtx_config(ssi_private, enable);

		goto config_done;
	}

	/*
	 * Configure single direction units while the SSI unit is running
	 * (online configuration)
	 */
	if (enable) {
		write_ssi_mask(&ssi->sier, 0, vals->sier);
		write_ssi_mask(&ssi->srcr, 0, vals->srcr);
		write_ssi_mask(&ssi->stcr, 0, vals->stcr);
	} else {
		u32 sier;
		u32 srcr;
		u32 stcr;

		/*
		 * Disabling the necessary flags for one of rx/tx while the
		 * other stream is active is a little bit more difficult. We
		 * have to disable only those flags that differ between both
		 * streams (rx XOR tx) and that are set in the stream that is
		 * disabled now. Otherwise we could alter flags of the other
		 * stream
		 */

		/* These assignments are simply vals without bits set in avals*/
		sier = vals->sier & (vals->sier ^ avals->sier);
		srcr = vals->srcr & (vals->srcr ^ avals->srcr);
		stcr = vals->stcr & (vals->stcr ^ avals->stcr);

		write_ssi_mask(&ssi->srcr, srcr, 0);
		write_ssi_mask(&ssi->stcr, stcr, 0);
		write_ssi_mask(&ssi->sier, sier, 0);
	}

config_done:
	/* Enabling of subunits is done after configuration */
	if (enable)
		write_ssi_mask(&ssi->scr, 0, vals->scr);
}


static void fsl_ssi_rx_config(struct fsl_ssi_private *ssi_private, bool enable)
{
	fsl_ssi_config(ssi_private, enable, &ssi_private->rxtx_reg_val.rx);
}

static void fsl_ssi_tx_config(struct fsl_ssi_private *ssi_private, bool enable)
{
	fsl_ssi_config(ssi_private, enable, &ssi_private->rxtx_reg_val.tx);
}

/*
 * Setup rx/tx register values used to enable/disable the streams. These will
 * be used later in fsl_ssi_config to setup the streams without the need to
 * check for all different SSI modes.
 */
static void fsl_ssi_setup_reg_vals(struct fsl_ssi_private *ssi_private)
{
	struct fsl_ssi_rxtx_reg_val *reg = &ssi_private->rxtx_reg_val;

	reg->rx.sier = CCSR_SSI_SIER_RFF0_EN;
	reg->rx.srcr = CCSR_SSI_SRCR_RFEN0;
	reg->rx.scr = 0;
	reg->tx.sier = CCSR_SSI_SIER_TFE0_EN;
	reg->tx.stcr = CCSR_SSI_STCR_TFEN0;
	reg->tx.scr = 0;

	if (!ssi_private->imx_ac97) {
		reg->rx.scr = CCSR_SSI_SCR_SSIEN | CCSR_SSI_SCR_RE;
		reg->rx.sier |= CCSR_SSI_SIER_RFF0_EN;
		reg->tx.scr = CCSR_SSI_SCR_SSIEN | CCSR_SSI_SCR_TE;
		reg->tx.sier |= CCSR_SSI_SIER_TFE0_EN;
	}

	if (ssi_private->use_dma) {
		reg->rx.sier |= CCSR_SSI_SIER_RDMAE;
		reg->tx.sier |= CCSR_SSI_SIER_TDMAE;
	} else {
		reg->rx.sier |= CCSR_SSI_SIER_RIE;
		reg->tx.sier |= CCSR_SSI_SIER_TIE;
	}

	reg->rx.sier |= FSLSSI_SIER_DBG_RX_FLAGS;
	reg->tx.sier |= FSLSSI_SIER_DBG_TX_FLAGS;
}

static void fsl_ssi_setup_ac97(struct fsl_ssi_private *ssi_private)
{
	struct ccsr_ssi __iomem *ssi = ssi_private->ssi;

	/*
	 * Setup the clock control register
	 */
	write_ssi(CCSR_SSI_SxCCR_WL(17) | CCSR_SSI_SxCCR_DC(13),
			&ssi->stccr);
	write_ssi(CCSR_SSI_SxCCR_WL(17) | CCSR_SSI_SxCCR_DC(13),
			&ssi->srccr);

	/*
	 * Enable AC97 mode and startup the SSI
	 */
	write_ssi(CCSR_SSI_SACNT_AC97EN | CCSR_SSI_SACNT_FV,
			&ssi->sacnt);
	write_ssi(0xff, &ssi->saccdis);
	write_ssi(0x300, &ssi->saccen);

	/*
	 * Enable SSI, Transmit and Receive. AC97 has to communicate with the
	 * codec before a stream is started.
	 */
	write_ssi_mask(&ssi->scr, 0, CCSR_SSI_SCR_SSIEN |
			CCSR_SSI_SCR_TE | CCSR_SSI_SCR_RE);

	write_ssi(CCSR_SSI_SOR_WAIT(3), &ssi->sor);
}

static int fsl_ssi_setup(struct fsl_ssi_private *ssi_private)
{
	struct ccsr_ssi __iomem *ssi = ssi_private->ssi;
	u8 wm;
	int synchronous = ssi_private->cpu_dai_drv.symmetric_rates;

	fsl_ssi_setup_reg_vals(ssi_private);

	if (ssi_private->imx_ac97)
		ssi_private->i2s_mode = CCSR_SSI_SCR_I2S_MODE_NORMAL | CCSR_SSI_SCR_NET;
	else
		ssi_private->i2s_mode = CCSR_SSI_SCR_I2S_MODE_SLAVE;

	/*
	 * Section 16.5 of the MPC8610 reference manual says that the SSI needs
	 * to be disabled before updating the registers we set here.
	 */
	write_ssi_mask(&ssi->scr, CCSR_SSI_SCR_SSIEN, 0);

	/*
	 * Program the SSI into I2S Slave Non-Network Synchronous mode. Also
	 * enable the transmit and receive FIFO.
	 *
	 * FIXME: Little-endian samples require a different shift dir
	 */
	write_ssi_mask(&ssi->scr,
		CCSR_SSI_SCR_I2S_MODE_MASK | CCSR_SSI_SCR_SYN,
		CCSR_SSI_SCR_TFR_CLK_DIS |
		ssi_private->i2s_mode |
		(synchronous ? CCSR_SSI_SCR_SYN : 0));

	write_ssi(CCSR_SSI_STCR_TXBIT0 | CCSR_SSI_STCR_TFSI |
			CCSR_SSI_STCR_TEFS | CCSR_SSI_STCR_TSCKP, &ssi->stcr);

	write_ssi(CCSR_SSI_SRCR_RXBIT0 | CCSR_SSI_SRCR_RFSI |
			CCSR_SSI_SRCR_REFS | CCSR_SSI_SRCR_RSCKP, &ssi->srcr);

	/*
	 * The DC and PM bits are only used if the SSI is the clock master.
	 */

	/*
	 * Set the watermark for transmit FIFI 0 and receive FIFO 0. We don't
	 * use FIFO 1. We program the transmit water to signal a DMA transfer
	 * if there are only two (or fewer) elements left in the FIFO. Two
	 * elements equals one frame (left channel, right channel). This value,
	 * however, depends on the depth of the transmit buffer.
	 *
	 * We set the watermark on the same level as the DMA burstsize.  For
	 * fiq it is probably better to use the biggest possible watermark
	 * size.
	 */
	if (ssi_private->use_dma)
		wm = ssi_private->fifo_depth - 2;
	else
		wm = ssi_private->fifo_depth;

	write_ssi(CCSR_SSI_SFCSR_TFWM0(wm) | CCSR_SSI_SFCSR_RFWM0(wm) |
		CCSR_SSI_SFCSR_TFWM1(wm) | CCSR_SSI_SFCSR_RFWM1(wm),
		&ssi->sfcsr);

	/*
	 * For ac97 interrupts are enabled with the startup of the substream
	 * because it is also running without an active substream. Normally SSI
	 * is only enabled when there is a substream.
	 */
	if (ssi_private->imx_ac97)
		fsl_ssi_setup_ac97(ssi_private);

	/*
	 * Set a default slot number so that there is no need for those common
	 * cases like I2S mode to call the extra set_tdm_slot() any more.
	 */
	if (!ssi_private->imx_ac97) {
		write_ssi_mask(&ssi->stccr, CCSR_SSI_SxCCR_DC_MASK,
				CCSR_SSI_SxCCR_DC(2));
		write_ssi_mask(&ssi->srccr, CCSR_SSI_SxCCR_DC_MASK,
				CCSR_SSI_SxCCR_DC(2));
	}

	if (ssi_private->use_dual_fifo) {
		write_ssi_mask(&ssi->srcr, 0, CCSR_SSI_SRCR_RFEN1);
		write_ssi_mask(&ssi->stcr, 0, CCSR_SSI_STCR_TFEN1);
		write_ssi_mask(&ssi->scr, 0, CCSR_SSI_SCR_TCH_EN);
	}

	return 0;
}


/**
 * fsl_ssi_startup: create a new substream
 *
 * This is the first function called when a stream is opened.
 *
 * If this is the first stream open, then grab the IRQ and program most of
 * the SSI registers.
 */
static int fsl_ssi_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct fsl_ssi_private *ssi_private =
		snd_soc_dai_get_drvdata(rtd->cpu_dai);
	unsigned long flags;

	/* First, we only do fsl_ssi_setup() when SSI is going to be active.
	 * Second, fsl_ssi_setup was already called by ac97_init earlier if
	 * the driver is in ac97 mode.
	 */
	if (!dai->active && !ssi_private->imx_ac97) {
		fsl_ssi_setup(ssi_private);
		spin_lock_irqsave(&ssi_private->baudclk_lock, flags);
		ssi_private->baudclk_locked = false;
		spin_unlock_irqrestore(&ssi_private->baudclk_lock, flags);
	}

	/* When using dual fifo mode, it is safer to ensure an even period
	 * size. If appearing to an odd number while DMA always starts its
	 * task from fifo0, fifo1 would be neglected at the end of each
	 * period. But SSI would still access fifo1 with an invalid data.
	 */
	if (ssi_private->use_dual_fifo)
		snd_pcm_hw_constraint_step(substream->runtime, 0,
				SNDRV_PCM_HW_PARAM_PERIOD_SIZE, 2);

	return 0;
}

/**
 * fsl_ssi_hw_params - program the sample size
 *
 * Most of the SSI registers have been programmed in the startup function,
 * but the word length must be programmed here.  Unfortunately, programming
 * the SxCCR.WL bits requires the SSI to be temporarily disabled.  This can
 * cause a problem with supporting simultaneous playback and capture.  If
 * the SSI is already playing a stream, then that stream may be temporarily
 * stopped when you start capture.
 *
 * Note: The SxCCR.DC and SxCCR.PM bits are only used if the SSI is the
 * clock master.
 */
static int fsl_ssi_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *hw_params, struct snd_soc_dai *cpu_dai)
{
	struct fsl_ssi_private *ssi_private = snd_soc_dai_get_drvdata(cpu_dai);
	struct ccsr_ssi __iomem *ssi = ssi_private->ssi;
	unsigned int channels = params_channels(hw_params);
	unsigned int sample_size =
		snd_pcm_format_width(params_format(hw_params));
	u32 wl = CCSR_SSI_SxCCR_WL(sample_size);
	int enabled = read_ssi(&ssi->scr) & CCSR_SSI_SCR_SSIEN;

	/*
	 * If we're in synchronous mode, and the SSI is already enabled,
	 * then STCCR is already set properly.
	 */
	if (enabled && ssi_private->cpu_dai_drv.symmetric_rates)
		return 0;

	/*
	 * FIXME: The documentation says that SxCCR[WL] should not be
	 * modified while the SSI is enabled.  The only time this can
	 * happen is if we're trying to do simultaneous playback and
	 * capture in asynchronous mode.  Unfortunately, I have been enable
	 * to get that to work at all on the P1022DS.  Therefore, we don't
	 * bother to disable/enable the SSI when setting SxCCR[WL], because
	 * the SSI will stop anyway.  Maybe one day, this will get fixed.
	 */

	/* In synchronous mode, the SSI uses STCCR for capture */
	if ((substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ||
	    ssi_private->cpu_dai_drv.symmetric_rates)
		write_ssi_mask(&ssi->stccr, CCSR_SSI_SxCCR_WL_MASK, wl);
	else
		write_ssi_mask(&ssi->srccr, CCSR_SSI_SxCCR_WL_MASK, wl);

	if (!ssi_private->imx_ac97)
		write_ssi_mask(&ssi->scr,
				CCSR_SSI_SCR_NET | CCSR_SSI_SCR_I2S_MODE_MASK,
				channels == 1 ? 0 : ssi_private->i2s_mode);

	return 0;
}

/**
 * fsl_ssi_set_dai_fmt - configure Digital Audio Interface Format.
 */
static int fsl_ssi_set_dai_fmt(struct snd_soc_dai *cpu_dai, unsigned int fmt)
{
	struct fsl_ssi_private *ssi_private = snd_soc_dai_get_drvdata(cpu_dai);
	struct ccsr_ssi __iomem *ssi = ssi_private->ssi;
	u32 strcr = 0, stcr, srcr, scr, mask;

	scr = read_ssi(&ssi->scr) & ~(CCSR_SSI_SCR_SYN | CCSR_SSI_SCR_I2S_MODE_MASK);
	scr |= CCSR_SSI_SCR_NET;

	mask = CCSR_SSI_STCR_TXBIT0 | CCSR_SSI_STCR_TFDIR | CCSR_SSI_STCR_TXDIR |
		CCSR_SSI_STCR_TSCKP | CCSR_SSI_STCR_TFSI | CCSR_SSI_STCR_TFSL |
		CCSR_SSI_STCR_TEFS;
	stcr = read_ssi(&ssi->stcr) & ~mask;
	srcr = read_ssi(&ssi->srcr) & ~mask;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
		case SND_SOC_DAIFMT_CBS_CFS:
			ssi_private->i2s_mode = CCSR_SSI_SCR_I2S_MODE_MASTER;
			break;
		case SND_SOC_DAIFMT_CBM_CFM:
			ssi_private->i2s_mode = CCSR_SSI_SCR_I2S_MODE_SLAVE;
			break;
		default:
			return -EINVAL;
		}
		scr |= ssi_private->i2s_mode;

		/* Data on rising edge of bclk, frame low, 1clk before data */
		strcr |= CCSR_SSI_STCR_TFSI | CCSR_SSI_STCR_TSCKP |
			CCSR_SSI_STCR_TXBIT0 | CCSR_SSI_STCR_TEFS;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		/* Data on rising edge of bclk, frame high */
		strcr |= CCSR_SSI_STCR_TXBIT0 | CCSR_SSI_STCR_TSCKP;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		/* Data on rising edge of bclk, frame high, 1clk before data */
		strcr |= CCSR_SSI_STCR_TFSL | CCSR_SSI_STCR_TSCKP |
			CCSR_SSI_STCR_TXBIT0 | CCSR_SSI_STCR_TEFS;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		/* Data on rising edge of bclk, frame high */
		strcr |= CCSR_SSI_STCR_TFSL | CCSR_SSI_STCR_TSCKP |
			CCSR_SSI_STCR_TXBIT0;
		break;
	default:
		return -EINVAL;
	}

	/* DAI clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		/* Nothing to do for both normal cases */
		break;
	case SND_SOC_DAIFMT_IB_NF:
		/* Invert bit clock */
		strcr ^= CCSR_SSI_STCR_TSCKP;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		/* Invert frame clock */
		strcr ^= CCSR_SSI_STCR_TFSI;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		/* Invert both clocks */
		strcr ^= CCSR_SSI_STCR_TSCKP;
		strcr ^= CCSR_SSI_STCR_TFSI;
		break;
	default:
		return -EINVAL;
	}

	/* DAI clock master masks */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		strcr |= CCSR_SSI_STCR_TFDIR | CCSR_SSI_STCR_TXDIR;
		scr |= CCSR_SSI_SCR_SYS_CLK_EN;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		scr &= ~CCSR_SSI_SCR_SYS_CLK_EN;
		break;
	default:
		return -EINVAL;
	}

	stcr |= strcr;
	srcr |= strcr;

	if (ssi_private->cpu_dai_drv.symmetric_rates) {
		/* Need to clear RXDIR when using SYNC mode */
		srcr &= ~CCSR_SSI_SRCR_RXDIR;
		scr |= CCSR_SSI_SCR_SYN;
	}

	write_ssi(stcr, &ssi->stcr);
	write_ssi(srcr, &ssi->srcr);
	write_ssi(scr, &ssi->scr);

	return 0;
}

/**
 * fsl_ssi_set_dai_sysclk - configure Digital Audio Interface bit clock
 *
 * Note: This function can be only called when using SSI as DAI master
 *
 * Quick instruction for parameters:
 * freq: Output BCLK frequency = samplerate * 32 (fixed) * channels
 * dir: SND_SOC_CLOCK_OUT -> TxBCLK, SND_SOC_CLOCK_IN -> RxBCLK.
 */
static int fsl_ssi_set_dai_sysclk(struct snd_soc_dai *cpu_dai,
				  int clk_id, unsigned int freq, int dir)
{
	struct fsl_ssi_private *ssi_private = snd_soc_dai_get_drvdata(cpu_dai);
	struct ccsr_ssi __iomem *ssi = ssi_private->ssi;
	int synchronous = ssi_private->cpu_dai_drv.symmetric_rates, ret;
	u32 pm = 999, div2, psr, stccr, mask, afreq, factor, i;
	unsigned long flags, clkrate, baudrate, tmprate;
	u64 sub, savesub = 100000;

	/* Don't apply it to any non-baudclk circumstance */
	if (IS_ERR(ssi_private->baudclk))
		return -EINVAL;

	/* It should be already enough to divide clock by setting pm alone */
	psr = 0;
	div2 = 0;

	factor = (div2 + 1) * (7 * psr + 1) * 2;

	for (i = 0; i < 255; i++) {
		/* The bclk rate must be smaller than 1/5 sysclk rate */
		if (factor * (i + 1) < 5)
			continue;

		tmprate = freq * factor * (i + 2);
		clkrate = clk_round_rate(ssi_private->baudclk, tmprate);

		do_div(clkrate, factor);
		afreq = (u32)clkrate / (i + 1);

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

		if (sub < savesub) {
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
		dev_err(cpu_dai->dev, "failed to handle the required sysclk\n");
		return -EINVAL;
	}

	stccr = CCSR_SSI_SxCCR_PM(pm + 1) | (div2 ? CCSR_SSI_SxCCR_DIV2 : 0) |
		(psr ? CCSR_SSI_SxCCR_PSR : 0);
	mask = CCSR_SSI_SxCCR_PM_MASK | CCSR_SSI_SxCCR_DIV2 | CCSR_SSI_SxCCR_PSR;

	if (dir == SND_SOC_CLOCK_OUT || synchronous)
		write_ssi_mask(&ssi->stccr, mask, stccr);
	else
		write_ssi_mask(&ssi->srccr, mask, stccr);

	spin_lock_irqsave(&ssi_private->baudclk_lock, flags);
	if (!ssi_private->baudclk_locked) {
		ret = clk_set_rate(ssi_private->baudclk, baudrate);
		if (ret) {
			spin_unlock_irqrestore(&ssi_private->baudclk_lock, flags);
			dev_err(cpu_dai->dev, "failed to set baudclk rate\n");
			return -EINVAL;
		}
		ssi_private->baudclk_locked = true;
	}
	spin_unlock_irqrestore(&ssi_private->baudclk_lock, flags);

	return 0;
}

/**
 * fsl_ssi_set_dai_tdm_slot - set TDM slot number
 *
 * Note: This function can be only called when using SSI as DAI master
 */
static int fsl_ssi_set_dai_tdm_slot(struct snd_soc_dai *cpu_dai, u32 tx_mask,
				u32 rx_mask, int slots, int slot_width)
{
	struct fsl_ssi_private *ssi_private = snd_soc_dai_get_drvdata(cpu_dai);
	struct ccsr_ssi __iomem *ssi = ssi_private->ssi;
	u32 val;

	/* The slot number should be >= 2 if using Network mode or I2S mode */
	val = read_ssi(&ssi->scr) & (CCSR_SSI_SCR_I2S_MODE_MASK | CCSR_SSI_SCR_NET);
	if (val && slots < 2) {
		dev_err(cpu_dai->dev, "slot number should be >= 2 in I2S or NET\n");
		return -EINVAL;
	}

	write_ssi_mask(&ssi->stccr, CCSR_SSI_SxCCR_DC_MASK,
			CCSR_SSI_SxCCR_DC(slots));
	write_ssi_mask(&ssi->srccr, CCSR_SSI_SxCCR_DC_MASK,
			CCSR_SSI_SxCCR_DC(slots));

	/* The register SxMSKs needs SSI to provide essential clock due to
	 * hardware design. So we here temporarily enable SSI to set them.
	 */
	val = read_ssi(&ssi->scr) & CCSR_SSI_SCR_SSIEN;
	write_ssi_mask(&ssi->scr, 0, CCSR_SSI_SCR_SSIEN);

	write_ssi(tx_mask, &ssi->stmsk);
	write_ssi(rx_mask, &ssi->srmsk);

	write_ssi_mask(&ssi->scr, CCSR_SSI_SCR_SSIEN, val);

	return 0;
}

/**
 * fsl_ssi_trigger: start and stop the DMA transfer.
 *
 * This function is called by ALSA to start, stop, pause, and resume the DMA
 * transfer of data.
 *
 * The DMA channel is in external master start and pause mode, which
 * means the SSI completely controls the flow of data.
 */
static int fsl_ssi_trigger(struct snd_pcm_substream *substream, int cmd,
			   struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct fsl_ssi_private *ssi_private = snd_soc_dai_get_drvdata(rtd->cpu_dai);
	struct ccsr_ssi __iomem *ssi = ssi_private->ssi;
	unsigned long flags;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			fsl_ssi_tx_config(ssi_private, true);
		else
			fsl_ssi_rx_config(ssi_private, true);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			fsl_ssi_tx_config(ssi_private, false);
		else
			fsl_ssi_rx_config(ssi_private, false);

		if (!ssi_private->imx_ac97 && (read_ssi(&ssi->scr) &
					(CCSR_SSI_SCR_TE | CCSR_SSI_SCR_RE)) == 0) {
			spin_lock_irqsave(&ssi_private->baudclk_lock, flags);
			ssi_private->baudclk_locked = false;
			spin_unlock_irqrestore(&ssi_private->baudclk_lock, flags);
		}
		break;

	default:
		return -EINVAL;
	}

	if (ssi_private->imx_ac97) {
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			write_ssi(CCSR_SSI_SOR_TX_CLR, &ssi->sor);
		else
			write_ssi(CCSR_SSI_SOR_RX_CLR, &ssi->sor);
	}

	return 0;
}

static int fsl_ssi_dai_probe(struct snd_soc_dai *dai)
{
	struct fsl_ssi_private *ssi_private = snd_soc_dai_get_drvdata(dai);

	if (ssi_private->ssi_on_imx && ssi_private->use_dma) {
		dai->playback_dma_data = &ssi_private->dma_params_tx;
		dai->capture_dma_data = &ssi_private->dma_params_rx;
	}

	return 0;
}

static const struct snd_soc_dai_ops fsl_ssi_dai_ops = {
	.startup	= fsl_ssi_startup,
	.hw_params	= fsl_ssi_hw_params,
	.set_fmt	= fsl_ssi_set_dai_fmt,
	.set_sysclk	= fsl_ssi_set_dai_sysclk,
	.set_tdm_slot	= fsl_ssi_set_dai_tdm_slot,
	.trigger	= fsl_ssi_trigger,
};

/* Template for the CPU dai driver structure */
static struct snd_soc_dai_driver fsl_ssi_dai_template = {
	.probe = fsl_ssi_dai_probe,
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = FSLSSI_I2S_RATES,
		.formats = FSLSSI_I2S_FORMATS,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = FSLSSI_I2S_RATES,
		.formats = FSLSSI_I2S_FORMATS,
	},
	.ops = &fsl_ssi_dai_ops,
};

static const struct snd_soc_component_driver fsl_ssi_component = {
	.name		= "fsl-ssi",
};

static struct snd_soc_dai_driver fsl_ssi_ac97_dai = {
	.ac97_control = 1,
	.playback = {
		.stream_name = "AC97 Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.stream_name = "AC97 Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &fsl_ssi_dai_ops,
};


static struct fsl_ssi_private *fsl_ac97_data;

static void fsl_ssi_ac97_init(void)
{
	fsl_ssi_setup(fsl_ac97_data);
}

static void fsl_ssi_ac97_write(struct snd_ac97 *ac97, unsigned short reg,
		unsigned short val)
{
	struct ccsr_ssi *ssi = fsl_ac97_data->ssi;
	unsigned int lreg;
	unsigned int lval;

	if (reg > 0x7f)
		return;


	lreg = reg <<  12;
	write_ssi(lreg, &ssi->sacadd);

	lval = val << 4;
	write_ssi(lval , &ssi->sacdat);

	write_ssi_mask(&ssi->sacnt, CCSR_SSI_SACNT_RDWR_MASK,
			CCSR_SSI_SACNT_WR);
	udelay(100);
}

static unsigned short fsl_ssi_ac97_read(struct snd_ac97 *ac97,
		unsigned short reg)
{
	struct ccsr_ssi *ssi = fsl_ac97_data->ssi;

	unsigned short val = -1;
	unsigned int lreg;

	lreg = (reg & 0x7f) <<  12;
	write_ssi(lreg, &ssi->sacadd);
	write_ssi_mask(&ssi->sacnt, CCSR_SSI_SACNT_RDWR_MASK,
			CCSR_SSI_SACNT_RD);

	udelay(100);

	val = (read_ssi(&ssi->sacdat) >> 4) & 0xffff;

	return val;
}

static struct snd_ac97_bus_ops fsl_ssi_ac97_ops = {
	.read		= fsl_ssi_ac97_read,
	.write		= fsl_ssi_ac97_write,
};

/**
 * Make every character in a string lower-case
 */
static void make_lowercase(char *s)
{
	char *p = s;
	char c;

	while ((c = *p)) {
		if ((c >= 'A') && (c <= 'Z'))
			*p = c + ('a' - 'A');
		p++;
	}
}

static int fsl_ssi_probe(struct platform_device *pdev)
{
	struct fsl_ssi_private *ssi_private;
	int ret = 0;
	struct device_attribute *dev_attr = NULL;
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *of_id;
	enum fsl_ssi_type hw_type;
	const char *p, *sprop;
	const uint32_t *iprop;
	struct resource res;
	char name[64];
	bool shared;
	bool ac97 = false;

	/* SSIs that are not connected on the board should have a
	 *      status = "disabled"
	 * property in their device tree nodes.
	 */
	if (!of_device_is_available(np))
		return -ENODEV;

	of_id = of_match_device(fsl_ssi_ids, &pdev->dev);
	if (!of_id)
		return -EINVAL;
	hw_type = (enum fsl_ssi_type) of_id->data;

	/* We only support the SSI in "I2S Slave" mode */
	sprop = of_get_property(np, "fsl,mode", NULL);
	if (!sprop) {
		dev_err(&pdev->dev, "fsl,mode property is necessary\n");
		return -EINVAL;
	}
	if (!strcmp(sprop, "ac97-slave")) {
		ac97 = true;
	} else if (strcmp(sprop, "i2s-slave")) {
		dev_notice(&pdev->dev, "mode %s is unsupported\n", sprop);
		return -ENODEV;
	}

	/* The DAI name is the last part of the full name of the node. */
	p = strrchr(np->full_name, '/') + 1;
	ssi_private = devm_kzalloc(&pdev->dev, sizeof(*ssi_private) + strlen(p),
			      GFP_KERNEL);
	if (!ssi_private) {
		dev_err(&pdev->dev, "could not allocate DAI object\n");
		return -ENOMEM;
	}

	strcpy(ssi_private->name, p);

	ssi_private->use_dma = !of_property_read_bool(np,
			"fsl,fiq-stream-filter");
	ssi_private->hw_type = hw_type;

	if (ac97) {
		memcpy(&ssi_private->cpu_dai_drv, &fsl_ssi_ac97_dai,
				sizeof(fsl_ssi_ac97_dai));

		fsl_ac97_data = ssi_private;
		ssi_private->imx_ac97 = true;

		snd_soc_set_ac97_ops_of_reset(&fsl_ssi_ac97_ops, pdev);
	} else {
		/* Initialize this copy of the CPU DAI driver structure */
		memcpy(&ssi_private->cpu_dai_drv, &fsl_ssi_dai_template,
		       sizeof(fsl_ssi_dai_template));
	}
	ssi_private->cpu_dai_drv.name = ssi_private->name;

	/* Get the addresses and IRQ */
	ret = of_address_to_resource(np, 0, &res);
	if (ret) {
		dev_err(&pdev->dev, "could not determine device resources\n");
		return ret;
	}
	ssi_private->ssi = of_iomap(np, 0);
	if (!ssi_private->ssi) {
		dev_err(&pdev->dev, "could not map device resources\n");
		return -ENOMEM;
	}
	ssi_private->ssi_phys = res.start;

	ssi_private->irq = irq_of_parse_and_map(np, 0);
	if (!ssi_private->irq) {
		dev_err(&pdev->dev, "no irq for node %s\n", np->full_name);
		return -ENXIO;
	}

	/* Are the RX and the TX clocks locked? */
	if (!of_find_property(np, "fsl,ssi-asynchronous", NULL)) {
		ssi_private->cpu_dai_drv.symmetric_rates = 1;
		ssi_private->cpu_dai_drv.symmetric_channels = 1;
		ssi_private->cpu_dai_drv.symmetric_samplebits = 1;
	}

	/* Determine the FIFO depth. */
	iprop = of_get_property(np, "fsl,fifo-depth", NULL);
	if (iprop)
		ssi_private->fifo_depth = be32_to_cpup(iprop);
	else
                /* Older 8610 DTs didn't have the fifo-depth property */
		ssi_private->fifo_depth = 8;

	ssi_private->baudclk_locked = false;
	spin_lock_init(&ssi_private->baudclk_lock);

	/*
	 * imx51 and later SoCs have a slightly different IP that allows the
	 * SSI configuration while the SSI unit is running.
	 *
	 * More important, it is necessary on those SoCs to configure the
	 * sperate TX/RX DMA bits just before starting the stream
	 * (fsl_ssi_trigger). The SDMA unit has to be configured before fsl_ssi
	 * sends any DMA requests to the SDMA unit, otherwise it is not defined
	 * how the SDMA unit handles the DMA request.
	 *
	 * SDMA units are present on devices starting at imx35 but the imx35
	 * reference manual states that the DMA bits should not be changed
	 * while the SSI unit is running (SSIEN). So we support the necessary
	 * online configuration of fsl-ssi starting at imx51.
	 */
	switch (hw_type) {
	case FSL_SSI_MCP8610:
	case FSL_SSI_MX21:
	case FSL_SSI_MX35:
		ssi_private->offline_config = true;
		break;
	case FSL_SSI_MX51:
		ssi_private->offline_config = false;
		break;
	}

	if (hw_type == FSL_SSI_MX21 || hw_type == FSL_SSI_MX51 ||
			hw_type == FSL_SSI_MX35) {
		u32 dma_events[2], dmas[4];
		ssi_private->ssi_on_imx = true;

		ssi_private->clk = devm_clk_get(&pdev->dev, NULL);
		if (IS_ERR(ssi_private->clk)) {
			ret = PTR_ERR(ssi_private->clk);
			dev_err(&pdev->dev, "could not get clock: %d\n", ret);
			goto error_irqmap;
		}
		ret = clk_prepare_enable(ssi_private->clk);
		if (ret) {
			dev_err(&pdev->dev, "clk_prepare_enable failed: %d\n",
				ret);
			goto error_irqmap;
		}

		/* For those SLAVE implementations, we ingore non-baudclk cases
		 * and, instead, abandon MASTER mode that needs baud clock.
		 */
		ssi_private->baudclk = devm_clk_get(&pdev->dev, "baud");
		if (IS_ERR(ssi_private->baudclk))
			dev_warn(&pdev->dev, "could not get baud clock: %ld\n",
				 PTR_ERR(ssi_private->baudclk));
		else
			clk_prepare_enable(ssi_private->baudclk);

		/*
		 * We have burstsize be "fifo_depth - 2" to match the SSI
		 * watermark setting in fsl_ssi_startup().
		 */
		ssi_private->dma_params_tx.maxburst =
			ssi_private->fifo_depth - 2;
		ssi_private->dma_params_rx.maxburst =
			ssi_private->fifo_depth - 2;
		ssi_private->dma_params_tx.addr =
			ssi_private->ssi_phys + offsetof(struct ccsr_ssi, stx0);
		ssi_private->dma_params_rx.addr =
			ssi_private->ssi_phys + offsetof(struct ccsr_ssi, srx0);
		ssi_private->dma_params_tx.filter_data =
			&ssi_private->filter_data_tx;
		ssi_private->dma_params_rx.filter_data =
			&ssi_private->filter_data_rx;
		if (!of_property_read_bool(pdev->dev.of_node, "dmas") &&
				ssi_private->use_dma) {
			/*
			 * FIXME: This is a temporary solution until all
			 * necessary dma drivers support the generic dma
			 * bindings.
			 */
			ret = of_property_read_u32_array(pdev->dev.of_node,
					"fsl,ssi-dma-events", dma_events, 2);
			if (ret && ssi_private->use_dma) {
				dev_err(&pdev->dev, "could not get dma events but fsl-ssi is configured to use DMA\n");
				goto error_clk;
			}
		}
		/* Should this be merge with the above? */
		if (!of_property_read_u32_array(pdev->dev.of_node, "dmas", dmas, 4)
				&& dmas[2] == IMX_DMATYPE_SSI_DUAL) {
			ssi_private->use_dual_fifo = true;
			/* When using dual fifo mode, we need to keep watermark
			 * as even numbers due to dma script limitation.
			 */
			ssi_private->dma_params_tx.maxburst &= ~0x1;
			ssi_private->dma_params_rx.maxburst &= ~0x1;
		}

		shared = of_device_is_compatible(of_get_parent(np),
			    "fsl,spba-bus");

		imx_pcm_dma_params_init_data(&ssi_private->filter_data_tx,
			dma_events[0], shared ? IMX_DMATYPE_SSI_SP : IMX_DMATYPE_SSI);
		imx_pcm_dma_params_init_data(&ssi_private->filter_data_rx,
			dma_events[1], shared ? IMX_DMATYPE_SSI_SP : IMX_DMATYPE_SSI);
	}

	/*
	 * Enable interrupts only for MCP8610 and MX51. The other MXs have
	 * different writeable interrupt status registers.
	 */
	if (ssi_private->use_dma) {
		/* The 'name' should not have any slashes in it. */
		ret = devm_request_irq(&pdev->dev, ssi_private->irq,
					fsl_ssi_isr, 0, ssi_private->name,
					ssi_private);
		ssi_private->irq_stats = true;
		if (ret < 0) {
			dev_err(&pdev->dev, "could not claim irq %u\n",
					ssi_private->irq);
			goto error_clk;
		}
	}

	/* Register with ASoC */
	dev_set_drvdata(&pdev->dev, ssi_private);

	ret = snd_soc_register_component(&pdev->dev, &fsl_ssi_component,
					 &ssi_private->cpu_dai_drv, 1);
	if (ret) {
		dev_err(&pdev->dev, "failed to register DAI: %d\n", ret);
		goto error_dev;
	}

	ret = fsl_ssi_debugfs_create(ssi_private, &pdev->dev);
	if (ret)
		goto error_dbgfs;

	if (ssi_private->ssi_on_imx) {
		if (!ssi_private->use_dma) {

			/*
			 * Some boards use an incompatible codec. To get it
			 * working, we are using imx-fiq-pcm-audio, that
			 * can handle those codecs. DMA is not possible in this
			 * situation.
			 */

			ssi_private->fiq_params.irq = ssi_private->irq;
			ssi_private->fiq_params.base = ssi_private->ssi;
			ssi_private->fiq_params.dma_params_rx =
				&ssi_private->dma_params_rx;
			ssi_private->fiq_params.dma_params_tx =
				&ssi_private->dma_params_tx;

			ret = imx_pcm_fiq_init(pdev, &ssi_private->fiq_params);
			if (ret)
				goto error_pcm;
		} else {
			ret = imx_pcm_dma_init(pdev);
			if (ret)
				goto error_pcm;
		}
	}

	/*
	 * If codec-handle property is missing from SSI node, we assume
	 * that the machine driver uses new binding which does not require
	 * SSI driver to trigger machine driver's probe.
	 */
	if (!of_get_property(np, "codec-handle", NULL)) {
		ssi_private->new_binding = true;
		goto done;
	}

	/* Trigger the machine driver's probe function.  The platform driver
	 * name of the machine driver is taken from /compatible property of the
	 * device tree.  We also pass the address of the CPU DAI driver
	 * structure.
	 */
	sprop = of_get_property(of_find_node_by_path("/"), "compatible", NULL);
	/* Sometimes the compatible name has a "fsl," prefix, so we strip it. */
	p = strrchr(sprop, ',');
	if (p)
		sprop = p + 1;
	snprintf(name, sizeof(name), "snd-soc-%s", sprop);
	make_lowercase(name);

	ssi_private->pdev =
		platform_device_register_data(&pdev->dev, name, 0, NULL, 0);
	if (IS_ERR(ssi_private->pdev)) {
		ret = PTR_ERR(ssi_private->pdev);
		dev_err(&pdev->dev, "failed to register platform: %d\n", ret);
		goto error_dai;
	}

done:
	if (ssi_private->imx_ac97)
		fsl_ssi_ac97_init();

	return 0;

error_dai:
	if (ssi_private->ssi_on_imx && !ssi_private->use_dma)
		imx_pcm_fiq_exit(pdev);

error_pcm:
	fsl_ssi_debugfs_remove(ssi_private);

error_dbgfs:
	snd_soc_unregister_component(&pdev->dev);

error_dev:
	device_remove_file(&pdev->dev, dev_attr);

error_clk:
	if (ssi_private->ssi_on_imx) {
		if (!IS_ERR(ssi_private->baudclk))
			clk_disable_unprepare(ssi_private->baudclk);
		clk_disable_unprepare(ssi_private->clk);
	}

error_irqmap:
	if (ssi_private->irq_stats)
		irq_dispose_mapping(ssi_private->irq);

	return ret;
}

static int fsl_ssi_remove(struct platform_device *pdev)
{
	struct fsl_ssi_private *ssi_private = dev_get_drvdata(&pdev->dev);

	fsl_ssi_debugfs_remove(ssi_private);

	if (!ssi_private->new_binding)
		platform_device_unregister(ssi_private->pdev);
	snd_soc_unregister_component(&pdev->dev);
	if (ssi_private->ssi_on_imx) {
		if (!IS_ERR(ssi_private->baudclk))
			clk_disable_unprepare(ssi_private->baudclk);
		clk_disable_unprepare(ssi_private->clk);
	}
	if (ssi_private->irq_stats)
		irq_dispose_mapping(ssi_private->irq);

	return 0;
}

static struct platform_driver fsl_ssi_driver = {
	.driver = {
		.name = "fsl-ssi-dai",
		.owner = THIS_MODULE,
		.of_match_table = fsl_ssi_ids,
	},
	.probe = fsl_ssi_probe,
	.remove = fsl_ssi_remove,
};

module_platform_driver(fsl_ssi_driver);

MODULE_ALIAS("platform:fsl-ssi-dai");
MODULE_AUTHOR("Timur Tabi <timur@freescale.com>");
MODULE_DESCRIPTION("Freescale Synchronous Serial Interface (SSI) ASoC Driver");
MODULE_LICENSE("GPL v2");
