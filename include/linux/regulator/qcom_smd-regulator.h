/* Copyright (c) 2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef __QCOM_SMD_REGULATOR_H_
#define __QCOM_SMD_REGULATOR_H_

#ifdef CONFIG_REGULATOR_QCOM_SMD_RPM
int qcom_rpm_set_floor(struct regulator *regulator, int floor);
int qcom_rpm_set_corner(struct regulator *regulator, int corner);
#else
static inline int qcom_rpm_set_floor(struct regulator *regulator, int floor)
{
	return -EINVAL;
}

static inline int qcom_rpm_set_corner(struct regulator *regulator, int corner)
{
	return -EINVAL;
}
#endif

#endif
