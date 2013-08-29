/*
 * Freescale S/PDIF ALSA SoC Digital Audio Interface (DAI) driver
 *
 * Copyright (C) 2013 Freescale Semiconductor, Inc.
 *
 * Based on stmp3xxx_spdif_dai.c
 * Vladimir Barinov <vbarinov@embeddedalley.com>
 * Copyright 2008 SigmaTel, Inc
 * Copyright 2008 Embedded Alley Solutions, Inc
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program  is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/clk.h>
#include <linux/clk-private.h>
#include <linux/bitrev.h>
#include <linux/regmap.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>

#include <sound/asoundef.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

#include "fsl_spdif.h"
#include "imx-pcm.h"

#define FSL_SPDIF_TXFIFO_WML	0x8
#define FSL_SPDIF_RXFIFO_WML	0x8

#define INTR_FOR_PLAYBACK (INT_TXFIFO_RESYNC)
#define INTR_FOR_CAPTURE (INT_SYM_ERR | INT_BIT_ERR | INT_URX_FUL | INT_URX_OV|\
		INT_QRX_FUL | INT_QRX_OV | INT_UQ_SYNC | INT_UQ_ERR |\
		INT_RXFIFO_RESYNC | INT_LOSS_LOCK | INT_DPLL_LOCKED)

/* Index list for the values that has if (DPLL Locked) condition */
static u8 srpc_dpll_locked[] = { 0x0, 0x1, 0x2, 0x3, 0x4, 0xa, 0xb };
#define SRPC_NODPLL_START1	0x5
#define SRPC_NODPLL_START2	0xc

#define DEFAULT_RXCLK_SRC	1

/*
 * SPDIF control structure
 * Defines channel status, subcode and Q sub
 */
struct spdif_mixer_control {
	/* spinlock to access control data */
	spinlock_t ctl_lock;

	/* IEC958 channel tx status bit */
	unsigned char ch_status[4];

	/* User bits */
	unsigned char subcode[2 * SPDIF_UBITS_SIZE];

	/* Q subcode part of user bits */
	unsigned char qsub[2 * SPDIF_QSUB_SIZE];

	/* Buffer offset for U/Q */
	u32 upos;
	u32 qpos;

	/* Ready buffer index of the two buffers */
	u32 ready_buf;
};

struct fsl_spdif_priv {
	struct spdif_mixer_control fsl_spdif_control;
	struct snd_soc_dai_driver cpu_dai_drv;
	struct platform_device *pdev;
	struct regmap *regmap;
	bool dpll_locked;
	u8 txclk_div[SPDIF_TXRATE_MAX];
	u8 txclk_src[SPDIF_TXRATE_MAX];
	u8 rxclk_src;
	struct clk *txclk[SPDIF_TXRATE_MAX];
	struct clk *rxclk;
	struct snd_dmaengine_dai_dma_data dma_params_tx;
	struct snd_dmaengine_dai_dma_data dma_params_rx;

	/* The name space will be allocated dynamically */
	char name[0];
};


/* DPLL locked and lock loss interrupt handler */
static void spdif_irq_dpll_lock(struct fsl_spdif_priv *spdif_priv)
{
	struct regmap *regmap = spdif_priv->regmap;
	struct platform_device *pdev = spdif_priv->pdev;
	u32 locked;

	regmap_read(regmap, REG_SPDIF_SRPC, &locked);
	locked &= SRPC_DPLL_LOCKED;

	dev_dbg(&pdev->dev, "isr: Rx dpll %s \n",
			locked ? "locked" : "loss lock");

	spdif_priv->dpll_locked = locked ? true : false;
}

/* Receiver found illegal symbol interrupt handler */
static void spdif_irq_sym_error(struct fsl_spdif_priv *spdif_priv)
{
	struct regmap *regmap = spdif_priv->regmap;
	struct platform_device *pdev = spdif_priv->pdev;

	dev_dbg(&pdev->dev, "isr: receiver found illegal symbol\n");

	if (!spdif_priv->dpll_locked) {
		/* DPLL unlocked seems no audio stream */
		regmap_update_bits(regmap, REG_SPDIF_SIE, INT_SYM_ERR, 0);
	}
}

/* U/Q Channel receive register full */
static void spdif_irq_uqrx_full(struct fsl_spdif_priv *spdif_priv, char name)
{
	struct spdif_mixer_control *ctrl = &spdif_priv->fsl_spdif_control;
	struct regmap *regmap = spdif_priv->regmap;
	struct platform_device *pdev = spdif_priv->pdev;
	u32 *pos, size, val, reg;

	switch (name) {
	case 'U':
		pos = &ctrl->upos;
		size = SPDIF_UBITS_SIZE;
		reg = REG_SPDIF_SRU;
		break;
	case 'Q':
		pos = &ctrl->qpos;
		size = SPDIF_QSUB_SIZE;
		reg = REG_SPDIF_SRQ;
		break;
	default:
		dev_err(&pdev->dev, "unsupported channel name\n");
		return;
	}

	dev_dbg(&pdev->dev, "isr: %c Channel receive register full\n", name);

	if (*pos >= size * 2) {
		*pos = 0;
	} else if (unlikely((*pos % size) + 3 > size)) {
		dev_err(&pdev->dev, "User bit receivce buffer overflow\n");
		return;
	}

	regmap_read(regmap, reg, &val);
	ctrl->subcode[*pos++] = val >> 16;
	ctrl->subcode[*pos++] = val >> 8;
	ctrl->subcode[*pos++] = val;
}

