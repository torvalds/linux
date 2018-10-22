/*
 * Copyright (C) 2014-2015 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "cygnus-ssp.h"

#define DEFAULT_VCO    1354750204

#define CAPTURE_FCI_ID_BASE 0x180
#define CYGNUS_SSP_TRISTATE_MASK 0x001fff
#define CYGNUS_PLLCLKSEL_MASK 0xf

/* Used with stream_on field to indicate which streams are active */
#define  PLAYBACK_STREAM_MASK   BIT(0)
#define  CAPTURE_STREAM_MASK    BIT(1)

#define I2S_STREAM_CFG_MASK      0xff003ff
#define I2S_CAP_STREAM_CFG_MASK  0xf0
#define SPDIF_STREAM_CFG_MASK    0x3ff
#define CH_GRP_STEREO            0x1

/* Begin register offset defines */
#define AUD_MISC_SEROUT_OE_REG_BASE  0x01c
#define AUD_MISC_SEROUT_SPDIF_OE  12
#define AUD_MISC_SEROUT_MCLK_OE   3
#define AUD_MISC_SEROUT_LRCK_OE   2
#define AUD_MISC_SEROUT_SCLK_OE   1
#define AUD_MISC_SEROUT_SDAT_OE   0

/* AUD_FMM_BF_CTRL_xxx regs */
#define BF_DST_CFG0_OFFSET  0x100
#define BF_DST_CFG1_OFFSET  0x104
#define BF_DST_CFG2_OFFSET  0x108

#define BF_DST_CTRL0_OFFSET 0x130
#define BF_DST_CTRL1_OFFSET 0x134
#define BF_DST_CTRL2_OFFSET 0x138

#define BF_SRC_CFG0_OFFSET  0x148
#define BF_SRC_CFG1_OFFSET  0x14c
#define BF_SRC_CFG2_OFFSET  0x150
#define BF_SRC_CFG3_OFFSET  0x154

#define BF_SRC_CTRL0_OFFSET 0x1c0
#define BF_SRC_CTRL1_OFFSET 0x1c4
#define BF_SRC_CTRL2_OFFSET 0x1c8
#define BF_SRC_CTRL3_OFFSET 0x1cc

#define BF_SRC_GRP0_OFFSET  0x1fc
#define BF_SRC_GRP1_OFFSET  0x200
#define BF_SRC_GRP2_OFFSET  0x204
#define BF_SRC_GRP3_OFFSET  0x208

#define BF_SRC_GRP_EN_OFFSET        0x320
#define BF_SRC_GRP_FLOWON_OFFSET    0x324
#define BF_SRC_GRP_SYNC_DIS_OFFSET  0x328

/* AUD_FMM_IOP_OUT_I2S_xxx regs */
#define OUT_I2S_0_STREAM_CFG_OFFSET 0xa00
#define OUT_I2S_0_CFG_OFFSET        0xa04
#define OUT_I2S_0_MCLK_CFG_OFFSET   0xa0c

#define OUT_I2S_1_STREAM_CFG_OFFSET 0xa40
#define OUT_I2S_1_CFG_OFFSET        0xa44
#define OUT_I2S_1_MCLK_CFG_OFFSET   0xa4c

#define OUT_I2S_2_STREAM_CFG_OFFSET 0xa80
#define OUT_I2S_2_CFG_OFFSET        0xa84
#define OUT_I2S_2_MCLK_CFG_OFFSET   0xa8c

/* AUD_FMM_IOP_OUT_SPDIF_xxx regs */
#define SPDIF_STREAM_CFG_OFFSET  0xac0
#define SPDIF_CTRL_OFFSET        0xac4
#define SPDIF_FORMAT_CFG_OFFSET  0xad8
#define SPDIF_MCLK_CFG_OFFSET    0xadc

/* AUD_FMM_IOP_PLL_0_xxx regs */
#define IOP_PLL_0_MACRO_OFFSET    0xb00
#define IOP_PLL_0_MDIV_Ch0_OFFSET 0xb14
#define IOP_PLL_0_MDIV_Ch1_OFFSET 0xb18
#define IOP_PLL_0_MDIV_Ch2_OFFSET 0xb1c

#define IOP_PLL_0_ACTIVE_MDIV_Ch0_OFFSET 0xb30
#define IOP_PLL_0_ACTIVE_MDIV_Ch1_OFFSET 0xb34
#define IOP_PLL_0_ACTIVE_MDIV_Ch2_OFFSET 0xb38

/* AUD_FMM_IOP_xxx regs */
#define IOP_PLL_0_CONTROL_OFFSET     0xb04
#define IOP_PLL_0_USER_NDIV_OFFSET   0xb08
#define IOP_PLL_0_ACTIVE_NDIV_OFFSET 0xb20
#define IOP_PLL_0_RESET_OFFSET       0xb5c

/* AUD_FMM_IOP_IN_I2S_xxx regs */
#define IN_I2S_0_STREAM_CFG_OFFSET 0x00
#define IN_I2S_0_CFG_OFFSET        0x04
#define IN_I2S_1_STREAM_CFG_OFFSET 0x40
#define IN_I2S_1_CFG_OFFSET        0x44
#define IN_I2S_2_STREAM_CFG_OFFSET 0x80
#define IN_I2S_2_CFG_OFFSET        0x84

/* AUD_FMM_IOP_MISC_xxx regs */
#define IOP_SW_INIT_LOGIC          0x1c0

/* End register offset defines */


/* AUD_FMM_IOP_OUT_I2S_x_MCLK_CFG_0_REG */
#define I2S_OUT_MCLKRATE_SHIFT 16

/* AUD_FMM_IOP_OUT_I2S_x_MCLK_CFG_REG */
#define I2S_OUT_PLLCLKSEL_SHIFT  0

/* AUD_FMM_IOP_OUT_I2S_x_STREAM_CFG */
#define I2S_OUT_STREAM_ENA  31
#define I2S_OUT_STREAM_CFG_GROUP_ID  20
#define I2S_OUT_STREAM_CFG_CHANNEL_GROUPING  24

/* AUD_FMM_IOP_IN_I2S_x_CAP */
#define I2S_IN_STREAM_CFG_CAP_ENA   31
#define I2S_IN_STREAM_CFG_0_GROUP_ID 4

/* AUD_FMM_IOP_OUT_I2S_x_I2S_CFG_REG */
#define I2S_OUT_CFGX_CLK_ENA         0
#define I2S_OUT_CFGX_DATA_ENABLE     1
#define I2S_OUT_CFGX_DATA_ALIGNMENT  6
#define I2S_OUT_CFGX_BITS_PER_SLOT  13
#define I2S_OUT_CFGX_VALID_SLOT     14
#define I2S_OUT_CFGX_FSYNC_WIDTH    18
#define I2S_OUT_CFGX_SCLKS_PER_1FS_DIV32 26
#define I2S_OUT_CFGX_SLAVE_MODE     30
#define I2S_OUT_CFGX_TDM_MODE       31

