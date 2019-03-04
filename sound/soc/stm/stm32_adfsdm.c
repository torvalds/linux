// SPDX-License-Identifier: GPL-2.0
/*
 * This file is part of STM32 DFSDM ASoC DAI driver
 *
 * Copyright (C) 2017, STMicroelectronics - All Rights Reserved
 * Authors: Arnaud Pouliquen <arnaud.pouliquen@st.com>
 *          Olivier Moysan <olivier.moysan@st.com>
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <linux/iio/iio.h>
#include <linux/iio/consumer.h>
#include <linux/iio/adc/stm32-dfsdm-adc.h>

#include <sound/pcm.h>
#include <sound/soc.h>

#define STM32_ADFSDM_DRV_NAME "stm32-adfsdm"

#define DFSDM_MAX_PERIOD_SIZE	(PAGE_SIZE / 2)
#define DFSDM_MAX_PERIODS	6

struct stm32_adfsdm_priv {
	struct snd_soc_dai_driver dai_drv;
	struct snd_pcm_substream *substream;
	struct device *dev;

	/* IIO */
	struct iio_channel *iio_ch;
	struct iio_cb_buffer *iio_cb;
	bool iio_active;

	/* PCM buffer */
	unsigned char *pcm_buff;
	unsigned int pos;

	struct mutex lock; /* protect against race condition on iio state */
};

static const struct snd_pcm_hardware stm32_adfsdm_pcm_hw = {
	.info = SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER |
	    SNDRV_PCM_INFO_PAUSE,
	.formats = SNDRV_PCM_FMTBIT_S32_LE,

	.rate_min = 8000,
	.rate_max = 32000,

	.channels_min = 1,
	.channels_max = 1,

	.periods_min = 2,
	.periods_max = DFSDM_MAX_PERIODS,

	.period_bytes_max = DFSDM_MAX_PERIOD_SIZE,
	.buffer_bytes_max = DFSDM_MAX_PERIODS * DFSDM_MAX_PERIOD_SIZE
};

static void stm32_adfsdm_shutdown(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct stm32_adfsdm_priv *priv = snd_soc_dai_get_drvdata(dai);

	mutex_lock(&priv->lock);
	if (priv->iio_active) {
		iio_channel_stop_all_cb(priv->iio_cb);
		priv->iio_active = false;
	}
	mutex_unlock(&priv->lock);
}

static int stm32_adfsdm_dai_prepare(struct snd_pcm_substream *substream,
				    struct snd_soc_dai *dai)
{
	struct stm32_adfsdm_priv *priv = snd_soc_dai_get_drvdata(dai);
	int ret;

	mutex_lock(&priv->lock);
	if (priv->iio_active) {
		iio_channel_stop_all_cb(priv->iio_cb);
		priv->iio_active = false;
	}

	ret = iio_write_channel_attribute(priv->iio_ch,
					  substream->runtime->rate, 0,
					  IIO_CHAN_INFO_SAMP_FREQ);
	if (ret < 0) {
		dev_err(dai->dev, "%s: Failed to set %d sampling rate\n",
			__func__, substream->runtime->rate);
		goto out;
	}

	if (!priv->iio_active) {
		ret = iio_channel_start_all_cb(priv->iio_cb);
		if (!ret)
			priv->iio_active = true;
		else
			dev_err(dai->dev, "%s: IIO channel start failed (%d)\n",
				__func__, ret);
	}

out:
	mutex_unlock(&priv->lock);

	return ret;
}

static int stm32_adfsdm_set_sysclk(struct snd_soc_dai *dai, int clk_id,
				   unsigned int freq, int dir)
{
	struct stm32_adfsdm_priv *priv = snd_soc_dai_get_drvdata(dai);
	ssize_t size;
	char str_freq[10];

	dev_dbg(dai->dev, "%s: Enter for freq %d\n", __func__, freq);

	/* Set IIO frequency if CODEC is master as clock comes from SPI_IN */

	snprintf(str_freq, sizeof(str_freq), "%d\n", freq);
	size = iio_write_channel_ext_info(priv->iio_ch, "spi_clk_freq",
					  str_freq, sizeof(str_freq));
	if (size != sizeof(str_freq)) {
		dev_err(dai->dev, "%s: Failed to set SPI clock\n",
			__func__);
		return -EINVAL;
	}
	return 0;
}

