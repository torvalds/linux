/*
 * Copyright (C) STMicroelectronics SA 2015
 * Authors: Arnaud Pouliquen <arnaud.pouliquen@st.com>
 *          for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>

#include <sound/asoundef.h>
#include <sound/soc.h>

#include "uniperif.h"

/*
 * Some hardware-related definitions
 */

/* sys config registers definitions */
#define SYS_CFG_AUDIO_GLUE 0xA4

/*
 * Driver specific types.
 */

#define UNIPERIF_PLAYER_CLK_ADJ_MIN  -999999
#define UNIPERIF_PLAYER_CLK_ADJ_MAX  1000000
#define UNIPERIF_PLAYER_I2S_OUT 1 /* player id connected to I2S/TDM TX bus */

/*
 * Note: snd_pcm_hardware is linked to DMA controller but is declared here to
 * integrate  DAI_CPU capability in term of rate and supported channels
 */
static const struct snd_pcm_hardware uni_player_pcm_hw = {
	.info = SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID,
	.formats = SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S16_LE,

	.rates = SNDRV_PCM_RATE_CONTINUOUS,
	.rate_min = 8000,
	.rate_max = 192000,

	.channels_min = 2,
	.channels_max = 8,

	.periods_min = 2,
	.periods_max = 48,

	.period_bytes_min = 128,
	.period_bytes_max = 64 * PAGE_SIZE,
	.buffer_bytes_max = 256 * PAGE_SIZE
};

static inline int reset_player(struct uniperif *player)
{
	int count = 10;

	if (player->ver < SND_ST_UNIPERIF_VERSION_UNI_PLR_TOP_1_0) {
		while (GET_UNIPERIF_SOFT_RST_SOFT_RST(player) && count) {
			udelay(5);
			count--;
		}
	}

	if (!count) {
		dev_err(player->dev, "Failed to reset uniperif");
		return -EIO;
	}

	return 0;
}

/*
 * uni_player_irq_handler
 * In case of error audio stream is stopped; stop action is protected via PCM
 * stream lock to avoid race condition with trigger callback.
 */
static irqreturn_t uni_player_irq_handler(int irq, void *dev_id)
{
	irqreturn_t ret = IRQ_NONE;
	struct uniperif *player = dev_id;
	unsigned int status;
	unsigned int tmp;

	if (player->state == UNIPERIF_STATE_STOPPED) {
		/* Unexpected IRQ: do nothing */
		return IRQ_NONE;
	}

	/* Get interrupt status & clear them immediately */
	status = GET_UNIPERIF_ITS(player);
	SET_UNIPERIF_ITS_BCLR(player, status);

	/* Check for fifo error (underrun) */
	if (unlikely(status & UNIPERIF_ITS_FIFO_ERROR_MASK(player))) {
		dev_err(player->dev, "FIFO underflow error detected");

		/* Interrupt is just for information when underflow recovery */
		if (player->underflow_enabled) {
			/* Update state to underflow */
			player->state = UNIPERIF_STATE_UNDERFLOW;

		} else {
			/* Disable interrupt so doesn't continually fire */
			SET_UNIPERIF_ITM_BCLR_FIFO_ERROR(player);

			/* Stop the player */
			snd_pcm_stream_lock(player->substream);
			snd_pcm_stop(player->substream, SNDRV_PCM_STATE_XRUN);
			snd_pcm_stream_unlock(player->substream);
		}

		ret = IRQ_HANDLED;
	}

	/* Check for dma error (overrun) */
	if (unlikely(status & UNIPERIF_ITS_DMA_ERROR_MASK(player))) {
		dev_err(player->dev, "DMA error detected");

		/* Disable interrupt so doesn't continually fire */
		SET_UNIPERIF_ITM_BCLR_DMA_ERROR(player);

		/* Stop the player */
		snd_pcm_stream_lock(player->substream);
		snd_pcm_stop(player->substream, SNDRV_PCM_STATE_XRUN);
		snd_pcm_stream_unlock(player->substream);

		ret = IRQ_HANDLED;
	}

	/* Check for underflow recovery done */
	if (unlikely(status & UNIPERIF_ITM_UNDERFLOW_REC_DONE_MASK(player))) {
		if (!player->underflow_enabled) {
			dev_err(player->dev, "unexpected Underflow recovering");
			return -EPERM;
		}
		/* Read the underflow recovery duration */
		tmp = GET_UNIPERIF_STATUS_1_UNDERFLOW_DURATION(player);

		/* Clear the underflow recovery duration */
		SET_UNIPERIF_BIT_CONTROL_CLR_UNDERFLOW_DURATION(player);

		/* Update state to started */
		player->state = UNIPERIF_STATE_STARTED;

		ret = IRQ_HANDLED;
	}

	/* Check if underflow recovery failed */
	if (unlikely(status &
		     UNIPERIF_ITM_UNDERFLOW_REC_FAILED_MASK(player))) {
		dev_err(player->dev, "Underflow recovery failed");

		/* Stop the player */
		snd_pcm_stream_lock(player->substream);
		snd_pcm_stop(player->substream, SNDRV_PCM_STATE_XRUN);
		snd_pcm_stream_unlock(player->substream);

		ret = IRQ_HANDLED;
	}

	return ret;
}