/* U/Q Channel sync found */
static void spdif_irq_uq_sync(struct fsl_spdif_priv *spdif_priv)
{
	struct spdif_mixer_control *ctrl = &spdif_priv->fsl_spdif_control;
	struct platform_device *pdev = spdif_priv->pdev;

	dev_dbg(&pdev->dev, "isr: U/Q Channel sync found\n");

	/* U/Q buffer reset */
	if (ctrl->qpos == 0)
		return;

	/* Set ready to this buffer */
	ctrl->ready_buf = (ctrl->qpos - 1) / SPDIF_QSUB_SIZE + 1;
}

/* U/Q Channel framing error */
static void spdif_irq_uq_err(struct fsl_spdif_priv *spdif_priv)
{
	struct spdif_mixer_control *ctrl = &spdif_priv->fsl_spdif_control;
	struct regmap *regmap = spdif_priv->regmap;
	struct platform_device *pdev = spdif_priv->pdev;
	u32 val;

	dev_dbg(&pdev->dev, "isr: U/Q Channel framing error\n");

	/* Read U/Q data to clear the irq and do buffer reset */
	regmap_read(regmap, REG_SPDIF_SRU, &val);
	regmap_read(regmap, REG_SPDIF_SRQ, &val);

	/* Drop this U/Q buffer */
	ctrl->ready_buf = 0;
	ctrl->upos = 0;
	ctrl->qpos = 0;
}

/* Get spdif interrupt status and clear the interrupt */
static u32 spdif_intr_status_clear(struct fsl_spdif_priv *spdif_priv)
{
	struct regmap *regmap = spdif_priv->regmap;
	u32 val, val2;

	regmap_read(regmap, REG_SPDIF_SIS, &val);
	regmap_read(regmap, REG_SPDIF_SIE, &val2);

	regmap_write(regmap, REG_SPDIF_SIC, val & val2);

	return val;
}

static irqreturn_t spdif_isr(int irq, void *devid)
{
	struct fsl_spdif_priv *spdif_priv = (struct fsl_spdif_priv *)devid;
	struct platform_device *pdev = spdif_priv->pdev;
	u32 sis;

	sis = spdif_intr_status_clear(spdif_priv);

	if (sis & INT_DPLL_LOCKED)
		spdif_irq_dpll_lock(spdif_priv);

	if (sis & INT_TXFIFO_UNOV)
		dev_dbg(&pdev->dev, "isr: Tx FIFO under/overrun\n");

	if (sis & INT_TXFIFO_RESYNC)
		dev_dbg(&pdev->dev, "isr: Tx FIFO resync\n");

	if (sis & INT_CNEW)
		dev_dbg(&pdev->dev, "isr: cstatus new\n");

	if (sis & INT_VAL_NOGOOD)
		dev_dbg(&pdev->dev, "isr: validity flag no good\n");

	if (sis & INT_SYM_ERR)
		spdif_irq_sym_error(spdif_priv);

	if (sis & INT_BIT_ERR)
		dev_dbg(&pdev->dev, "isr: receiver found parity bit error\n");

	if (sis & INT_URX_FUL)
		spdif_irq_uqrx_full(spdif_priv, 'U');

	if (sis & INT_URX_OV)
		dev_dbg(&pdev->dev, "isr: U Channel receive register overrun\n");

	if (sis & INT_QRX_FUL)
		spdif_irq_uqrx_full(spdif_priv, 'Q');

	if (sis & INT_QRX_OV)
		dev_dbg(&pdev->dev, "isr: Q Channel receive register overrun\n");

	if (sis & INT_UQ_SYNC)
		spdif_irq_uq_sync(spdif_priv);

	if (sis & INT_UQ_ERR)
		spdif_irq_uq_err(spdif_priv);

	if (sis & INT_RXFIFO_UNOV)
		dev_dbg(&pdev->dev, "isr: Rx FIFO under/overrun\n");

	if (sis & INT_RXFIFO_RESYNC)
		dev_dbg(&pdev->dev, "isr: Rx FIFO resync\n");

	if (sis & INT_LOSS_LOCK)
		spdif_irq_dpll_lock(spdif_priv);

	/* FIXME: Write Tx FIFO to clear TxEm */
	if (sis & INT_TX_EM)
		dev_dbg(&pdev->dev, "isr: Tx FIFO empty\n");

	/* FIXME: Read Rx FIFO to clear RxFIFOFul */
	if (sis & INT_RXFIFO_FUL)
		dev_dbg(&pdev->dev, "isr: Rx FIFO full\n");

	return IRQ_HANDLED;
}

static int spdif_softreset(struct fsl_spdif_priv *spdif_priv)
{
	struct regmap *regmap = spdif_priv->regmap;
	u32 val, cycle = 1000;

	regmap_write(regmap, REG_SPDIF_SCR, SCR_SOFT_RESET);

	/*
	 * RESET bit would be cleared after finishing its reset procedure,
	 * which typically lasts 8 cycles. 1000 cycles will keep it safe.
	 */
	do {
		regmap_read(regmap, REG_SPDIF_SCR, &val);
	} while ((val & SCR_SOFT_RESET) && cycle--);

	if (cycle)
		return 0;
	else
		return -EBUSY;
}

