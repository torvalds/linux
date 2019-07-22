/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2014 Free Electrons
 * Copyright (C) 2014 Atmel
 *
 * Author: Boris BREZILLON <boris.brezillon@free-electrons.com>
 */

#ifndef __LINUX_MFD_HLCDC_H
#define __LINUX_MFD_HLCDC_H

#include <linux/clk.h>
#include <linux/regmap.h>

#define ATMEL_HLCDC_CFG(i)		((i) * 0x4)
#define ATMEL_HLCDC_SIG_CFG		LCDCFG(5)
#define ATMEL_HLCDC_HSPOL		BIT(0)
#define ATMEL_HLCDC_VSPOL		BIT(1)
#define ATMEL_HLCDC_VSPDLYS		BIT(2)
#define ATMEL_HLCDC_VSPDLYE		BIT(3)
#define ATMEL_HLCDC_DISPPOL		BIT(4)
#define ATMEL_HLCDC_DITHER		BIT(6)
#define ATMEL_HLCDC_DISPDLY		BIT(7)
#define ATMEL_HLCDC_MODE_MASK		GENMASK(9, 8)
#define ATMEL_HLCDC_PP			BIT(10)
#define ATMEL_HLCDC_VSPSU		BIT(12)
#define ATMEL_HLCDC_VSPHO		BIT(13)
#define ATMEL_HLCDC_GUARDTIME_MASK	GENMASK(20, 16)

#define ATMEL_HLCDC_EN			0x20
#define ATMEL_HLCDC_DIS			0x24
#define ATMEL_HLCDC_SR			0x28
#define ATMEL_HLCDC_IER			0x2c
#define ATMEL_HLCDC_IDR			0x30
#define ATMEL_HLCDC_IMR			0x34
#define ATMEL_HLCDC_ISR			0x38

#define ATMEL_HLCDC_CLKPOL		BIT(0)
#define ATMEL_HLCDC_CLKSEL		BIT(2)
#define ATMEL_HLCDC_CLKPWMSEL		BIT(3)
#define ATMEL_HLCDC_CGDIS(i)		BIT(8 + (i))
#define ATMEL_HLCDC_CLKDIV_SHFT		16
#define ATMEL_HLCDC_CLKDIV_MASK		GENMASK(23, 16)
#define ATMEL_HLCDC_CLKDIV(div)		((div - 2) << ATMEL_HLCDC_CLKDIV_SHFT)

#define ATMEL_HLCDC_PIXEL_CLK		BIT(0)
#define ATMEL_HLCDC_SYNC		BIT(1)
#define ATMEL_HLCDC_DISP		BIT(2)
#define ATMEL_HLCDC_PWM			BIT(3)
#define ATMEL_HLCDC_SIP			BIT(4)

#define ATMEL_HLCDC_SOF			BIT(0)
#define ATMEL_HLCDC_SYNCDIS		BIT(1)
#define ATMEL_HLCDC_FIFOERR		BIT(4)
#define ATMEL_HLCDC_LAYER_STATUS(x)	BIT((x) + 8)

/**
 * Structure shared by the MFD device and its subdevices.
 *
 * @regmap: register map used to access HLCDC IP registers
 * @periph_clk: the hlcdc peripheral clock
 * @sys_clk: the hlcdc system clock
 * @slow_clk: the system slow clk
 * @irq: the hlcdc irq
 */
struct atmel_hlcdc {
	struct regmap *regmap;
	struct clk *periph_clk;
	struct clk *sys_clk;
	struct clk *slow_clk;
	int irq;
};

#endif /* __LINUX_MFD_HLCDC_H */
