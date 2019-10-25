// SPDX-License-Identifier: GPL-2.0
//
// Xilinx ASoC I2S audio support
//
// Copyright (C) 2018 Xilinx, Inc.
//
// Author: Praveen Vuppala <praveenv@xilinx.com>
// Author: Maruthi Srinivas Bayyavarapu <maruthis@xilinx.com>

#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#define DRV_NAME "xlnx_i2s"

#define I2S_CORE_CTRL_OFFSET		0x08
#define I2S_I2STIM_OFFSET		0x20
#define I2S_CH0_OFFSET			0x30
#define I2S_I2STIM_VALID_MASK		GENMASK(7, 0)

static int xlnx_i2s_set_sclkout_div(struct snd_soc_dai *cpu_dai,
				    int div_id, int div)
{
	void __iomem *base = snd_soc_dai_get_drvdata(cpu_dai);

	if (!div || (div & ~I2S_I2STIM_VALID_MASK))
		return -EINVAL;

	writel(div, base + I2S_I2STIM_OFFSET);

	return 0;
}

static int xlnx_i2s_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params,
			      struct snd_soc_dai *i2s_dai)
{
	u32 reg_off, chan_id;
	void __iomem *base = snd_soc_dai_get_drvdata(i2s_dai);

	chan_id = params_channels(params) / 2;

	while (chan_id > 0) {
		reg_off = I2S_CH0_OFFSET + ((chan_id - 1) * 4);
		writel(chan_id, base + reg_off);
		chan_id--;
	}

	return 0;
}

static int xlnx_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
			    struct snd_soc_dai *i2s_dai)
{
	void __iomem *base = snd_soc_dai_get_drvdata(i2s_dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		writel(1, base + I2S_CORE_CTRL_OFFSET);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		writel(0, base + I2S_CORE_CTRL_OFFSET);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct snd_soc_dai_ops xlnx_i2s_dai_ops = {
	.trigger = xlnx_i2s_trigger,
	.set_clkdiv = xlnx_i2s_set_sclkout_div,
	.hw_params = xlnx_i2s_hw_params
};

static const struct snd_soc_component_driver xlnx_i2s_component = {
	.name = DRV_NAME,
};

static const struct of_device_id xlnx_i2s_of_match[] = {
	{ .compatible = "xlnx,i2s-transmitter-1.0", },
	{ .compatible = "xlnx,i2s-receiver-1.0", },
	{},
};
MODULE_DEVICE_TABLE(of, xlnx_i2s_of_match);

static int xlnx_i2s_probe(struct platform_device *pdev)
{
	void __iomem *base;
	struct snd_soc_dai_driver *dai_drv;
	int ret;
	u32 ch, format, data_width;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;

	dai_drv = devm_kzalloc(&pdev->dev, sizeof(*dai_drv), GFP_KERNEL);
	if (!dai_drv)
		return -ENOMEM;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	ret = of_property_read_u32(node, "xlnx,num-channels", &ch);
	if (ret < 0) {
		dev_err(dev, "cannot get supported channels\n");
		return ret;
	}
	ch = ch * 2;

	ret = of_property_read_u32(node, "xlnx,dwidth", &data_width);
	if (ret < 0) {
		dev_err(dev, "cannot get data width\n");
		return ret;
	}
	switch (data_width) {
	case 16:
		format = SNDRV_PCM_FMTBIT_S16_LE;
		break;
	case 24:
		format = SNDRV_PCM_FMTBIT_S24_LE;
		break;
	default:
		return -EINVAL;
	}

	if (of_device_is_compatible(node, "xlnx,i2s-transmitter-1.0")) {
		dai_drv->name = "xlnx_i2s_playback";
		dai_drv->playback.stream_name = "Playback";
		dai_drv->playback.formats = format;
		dai_drv->playback.channels_min = ch;
		dai_drv->playback.channels_max = ch;
		dai_drv->playback.rates	= SNDRV_PCM_RATE_8000_192000;
		dai_drv->ops = &xlnx_i2s_dai_ops;
	} else if (of_device_is_compatible(node, "xlnx,i2s-receiver-1.0")) {
		dai_drv->name = "xlnx_i2s_capture";
		dai_drv->capture.stream_name = "Capture";
		dai_drv->capture.formats = format;
		dai_drv->capture.channels_min = ch;
		dai_drv->capture.channels_max = ch;
		dai_drv->capture.rates = SNDRV_PCM_RATE_8000_192000;
		dai_drv->ops = &xlnx_i2s_dai_ops;
	} else {
		return -ENODEV;
	}

	dev_set_drvdata(&pdev->dev, base);

	ret = devm_snd_soc_register_component(&pdev->dev, &xlnx_i2s_component,
					      dai_drv, 1);
	if (ret) {
		dev_err(&pdev->dev, "i2s component registration failed\n");
		return ret;
	}

	dev_info(&pdev->dev, "%s DAI registered\n", dai_drv->name);

	return ret;
}

static struct platform_driver xlnx_i2s_aud_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = xlnx_i2s_of_match,
	},
	.probe = xlnx_i2s_probe,
};

module_platform_driver(xlnx_i2s_aud_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Praveen Vuppala  <praveenv@xilinx.com>");
MODULE_AUTHOR("Maruthi Srinivas Bayyavarapu <maruthis@xilinx.com>");