static int uni_player_clk_set_rate(struct uniperif *player, unsigned long rate)
{
	int rate_adjusted, rate_achieved, delta, ret;
	int adjustment = player->clk_adj;

	/*
	 *             a
	 * F = f + --------- * f = f + d
	 *          1000000
	 *
	 *         a
	 * d = --------- * f
	 *      1000000
	 *
	 * where:
	 *   f - nominal rate
	 *   a - adjustment in ppm (parts per milion)
	 *   F - rate to be set in synthesizer
	 *   d - delta (difference) between f and F
	 */
	if (adjustment < 0) {
		/* div64_64 operates on unsigned values... */
		delta = -1;
		adjustment = -adjustment;
	} else {
		delta = 1;
	}
	/* 500000 ppm is 0.5, which is used to round up values */
	delta *= (int)div64_u64((uint64_t)rate *
				(uint64_t)adjustment + 500000, 1000000);
	rate_adjusted = rate + delta;

	/* Adjusted rate should never be == 0 */
	if (!rate_adjusted)
		return -EINVAL;

	ret = clk_set_rate(player->clk, rate_adjusted);
	if (ret < 0)
		return ret;

	rate_achieved = clk_get_rate(player->clk);
	if (!rate_achieved)
		/* If value is 0 means that clock or parent not valid */
		return -EINVAL;

	/*
	 * Using ALSA's adjustment control, we can modify the rate to be up
	 * to twice as much as requested, but no more
	 */
	delta = rate_achieved - rate;
	if (delta < 0) {
		/* div64_64 operates on unsigned values... */
		delta = -delta;
		adjustment = -1;
	} else {
		adjustment = 1;
	}
	/* Frequency/2 is added to round up result */
	adjustment *= (int)div64_u64((uint64_t)delta * 1000000 + rate / 2,
				     rate);
	player->clk_adj = adjustment;
	return 0;
}

static void uni_player_set_channel_status(struct uniperif *player,
					  struct snd_pcm_runtime *runtime)
{
	int n;
	unsigned int status;

	/*
	 * Some AVRs and TVs require the channel status to contain a correct
	 * sampling frequency. If no sample rate is already specified, then
	 * set one.
	 */
	mutex_lock(&player->ctrl_lock);
	if (runtime) {
		switch (runtime->rate) {
		case 22050:
			player->stream_settings.iec958.status[3] =
						IEC958_AES3_CON_FS_22050;
			break;
		case 44100:
			player->stream_settings.iec958.status[3] =
						IEC958_AES3_CON_FS_44100;
			break;
		case 88200:
			player->stream_settings.iec958.status[3] =
						IEC958_AES3_CON_FS_88200;
			break;
		case 176400:
			player->stream_settings.iec958.status[3] =
						IEC958_AES3_CON_FS_176400;
			break;
		case 24000:
			player->stream_settings.iec958.status[3] =
						IEC958_AES3_CON_FS_24000;
			break;
		case 48000:
			player->stream_settings.iec958.status[3] =
						IEC958_AES3_CON_FS_48000;
			break;
		case 96000:
			player->stream_settings.iec958.status[3] =
						IEC958_AES3_CON_FS_96000;
			break;
		case 192000:
			player->stream_settings.iec958.status[3] =
						IEC958_AES3_CON_FS_192000;
			break;
		case 32000:
			player->stream_settings.iec958.status[3] =
						IEC958_AES3_CON_FS_32000;
			break;
		default:
			/* Mark as sampling frequency not indicated */
			player->stream_settings.iec958.status[3] =
						IEC958_AES3_CON_FS_NOTID;
			break;
		}
	}

	/* Audio mode:
	 * Use audio mode status to select PCM or encoded mode
	 */
	if (player->stream_settings.iec958.status[0] & IEC958_AES0_NONAUDIO)
		player->stream_settings.encoding_mode =
			UNIPERIF_IEC958_ENCODING_MODE_ENCODED;
	else
		player->stream_settings.encoding_mode =
			UNIPERIF_IEC958_ENCODING_MODE_PCM;

	if (player->stream_settings.encoding_mode ==
		UNIPERIF_IEC958_ENCODING_MODE_PCM)
		/* Clear user validity bits */
		SET_UNIPERIF_USER_VALIDITY_VALIDITY_LR(player, 0);
	else
		/* Set user validity bits */
		SET_UNIPERIF_USER_VALIDITY_VALIDITY_LR(player, 1);

	/* Program the new channel status */
	for (n = 0; n < 6; ++n) {
		status  =
		player->stream_settings.iec958.status[0 + (n * 4)] & 0xf;
		status |=
		player->stream_settings.iec958.status[1 + (n * 4)] << 8;
		status |=
		player->stream_settings.iec958.status[2 + (n * 4)] << 16;
		status |=
		player->stream_settings.iec958.status[3 + (n * 4)] << 24;
		SET_UNIPERIF_CHANNEL_STA_REGN(player, n, status);
	}
	mutex_unlock(&player->ctrl_lock);