static void spdif_set_cstatus(struct spdif_mixer_control *ctrl,
				u8 mask, u8 cstatus)
{
	ctrl->ch_status[3] &= ~mask;
	ctrl->ch_status[3] |= cstatus & mask;
}

static void spdif_write_channel_status(struct fsl_spdif_priv *spdif_priv)
{
	struct spdif_mixer_control *ctrl = &spdif_priv->fsl_spdif_control;
	struct regmap *regmap = spdif_priv->regmap;
	struct platform_device *pdev = spdif_priv->pdev;
	u32 ch_status;

	ch_status = (bitrev8(ctrl->ch_status[0]) << 16) |
		(bitrev8(ctrl->ch_status[1]) << 8) |
		bitrev8(ctrl->ch_status[2]);
	regmap_write(regmap, REG_SPDIF_STCSCH, ch_status);

	dev_dbg(&pdev->dev, "STCSCH: 0x%06x\n", ch_status);

	ch_status = bitrev8(ctrl->ch_status[3]) << 16;
	regmap_write(regmap, REG_SPDIF_STCSCL, ch_status);

	dev_dbg(&pdev->dev, "STCSCL: 0x%06x\n", ch_status);
}

/* Set SPDIF PhaseConfig register for rx clock */
static int spdif_set_rx_clksrc(struct fsl_spdif_priv *spdif_priv,
				enum spdif_gainsel gainsel, int dpll_locked)
{
	struct regmap *regmap = spdif_priv->regmap;
	u8 clksrc = spdif_priv->rxclk_src;

	if (clksrc >= SRPC_CLKSRC_MAX || gainsel >= GAINSEL_MULTI_MAX)
		return -EINVAL;

	regmap_update_bits(regmap, REG_SPDIF_SRPC,
			SRPC_CLKSRC_SEL_MASK | SRPC_GAINSEL_MASK,
			SRPC_CLKSRC_SEL_SET(clksrc) | SRPC_GAINSEL_SET(gainsel));

	return 0;
}

static int spdif_set_sample_rate(struct snd_pcm_substream *substream,
				int sample_rate)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct fsl_spdif_priv *spdif_priv = snd_soc_dai_get_drvdata(rtd->cpu_dai);
	struct spdif_mixer_control *ctrl = &spdif_priv->fsl_spdif_control;
	struct regmap *regmap = spdif_priv->regmap;
	struct platform_device *pdev = spdif_priv->pdev;
	unsigned long csfs = 0;
	u32 stc, mask, rate;
	u8 clk, div;
	int ret;

	switch (sample_rate) {
	case 32000:
		rate = SPDIF_TXRATE_32000;
		csfs = IEC958_AES3_CON_FS_32000;
		break;
	case 44100:
		rate = SPDIF_TXRATE_44100;
		csfs = IEC958_AES3_CON_FS_44100;
		break;
	case 48000:
		rate = SPDIF_TXRATE_48000;
		csfs = IEC958_AES3_CON_FS_48000;
		break;
	default:
		dev_err(&pdev->dev, "unsupported sample rate %d\n", sample_rate);
		return -EINVAL;
	}

	clk = spdif_priv->txclk_src[rate];
	if (clk >= STC_TXCLK_SRC_MAX) {
		dev_err(&pdev->dev, "tx clock source is out of range\n");
		return -EINVAL;
	}

	div = spdif_priv->txclk_div[rate];
	if (div == 0) {
		dev_err(&pdev->dev, "the divisor can't be zero\n");
		return -EINVAL;
	}

	/*
	 * The S/PDIF block needs a clock of 64 * fs * div.  The S/PDIF block
	 * will divide by (div).  So request 64 * fs * (div+1) which will
	 * get rounded.
	 */
	ret = clk_set_rate(spdif_priv->txclk[rate], 64 * sample_rate * (div + 1));
	if (ret) {
		dev_err(&pdev->dev, "failed to set tx clock rate\n");
		return ret;
	}

	dev_dbg(&pdev->dev, "expected clock rate = %d\n",
			(64 * sample_rate * div));
	dev_dbg(&pdev->dev, "actual clock rate = %ld\n",
			clk_get_rate(spdif_priv->txclk[rate]));

	/* set fs field in consumer channel status */
	spdif_set_cstatus(ctrl, IEC958_AES3_CON_FS, csfs);

	/* select clock source and divisor */
	stc = STC_TXCLK_ALL_EN | STC_TXCLK_SRC_SET(clk) | STC_TXCLK_DIV(div);
	mask = STC_TXCLK_ALL_EN_MASK | STC_TXCLK_SRC_MASK | STC_TXCLK_DIV_MASK;
	regmap_update_bits(regmap, REG_SPDIF_STC, mask, stc);

	dev_dbg(&pdev->dev, "set sample rate to %d\n", sample_rate);

	return 0;
}

