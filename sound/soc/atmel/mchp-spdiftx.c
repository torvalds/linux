// SPDX-License-Identifier: GPL-2.0
//
// Driver for Microchip S/PDIF TX Controller
//
// Copyright (C) 2020 Microchip Technology Inc. and its subsidiaries
//
// Author: Codrin Ciubotariu <codrin.ciubotariu@microchip.com>

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/spinlock.h>

#include <sound/asoundef.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

/*
 * ---- S/PDIF Transmitter Controller Register map ----
 */
#define SPDIFTX_CR			0x00	/* Control Register */
#define SPDIFTX_MR			0x04	/* Mode Register */
#define SPDIFTX_CDR			0x0C	/* Common Data Register */

#define SPDIFTX_IER			0x14	/* Interrupt Enable Register */
#define SPDIFTX_IDR			0x18	/* Interrupt Disable Register */
#define SPDIFTX_IMR			0x1C	/* Interrupt Mask Register */
#define SPDIFTX_ISR			0x20	/* Interrupt Status Register */

#define SPDIFTX_CH1UD(reg)	(0x50 + (reg) * 4)	/* User Data 1 Register x */
#define SPDIFTX_CH1S(reg)	(0x80 + (reg) * 4)	/* Channel Status 1 Register x */

#define SPDIFTX_VERSION			0xF0

/*
 * ---- Control Register (Write-only) ----
 */
#define SPDIFTX_CR_SWRST		BIT(0)	/* Software Reset */
#define SPDIFTX_CR_FCLR			BIT(1)	/* FIFO clear */

/*
 * ---- Mode Register (Read/Write) ----
 */
/* Transmit Enable */
#define SPDIFTX_MR_TXEN_MASK		GENMASK(0, 0)
#define SPDIFTX_MR_TXEN_DISABLE		(0 << 0)
#define SPDIFTX_MR_TXEN_ENABLE		(1 << 0)

/* Multichannel Transfer */
#define SPDIFTX_MR_MULTICH_MASK		GENAMSK(1, 1)
#define SPDIFTX_MR_MULTICH_MONO		(0 << 1)
#define SPDIFTX_MR_MULTICH_DUAL		(1 << 1)

/* Data Word Endian Mode */
#define SPDIFTX_MR_ENDIAN_MASK		GENMASK(2, 2)
#define SPDIFTX_MR_ENDIAN_LITTLE	(0 << 2)
#define SPDIFTX_MR_ENDIAN_BIG		(1 << 2)

/* Data Justification */
#define SPDIFTX_MR_JUSTIFY_MASK		GENMASK(3, 3)
#define SPDIFTX_MR_JUSTIFY_LSB		(0 << 3)
#define SPDIFTX_MR_JUSTIFY_MSB		(1 << 3)

/* Common Audio Register Transfer Mode */
#define SPDIFTX_MR_CMODE_MASK			GENMASK(5, 4)
#define SPDIFTX_MR_CMODE_INDEX_ACCESS		(0 << 4)
#define SPDIFTX_MR_CMODE_TOGGLE_ACCESS		(1 << 4)
#define SPDIFTX_MR_CMODE_INTERLVD_ACCESS	(2 << 4)

/* Valid Bits per Sample */
#define SPDIFTX_MR_VBPS_MASK		GENMASK(13, 8)
#define SPDIFTX_MR_VBPS(bps)		(((bps) << 8) & SPDIFTX_MR_VBPS_MASK)

/* Chunk Size */
#define SPDIFTX_MR_CHUNK_MASK		GENMASK(19, 16)
#define SPDIFTX_MR_CHUNK(size)		(((size) << 16) & SPDIFTX_MR_CHUNK_MASK)

/* Validity Bits for Channels 1 and 2 */
#define SPDIFTX_MR_VALID1			BIT(24)
#define SPDIFTX_MR_VALID2			BIT(25)

/* Disable Null Frame on underrun */
#define SPDIFTX_MR_DNFR_MASK		GENMASK(27, 27)
#define SPDIFTX_MR_DNFR_INVALID		(0 << 27)
#define SPDIFTX_MR_DNFR_VALID		(1 << 27)