/* AUD_FMM_BF_CTRL_SOURCECH_CFGx_REG */
#define BF_SRC_CFGX_SFIFO_ENA              0
#define BF_SRC_CFGX_BUFFER_PAIR_ENABLE     1
#define BF_SRC_CFGX_SAMPLE_CH_MODE         2
#define BF_SRC_CFGX_SFIFO_SZ_DOUBLE        5
#define BF_SRC_CFGX_NOT_PAUSE_WHEN_EMPTY  10
#define BF_SRC_CFGX_BIT_RES               20
#define BF_SRC_CFGX_PROCESS_SEQ_ID_VALID  31

/* AUD_FMM_BF_CTRL_DESTCH_CFGx_REG */
#define BF_DST_CFGX_CAP_ENA              0
#define BF_DST_CFGX_BUFFER_PAIR_ENABLE   1
#define BF_DST_CFGX_DFIFO_SZ_DOUBLE      2
#define BF_DST_CFGX_NOT_PAUSE_WHEN_FULL 11
#define BF_DST_CFGX_FCI_ID              12
#define BF_DST_CFGX_CAP_MODE            24
#define BF_DST_CFGX_PROC_SEQ_ID_VALID   31

/* AUD_FMM_IOP_OUT_SPDIF_xxx */
#define SPDIF_0_OUT_DITHER_ENA     3
#define SPDIF_0_OUT_STREAM_ENA    31

/* AUD_FMM_IOP_PLL_0_USER */
#define IOP_PLL_0_USER_NDIV_FRAC   10

/* AUD_FMM_IOP_PLL_0_ACTIVE */
#define IOP_PLL_0_ACTIVE_NDIV_FRAC 10


#define INIT_SSP_REGS(num) (struct cygnus_ssp_regs){ \
		.i2s_stream_cfg = OUT_I2S_ ##num## _STREAM_CFG_OFFSET, \
		.i2s_cap_stream_cfg = IN_I2S_ ##num## _STREAM_CFG_OFFSET, \
		.i2s_cfg = OUT_I2S_ ##num## _CFG_OFFSET, \
		.i2s_cap_cfg = IN_I2S_ ##num## _CFG_OFFSET, \
		.i2s_mclk_cfg = OUT_I2S_ ##num## _MCLK_CFG_OFFSET, \
		.bf_destch_ctrl = BF_DST_CTRL ##num## _OFFSET, \
		.bf_destch_cfg = BF_DST_CFG ##num## _OFFSET, \
		.bf_sourcech_ctrl = BF_SRC_CTRL ##num## _OFFSET, \
		.bf_sourcech_cfg = BF_SRC_CFG ##num## _OFFSET, \
		.bf_sourcech_grp = BF_SRC_GRP ##num## _OFFSET \
}

struct pll_macro_entry {
	u32 mclk;
	u32 pll_ch_num;
};

/*
 * PLL has 3 output channels (1x, 2x, and 4x). Below are
 * the common MCLK frequencies used by audio driver
 */
static const struct pll_macro_entry pll_predef_mclk[] = {
	{ 4096000, 0},
	{ 8192000, 1},
	{16384000, 2},

	{ 5644800, 0},
	{11289600, 1},
	{22579200, 2},

	{ 6144000, 0},
	{12288000, 1},
	{24576000, 2},

	{12288000, 0},
	{24576000, 1},
	{49152000, 2},

	{22579200, 0},
	{45158400, 1},
	{90316800, 2},

	{24576000, 0},
	{49152000, 1},
	{98304000, 2},
};

#define CYGNUS_RATE_MIN     8000
#define CYGNUS_RATE_MAX   384000

/* List of valid frame sizes for tdm mode */
static const int ssp_valid_tdm_framesize[] = {32, 64, 128, 256, 512};

static const unsigned int cygnus_rates[] = {
	 8000, 11025,  16000,  22050,  32000,  44100, 48000,
	88200, 96000, 176400, 192000, 352800, 384000
};

static const struct snd_pcm_hw_constraint_list cygnus_rate_constraint = {
	.count = ARRAY_SIZE(cygnus_rates),
	.list = cygnus_rates,
};

static struct cygnus_aio_port *cygnus_dai_get_portinfo(struct snd_soc_dai *dai)
{
	struct cygnus_audio *cygaud = snd_soc_dai_get_drvdata(dai);

	return &cygaud->portinfo[dai->id];
}

