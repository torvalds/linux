/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SOC_RESET_MESON_AUX_H
#define __SOC_RESET_MESON_AUX_H

#include <linux/err.h>

struct device;
struct regmap;

#if IS_ENABLED(CONFIG_RESET_MESON_AUX)
int devm_meson_rst_aux_register(struct device *dev,
				struct regmap *map,
				const char *adev_name);
#else
static inline int devm_meson_rst_aux_register(struct device *dev,
					      struct regmap *map,
					      const char *adev_name)
{
	return 0;
}
#endif

#endif /* __SOC_RESET_MESON_AUX_H */