/* Bytes per Sample */
#define SPDIFTX_MR_BPS_MASK		GENMASK(29, 28)
#define SPDIFTX_MR_BPS(bytes) \
	((((bytes) - 1) << 28) & SPDIFTX_MR_BPS_MASK)

/*
 * ---- Interrupt Enable/Disable/Mask/Status Register (Write/Read-only) ----
 */
#define SPDIFTX_IR_TXRDY		BIT(0)
#define SPDIFTX_IR_TXEMPTY		BIT(1)
#define SPDIFTX_IR_TXFULL		BIT(2)
#define SPDIFTX_IR_TXCHUNK		BIT(3)
#define SPDIFTX_IR_TXUDR		BIT(4)
#define SPDIFTX_IR_TXOVR		BIT(5)
#define SPDIFTX_IR_CSRDY		BIT(6)
#define SPDIFTX_IR_UDRDY		BIT(7)
#define SPDIFTX_IR_TXRDYCH(ch)		BIT((ch) + 8)
#define SPDIFTX_IR_SECE			BIT(10)
#define SPDIFTX_IR_TXUDRCH(ch)		BIT((ch) + 11)
#define SPDIFTX_IR_BEND			BIT(13)

static bool mchp_spdiftx_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SPDIFTX_MR:
	case SPDIFTX_IMR:
	case SPDIFTX_ISR:
	case SPDIFTX_CH1UD(0):
	case SPDIFTX_CH1UD(1):
	case SPDIFTX_CH1UD(2):
	case SPDIFTX_CH1UD(3):
	case SPDIFTX_CH1UD(4):
	case SPDIFTX_CH1UD(5):
	case SPDIFTX_CH1S(0):
	case SPDIFTX_CH1S(1):
	case SPDIFTX_CH1S(2):
	case SPDIFTX_CH1S(3):
	case SPDIFTX_CH1S(4):
	case SPDIFTX_CH1S(5):
		return true;
	default:
		return false;
	}
}

static bool mchp_spdiftx_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SPDIFTX_CR:
	case SPDIFTX_MR:
	case SPDIFTX_CDR:
	case SPDIFTX_IER:
	case SPDIFTX_IDR:
	case SPDIFTX_CH1UD(0):
	case SPDIFTX_CH1UD(1):
	case SPDIFTX_CH1UD(2):
	case SPDIFTX_CH1UD(3):
	case SPDIFTX_CH1UD(4):
	case SPDIFTX_CH1UD(5):
	case SPDIFTX_CH1S(0):
	case SPDIFTX_CH1S(1):
	case SPDIFTX_CH1S(2):
	case SPDIFTX_CH1S(3):
	case SPDIFTX_CH1S(4):
	case SPDIFTX_CH1S(5):
		return true;
	default:
		return false;
	}
}

static bool mchp_spdiftx_precious_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SPDIFTX_CDR:
	case SPDIFTX_ISR:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config mchp_spdiftx_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = SPDIFTX_VERSION,
	.readable_reg = mchp_spdiftx_readable_reg,
	.writeable_reg = mchp_spdiftx_writeable_reg,
	.precious_reg = mchp_spdiftx_precious_reg,
	.cache_type = REGCACHE_FLAT,
};

#define SPDIFTX_GCLK_RATIO	128

#define SPDIFTX_CS_BITS		192
#define SPDIFTX_UD_BITS		192

struct mchp_spdiftx_mixer_control {
	unsigned char				ch_stat[SPDIFTX_CS_BITS / 8];
	unsigned char				user_data[SPDIFTX_UD_BITS / 8];
	spinlock_t				lock; /* exclusive access to control data */
};

struct mchp_spdiftx_dev {
	struct mchp_spdiftx_mixer_control	control;
	struct snd_dmaengine_dai_dma_data	playback;
	struct device				*dev;
	struct regmap				*regmap;
	struct clk				*pclk;
	struct clk				*gclk;
	unsigned int				fmt;
	unsigned int				suspend_irq;
};

static inline int mchp_spdiftx_is_running(struct mchp_spdiftx_dev *dev)
{
	u32 mr;

	regmap_read(dev->regmap, SPDIFTX_MR, &mr);
	return !!(mr & SPDIFTX_MR_TXEN_ENABLE);
}

