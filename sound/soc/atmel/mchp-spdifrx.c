// SPDX-License-Identifier: GPL-2.0
//
// Driver for Microchip S/PDIF RX Controller
//
// Copyright (C) 2020 Microchip Technology Inc. and its subsidiaries
//
// Author: Codrin Ciubotariu <codrin.ciubotariu@microchip.com>

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>

#include <sound/dmaengine_pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

/*
 * ---- S/PDIF Receiver Controller Register map ----
 */
#define SPDIFRX_CR			0x00	/* Control Register */
#define SPDIFRX_MR			0x04	/* Mode Register */

#define SPDIFRX_IER			0x10	/* Interrupt Enable Register */
#define SPDIFRX_IDR			0x14	/* Interrupt Disable Register */
#define SPDIFRX_IMR			0x18	/* Interrupt Mask Register */
#define SPDIFRX_ISR			0x1c	/* Interrupt Status Register */
#define SPDIFRX_RSR			0x20	/* Status Register */
#define SPDIFRX_RHR			0x24	/* Holding Register */

#define SPDIFRX_CHSR(channel, reg)	\
	(0x30 + (channel) * 0x30 + (reg) * 4)	/* Channel x Status Registers */

#define SPDIFRX_CHUD(channel, reg)	\
	(0x48 + (channel) * 0x30 + (reg) * 4)	/* Channel x User Data Registers */

#define SPDIFRX_WPMR			0xE4	/* Write Protection Mode Register */
#define SPDIFRX_WPSR			0xE8	/* Write Protection Status Register */

#define SPDIFRX_VERSION			0xFC	/* Version Register */

/*
 * ---- Control Register (Write-only) ----
 */
#define SPDIFRX_CR_SWRST		BIT(0)	/* Software Reset */

/*
 * ---- Mode Register (Read/Write) ----
 */
/* Receive Enable */
#define SPDIFRX_MR_RXEN_MASK		GENMASK(0, 0)
#define SPDIFRX_MR_RXEN_DISABLE		(0 << 0)	/* SPDIF Receiver Disabled */
#define SPDIFRX_MR_RXEN_ENABLE		(1 << 0)	/* SPDIF Receiver Enabled */

/* Validity Bit Mode */
#define SPDIFRX_MR_VBMODE_MASK		GENAMSK(1, 1)
#define SPDIFRX_MR_VBMODE_ALWAYS_LOAD \
	(0 << 1)	/* Load sample regardless of validity bit value */
#define SPDIFRX_MR_VBMODE_DISCARD_IF_VB1 \
	(1 << 1)	/* Load sample only if validity bit is 0 */

/* Data Word Endian Mode */
#define SPDIFRX_MR_ENDIAN_MASK		GENMASK(2, 2)
#define SPDIFRX_MR_ENDIAN_LITTLE	(0 << 2)	/* Little Endian Mode */
#define SPDIFRX_MR_ENDIAN_BIG		(1 << 2)	/* Big Endian Mode */

/* Parity Bit Mode */
#define SPDIFRX_MR_PBMODE_MASK		GENMASK(3, 3)
#define SPDIFRX_MR_PBMODE_PARCHECK	(0 << 3)	/* Parity Check Enabled */
#define SPDIFRX_MR_PBMODE_NOPARCHECK	(1 << 3)	/* Parity Check Disabled */

/* Sample Data Width */
#define SPDIFRX_MR_DATAWIDTH_MASK	GENMASK(5, 4)
#define SPDIFRX_MR_DATAWIDTH(width) \
	(((6 - (width) / 4) << 4) & SPDIFRX_MR_DATAWIDTH_MASK)

/* Packed Data Mode in Receive Holding Register */
#define SPDIFRX_MR_PACK_MASK		GENMASK(7, 7)
#define SPDIFRX_MR_PACK_DISABLED	(0 << 7)
#define SPDIFRX_MR_PACK_ENABLED		(1 << 7)

/* Start of Block Bit Mode */
#define SPDIFRX_MR_SBMODE_MASK		GENMASK(8, 8)
#define SPDIFRX_MR_SBMODE_ALWAYS_LOAD	(0 << 8)
#define SPDIFRX_MR_SBMODE_DISCARD	(1 << 8)

/* Consecutive Preamble Error Threshold Automatic Restart */
#define SPDIFRX_MR_AUTORST_MASK			GENMASK(24, 24)
#define SPDIFRX_MR_AUTORST_NOACTION		(0 << 24)
#define SPDIFRX_MR_AUTORST_UNLOCK_ON_PRE_ERR	(1 << 24)

/*
 * ---- Interrupt Enable/Disable/Mask/Status Register (Write/Read-only) ----
 */
