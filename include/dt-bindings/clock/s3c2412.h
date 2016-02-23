/*
 * Copyright (c) 2013 Heiko Stuebner <heiko@sntech.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Device Tree binding constants clock controllers of Samsung S3C2412.
 */

#ifndef _DT_BINDINGS_CLOCK_SAMSUNG_S3C2412_CLOCK_H
#define _DT_BINDINGS_CLOCK_SAMSUNG_S3C2412_CLOCK_H

/*
 * Let each exported clock get a unique index, which is used on DT-enabled
 * platforms to lookup the clock from a clock specifier. These indices are
 * therefore considered an ABI and so must not be changed. This implies
 * that new clocks should be added either in free spaces between clock groups
 * or at the end.
 */

/* Core clocks. */

/* id 1 is reserved */
#define MPLL			2
#define UPLL			3
#define MDIVCLK			4
#define MSYSCLK			5
#define USYSCLK			6
#define HCLK			7
#define PCLK			8
#define ARMDIV			9
#define ARMCLK			10


/* Special clocks */
#define SCLK_CAM		16
#define SCLK_UART		17
#define SCLK_I2S		18
#define SCLK_USBD		19
#define SCLK_USBH		20

/* pclk-gates */
#define PCLK_WDT		32
#define PCLK_SPI		33
#define PCLK_I2S		34
#define PCLK_I2C		35
#define PCLK_ADC		36
#define PCLK_RTC		37
#define PCLK_GPIO		38
#define PCLK_UART2		39
#define PCLK_UART1		40
#define PCLK_UART0		41
#define PCLK_SDI		42
#define PCLK_PWM		43
#define PCLK_USBD		44

/* hclk-gates */
#define HCLK_HALF		48
#define HCLK_X2			49
#define HCLK_SDRAM		50
#define HCLK_USBH		51
#define HCLK_LCD		52
#define HCLK_NAND		53
#define HCLK_DMA3		54
#define HCLK_DMA2		55
#define HCLK_DMA1		56
#define HCLK_DMA0		57

/* Total number of clocks. */
#define NR_CLKS			(HCLK_DMA0 + 1)

#endif /* _DT_BINDINGS_CLOCK_SAMSUNG_S3C2412_CLOCK_H */