static int audio_ssp_init_portregs(struct cygnus_aio_port *aio)
{
	u32 value, fci_id;
	int status = 0;

	switch (aio->port_type) {
	case PORT_TDM:
		value = readl(aio->cygaud->audio + aio->regs.i2s_stream_cfg);
		value &= ~I2S_STREAM_CFG_MASK;

		/* Set Group ID */
		writel(aio->portnum,
			aio->cygaud->audio + aio->regs.bf_sourcech_grp);

		/* Configure the AUD_FMM_IOP_OUT_I2S_x_STREAM_CFG reg */
		value |= aio->portnum << I2S_OUT_STREAM_CFG_GROUP_ID;
		value |= aio->portnum; /* FCI ID is the port num */
		value |= CH_GRP_STEREO << I2S_OUT_STREAM_CFG_CHANNEL_GROUPING;
		writel(value, aio->cygaud->audio + aio->regs.i2s_stream_cfg);

		/* Configure the AUD_FMM_BF_CTRL_SOURCECH_CFGX reg */
		value = readl(aio->cygaud->audio + aio->regs.bf_sourcech_cfg);
		value &= ~BIT(BF_SRC_CFGX_NOT_PAUSE_WHEN_EMPTY);
		value |= BIT(BF_SRC_CFGX_SFIFO_SZ_DOUBLE);
		value |= BIT(BF_SRC_CFGX_PROCESS_SEQ_ID_VALID);
		writel(value, aio->cygaud->audio + aio->regs.bf_sourcech_cfg);

		/* Configure the AUD_FMM_IOP_IN_I2S_x_CAP_STREAM_CFG_0 reg */
		value = readl(aio->cygaud->i2s_in +
			aio->regs.i2s_cap_stream_cfg);
		value &= ~I2S_CAP_STREAM_CFG_MASK;
		value |= aio->portnum << I2S_IN_STREAM_CFG_0_GROUP_ID;
		writel(value, aio->cygaud->i2s_in +
			aio->regs.i2s_cap_stream_cfg);

		/* Configure the AUD_FMM_BF_CTRL_DESTCH_CFGX_REG_BASE reg */
		fci_id = CAPTURE_FCI_ID_BASE + aio->portnum;

		value = readl(aio->cygaud->audio + aio->regs.bf_destch_cfg);
		value |= BIT(BF_DST_CFGX_DFIFO_SZ_DOUBLE);
		value &= ~BIT(BF_DST_CFGX_NOT_PAUSE_WHEN_FULL);
		value |= (fci_id << BF_DST_CFGX_FCI_ID);
		value |= BIT(BF_DST_CFGX_PROC_SEQ_ID_VALID);
		writel(value, aio->cygaud->audio + aio->regs.bf_destch_cfg);

		/* Enable the transmit pin for this port */
		value = readl(aio->cygaud->audio + AUD_MISC_SEROUT_OE_REG_BASE);
		value &= ~BIT((aio->portnum * 4) + AUD_MISC_SEROUT_SDAT_OE);
		writel(value, aio->cygaud->audio + AUD_MISC_SEROUT_OE_REG_BASE);
		break;
	case PORT_SPDIF:
		writel(aio->portnum, aio->cygaud->audio + BF_SRC_GRP3_OFFSET);

		value = readl(aio->cygaud->audio + SPDIF_CTRL_OFFSET);
		value |= BIT(SPDIF_0_OUT_DITHER_ENA);
		writel(value, aio->cygaud->audio + SPDIF_CTRL_OFFSET);

		/* Enable and set the FCI ID for the SPDIF channel */
		value = readl(aio->cygaud->audio + SPDIF_STREAM_CFG_OFFSET);
		value &= ~SPDIF_STREAM_CFG_MASK;
		value |= aio->portnum; /* FCI ID is the port num */
		value |= BIT(SPDIF_0_OUT_STREAM_ENA);
		writel(value, aio->cygaud->audio + SPDIF_STREAM_CFG_OFFSET);

		value = readl(aio->cygaud->audio + aio->regs.bf_sourcech_cfg);
		value &= ~BIT(BF_SRC_CFGX_NOT_PAUSE_WHEN_EMPTY);
		value |= BIT(BF_SRC_CFGX_SFIFO_SZ_DOUBLE);
		value |= BIT(BF_SRC_CFGX_PROCESS_SEQ_ID_VALID);
		writel(value, aio->cygaud->audio + aio->regs.bf_sourcech_cfg);

		/* Enable the spdif output pin */
		value = readl(aio->cygaud->audio + AUD_MISC_SEROUT_OE_REG_BASE);
		value &= ~BIT(AUD_MISC_SEROUT_SPDIF_OE);
		writel(value, aio->cygaud->audio + AUD_MISC_SEROUT_OE_REG_BASE);
		break;
	default:
		dev_err(aio->cygaud->dev, "Port not supported\n");
		status = -EINVAL;
	}

	return status;
}

static void audio_ssp_in_enable(struct cygnus_aio_port *aio)
{
	u32 value;

	value = readl(aio->cygaud->audio + aio->regs.bf_destch_cfg);
	value |= BIT(BF_DST_CFGX_CAP_ENA);
	writel(value, aio->cygaud->audio + aio->regs.bf_destch_cfg);

	writel(0x1, aio->cygaud->audio + aio->regs.bf_destch_ctrl);

	value = readl(aio->cygaud->audio + aio->regs.i2s_cfg);
	value |= BIT(I2S_OUT_CFGX_CLK_ENA);
	value |= BIT(I2S_OUT_CFGX_DATA_ENABLE);
	writel(value, aio->cygaud->audio + aio->regs.i2s_cfg);

	value = readl(aio->cygaud->i2s_in + aio->regs.i2s_cap_stream_cfg);
	value |= BIT(I2S_IN_STREAM_CFG_CAP_ENA);
	writel(value, aio->cygaud->i2s_in + aio->regs.i2s_cap_stream_cfg);

	aio->streams_on |= CAPTURE_STREAM_MASK;
}

static void audio_ssp_in_disable(struct cygnus_aio_port *aio)
{
	u32 value;

	value = readl(aio->cygaud->i2s_in + aio->regs.i2s_cap_stream_cfg);
	value &= ~BIT(I2S_IN_STREAM_CFG_CAP_ENA);
	writel(value, aio->cygaud->i2s_in + aio->regs.i2s_cap_stream_cfg);

	aio->streams_on &= ~CAPTURE_STREAM_MASK;

	/* If both playback and capture are off */
	if (!aio->streams_on) {
		value = readl(aio->cygaud->audio + aio->regs.i2s_cfg);
		value &= ~BIT(I2S_OUT_CFGX_CLK_ENA);
		value &= ~BIT(I2S_OUT_CFGX_DATA_ENABLE);
		writel(value, aio->cygaud->audio + aio->regs.i2s_cfg);
	}

	writel(0x0, aio->cygaud->audio + aio->regs.bf_destch_ctrl);

	value = readl(aio->cygaud->audio + aio->regs.bf_destch_cfg);
	value &= ~BIT(BF_DST_CFGX_CAP_ENA);
	writel(value, aio->cygaud->audio + aio->regs.bf_destch_cfg);
}

static int audio_ssp_out_enable(struct cygnus_aio_port *aio)
{
	u32 value;
	int status = 0;

	switch (aio->port_type) {
	case PORT_TDM:
		value = readl(aio->cygaud->audio + aio->regs.i2s_stream_cfg);
		value |= BIT(I2S_OUT_STREAM_ENA);
		writel(value, aio->cygaud->audio + aio->regs.i2s_stream_cfg);

		writel(1, aio->cygaud->audio + aio->regs.bf_sourcech_ctrl);

		value = readl(aio->cygaud->audio + aio->regs.i2s_cfg);
		value |= BIT(I2S_OUT_CFGX_CLK_ENA);
		value |= BIT(I2S_OUT_CFGX_DATA_ENABLE);
		writel(value, aio->cygaud->audio + aio->regs.i2s_cfg);

		value = readl(aio->cygaud->audio + aio->regs.bf_sourcech_cfg);
		value |= BIT(BF_SRC_CFGX_SFIFO_ENA);
		writel(value, aio->cygaud->audio + aio->regs.bf_sourcech_cfg);

		aio->streams_on |= PLAYBACK_STREAM_MASK;
		break;
	case PORT_SPDIF:
		value = readl(aio->cygaud->audio + SPDIF_FORMAT_CFG_OFFSET);
		value |= 0x3;
		writel(value, aio->cygaud->audio + SPDIF_FORMAT_CFG_OFFSET);

		writel(1, aio->cygaud->audio + aio->regs.bf_sourcech_ctrl);

		value = readl(aio->cygaud->audio + aio->regs.bf_sourcech_cfg);
		value |= BIT(BF_SRC_CFGX_SFIFO_ENA);
		writel(value, aio->cygaud->audio + aio->regs.bf_sourcech_cfg);
		break;
	default:
		dev_err(aio->cygaud->dev,
			"Port not supported %d\n", aio->portnum);
		status = -EINVAL;
	}

	return status;
}

