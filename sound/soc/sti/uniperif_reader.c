/*
 * Copyright (C) STMicroelectronics SA 2015
 * Authors: Arnaud Pouliquen <arnaud.pouliquen@st.com>
 *          for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#include <sound/soc.h>

#include "uniperif.h"

#define UNIPERIF_READER_I2S_IN 0 /* reader id connected to I2S/TDM TX bus */
/*
 * Note: snd_pcm_hardware is linked to DMA controller but is declared here to
 * integrate unireader capability in term of rate and supported channels
 */
static const struct snd_pcm_hardware uni_reader_pcm_hw = {
	.info = SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID,
	.formats = SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S16_LE,

	.rates = SNDRV_PCM_RATE_CONTINUOUS,
	.rate_min = 8000,
	.rate_max = 96000,

	.channels_min = 2,
	.channels_max = 8,

	.periods_min = 2,
	.periods_max = 48,

	.period_bytes_min = 128,
	.period_bytes_max = 64 * PAGE_SIZE,
	.buffer_bytes_max = 256 * PAGE_SIZE
};

/*
 * uni_reader_irq_handler
 * In case of error audio stream is stopped; stop action is protected via PCM
 * stream lock  to avoid race condition with trigger callback.
 */
static irqreturn_t uni_reader_irq_handler(int irq, void *dev_id)
{
	irqreturn_t ret = IRQ_NONE;
	struct uniperif *reader = dev_id;
	unsigned int status;

	if (reader->state == UNIPERIF_STATE_STOPPED) {
		/* Unexpected IRQ: do nothing */
		dev_warn(reader->dev, "unexpected IRQ\n");
		return IRQ_HANDLED;
	}

	/* Get interrupt status & clear them immediately */
	status = GET_UNIPERIF_ITS(reader);
	SET_UNIPERIF_ITS_BCLR(reader, status);

	/* Check for fifo overflow error */
	if (unlikely(status & UNIPERIF_ITS_FIFO_ERROR_MASK(reader))) {
		dev_err(reader->dev, "FIFO error detected\n");

		snd_pcm_stream_lock(reader->substream);
		snd_pcm_stop(reader->substream, SNDRV_PCM_STATE_XRUN);
		snd_pcm_stream_unlock(reader->substream);

		return IRQ_HANDLED;
	}

	return ret;
}

static int uni_reader_prepare_pcm(struct snd_pcm_runtime *runtime,
				  struct uniperif *reader)
{
	int slot_width;

	/* Force slot width to 32 in I2S mode */
	if ((reader->daifmt & SND_SOC_DAIFMT_FORMAT_MASK)
		== SND_SOC_DAIFMT_I2S) {
		slot_width = 32;
	} else {
		switch (runtime->format) {
		case SNDRV_PCM_FORMAT_S16_LE:
			slot_width = 16;
			break;
		default:
			slot_width = 32;
			break;
		}
	}

	/* Number of bits per subframe (i.e one channel sample) on input. */
	switch (slot_width) {
	case 32:
		SET_UNIPERIF_I2S_FMT_NBIT_32(reader);
		SET_UNIPERIF_I2S_FMT_DATA_SIZE_32(reader);
		break;
	case 16:
		SET_UNIPERIF_I2S_FMT_NBIT_16(reader);
		SET_UNIPERIF_I2S_FMT_DATA_SIZE_16(reader);
		break;
	default:
		dev_err(reader->dev, "subframe format not supported\n");
		return -EINVAL;
	}

	/* Configure data memory format */
	switch (runtime->format) {
	case SNDRV_PCM_FORMAT_S16_LE:
		/* One data word contains two samples */
		SET_UNIPERIF_CONFIG_MEM_FMT_16_16(reader);
		break;

	case SNDRV_PCM_FORMAT_S32_LE:
		/*
		 * Actually "16 bits/0 bits" means "32/28/24/20/18/16 bits
		 * on the MSB then zeros (if less than 32 bytes)"...
		 */
		SET_UNIPERIF_CONFIG_MEM_FMT_16_0(reader);
		break;

	default:
		dev_err(reader->dev, "format not supported\n");
		return -EINVAL;
	}