static void mchp_spdiftx_channel_status_write(struct mchp_spdiftx_dev *dev)
{
	struct mchp_spdiftx_mixer_control *ctrl = &dev->control;
	u32 val;
	int i;

	for (i = 0; i < ARRAY_SIZE(ctrl->ch_stat) / 4; i++) {
		val = (ctrl->ch_stat[(i * 4) + 0] << 0) |
		      (ctrl->ch_stat[(i * 4) + 1] << 8) |
		      (ctrl->ch_stat[(i * 4) + 2] << 16) |
		      (ctrl->ch_stat[(i * 4) + 3] << 24);

		regmap_write(dev->regmap, SPDIFTX_CH1S(i), val);
	}
}

static void mchp_spdiftx_user_data_write(struct mchp_spdiftx_dev *dev)
{
	struct mchp_spdiftx_mixer_control *ctrl = &dev->control;
	u32 val;
	int i;

	for (i = 0; i < ARRAY_SIZE(ctrl->user_data) / 4; i++) {
		val = (ctrl->user_data[(i * 4) + 0] << 0) |
		      (ctrl->user_data[(i * 4) + 1] << 8) |
		      (ctrl->user_data[(i * 4) + 2] << 16) |
		      (ctrl->user_data[(i * 4) + 3] << 24);

		regmap_write(dev->regmap, SPDIFTX_CH1UD(i), val);
	}
}

static irqreturn_t mchp_spdiftx_interrupt(int irq, void *dev_id)
{
	struct mchp_spdiftx_dev *dev = dev_id;
	struct mchp_spdiftx_mixer_control *ctrl = &dev->control;
	u32 sr, imr, pending, idr = 0;

	regmap_read(dev->regmap, SPDIFTX_ISR, &sr);
	regmap_read(dev->regmap, SPDIFTX_IMR, &imr);
	pending = sr & imr;

	if (!pending)
		return IRQ_NONE;

	if (pending & SPDIFTX_IR_TXUDR) {
		dev_warn(dev->dev, "underflow detected\n");
		idr |= SPDIFTX_IR_TXUDR;
	}

	if (pending & SPDIFTX_IR_TXOVR) {
		dev_warn(dev->dev, "overflow detected\n");
		idr |= SPDIFTX_IR_TXOVR;
	}

	if (pending & SPDIFTX_IR_UDRDY) {
		spin_lock(&ctrl->lock);
		mchp_spdiftx_user_data_write(dev);
		spin_unlock(&ctrl->lock);
		idr |= SPDIFTX_IR_UDRDY;
	}

	if (pending & SPDIFTX_IR_CSRDY) {
		spin_lock(&ctrl->lock);
		mchp_spdiftx_channel_status_write(dev);
		spin_unlock(&ctrl->lock);
		idr |= SPDIFTX_IR_CSRDY;
	}

	regmap_write(dev->regmap, SPDIFTX_IDR, idr);

	return IRQ_HANDLED;
}

static int mchp_spdiftx_dai_startup(struct snd_pcm_substream *substream,
				    struct snd_soc_dai *dai)
{
	struct mchp_spdiftx_dev *dev = snd_soc_dai_get_drvdata(dai);

	/* Software reset the IP */
	regmap_write(dev->regmap, SPDIFTX_CR,
		     SPDIFTX_CR_SWRST | SPDIFTX_CR_FCLR);

	return 0;
}

static void mchp_spdiftx_dai_shutdown(struct snd_pcm_substream *substream,
				      struct snd_soc_dai *dai)
{
	struct mchp_spdiftx_dev *dev = snd_soc_dai_get_drvdata(dai);

	/* Disable interrupts */
	regmap_write(dev->regmap, SPDIFTX_IDR, 0xffffffff);
}