static int audio_ssp_out_disable(struct cygnus_aio_port *aio)
{
	u32 value;
	int status = 0;

	switch (aio->port_type) {
	case PORT_TDM:
		aio->streams_on &= ~PLAYBACK_STREAM_MASK;

		/* If both playback and capture are off */
		if (!aio->streams_on) {
			value = readl(aio->cygaud->audio + aio->regs.i2s_cfg);
			value &= ~BIT(I2S_OUT_CFGX_CLK_ENA);
			value &= ~BIT(I2S_OUT_CFGX_DATA_ENABLE);
			writel(value, aio->cygaud->audio + aio->regs.i2s_cfg);
		}

		/* set group_sync_dis = 1 */
		value = readl(aio->cygaud->audio + BF_SRC_GRP_SYNC_DIS_OFFSET);
		value |= BIT(aio->portnum);
		writel(value, aio->cygaud->audio + BF_SRC_GRP_SYNC_DIS_OFFSET);

		writel(0, aio->cygaud->audio + aio->regs.bf_sourcech_ctrl);

		value = readl(aio->cygaud->audio + aio->regs.bf_sourcech_cfg);
		value &= ~BIT(BF_SRC_CFGX_SFIFO_ENA);
		writel(value, aio->cygaud->audio + aio->regs.bf_sourcech_cfg);

		/* set group_sync_dis = 0 */
		value = readl(aio->cygaud->audio + BF_SRC_GRP_SYNC_DIS_OFFSET);
		value &= ~BIT(aio->portnum);
		writel(value, aio->cygaud->audio + BF_SRC_GRP_SYNC_DIS_OFFSET);

		value = readl(aio->cygaud->audio + aio->regs.i2s_stream_cfg);
		value &= ~BIT(I2S_OUT_STREAM_ENA);
		writel(value, aio->cygaud->audio + aio->regs.i2s_stream_cfg);

		/* IOP SW INIT on OUT_I2S_x */
		value = readl(aio->cygaud->i2s_in + IOP_SW_INIT_LOGIC);
		value |= BIT(aio->portnum);
		writel(value, aio->cygaud->i2s_in + IOP_SW_INIT_LOGIC);
		value &= ~BIT(aio->portnum);
		writel(value, aio->cygaud->i2s_in + IOP_SW_INIT_LOGIC);
		break;
	case PORT_SPDIF:
		value = readl(aio->cygaud->audio + SPDIF_FORMAT_CFG_OFFSET);
		value &= ~0x3;
		writel(value, aio->cygaud->audio + SPDIF_FORMAT_CFG_OFFSET);
		writel(0, aio->cygaud->audio + aio->regs.bf_sourcech_ctrl);

		value = readl(aio->cygaud->audio + aio->regs.bf_sourcech_cfg);
		value &= ~BIT(BF_SRC_CFGX_SFIFO_ENA);
		writel(value, aio->cygaud->audio + aio->regs.bf_sourcech_cfg);
		break;
	default:
		dev_err(aio->cygaud->dev,
			"Port not supported %d\n", aio->portnum);
		status = -EINVAL;
	}

	return status;
}

static int pll_configure_mclk(struct cygnus_audio *cygaud, u32 mclk,
	struct cygnus_aio_port *aio)
{
	int i = 0, error;
	bool found = false;
	const struct pll_macro_entry *p_entry;
	struct clk *ch_clk;

	for (i = 0; i < ARRAY_SIZE(pll_predef_mclk); i++) {
		p_entry = &pll_predef_mclk[i];
		if (p_entry->mclk == mclk) {
			found = true;
			break;
		}
	}
	if (!found) {
		dev_err(cygaud->dev,
			"%s No valid mclk freq (%u) found!\n", __func__, mclk);
		return -EINVAL;
	}

	ch_clk = cygaud->audio_clk[p_entry->pll_ch_num];

	if ((aio->clk_trace.cap_en) && (!aio->clk_trace.cap_clk_en)) {
		error = clk_prepare_enable(ch_clk);
		if (error) {
			dev_err(cygaud->dev, "%s clk_prepare_enable failed %d\n",
				__func__, error);
			return error;
		}
		aio->clk_trace.cap_clk_en = true;
	}

	if ((aio->clk_trace.play_en) && (!aio->clk_trace.play_clk_en)) {
		error = clk_prepare_enable(ch_clk);
		if (error) {
			dev_err(cygaud->dev, "%s clk_prepare_enable failed %d\n",
				__func__, error);
			return error;
		}
		aio->clk_trace.play_clk_en = true;
	}

	error = clk_set_rate(ch_clk, mclk);
	if (error) {
		dev_err(cygaud->dev, "%s Set MCLK rate failed: %d\n",
			__func__, error);
		return error;
	}

	return p_entry->pll_ch_num;
}

static int cygnus_ssp_set_clocks(struct cygnus_aio_port *aio)
{
	u32 value;
	u32 mask = 0xf;
	u32 sclk;
	u32 mclk_rate;
	unsigned int bit_rate;
	unsigned int ratio;

	bit_rate = aio->bit_per_frame * aio->lrclk;

	/*
	 * Check if the bit clock can be generated from the given MCLK.
	 * MCLK must be a perfect multiple of bit clock and must be one of the
	 * following values... (2,4,6,8,10,12,14)
	 */
	if ((aio->mclk % bit_rate) != 0)
		return -EINVAL;

	ratio = aio->mclk / bit_rate;
	switch (ratio) {
	case 2:
	case 4:
	case 6:
	case 8:
	case 10:
	case 12:
	case 14:
		mclk_rate = ratio / 2;
		break;

	default:
		dev_err(aio->cygaud->dev,
			"Invalid combination of MCLK and BCLK\n");
		dev_err(aio->cygaud->dev, "lrclk = %u, bits/frame = %u, mclk = %u\n",
			aio->lrclk, aio->bit_per_frame, aio->mclk);
		return -EINVAL;
	}

	/* Set sclk rate */
	switch (aio->port_type) {
	case PORT_TDM:
		sclk = aio->bit_per_frame;
		if (sclk == 512)
			sclk = 0;

		/* sclks_per_1fs_div = sclk cycles/32 */
		sclk /= 32;

		/* Set number of bitclks per frame */
		value = readl(aio->cygaud->audio + aio->regs.i2s_cfg);
		value &= ~(mask << I2S_OUT_CFGX_SCLKS_PER_1FS_DIV32);
		value |= sclk << I2S_OUT_CFGX_SCLKS_PER_1FS_DIV32;
		writel(value, aio->cygaud->audio + aio->regs.i2s_cfg);
		dev_dbg(aio->cygaud->dev,
			"SCLKS_PER_1FS_DIV32 = 0x%x\n", value);
		break;
	case PORT_SPDIF:
		break;
	default:
		dev_err(aio->cygaud->dev, "Unknown port type\n");
		return -EINVAL;
	}

	/* Set MCLK_RATE ssp port (spdif and ssp are the same) */
	value = readl(aio->cygaud->audio + aio->regs.i2s_mclk_cfg);
	value &= ~(0xf << I2S_OUT_MCLKRATE_SHIFT);
	value |= (mclk_rate << I2S_OUT_MCLKRATE_SHIFT);
	writel(value, aio->cygaud->audio + aio->regs.i2s_mclk_cfg);

	dev_dbg(aio->cygaud->dev, "mclk cfg reg = 0x%x\n", value);
	dev_dbg(aio->cygaud->dev, "bits per frame = %u, mclk = %u Hz, lrclk = %u Hz\n",
			aio->bit_per_frame, aio->mclk, aio->lrclk);
	return 0;
}

