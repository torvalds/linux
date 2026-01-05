// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2025 Samsung Electronics Co., Ltd.
// Author: Michal Wilczynski <m.wilczynski@samsung.com>

#include <linux/pwm.h>

__rust_helper struct device *rust_helper_pwmchip_parent(const struct pwm_chip *chip)
{
	return pwmchip_parent(chip);
}

__rust_helper void *rust_helper_pwmchip_get_drvdata(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

__rust_helper void rust_helper_pwmchip_set_drvdata(struct pwm_chip *chip, void *data)
{
	pwmchip_set_drvdata(chip, data);
}