static int mchp_spdiftx_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	struct mchp_spdiftx_dev *dev = snd_soc_dai_get_drvdata(dai);
	struct mchp_spdiftx_mixer_control *ctrl = &dev->control;
	u32 mr;
	int running;
	int ret;

	/* do not start/stop while channel status or user data is updated */
	spin_lock(&ctrl->lock);
	regmap_read(dev->regmap, SPDIFTX_MR, &mr);
	running = !!(mr & SPDIFTX_MR_TXEN_ENABLE);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_START:
		regmap_write(dev->regmap, SPDIFTX_IER, dev->suspend_irq |
			     SPDIFTX_IR_TXUDR | SPDIFTX_IR_TXOVR);
		dev->suspend_irq = 0;
		fallthrough;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (!running) {
			mr &= ~SPDIFTX_MR_TXEN_MASK;
			mr |= SPDIFTX_MR_TXEN_ENABLE;
		}
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
		regmap_read(dev->regmap, SPDIFTX_IMR, &dev->suspend_irq);
		fallthrough;
	case SNDRV_PCM_TRIGGER_STOP:
		regmap_write(dev->regmap, SPDIFTX_IDR, dev->suspend_irq |
			     SPDIFTX_IR_TXUDR | SPDIFTX_IR_TXOVR);
		fallthrough;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (running) {
			mr &= ~SPDIFTX_MR_TXEN_MASK;
			mr |= SPDIFTX_MR_TXEN_DISABLE;
		}
		break;
	default:
		spin_unlock(&ctrl->lock);
		return -EINVAL;
	}

	ret = regmap_write(dev->regmap, SPDIFTX_MR, mr);
	spin_unlock(&ctrl->lock);
	if (ret)
		dev_err(dev->dev, "unable to disable TX: %d\n", ret);

	return ret;
}