	/* Update the channel status */
	if (player->ver < SND_ST_UNIPERIF_VERSION_UNI_PLR_TOP_1_0)
		SET_UNIPERIF_CONFIG_CHL_STS_UPDATE(player);
	else
		SET_UNIPERIF_BIT_CONTROL_CHL_STS_UPDATE(player);
}

static int uni_player_prepare_iec958(struct uniperif *player,
				     struct snd_pcm_runtime *runtime)
{
	int clk_div;

	clk_div = player->mclk / runtime->rate;

	/* Oversampling must be multiple of 128 as iec958 frame is 32-bits */
	if ((clk_div % 128) || (clk_div <= 0)) {
		dev_err(player->dev, "%s: invalid clk_div %d",
			__func__, clk_div);
		return -EINVAL;
	}

	switch (runtime->format) {
	case SNDRV_PCM_FORMAT_S16_LE:
		/* 16/16 memory format */
		SET_UNIPERIF_CONFIG_MEM_FMT_16_16(player);
		/* 16-bits per sub-frame */
		SET_UNIPERIF_I2S_FMT_NBIT_32(player);
		/* Set 16-bit sample precision */
		SET_UNIPERIF_I2S_FMT_DATA_SIZE_16(player);
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		/* 16/0 memory format */
		SET_UNIPERIF_CONFIG_MEM_FMT_16_0(player);
		/* 32-bits per sub-frame */
		SET_UNIPERIF_I2S_FMT_NBIT_32(player);
		/* Set 24-bit sample precision */
		SET_UNIPERIF_I2S_FMT_DATA_SIZE_24(player);
		break;
	default:
		dev_err(player->dev, "format not supported");
		return -EINVAL;
	}

	/* Set parity to be calculated by the hardware */
	SET_UNIPERIF_CONFIG_PARITY_CNTR_BY_HW(player);

	/* Set channel status bits to be inserted by the hardware */
	SET_UNIPERIF_CONFIG_CHANNEL_STA_CNTR_BY_HW(player);

	/* Set user data bits to be inserted by the hardware */
	SET_UNIPERIF_CONFIG_USER_DAT_CNTR_BY_HW(player);

	/* Set validity bits to be inserted by the hardware */
	SET_UNIPERIF_CONFIG_VALIDITY_DAT_CNTR_BY_HW(player);

	/* Set full software control to disabled */
	SET_UNIPERIF_CONFIG_SPDIF_SW_CTRL_DISABLE(player);

	SET_UNIPERIF_CTRL_ZERO_STUFF_HW(player);

	/* Update the channel status */
	uni_player_set_channel_status(player, runtime);

	/* Clear the user validity user bits */
	SET_UNIPERIF_USER_VALIDITY_VALIDITY_LR(player, 0);

	/* Disable one-bit audio mode */
	SET_UNIPERIF_CONFIG_ONE_BIT_AUD_DISABLE(player);

	/* Enable consecutive frames repetition of Z preamble (not for HBRA) */
	SET_UNIPERIF_CONFIG_REPEAT_CHL_STS_ENABLE(player);

	/* Change to SUF0_SUBF1 and left/right channels swap! */
	SET_UNIPERIF_CONFIG_SUBFRAME_SEL_SUBF1_SUBF0(player);

	/* Set data output as MSB first */
	SET_UNIPERIF_I2S_FMT_ORDER_MSB(player);

	if (player->stream_settings.encoding_mode ==
				UNIPERIF_IEC958_ENCODING_MODE_ENCODED)
		SET_UNIPERIF_CTRL_EXIT_STBY_ON_EOBLOCK_ON(player);
	else
		SET_UNIPERIF_CTRL_EXIT_STBY_ON_EOBLOCK_OFF(player);

	SET_UNIPERIF_I2S_FMT_NUM_CH(player, runtime->channels / 2);

	/* Set rounding to off */
	SET_UNIPERIF_CTRL_ROUNDING_OFF(player);

	/* Set clock divisor */
	SET_UNIPERIF_CTRL_DIVIDER(player, clk_div / 128);

	/* Set the spdif latency to not wait before starting player */
	SET_UNIPERIF_CTRL_SPDIF_LAT_OFF(player);

	/*
	 * Ensure iec958 formatting is off. It will be enabled in function
	 * uni_player_start() at the same time as the operation
	 * mode is set to work around a silicon issue.
	 */
	if (player->ver < SND_ST_UNIPERIF_VERSION_UNI_PLR_TOP_1_0)
		SET_UNIPERIF_CTRL_SPDIF_FMT_OFF(player);
	else
		SET_UNIPERIF_CTRL_SPDIF_FMT_ON(player);

	return 0;
}

static int uni_player_prepare_pcm(struct uniperif *player,
				  struct snd_pcm_runtime *runtime)
{
	int output_frame_size, slot_width, clk_div;

	/* Force slot width to 32 in I2S mode (HW constraint) */
	if ((player->daifmt & SND_SOC_DAIFMT_FORMAT_MASK) ==
		SND_SOC_DAIFMT_I2S)
		slot_width = 32;
	else
		slot_width = snd_pcm_format_width(runtime->format);

	output_frame_size = slot_width * runtime->channels;

