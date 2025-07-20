/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef __QCOM_SND_USB_OFFLOAD_UTILS_H__
#define __QCOM_SND_USB_OFFLOAD_UTILS_H__

#include <sound/soc.h>

#if IS_ENABLED(CONFIG_SND_SOC_QCOM_OFFLOAD_UTILS)
int qcom_snd_usb_offload_jack_setup(struct snd_soc_pcm_runtime *rtd,
				    struct snd_soc_jack *jack, bool *jack_setup);

int qcom_snd_usb_offload_jack_remove(struct snd_soc_pcm_runtime *rtd,
				     bool *jack_setup);
#else
static inline int qcom_snd_usb_offload_jack_setup(struct snd_soc_pcm_runtime *rtd,
						  struct snd_soc_jack *jack,
						  bool *jack_setup)
{
	return -ENODEV;
}

static inline int qcom_snd_usb_offload_jack_remove(struct snd_soc_pcm_runtime *rtd,
						   bool *jack_setup)
{
	return -ENODEV;
}
#endif /* IS_ENABLED(CONFIG_SND_SOC_QCOM_OFFLOAD_UTILS) */
#endif /* __QCOM_SND_USB_OFFLOAD_UTILS_H__ */
