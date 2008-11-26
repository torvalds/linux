/*
 * gpio.h  --  GPIO Driver for Wolfson WM8350 PMIC
 *
 * Copyright 2007 Wolfson Microelectronics PLC
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#ifndef __LINUX_MFD_WM8350_GPIO_H_
#define __LINUX_MFD_WM8350_GPIO_H_

#include <linux/platform_device.h>

/*
 * GPIO Registers.
 */
#define WM8350_GPIO_DEBOUNCE                    0x80
#define WM8350_GPIO_PIN_PULL_UP_CONTROL         0x81
#define WM8350_GPIO_PULL_DOWN_CONTROL           0x82
#define WM8350_GPIO_INT_MODE                    0x83
#define WM8350_GPIO_CONTROL                     0x85
#define WM8350_GPIO_CONFIGURATION_I_O           0x86
#define WM8350_GPIO_PIN_POLARITY_TYPE           0x87
#define WM8350_GPIO_FUNCTION_SELECT_1           0x8C
#define WM8350_GPIO_FUNCTION_SELECT_2           0x8D
#define WM8350_GPIO_FUNCTION_SELECT_3           0x8E
#define WM8350_GPIO_FUNCTION_SELECT_4           0x8F

/*
 * GPIO Functions
 */
#define WM8350_GPIO0_GPIO_IN			0x0
#define WM8350_GPIO0_GPIO_OUT			0x0
#define WM8350_GPIO0_PWR_ON_IN			0x1
#define WM8350_GPIO0_PWR_ON_OUT			0x1
#define WM8350_GPIO0_LDO_EN_IN			0x2
#define WM8350_GPIO0_VRTC_OUT			0x2
#define WM8350_GPIO0_LPWR1_IN			0x3
#define WM8350_GPIO0_POR_B_OUT			0x3

#define WM8350_GPIO1_GPIO_IN			0x0
#define WM8350_GPIO1_GPIO_OUT			0x0
#define WM8350_GPIO1_PWR_ON_IN			0x1
#define WM8350_GPIO1_DO_CONF_OUT		0x1
#define WM8350_GPIO1_LDO_EN_IN			0x2
#define WM8350_GPIO1_RESET_OUT			0x2
#define WM8350_GPIO1_LPWR2_IN			0x3
#define WM8350_GPIO1_MEMRST_OUT			0x3

#define WM8350_GPIO2_GPIO_IN			0x0
#define WM8350_GPIO2_GPIO_OUT			0x0
#define WM8350_GPIO2_PWR_ON_IN			0x1
#define WM8350_GPIO2_PWR_ON_OUT			0x1
#define WM8350_GPIO2_WAKE_UP_IN			0x2
#define WM8350_GPIO2_VRTC_OUT			0x2
#define WM8350_GPIO2_32KHZ_IN			0x3
#define WM8350_GPIO2_32KHZ_OUT			0x3

#define WM8350_GPIO3_GPIO_IN			0x0
#define WM8350_GPIO3_GPIO_OUT			0x0
#define WM8350_GPIO3_PWR_ON_IN			0x1
#define WM8350_GPIO3_P_CLK_OUT			0x1
#define WM8350_GPIO3_LDO_EN_IN			0x2
#define WM8350_GPIO3_VRTC_OUT			0x2
#define WM8350_GPIO3_PWR_OFF_IN			0x3
#define WM8350_GPIO3_32KHZ_OUT			0x3

#define WM8350_GPIO4_GPIO_IN			0x0
#define WM8350_GPIO4_GPIO_OUT			0x0
#define WM8350_GPIO4_MR_IN			0x1
#define WM8350_GPIO4_MEM_RST_OUT		0x1
#define WM8350_GPIO4_FLASH_IN			0x2
#define WM8350_GPIO4_ADA_OUT			0x2
#define WM8350_GPIO4_HIBERNATE_IN		0x3
#define WM8350_GPIO4_FLASH_OUT			0x3
#define WM8350_GPIO4_MICDET_OUT			0x4
#define WM8350_GPIO4_MICSHT_OUT			0x5

