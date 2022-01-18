/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __Q6DSP_AUDIO_PORTS_H__
#define __Q6DSP_AUDIO_PORTS_H__

struct q6dsp_audio_port_dai_driver_config {
	int (*probe)(struct snd_soc_dai *dai);
	int (*remove)(struct snd_soc_dai *dai);
	const struct snd_soc_dai_ops *q6hdmi_ops;
	const struct snd_soc_dai_ops *q6slim_ops;
	const struct snd_soc_dai_ops *q6i2s_ops;
	const struct snd_soc_dai_ops *q6tdm_ops;
	const struct snd_soc_dai_ops *q6dma_ops;
};

struct snd_soc_dai_driver *q6dsp_audio_ports_set_config(struct device *dev,
					struct q6dsp_audio_port_dai_driver_config *cfg,
					int *num_dais);
int q6dsp_audio_ports_of_xlate_dai_name(struct snd_soc_component *component,
					const struct of_phandle_args *args,
					const char **dai_name);
#endif /* __Q6DSP_AUDIO_PORTS_H__ */