static int mchp_spdiftx_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	unsigned long flags;
	struct mchp_spdiftx_dev *dev = snd_soc_dai_get_drvdata(dai);
	struct mchp_spdiftx_mixer_control *ctrl = &dev->control;
	u32 mr;
	unsigned int bps = params_physical_width(params) / 8;
	unsigned char aes3;
	int ret;

	dev_dbg(dev->dev, "%s() rate=%u format=%#x width=%u channels=%u\n",
		__func__, params_rate(params), params_format(params),
		params_width(params), params_channels(params));

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		dev_err(dev->dev, "Capture is not supported\n");
		return -EINVAL;
	}

	regmap_read(dev->regmap, SPDIFTX_MR, &mr);

	if (mr & SPDIFTX_MR_TXEN_ENABLE) {
		dev_err(dev->dev, "PCM already running\n");
		return -EBUSY;
	}

	/* Defaults: Toggle mode, justify to LSB, chunksize 1 */
	mr = SPDIFTX_MR_CMODE_TOGGLE_ACCESS | SPDIFTX_MR_JUSTIFY_LSB;
	dev->playback.maxburst = 1;
	switch (params_channels(params)) {
	case 1:
		mr |= SPDIFTX_MR_MULTICH_MONO;
		break;
	case 2:
		mr |= SPDIFTX_MR_MULTICH_DUAL;
		if (bps > 2)
			dev->playback.maxburst = 2;
		break;
	default:
		dev_err(dev->dev, "unsupported number of channels: %d\n",
			params_channels(params));
		return -EINVAL;
	}
	mr |= SPDIFTX_MR_CHUNK(dev->playback.maxburst);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		mr |= SPDIFTX_MR_VBPS(8);
		break;
	case SNDRV_PCM_FORMAT_S16_BE:
		mr |= SPDIFTX_MR_ENDIAN_BIG;
		fallthrough;
	case SNDRV_PCM_FORMAT_S16_LE:
		mr |= SPDIFTX_MR_VBPS(16);
		break;
	case SNDRV_PCM_FORMAT_S18_3BE:
		mr |= SPDIFTX_MR_ENDIAN_BIG;
		fallthrough;
	case SNDRV_PCM_FORMAT_S18_3LE:
		mr |= SPDIFTX_MR_VBPS(18);
		break;
	case SNDRV_PCM_FORMAT_S20_3BE:
		mr |= SPDIFTX_MR_ENDIAN_BIG;
		fallthrough;
	case SNDRV_PCM_FORMAT_S20_3LE:
		mr |= SPDIFTX_MR_VBPS(20);
		break;
	case SNDRV_PCM_FORMAT_S24_3BE:
		mr |= SPDIFTX_MR_ENDIAN_BIG;
		fallthrough;
	case SNDRV_PCM_FORMAT_S24_3LE:
		mr |= SPDIFTX_MR_VBPS(24);
		break;
	case SNDRV_PCM_FORMAT_S24_BE:
		mr |= SPDIFTX_MR_ENDIAN_BIG;
		fallthrough;
	case SNDRV_PCM_FORMAT_S24_LE:
		mr |= SPDIFTX_MR_VBPS(24);
		break;
	case SNDRV_PCM_FORMAT_S32_BE:
		mr |= SPDIFTX_MR_ENDIAN_BIG;
		fallthrough;
	case SNDRV_PCM_FORMAT_S32_LE:
		mr |= SPDIFTX_MR_VBPS(32);
		break;
	default:
		dev_err(dev->dev, "unsupported PCM format: %d\n",
			params_format(params));
		return -EINVAL;
	}

	mr |= SPDIFTX_MR_BPS(bps);

	switch (params_rate(params)) {
	case 22050:
		aes3 = IEC958_AES3_CON_FS_22050;
		break;
	case 24000:
		aes3 = IEC958_AES3_CON_FS_24000;
		break;
	case 32000:
		aes3 = IEC958_AES3_CON_FS_32000;
		break;
	case 44100:
		aes3 = IEC958_AES3_CON_FS_44100;
		break;
	case 48000:
		aes3 = IEC958_AES3_CON_FS_48000;
		break;
	case 88200:
		aes3 = IEC958_AES3_CON_FS_88200;
		break;
	case 96000:
		aes3 = IEC958_AES3_CON_FS_96000;
		break;
	case 176400:
		aes3 = IEC958_AES3_CON_FS_176400;
		break;
	case 192000:
		aes3 = IEC958_AES3_CON_FS_192000;
		break;
	case 8000:
	case 11025:
	case 16000:
	case 64000:
		aes3 = IEC958_AES3_CON_FS_NOTID;
		break;
	default:
		dev_err(dev->dev, "unsupported sample frequency: %u\n",
			params_rate(params));
		return -EINVAL;
	}
	spin_lock_irqsave(&ctrl->lock, flags);
	ctrl->ch_stat[3] &= ~IEC958_AES3_CON_FS;
	ctrl->ch_stat[3] |= aes3;
	mchp_spdiftx_channel_status_write(dev);
	spin_unlock_irqrestore(&ctrl->lock, flags);

	/* GCLK is enabled by runtime PM. */
	clk_disable_unprepare(dev->gclk);

	ret = clk_set_rate(dev->gclk, params_rate(params) *
				      SPDIFTX_GCLK_RATIO);
	if (ret) {
		dev_err(dev->dev,
			"unable to change gclk rate to: rate %u * ratio %u\n",
			params_rate(params), SPDIFTX_GCLK_RATIO);
		return ret;
	}
	ret = clk_prepare_enable(dev->gclk);
	if (ret) {
		dev_err(dev->dev, "unable to enable gclk: %d\n", ret);
		return ret;
	}

	dev_dbg(dev->dev, "%s(): GCLK set to %d\n", __func__,
		params_rate(params) * SPDIFTX_GCLK_RATIO);

	regmap_write(dev->regmap, SPDIFTX_MR, mr);

	return 0;
}

static int mchp_spdiftx_hw_free(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct mchp_spdiftx_dev *dev = snd_soc_dai_get_drvdata(dai);

	return regmap_write(dev->regmap, SPDIFTX_CR,
			    SPDIFTX_CR_SWRST | SPDIFTX_CR_FCLR);
}

static const struct snd_soc_dai_ops mchp_spdiftx_dai_ops = {
	.startup	= mchp_spdiftx_dai_startup,
	.shutdown	= mchp_spdiftx_dai_shutdown,
	.trigger	= mchp_spdiftx_trigger,
	.hw_params	= mchp_spdiftx_hw_params,
	.hw_free	= mchp_spdiftx_hw_free,
};

#define MCHP_SPDIFTX_RATES	SNDRV_PCM_RATE_8000_192000