static int cygnus_ssp_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct cygnus_aio_port *aio = cygnus_dai_get_portinfo(dai);
	int rate, bitres;
	u32 value;
	u32 mask = 0x1f;
	int ret = 0;

	dev_dbg(aio->cygaud->dev, "%s port = %d\n", __func__, aio->portnum);
	dev_dbg(aio->cygaud->dev, "params_channels %d\n",
			params_channels(params));
	dev_dbg(aio->cygaud->dev, "rate %d\n", params_rate(params));
	dev_dbg(aio->cygaud->dev, "format %d\n", params_format(params));

	rate = params_rate(params);

	switch (aio->mode) {
	case CYGNUS_SSPMODE_TDM:
		if ((rate == 192000) && (params_channels(params) > 4)) {
			dev_err(aio->cygaud->dev, "Cannot run %d channels at %dHz\n",
				params_channels(params), rate);
			return -EINVAL;
		}
		break;
	case CYGNUS_SSPMODE_I2S:
		aio->bit_per_frame = 64; /* I2S must be 64 bit per frame */
		break;
	default:
		dev_err(aio->cygaud->dev,
			"%s port running in unknown mode\n", __func__);
		return -EINVAL;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		value = readl(aio->cygaud->audio + aio->regs.bf_sourcech_cfg);
		value &= ~BIT(BF_SRC_CFGX_BUFFER_PAIR_ENABLE);
		value &= ~BIT(BF_SRC_CFGX_SAMPLE_CH_MODE);
		writel(value, aio->cygaud->audio + aio->regs.bf_sourcech_cfg);

		switch (params_format(params)) {
		case SNDRV_PCM_FORMAT_S16_LE:
			bitres = 16;
			break;

		case SNDRV_PCM_FORMAT_S32_LE:
			/* 32 bit mode is coded as 0 */
			bitres = 0;
			break;

		default:
			return -EINVAL;
		}

		value = readl(aio->cygaud->audio + aio->regs.bf_sourcech_cfg);
		value &= ~(mask << BF_SRC_CFGX_BIT_RES);
		value |= (bitres << BF_SRC_CFGX_BIT_RES);
		writel(value, aio->cygaud->audio + aio->regs.bf_sourcech_cfg);

	} else {

		switch (params_format(params)) {
		case SNDRV_PCM_FORMAT_S16_LE:
			value = readl(aio->cygaud->audio +
					aio->regs.bf_destch_cfg);
			value |= BIT(BF_DST_CFGX_CAP_MODE);
			writel(value, aio->cygaud->audio +
					aio->regs.bf_destch_cfg);
			break;

		case SNDRV_PCM_FORMAT_S32_LE:
			value = readl(aio->cygaud->audio +
					aio->regs.bf_destch_cfg);
			value &= ~BIT(BF_DST_CFGX_CAP_MODE);
			writel(value, aio->cygaud->audio +
					aio->regs.bf_destch_cfg);
			break;

		default:
			return -EINVAL;
		}
	}

	aio->lrclk = rate;

	if (!aio->is_slave)
		ret = cygnus_ssp_set_clocks(aio);

	return ret;
}

/*
 * This function sets the mclk frequency for pll clock
 */
static int cygnus_ssp_set_sysclk(struct snd_soc_dai *dai,
			int clk_id, unsigned int freq, int dir)
{
	int sel;
	u32 value;
	struct cygnus_aio_port *aio = cygnus_dai_get_portinfo(dai);
	struct cygnus_audio *cygaud = snd_soc_dai_get_drvdata(dai);

	dev_dbg(aio->cygaud->dev,
		"%s Enter port = %d\n", __func__, aio->portnum);
	sel = pll_configure_mclk(cygaud, freq, aio);
	if (sel < 0) {
		dev_err(aio->cygaud->dev,
			"%s Setting mclk failed.\n", __func__);
		return -EINVAL;
	}

	aio->mclk = freq;

	dev_dbg(aio->cygaud->dev, "%s Setting MCLKSEL to %d\n", __func__, sel);
	value = readl(aio->cygaud->audio + aio->regs.i2s_mclk_cfg);
	value &= ~(0xf << I2S_OUT_PLLCLKSEL_SHIFT);
	value |= (sel << I2S_OUT_PLLCLKSEL_SHIFT);
	writel(value, aio->cygaud->audio + aio->regs.i2s_mclk_cfg);

	return 0;
}

static int cygnus_ssp_startup(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct cygnus_aio_port *aio = cygnus_dai_get_portinfo(dai);

	snd_soc_dai_set_dma_data(dai, substream, aio);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		aio->clk_trace.play_en = true;
	else
		aio->clk_trace.cap_en = true;

	substream->runtime->hw.rate_min = CYGNUS_RATE_MIN;
	substream->runtime->hw.rate_max = CYGNUS_RATE_MAX;

	snd_pcm_hw_constraint_list(substream->runtime, 0,
			SNDRV_PCM_HW_PARAM_RATE, &cygnus_rate_constraint);
	return 0;
}

static void cygnus_ssp_shutdown(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct cygnus_aio_port *aio = cygnus_dai_get_portinfo(dai);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		aio->clk_trace.play_en = false;
	else
		aio->clk_trace.cap_en = false;

	if (!aio->is_slave) {
		u32 val;

		val = readl(aio->cygaud->audio + aio->regs.i2s_mclk_cfg);
		val &= CYGNUS_PLLCLKSEL_MASK;
		if (val >= ARRAY_SIZE(aio->cygaud->audio_clk)) {
			dev_err(aio->cygaud->dev, "Clk index %u is out of bounds\n",
				val);
			return;
		}

		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			if (aio->clk_trace.play_clk_en) {
				clk_disable_unprepare(aio->cygaud->
						audio_clk[val]);
				aio->clk_trace.play_clk_en = false;
			}
		} else {
			if (aio->clk_trace.cap_clk_en) {
				clk_disable_unprepare(aio->cygaud->
						audio_clk[val]);
				aio->clk_trace.cap_clk_en = false;
			}
		}
	}
}