#define WM8350_GPIO5_GPIO_IN			0x0
#define WM8350_GPIO5_GPIO_OUT			0x0
#define WM8350_GPIO5_LPWR1_IN			0x1
#define WM8350_GPIO5_P_CLK_OUT			0x1
#define WM8350_GPIO5_ADCLRCLK_IN		0x2
#define WM8350_GPIO5_ADCLRCLK_OUT		0x2
#define WM8350_GPIO5_HIBERNATE_IN		0x3
#define WM8350_GPIO5_32KHZ_OUT			0x3
#define WM8350_GPIO5_MICDET_OUT			0x4
#define WM8350_GPIO5_MICSHT_OUT			0x5
#define WM8350_GPIO5_ADA_OUT			0x6
#define WM8350_GPIO5_OPCLK_OUT			0x7

#define WM8350_GPIO6_GPIO_IN			0x0
#define WM8350_GPIO6_GPIO_OUT			0x0
#define WM8350_GPIO6_LPWR2_IN			0x1
#define WM8350_GPIO6_MEMRST_OUT			0x1
#define WM8350_GPIO6_FLASH_IN			0x2
#define WM8350_GPIO6_ADA_OUT			0x2
#define WM8350_GPIO6_HIBERNATE_IN		0x3
#define WM8350_GPIO6_RTC_OUT			0x3
#define WM8350_GPIO6_MICDET_OUT			0x4
#define WM8350_GPIO6_MICSHT_OUT			0x5
#define WM8350_GPIO6_ADCLRCLKB_OUT		0x6
#define WM8350_GPIO6_SDOUT_OUT			0x7

#define WM8350_GPIO7_GPIO_IN			0x0
#define WM8350_GPIO7_GPIO_OUT			0x0
#define WM8350_GPIO7_LPWR3_IN			0x1
#define WM8350_GPIO7_P_CLK_OUT			0x1
#define WM8350_GPIO7_MASK_IN			0x2
#define WM8350_GPIO7_VCC_FAULT_OUT		0x2
#define WM8350_GPIO7_HIBERNATE_IN		0x3
#define WM8350_GPIO7_BATT_FAULT_OUT		0x3
#define WM8350_GPIO7_MICDET_OUT			0x4
#define WM8350_GPIO7_MICSHT_OUT			0x5
#define WM8350_GPIO7_ADA_OUT			0x6
#define WM8350_GPIO7_CSB_IN			0x7

#define WM8350_GPIO8_GPIO_IN			0x0
#define WM8350_GPIO8_GPIO_OUT			0x0
#define WM8350_GPIO8_MR_IN			0x1
#define WM8350_GPIO8_VCC_FAULT_OUT		0x1
#define WM8350_GPIO8_ADCBCLK_IN			0x2
#define WM8350_GPIO8_ADCBCLK_OUT		0x2
#define WM8350_GPIO8_PWR_OFF_IN			0x3
#define WM8350_GPIO8_BATT_FAULT_OUT		0x3
#define WM8350_GPIO8_ALTSCL_IN			0xf

#define WM8350_GPIO9_GPIO_IN			0x0
#define WM8350_GPIO9_GPIO_OUT			0x0
#define WM8350_GPIO9_HEARTBEAT_IN		0x1
#define WM8350_GPIO9_VCC_FAULT_OUT		0x1
#define WM8350_GPIO9_MASK_IN			0x2
#define WM8350_GPIO9_LINE_GT_BATT_OUT		0x2
#define WM8350_GPIO9_PWR_OFF_IN			0x3
#define WM8350_GPIO9_BATT_FAULT_OUT		0x3
#define WM8350_GPIO9_ALTSDA_OUT			0xf

#define WM8350_GPIO10_GPIO_IN			0x0
#define WM8350_GPIO10_GPIO_OUT			0x0
#define WM8350_GPIO10_ISINKC_OUT		0x1
#define WM8350_GPIO10_PWR_OFF_IN		0x2
#define WM8350_GPIO10_LINE_GT_BATT_OUT		0x2
#define WM8350_GPIO10_CHD_IND_IN		0x3