#define MCHP_SPDIFTX_FORMATS	(SNDRV_PCM_FMTBIT_S8 |		\
				 SNDRV_PCM_FMTBIT_S16_LE |	\
				 SNDRV_PCM_FMTBIT_U16_BE |	\
				 SNDRV_PCM_FMTBIT_S18_3LE |	\
				 SNDRV_PCM_FMTBIT_S18_3BE |	\
				 SNDRV_PCM_FMTBIT_S20_3LE |	\
				 SNDRV_PCM_FMTBIT_S20_3BE |	\
				 SNDRV_PCM_FMTBIT_S24_3LE |	\
				 SNDRV_PCM_FMTBIT_S24_3BE |	\
				 SNDRV_PCM_FMTBIT_S24_LE |	\
				 SNDRV_PCM_FMTBIT_S24_BE |	\
				 SNDRV_PCM_FMTBIT_S32_LE |	\
				 SNDRV_PCM_FMTBIT_S32_BE	\
				 )

static int mchp_spdiftx_info(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;

	return 0;
}

static int mchp_spdiftx_cs_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *uvalue)
{
	unsigned long flags;
	struct snd_soc_dai *dai = snd_kcontrol_chip(kcontrol);
	struct mchp_spdiftx_dev *dev = snd_soc_dai_get_drvdata(dai);
	struct mchp_spdiftx_mixer_control *ctrl = &dev->control;

	spin_lock_irqsave(&ctrl->lock, flags);
	memcpy(uvalue->value.iec958.status, ctrl->ch_stat,
	       sizeof(ctrl->ch_stat));
	spin_unlock_irqrestore(&ctrl->lock, flags);

	return 0;
}

static int mchp_spdiftx_cs_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *uvalue)
{
	unsigned long flags;
	struct snd_soc_dai *dai = snd_kcontrol_chip(kcontrol);
	struct mchp_spdiftx_dev *dev = snd_soc_dai_get_drvdata(dai);
	struct mchp_spdiftx_mixer_control *ctrl = &dev->control;
	int changed = 0;
	int i;

	spin_lock_irqsave(&ctrl->lock, flags);
	for (i = 0; i < ARRAY_SIZE(ctrl->ch_stat); i++) {
		if (ctrl->ch_stat[i] != uvalue->value.iec958.status[i])
			changed = 1;
		ctrl->ch_stat[i] = uvalue->value.iec958.status[i];
	}

	if (changed) {
		/* don't enable IP while we copy the channel status */
		if (mchp_spdiftx_is_running(dev)) {
			/*
			 * if SPDIF is running, wait for interrupt to write
			 * channel status
			 */
			regmap_write(dev->regmap, SPDIFTX_IER,
				     SPDIFTX_IR_CSRDY);
		} else {
			mchp_spdiftx_channel_status_write(dev);
		}
	}
	spin_unlock_irqrestore(&ctrl->lock, flags);

	return changed;
}

static int mchp_spdiftx_cs_mask(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *uvalue)
{
	memset(uvalue->value.iec958.status, 0xff,
	       sizeof(uvalue->value.iec958.status));

	return 0;
}

static int mchp_spdiftx_subcode_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *uvalue)
{
	struct snd_soc_dai *dai = snd_kcontrol_chip(kcontrol);
	struct mchp_spdiftx_dev *dev = snd_soc_dai_get_drvdata(dai);
	struct mchp_spdiftx_mixer_control *ctrl = &dev->control;
	unsigned long flags;

	spin_lock_irqsave(&ctrl->lock, flags);
	memcpy(uvalue->value.iec958.subcode, ctrl->user_data,
	       sizeof(ctrl->user_data));
	spin_unlock_irqrestore(&ctrl->lock, flags);

	return 0;
}