/*
 * Bit    Update  Notes
 * 31     Yes     TDM Mode        (1 = TDM, 0 = i2s)
 * 30     Yes     Slave Mode	  (1 = Slave, 0 = Master)
 * 29:26  No      Sclks per frame
 * 25:18  Yes     FS Width
 * 17:14  No      Valid Slots
 * 13     No      Bits		  (1 = 16 bits, 0 = 32 bits)
 * 12:08  No     Bits per samp
 * 07     Yes     Justifcation    (1 = LSB, 0 = MSB)
 * 06     Yes     Alignment       (1 = Delay 1 clk, 0 = no delay
 * 05     Yes     SCLK polarity   (1 = Rising, 0 = Falling)
 * 04     Yes     LRCLK Polarity  (1 = High for left, 0 = Low for left)
 * 03:02  Yes     Reserved - write as zero
 * 01     No      Data Enable
 * 00     No      CLK Enable
 */
#define I2S_OUT_CFG_REG_UPDATE_MASK   0x3C03FF03

/* Input cfg is same as output, but the FS width is not a valid field */
#define I2S_IN_CFG_REG_UPDATE_MASK  (I2S_OUT_CFG_REG_UPDATE_MASK | 0x03FC0000)

int cygnus_ssp_set_custom_fsync_width(struct snd_soc_dai *cpu_dai, int len)
{
	struct cygnus_aio_port *aio = cygnus_dai_get_portinfo(cpu_dai);

	if ((len > 0) && (len < 256)) {
		aio->fsync_width = len;
		return 0;
	} else {
		return -EINVAL;
	}
}
EXPORT_SYMBOL_GPL(cygnus_ssp_set_custom_fsync_width);

static int cygnus_ssp_set_fmt(struct snd_soc_dai *cpu_dai, unsigned int fmt)
{
	struct cygnus_aio_port *aio = cygnus_dai_get_portinfo(cpu_dai);
	u32 ssp_curcfg;
	u32 ssp_newcfg;
	u32 ssp_outcfg;
	u32 ssp_incfg;
	u32 val;
	u32 mask;

	dev_dbg(aio->cygaud->dev, "%s Enter  fmt: %x\n", __func__, fmt);

	if (aio->port_type == PORT_SPDIF)
		return -EINVAL;

	ssp_newcfg = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		ssp_newcfg |= BIT(I2S_OUT_CFGX_SLAVE_MODE);
		aio->is_slave = 1;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		ssp_newcfg &= ~BIT(I2S_OUT_CFGX_SLAVE_MODE);
		aio->is_slave = 0;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		ssp_newcfg |= BIT(I2S_OUT_CFGX_DATA_ALIGNMENT);
		ssp_newcfg |= BIT(I2S_OUT_CFGX_FSYNC_WIDTH);
		aio->mode = CYGNUS_SSPMODE_I2S;
		break;

	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		ssp_newcfg |= BIT(I2S_OUT_CFGX_TDM_MODE);

		/* DSP_A = data after FS, DSP_B = data during FS */
		if ((fmt & SND_SOC_DAIFMT_FORMAT_MASK) == SND_SOC_DAIFMT_DSP_A)
			ssp_newcfg |= BIT(I2S_OUT_CFGX_DATA_ALIGNMENT);

		if ((aio->fsync_width > 0) && (aio->fsync_width < 256))
			ssp_newcfg |=
				(aio->fsync_width << I2S_OUT_CFGX_FSYNC_WIDTH);
		else
			ssp_newcfg |= BIT(I2S_OUT_CFGX_FSYNC_WIDTH);

		aio->mode = CYGNUS_SSPMODE_TDM;
		break;

	default:
		return -EINVAL;
	}

	/*
	 * SSP out cfg.
	 * Retain bits we do not want to update, then OR in new bits
	 */
	ssp_curcfg = readl(aio->cygaud->audio + aio->regs.i2s_cfg);
	ssp_outcfg = (ssp_curcfg & I2S_OUT_CFG_REG_UPDATE_MASK) | ssp_newcfg;
	writel(ssp_outcfg, aio->cygaud->audio + aio->regs.i2s_cfg);

	/*
	 * SSP in cfg.
	 * Retain bits we do not want to update, then OR in new bits
	 */
	ssp_curcfg = readl(aio->cygaud->i2s_in + aio->regs.i2s_cap_cfg);
	ssp_incfg = (ssp_curcfg & I2S_IN_CFG_REG_UPDATE_MASK) | ssp_newcfg;
	writel(ssp_incfg, aio->cygaud->i2s_in + aio->regs.i2s_cap_cfg);

	val = readl(aio->cygaud->audio + AUD_MISC_SEROUT_OE_REG_BASE);

	/*
	 * Configure the word clk and bit clk as output or tristate
	 * Each port has 4 bits for controlling its pins.
	 * Shift the mask based upon port number.
	 */
	mask = BIT(AUD_MISC_SEROUT_LRCK_OE)
			| BIT(AUD_MISC_SEROUT_SCLK_OE)
			| BIT(AUD_MISC_SEROUT_MCLK_OE);
	mask = mask << (aio->portnum * 4);
	if (aio->is_slave)
		/* Set bit for tri-state */
		val |= mask;
	else
		/* Clear bit for drive */
		val &= ~mask;

	dev_dbg(aio->cygaud->dev, "%s  Set OE bits 0x%x\n", __func__, val);
	writel(val, aio->cygaud->audio + AUD_MISC_SEROUT_OE_REG_BASE);

	return 0;
}

static int cygnus_ssp_trigger(struct snd_pcm_substream *substream, int cmd,
			       struct snd_soc_dai *dai)
{
	struct cygnus_aio_port *aio = cygnus_dai_get_portinfo(dai);
	struct cygnus_audio *cygaud = snd_soc_dai_get_drvdata(dai);

