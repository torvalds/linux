/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * Author: Ola Lilja <ola.o.lilja@stericsson.com>,
 *         Roger Nilsson <roger.xr.nilsson@stericsson.com>,
 *         Sandeep Kaushik <sandeep.kaushik@st.com>
 *         for ST-Ericsson.
 *
 * License terms:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include <mach/hardware.h>
#include <mach/board-mop500-msp.h>

#include <sound/soc.h>

#include "ux500_msp_i2s.h"

 /* Protocol desciptors */
static const struct msp_protdesc prot_descs[] = {
	{ /* I2S */
		MSP_SINGLE_PHASE,
		MSP_SINGLE_PHASE,
		MSP_PHASE2_START_MODE_IMEDIATE,
		MSP_PHASE2_START_MODE_IMEDIATE,
		MSP_BTF_MS_BIT_FIRST,
		MSP_BTF_MS_BIT_FIRST,
		MSP_FRAME_LEN_1,
		MSP_FRAME_LEN_1,
		MSP_FRAME_LEN_1,
		MSP_FRAME_LEN_1,
		MSP_ELEM_LEN_32,
		MSP_ELEM_LEN_32,
		MSP_ELEM_LEN_32,
		MSP_ELEM_LEN_32,
		MSP_DELAY_1,
		MSP_DELAY_1,
		MSP_RISING_EDGE,
		MSP_FALLING_EDGE,
		MSP_FSYNC_POL_ACT_LO,
		MSP_FSYNC_POL_ACT_LO,
		MSP_SWAP_NONE,
		MSP_SWAP_NONE,
		MSP_COMPRESS_MODE_LINEAR,
		MSP_EXPAND_MODE_LINEAR,
		MSP_FSYNC_IGNORE,
		31,
		15,
		32,
	}, { /* PCM */
		MSP_DUAL_PHASE,
		MSP_DUAL_PHASE,
		MSP_PHASE2_START_MODE_FSYNC,
		MSP_PHASE2_START_MODE_FSYNC,
		MSP_BTF_MS_BIT_FIRST,
		MSP_BTF_MS_BIT_FIRST,
		MSP_FRAME_LEN_1,
		MSP_FRAME_LEN_1,
		MSP_FRAME_LEN_1,
		MSP_FRAME_LEN_1,
		MSP_ELEM_LEN_16,
		MSP_ELEM_LEN_16,
		MSP_ELEM_LEN_16,
		MSP_ELEM_LEN_16,
		MSP_DELAY_0,
		MSP_DELAY_0,
		MSP_RISING_EDGE,
		MSP_FALLING_EDGE,
		MSP_FSYNC_POL_ACT_HI,
		MSP_FSYNC_POL_ACT_HI,
		MSP_SWAP_NONE,
		MSP_SWAP_NONE,
		MSP_COMPRESS_MODE_LINEAR,
		MSP_EXPAND_MODE_LINEAR,
		MSP_FSYNC_IGNORE,
		255,
		0,
		256,
	}, { /* Companded PCM */
		MSP_SINGLE_PHASE,
		MSP_SINGLE_PHASE,
		MSP_PHASE2_START_MODE_FSYNC,
		MSP_PHASE2_START_MODE_FSYNC,
		MSP_BTF_MS_BIT_FIRST,
		MSP_BTF_MS_BIT_FIRST,
		MSP_FRAME_LEN_1,
		MSP_FRAME_LEN_1,
		MSP_FRAME_LEN_1,
		MSP_FRAME_LEN_1,
		MSP_ELEM_LEN_8,
		MSP_ELEM_LEN_8,
		MSP_ELEM_LEN_8,
		MSP_ELEM_LEN_8,
		MSP_DELAY_0,
		MSP_DELAY_0,
		MSP_RISING_EDGE,
		MSP_RISING_EDGE,
		MSP_FSYNC_POL_ACT_HI,
		MSP_FSYNC_POL_ACT_HI,
		MSP_SWAP_NONE,
		MSP_SWAP_NONE,
		MSP_COMPRESS_MODE_LINEAR,
		MSP_EXPAND_MODE_LINEAR,
		MSP_FSYNC_IGNORE,
		255,
		0,
		256,
	},
};