static int mchp_spdiftx_subcode_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *uvalue)
{
	unsigned long flags;
	struct snd_soc_dai *dai = snd_kcontrol_chip(kcontrol);
	struct mchp_spdiftx_dev *dev = snd_soc_dai_get_drvdata(dai);
	struct mchp_spdiftx_mixer_control *ctrl = &dev->control;
	int changed = 0;
	int i;

	spin_lock_irqsave(&ctrl->lock, flags);
	for (i = 0; i < ARRAY_SIZE(ctrl->user_data); i++) {
		if (ctrl->user_data[i] != uvalue->value.iec958.subcode[i])
			changed = 1;

		ctrl->user_data[i] = uvalue->value.iec958.subcode[i];
	}
	if (changed) {
		if (mchp_spdiftx_is_running(dev)) {
			/*
			 * if SPDIF is running, wait for interrupt to write
			 * user data
			 */
			regmap_write(dev->regmap, SPDIFTX_IER,
				     SPDIFTX_IR_UDRDY);
		} else {
			mchp_spdiftx_user_data_write(dev);
		}
	}
	spin_unlock_irqrestore(&ctrl->lock, flags);

	return changed;
}

static struct snd_kcontrol_new mchp_spdiftx_ctrls[] = {
	/* Channel status controller */
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = SNDRV_CTL_NAME_IEC958("", PLAYBACK, DEFAULT),
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
			SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = mchp_spdiftx_info,
		.get = mchp_spdiftx_cs_get,
		.put = mchp_spdiftx_cs_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = SNDRV_CTL_NAME_IEC958("", PLAYBACK, MASK),
		.access = SNDRV_CTL_ELEM_ACCESS_READ,
			SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = mchp_spdiftx_info,
		.get = mchp_spdiftx_cs_mask,
	},
	/* User bits controller */
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = "IEC958 Subcode Playback Default",
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = mchp_spdiftx_info,
		.get = mchp_spdiftx_subcode_get,
		.put = mchp_spdiftx_subcode_put,
	},
};

static int mchp_spdiftx_dai_probe(struct snd_soc_dai *dai)
{
	struct mchp_spdiftx_dev *dev = snd_soc_dai_get_drvdata(dai);

	snd_soc_dai_init_dma_data(dai, &dev->playback, NULL);

	/* Add controls */
	snd_soc_add_dai_controls(dai, mchp_spdiftx_ctrls,
				 ARRAY_SIZE(mchp_spdiftx_ctrls));

	return 0;
}

static struct snd_soc_dai_driver mchp_spdiftx_dai = {
	.name = "mchp-spdiftx",
	.probe	= mchp_spdiftx_dai_probe,
	.playback = {
		.stream_name = "S/PDIF Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = MCHP_SPDIFTX_RATES,
		.formats = MCHP_SPDIFTX_FORMATS,
	},
	.ops = &mchp_spdiftx_dai_ops,
};

static const struct snd_soc_component_driver mchp_spdiftx_component = {
	.name			= "mchp-spdiftx",
	.legacy_dai_naming	= 1,
};

static const struct of_device_id mchp_spdiftx_dt_ids[] = {
	{
		.compatible = "microchip,sama7g5-spdiftx",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mchp_spdiftx_dt_ids);

static int mchp_spdiftx_runtime_suspend(struct device *dev)
{
	struct mchp_spdiftx_dev *spdiftx = dev_get_drvdata(dev);

	regcache_cache_only(spdiftx->regmap, true);

	clk_disable_unprepare(spdiftx->gclk);
	clk_disable_unprepare(spdiftx->pclk);

	return 0;
}

static int mchp_spdiftx_runtime_resume(struct device *dev)
{
	struct mchp_spdiftx_dev *spdiftx = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(spdiftx->pclk);
	if (ret) {
		dev_err(spdiftx->dev,
			"failed to enable the peripheral clock: %d\n", ret);
		return ret;
	}
	ret = clk_prepare_enable(spdiftx->gclk);
	if (ret) {
		dev_err(spdiftx->dev,
			"failed to enable generic clock: %d\n", ret);
		goto disable_pclk;
	}

	regcache_cache_only(spdiftx->regmap, false);
	regcache_mark_dirty(spdiftx->regmap);
	ret = regcache_sync(spdiftx->regmap);
	if (ret) {
		regcache_cache_only(spdiftx->regmap, true);
		clk_disable_unprepare(spdiftx->gclk);
disable_pclk:
		clk_disable_unprepare(spdiftx->pclk);
	}

	return ret;
}

static const struct dev_pm_ops mchp_spdiftx_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)
	RUNTIME_PM_OPS(mchp_spdiftx_runtime_suspend, mchp_spdiftx_runtime_resume,
		       NULL)
};