	dev_dbg(aio->cygaud->dev,
		"%s cmd %d at port = %d\n", __func__, cmd, aio->portnum);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			audio_ssp_out_enable(aio);
		else
			audio_ssp_in_enable(aio);
		cygaud->active_ports++;

		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			audio_ssp_out_disable(aio);
		else
			audio_ssp_in_disable(aio);
		cygaud->active_ports--;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int cygnus_set_dai_tdm_slot(struct snd_soc_dai *cpu_dai,
	unsigned int tx_mask, unsigned int rx_mask, int slots, int slot_width)
{
	struct cygnus_aio_port *aio = cygnus_dai_get_portinfo(cpu_dai);
	u32 value;
	int bits_per_slot = 0;     /* default to 32-bits per slot */
	int frame_bits;
	unsigned int active_slots;
	bool found = false;
	int i;

	if (tx_mask != rx_mask) {
		dev_err(aio->cygaud->dev,
			"%s tx_mask must equal rx_mask\n", __func__);
		return -EINVAL;
	}

	active_slots = hweight32(tx_mask);

	if (active_slots > 16)
		return -EINVAL;

	/* Slot value must be even */
	if (active_slots % 2)
		return -EINVAL;

	/* We encode 16 slots as 0 in the reg */
	if (active_slots == 16)
		active_slots = 0;

	/* Slot Width is either 16 or 32 */
	switch (slot_width) {
	case 16:
		bits_per_slot = 1;
		break;
	case 32:
		bits_per_slot = 0;
		break;
	default:
		bits_per_slot = 0;
		dev_warn(aio->cygaud->dev,
			"%s Defaulting Slot Width to 32\n", __func__);
	}

	frame_bits = slots * slot_width;

	for (i = 0; i < ARRAY_SIZE(ssp_valid_tdm_framesize); i++) {
		if (ssp_valid_tdm_framesize[i] == frame_bits) {
			found = true;
			break;
		}
	}

	if (!found) {
		dev_err(aio->cygaud->dev,
			"%s In TDM mode, frame bits INVALID (%d)\n",
			__func__, frame_bits);
		return -EINVAL;
	}

	aio->bit_per_frame = frame_bits;

	dev_dbg(aio->cygaud->dev, "%s active_slots %u, bits per frame %d\n",
			__func__, active_slots, frame_bits);

	/* Set capture side of ssp port */
	value = readl(aio->cygaud->i2s_in + aio->regs.i2s_cap_cfg);
	value &= ~(0xf << I2S_OUT_CFGX_VALID_SLOT);
	value |= (active_slots << I2S_OUT_CFGX_VALID_SLOT);
	value &= ~BIT(I2S_OUT_CFGX_BITS_PER_SLOT);
	value |= (bits_per_slot << I2S_OUT_CFGX_BITS_PER_SLOT);
	writel(value, aio->cygaud->i2s_in + aio->regs.i2s_cap_cfg);

	/* Set playback side of ssp port */
	value = readl(aio->cygaud->audio + aio->regs.i2s_cfg);
	value &= ~(0xf << I2S_OUT_CFGX_VALID_SLOT);
	value |= (active_slots << I2S_OUT_CFGX_VALID_SLOT);
	value &= ~BIT(I2S_OUT_CFGX_BITS_PER_SLOT);
	value |= (bits_per_slot << I2S_OUT_CFGX_BITS_PER_SLOT);
	writel(value, aio->cygaud->audio + aio->regs.i2s_cfg);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int cygnus_ssp_suspend(struct snd_soc_dai *cpu_dai)
{
	struct cygnus_aio_port *aio = cygnus_dai_get_portinfo(cpu_dai);

	if (!aio->is_slave) {
		u32 val;

		val = readl(aio->cygaud->audio + aio->regs.i2s_mclk_cfg);
		val &= CYGNUS_PLLCLKSEL_MASK;
		if (val >= ARRAY_SIZE(aio->cygaud->audio_clk)) {
			dev_err(aio->cygaud->dev, "Clk index %u is out of bounds\n",
				val);
			return -EINVAL;
		}

		if (aio->clk_trace.cap_clk_en)
			clk_disable_unprepare(aio->cygaud->audio_clk[val]);
		if (aio->clk_trace.play_clk_en)
			clk_disable_unprepare(aio->cygaud->audio_clk[val]);

		aio->pll_clk_num = val;
	}

	return 0;
}

static int cygnus_ssp_resume(struct snd_soc_dai *cpu_dai)
{
	struct cygnus_aio_port *aio = cygnus_dai_get_portinfo(cpu_dai);
	int error;

	if (!aio->is_slave) {
		if (aio->clk_trace.cap_clk_en) {
			error = clk_prepare_enable(aio->cygaud->
					audio_clk[aio->pll_clk_num]);
			if (error) {
				dev_err(aio->cygaud->dev, "%s clk_prepare_enable failed\n",
					__func__);
				return -EINVAL;
			}
		}
		if (aio->clk_trace.play_clk_en) {
			error = clk_prepare_enable(aio->cygaud->
					audio_clk[aio->pll_clk_num]);
			if (error) {
				if (aio->clk_trace.cap_clk_en)
					clk_disable_unprepare(aio->cygaud->
						audio_clk[aio->pll_clk_num]);
				dev_err(aio->cygaud->dev, "%s clk_prepare_enable failed\n",
					__func__);
				return -EINVAL;
			}
		}
	}

	return 0;
}
#else
#define cygnus_ssp_suspend NULL
#define cygnus_ssp_resume  NULL
#endif

static const struct snd_soc_dai_ops cygnus_ssp_dai_ops = {
	.startup	= cygnus_ssp_startup,
	.shutdown	= cygnus_ssp_shutdown,
	.trigger	= cygnus_ssp_trigger,
	.hw_params	= cygnus_ssp_hw_params,
	.set_fmt	= cygnus_ssp_set_fmt,
	.set_sysclk	= cygnus_ssp_set_sysclk,
	.set_tdm_slot	= cygnus_set_dai_tdm_slot,
};

static const struct snd_soc_dai_ops cygnus_spdif_dai_ops = {
	.startup	= cygnus_ssp_startup,
	.shutdown	= cygnus_ssp_shutdown,
	.trigger	= cygnus_ssp_trigger,
	.hw_params	= cygnus_ssp_hw_params,
	.set_sysclk	= cygnus_ssp_set_sysclk,
};

#define INIT_CPU_DAI(num) { \
	.name = "cygnus-ssp" #num, \
	.playback = { \
		.channels_min = 2, \
		.channels_max = 16, \
		.rates = SNDRV_PCM_RATE_KNOT, \
		.formats = SNDRV_PCM_FMTBIT_S16_LE | \
				SNDRV_PCM_FMTBIT_S32_LE, \
	}, \
	.capture = { \
		.channels_min = 2, \
		.channels_max = 16, \
		.rates = SNDRV_PCM_RATE_KNOT, \
		.formats =  SNDRV_PCM_FMTBIT_S16_LE | \
				SNDRV_PCM_FMTBIT_S32_LE, \
	}, \
	.ops = &cygnus_ssp_dai_ops, \
	.suspend = cygnus_ssp_suspend, \
	.resume = cygnus_ssp_resume, \
}

static const struct snd_soc_dai_driver cygnus_ssp_dai_info[] = {
	INIT_CPU_DAI(0),
	INIT_CPU_DAI(1),
	INIT_CPU_DAI(2),
};

static const struct snd_soc_dai_driver cygnus_spdif_dai_info = {
	.name = "cygnus-spdif",
	.playback = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_KNOT,
		.formats = SNDRV_PCM_FMTBIT_S16_LE |
			SNDRV_PCM_FMTBIT_S32_LE,
	},
	.ops = &cygnus_spdif_dai_ops,
	.suspend = cygnus_ssp_suspend,
	.resume = cygnus_ssp_resume,
};

static struct snd_soc_dai_driver cygnus_ssp_dai[CYGNUS_MAX_PORTS];

static const struct snd_soc_component_driver cygnus_ssp_component = {
	.name		= "cygnus-audio",
};

/*
 * Return < 0 if error
 * Return 0 if disabled
 * Return 1 if enabled and node is parsed successfully
 */
