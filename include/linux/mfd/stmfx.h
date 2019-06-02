/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019 STMicroelectronics
 * Author(s): Amelie Delaunay <amelie.delaunay@st.com>.
 */

#ifndef MFD_STMFX_H
#define MFX_STMFX_H

#include <linux/regmap.h>

/* General */
#define STMFX_REG_CHIP_ID		0x00 /* R */
#define STMFX_REG_FW_VERSION_MSB	0x01 /* R */
#define STMFX_REG_FW_VERSION_LSB	0x02 /* R */
#define STMFX_REG_SYS_CTRL		0x40 /* RW */
/* IRQ output management */
#define STMFX_REG_IRQ_OUT_PIN		0x41 /* RW */
#define STMFX_REG_IRQ_SRC_EN		0x42 /* RW */
#define STMFX_REG_IRQ_PENDING		0x08 /* R */
#define STMFX_REG_IRQ_ACK		0x44 /* RW */
/* GPIO management */
#define STMFX_REG_IRQ_GPI_PENDING1	0x0C /* R */
#define STMFX_REG_IRQ_GPI_PENDING2	0x0D /* R */
#define STMFX_REG_IRQ_GPI_PENDING3	0x0E /* R */
#define STMFX_REG_GPIO_STATE1		0x10 /* R */
#define STMFX_REG_GPIO_STATE2		0x11 /* R */
#define STMFX_REG_GPIO_STATE3		0x12 /* R */
#define STMFX_REG_IRQ_GPI_SRC1		0x48 /* RW */
#define STMFX_REG_IRQ_GPI_SRC2		0x49 /* RW */
#define STMFX_REG_IRQ_GPI_SRC3		0x4A /* RW */
#define STMFX_REG_IRQ_GPI_EVT1		0x4C /* RW */
#define STMFX_REG_IRQ_GPI_EVT2		0x4D /* RW */
#define STMFX_REG_IRQ_GPI_EVT3		0x4E /* RW */
#define STMFX_REG_IRQ_GPI_TYPE1		0x50 /* RW */
#define STMFX_REG_IRQ_GPI_TYPE2		0x51 /* RW */
#define STMFX_REG_IRQ_GPI_TYPE3		0x52 /* RW */
#define STMFX_REG_IRQ_GPI_ACK1		0x54 /* RW */
#define STMFX_REG_IRQ_GPI_ACK2		0x55 /* RW */
#define STMFX_REG_IRQ_GPI_ACK3		0x56 /* RW */
#define STMFX_REG_GPIO_DIR1		0x60 /* RW */
#define STMFX_REG_GPIO_DIR2		0x61 /* RW */
#define STMFX_REG_GPIO_DIR3		0x62 /* RW */
#define STMFX_REG_GPIO_TYPE1		0x64 /* RW */
#define STMFX_REG_GPIO_TYPE2		0x65 /* RW */
#define STMFX_REG_GPIO_TYPE3		0x66 /* RW */
#define STMFX_REG_GPIO_PUPD1		0x68 /* RW */
#define STMFX_REG_GPIO_PUPD2		0x69 /* RW */
#define STMFX_REG_GPIO_PUPD3		0x6A /* RW */
#define STMFX_REG_GPO_SET1		0x6C /* RW */
#define STMFX_REG_GPO_SET2		0x6D /* RW */
#define STMFX_REG_GPO_SET3		0x6E /* RW */
#define STMFX_REG_GPO_CLR1		0x70 /* RW */
#define STMFX_REG_GPO_CLR2		0x71 /* RW */
#define STMFX_REG_GPO_CLR3		0x72 /* RW */

#define STMFX_REG_MAX			0xB0

/* MFX boot time is around 10ms, so after reset, we have to wait this delay */
#define STMFX_BOOT_TIME_MS 10

/* STMFX_REG_CHIP_ID bitfields */
#define STMFX_REG_CHIP_ID_MASK		GENMASK(7, 0)

/* STMFX_REG_SYS_CTRL bitfields */
#define STMFX_REG_SYS_CTRL_GPIO_EN	BIT(0)
#define STMFX_REG_SYS_CTRL_TS_EN	BIT(1)
#define STMFX_REG_SYS_CTRL_IDD_EN	BIT(2)
#define STMFX_REG_SYS_CTRL_ALTGPIO_EN	BIT(3)
#define STMFX_REG_SYS_CTRL_SWRST	BIT(7)

/* STMFX_REG_IRQ_OUT_PIN bitfields */
#define STMFX_REG_IRQ_OUT_PIN_TYPE	BIT(0) /* 0-OD 1-PP */
#define STMFX_REG_IRQ_OUT_PIN_POL	BIT(1) /* 0-active LOW 1-active HIGH */

/* STMFX_REG_IRQ_(SRC_EN/PENDING/ACK) bit shift */
enum stmfx_irqs {
	STMFX_REG_IRQ_SRC_EN_GPIO = 0,
	STMFX_REG_IRQ_SRC_EN_IDD,
	STMFX_REG_IRQ_SRC_EN_ERROR,
	STMFX_REG_IRQ_SRC_EN_TS_DET,
	STMFX_REG_IRQ_SRC_EN_TS_NE,
	STMFX_REG_IRQ_SRC_EN_TS_TH,
	STMFX_REG_IRQ_SRC_EN_TS_FULL,
	STMFX_REG_IRQ_SRC_EN_TS_OVF,
	STMFX_REG_IRQ_SRC_MAX,
};

enum stmfx_functions {
	STMFX_FUNC_GPIO		= BIT(0), /* GPIO[15:0] */
	STMFX_FUNC_ALTGPIO_LOW	= BIT(1), /* aGPIO[3:0] */
	STMFX_FUNC_ALTGPIO_HIGH = BIT(2), /* aGPIO[7:4] */
	STMFX_FUNC_TS		= BIT(3),
	STMFX_FUNC_IDD		= BIT(4),
};

/**
 * struct stmfx_ddata - STMFX MFD structure
 * @device:		device reference used for logs
 * @map:		register map
 * @vdd:		STMFX power supply
 * @irq_domain:		IRQ domain
 * @lock:		IRQ bus lock
 * @irq_src:		cache of IRQ_SRC_EN register for bus_lock
 * @bkp_sysctrl:	backup of SYS_CTRL register for suspend/resume
 * @bkp_irqoutpin:	backup of IRQ_OUT_PIN register for suspend/resume
 */
struct stmfx {
	struct device *dev;
	struct regmap *map;
	struct regulator *vdd;
	struct irq_domain *irq_domain;
	struct mutex lock; /* IRQ bus lock */
	u8 irq_src;
#ifdef CONFIG_PM
	u8 bkp_sysctrl;
	u8 bkp_irqoutpin;
#endif
};

int stmfx_function_enable(struct stmfx *stmfx, u32 func);
int stmfx_function_disable(struct stmfx *stmfx, u32 func);
#endif