static int mchp_spdiftx_probe(struct platform_device *pdev)
{
	struct mchp_spdiftx_dev *dev;
	struct resource *mem;
	struct regmap *regmap;
	void __iomem *base;
	struct mchp_spdiftx_mixer_control *ctrl;
	int irq;
	int err;

	/* Get memory for driver data. */
	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	/* Map I/O registers. */
	base = devm_platform_get_and_ioremap_resource(pdev, 0, &mem);
	if (IS_ERR(base))
		return PTR_ERR(base);

	regmap = devm_regmap_init_mmio(&pdev->dev, base,
				       &mchp_spdiftx_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	/* Request IRQ */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	err = devm_request_irq(&pdev->dev, irq, mchp_spdiftx_interrupt, 0,
			       dev_name(&pdev->dev), dev);
	if (err)
		return err;

	/* Get the peripheral clock */
	dev->pclk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(dev->pclk)) {
		err = PTR_ERR(dev->pclk);
		dev_err(&pdev->dev,
			"failed to get the peripheral clock: %d\n", err);
		return err;
	}

	/* Get the generic clock */
	dev->gclk = devm_clk_get(&pdev->dev, "gclk");
	if (IS_ERR(dev->gclk)) {
		err = PTR_ERR(dev->gclk);
		dev_err(&pdev->dev,
			"failed to get the PMC generic clock: %d\n", err);
		return err;
	}

	ctrl = &dev->control;
	spin_lock_init(&ctrl->lock);

	/* Init channel status */
	ctrl->ch_stat[0] = IEC958_AES0_CON_NOT_COPYRIGHT |
			   IEC958_AES0_CON_EMPHASIS_NONE;

	dev->dev = &pdev->dev;
	dev->regmap = regmap;
	platform_set_drvdata(pdev, dev);

	pm_runtime_enable(dev->dev);
	if (!pm_runtime_enabled(dev->dev)) {
		err = mchp_spdiftx_runtime_resume(dev->dev);
		if (err)
			return err;
	}

	dev->playback.addr = (dma_addr_t)mem->start + SPDIFTX_CDR;
	dev->playback.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;

	err = devm_snd_dmaengine_pcm_register(&pdev->dev, NULL, 0);
	if (err) {
		dev_err(&pdev->dev, "failed to register PMC: %d\n", err);
		goto pm_runtime_suspend;
	}

	err = devm_snd_soc_register_component(&pdev->dev,
					      &mchp_spdiftx_component,
					      &mchp_spdiftx_dai, 1);
	if (err) {
		dev_err(&pdev->dev, "failed to register component: %d\n", err);
		goto pm_runtime_suspend;
	}

	return 0;

pm_runtime_suspend:
	if (!pm_runtime_status_suspended(dev->dev))
		mchp_spdiftx_runtime_suspend(dev->dev);
	pm_runtime_disable(dev->dev);

	return err;
}

static int mchp_spdiftx_remove(struct platform_device *pdev)
{
	struct mchp_spdiftx_dev *dev = platform_get_drvdata(pdev);

	if (!pm_runtime_status_suspended(dev->dev))
		mchp_spdiftx_runtime_suspend(dev->dev);

	pm_runtime_disable(dev->dev);

	return 0;
}

static struct platform_driver mchp_spdiftx_driver = {
	.probe	= mchp_spdiftx_probe,
	.remove = mchp_spdiftx_remove,
	.driver	= {
		.name	= "mchp_spdiftx",
		.of_match_table = of_match_ptr(mchp_spdiftx_dt_ids),
		.pm = pm_ptr(&mchp_spdiftx_pm_ops)
	},
};

module_platform_driver(mchp_spdiftx_driver);

MODULE_AUTHOR("Codrin Ciubotariu <codrin.ciubotariu@microchip.com>");
MODULE_DESCRIPTION("Microchip S/PDIF TX Controller Driver");
MODULE_LICENSE("GPL v2");
