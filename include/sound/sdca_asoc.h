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
struct sdca_function_data;
struct snd_kcontrol_new;
struct snd_soc_component_driver;
struct snd_soc_dai_driver;
struct snd_soc_dai_ops;
struct snd_soc_dapm_route;
struct snd_soc_dapm_widget;

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

#endif // __SDCA_ASOC_H__