#define SPDIFRX_IR_RXRDY			BIT(0)
#define SPDIFRX_IR_LOCKED			BIT(1)
#define SPDIFRX_IR_LOSS				BIT(2)
#define SPDIFRX_IR_BLOCKEND			BIT(3)
#define SPDIFRX_IR_SFE				BIT(4)
#define SPDIFRX_IR_PAR_ERR			BIT(5)
#define SPDIFRX_IR_OVERRUN			BIT(6)
#define SPDIFRX_IR_RXFULL			BIT(7)
#define SPDIFRX_IR_CSC(ch)			BIT((ch) + 8)
#define SPDIFRX_IR_SECE				BIT(10)
#define SPDIFRX_IR_BLOCKST			BIT(11)
#define SPDIFRX_IR_NRZ_ERR			BIT(12)
#define SPDIFRX_IR_PRE_ERR			BIT(13)
#define SPDIFRX_IR_CP_ERR			BIT(14)

/*
 * ---- Receiver Status Register (Read/Write) ----
 */
/* Enable Status */
#define SPDIFRX_RSR_ULOCK			BIT(0)
#define SPDIFRX_RSR_BADF			BIT(1)
#define SPDIFRX_RSR_LOWF			BIT(2)
#define SPDIFRX_RSR_NOSIGNAL			BIT(3)
#define SPDIFRX_RSR_IFS_MASK			GENMASK(27, 16)
#define SPDIFRX_RSR_IFS(reg)			\
	(((reg) & SPDIFRX_RSR_IFS_MASK) >> 16)

/*
 *  ---- Version Register (Read-only) ----
 */
#define SPDIFRX_VERSION_MASK		GENMASK(11, 0)
#define SPDIFRX_VERSION_MFN_MASK	GENMASK(18, 16)
#define SPDIFRX_VERSION_MFN(reg)	(((reg) & SPDIFRX_VERSION_MFN_MASK) >> 16)

static bool mchp_spdifrx_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SPDIFRX_MR:
	case SPDIFRX_IMR:
	case SPDIFRX_ISR:
	case SPDIFRX_RSR:
	case SPDIFRX_CHSR(0, 0):
	case SPDIFRX_CHSR(0, 1):
	case SPDIFRX_CHSR(0, 2):
	case SPDIFRX_CHSR(0, 3):
	case SPDIFRX_CHSR(0, 4):
	case SPDIFRX_CHSR(0, 5):
	case SPDIFRX_CHUD(0, 0):
	case SPDIFRX_CHUD(0, 1):
	case SPDIFRX_CHUD(0, 2):
	case SPDIFRX_CHUD(0, 3):
	case SPDIFRX_CHUD(0, 4):
	case SPDIFRX_CHUD(0, 5):
	case SPDIFRX_CHSR(1, 0):
	case SPDIFRX_CHSR(1, 1):
	case SPDIFRX_CHSR(1, 2):
	case SPDIFRX_CHSR(1, 3):
	case SPDIFRX_CHSR(1, 4):
	case SPDIFRX_CHSR(1, 5):
	case SPDIFRX_CHUD(1, 0):
	case SPDIFRX_CHUD(1, 1):
	case SPDIFRX_CHUD(1, 2):
	case SPDIFRX_CHUD(1, 3):
	case SPDIFRX_CHUD(1, 4):
	case SPDIFRX_CHUD(1, 5):
	case SPDIFRX_WPMR:
	case SPDIFRX_WPSR:
	case SPDIFRX_VERSION:
		return true;
	default:
		return false;
	}
}

static bool mchp_spdifrx_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SPDIFRX_CR:
	case SPDIFRX_MR:
	case SPDIFRX_IER:
	case SPDIFRX_IDR:
	case SPDIFRX_WPMR:
		return true;
	default:
		return false;
	}
}

static bool mchp_spdifrx_precious_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SPDIFRX_ISR:
	case SPDIFRX_RHR:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config mchp_spdifrx_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = SPDIFRX_VERSION,
	.readable_reg = mchp_spdifrx_readable_reg,
	.writeable_reg = mchp_spdifrx_writeable_reg,
	.precious_reg = mchp_spdifrx_precious_reg,
};

#define SPDIFRX_GCLK_RATIO_MIN	(12 * 64)

#define SPDIFRX_CS_BITS		192
#define SPDIFRX_UD_BITS		192

#define SPDIFRX_CHANNELS	2

struct mchp_spdifrx_ch_stat {
	unsigned char data[SPDIFRX_CS_BITS / 8];
	struct completion done;
};

struct mchp_spdifrx_user_data {
	unsigned char data[SPDIFRX_UD_BITS / 8];
	struct completion done;
};