int fsl_spdif_startup(struct snd_pcm_substream *substream,
			struct snd_soc_dai *cpu_dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct fsl_spdif_priv *spdif_priv = snd_soc_dai_get_drvdata(rtd->cpu_dai);
	struct platform_device *pdev = spdif_priv->pdev;
	struct regmap *regmap = spdif_priv->regmap;
	u32 scr, mask, i;
	int ret;

	/* Reset module and interrupts only for first initialization */
	if (!cpu_dai->active) {
		ret = spdif_softreset(spdif_priv);
		if (ret) {
			dev_err(&pdev->dev, "failed to soft reset\n");
			return ret;
		}

		/* Disable all the interrupts */
		regmap_update_bits(regmap, REG_SPDIF_SIE, 0xffffff, 0);
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		scr = SCR_TXFIFO_AUTOSYNC | SCR_TXFIFO_CTRL_NORMAL |
			SCR_TXSEL_NORMAL | SCR_USRC_SEL_CHIP |
			SCR_TXFIFO_FSEL_IF8;
		mask = SCR_TXFIFO_AUTOSYNC_MASK | SCR_TXFIFO_CTRL_MASK |
			SCR_TXSEL_MASK | SCR_USRC_SEL_MASK |
			SCR_TXFIFO_FSEL_MASK;
		for (i = 0; i < SPDIF_TXRATE_MAX; i++)
			clk_prepare_enable(spdif_priv->txclk[i]);
	} else {
		scr = SCR_RXFIFO_FSEL_IF8 | SCR_RXFIFO_AUTOSYNC;
		mask = SCR_RXFIFO_FSEL_MASK | SCR_RXFIFO_AUTOSYNC_MASK|
			SCR_RXFIFO_CTL_MASK | SCR_RXFIFO_OFF_MASK;
		clk_prepare_enable(spdif_priv->rxclk);
	}
	regmap_update_bits(regmap, REG_SPDIF_SCR, mask, scr);

	/* Power up SPDIF module */
	regmap_update_bits(regmap, REG_SPDIF_SCR, SCR_LOW_POWER, 0);

	return 0;
}

static void fsl_spdif_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *cpu_dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct fsl_spdif_priv *spdif_priv = snd_soc_dai_get_drvdata(rtd->cpu_dai);
	struct regmap *regmap = spdif_priv->regmap;
	u32 scr, mask, i;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		scr = 0;
		mask = SCR_TXFIFO_AUTOSYNC_MASK | SCR_TXFIFO_CTRL_MASK |
			SCR_TXSEL_MASK | SCR_USRC_SEL_MASK |
			SCR_TXFIFO_FSEL_MASK;
		for (i = 0; i < SPDIF_TXRATE_MAX; i++)
			clk_disable_unprepare(spdif_priv->txclk[i]);
	} else {
		scr = SCR_RXFIFO_OFF | SCR_RXFIFO_CTL_ZERO;
		mask = SCR_RXFIFO_FSEL_MASK | SCR_RXFIFO_AUTOSYNC_MASK|
			SCR_RXFIFO_CTL_MASK | SCR_RXFIFO_OFF_MASK;
		clk_disable_unprepare(spdif_priv->rxclk);
	}
	regmap_update_bits(regmap, REG_SPDIF_SCR, mask, scr);

	/* Power down SPDIF module only if tx&rx are both inactive */
	if (!cpu_dai->active) {
		spdif_intr_status_clear(spdif_priv);
		regmap_update_bits(regmap, REG_SPDIF_SCR,
				SCR_LOW_POWER, SCR_LOW_POWER);
	}
}

static int fsl_spdif_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct fsl_spdif_priv *spdif_priv = snd_soc_dai_get_drvdata(rtd->cpu_dai);
	struct spdif_mixer_control *ctrl = &spdif_priv->fsl_spdif_control;
	struct platform_device *pdev = spdif_priv->pdev;
	u32 sample_rate = params_rate(params);
	int ret = 0;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		ret  = spdif_set_sample_rate(substream, sample_rate);
		if (ret) {
			dev_err(&pdev->dev, "%s: set sample rate failed: %d\n",
					__func__, sample_rate);
			return ret;
		}
		spdif_set_cstatus(ctrl, IEC958_AES3_CON_CLOCK,
				IEC958_AES3_CON_CLOCK_1000PPM);
		spdif_write_channel_status(spdif_priv);
	} else {
		/* Setup rx clock source */
		ret = spdif_set_rx_clksrc(spdif_priv, SPDIF_DEFAULT_GAINSEL, 1);
	}

	return ret;
}

static int fsl_spdif_trigger(struct snd_pcm_substream *substream,
				int cmd, struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct fsl_spdif_priv *spdif_priv = snd_soc_dai_get_drvdata(rtd->cpu_dai);
	struct regmap *regmap = spdif_priv->regmap;
	int is_playack = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);
	u32 intr = is_playack ? INTR_FOR_PLAYBACK : INTR_FOR_CAPTURE;
	u32 dmaen = is_playack ? SCR_DMA_TX_EN : SCR_DMA_RX_EN;;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		regmap_update_bits(regmap, REG_SPDIF_SIE, intr, intr);
		regmap_update_bits(regmap, REG_SPDIF_SCR, dmaen, dmaen);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		regmap_update_bits(regmap, REG_SPDIF_SCR, dmaen, 0);
		regmap_update_bits(regmap, REG_SPDIF_SIE, intr, 0);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

struct snd_soc_dai_ops fsl_spdif_dai_ops = {
	.startup = fsl_spdif_startup,
	.hw_params = fsl_spdif_hw_params,
	.trigger = fsl_spdif_trigger,
	.shutdown = fsl_spdif_shutdown,
};