static void set_prot_desc_tx(struct ux500_msp *msp,
			struct msp_protdesc *protdesc,
			enum msp_data_size data_size)
{
	u32 temp_reg = 0;

	temp_reg |= MSP_P2_ENABLE_BIT(protdesc->tx_phase_mode);
	temp_reg |= MSP_P2_START_MODE_BIT(protdesc->tx_phase2_start_mode);
	temp_reg |= MSP_P1_FRAME_LEN_BITS(protdesc->tx_frame_len_1);
	temp_reg |= MSP_P2_FRAME_LEN_BITS(protdesc->tx_frame_len_2);
	if (msp->def_elem_len) {
		temp_reg |= MSP_P1_ELEM_LEN_BITS(protdesc->tx_elem_len_1);
		temp_reg |= MSP_P2_ELEM_LEN_BITS(protdesc->tx_elem_len_2);
	} else {
		temp_reg |= MSP_P1_ELEM_LEN_BITS(data_size);
		temp_reg |= MSP_P2_ELEM_LEN_BITS(data_size);
	}
	temp_reg |= MSP_DATA_DELAY_BITS(protdesc->tx_data_delay);
	temp_reg |= MSP_SET_ENDIANNES_BIT(protdesc->tx_byte_order);
	temp_reg |= MSP_FSYNC_POL(protdesc->tx_fsync_pol);
	temp_reg |= MSP_DATA_WORD_SWAP(protdesc->tx_half_word_swap);
	temp_reg |= MSP_SET_COMPANDING_MODE(protdesc->compression_mode);
	temp_reg |= MSP_SET_FSYNC_IGNORE(protdesc->frame_sync_ignore);

	writel(temp_reg, msp->registers + MSP_TCF);
}

static void set_prot_desc_rx(struct ux500_msp *msp,
			struct msp_protdesc *protdesc,
			enum msp_data_size data_size)
{
	u32 temp_reg = 0;

	temp_reg |= MSP_P2_ENABLE_BIT(protdesc->rx_phase_mode);
	temp_reg |= MSP_P2_START_MODE_BIT(protdesc->rx_phase2_start_mode);
	temp_reg |= MSP_P1_FRAME_LEN_BITS(protdesc->rx_frame_len_1);
	temp_reg |= MSP_P2_FRAME_LEN_BITS(protdesc->rx_frame_len_2);
	if (msp->def_elem_len) {
		temp_reg |= MSP_P1_ELEM_LEN_BITS(protdesc->rx_elem_len_1);
		temp_reg |= MSP_P2_ELEM_LEN_BITS(protdesc->rx_elem_len_2);
	} else {
		temp_reg |= MSP_P1_ELEM_LEN_BITS(data_size);
		temp_reg |= MSP_P2_ELEM_LEN_BITS(data_size);
	}

	temp_reg |= MSP_DATA_DELAY_BITS(protdesc->rx_data_delay);
	temp_reg |= MSP_SET_ENDIANNES_BIT(protdesc->rx_byte_order);
	temp_reg |= MSP_FSYNC_POL(protdesc->rx_fsync_pol);
	temp_reg |= MSP_DATA_WORD_SWAP(protdesc->rx_half_word_swap);
	temp_reg |= MSP_SET_COMPANDING_MODE(protdesc->expansion_mode);
	temp_reg |= MSP_SET_FSYNC_IGNORE(protdesc->frame_sync_ignore);

	writel(temp_reg, msp->registers + MSP_RCF);
}

static int configure_protocol(struct ux500_msp *msp,
			struct ux500_msp_config *config)
{
	struct msp_protdesc *protdesc;
	enum msp_data_size data_size;
	u32 temp_reg = 0;

	data_size = config->data_size;
	msp->def_elem_len = config->def_elem_len;
	if (config->default_protdesc == 1) {
		if (config->protocol >= MSP_INVALID_PROTOCOL) {
			dev_err(msp->dev, "%s: ERROR: Invalid protocol!\n",
				__func__);
			return -EINVAL;
		}
		protdesc =
		    (struct msp_protdesc *)&prot_descs[config->protocol];
	} else {
		protdesc = (struct msp_protdesc *)&config->protdesc;
	}