struct mchp_spdifrx_mixer_control {
		struct mchp_spdifrx_ch_stat ch_stat[SPDIFRX_CHANNELS];
		struct mchp_spdifrx_user_data user_data[SPDIFRX_CHANNELS];
		bool ulock;
		bool badf;
		bool signal;
};

struct mchp_spdifrx_dev {
	struct snd_dmaengine_dai_dma_data	capture;
	struct mchp_spdifrx_mixer_control	control;
	struct mutex				mlock;
	struct device				*dev;
	struct regmap				*regmap;
	struct clk				*pclk;
	struct clk				*gclk;
	unsigned int				fmt;
	unsigned int				trigger_enabled;
	unsigned int				gclk_enabled:1;
};

static void mchp_spdifrx_channel_status_read(struct mchp_spdifrx_dev *dev,
					     int channel)
{
	struct mchp_spdifrx_mixer_control *ctrl = &dev->control;
	u8 *ch_stat = &ctrl->ch_stat[channel].data[0];
	u32 val;
	int i;

	for (i = 0; i < ARRAY_SIZE(ctrl->ch_stat[channel].data) / 4; i++) {
		regmap_read(dev->regmap, SPDIFRX_CHSR(channel, i), &val);
		*ch_stat++ = val & 0xFF;
		*ch_stat++ = (val >> 8) & 0xFF;
		*ch_stat++ = (val >> 16) & 0xFF;
		*ch_stat++ = (val >> 24) & 0xFF;
	}
}

static void mchp_spdifrx_channel_user_data_read(struct mchp_spdifrx_dev *dev,
						int channel)
{
	struct mchp_spdifrx_mixer_control *ctrl = &dev->control;
	u8 *user_data = &ctrl->user_data[channel].data[0];
	u32 val;
	int i;

	for (i = 0; i < ARRAY_SIZE(ctrl->user_data[channel].data) / 4; i++) {
		regmap_read(dev->regmap, SPDIFRX_CHUD(channel, i), &val);
		*user_data++ = val & 0xFF;
		*user_data++ = (val >> 8) & 0xFF;
		*user_data++ = (val >> 16) & 0xFF;
		*user_data++ = (val >> 24) & 0xFF;
	}
}

static irqreturn_t mchp_spdif_interrupt(int irq, void *dev_id)
{
	struct mchp_spdifrx_dev *dev = dev_id;
	struct mchp_spdifrx_mixer_control *ctrl = &dev->control;
	u32 sr, imr, pending;
	irqreturn_t ret = IRQ_NONE;
	int ch;

	regmap_read(dev->regmap, SPDIFRX_ISR, &sr);
	regmap_read(dev->regmap, SPDIFRX_IMR, &imr);
	pending = sr & imr;
	dev_dbg(dev->dev, "ISR: %#x, IMR: %#x, pending: %#x\n", sr, imr,
		pending);

	if (!pending)
		return IRQ_NONE;

	if (pending & SPDIFRX_IR_BLOCKEND) {
		for (ch = 0; ch < SPDIFRX_CHANNELS; ch++) {
			mchp_spdifrx_channel_user_data_read(dev, ch);
			complete(&ctrl->user_data[ch].done);
		}
		regmap_write(dev->regmap, SPDIFRX_IDR, SPDIFRX_IR_BLOCKEND);
		ret = IRQ_HANDLED;
	}

	for (ch = 0; ch < SPDIFRX_CHANNELS; ch++) {
		if (pending & SPDIFRX_IR_CSC(ch)) {
			mchp_spdifrx_channel_status_read(dev, ch);
			complete(&ctrl->ch_stat[ch].done);
			regmap_write(dev->regmap, SPDIFRX_IDR, SPDIFRX_IR_CSC(ch));
			ret = IRQ_HANDLED;
		}
	}

	if (pending & SPDIFRX_IR_OVERRUN) {
		dev_warn(dev->dev, "Overrun detected\n");
		ret = IRQ_HANDLED;
	}

	return ret;
}