/*
 * FSL SPDIF IEC958 controller(mixer) functions
 *
 *	Channel status get/put control
 *	User bit value get/put control
 *	Valid bit value get control
 *	DPLL lock status get control
 *	User bit sync mode selection control
 */

static int fsl_spdif_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;

	return 0;
}

static int fsl_spdif_pb_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *uvalue)
{
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kcontrol);
	struct fsl_spdif_priv *spdif_priv = snd_soc_dai_get_drvdata(cpu_dai);
	struct spdif_mixer_control *ctrl = &spdif_priv->fsl_spdif_control;

	uvalue->value.iec958.status[0] = ctrl->ch_status[0];
	uvalue->value.iec958.status[1] = ctrl->ch_status[1];
	uvalue->value.iec958.status[2] = ctrl->ch_status[2];
	uvalue->value.iec958.status[3] = ctrl->ch_status[3];

	return 0;
}

static int fsl_spdif_pb_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *uvalue)
{
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kcontrol);
	struct fsl_spdif_priv *spdif_priv = snd_soc_dai_get_drvdata(cpu_dai);
	struct spdif_mixer_control *ctrl = &spdif_priv->fsl_spdif_control;

	ctrl->ch_status[0] = uvalue->value.iec958.status[0];
	ctrl->ch_status[1] = uvalue->value.iec958.status[1];
	ctrl->ch_status[2] = uvalue->value.iec958.status[2];
	ctrl->ch_status[3] = uvalue->value.iec958.status[3];

	spdif_write_channel_status(spdif_priv);

	return 0;
}

/* Get channel status from SPDIF_RX_CCHAN register */
static int fsl_spdif_capture_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kcontrol);
	struct fsl_spdif_priv *spdif_priv = snd_soc_dai_get_drvdata(cpu_dai);
	struct regmap *regmap = spdif_priv->regmap;
	u32 cstatus, val;

	regmap_read(regmap, REG_SPDIF_SIS, &val);
	if (!(val & INT_CNEW)) {
		return -EAGAIN;
	}

	regmap_read(regmap, REG_SPDIF_SRCSH, &cstatus);
	ucontrol->value.iec958.status[0] = (cstatus >> 16) & 0xFF;
	ucontrol->value.iec958.status[1] = (cstatus >> 8) & 0xFF;
	ucontrol->value.iec958.status[2] = cstatus & 0xFF;

	regmap_read(regmap, REG_SPDIF_SRCSL, &cstatus);
	ucontrol->value.iec958.status[3] = (cstatus >> 16) & 0xFF;
	ucontrol->value.iec958.status[4] = (cstatus >> 8) & 0xFF;
	ucontrol->value.iec958.status[5] = cstatus & 0xFF;

	/* Clear intr */
	regmap_write(regmap, REG_SPDIF_SIC, INT_CNEW);

	return 0;
}

/*
 * Get User bits (subcode) from chip value which readed out
 * in UChannel register.
 */
static int fsl_spdif_subcode_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kcontrol);
	struct fsl_spdif_priv *spdif_priv = snd_soc_dai_get_drvdata(cpu_dai);
	struct spdif_mixer_control *ctrl = &spdif_priv->fsl_spdif_control;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&ctrl->ctl_lock, flags);
	if (ctrl->ready_buf) {
		int idx = (ctrl->ready_buf - 1) * SPDIF_UBITS_SIZE;
		memcpy(&ucontrol->value.iec958.subcode[0],
				&ctrl->subcode[idx], SPDIF_UBITS_SIZE);
	} else {
		ret = -EAGAIN;
	}
	spin_unlock_irqrestore(&ctrl->ctl_lock, flags);

	return ret;
}

/* Q-subcode infomation. The byte size is SPDIF_UBITS_SIZE/8 */
static int fsl_spdif_qinfo(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	uinfo->count = SPDIF_QSUB_SIZE;

	return 0;
}

/* Get Q subcode from chip value which readed out in QChannel register */
static int fsl_spdif_qget(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kcontrol);
	struct fsl_spdif_priv *spdif_priv = snd_soc_dai_get_drvdata(cpu_dai);
	struct spdif_mixer_control *ctrl = &spdif_priv->fsl_spdif_control;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&ctrl->ctl_lock, flags);
	if (ctrl->ready_buf) {
		int idx = (ctrl->ready_buf - 1) * SPDIF_QSUB_SIZE;
		memcpy(&ucontrol->value.bytes.data[0],
				&ctrl->qsub[idx], SPDIF_QSUB_SIZE);
	} else {
		ret = -EAGAIN;
	}
	spin_unlock_irqrestore(&ctrl->ctl_lock, flags);

	return ret;
}

/* Valid bit infomation */
static int fsl_spdif_vbit_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;

	return 0;
}

/* Get valid good bit from interrupt status register */
static int fsl_spdif_vbit_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kcontrol);
	struct fsl_spdif_priv *spdif_priv = snd_soc_dai_get_drvdata(cpu_dai);
	struct regmap *regmap = spdif_priv->regmap;
	u32 val;

	val = regmap_read(regmap, REG_SPDIF_SIS, &val);
	ucontrol->value.integer.value[0] = (val & INT_VAL_NOGOOD) != 0;
	regmap_write(regmap, REG_SPDIF_SIC, INT_VAL_NOGOOD);

	return 0;
}