	if (data_size < MSP_DATA_BITS_DEFAULT || data_size > MSP_DATA_BITS_32) {
		dev_err(msp->dev,
			"%s: ERROR: Invalid data-size requested (data_size = %d)!\n",
			__func__, data_size);
		return -EINVAL;
	}

	if (config->direction & MSP_DIR_TX)
		set_prot_desc_tx(msp, protdesc, data_size);
	if (config->direction & MSP_DIR_RX)
		set_prot_desc_rx(msp, protdesc, data_size);

	/* The code below should not be separated. */
	temp_reg = readl(msp->registers + MSP_GCR) & ~TX_CLK_POL_RISING;
	temp_reg |= MSP_TX_CLKPOL_BIT(~protdesc->tx_clk_pol);
	writel(temp_reg, msp->registers + MSP_GCR);
	temp_reg = readl(msp->registers + MSP_GCR) & ~RX_CLK_POL_RISING;
	temp_reg |= MSP_RX_CLKPOL_BIT(protdesc->rx_clk_pol);
	writel(temp_reg, msp->registers + MSP_GCR);

	return 0;
}

static int setup_bitclk(struct ux500_msp *msp, struct ux500_msp_config *config)
{
	u32 reg_val_GCR;
	u32 frame_per = 0;
	u32 sck_div = 0;
	u32 frame_width = 0;
	u32 temp_reg = 0;
	struct msp_protdesc *protdesc = NULL;

	reg_val_GCR = readl(msp->registers + MSP_GCR);
	writel(reg_val_GCR & ~SRG_ENABLE, msp->registers + MSP_GCR);

	if (config->default_protdesc)
		protdesc =
			(struct msp_protdesc *)&prot_descs[config->protocol];
	else
		protdesc = (struct msp_protdesc *)&config->protdesc;

	switch (config->protocol) {
	case MSP_PCM_PROTOCOL:
	case MSP_PCM_COMPAND_PROTOCOL:
		frame_width = protdesc->frame_width;
		sck_div = config->f_inputclk / (config->frame_freq *
			(protdesc->clocks_per_frame));
		frame_per = protdesc->frame_period;
		break;
	case MSP_I2S_PROTOCOL:
		frame_width = protdesc->frame_width;
		sck_div = config->f_inputclk / (config->frame_freq *
			(protdesc->clocks_per_frame));
		frame_per = protdesc->frame_period;
		break;
	default:
		dev_err(msp->dev, "%s: ERROR: Unknown protocol (%d)!\n",
			__func__,
			config->protocol);
		return -EINVAL;
	}

	temp_reg = (sck_div - 1) & SCK_DIV_MASK;
	temp_reg |= FRAME_WIDTH_BITS(frame_width);
	temp_reg |= FRAME_PERIOD_BITS(frame_per);
	writel(temp_reg, msp->registers + MSP_SRG);

	msp->f_bitclk = (config->f_inputclk)/(sck_div + 1);

	/* Enable bit-clock */
	udelay(100);
	reg_val_GCR = readl(msp->registers + MSP_GCR);
	writel(reg_val_GCR | SRG_ENABLE, msp->registers + MSP_GCR);
	udelay(100);

	return 0;
}

static int configure_multichannel(struct ux500_msp *msp,
				struct ux500_msp_config *config)
{
	struct msp_protdesc *protdesc;
	struct msp_multichannel_config *mcfg;
	u32 reg_val_MCR;

	if (config->default_protdesc == 1) {
		if (config->protocol >= MSP_INVALID_PROTOCOL) {
			dev_err(msp->dev,
				"%s: ERROR: Invalid protocol (%d)!\n",
				__func__, config->protocol);
			return -EINVAL;
		}
		protdesc = (struct msp_protdesc *)
				&prot_descs[config->protocol];
	} else {
		protdesc = (struct msp_protdesc *)&config->protdesc;
	}