static int mchp_spdifrx_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	struct mchp_spdifrx_dev *dev = snd_soc_dai_get_drvdata(dai);
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		mutex_lock(&dev->mlock);
		/* Enable overrun interrupts */
		regmap_write(dev->regmap, SPDIFRX_IER, SPDIFRX_IR_OVERRUN);

		/* Enable receiver. */
		regmap_update_bits(dev->regmap, SPDIFRX_MR, SPDIFRX_MR_RXEN_MASK,
				   SPDIFRX_MR_RXEN_ENABLE);
		dev->trigger_enabled = true;
		mutex_unlock(&dev->mlock);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		mutex_lock(&dev->mlock);
		/* Disable overrun interrupts */
		regmap_write(dev->regmap, SPDIFRX_IDR, SPDIFRX_IR_OVERRUN);

		/* Disable receiver. */
		regmap_update_bits(dev->regmap, SPDIFRX_MR, SPDIFRX_MR_RXEN_MASK,
				   SPDIFRX_MR_RXEN_DISABLE);
		dev->trigger_enabled = false;
		mutex_unlock(&dev->mlock);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int mchp_spdifrx_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct mchp_spdifrx_dev *dev = snd_soc_dai_get_drvdata(dai);
	u32 mr = 0;
	int ret;

	dev_dbg(dev->dev, "%s() rate=%u format=%#x width=%u channels=%u\n",
		__func__, params_rate(params), params_format(params),
		params_width(params), params_channels(params));

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		dev_err(dev->dev, "Playback is not supported\n");
		return -EINVAL;
	}

	if (params_channels(params) != SPDIFRX_CHANNELS) {
		dev_err(dev->dev, "unsupported number of channels: %d\n",
			params_channels(params));
		return -EINVAL;
	}

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_BE:
	case SNDRV_PCM_FORMAT_S20_3BE:
	case SNDRV_PCM_FORMAT_S24_3BE:
	case SNDRV_PCM_FORMAT_S24_BE:
		mr |= SPDIFRX_MR_ENDIAN_BIG;
		fallthrough;
	case SNDRV_PCM_FORMAT_S16_LE:
	case SNDRV_PCM_FORMAT_S20_3LE:
	case SNDRV_PCM_FORMAT_S24_3LE:
	case SNDRV_PCM_FORMAT_S24_LE:
		mr |= SPDIFRX_MR_DATAWIDTH(params_width(params));
		break;
	default:
		dev_err(dev->dev, "unsupported PCM format: %d\n",
			params_format(params));
		return -EINVAL;
	}

	mutex_lock(&dev->mlock);
	if (dev->trigger_enabled) {
		dev_err(dev->dev, "PCM already running\n");
		ret = -EBUSY;
		goto unlock;
	}

	if (dev->gclk_enabled) {
		clk_disable_unprepare(dev->gclk);
		dev->gclk_enabled = 0;
	}
	ret = clk_set_min_rate(dev->gclk, params_rate(params) *
					  SPDIFRX_GCLK_RATIO_MIN + 1);
	if (ret) {
		dev_err(dev->dev,
			"unable to set gclk min rate: rate %u * ratio %u + 1\n",
			params_rate(params), SPDIFRX_GCLK_RATIO_MIN);
		goto unlock;
	}
	ret = clk_prepare_enable(dev->gclk);
	if (ret) {
		dev_err(dev->dev, "unable to enable gclk: %d\n", ret);
		goto unlock;
	}
	dev->gclk_enabled = 1;

	dev_dbg(dev->dev, "GCLK range min set to %d\n",
		params_rate(params) * SPDIFRX_GCLK_RATIO_MIN + 1);

	ret = regmap_write(dev->regmap, SPDIFRX_MR, mr);

unlock:
	mutex_unlock(&dev->mlock);

	return ret;
}

static int mchp_spdifrx_hw_free(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct mchp_spdifrx_dev *dev = snd_soc_dai_get_drvdata(dai);

	mutex_lock(&dev->mlock);
	if (dev->gclk_enabled) {
		clk_disable_unprepare(dev->gclk);
		dev->gclk_enabled = 0;
	}
	mutex_unlock(&dev->mlock);
	return 0;
}

static const struct snd_soc_dai_ops mchp_spdifrx_dai_ops = {
	.trigger	= mchp_spdifrx_trigger,
	.hw_params	= mchp_spdifrx_hw_params,
	.hw_free	= mchp_spdifrx_hw_free,
};

#define MCHP_SPDIF_RATES	SNDRV_PCM_RATE_8000_192000

#define MCHP_SPDIF_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE |	\
				 SNDRV_PCM_FMTBIT_U16_BE |	\
				 SNDRV_PCM_FMTBIT_S20_3LE |	\
				 SNDRV_PCM_FMTBIT_S20_3BE |	\
				 SNDRV_PCM_FMTBIT_S24_3LE |	\
				 SNDRV_PCM_FMTBIT_S24_3BE |	\
				 SNDRV_PCM_FMTBIT_S24_LE |	\
				 SNDRV_PCM_FMTBIT_S24_BE	\
				)

static int mchp_spdifrx_info(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;

	return 0;
}

static int mchp_spdifrx_cs_get(struct mchp_spdifrx_dev *dev,
			       int channel,
			       struct snd_ctl_elem_value *uvalue)
{
	struct mchp_spdifrx_mixer_control *ctrl = &dev->control;
	struct mchp_spdifrx_ch_stat *ch_stat = &ctrl->ch_stat[channel];
	int ret = 0;

	mutex_lock(&dev->mlock);