#define WM8350_GPIO11_GPIO_IN			0x0
#define WM8350_GPIO11_GPIO_OUT			0x0
#define WM8350_GPIO11_ISINKD_OUT		0x1
#define WM8350_GPIO11_WAKEUP_IN			0x2
#define WM8350_GPIO11_LINE_GT_BATT_OUT		0x2
#define WM8350_GPIO11_CHD_IND_IN		0x3

#define WM8350_GPIO12_GPIO_IN			0x0
#define WM8350_GPIO12_GPIO_OUT			0x0
#define WM8350_GPIO12_ISINKE_OUT		0x1
#define WM8350_GPIO12_LINE_GT_BATT_OUT		0x2
#define WM8350_GPIO12_LINE_EN_OUT		0x3
#define WM8350_GPIO12_32KHZ_OUT			0x4

#define WM8350_GPIO_DIR_IN			0
#define WM8350_GPIO_DIR_OUT			1
#define WM8350_GPIO_ACTIVE_LOW			0
#define WM8350_GPIO_ACTIVE_HIGH			1
#define WM8350_GPIO_PULL_NONE			0
#define WM8350_GPIO_PULL_UP			1
#define WM8350_GPIO_PULL_DOWN			2
#define WM8350_GPIO_INVERT_OFF			0
#define WM8350_GPIO_INVERT_ON			1
#define WM8350_GPIO_DEBOUNCE_OFF		0
#define WM8350_GPIO_DEBOUNCE_ON			1

/*
 * R128 (0x80) - GPIO Debounce
 */
#define WM8350_GP12_DB                          0x1000
#define WM8350_GP11_DB                          0x0800
#define WM8350_GP10_DB                          0x0400
#define WM8350_GP9_DB                           0x0200
#define WM8350_GP8_DB                           0x0100
#define WM8350_GP7_DB                           0x0080
#define WM8350_GP6_DB                           0x0040
#define WM8350_GP5_DB                           0x0020
#define WM8350_GP4_DB                           0x0010
#define WM8350_GP3_DB                           0x0008
#define WM8350_GP2_DB                           0x0004
#define WM8350_GP1_DB                           0x0002
#define WM8350_GP0_DB                           0x0001

/*
 * R129 (0x81) - GPIO Pin pull up Control
 */
#define WM8350_GP12_PU                          0x1000
#define WM8350_GP11_PU                          0x0800
#define WM8350_GP10_PU                          0x0400
#define WM8350_GP9_PU                           0x0200
#define WM8350_GP8_PU                           0x0100
#define WM8350_GP7_PU                           0x0080
#define WM8350_GP6_PU                           0x0040
#define WM8350_GP5_PU                           0x0020
#define WM8350_GP4_PU                           0x0010
#define WM8350_GP3_PU                           0x0008
#define WM8350_GP2_PU                           0x0004
#define WM8350_GP1_PU                           0x0002
#define WM8350_GP0_PU                           0x0001

/*
 * R130 (0x82) - GPIO Pull down Control
 */
#define WM8350_GP12_PD                          0x1000
#define WM8350_GP11_PD                          0x0800
#define WM8350_GP10_PD                          0x0400
#define WM8350_GP9_PD                           0x0200
#define WM8350_GP8_PD                           0x0100
#define WM8350_GP7_PD                           0x0080
#define WM8350_GP6_PD                           0x0040
#define WM8350_GP5_PD                           0x0020
#define WM8350_GP4_PD                           0x0010
#define WM8350_GP3_PD                           0x0008
#define WM8350_GP2_PD                           0x0004
#define WM8350_GP1_PD                           0x0002
#define WM8350_GP0_PD                           0x0001

/*
 * R131 (0x83) - GPIO Interrupt Mode
 */
#define WM8350_GP12_INTMODE                     0x1000
#define WM8350_GP11_INTMODE                     0x0800
#define WM8350_GP10_INTMODE                     0x0400
#define WM8350_GP9_INTMODE                      0x0200
#define WM8350_GP8_INTMODE                      0x0100
#define WM8350_GP7_INTMODE                      0x0080
#define WM8350_GP6_INTMODE                      0x0040
#define WM8350_GP5_INTMODE                      0x0020
#define WM8350_GP4_INTMODE                      0x0010
#define WM8350_GP3_INTMODE                      0x0008
#define WM8350_GP2_INTMODE                      0x0004
#define WM8350_GP1_INTMODE                      0x0002
#define WM8350_GP0_INTMODE                      0x0001