	mcfg = &config->multichannel_config;
	if (mcfg->tx_multichannel_enable) {
		if (protdesc->tx_phase_mode == MSP_SINGLE_PHASE) {
			reg_val_MCR = readl(msp->registers + MSP_MCR);
			writel(reg_val_MCR | (mcfg->tx_multichannel_enable ?
						1 << TMCEN_BIT : 0),
				msp->registers + MSP_MCR);
			writel(mcfg->tx_channel_0_enable,
				msp->registers + MSP_TCE0);
			writel(mcfg->tx_channel_1_enable,
				msp->registers + MSP_TCE1);
			writel(mcfg->tx_channel_2_enable,
				msp->registers + MSP_TCE2);
			writel(mcfg->tx_channel_3_enable,
				msp->registers + MSP_TCE3);
		} else {
			dev_err(msp->dev,
				"%s: ERROR: Only single-phase supported (TX-mode: %d)!\n",
				__func__, protdesc->tx_phase_mode);
			return -EINVAL;
		}
	}
	if (mcfg->rx_multichannel_enable) {
		if (protdesc->rx_phase_mode == MSP_SINGLE_PHASE) {
			reg_val_MCR = readl(msp->registers + MSP_MCR);
			writel(reg_val_MCR | (mcfg->rx_multichannel_enable ?
						1 << RMCEN_BIT : 0),
				msp->registers + MSP_MCR);
			writel(mcfg->rx_channel_0_enable,
					msp->registers + MSP_RCE0);
			writel(mcfg->rx_channel_1_enable,
					msp->registers + MSP_RCE1);
			writel(mcfg->rx_channel_2_enable,
					msp->registers + MSP_RCE2);
			writel(mcfg->rx_channel_3_enable,
					msp->registers + MSP_RCE3);
		} else {
			dev_err(msp->dev,
				"%s: ERROR: Only single-phase supported (RX-mode: %d)!\n",
				__func__, protdesc->rx_phase_mode);
			return -EINVAL;
		}
		if (mcfg->rx_comparison_enable_mode) {
			reg_val_MCR = readl(msp->registers + MSP_MCR);
			writel(reg_val_MCR |
				(mcfg->rx_comparison_enable_mode << RCMPM_BIT),
				msp->registers + MSP_MCR);

			writel(mcfg->comparison_mask,
					msp->registers + MSP_RCM);
			writel(mcfg->comparison_value,
					msp->registers + MSP_RCV);

		}
	}

	return 0;
}

static int enable_msp(struct ux500_msp *msp, struct ux500_msp_config *config)
{
	int status = 0;
	u32 reg_val_DMACR, reg_val_GCR;

	/* Check msp state whether in RUN or CONFIGURED Mode */
	if ((msp->msp_state == MSP_STATE_IDLE) && (msp->plat_init)) {
		status = msp->plat_init();
		if (status) {
			dev_err(msp->dev, "%s: ERROR: Failed to init MSP (%d)!\n",
				__func__, status);
			return status;
		}
	}

	/* Configure msp with protocol dependent settings */
	configure_protocol(msp, config);
	setup_bitclk(msp, config);
	if (config->multichannel_configured == 1) {
		status = configure_multichannel(msp, config);
		if (status)
			dev_warn(msp->dev,
				"%s: WARN: configure_multichannel failed (%d)!\n",
				__func__, status);
	}

	/* Make sure the correct DMA-directions are configured */
	if ((config->direction & MSP_DIR_RX) && (!msp->dma_cfg_rx)) {
		dev_err(msp->dev, "%s: ERROR: MSP RX-mode is not configured!",
			__func__);
		return -EINVAL;
	}
	if ((config->direction == MSP_DIR_TX) && (!msp->dma_cfg_tx)) {
		dev_err(msp->dev, "%s: ERROR: MSP TX-mode is not configured!",
			__func__);
		return -EINVAL;
	}

	reg_val_DMACR = readl(msp->registers + MSP_DMACR);
	if (config->direction & MSP_DIR_RX)
		reg_val_DMACR |= RX_DMA_ENABLE;
	if (config->direction & MSP_DIR_TX)
		reg_val_DMACR |= TX_DMA_ENABLE;
	writel(reg_val_DMACR, msp->registers + MSP_DMACR);

	writel(config->iodelay, msp->registers + MSP_IODLY);

	/* Enable frame generation logic */
	reg_val_GCR = readl(msp->registers + MSP_GCR);
	writel(reg_val_GCR | FRAME_GEN_ENABLE, msp->registers + MSP_GCR);

	return status;
}