	/*
	 * We may reach this point with both clocks enabled but the receiver
	 * still disabled. To void waiting for completion and return with
	 * timeout check the dev->trigger_enabled.
	 *
	 * To retrieve data:
	 * - if the receiver is enabled CSC IRQ will update the data in software
	 *   caches (ch_stat->data)
	 * - otherwise we just update it here the software caches with latest
	 *   available information and return it; in this case we don't need
	 *   spin locking as the IRQ is disabled and will not be raised from
	 *   anywhere else.
	 */

	if (dev->trigger_enabled) {
		reinit_completion(&ch_stat->done);
		regmap_write(dev->regmap, SPDIFRX_IER, SPDIFRX_IR_CSC(channel));
		/* Check for new data available */
		ret = wait_for_completion_interruptible_timeout(&ch_stat->done,
								msecs_to_jiffies(100));
		/* Valid stream might not be present */
		if (ret <= 0) {
			dev_dbg(dev->dev, "channel status for channel %d timeout\n",
				channel);
			regmap_write(dev->regmap, SPDIFRX_IDR, SPDIFRX_IR_CSC(channel));
			ret = ret ? : -ETIMEDOUT;
			goto unlock;
		} else {
			ret = 0;
		}
	} else {
		/* Update software cache with latest channel status. */
		mchp_spdifrx_channel_status_read(dev, channel);
	}

	memcpy(uvalue->value.iec958.status, ch_stat->data,
	       sizeof(ch_stat->data));

unlock:
	mutex_unlock(&dev->mlock);
	return ret;
}

static int mchp_spdifrx_cs1_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *uvalue)
{
	struct snd_soc_dai *dai = snd_kcontrol_chip(kcontrol);
	struct mchp_spdifrx_dev *dev = snd_soc_dai_get_drvdata(dai);

	return mchp_spdifrx_cs_get(dev, 0, uvalue);
}

static int mchp_spdifrx_cs2_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *uvalue)
{
	struct snd_soc_dai *dai = snd_kcontrol_chip(kcontrol);
	struct mchp_spdifrx_dev *dev = snd_soc_dai_get_drvdata(dai);

	return mchp_spdifrx_cs_get(dev, 1, uvalue);
}

static int mchp_spdifrx_cs_mask(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *uvalue)
{
	memset(uvalue->value.iec958.status, 0xff,
	       sizeof(uvalue->value.iec958.status));

	return 0;
}

static int mchp_spdifrx_subcode_ch_get(struct mchp_spdifrx_dev *dev,
				       int channel,
				       struct snd_ctl_elem_value *uvalue)
{
	struct mchp_spdifrx_mixer_control *ctrl = &dev->control;
	struct mchp_spdifrx_user_data *user_data = &ctrl->user_data[channel];
	int ret = 0;

	mutex_lock(&dev->mlock);

	/*
	 * We may reach this point with both clocks enabled but the receiver
	 * still disabled. To void waiting for completion to just timeout we
	 * check here the dev->trigger_enabled flag.
	 *
	 * To retrieve data:
	 * - if the receiver is enabled we need to wait for blockend IRQ to read
	 *   data to and update it for us in software caches
	 * - otherwise reading the SPDIFRX_CHUD() registers is enough.
	 */

	if (dev->trigger_enabled) {
		reinit_completion(&user_data->done);
		regmap_write(dev->regmap, SPDIFRX_IER, SPDIFRX_IR_BLOCKEND);
		ret = wait_for_completion_interruptible_timeout(&user_data->done,
								msecs_to_jiffies(100));
		/* Valid stream might not be present. */
		if (ret <= 0) {
			dev_dbg(dev->dev, "user data for channel %d timeout\n",
				channel);
			regmap_write(dev->regmap, SPDIFRX_IDR, SPDIFRX_IR_BLOCKEND);
			ret = ret ? : -ETIMEDOUT;
			goto unlock;
		} else {
			ret = 0;
		}
	} else {
		/* Update software cache with last available data. */
		mchp_spdifrx_channel_user_data_read(dev, channel);
	}

	memcpy(uvalue->value.iec958.subcode, user_data->data,
	       sizeof(user_data->data));

unlock:
	mutex_unlock(&dev->mlock);
	return ret;
}

static int mchp_spdifrx_subcode_ch1_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *uvalue)
{
	struct snd_soc_dai *dai = snd_kcontrol_chip(kcontrol);
	struct mchp_spdifrx_dev *dev = snd_soc_dai_get_drvdata(dai);

	return mchp_spdifrx_subcode_ch_get(dev, 0, uvalue);
}

static int mchp_spdifrx_subcode_ch2_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *uvalue)
{
	struct snd_soc_dai *dai = snd_kcontrol_chip(kcontrol);
	struct mchp_spdifrx_dev *dev = snd_soc_dai_get_drvdata(dai);

	return mchp_spdifrx_subcode_ch_get(dev, 1, uvalue);
}

