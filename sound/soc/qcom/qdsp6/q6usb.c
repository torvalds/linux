// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/auxiliary_bus.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dma-map-ops.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <sound/asound.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/q6usboffload.h>
#include <sound/soc.h>
#include <sound/soc-usb.h>

#include <dt-bindings/sound/qcom,q6afe.h>

#include "q6afe.h"
#include "q6dsp-lpass-ports.h"

#define Q6_USB_SID_MASK	0xF

struct q6usb_port_data {
	struct auxiliary_device uauxdev;
	struct q6afe_usb_cfg usb_cfg;
	struct snd_soc_usb *usb;
	struct snd_soc_jack *hs_jack;
	struct q6usb_offload priv;

	/* Protects against operations between SOC USB and ASoC */
	struct mutex mutex;
	struct list_head devices;
};

static const struct snd_soc_dapm_widget q6usb_dai_widgets[] = {
	SND_SOC_DAPM_HP("USB_RX_BE", NULL),
};

static const struct snd_soc_dapm_route q6usb_dapm_routes[] = {
	{"USB Playback", NULL, "USB_RX_BE"},
};

static int q6usb_hw_params(struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params,
			   struct snd_soc_dai *dai)
{
	struct q6usb_port_data *data = dev_get_drvdata(dai->dev);
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	int direction = substream->stream;
	struct q6afe_port *q6usb_afe;
	struct snd_soc_usb_device *sdev;
	int ret = -EINVAL;

	mutex_lock(&data->mutex);

	/* No active chip index */
	if (list_empty(&data->devices))
		goto out;

	sdev = list_last_entry(&data->devices, struct snd_soc_usb_device, list);

	ret = snd_soc_usb_find_supported_format(sdev->chip_idx, params, direction);
	if (ret < 0)
		goto out;

	q6usb_afe = q6afe_port_get_from_id(cpu_dai->dev, USB_RX);
	if (IS_ERR(q6usb_afe)) {
		ret = PTR_ERR(q6usb_afe);
		goto out;
	}

	/* Notify audio DSP about the devices being offloaded */
	ret = afe_port_send_usb_dev_param(q6usb_afe, sdev->card_idx,
					  sdev->ppcm_idx[sdev->num_playback - 1]);

out:
	mutex_unlock(&data->mutex);

	return ret;
}

static const struct snd_soc_dai_ops q6usb_ops = {
	.hw_params = q6usb_hw_params,
};

static struct snd_soc_dai_driver q6usb_be_dais[] = {
	{
		.playback = {
			.stream_name = "USB BE RX",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |
				SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |
				SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
				SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 |
				SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE |
				SNDRV_PCM_FMTBIT_U16_LE | SNDRV_PCM_FMTBIT_U16_BE |
				SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S24_BE |
				SNDRV_PCM_FMTBIT_U24_LE | SNDRV_PCM_FMTBIT_U24_BE,
			.channels_min = 1,
			.channels_max = 2,
			.rate_max =     192000,
			.rate_min =	8000,
		},
		.id = USB_RX,
		.name = "USB_RX_BE",
		.ops = &q6usb_ops,
	},
};

static int q6usb_audio_ports_of_xlate_dai_name(struct snd_soc_component *component,
					       const struct of_phandle_args *args,
					       const char **dai_name)
{
	int id = args->args[0];
	int ret = -EINVAL;
	int i;

	for (i = 0; i < ARRAY_SIZE(q6usb_be_dais); i++) {
		if (q6usb_be_dais[i].id == id) {
			*dai_name = q6usb_be_dais[i].name;
			ret = 0;
			break;
		}
	}

	return ret;
}

static int q6usb_get_pcm_id_from_widget(struct snd_soc_dapm_widget *w)
{
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_dai *dai;

	for_each_card_rtds(w->dapm->card, rtd) {
		dai = snd_soc_rtd_to_cpu(rtd, 0);
		/*
		 * Only look for playback widget. RTD number carries the assigned
		 * PCM index.
		 */
		if (dai->stream[0].widget == w)
			return rtd->id;
	}

	return -1;
}