/*
 * R133 (0x85) - GPIO Control
 */
#define WM8350_GP_DBTIME_MASK                   0x00C0

/*
 * R134 (0x86) - GPIO Configuration (i/o)
 */
#define WM8350_GP12_DIR                         0x1000
#define WM8350_GP11_DIR                         0x0800
#define WM8350_GP10_DIR                         0x0400
#define WM8350_GP9_DIR                          0x0200
#define WM8350_GP8_DIR                          0x0100
#define WM8350_GP7_DIR                          0x0080
#define WM8350_GP6_DIR                          0x0040
#define WM8350_GP5_DIR                          0x0020
#define WM8350_GP4_DIR                          0x0010
#define WM8350_GP3_DIR                          0x0008
#define WM8350_GP2_DIR                          0x0004
#define WM8350_GP1_DIR                          0x0002
#define WM8350_GP0_DIR                          0x0001

/*
 * R135 (0x87) - GPIO Pin Polarity / Type
 */
#define WM8350_GP12_CFG                         0x1000
#define WM8350_GP11_CFG                         0x0800
#define WM8350_GP10_CFG                         0x0400
#define WM8350_GP9_CFG                          0x0200
#define WM8350_GP8_CFG                          0x0100
#define WM8350_GP7_CFG                          0x0080
#define WM8350_GP6_CFG                          0x0040
#define WM8350_GP5_CFG                          0x0020
#define WM8350_GP4_CFG                          0x0010
#define WM8350_GP3_CFG                          0x0008
#define WM8350_GP2_CFG                          0x0004
#define WM8350_GP1_CFG                          0x0002
#define WM8350_GP0_CFG                          0x0001

/*
 * R140 (0x8C) - GPIO Function Select 1
 */
#define WM8350_GP3_FN_MASK                      0xF000
#define WM8350_GP2_FN_MASK                      0x0F00
#define WM8350_GP1_FN_MASK                      0x00F0
#define WM8350_GP0_FN_MASK                      0x000F

/*
 * R141 (0x8D) - GPIO Function Select 2
 */
#define WM8350_GP7_FN_MASK                      0xF000
#define WM8350_GP6_FN_MASK                      0x0F00
#define WM8350_GP5_FN_MASK                      0x00F0
#define WM8350_GP4_FN_MASK                      0x000F

/*
 * R142 (0x8E) - GPIO Function Select 3
 */
#define WM8350_GP11_FN_MASK                     0xF000
#define WM8350_GP10_FN_MASK                     0x0F00
#define WM8350_GP9_FN_MASK                      0x00F0
#define WM8350_GP8_FN_MASK                      0x000F

/*
 * R143 (0x8F) - GPIO Function Select 4
 */
#define WM8350_GP12_FN_MASK                     0x000F

/*
 * R230 (0xE6) - GPIO Pin Status
 */
#define WM8350_GP12_LVL                         0x1000
#define WM8350_GP11_LVL                         0x0800
#define WM8350_GP10_LVL                         0x0400
#define WM8350_GP9_LVL                          0x0200
#define WM8350_GP8_LVL                          0x0100
#define WM8350_GP7_LVL                          0x0080
#define WM8350_GP6_LVL                          0x0040
#define WM8350_GP5_LVL                          0x0020
#define WM8350_GP4_LVL                          0x0010
#define WM8350_GP3_LVL                          0x0008
#define WM8350_GP2_LVL                          0x0004
#define WM8350_GP1_LVL                          0x0002
#define WM8350_GP0_LVL                          0x0001

struct wm8350;

int wm8350_gpio_config(struct wm8350 *wm8350, int gpio, int dir, int func,
		       int pol, int pull, int invert, int debounce);

struct wm8350_gpio {
	struct platform_device *pdev;
};

/*
 * GPIO Interrupts
 */
#define WM8350_IRQ_GPIO(x)                      (50 + x)

#endif