	clk_div = player->mclk / runtime->rate;
	/*
	 * For 32 bits subframe clk_div must be a multiple of 128,
	 * for 16 bits must be a multiple of 64
	 */
	if ((slot_width == 32) && (clk_div % 128)) {
		dev_err(player->dev, "%s: invalid clk_div", __func__);
		return -EINVAL;
	}

	if ((slot_width == 16) && (clk_div % 64)) {
		dev_err(player->dev, "%s: invalid clk_div", __func__);
		return -EINVAL;
	}

	/*
	 * Number of bits per subframe (which is one channel sample)
	 * on output - Transfer 16 or 32 bits from FIFO
	 */
	switch (slot_width) {
	case 32:
		SET_UNIPERIF_I2S_FMT_NBIT_32(player);
		SET_UNIPERIF_I2S_FMT_DATA_SIZE_32(player);
		break;
	case 16:
		SET_UNIPERIF_I2S_FMT_NBIT_16(player);
		SET_UNIPERIF_I2S_FMT_DATA_SIZE_16(player);
		break;
	default:
		dev_err(player->dev, "subframe format not supported");
		return -EINVAL;
	}

	/* Configure data memory format */
	switch (runtime->format) {
	case SNDRV_PCM_FORMAT_S16_LE:
		/* One data word contains two samples */
		SET_UNIPERIF_CONFIG_MEM_FMT_16_16(player);
		break;

	case SNDRV_PCM_FORMAT_S32_LE:
		/*
		 * Actually "16 bits/0 bits" means "32/28/24/20/18/16 bits
		 * on the left than zeros (if less than 32 bytes)"... ;-)
		 */
		SET_UNIPERIF_CONFIG_MEM_FMT_16_0(player);
		break;

	default:
		dev_err(player->dev, "format not supported");
		return -EINVAL;
	}

	/* Set rounding to off */
	SET_UNIPERIF_CTRL_ROUNDING_OFF(player);

	/* Set clock divisor */
	SET_UNIPERIF_CTRL_DIVIDER(player, clk_div / (2 * output_frame_size));

	/* Number of channelsmust be even*/
	if ((runtime->channels % 2) || (runtime->channels < 2) ||
	    (runtime->channels > 10)) {
		dev_err(player->dev, "%s: invalid nb of channels", __func__);
		return -EINVAL;
	}

	SET_UNIPERIF_I2S_FMT_NUM_CH(player, runtime->channels / 2);

	/* Set 1-bit audio format to disabled */
	SET_UNIPERIF_CONFIG_ONE_BIT_AUD_DISABLE(player);

	SET_UNIPERIF_I2S_FMT_ORDER_MSB(player);

	/* No iec958 formatting as outputting to DAC  */
	SET_UNIPERIF_CTRL_SPDIF_FMT_OFF(player);

	return 0;
}

static int uni_player_prepare_tdm(struct uniperif *player,
				  struct snd_pcm_runtime *runtime)
{
	int tdm_frame_size; /* unip tdm frame size in bytes */
	int user_frame_size; /* user tdm frame size in bytes */
	/* default unip TDM_WORD_POS_X_Y */
	unsigned int word_pos[4] = {
		0x04060002, 0x0C0E080A, 0x14161012, 0x1C1E181A};
	int freq, ret;

	tdm_frame_size =
		sti_uniperiph_get_unip_tdm_frame_size(player);
	user_frame_size =
		sti_uniperiph_get_user_frame_size(runtime);

	/* fix 16/0 format */
	SET_UNIPERIF_CONFIG_MEM_FMT_16_0(player);
	SET_UNIPERIF_I2S_FMT_DATA_SIZE_32(player);

	/* number of words inserted on the TDM line */
	SET_UNIPERIF_I2S_FMT_NUM_CH(player, user_frame_size / 4 / 2);

	SET_UNIPERIF_I2S_FMT_ORDER_MSB(player);
	SET_UNIPERIF_I2S_FMT_ALIGN_LEFT(player);

	/* Enable the tdm functionality */
	SET_UNIPERIF_TDM_ENABLE_TDM_ENABLE(player);

	/* number of 8 bits timeslots avail in unip tdm frame */
	SET_UNIPERIF_TDM_FS_REF_DIV_NUM_TIMESLOT(player, tdm_frame_size);

	/* set the timeslot allocation for words in FIFO */
	sti_uniperiph_get_tdm_word_pos(player, word_pos);
	SET_UNIPERIF_TDM_WORD_POS(player, 1_2, word_pos[WORD_1_2]);
	SET_UNIPERIF_TDM_WORD_POS(player, 3_4, word_pos[WORD_3_4]);
	SET_UNIPERIF_TDM_WORD_POS(player, 5_6, word_pos[WORD_5_6]);
	SET_UNIPERIF_TDM_WORD_POS(player, 7_8, word_pos[WORD_7_8]);

	/* set unip clk rate (not done vai set_sysclk ops) */
	freq = runtime->rate * tdm_frame_size * 8;
	mutex_lock(&player->ctrl_lock);
	ret = uni_player_clk_set_rate(player, freq);
	if (!ret)
		player->mclk = freq;
	mutex_unlock(&player->ctrl_lock);

	return 0;
}

/*
 * ALSA uniperipheral iec958 controls
 */
