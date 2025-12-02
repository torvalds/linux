// SPDX-License-Identifier: GPL-2.0

#include <linux/regulator/consumer.h>

#ifndef CONFIG_REGULATOR

__rust_helper void rust_helper_regulator_put(struct regulator *regulator)
{
	regulator_put(regulator);
}

__rust_helper int rust_helper_regulator_set_voltage(struct regulator *regulator,
						    int min_uV, int max_uV)
{
	return regulator_set_voltage(regulator, min_uV, max_uV);
}

__rust_helper int rust_helper_regulator_get_voltage(struct regulator *regulator)
{
	return regulator_get_voltage(regulator);
}

__rust_helper struct regulator *rust_helper_regulator_get(struct device *dev,
							  const char *id)
{
	return regulator_get(dev, id);
}

__rust_helper int rust_helper_regulator_enable(struct regulator *regulator)
{
	return regulator_enable(regulator);
}

__rust_helper int rust_helper_regulator_disable(struct regulator *regulator)
{
	return regulator_disable(regulator);
}

__rust_helper int rust_helper_regulator_is_enabled(struct regulator *regulator)
{
	return regulator_is_enabled(regulator);
}

__rust_helper int rust_helper_devm_regulator_get_enable(struct device *dev,
							const char *id)
{
	return devm_regulator_get_enable(dev, id);
}

__rust_helper int
rust_helper_devm_regulator_get_enable_optional(struct device *dev,
					       const char *id)
{
	return devm_regulator_get_enable_optional(dev, id);
}

#endif
