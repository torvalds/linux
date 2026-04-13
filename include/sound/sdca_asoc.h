/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The MIPI SDCA specification is available for public downloads at
 * https://www.mipi.org/mipi-sdca-v1-0-download
 *
 * Copyright (C) 2025 Cirrus Logic, Inc. and
 *                    Cirrus Logic International Semiconductor Ltd.
 */

#ifndef __SDCA_ASOC_H__
#define __SDCA_ASOC_H__

struct device;
struct regmap;
struct sdca_function_data;
struct snd_ctl_elem_value;
struct snd_kcontrol;
struct snd_kcontrol_new;
struct snd_pcm_hw_params;
struct snd_pcm_substream;
struct snd_soc_component_driver;
struct snd_soc_dai;
struct snd_soc_dai_driver;
struct snd_soc_dai_ops;
struct snd_soc_dapm_route;
struct snd_soc_dapm_widget;

/* convenient macro to handle the mono volume in 7.8 fixed format representation */
#define SDCA_SINGLE_Q78_TLV(xname, xreg, xmin, xmax, xstep, tlv_array) \
{ \
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = (xname), \
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ | SNDRV_CTL_ELEM_ACCESS_READWRITE, \
	.tlv.p = (tlv_array), \
	.info = snd_soc_info_volsw, \
	.get = sdca_asoc_q78_get_volsw, \
	.put = sdca_asoc_q78_put_volsw, \
	.private_value = (unsigned long)&(struct soc_mixer_control) { \
		.reg = (xreg), .rreg = (xreg), \
		.min = (xmin), .max = (xmax), \
		.shift = (xstep), .rshift = (xstep), \
		.sign_bit = 15 \
	} \
}

/* convenient macro for stereo volume in 7.8 fixed format with separate registers for L/R */
#define SDCA_DOUBLE_Q78_TLV(xname, xreg_l, xreg_r, xmin, xmax, xstep, tlv_array) \
{ \
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = (xname), \
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ | SNDRV_CTL_ELEM_ACCESS_READWRITE, \
	.tlv.p = (tlv_array), \
	.info = snd_soc_info_volsw, \
	.get = sdca_asoc_q78_get_volsw, \
	.put = sdca_asoc_q78_put_volsw, \
	.private_value = (unsigned long)&(struct soc_mixer_control) { \
		.reg = (xreg_l), .rreg = (xreg_r), \
		.min = (xmin), .max = (xmax), \
		.shift = (xstep), .rshift = (xstep), \
		.sign_bit = 15 \
	} \
}

int sdca_asoc_count_component(struct device *dev, struct sdca_function_data *function,
			      int *num_widgets, int *num_routes, int *num_controls,
			      int *num_dais);

int sdca_asoc_populate_dapm(struct device *dev, struct sdca_function_data *function,
			    struct snd_soc_dapm_widget *widgets,
			    struct snd_soc_dapm_route *routes);
int sdca_asoc_populate_controls(struct device *dev,
				struct sdca_function_data *function,
				struct snd_kcontrol_new *kctl);
int sdca_asoc_populate_dais(struct device *dev, struct sdca_function_data *function,
			    struct snd_soc_dai_driver *dais,
			    const struct snd_soc_dai_ops *ops);

int sdca_asoc_populate_component(struct device *dev,
				 struct sdca_function_data *function,
				 struct snd_soc_component_driver *component_drv,
				 struct snd_soc_dai_driver **dai_drv, int *num_dai_drv,
				 const struct snd_soc_dai_ops *ops);

int sdca_asoc_set_constraints(struct device *dev, struct regmap *regmap,
			      struct sdca_function_data *function,
			      struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai);
void sdca_asoc_free_constraints(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai);
int sdca_asoc_get_port(struct device *dev, struct regmap *regmap,
		       struct sdca_function_data *function,
		       struct snd_soc_dai *dai);
int sdca_asoc_hw_params(struct device *dev, struct regmap *regmap,
			struct sdca_function_data *function,
			struct snd_pcm_substream *substream,
			struct snd_pcm_hw_params *params,
			struct snd_soc_dai *dai);
int sdca_asoc_q78_put_volsw(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol);
int sdca_asoc_q78_get_volsw(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol);
#endif // __SDCA_ASOC_H__