/* DPLL lock infomation */
static int fsl_spdif_rxrate_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 16000;
	uinfo->value.integer.max = 96000;

	return 0;
}

static u32 gainsel_multi[GAINSEL_MULTI_MAX] = {
	24, 16, 12, 8, 6, 4, 3,
};

/* Get RX data clock rate given the SPDIF bus_clk */
static int spdif_get_rxclk_rate(struct fsl_spdif_priv *spdif_priv,
				enum spdif_gainsel gainsel)
{
	struct regmap *regmap = spdif_priv->regmap;
	struct platform_device *pdev = spdif_priv->pdev;
	u64 tmpval64, busclk_freq = 0;
	u32 freqmeas, phaseconf;
	u8 clksrc;

	regmap_read(regmap, REG_SPDIF_SRFM, &freqmeas);
	regmap_read(regmap, REG_SPDIF_SRPC, &phaseconf);

	clksrc = (phaseconf >> SRPC_CLKSRC_SEL_OFFSET) & 0xf;
	if (srpc_dpll_locked[clksrc] && (phaseconf & SRPC_DPLL_LOCKED)) {
		/* Get bus clock from system */
		busclk_freq = clk_get_rate(spdif_priv->rxclk);
	}

	/* FreqMeas_CLK = (BUS_CLK * FreqMeas) / 2 ^ 10 / GAINSEL / 128 */
	tmpval64 = (u64) busclk_freq * freqmeas;
	do_div(tmpval64, gainsel_multi[gainsel] * 1024);
	do_div(tmpval64, 128 * 1024);

	dev_dbg(&pdev->dev, "FreqMeas: %d\n", freqmeas);
	dev_dbg(&pdev->dev, "BusclkFreq: %lld\n", busclk_freq);
	dev_dbg(&pdev->dev, "RxRate: %lld\n", tmpval64);

	return (int)tmpval64;
}

/*
 * Get DPLL lock or not info from stable interrupt status register.
 * User application must use this control to get locked,
 * then can do next PCM operation
 */
static int fsl_spdif_rxrate_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kcontrol);
	struct fsl_spdif_priv *spdif_priv = snd_soc_dai_get_drvdata(cpu_dai);
	int rate = spdif_get_rxclk_rate(spdif_priv, SPDIF_DEFAULT_GAINSEL);

	if (spdif_priv->dpll_locked)
		ucontrol->value.integer.value[0] = rate;
	else
		ucontrol->value.integer.value[0] = 0;

	return 0;
}

/* User bit sync mode info */
static int fsl_spdif_usync_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;

	return 0;
}

/*
 * User bit sync mode:
 * 1 CD User channel subcode
 * 0 Non-CD data
 */
static int fsl_spdif_usync_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kcontrol);
	struct fsl_spdif_priv *spdif_priv = snd_soc_dai_get_drvdata(cpu_dai);
	struct regmap *regmap = spdif_priv->regmap;
	u32 val;

	regmap_read(regmap, REG_SPDIF_SRCD, &val);
	ucontrol->value.integer.value[0] = (val & SRCD_CD_USER) != 0;

	return 0;
}

/*
 * User bit sync mode:
 * 1 CD User channel subcode
 * 0 Non-CD data
 */
static int fsl_spdif_usync_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kcontrol);
	struct fsl_spdif_priv *spdif_priv = snd_soc_dai_get_drvdata(cpu_dai);
	struct regmap *regmap = spdif_priv->regmap;
	u32 val = ucontrol->value.integer.value[0] << SRCD_CD_USER_OFFSET;

	regmap_update_bits(regmap, REG_SPDIF_SRCD, SRCD_CD_USER, val);

	return 0;
}

/* FSL SPDIF IEC958 controller defines */
static struct snd_kcontrol_new fsl_spdif_ctrls[] = {
	/* Status cchanel controller */
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958("", PLAYBACK, DEFAULT),
		.access = SNDRV_CTL_ELEM_ACCESS_READ |
			SNDRV_CTL_ELEM_ACCESS_WRITE |
			SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = fsl_spdif_info,
		.get = fsl_spdif_pb_get,
		.put = fsl_spdif_pb_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = SNDRV_CTL_NAME_IEC958("", CAPTURE, DEFAULT),
		.access = SNDRV_CTL_ELEM_ACCESS_READ |
			SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = fsl_spdif_info,
		.get = fsl_spdif_capture_get,
	},
	/* User bits controller */
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = "IEC958 Subcode Capture Default",
		.access = SNDRV_CTL_ELEM_ACCESS_READ |
			SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = fsl_spdif_info,
		.get = fsl_spdif_subcode_get,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = "IEC958 Q-subcode Capture Default",
		.access = SNDRV_CTL_ELEM_ACCESS_READ |
			SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = fsl_spdif_qinfo,
		.get = fsl_spdif_qget,
	},
	/* Valid bit error controller */
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = "IEC958 V-Bit Errors",
		.access = SNDRV_CTL_ELEM_ACCESS_READ |
			SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = fsl_spdif_vbit_info,
		.get = fsl_spdif_vbit_get,
	},
	/* DPLL lock info get controller */
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = "RX Sample Rate",
		.access = SNDRV_CTL_ELEM_ACCESS_READ |
			SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = fsl_spdif_rxrate_info,
		.get = fsl_spdif_rxrate_get,
	},
	/* User bit sync mode set/get controller */
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = "IEC958 USyncMode CDText",
		.access = SNDRV_CTL_ELEM_ACCESS_READ |
			SNDRV_CTL_ELEM_ACCESS_WRITE |
			SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = fsl_spdif_usync_info,
		.get = fsl_spdif_usync_get,
		.put = fsl_spdif_usync_put,
	},
};

