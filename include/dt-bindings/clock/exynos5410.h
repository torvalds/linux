/*
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * Copyright (c) 2016 Krzysztof Kozlowski
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Device Tree binding constants for Exynos5421 clock controller.
*/

#ifndef _DT_BINDINGS_CLOCK_EXYNOS_5410_H
#define _DT_BINDINGS_CLOCK_EXYNOS_5410_H

/* core clocks */
#define CLK_FIN_PLL		1
#define CLK_FOUT_APLL		2
#define CLK_FOUT_CPLL		3
#define CLK_FOUT_MPLL		4
#define CLK_FOUT_BPLL		5
#define CLK_FOUT_KPLL		6

/* gate for special clocks (sclk) */
#define CLK_SCLK_UART0		128
#define CLK_SCLK_UART1		129
#define CLK_SCLK_UART2		130
#define CLK_SCLK_UART3		131
#define CLK_SCLK_MMC0		132
#define CLK_SCLK_MMC1		133
#define CLK_SCLK_MMC2		134
#define CLK_SCLK_USBD300	150
#define CLK_SCLK_USBD301	151
#define CLK_SCLK_USBPHY300	152
#define CLK_SCLK_USBPHY301	153
#define CLK_SCLK_PWM		155

/* gate clocks */
#define CLK_UART0		257
#define CLK_UART1		258
#define CLK_UART2		259
#define CLK_UART3		260
#define CLK_PWM			279
#define CLK_MCT			315
#define CLK_MMC0		351
#define CLK_MMC1		352
#define CLK_MMC2		353
#define CLK_USBH20		365
#define CLK_USBD300		366
#define CLK_USBD301		367

#define CLK_NR_CLKS		512

#endif /* _DT_BINDINGS_CLOCK_EXYNOS_5410_H */
