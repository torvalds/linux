/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __HDAC_HDMI_H__
#define __HDAC_HDMI_H__

int hdac_hdmi_jack_init(struct snd_soc_dai *dai, int device,
				struct snd_soc_jack *jack);

int hdac_hdmi_jack_port_init(struct snd_soc_component *component,
			struct snd_soc_dapm_context *dapm);
#endif /* __HDAC_HDMI_H__ */