static int mchp_spdifrx_boolean_info(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;

	return 0;
}

static int mchp_spdifrx_ulock_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *uvalue)
{
	struct snd_soc_dai *dai = snd_kcontrol_chip(kcontrol);
	struct mchp_spdifrx_dev *dev = snd_soc_dai_get_drvdata(dai);
	struct mchp_spdifrx_mixer_control *ctrl = &dev->control;
	u32 val;
	bool ulock_old = ctrl->ulock;

	mutex_lock(&dev->mlock);

	/*
	 * The RSR.ULOCK has wrong value if both pclk and gclk are enabled
	 * and the receiver is disabled. Thus we take into account the
	 * dev->trigger_enabled here to return a real status.
	 */
	if (dev->trigger_enabled) {
		regmap_read(dev->regmap, SPDIFRX_RSR, &val);
		ctrl->ulock = !(val & SPDIFRX_RSR_ULOCK);
	} else {
		ctrl->ulock = 0;
	}

	uvalue->value.integer.value[0] = ctrl->ulock;

	mutex_unlock(&dev->mlock);

	return ulock_old != ctrl->ulock;
}

static int mchp_spdifrx_badf_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *uvalue)
{
	struct snd_soc_dai *dai = snd_kcontrol_chip(kcontrol);
	struct mchp_spdifrx_dev *dev = snd_soc_dai_get_drvdata(dai);
	struct mchp_spdifrx_mixer_control *ctrl = &dev->control;
	u32 val;
	bool badf_old = ctrl->badf;

	mutex_lock(&dev->mlock);

	/*
	 * The RSR.ULOCK has wrong value if both pclk and gclk are enabled
	 * and the receiver is disabled. Thus we take into account the
	 * dev->trigger_enabled here to return a real status.
	 */
	if (dev->trigger_enabled) {
		regmap_read(dev->regmap, SPDIFRX_RSR, &val);
		ctrl->badf = !!(val & SPDIFRX_RSR_BADF);
	} else {
		ctrl->badf = 0;
	}

	mutex_unlock(&dev->mlock);

	uvalue->value.integer.value[0] = ctrl->badf;

	return badf_old != ctrl->badf;
}

static int mchp_spdifrx_signal_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *uvalue)
{
	struct snd_soc_dai *dai = snd_kcontrol_chip(kcontrol);
	struct mchp_spdifrx_dev *dev = snd_soc_dai_get_drvdata(dai);
	struct mchp_spdifrx_mixer_control *ctrl = &dev->control;
	u32 val = ~0U, loops = 10;
	int ret;
	bool signal_old = ctrl->signal;

	mutex_lock(&dev->mlock);

	/*
	 * To get the signal we need to have receiver enabled. This
	 * could be enabled also from trigger() function thus we need to
	 * take care of not disabling the receiver when it runs.
	 */
	if (!dev->trigger_enabled) {
		ret = clk_prepare_enable(dev->gclk);
		if (ret)
			goto unlock;

		regmap_update_bits(dev->regmap, SPDIFRX_MR, SPDIFRX_MR_RXEN_MASK,
				   SPDIFRX_MR_RXEN_ENABLE);

		/* Wait for RSR.ULOCK bit. */
		while (--loops) {
			regmap_read(dev->regmap, SPDIFRX_RSR, &val);
			if (!(val & SPDIFRX_RSR_ULOCK))
				break;
			usleep_range(100, 150);
		}

		regmap_update_bits(dev->regmap, SPDIFRX_MR, SPDIFRX_MR_RXEN_MASK,
				   SPDIFRX_MR_RXEN_DISABLE);

		clk_disable_unprepare(dev->gclk);
	} else {
		regmap_read(dev->regmap, SPDIFRX_RSR, &val);
	}

unlock:
	mutex_unlock(&dev->mlock);

	if (!(val & SPDIFRX_RSR_ULOCK))
		ctrl->signal = !(val & SPDIFRX_RSR_NOSIGNAL);
	else
		ctrl->signal = 0;
	uvalue->value.integer.value[0] = ctrl->signal;

	return signal_old != ctrl->signal;
}

static int mchp_spdifrx_rate_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 192000;

	return 0;
}