static void flush_fifo_rx(struct ux500_msp *msp)
{
	u32 reg_val_DR, reg_val_GCR, reg_val_FLR;
	u32 limit = 32;

	reg_val_GCR = readl(msp->registers + MSP_GCR);
	writel(reg_val_GCR | RX_ENABLE, msp->registers + MSP_GCR);

	reg_val_FLR = readl(msp->registers + MSP_FLR);
	while (!(reg_val_FLR & RX_FIFO_EMPTY) && limit--) {
		reg_val_DR = readl(msp->registers + MSP_DR);
		reg_val_FLR = readl(msp->registers + MSP_FLR);
	}

	writel(reg_val_GCR, msp->registers + MSP_GCR);
}

static void flush_fifo_tx(struct ux500_msp *msp)
{
	u32 reg_val_TSTDR, reg_val_GCR, reg_val_FLR;
	u32 limit = 32;

	reg_val_GCR = readl(msp->registers + MSP_GCR);
	writel(reg_val_GCR | TX_ENABLE, msp->registers + MSP_GCR);
	writel(MSP_ITCR_ITEN | MSP_ITCR_TESTFIFO, msp->registers + MSP_ITCR);

	reg_val_FLR = readl(msp->registers + MSP_FLR);
	while (!(reg_val_FLR & TX_FIFO_EMPTY) && limit--) {
		reg_val_TSTDR = readl(msp->registers + MSP_TSTDR);
		reg_val_FLR = readl(msp->registers + MSP_FLR);
	}
	writel(0x0, msp->registers + MSP_ITCR);
	writel(reg_val_GCR, msp->registers + MSP_GCR);
}

int ux500_msp_i2s_open(struct ux500_msp *msp,
		struct ux500_msp_config *config)
{
	u32 old_reg, new_reg, mask;
	int res;
	unsigned int tx_sel, rx_sel, tx_busy, rx_busy;

	if (in_interrupt()) {
		dev_err(msp->dev,
			"%s: ERROR: Open called in interrupt context!\n",
			__func__);
		return -1;
	}

	tx_sel = (config->direction & MSP_DIR_TX) > 0;
	rx_sel = (config->direction & MSP_DIR_RX) > 0;
	if (!tx_sel && !rx_sel) {
		dev_err(msp->dev, "%s: Error: No direction selected!\n",
			__func__);
		return -EINVAL;
	}

	tx_busy = (msp->dir_busy & MSP_DIR_TX) > 0;
	rx_busy = (msp->dir_busy & MSP_DIR_RX) > 0;
	if (tx_busy && tx_sel) {
		dev_err(msp->dev, "%s: Error: TX is in use!\n", __func__);
		return -EBUSY;
	}
	if (rx_busy && rx_sel) {
		dev_err(msp->dev, "%s: Error: RX is in use!\n", __func__);
		return -EBUSY;
	}

	msp->dir_busy |= (tx_sel ? MSP_DIR_TX : 0) | (rx_sel ? MSP_DIR_RX : 0);

	/* First do the global config register */
	mask = RX_CLK_SEL_MASK | TX_CLK_SEL_MASK | RX_FSYNC_MASK |
	    TX_FSYNC_MASK | RX_SYNC_SEL_MASK | TX_SYNC_SEL_MASK |
	    RX_FIFO_ENABLE_MASK | TX_FIFO_ENABLE_MASK | SRG_CLK_SEL_MASK |
	    LOOPBACK_MASK | TX_EXTRA_DELAY_MASK;

	new_reg = (config->tx_clk_sel | config->rx_clk_sel |
		config->rx_fsync_pol | config->tx_fsync_pol |
		config->rx_fsync_sel | config->tx_fsync_sel |
		config->rx_fifo_config | config->tx_fifo_config |
		config->srg_clk_sel | config->loopback_enable |
		config->tx_data_enable);

	old_reg = readl(msp->registers + MSP_GCR);
	old_reg &= ~mask;
	new_reg |= old_reg;
	writel(new_reg, msp->registers + MSP_GCR);

	res = enable_msp(msp, config);
	if (res < 0) {
		dev_err(msp->dev, "%s: ERROR: enable_msp failed (%d)!\n",
			__func__, res);
		return -EBUSY;
	}
	if (config->loopback_enable & 0x80)
		msp->loopback_enable = 1;

	/* Flush FIFOs */
	flush_fifo_tx(msp);
	flush_fifo_rx(msp);

	msp->msp_state = MSP_STATE_CONFIGURED;
	return 0;
}