static int q6usb_usb_mixer_enabled(struct snd_soc_dapm_widget *w)
{
	struct snd_soc_dapm_path *p;

	/* Checks to ensure USB path is enabled/connected */
	snd_soc_dapm_widget_for_each_sink_path(w, p)
		if (!strcmp(p->sink->name, "USB Mixer") && p->connect)
			return 1;

	return 0;
}

static int q6usb_get_pcm_id(struct snd_soc_component *component)
{
	struct snd_soc_dapm_widget *w;
	struct snd_soc_dapm_path *p;
	int pidx;

	/*
	 * Traverse widgets to find corresponding FE widget.  The DAI links are
	 * built like the following:
	 *    MultiMedia* <-> MM_DL* <-> USB Mixer*
	 */
	for_each_card_widgets(component->card, w) {
		if (!strncmp(w->name, "MultiMedia", 10)) {
			/*
			 * Look up all paths associated with the FE widget to see if
			 * the USB BE is enabled.  The sink widget is responsible to
			 * link with the USB mixers.
			 */
			snd_soc_dapm_widget_for_each_sink_path(w, p) {
				if (q6usb_usb_mixer_enabled(p->sink)) {
					pidx = q6usb_get_pcm_id_from_widget(w);
					return pidx;
				}
			}
		}
	}

	return -1;
}

static int q6usb_update_offload_route(struct snd_soc_component *component, int card,
				      int pcm, int direction, enum snd_soc_usb_kctl path,
				      long *route)
{
	struct q6usb_port_data *data = dev_get_drvdata(component->dev);
	struct snd_soc_usb_device *sdev;
	int ret = 0;
	int idx = -1;

	mutex_lock(&data->mutex);

	if (list_empty(&data->devices) ||
	    direction == SNDRV_PCM_STREAM_CAPTURE) {
		ret = -ENODEV;
		goto out;
	}

	sdev = list_last_entry(&data->devices, struct snd_soc_usb_device, list);

	/*
	 * Will always look for last PCM device discovered/probed as the
	 * active offload index.
	 */
	if (card == sdev->card_idx &&
	    pcm == sdev->ppcm_idx[sdev->num_playback - 1]) {
		idx = path == SND_SOC_USB_KCTL_CARD_ROUTE ?
				component->card->snd_card->number :
				q6usb_get_pcm_id(component);
	}

out:
	route[0] = idx;
	mutex_unlock(&data->mutex);

	return ret;
}

static int q6usb_alsa_connection_cb(struct snd_soc_usb *usb,
				    struct snd_soc_usb_device *sdev, bool connected)
{
	struct q6usb_port_data *data;

	if (!usb->component)
		return -ENODEV;

	data = dev_get_drvdata(usb->component->dev);

	mutex_lock(&data->mutex);
	if (connected) {
		if (data->hs_jack)
			snd_jack_report(data->hs_jack->jack, SND_JACK_USB);

		/* Selects the latest USB headset plugged in for offloading */
		list_add_tail(&sdev->list, &data->devices);
	} else {
		list_del(&sdev->list);

		if (data->hs_jack)
			snd_jack_report(data->hs_jack->jack, 0);
	}
	mutex_unlock(&data->mutex);

	return 0;
}

static void q6usb_component_disable_jack(struct q6usb_port_data *data)
{
	/* Offload jack has already been disabled */
	if (!data->hs_jack)
		return;

	snd_jack_report(data->hs_jack->jack, 0);
	data->hs_jack = NULL;
}

static void q6usb_component_enable_jack(struct q6usb_port_data *data,
					struct snd_soc_jack *jack)
{
	snd_jack_report(jack->jack, !list_empty(&data->devices) ? SND_JACK_USB : 0);
	data->hs_jack = jack;
}

static int q6usb_component_set_jack(struct snd_soc_component *component,
				    struct snd_soc_jack *jack, void *priv)
{
	struct q6usb_port_data *data = dev_get_drvdata(component->dev);

	mutex_lock(&data->mutex);
	if (jack)
		q6usb_component_enable_jack(data, jack);
	else
		q6usb_component_disable_jack(data);
	mutex_unlock(&data->mutex);