static int  uni_player_ctl_iec958_info(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;

	return 0;
}

static int uni_player_ctl_iec958_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *dai = snd_kcontrol_chip(kcontrol);
	struct sti_uniperiph_data *priv = snd_soc_dai_get_drvdata(dai);
	struct uniperif *player = priv->dai_data.uni;
	struct snd_aes_iec958 *iec958 = &player->stream_settings.iec958;

	mutex_lock(&player->ctrl_lock);
	ucontrol->value.iec958.status[0] = iec958->status[0];
	ucontrol->value.iec958.status[1] = iec958->status[1];
	ucontrol->value.iec958.status[2] = iec958->status[2];
	ucontrol->value.iec958.status[3] = iec958->status[3];
	mutex_unlock(&player->ctrl_lock);
	return 0;
}

static int uni_player_ctl_iec958_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *dai = snd_kcontrol_chip(kcontrol);
	struct sti_uniperiph_data *priv = snd_soc_dai_get_drvdata(dai);
	struct uniperif *player = priv->dai_data.uni;
	struct snd_aes_iec958 *iec958 =  &player->stream_settings.iec958;

	mutex_lock(&player->ctrl_lock);
	iec958->status[0] = ucontrol->value.iec958.status[0];
	iec958->status[1] = ucontrol->value.iec958.status[1];
	iec958->status[2] = ucontrol->value.iec958.status[2];
	iec958->status[3] = ucontrol->value.iec958.status[3];
	mutex_unlock(&player->ctrl_lock);

	uni_player_set_channel_status(player, NULL);

	return 0;
}

static struct snd_kcontrol_new uni_player_iec958_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_PCM,
	.name = SNDRV_CTL_NAME_IEC958("", PLAYBACK, DEFAULT),
	.info = uni_player_ctl_iec958_info,
	.get = uni_player_ctl_iec958_get,
	.put = uni_player_ctl_iec958_put,
};

/*
 * uniperif rate adjustement control
 */
static int snd_sti_clk_adjustment_info(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = UNIPERIF_PLAYER_CLK_ADJ_MIN;
	uinfo->value.integer.max = UNIPERIF_PLAYER_CLK_ADJ_MAX;
	uinfo->value.integer.step = 1;

	return 0;
}

static int snd_sti_clk_adjustment_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *dai = snd_kcontrol_chip(kcontrol);
	struct sti_uniperiph_data *priv = snd_soc_dai_get_drvdata(dai);
	struct uniperif *player = priv->dai_data.uni;

	mutex_lock(&player->ctrl_lock);
	ucontrol->value.integer.value[0] = player->clk_adj;
	mutex_unlock(&player->ctrl_lock);

	return 0;
}

static int snd_sti_clk_adjustment_put(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *dai = snd_kcontrol_chip(kcontrol);
	struct sti_uniperiph_data *priv = snd_soc_dai_get_drvdata(dai);
	struct uniperif *player = priv->dai_data.uni;
	int ret = 0;

	if ((ucontrol->value.integer.value[0] < UNIPERIF_PLAYER_CLK_ADJ_MIN) ||
	    (ucontrol->value.integer.value[0] > UNIPERIF_PLAYER_CLK_ADJ_MAX))
		return -EINVAL;

	mutex_lock(&player->ctrl_lock);
	player->clk_adj = ucontrol->value.integer.value[0];

	if (player->mclk)
		ret = uni_player_clk_set_rate(player, player->mclk);
	mutex_unlock(&player->ctrl_lock);

	return ret;
}

static struct snd_kcontrol_new uni_player_clk_adj_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_PCM,
	.name = "PCM Playback Oversampling Freq. Adjustment",
	.info = snd_sti_clk_adjustment_info,
	.get = snd_sti_clk_adjustment_get,
	.put = snd_sti_clk_adjustment_put,
};

static struct snd_kcontrol_new *snd_sti_pcm_ctl[] = {
	&uni_player_clk_adj_ctl,
};

static struct snd_kcontrol_new *snd_sti_iec_ctl[] = {
	&uni_player_iec958_ctl,
	&uni_player_clk_adj_ctl,
};

static int uni_player_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct sti_uniperiph_data *priv = snd_soc_dai_get_drvdata(dai);
	struct uniperif *player = priv->dai_data.uni;
	int ret;

	player->substream = substream;

	player->clk_adj = 0;

	if (!UNIPERIF_TYPE_IS_TDM(player))
		return 0;

	/* refine hw constraint in tdm mode */
	ret = snd_pcm_hw_rule_add(substream->runtime, 0,
				  SNDRV_PCM_HW_PARAM_CHANNELS,
				  sti_uniperiph_fix_tdm_chan,
				  player, SNDRV_PCM_HW_PARAM_CHANNELS,
				  -1);
	if (ret < 0)
		return ret;

	return snd_pcm_hw_rule_add(substream->runtime, 0,
				   SNDRV_PCM_HW_PARAM_FORMAT,
				   sti_uniperiph_fix_tdm_format,
				   player, SNDRV_PCM_HW_PARAM_FORMAT,
				   -1);
}

