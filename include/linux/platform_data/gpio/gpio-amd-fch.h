/* SPDX-License-Identifier: GPL-2.0+ */

/*
 * AMD FCH gpio driver platform-data
 *
 * Copyright (C) 2018 metux IT consult
 * Author: Enrico Weigelt <info@metux.net>
 *
 */

#ifndef __LINUX_PLATFORM_DATA_GPIO_AMD_FCH_H
#define __LINUX_PLATFORM_DATA_GPIO_AMD_FCH_H

#define AMD_FCH_GPIO_DRIVER_NAME "gpio_amd_fch"

/*
 * gpio register index definitions
 */
#define AMD_FCH_GPIO_REG_GPIO49		0x40
#define AMD_FCH_GPIO_REG_GPIO50		0x41
#define AMD_FCH_GPIO_REG_GPIO51		0x42
#define AMD_FCH_GPIO_REG_GPIO59_DEVSLP0	0x43
#define AMD_FCH_GPIO_REG_GPIO57		0x44
#define AMD_FCH_GPIO_REG_GPIO58		0x45
#define AMD_FCH_GPIO_REG_GPIO59_DEVSLP1	0x46
#define AMD_FCH_GPIO_REG_GPIO64		0x47
#define AMD_FCH_GPIO_REG_GPIO68		0x48
#define AMD_FCH_GPIO_REG_GPIO66_SPKR	0x5B
#define AMD_FCH_GPIO_REG_GPIO71		0x4D
#define AMD_FCH_GPIO_REG_GPIO32_GE1	0x59
#define AMD_FCH_GPIO_REG_GPIO33_GE2	0x5A
#define AMT_FCH_GPIO_REG_GEVT22		0x09

/*
 * struct amd_fch_gpio_pdata - GPIO chip platform data
 * @gpio_num: number of entries
 * @gpio_reg: array of gpio registers
 * @gpio_names: array of gpio names
 */
struct amd_fch_gpio_pdata {
	int			gpio_num;
	int			*gpio_reg;
	const char * const	*gpio_names;
};

#endif /* __LINUX_PLATFORM_DATA_GPIO_AMD_FCH_H */