static const struct snd_soc_dai_ops stm32_adfsdm_dai_ops = {
	.shutdown = stm32_adfsdm_shutdown,
	.prepare = stm32_adfsdm_dai_prepare,
	.set_sysclk = stm32_adfsdm_set_sysclk,
};

static const struct snd_soc_dai_driver stm32_adfsdm_dai = {
	.capture = {
		    .channels_min = 1,
		    .channels_max = 1,
		    .formats = SNDRV_PCM_FMTBIT_S32_LE,
		    .rates = (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
			      SNDRV_PCM_RATE_32000),
		    },
	.ops = &stm32_adfsdm_dai_ops,
};

static const struct snd_soc_component_driver stm32_adfsdm_dai_component = {
	.name = "stm32_dfsdm_audio",
};

static int stm32_afsdm_pcm_cb(const void *data, size_t size, void *private)
{
	struct stm32_adfsdm_priv *priv = private;
	struct snd_soc_pcm_runtime *rtd = priv->substream->private_data;
	u8 *pcm_buff = priv->pcm_buff;
	u8 *src_buff = (u8 *)data;
	unsigned int buff_size = snd_pcm_lib_buffer_bytes(priv->substream);
	unsigned int period_size = snd_pcm_lib_period_bytes(priv->substream);
	unsigned int old_pos = priv->pos;
	unsigned int cur_size = size;

	dev_dbg(rtd->dev, "%s: buff_add :%pK, pos = %d, size = %zu\n",
		__func__, &pcm_buff[priv->pos], priv->pos, size);

	if ((priv->pos + size) > buff_size) {
		memcpy(&pcm_buff[priv->pos], src_buff, buff_size - priv->pos);
		cur_size -= buff_size - priv->pos;
		priv->pos = 0;
	}

	memcpy(&pcm_buff[priv->pos], &src_buff[size - cur_size], cur_size);
	priv->pos = (priv->pos + cur_size) % buff_size;

	if (cur_size != size || (old_pos && (old_pos % period_size < size)))
		snd_pcm_period_elapsed(priv->substream);

	return 0;
}

static int stm32_adfsdm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct stm32_adfsdm_priv *priv =
		snd_soc_dai_get_drvdata(rtd->cpu_dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		priv->pos = 0;
		return stm32_dfsdm_get_buff_cb(priv->iio_ch->indio_dev,
					       stm32_afsdm_pcm_cb, priv);
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
		return stm32_dfsdm_release_buff_cb(priv->iio_ch->indio_dev);
	}

	return -EINVAL;
}

static int stm32_adfsdm_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct stm32_adfsdm_priv *priv = snd_soc_dai_get_drvdata(rtd->cpu_dai);
	int ret;

	ret =  snd_soc_set_runtime_hwparams(substream, &stm32_adfsdm_pcm_hw);
	if (!ret)
		priv->substream = substream;

	return ret;
}

static int stm32_adfsdm_pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct stm32_adfsdm_priv *priv =
		snd_soc_dai_get_drvdata(rtd->cpu_dai);

	snd_pcm_lib_free_pages(substream);
	priv->substream = NULL;

	return 0;
}

static snd_pcm_uframes_t stm32_adfsdm_pcm_pointer(
					    struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct stm32_adfsdm_priv *priv =
		snd_soc_dai_get_drvdata(rtd->cpu_dai);

	return bytes_to_frames(substream->runtime, priv->pos);
}

static int stm32_adfsdm_pcm_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct stm32_adfsdm_priv *priv =
		snd_soc_dai_get_drvdata(rtd->cpu_dai);
	int ret;

	ret =  snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));
	if (ret < 0)
		return ret;
	priv->pcm_buff = substream->runtime->dma_area;

	return iio_channel_cb_set_buffer_watermark(priv->iio_cb,
						   params_period_size(params));
}