	/* Number of channels must be even */
	if ((runtime->channels % 2) || (runtime->channels < 2) ||
	    (runtime->channels > 10)) {
		dev_err(reader->dev, "%s: invalid nb of channels\n", __func__);
		return -EINVAL;
	}

	SET_UNIPERIF_I2S_FMT_NUM_CH(reader, runtime->channels / 2);
	SET_UNIPERIF_I2S_FMT_ORDER_MSB(reader);

	return 0;
}

static int uni_reader_prepare_tdm(struct snd_pcm_runtime *runtime,
				  struct uniperif *reader)
{
	int frame_size; /* user tdm frame size in bytes */
	/* default unip TDM_WORD_POS_X_Y */
	unsigned int word_pos[4] = {
		0x04060002, 0x0C0E080A, 0x14161012, 0x1C1E181A};

	frame_size = sti_uniperiph_get_user_frame_size(runtime);

	/* fix 16/0 format */
	SET_UNIPERIF_CONFIG_MEM_FMT_16_0(reader);
	SET_UNIPERIF_I2S_FMT_DATA_SIZE_32(reader);

	/* number of words inserted on the TDM line */
	SET_UNIPERIF_I2S_FMT_NUM_CH(reader, frame_size / 4 / 2);

	SET_UNIPERIF_I2S_FMT_ORDER_MSB(reader);
	SET_UNIPERIF_I2S_FMT_ALIGN_LEFT(reader);
	SET_UNIPERIF_TDM_ENABLE_TDM_ENABLE(reader);

	/*
	 * set the timeslots allocation for words in FIFO
	 *
	 * HW bug: (LSB word < MSB word) => this config is not possible
	 *         So if we want (LSB word < MSB) word, then it shall be
	 *         handled by user
	 */
	sti_uniperiph_get_tdm_word_pos(reader, word_pos);
	SET_UNIPERIF_TDM_WORD_POS(reader, 1_2, word_pos[WORD_1_2]);
	SET_UNIPERIF_TDM_WORD_POS(reader, 3_4, word_pos[WORD_3_4]);
	SET_UNIPERIF_TDM_WORD_POS(reader, 5_6, word_pos[WORD_5_6]);
	SET_UNIPERIF_TDM_WORD_POS(reader, 7_8, word_pos[WORD_7_8]);

	return 0;
}