static int mchp_spdifrx_rate_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *dai = snd_kcontrol_chip(kcontrol);
	struct mchp_spdifrx_dev *dev = snd_soc_dai_get_drvdata(dai);
	u32 val;
	int rate;

	mutex_lock(&dev->mlock);

	/*
	 * The RSR.ULOCK has wrong value if both pclk and gclk are enabled
	 * and the receiver is disabled. Thus we take into account the
	 * dev->trigger_enabled here to return a real status.
	 */
	if (dev->trigger_enabled) {
		regmap_read(dev->regmap, SPDIFRX_RSR, &val);
		/* If the receiver is not locked, ISF data is invalid. */
		if (val & SPDIFRX_RSR_ULOCK || !(val & SPDIFRX_RSR_IFS_MASK)) {
			ucontrol->value.integer.value[0] = 0;
			goto unlock;
		}
	} else {
		/* Reveicer is not locked, IFS data is invalid. */
		ucontrol->value.integer.value[0] = 0;
		goto unlock;
	}

	rate = clk_get_rate(dev->gclk);

	ucontrol->value.integer.value[0] = rate / (32 * SPDIFRX_RSR_IFS(val));

unlock:
	mutex_unlock(&dev->mlock);
	return 0;
}

static struct snd_kcontrol_new mchp_spdifrx_ctrls[] = {
	/* Channel status controller */
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = SNDRV_CTL_NAME_IEC958("", CAPTURE, DEFAULT)
			" Channel 1",
		.access = SNDRV_CTL_ELEM_ACCESS_READ |
			SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = mchp_spdifrx_info,
		.get = mchp_spdifrx_cs1_get,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = SNDRV_CTL_NAME_IEC958("", CAPTURE, DEFAULT)
			" Channel 2",
		.access = SNDRV_CTL_ELEM_ACCESS_READ |
			SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = mchp_spdifrx_info,
		.get = mchp_spdifrx_cs2_get,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = SNDRV_CTL_NAME_IEC958("", CAPTURE, MASK),
		.access = SNDRV_CTL_ELEM_ACCESS_READ,
		.info = mchp_spdifrx_info,
		.get = mchp_spdifrx_cs_mask,
	},
	/* User bits controller */
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = "IEC958 Subcode Capture Default Channel 1",
		.access = SNDRV_CTL_ELEM_ACCESS_READ |
			SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = mchp_spdifrx_info,
		.get = mchp_spdifrx_subcode_ch1_get,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = "IEC958 Subcode Capture Default Channel 2",
		.access = SNDRV_CTL_ELEM_ACCESS_READ |
			SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = mchp_spdifrx_info,
		.get = mchp_spdifrx_subcode_ch2_get,
	},
	/* Lock status */
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = SNDRV_CTL_NAME_IEC958("", CAPTURE, NONE) "Unlocked",
		.access = SNDRV_CTL_ELEM_ACCESS_READ |
			SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = mchp_spdifrx_boolean_info,
		.get = mchp_spdifrx_ulock_get,
	},
	/* Bad format */
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = SNDRV_CTL_NAME_IEC958("", CAPTURE, NONE)"Bad Format",
		.access = SNDRV_CTL_ELEM_ACCESS_READ |
			SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = mchp_spdifrx_boolean_info,
		.get = mchp_spdifrx_badf_get,
	},
	/* Signal */
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = SNDRV_CTL_NAME_IEC958("", CAPTURE, NONE) "Signal",
		.access = SNDRV_CTL_ELEM_ACCESS_READ |
			SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = mchp_spdifrx_boolean_info,
		.get = mchp_spdifrx_signal_get,
	},
	/* Sampling rate */
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = SNDRV_CTL_NAME_IEC958("", CAPTURE, NONE) "Rate",
		.access = SNDRV_CTL_ELEM_ACCESS_READ |
			SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = mchp_spdifrx_rate_info,
		.get = mchp_spdifrx_rate_get,
	},
};

static int mchp_spdifrx_dai_probe(struct snd_soc_dai *dai)
{
	struct mchp_spdifrx_dev *dev = snd_soc_dai_get_drvdata(dai);
	struct mchp_spdifrx_mixer_control *ctrl = &dev->control;
	int ch;
	int err;

	err = clk_prepare_enable(dev->pclk);
	if (err) {
		dev_err(dev->dev,
			"failed to enable the peripheral clock: %d\n", err);
		return err;
	}

	snd_soc_dai_init_dma_data(dai, NULL, &dev->capture);

	/* Software reset the IP */
	regmap_write(dev->regmap, SPDIFRX_CR, SPDIFRX_CR_SWRST);

	/* Default configuration */
	regmap_write(dev->regmap, SPDIFRX_MR,
		     SPDIFRX_MR_VBMODE_DISCARD_IF_VB1 |
		     SPDIFRX_MR_SBMODE_DISCARD |
		     SPDIFRX_MR_AUTORST_NOACTION |
		     SPDIFRX_MR_PACK_DISABLED);

	for (ch = 0; ch < SPDIFRX_CHANNELS; ch++) {
		init_completion(&ctrl->ch_stat[ch].done);
		init_completion(&ctrl->user_data[ch].done);
	}

	/* Add controls */
	snd_soc_add_dai_controls(dai, mchp_spdifrx_ctrls,
				 ARRAY_SIZE(mchp_spdifrx_ctrls));

	return 0;
}