static int stm32_adfsdm_pcm_hw_free(struct snd_pcm_substream *substream)
{
	snd_pcm_lib_free_pages(substream);

	return 0;
}

static struct snd_pcm_ops stm32_adfsdm_pcm_ops = {
	.open		= stm32_adfsdm_pcm_open,
	.close		= stm32_adfsdm_pcm_close,
	.hw_params	= stm32_adfsdm_pcm_hw_params,
	.hw_free	= stm32_adfsdm_pcm_hw_free,
	.trigger	= stm32_adfsdm_trigger,
	.pointer	= stm32_adfsdm_pcm_pointer,
};

static int stm32_adfsdm_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_pcm *pcm = rtd->pcm;
	struct stm32_adfsdm_priv *priv =
		snd_soc_dai_get_drvdata(rtd->cpu_dai);
	unsigned int size = DFSDM_MAX_PERIODS * DFSDM_MAX_PERIOD_SIZE;

	return snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
						     priv->dev, size, size);
}

static void stm32_adfsdm_pcm_free(struct snd_pcm *pcm)
{
	struct snd_pcm_substream *substream;

	substream = pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream;
	if (substream)
		snd_pcm_lib_preallocate_free_for_all(pcm);
}

static struct snd_soc_component_driver stm32_adfsdm_soc_platform = {
	.ops		= &stm32_adfsdm_pcm_ops,
	.pcm_new	= stm32_adfsdm_pcm_new,
	.pcm_free	= stm32_adfsdm_pcm_free,
};

static const struct of_device_id stm32_adfsdm_of_match[] = {
	{.compatible = "st,stm32h7-dfsdm-dai"},
	{}
};
MODULE_DEVICE_TABLE(of, stm32_adfsdm_of_match);

static int stm32_adfsdm_probe(struct platform_device *pdev)
{
	struct stm32_adfsdm_priv *priv;
	struct snd_soc_component *component;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &pdev->dev;
	priv->dai_drv = stm32_adfsdm_dai;
	mutex_init(&priv->lock);

	dev_set_drvdata(&pdev->dev, priv);

	ret = devm_snd_soc_register_component(&pdev->dev,
					      &stm32_adfsdm_dai_component,
					      &priv->dai_drv, 1);
	if (ret < 0)
		return ret;

	/* Associate iio channel */
	priv->iio_ch  = devm_iio_channel_get_all(&pdev->dev);
	if (IS_ERR(priv->iio_ch))
		return PTR_ERR(priv->iio_ch);

	priv->iio_cb = iio_channel_get_all_cb(&pdev->dev, NULL, NULL);
	if (IS_ERR(priv->iio_cb))
		return PTR_ERR(priv->iio_cb);

	component = devm_kzalloc(&pdev->dev, sizeof(*component), GFP_KERNEL);
	if (!component)
		return -ENOMEM;
#ifdef CONFIG_DEBUG_FS
	component->debugfs_prefix = "pcm";
#endif

	ret = snd_soc_add_component(&pdev->dev, component,
				    &stm32_adfsdm_soc_platform, NULL, 0);
	if (ret < 0)
		dev_err(&pdev->dev, "%s: Failed to register PCM platform\n",
			__func__);

	return ret;
}

static int stm32_adfsdm_remove(struct platform_device *pdev)
{
	snd_soc_unregister_component(&pdev->dev);

	return 0;
}

static struct platform_driver stm32_adfsdm_driver = {
	.driver = {
		   .name = STM32_ADFSDM_DRV_NAME,
		   .of_match_table = stm32_adfsdm_of_match,
		   },
	.probe = stm32_adfsdm_probe,
	.remove = stm32_adfsdm_remove,
};

module_platform_driver(stm32_adfsdm_driver);

MODULE_DESCRIPTION("stm32 DFSDM DAI driver");
MODULE_AUTHOR("Arnaud Pouliquen <arnaud.pouliquen@st.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" STM32_ADFSDM_DRV_NAME);