static int uni_reader_prepare(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct sti_uniperiph_data *priv = snd_soc_dai_get_drvdata(dai);
	struct uniperif *reader = priv->dai_data.uni;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int transfer_size, trigger_limit, ret;

	/* The reader should be stopped */
	if (reader->state != UNIPERIF_STATE_STOPPED) {
		dev_err(reader->dev, "%s: invalid reader state %d\n", __func__,
			reader->state);
		return -EINVAL;
	}

	/* Calculate transfer size (in fifo cells and bytes) for frame count */
	if (reader->type == SND_ST_UNIPERIF_TYPE_TDM) {
		/* transfer size = unip frame size (in 32 bits FIFO cell) */
		transfer_size =
			sti_uniperiph_get_user_frame_size(runtime) / 4;
	} else {
		transfer_size = runtime->channels * UNIPERIF_FIFO_FRAMES;
	}

	/* Calculate number of empty cells available before asserting DREQ */
	if (reader->ver < SND_ST_UNIPERIF_VERSION_UNI_PLR_TOP_1_0)
		trigger_limit = UNIPERIF_FIFO_SIZE - transfer_size;
	else
		/*
		 * Since SND_ST_UNIPERIF_VERSION_UNI_PLR_TOP_1_0
		 * FDMA_TRIGGER_LIMIT also controls when the state switches
		 * from OFF or STANDBY to AUDIO DATA.
		 */
		trigger_limit = transfer_size;

	/* Trigger limit must be an even number */
	if ((!trigger_limit % 2) ||
	    (trigger_limit != 1 && transfer_size % 2) ||
	    (trigger_limit > UNIPERIF_CONFIG_DMA_TRIG_LIMIT_MASK(reader))) {
		dev_err(reader->dev, "invalid trigger limit %d\n",
			trigger_limit);
		return -EINVAL;
	}

	SET_UNIPERIF_CONFIG_DMA_TRIG_LIMIT(reader, trigger_limit);

	if (UNIPERIF_TYPE_IS_TDM(reader))
		ret = uni_reader_prepare_tdm(runtime, reader);
	else
		ret = uni_reader_prepare_pcm(runtime, reader);
	if (ret)
		return ret;

	switch (reader->daifmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		SET_UNIPERIF_I2S_FMT_ALIGN_LEFT(reader);
		SET_UNIPERIF_I2S_FMT_PADDING_I2S_MODE(reader);
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		SET_UNIPERIF_I2S_FMT_ALIGN_LEFT(reader);
		SET_UNIPERIF_I2S_FMT_PADDING_SONY_MODE(reader);
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		SET_UNIPERIF_I2S_FMT_ALIGN_RIGHT(reader);
		SET_UNIPERIF_I2S_FMT_PADDING_SONY_MODE(reader);
		break;
	default:
		dev_err(reader->dev, "format not supported\n");
		return -EINVAL;
	}

	/* Data clocking (changing) on the rising/falling edge */
	switch (reader->daifmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		SET_UNIPERIF_I2S_FMT_LR_POL_LOW(reader);
		SET_UNIPERIF_I2S_FMT_SCLK_EDGE_RISING(reader);
		break;
	case SND_SOC_DAIFMT_NB_IF:
		SET_UNIPERIF_I2S_FMT_LR_POL_HIG(reader);
		SET_UNIPERIF_I2S_FMT_SCLK_EDGE_RISING(reader);
		break;
	case SND_SOC_DAIFMT_IB_NF:
		SET_UNIPERIF_I2S_FMT_LR_POL_LOW(reader);
		SET_UNIPERIF_I2S_FMT_SCLK_EDGE_FALLING(reader);
		break;
	case SND_SOC_DAIFMT_IB_IF:
		SET_UNIPERIF_I2S_FMT_LR_POL_HIG(reader);
		SET_UNIPERIF_I2S_FMT_SCLK_EDGE_FALLING(reader);
		break;
	}

	/* Clear any pending interrupts */
	SET_UNIPERIF_ITS_BCLR(reader, GET_UNIPERIF_ITS(reader));

	SET_UNIPERIF_I2S_FMT_NO_OF_SAMPLES_TO_READ(reader, 0);

	/* Set the interrupt mask */
	SET_UNIPERIF_ITM_BSET_DMA_ERROR(reader);
	SET_UNIPERIF_ITM_BSET_FIFO_ERROR(reader);
	SET_UNIPERIF_ITM_BSET_MEM_BLK_READ(reader);

	/* Enable underflow recovery interrupts */
	if (reader->underflow_enabled) {
		SET_UNIPERIF_ITM_BSET_UNDERFLOW_REC_DONE(reader);
		SET_UNIPERIF_ITM_BSET_UNDERFLOW_REC_FAILED(reader);
	}

	/* Reset uniperipheral reader */
	return sti_uniperiph_reset(reader);
}

static int uni_reader_start(struct uniperif *reader)
{
	/* The reader should be stopped */
	if (reader->state != UNIPERIF_STATE_STOPPED) {
		dev_err(reader->dev, "%s: invalid reader state\n", __func__);
		return -EINVAL;
	}

	/* Enable reader interrupts (and clear possible stalled ones) */
	SET_UNIPERIF_ITS_BCLR_FIFO_ERROR(reader);
	SET_UNIPERIF_ITM_BSET_FIFO_ERROR(reader);

	/* Launch the reader */
	SET_UNIPERIF_CTRL_OPERATION_PCM_DATA(reader);

	/* Update state to started */
	reader->state = UNIPERIF_STATE_STARTED;
	return 0;
}