static int uni_player_set_sysclk(struct snd_soc_dai *dai, int clk_id,
				 unsigned int freq, int dir)
{
	struct sti_uniperiph_data *priv = snd_soc_dai_get_drvdata(dai);
	struct uniperif *player = priv->dai_data.uni;
	int ret;

	if (UNIPERIF_TYPE_IS_TDM(player) || (dir == SND_SOC_CLOCK_IN))
		return 0;

	if (clk_id != 0)
		return -EINVAL;

	mutex_lock(&player->ctrl_lock);
	ret = uni_player_clk_set_rate(player, freq);
	if (!ret)
		player->mclk = freq;
	mutex_unlock(&player->ctrl_lock);

	return ret;
}

static int uni_player_prepare(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct sti_uniperiph_data *priv = snd_soc_dai_get_drvdata(dai);
	struct uniperif *player = priv->dai_data.uni;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int transfer_size, trigger_limit;
	int ret;

	/* The player should be stopped */
	if (player->state != UNIPERIF_STATE_STOPPED) {
		dev_err(player->dev, "%s: invalid player state %d", __func__,
			player->state);
		return -EINVAL;
	}

	/* Calculate transfer size (in fifo cells and bytes) for frame count */
	if (player->type == SND_ST_UNIPERIF_TYPE_TDM) {
		/* transfer size = user frame size (in 32 bits FIFO cell) */
		transfer_size =
			sti_uniperiph_get_user_frame_size(runtime) / 4;
	} else {
		transfer_size = runtime->channels * UNIPERIF_FIFO_FRAMES;
	}

	/* Calculate number of empty cells available before asserting DREQ */
	if (player->ver < SND_ST_UNIPERIF_VERSION_UNI_PLR_TOP_1_0) {
		trigger_limit = UNIPERIF_FIFO_SIZE - transfer_size;
	} else {
		/*
		 * Since SND_ST_UNIPERIF_VERSION_UNI_PLR_TOP_1_0
		 * FDMA_TRIGGER_LIMIT also controls when the state switches
		 * from OFF or STANDBY to AUDIO DATA.
		 */
		trigger_limit = transfer_size;
	}

	/* Trigger limit must be an even number */
	if ((!trigger_limit % 2) || (trigger_limit != 1 && transfer_size % 2) ||
	    (trigger_limit > UNIPERIF_CONFIG_DMA_TRIG_LIMIT_MASK(player))) {
		dev_err(player->dev, "invalid trigger limit %d", trigger_limit);
		return -EINVAL;
	}

	SET_UNIPERIF_CONFIG_DMA_TRIG_LIMIT(player, trigger_limit);

	/* Uniperipheral setup depends on player type */
	switch (player->type) {
	case SND_ST_UNIPERIF_TYPE_HDMI:
		ret = uni_player_prepare_iec958(player, runtime);
		break;
	case SND_ST_UNIPERIF_TYPE_PCM:
		ret = uni_player_prepare_pcm(player, runtime);
		break;
	case SND_ST_UNIPERIF_TYPE_SPDIF:
		ret = uni_player_prepare_iec958(player, runtime);
		break;
	case SND_ST_UNIPERIF_TYPE_TDM:
		ret = uni_player_prepare_tdm(player, runtime);
		break;
	default:
		dev_err(player->dev, "invalid player type");
		return -EINVAL;
	}

	if (ret)
		return ret;

	switch (player->daifmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		SET_UNIPERIF_I2S_FMT_LR_POL_LOW(player);
		SET_UNIPERIF_I2S_FMT_SCLK_EDGE_RISING(player);
		break;
	case SND_SOC_DAIFMT_NB_IF:
		SET_UNIPERIF_I2S_FMT_LR_POL_HIG(player);
		SET_UNIPERIF_I2S_FMT_SCLK_EDGE_RISING(player);
		break;
	case SND_SOC_DAIFMT_IB_NF:
		SET_UNIPERIF_I2S_FMT_LR_POL_LOW(player);
		SET_UNIPERIF_I2S_FMT_SCLK_EDGE_FALLING(player);
		break;
	case SND_SOC_DAIFMT_IB_IF:
		SET_UNIPERIF_I2S_FMT_LR_POL_HIG(player);
		SET_UNIPERIF_I2S_FMT_SCLK_EDGE_FALLING(player);
		break;
	}

	switch (player->daifmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		SET_UNIPERIF_I2S_FMT_ALIGN_LEFT(player);
		SET_UNIPERIF_I2S_FMT_PADDING_I2S_MODE(player);
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		SET_UNIPERIF_I2S_FMT_ALIGN_LEFT(player);
		SET_UNIPERIF_I2S_FMT_PADDING_SONY_MODE(player);
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		SET_UNIPERIF_I2S_FMT_ALIGN_RIGHT(player);
		SET_UNIPERIF_I2S_FMT_PADDING_SONY_MODE(player);
		break;
	default:
		dev_err(player->dev, "format not supported");
		return -EINVAL;
	}

	SET_UNIPERIF_I2S_FMT_NO_OF_SAMPLES_TO_READ(player, 0);

	/* Reset uniperipheral player */
	SET_UNIPERIF_SOFT_RST_SOFT_RST(player);

	return reset_player(player);
}