static void disable_msp_rx(struct ux500_msp *msp)
{
	u32 reg_val_GCR, reg_val_DMACR, reg_val_IMSC;

	reg_val_GCR = readl(msp->registers + MSP_GCR);
	writel(reg_val_GCR & ~RX_ENABLE, msp->registers + MSP_GCR);
	reg_val_DMACR = readl(msp->registers + MSP_DMACR);
	writel(reg_val_DMACR & ~RX_DMA_ENABLE, msp->registers + MSP_DMACR);
	reg_val_IMSC = readl(msp->registers + MSP_IMSC);
	writel(reg_val_IMSC &
			~(RX_SERVICE_INT | RX_OVERRUN_ERROR_INT),
			msp->registers + MSP_IMSC);

	msp->dir_busy &= ~MSP_DIR_RX;
}

static void disable_msp_tx(struct ux500_msp *msp)
{
	u32 reg_val_GCR, reg_val_DMACR, reg_val_IMSC;

	reg_val_GCR = readl(msp->registers + MSP_GCR);
	writel(reg_val_GCR & ~TX_ENABLE, msp->registers + MSP_GCR);
	reg_val_DMACR = readl(msp->registers + MSP_DMACR);
	writel(reg_val_DMACR & ~TX_DMA_ENABLE, msp->registers + MSP_DMACR);
	reg_val_IMSC = readl(msp->registers + MSP_IMSC);
	writel(reg_val_IMSC &
			~(TX_SERVICE_INT | TX_UNDERRUN_ERR_INT),
			msp->registers + MSP_IMSC);

	msp->dir_busy &= ~MSP_DIR_TX;
}

static int disable_msp(struct ux500_msp *msp, unsigned int dir)
{
	u32 reg_val_GCR;
	int status = 0;
	unsigned int disable_tx, disable_rx;

	reg_val_GCR = readl(msp->registers + MSP_GCR);
	disable_tx = dir & MSP_DIR_TX;
	disable_rx = dir & MSP_DIR_TX;
	if (disable_tx && disable_rx) {
		reg_val_GCR = readl(msp->registers + MSP_GCR);
		writel(reg_val_GCR | LOOPBACK_MASK,
				msp->registers + MSP_GCR);

		/* Flush TX-FIFO */
		flush_fifo_tx(msp);

		/* Disable TX-channel */
		writel((readl(msp->registers + MSP_GCR) &
			       (~TX_ENABLE)), msp->registers + MSP_GCR);

		/* Flush RX-FIFO */
		flush_fifo_rx(msp);

		/* Disable Loopback and Receive channel */
		writel((readl(msp->registers + MSP_GCR) &
				(~(RX_ENABLE | LOOPBACK_MASK))),
				msp->registers + MSP_GCR);

		disable_msp_tx(msp);
		disable_msp_rx(msp);
	} else if (disable_tx)
		disable_msp_tx(msp);
	else if (disable_rx)
		disable_msp_rx(msp);

	return status;
}

int ux500_msp_i2s_trigger(struct ux500_msp *msp, int cmd, int direction)
{
	u32 reg_val_GCR, enable_bit;

	if (msp->msp_state == MSP_STATE_IDLE) {
		dev_err(msp->dev, "%s: ERROR: MSP is not configured!\n",
			__func__);
		return -EINVAL;
	}

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (direction == SNDRV_PCM_STREAM_PLAYBACK)
			enable_bit = TX_ENABLE;
		else
			enable_bit = RX_ENABLE;
		reg_val_GCR = readl(msp->registers + MSP_GCR);
		writel(reg_val_GCR | enable_bit, msp->registers + MSP_GCR);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (direction == SNDRV_PCM_STREAM_PLAYBACK)
			disable_msp_tx(msp);
		else
			disable_msp_rx(msp);
		break;
	default:
		return -EINVAL;
		break;
	}

	return 0;
}