static int fsl_spdif_dai_probe(struct snd_soc_dai *dai)
{
	struct fsl_spdif_priv *spdif_private = snd_soc_dai_get_drvdata(dai);

	dai->playback_dma_data = &spdif_private->dma_params_tx;
	dai->capture_dma_data = &spdif_private->dma_params_rx;

	snd_soc_add_dai_controls(dai, fsl_spdif_ctrls, ARRAY_SIZE(fsl_spdif_ctrls));

	return 0;
}

struct snd_soc_dai_driver fsl_spdif_dai = {
	.probe = &fsl_spdif_dai_probe,
	.playback = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = FSL_SPDIF_RATES_PLAYBACK,
		.formats = FSL_SPDIF_FORMATS_PLAYBACK,
	},
	.capture = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = FSL_SPDIF_RATES_CAPTURE,
		.formats = FSL_SPDIF_FORMATS_CAPTURE,
	},
	.ops = &fsl_spdif_dai_ops,
};

static const struct snd_soc_component_driver fsl_spdif_component = {
	.name		= "fsl-spdif",
};

/* FSL SPDIF REGMAP */

static bool fsl_spdif_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case REG_SPDIF_SCR:
	case REG_SPDIF_SRCD:
	case REG_SPDIF_SRPC:
	case REG_SPDIF_SIE:
	case REG_SPDIF_SIS:
	case REG_SPDIF_SRL:
	case REG_SPDIF_SRR:
	case REG_SPDIF_SRCSH:
	case REG_SPDIF_SRCSL:
	case REG_SPDIF_SRU:
	case REG_SPDIF_SRQ:
	case REG_SPDIF_STCSCH:
	case REG_SPDIF_STCSCL:
	case REG_SPDIF_SRFM:
	case REG_SPDIF_STC:
		return true;
	default:
		return false;
	};
}

static bool fsl_spdif_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case REG_SPDIF_SCR:
	case REG_SPDIF_SRCD:
	case REG_SPDIF_SRPC:
	case REG_SPDIF_SIE:
	case REG_SPDIF_SIC:
	case REG_SPDIF_STL:
	case REG_SPDIF_STR:
	case REG_SPDIF_STCSCH:
	case REG_SPDIF_STCSCL:
	case REG_SPDIF_STC:
		return true;
	default:
		return false;
	};
}

static const struct regmap_config fsl_spdif_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,

	.max_register = REG_SPDIF_STC,
	.readable_reg = fsl_spdif_readable_reg,
	.writeable_reg = fsl_spdif_writeable_reg,
};

static u32 fsl_spdif_txclk_caldiv(struct fsl_spdif_priv *spdif_priv,
				struct clk *clk, u64 savesub,
				enum spdif_txrate index)
{
	const u32 rate[] = { 32000, 44100, 48000 };
	u64 rate_ideal, rate_actual, sub;
	u32 div, arate;

	for (div = 1; div <= 128; div++) {
		rate_ideal = rate[index] * (div + 1) * 64;
		rate_actual = clk_round_rate(clk, rate_ideal);

		arate = rate_actual / 64;
		arate /= div;

		if (arate == rate[index]) {
			/* We are lucky */
			savesub = 0;
			spdif_priv->txclk_div[index] = div;
			break;
		} else if (arate / rate[index] == 1) {
			/* A little bigger than expect */
			sub = (arate - rate[index]) * 100000;
			do_div(sub, rate[index]);
			if (sub < savesub) {
				savesub = sub;
				spdif_priv->txclk_div[index] = div;
			}
		} else if (rate[index] / arate == 1) {
			/* A little smaller than expect */
			sub = (rate[index] - arate) * 100000;
			do_div(sub, rate[index]);
			if (sub < savesub) {
				savesub = sub;
				spdif_priv->txclk_div[index] = div;
			}
		}
	}

	return savesub;
}

static int fsl_spdif_probe_txclk(struct fsl_spdif_priv *spdif_priv,
				enum spdif_txrate index)
{
	const u32 rate[] = { 32000, 44100, 48000 };
	struct platform_device *pdev = spdif_priv->pdev;
	struct device *dev = &pdev->dev;
	u64 savesub = 100000, ret;
	struct clk *clk;
	char tmp[16];
	int i;

	for (i = 0; i < STC_TXCLK_SRC_MAX; i++) {
		sprintf(tmp, "rxtx%d", i);
		clk = devm_clk_get(&pdev->dev, tmp);
		if (IS_ERR(clk)) {
			dev_err(dev, "no rxtx%d clock in devicetree\n", i);
			return PTR_ERR(clk);
		}
		if (!clk_get_rate(clk))
			continue;

		ret = fsl_spdif_txclk_caldiv(spdif_priv, clk, savesub, index);
		if (savesub == ret)
			continue;

		savesub = ret;
		spdif_priv->txclk[index] = clk;
		spdif_priv->txclk_src[index] = i;

		/* To quick catch a divisor, we allow a 0.1% deviation */
		if (savesub < 100)
			break;
	}