static int uni_player_start(struct uniperif *player)
{
	int ret;

	/* The player should be stopped */
	if (player->state != UNIPERIF_STATE_STOPPED) {
		dev_err(player->dev, "%s: invalid player state", __func__);
		return -EINVAL;
	}

	ret = clk_prepare_enable(player->clk);
	if (ret) {
		dev_err(player->dev, "%s: Failed to enable clock", __func__);
		return ret;
	}

	/* Clear any pending interrupts */
	SET_UNIPERIF_ITS_BCLR(player, GET_UNIPERIF_ITS(player));

	/* Set the interrupt mask */
	SET_UNIPERIF_ITM_BSET_DMA_ERROR(player);
	SET_UNIPERIF_ITM_BSET_FIFO_ERROR(player);

	/* Enable underflow recovery interrupts */
	if (player->underflow_enabled) {
		SET_UNIPERIF_ITM_BSET_UNDERFLOW_REC_DONE(player);
		SET_UNIPERIF_ITM_BSET_UNDERFLOW_REC_FAILED(player);
	}

	/* Reset uniperipheral player */
	SET_UNIPERIF_SOFT_RST_SOFT_RST(player);

	ret = reset_player(player);
	if (ret < 0) {
		clk_disable_unprepare(player->clk);
		return ret;
	}

	/*
	 * Does not use IEC61937 features of the uniperipheral hardware.
	 * Instead it performs IEC61937 in software and inserts it directly
	 * into the audio data stream. As such, when encoded mode is selected,
	 * linear pcm mode is still used, but with the differences of the
	 * channel status bits set for encoded mode and the validity bits set.
	 */
	SET_UNIPERIF_CTRL_OPERATION_PCM_DATA(player);

	/*
	 * If iec958 formatting is required for hdmi or spdif, then it must be
	 * enabled after the operation mode is set. If set prior to this, it
	 * will not take affect and hang the player.
	 */
	if (player->ver < SND_ST_UNIPERIF_VERSION_UNI_PLR_TOP_1_0)
		if (UNIPERIF_TYPE_IS_IEC958(player))
			SET_UNIPERIF_CTRL_SPDIF_FMT_ON(player);

	/* Force channel status update (no update if clk disable) */
	if (player->ver < SND_ST_UNIPERIF_VERSION_UNI_PLR_TOP_1_0)
		SET_UNIPERIF_CONFIG_CHL_STS_UPDATE(player);
	else
		SET_UNIPERIF_BIT_CONTROL_CHL_STS_UPDATE(player);

	/* Update state to started */
	player->state = UNIPERIF_STATE_STARTED;

	return 0;
}

static int uni_player_stop(struct uniperif *player)
{
	int ret;

	/* The player should not be in stopped state */
	if (player->state == UNIPERIF_STATE_STOPPED) {
		dev_err(player->dev, "%s: invalid player state", __func__);
		return -EINVAL;
	}

	/* Turn the player off */
	SET_UNIPERIF_CTRL_OPERATION_OFF(player);

	/* Soft reset the player */
	SET_UNIPERIF_SOFT_RST_SOFT_RST(player);

	ret = reset_player(player);
	if (ret < 0)
		return ret;

	/* Disable interrupts */
	SET_UNIPERIF_ITM_BCLR(player, GET_UNIPERIF_ITM(player));

	/* Disable clock */
	clk_disable_unprepare(player->clk);

	/* Update state to stopped and return */
	player->state = UNIPERIF_STATE_STOPPED;

	return 0;
}

int uni_player_resume(struct uniperif *player)
{
	int ret;

	/* Select the frequency synthesizer clock */
	if (player->clk_sel) {
		ret = regmap_field_write(player->clk_sel, 1);
		if (ret) {
			dev_err(player->dev,
				"%s: Failed to select freq synth clock",
				__func__);
			return ret;
		}
	}

	SET_UNIPERIF_CONFIG_BACK_STALL_REQ_DISABLE(player);
	SET_UNIPERIF_CTRL_ROUNDING_OFF(player);
	SET_UNIPERIF_CTRL_SPDIF_LAT_OFF(player);
	SET_UNIPERIF_CONFIG_IDLE_MOD_DISABLE(player);

	return 0;
}
EXPORT_SYMBOL_GPL(uni_player_resume);

static int uni_player_trigger(struct snd_pcm_substream *substream,
			      int cmd, struct snd_soc_dai *dai)
{
	struct sti_uniperiph_data *priv = snd_soc_dai_get_drvdata(dai);
	struct uniperif *player = priv->dai_data.uni;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		return uni_player_start(player);
	case SNDRV_PCM_TRIGGER_STOP:
		return uni_player_stop(player);
	case SNDRV_PCM_TRIGGER_RESUME:
		return uni_player_resume(player);
	default:
		return -EINVAL;
	}
}

static void uni_player_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct sti_uniperiph_data *priv = snd_soc_dai_get_drvdata(dai);
	struct uniperif *player = priv->dai_data.uni;

	if (player->state != UNIPERIF_STATE_STOPPED)
		/* Stop the player */
		uni_player_stop(player);

	player->substream = NULL;
}