	return 0;
}

static void q6usb_dai_aux_release(struct device *dev) {}

static int q6usb_dai_add_aux_device(struct q6usb_port_data *data,
				    struct auxiliary_device *auxdev)
{
	int ret;

	auxdev->dev.parent = data->priv.dev;
	auxdev->dev.release = q6usb_dai_aux_release;
	auxdev->name = "qc-usb-audio-offload";

	ret = auxiliary_device_init(auxdev);
	if (ret)
		return ret;

	ret = auxiliary_device_add(auxdev);
	if (ret)
		auxiliary_device_uninit(auxdev);

	return ret;
}

static int q6usb_component_probe(struct snd_soc_component *component)
{
	struct q6usb_port_data *data = dev_get_drvdata(component->dev);
	struct snd_soc_usb *usb;
	int ret;

	/* Add the QC USB SND aux device */
	ret = q6usb_dai_add_aux_device(data, &data->uauxdev);
	if (ret < 0)
		return ret;

	usb = snd_soc_usb_allocate_port(component, &data->priv);
	if (IS_ERR(usb))
		return -ENOMEM;

	usb->connection_status_cb = q6usb_alsa_connection_cb;
	usb->update_offload_route_info = q6usb_update_offload_route;

	snd_soc_usb_add_port(usb);
	data->usb = usb;

	return 0;
}

static void q6usb_component_remove(struct snd_soc_component *component)
{
	struct q6usb_port_data *data = dev_get_drvdata(component->dev);

	snd_soc_usb_remove_port(data->usb);
	auxiliary_device_delete(&data->uauxdev);
	auxiliary_device_uninit(&data->uauxdev);
	snd_soc_usb_free_port(data->usb);
}

static const struct snd_soc_component_driver q6usb_dai_component = {
	.probe = q6usb_component_probe,
	.set_jack = q6usb_component_set_jack,
	.remove = q6usb_component_remove,
	.name = "q6usb-dai-component",
	.dapm_widgets = q6usb_dai_widgets,
	.num_dapm_widgets = ARRAY_SIZE(q6usb_dai_widgets),
	.dapm_routes = q6usb_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(q6usb_dapm_routes),
	.of_xlate_dai_name = q6usb_audio_ports_of_xlate_dai_name,
};

static int q6usb_dai_dev_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct q6usb_port_data *data;
	struct device *dev = &pdev->dev;
	struct of_phandle_args args;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret = of_property_read_u16(node, "qcom,usb-audio-intr-idx",
				   &data->priv.intr_num);
	if (ret) {
		dev_err(&pdev->dev, "failed to read intr idx.\n");
		return ret;
	}

	ret = of_parse_phandle_with_fixed_args(node, "iommus", 1, 0, &args);
	if (!ret)
		data->priv.sid = args.args[0] & Q6_USB_SID_MASK;

	ret = devm_mutex_init(dev, &data->mutex);
	if (ret < 0)
		return ret;

	data->priv.domain = iommu_get_domain_for_dev(&pdev->dev);

	data->priv.dev = dev;
	INIT_LIST_HEAD(&data->devices);
	dev_set_drvdata(dev, data);

	return devm_snd_soc_register_component(dev, &q6usb_dai_component,
					q6usb_be_dais, ARRAY_SIZE(q6usb_be_dais));
}

static const struct of_device_id q6usb_dai_device_id[] = {
	{ .compatible = "qcom,q6usb" },
	{},
};
MODULE_DEVICE_TABLE(of, q6usb_dai_device_id);

static struct platform_driver q6usb_dai_platform_driver = {
	.driver = {
		.name = "q6usb-dai",
		.of_match_table = q6usb_dai_device_id,
	},
	.probe = q6usb_dai_dev_probe,
	/*
	 * Remove not required as resources are cleaned up as part of
	 * component removal.  Others are device managed resources.
	 */
};
module_platform_driver(q6usb_dai_platform_driver);

MODULE_DESCRIPTION("Q6 USB backend dai driver");
MODULE_LICENSE("GPL");