static int parse_ssp_child_node(struct platform_device *pdev,
				struct device_node *dn,
				struct cygnus_audio *cygaud,
				struct snd_soc_dai_driver *p_dai)
{
	struct cygnus_aio_port *aio;
	struct cygnus_ssp_regs ssp_regs[3];
	u32 rawval;
	int portnum = -1;
	enum cygnus_audio_port_type port_type;

	if (of_property_read_u32(dn, "reg", &rawval)) {
		dev_err(&pdev->dev, "Missing reg property\n");
		return -EINVAL;
	}

	portnum = rawval;
	switch (rawval) {
	case 0:
		ssp_regs[0] = INIT_SSP_REGS(0);
		port_type = PORT_TDM;
		break;
	case 1:
		ssp_regs[1] = INIT_SSP_REGS(1);
		port_type = PORT_TDM;
		break;
	case 2:
		ssp_regs[2] = INIT_SSP_REGS(2);
		port_type = PORT_TDM;
		break;
	case 3:
		port_type = PORT_SPDIF;
		break;
	default:
		dev_err(&pdev->dev, "Bad value for reg %u\n", rawval);
		return -EINVAL;
	}

	aio = &cygaud->portinfo[portnum];
	aio->cygaud = cygaud;
	aio->portnum = portnum;
	aio->port_type = port_type;
	aio->fsync_width = -1;

	switch (port_type) {
	case PORT_TDM:
		aio->regs = ssp_regs[portnum];
		*p_dai = cygnus_ssp_dai_info[portnum];
		aio->mode = CYGNUS_SSPMODE_UNKNOWN;
		break;

	case PORT_SPDIF:
		aio->regs.bf_sourcech_cfg = BF_SRC_CFG3_OFFSET;
		aio->regs.bf_sourcech_ctrl = BF_SRC_CTRL3_OFFSET;
		aio->regs.i2s_mclk_cfg = SPDIF_MCLK_CFG_OFFSET;
		aio->regs.i2s_stream_cfg = SPDIF_STREAM_CFG_OFFSET;
		*p_dai = cygnus_spdif_dai_info;

		/* For the purposes of this code SPDIF can be I2S mode */
		aio->mode = CYGNUS_SSPMODE_I2S;
		break;
	default:
		dev_err(&pdev->dev, "Bad value for port_type %d\n", port_type);
		return -EINVAL;
	}

	dev_dbg(&pdev->dev, "%s portnum = %d\n", __func__, aio->portnum);
	aio->streams_on = 0;
	aio->cygaud->dev = &pdev->dev;
	aio->clk_trace.play_en = false;
	aio->clk_trace.cap_en = false;

	audio_ssp_init_portregs(aio);
	return 0;
}

static int audio_clk_init(struct platform_device *pdev,
						struct cygnus_audio *cygaud)
{
	int i;
	char clk_name[PROP_LEN_MAX];

	for (i = 0; i < ARRAY_SIZE(cygaud->audio_clk); i++) {
		snprintf(clk_name, PROP_LEN_MAX, "ch%d_audio", i);

		cygaud->audio_clk[i] = devm_clk_get(&pdev->dev, clk_name);
		if (IS_ERR(cygaud->audio_clk[i]))
			return PTR_ERR(cygaud->audio_clk[i]);
	}

	return 0;
}

static int cygnus_ssp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *child_node;
	struct resource *res;
	struct cygnus_audio *cygaud;
	int err = -EINVAL;
	int node_count;
	int active_port_count;

	cygaud = devm_kzalloc(dev, sizeof(struct cygnus_audio), GFP_KERNEL);
	if (!cygaud)
		return -ENOMEM;

	dev_set_drvdata(dev, cygaud);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "aud");
	cygaud->audio = devm_ioremap_resource(dev, res);
	if (IS_ERR(cygaud->audio))
		return PTR_ERR(cygaud->audio);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "i2s_in");
	cygaud->i2s_in = devm_ioremap_resource(dev, res);
	if (IS_ERR(cygaud->i2s_in))
		return PTR_ERR(cygaud->i2s_in);

	/* Tri-state all controlable pins until we know that we need them */
	writel(CYGNUS_SSP_TRISTATE_MASK,
			cygaud->audio + AUD_MISC_SEROUT_OE_REG_BASE);

	node_count = of_get_child_count(pdev->dev.of_node);
	if ((node_count < 1) || (node_count > CYGNUS_MAX_PORTS)) {
		dev_err(dev, "child nodes is %d.  Must be between 1 and %d\n",
			node_count, CYGNUS_MAX_PORTS);
		return -EINVAL;
	}

	active_port_count = 0;

	for_each_available_child_of_node(pdev->dev.of_node, child_node) {
		err = parse_ssp_child_node(pdev, child_node, cygaud,
					&cygnus_ssp_dai[active_port_count]);

		/* negative is err, 0 is active and good, 1 is disabled */
		if (err < 0)
			return err;
		else if (!err) {
			dev_dbg(dev, "Activating DAI: %s\n",
				cygnus_ssp_dai[active_port_count].name);
			active_port_count++;
		}
	}

	cygaud->dev = dev;
	cygaud->active_ports = 0;

	dev_dbg(dev, "Registering %d DAIs\n", active_port_count);
	err = devm_snd_soc_register_component(dev, &cygnus_ssp_component,
				cygnus_ssp_dai, active_port_count);
	if (err) {
		dev_err(dev, "snd_soc_register_dai failed\n");
		return err;
	}

	cygaud->irq_num = platform_get_irq(pdev, 0);
	if (cygaud->irq_num <= 0) {
		dev_err(dev, "platform_get_irq failed\n");
		err = cygaud->irq_num;
		return err;
	}

	err = audio_clk_init(pdev, cygaud);
	if (err) {
		dev_err(dev, "audio clock initialization failed\n");
		return err;
	}

	err = cygnus_soc_platform_register(dev, cygaud);
	if (err) {
		dev_err(dev, "platform reg error %d\n", err);
		return err;
	}

	return 0;
}

static int cygnus_ssp_remove(struct platform_device *pdev)
{
	cygnus_soc_platform_unregister(&pdev->dev);

	return 0;
}

static const struct of_device_id cygnus_ssp_of_match[] = {
	{ .compatible = "brcm,cygnus-audio" },
	{},
};
MODULE_DEVICE_TABLE(of, cygnus_ssp_of_match);

static struct platform_driver cygnus_ssp_driver = {
	.probe		= cygnus_ssp_probe,
	.remove		= cygnus_ssp_remove,
	.driver		= {
		.name	= "cygnus-ssp",
		.of_match_table = cygnus_ssp_of_match,
	},
};

module_platform_driver(cygnus_ssp_driver);

MODULE_ALIAS("platform:cygnus-ssp");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Broadcom");
MODULE_DESCRIPTION("Cygnus ASoC SSP Interface");