static int uni_reader_stop(struct uniperif *reader)
{
	/* The reader should not be in stopped state */
	if (reader->state == UNIPERIF_STATE_STOPPED) {
		dev_err(reader->dev, "%s: invalid reader state\n", __func__);
		return -EINVAL;
	}

	/* Turn the reader off */
	SET_UNIPERIF_CTRL_OPERATION_OFF(reader);

	/* Disable interrupts */
	SET_UNIPERIF_ITM_BCLR(reader, GET_UNIPERIF_ITM(reader));

	/* Update state to stopped and return */
	reader->state = UNIPERIF_STATE_STOPPED;

	return 0;
}

static int  uni_reader_trigger(struct snd_pcm_substream *substream,
			       int cmd, struct snd_soc_dai *dai)
{
	struct sti_uniperiph_data *priv = snd_soc_dai_get_drvdata(dai);
	struct uniperif *reader = priv->dai_data.uni;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		return  uni_reader_start(reader);
	case SNDRV_PCM_TRIGGER_STOP:
		return  uni_reader_stop(reader);
	default:
		return -EINVAL;
	}
}

static int uni_reader_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct sti_uniperiph_data *priv = snd_soc_dai_get_drvdata(dai);
	struct uniperif *reader = priv->dai_data.uni;
	int ret;

	if (!UNIPERIF_TYPE_IS_TDM(reader))
		return 0;

	/* refine hw constraint in tdm mode */
	ret = snd_pcm_hw_rule_add(substream->runtime, 0,
				  SNDRV_PCM_HW_PARAM_CHANNELS,
				  sti_uniperiph_fix_tdm_chan,
				  reader, SNDRV_PCM_HW_PARAM_CHANNELS,
				  -1);
	if (ret < 0)
		return ret;

	return snd_pcm_hw_rule_add(substream->runtime, 0,
				   SNDRV_PCM_HW_PARAM_FORMAT,
				   sti_uniperiph_fix_tdm_format,
				   reader, SNDRV_PCM_HW_PARAM_FORMAT,
				   -1);
}

static void uni_reader_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct sti_uniperiph_data *priv = snd_soc_dai_get_drvdata(dai);
	struct uniperif *reader = priv->dai_data.uni;

	if (reader->state != UNIPERIF_STATE_STOPPED) {
		/* Stop the reader */
		uni_reader_stop(reader);
	}
}

static const struct snd_soc_dai_ops uni_reader_dai_ops = {
		.startup = uni_reader_startup,
		.shutdown = uni_reader_shutdown,
		.prepare = uni_reader_prepare,
		.trigger = uni_reader_trigger,
		.hw_params = sti_uniperiph_dai_hw_params,
		.set_fmt = sti_uniperiph_dai_set_fmt,
		.set_tdm_slot = sti_uniperiph_set_tdm_slot
};

int uni_reader_init(struct platform_device *pdev,
		    struct uniperif *reader)
{
	int ret = 0;

	reader->dev = &pdev->dev;
	reader->state = UNIPERIF_STATE_STOPPED;
	reader->dai_ops = &uni_reader_dai_ops;

	if (UNIPERIF_TYPE_IS_TDM(reader))
		reader->hw = &uni_tdm_hw;
	else
		reader->hw = &uni_reader_pcm_hw;

	ret = devm_request_irq(&pdev->dev, reader->irq,
			       uni_reader_irq_handler, IRQF_SHARED,
			       dev_name(&pdev->dev), reader);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to request IRQ\n");
		return -EBUSY;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(uni_reader_init);