static int mchp_spdifrx_dai_remove(struct snd_soc_dai *dai)
{
	struct mchp_spdifrx_dev *dev = snd_soc_dai_get_drvdata(dai);

	/* Disable interrupts */
	regmap_write(dev->regmap, SPDIFRX_IDR, GENMASK(14, 0));

	clk_disable_unprepare(dev->pclk);

	return 0;
}

static struct snd_soc_dai_driver mchp_spdifrx_dai = {
	.name = "mchp-spdifrx",
	.probe	= mchp_spdifrx_dai_probe,
	.remove	= mchp_spdifrx_dai_remove,
	.capture = {
		.stream_name = "S/PDIF Capture",
		.channels_min = SPDIFRX_CHANNELS,
		.channels_max = SPDIFRX_CHANNELS,
		.rates = MCHP_SPDIF_RATES,
		.formats = MCHP_SPDIF_FORMATS,
	},
	.ops = &mchp_spdifrx_dai_ops,
};

static const struct snd_soc_component_driver mchp_spdifrx_component = {
	.name		= "mchp-spdifrx",
};

static const struct of_device_id mchp_spdifrx_dt_ids[] = {
	{
		.compatible = "microchip,sama7g5-spdifrx",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mchp_spdifrx_dt_ids);

static int mchp_spdifrx_probe(struct platform_device *pdev)
{
	struct mchp_spdifrx_dev *dev;
	struct resource *mem;
	struct regmap *regmap;
	void __iomem *base;
	int irq;
	int err;
	u32 vers;

	/* Get memory for driver data. */
	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	/* Map I/O registers. */
	base = devm_platform_get_and_ioremap_resource(pdev, 0, &mem);
	if (IS_ERR(base))
		return PTR_ERR(base);

	regmap = devm_regmap_init_mmio(&pdev->dev, base,
				       &mchp_spdifrx_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	/* Request IRQ. */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	err = devm_request_irq(&pdev->dev, irq, mchp_spdif_interrupt, 0,
			       dev_name(&pdev->dev), dev);
	if (err)
		return err;

	/* Get the peripheral clock */
	dev->pclk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(dev->pclk)) {
		err = PTR_ERR(dev->pclk);
		dev_err(&pdev->dev, "failed to get the peripheral clock: %d\n",
			err);
		return err;
	}

	/* Get the generated clock */
	dev->gclk = devm_clk_get(&pdev->dev, "gclk");
	if (IS_ERR(dev->gclk)) {
		err = PTR_ERR(dev->gclk);
		dev_err(&pdev->dev,
			"failed to get the PMC generated clock: %d\n", err);
		return err;
	}

	/*
	 * Signal control need a valid rate on gclk. hw_params() configures
	 * it propertly but requesting signal before any hw_params() has been
	 * called lead to invalid value returned for signal. Thus, configure
	 * gclk at a valid rate, here, in initialization, to simplify the
	 * control path.
	 */
	clk_set_min_rate(dev->gclk, 48000 * SPDIFRX_GCLK_RATIO_MIN + 1);

	mutex_init(&dev->mlock);

	dev->dev = &pdev->dev;
	dev->regmap = regmap;
	platform_set_drvdata(pdev, dev);

	dev->capture.addr	= (dma_addr_t)mem->start + SPDIFRX_RHR;
	dev->capture.maxburst	= 1;

	err = devm_snd_dmaengine_pcm_register(&pdev->dev, NULL, 0);
	if (err) {
		dev_err(&pdev->dev, "failed to register PMC: %d\n", err);
		return err;
	}

	err = devm_snd_soc_register_component(&pdev->dev,
					      &mchp_spdifrx_component,
					      &mchp_spdifrx_dai, 1);
	if (err) {
		dev_err(&pdev->dev, "fail to register dai\n");
		return err;
	}

	regmap_read(regmap, SPDIFRX_VERSION, &vers);
	dev_info(&pdev->dev, "hw version: %#lx\n", vers & SPDIFRX_VERSION_MASK);

	return 0;
}

static struct platform_driver mchp_spdifrx_driver = {
	.probe	= mchp_spdifrx_probe,
	.driver	= {
		.name	= "mchp_spdifrx",
		.of_match_table = of_match_ptr(mchp_spdifrx_dt_ids),
	},
};

module_platform_driver(mchp_spdifrx_driver);

MODULE_AUTHOR("Codrin Ciubotariu <codrin.ciubotariu@microchip.com>");
MODULE_DESCRIPTION("Microchip S/PDIF RX Controller Driver");
MODULE_LICENSE("GPL v2");