int ux500_msp_i2s_close(struct ux500_msp *msp, unsigned int dir)
{
	int status = 0;

	dev_dbg(msp->dev, "%s: Enter (dir = 0x%01x).\n", __func__, dir);

	status = disable_msp(msp, dir);
	if (msp->dir_busy == 0) {
		/* disable sample rate and frame generators */
		msp->msp_state = MSP_STATE_IDLE;
		writel((readl(msp->registers + MSP_GCR) &
			       (~(FRAME_GEN_ENABLE | SRG_ENABLE))),
			      msp->registers + MSP_GCR);
		if (msp->plat_exit)
			status = msp->plat_exit();
			if (status)
				dev_warn(msp->dev,
					"%s: WARN: ux500_msp_i2s_exit failed (%d)!\n",
					__func__, status);
		writel(0, msp->registers + MSP_GCR);
		writel(0, msp->registers + MSP_TCF);
		writel(0, msp->registers + MSP_RCF);
		writel(0, msp->registers + MSP_DMACR);
		writel(0, msp->registers + MSP_SRG);
		writel(0, msp->registers + MSP_MCR);
		writel(0, msp->registers + MSP_RCM);
		writel(0, msp->registers + MSP_RCV);
		writel(0, msp->registers + MSP_TCE0);
		writel(0, msp->registers + MSP_TCE1);
		writel(0, msp->registers + MSP_TCE2);
		writel(0, msp->registers + MSP_TCE3);
		writel(0, msp->registers + MSP_RCE0);
		writel(0, msp->registers + MSP_RCE1);
		writel(0, msp->registers + MSP_RCE2);
		writel(0, msp->registers + MSP_RCE3);
	}

	return status;

}

int ux500_msp_i2s_init_msp(struct platform_device *pdev,
			struct ux500_msp **msp_p,
			struct msp_i2s_platform_data *platform_data)
{
	int ret = 0;
	struct resource *res = NULL;
	struct i2s_controller *i2s_cont;
	struct ux500_msp *msp;

	dev_dbg(&pdev->dev, "%s: Enter (name: %s, id: %d).\n", __func__,
		pdev->name, platform_data->id);

	*msp_p = devm_kzalloc(&pdev->dev, sizeof(struct ux500_msp), GFP_KERNEL);
	msp = *msp_p;

	msp->id = platform_data->id;
	msp->dev = &pdev->dev;
	msp->plat_init = platform_data->msp_i2s_init;
	msp->plat_exit = platform_data->msp_i2s_exit;
	msp->dma_cfg_rx = platform_data->msp_i2s_dma_rx;
	msp->dma_cfg_tx = platform_data->msp_i2s_dma_tx;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "%s: ERROR: Unable to get resource!\n",
			__func__);
		ret = -ENOMEM;
		goto err_res;
	}

	msp->registers = ioremap(res->start, (res->end - res->start + 1));
	if (msp->registers == NULL) {
		dev_err(&pdev->dev, "%s: ERROR: ioremap failed!\n", __func__);
		ret = -ENOMEM;
		goto err_res;
	}

	msp->msp_state = MSP_STATE_IDLE;
	msp->loopback_enable = 0;

	/* I2S-controller is allocated and added in I2S controller class. */
	i2s_cont = devm_kzalloc(&pdev->dev, sizeof(*i2s_cont), GFP_KERNEL);
	if (!i2s_cont) {
		dev_err(&pdev->dev,
			"%s: ERROR: Failed to allocate I2S-controller!\n",
			__func__);
		goto err_i2s_cont;
	}
	i2s_cont->dev.parent = &pdev->dev;
	i2s_cont->data = (void *)msp;
	i2s_cont->id = (s16)msp->id;
	snprintf(i2s_cont->name, sizeof(i2s_cont->name), "ux500-msp-i2s.%04x",
		msp->id);
	dev_dbg(&pdev->dev, "I2S device-name: '%s'\n", i2s_cont->name);
	msp->i2s_cont = i2s_cont;

	return 0;

err_i2s_cont:
	iounmap(msp->registers);

err_res:
	devm_kfree(&pdev->dev, msp);

	return ret;
}

void ux500_msp_i2s_cleanup_msp(struct platform_device *pdev,
			struct ux500_msp *msp)
{
	dev_dbg(msp->dev, "%s: Enter (id = %d).\n", __func__, msp->id);

	device_unregister(&msp->i2s_cont->dev);
	devm_kfree(&pdev->dev, msp->i2s_cont);

	iounmap(msp->registers);

	devm_kfree(&pdev->dev, msp);
}

MODULE_LICENSE("GPLv2");
