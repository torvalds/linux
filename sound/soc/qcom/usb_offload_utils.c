// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <dt-bindings/sound/qcom,q6afe.h>
#include <linux/module.h>
#include <sound/jack.h>
#include <sound/soc-usb.h>

#include "usb_offload_utils.h"

int qcom_snd_usb_offload_jack_setup(struct snd_soc_pcm_runtime *rtd,
				    struct snd_soc_jack *jack, bool *jack_setup)
{
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	int ret = 0;

	if (cpu_dai->id != USB_RX)
		return -EINVAL;

	if (!*jack_setup) {
		ret = snd_soc_usb_setup_offload_jack(codec_dai->component, jack);
		if (ret)
			return ret;
	}

	*jack_setup = true;

	return 0;
}
EXPORT_SYMBOL_GPL(qcom_snd_usb_offload_jack_setup);

int qcom_snd_usb_offload_jack_remove(struct snd_soc_pcm_runtime *rtd,
				     bool *jack_setup)
{
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	int ret = 0;

	if (cpu_dai->id != USB_RX)
		return -EINVAL;

	if (*jack_setup) {
		ret = snd_soc_component_set_jack(codec_dai->component, NULL, NULL);
		if (ret)
			return ret;
	}

	*jack_setup = false;

	return 0;
}
EXPORT_SYMBOL_GPL(qcom_snd_usb_offload_jack_remove);
MODULE_DESCRIPTION("ASoC Q6 USB offload controls");
MODULE_LICENSE("GPL");
