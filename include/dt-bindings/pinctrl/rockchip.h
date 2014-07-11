/*
 * Header providing constants for Rockchip pinctrl bindings.
 *
 * Copyright (c) 2013 MundoReader S.L.
 * Author: Heiko Stuebner <heiko@sntech.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __DT_BINDINGS_ROCKCHIP_PINCTRL_H__
#define __DT_BINDINGS_ROCKCHIP_PINCTRL_H__

#define RK_GPIO0	0
#define RK_GPIO1	1
#define RK_GPIO2	2
#define RK_GPIO3	3
#define RK_GPIO4	4
#define RK_GPIO5	5
#define RK_GPIO6	6
#define RK_GPIO7	7
#define RK_GPIO8	8


#define RK_FUNC_GPIO	0
#define RK_FUNC_1	1
#define RK_FUNC_2	2
#define RK_FUNC_3	3
#define RK_FUNC_4	4
#define RK_FUNC_5	5
#define RK_FUNC_6	6
#define RK_FUNC_7	7



/*special virtual pin for vcc domain setting*/
#define VIRTUAL_PIN_FOR_AP0_VCC		0xfA00
#define VIRTUAL_PIN_FOR_AP1_VCC		0xfA10
#define VIRTUAL_PIN_FOR_CIF_VCC		0xfA20
#define VIRTUAL_PIN_FOR_FLASH_VCC	0xfA30
#define VIRTUAL_PIN_FOR_VCCIO0_VCC	0xfA40
#define VIRTUAL_PIN_FOR_VCCIO1_VCC	0xfA50
#define VIRTUAL_PIN_FOR_LCDC0_VCC	0xfA60
#define VIRTUAL_PIN_FOR_LCDC1_VCC	0xfA70


#define RK32_VIRTUAL_PIN_FOR_LCDC_VCC		0xfA00
#define RK32_VIRTUAL_PIN_FOR_DVP_VCC		0xfA10
#define RK32_VIRTUAL_PIN_FOR_FLASH0_VCC		0xfA20
#define RK32_VIRTUAL_PIN_FOR_FLASH1_VCC		0xfA30
#define RK32_VIRTUAL_PIN_FOR_WIFI_VCC		0xfA40
#define RK32_VIRTUAL_PIN_FOR_BB_VCC		0xfA50
#define RK32_VIRTUAL_PIN_FOR_AUDIO_VCC		0xfA60
#define RK32_VIRTUAL_PIN_FOR_SDCARD_VCC		0xfA70

#define RK32_VIRTUAL_PIN_FOR_GPIO30_VCC		0xfB00
#define RK32_VIRTUAL_PIN_FOR_GPIO1830_VCC	0xfB10


#define TYPE_PULL_REG		0x01
#define TYPE_VOL_REG		0x02
#define TYPE_DRV_REG		0x03
#define TYPE_TRI_REG		0x04

#define RK2928_PULL_OFFSET		0x118
#define RK2928_PULL_PINS_PER_REG	16
#define RK2928_PULL_BANK_STRIDE		8

#define RK3188_PULL_BITS_PER_PIN	2
#define RK3188_PULL_PINS_PER_REG	8
#define RK3188_PULL_BANK_STRIDE		16

#define RK3036_PULL_BITS_PER_PIN	1
#define RK3036_PULL_PINS_PER_REG	16
#define RK3036_PULL_BANK_STRIDE		8



/*warning:don not chang the following value*/
#define VALUE_PULL_NORMAL	0
#define VALUE_PULL_UP		1
#define VALUE_PULL_DOWN		2
#define VALUE_PULL_KEEP		3
#define VALUE_PULL_DISABLE	4 //don't set and keep pull default
#define VALUE_PULL_DEFAULT	4 //don't set and keep pull default


//for rk2928,rk3036
#define VALUE_PULL_UPDOWN_DISABLE		0
#define VALUE_PULL_UPDOWN_ENABLE		1

#define VALUE_VOL_DEFAULT	0
#define VALUE_VOL_3V3		0
#define VALUE_VOL_1V8		1

#define VALUE_DRV_DEFAULT	0
#define VALUE_DRV_2MA		0
#define VALUE_DRV_4MA		1
#define VALUE_DRV_8MA		2
#define VALUE_DRV_12MA		3

#define VALUE_TRI_DEFAULT	0
#define VALUE_TRI_FALSE		0
#define VALUE_TRI_TRUE		1


/*
 * pin config bit field definitions
 *
 * pull-up:	1..0	(2)
 * voltage:	3..2	(2)
 * drive:		5..4	(2)
 * trisiate:	7..6	(2)
 *
 * MSB of each field is presence bit for the config.
 */
#define PULL_SHIFT		0
#define PULL_PRESENT		(1 << 2)
#define VOL_SHIFT		3
#define VOL_PRESENT		(1 << 5)
#define DRV_SHIFT		6
#define DRV_PRESENT		(1 << 8)
#define TRI_SHIFT		9
#define TRI_PRESENT		(1 << 11)

#define CONFIG_TO_PULL(c)	((c) >> PULL_SHIFT & 0x3)
#define CONFIG_TO_VOL(c)	((c) >> VOL_SHIFT & 0x3)
#define CONFIG_TO_DRV(c)	((c) >> DRV_SHIFT & 0x3)
#define CONFIG_TO_TRI(c)	((c) >> TRI_SHIFT & 0x3)


#define MAX_NUM_CONFIGS 	4
#define POS_PULL		0
#define POS_VOL			1
#define POS_DRV			2
#define POS_TRI			3


#define	GPIO_A0			0
#define	GPIO_A1			1
#define	GPIO_A2			2
#define	GPIO_A3			3
#define	GPIO_A4			4
#define	GPIO_A5			5
#define	GPIO_A6			6
#define	GPIO_A7			7
#define	GPIO_B0			8
#define	GPIO_B1			9
#define	GPIO_B2			10
#define	GPIO_B3			11
#define	GPIO_B4			12
#define	GPIO_B5			13
#define	GPIO_B6			14
#define	GPIO_B7			15
#define	GPIO_C0			16
#define	GPIO_C1			17
#define	GPIO_C2			18
#define	GPIO_C3			19
#define	GPIO_C4			20
#define	GPIO_C5			21
#define	GPIO_C6			22
#define	GPIO_C7			23
#define	GPIO_D0			24
#define	GPIO_D1			25
#define	GPIO_D2			26
#define	GPIO_D3			27
#define	GPIO_D4			28
#define	GPIO_D5			29
#define	GPIO_D6			30
#define	GPIO_D7			31

#define FUNC_TO_GPIO(m)		((m) & 0xfff0)


#endif