static int uni_player_parse_dt_audio_glue(struct platform_device *pdev,
					  struct uniperif *player)
{
	struct device_node *node = pdev->dev.of_node;
	struct regmap *regmap;
	struct reg_field regfield[2] = {
		/* PCM_CLK_SEL */
		REG_FIELD(SYS_CFG_AUDIO_GLUE,
			  8 + player->id,
			  8 + player->id),
		/* PCMP_VALID_SEL */
		REG_FIELD(SYS_CFG_AUDIO_GLUE, 0, 1)
	};

	regmap = syscon_regmap_lookup_by_phandle(node, "st,syscfg");

	if (IS_ERR(regmap)) {
		dev_err(&pdev->dev, "sti-audio-clk-glue syscf not found\n");
		return PTR_ERR(regmap);
	}

	player->clk_sel = regmap_field_alloc(regmap, regfield[0]);
	player->valid_sel = regmap_field_alloc(regmap, regfield[1]);

	return 0;
}

static const struct snd_soc_dai_ops uni_player_dai_ops = {
		.startup = uni_player_startup,
		.shutdown = uni_player_shutdown,
		.prepare = uni_player_prepare,
		.trigger = uni_player_trigger,
		.hw_params = sti_uniperiph_dai_hw_params,
		.set_fmt = sti_uniperiph_dai_set_fmt,
		.set_sysclk = uni_player_set_sysclk,
		.set_tdm_slot = sti_uniperiph_set_tdm_slot
};

int uni_player_init(struct platform_device *pdev,
		    struct uniperif *player)
{
	int ret = 0;

	player->dev = &pdev->dev;
	player->state = UNIPERIF_STATE_STOPPED;
	player->dai_ops = &uni_player_dai_ops;

	/* Get PCM_CLK_SEL & PCMP_VALID_SEL from audio-glue-ctrl SoC reg */
	ret = uni_player_parse_dt_audio_glue(pdev, player);

	if (ret < 0) {
		dev_err(player->dev, "Failed to parse DeviceTree");
		return ret;
	}

	/* Underflow recovery is only supported on later ip revisions */
	if (player->ver >= SND_ST_UNIPERIF_VERSION_UNI_PLR_TOP_1_0)
		player->underflow_enabled = 1;

	if (UNIPERIF_TYPE_IS_TDM(player))
		player->hw = &uni_tdm_hw;
	else
		player->hw = &uni_player_pcm_hw;

	/* Get uniperif resource */
	player->clk = of_clk_get(pdev->dev.of_node, 0);
	if (IS_ERR(player->clk))
		ret = PTR_ERR(player->clk);

	/* Select the frequency synthesizer clock */
	if (player->clk_sel) {
		ret = regmap_field_write(player->clk_sel, 1);
		if (ret) {
			dev_err(player->dev,
				"%s: Failed to select freq synth clock",
				__func__);
			return ret;
		}
	}

	/* connect to I2S/TDM TX bus */
	if (player->valid_sel &&
	    (player->id == UNIPERIF_PLAYER_I2S_OUT)) {
		ret = regmap_field_write(player->valid_sel, player->id);
		if (ret) {
			dev_err(player->dev,
				"%s: unable to connect to tdm bus", __func__);
			return ret;
		}
	}

	ret = devm_request_irq(&pdev->dev, player->irq,
			       uni_player_irq_handler, IRQF_SHARED,
			       dev_name(&pdev->dev), player);
	if (ret < 0)
		return ret;

	mutex_init(&player->ctrl_lock);

	/* Ensure that disabled by default */
	SET_UNIPERIF_CONFIG_BACK_STALL_REQ_DISABLE(player);
	SET_UNIPERIF_CTRL_ROUNDING_OFF(player);
	SET_UNIPERIF_CTRL_SPDIF_LAT_OFF(player);
	SET_UNIPERIF_CONFIG_IDLE_MOD_DISABLE(player);

	if (UNIPERIF_TYPE_IS_IEC958(player)) {
		/* Set default iec958 status bits  */

		/* Consumer, PCM, copyright, 2ch, mode 0 */
		player->stream_settings.iec958.status[0] = 0x00;
		/* Broadcast reception category */
		player->stream_settings.iec958.status[1] =
					IEC958_AES1_CON_GENERAL;
		/* Do not take into account source or channel number */
		player->stream_settings.iec958.status[2] =
					IEC958_AES2_CON_SOURCE_UNSPEC;
		/* Sampling frequency not indicated */
		player->stream_settings.iec958.status[3] =
					IEC958_AES3_CON_FS_NOTID;
		/* Max sample word 24-bit, sample word length not indicated */
		player->stream_settings.iec958.status[4] =
					IEC958_AES4_CON_MAX_WORDLEN_24 |
					IEC958_AES4_CON_WORDLEN_24_20;

		player->num_ctrls = ARRAY_SIZE(snd_sti_iec_ctl);
		player->snd_ctrls = snd_sti_iec_ctl[0];
	} else {
		player->num_ctrls = ARRAY_SIZE(snd_sti_pcm_ctl);
		player->snd_ctrls = snd_sti_pcm_ctl[0];
	}

	return 0;
}
EXPORT_SYMBOL_GPL(uni_player_init);