	dev_dbg(&pdev->dev, "use rxtx%d as tx clock source for %dHz sample rate",
			spdif_priv->txclk_src[index], rate[index]);
	dev_dbg(&pdev->dev, "use divisor %d for %dHz sample rate",
			spdif_priv->txclk_div[index], rate[index]);

	return 0;
}

static int fsl_spdif_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct fsl_spdif_priv *spdif_priv;
	struct spdif_mixer_control *ctrl;
	struct resource *res;
	void __iomem *regs;
	int irq, ret, i;

	if (!np)
		return -ENODEV;

	spdif_priv = devm_kzalloc(&pdev->dev,
			sizeof(struct fsl_spdif_priv) + strlen(np->name) + 1,
			GFP_KERNEL);
	if (!spdif_priv)
		return -ENOMEM;

	strcpy(spdif_priv->name, np->name);

	spdif_priv->pdev = pdev;

	/* Initialize this copy of the CPU DAI driver structure */
	memcpy(&spdif_priv->cpu_dai_drv, &fsl_spdif_dai, sizeof(fsl_spdif_dai));
	spdif_priv->cpu_dai_drv.name = spdif_priv->name;

	/* Get the addresses and IRQ */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (IS_ERR(res)) {
		dev_err(&pdev->dev, "could not determine device resources\n");
		return PTR_ERR(res);
	}

	regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	spdif_priv->regmap = devm_regmap_init_mmio_clk(&pdev->dev,
			"core", regs, &fsl_spdif_regmap_config);
	if (IS_ERR(spdif_priv->regmap)) {
		dev_err(&pdev->dev, "regmap init failed\n");
		return PTR_ERR(spdif_priv->regmap);
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no irq for node %s\n", np->full_name);
		return irq;
	}

	ret = devm_request_irq(&pdev->dev, irq, spdif_isr, 0,
			spdif_priv->name, spdif_priv);
	if (ret) {
		dev_err(&pdev->dev, "could not claim irq %u\n", irq);
		return ret;
	}

	/* Select clock source for rx/tx clock */
	spdif_priv->rxclk = devm_clk_get(&pdev->dev, "rxtx1");
	if (IS_ERR(spdif_priv->rxclk)) {
		dev_err(&pdev->dev, "no rxtx1 clock in devicetree\n");
		return PTR_ERR(spdif_priv->rxclk);
	}
	spdif_priv->rxclk_src = DEFAULT_RXCLK_SRC;

	for (i = 0; i < SPDIF_TXRATE_MAX; i++) {
		ret = fsl_spdif_probe_txclk(spdif_priv, i);
		if (ret)
			return ret;
	}

	/* Initial spinlock for control data */
	ctrl = &spdif_priv->fsl_spdif_control;
	spin_lock_init(&ctrl->ctl_lock);

	/* Init tx channel status default value */
	ctrl->ch_status[0] =
		IEC958_AES0_CON_NOT_COPYRIGHT | IEC958_AES0_CON_EMPHASIS_5015;
	ctrl->ch_status[1] = IEC958_AES1_CON_DIGDIGCONV_ID;
	ctrl->ch_status[2] = 0x00;
	ctrl->ch_status[3] =
		IEC958_AES3_CON_FS_44100 | IEC958_AES3_CON_CLOCK_1000PPM;

	spdif_priv->dpll_locked = false;

	spdif_priv->dma_params_tx.maxburst = FSL_SPDIF_TXFIFO_WML;
	spdif_priv->dma_params_rx.maxburst = FSL_SPDIF_RXFIFO_WML;
	spdif_priv->dma_params_tx.addr = res->start + REG_SPDIF_STL;
	spdif_priv->dma_params_rx.addr = res->start + REG_SPDIF_SRL;

	/* Register with ASoC */
	dev_set_drvdata(&pdev->dev, spdif_priv);

	ret = snd_soc_register_component(&pdev->dev, &fsl_spdif_component,
					 &spdif_priv->cpu_dai_drv, 1);
	if (ret) {
		dev_err(&pdev->dev, "failed to register DAI: %d\n", ret);
		return ret;
	}

	ret = imx_pcm_dma_init(pdev);
	if (ret) {
		dev_err(&pdev->dev, "imx_pcm_dma_init failed: %d\n", ret);
		goto error_component;
	}

	return ret;

error_component:
	snd_soc_unregister_component(&pdev->dev);

	return ret;
}

static int fsl_spdif_remove(struct platform_device *pdev)
{
	imx_pcm_dma_exit(pdev);
	snd_soc_unregister_component(&pdev->dev);

	return 0;
}

static const struct of_device_id fsl_spdif_dt_ids[] = {
	{ .compatible = "fsl,imx35-spdif", },
	{}
};
MODULE_DEVICE_TABLE(of, fsl_spdif_dt_ids);

static struct platform_driver fsl_spdif_driver = {
	.driver = {
		.name = "fsl-spdif-dai",
		.owner = THIS_MODULE,
		.of_match_table = fsl_spdif_dt_ids,
	},
	.probe = fsl_spdif_probe,
	.remove = fsl_spdif_remove,
};

module_platform_driver(fsl_spdif_driver);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("Freescale S/PDIF CPU DAI Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:fsl-spdif-dai");
