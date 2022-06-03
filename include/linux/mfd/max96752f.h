/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Defining registers address and its bit definitions of MAX96752F
 *
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 */

#ifndef _MFD_MAX96752F_H_
#define _MFD_MAX96752F_H_

#include <linux/bitfield.h>

#define GPIO_A_REG(gpio)	(0x0200 + ((gpio) * 3))
#define GPIO_B_REG(gpio)	(0x0201 + ((gpio) * 3))
#define GPIO_C_REG(gpio)	(0x0202 + ((gpio) * 3))
#define OLDI_REG(x)		(0x01cd + (x))

/* 0000h */
#define DEV_ADDR		GENMASK(7, 1)
#define CFG_BLOCK		BIT(0)

/* 0001h */
#define LVDS_HALFSW		BIT(7)
#define IIC_2_EN		BIT(5)
#define IIC_1_EN		BIT(4)
#define TX_RATE			GENMASK(3, 2)
#define RX_RATE			GENMASK(1, 0)

/* 0002h */
#define LOCK_CFG		BIT(7)
#define VID_EN			BIT(6)
#define DIS_LOCAL_CC		BIT(5)
#define DIS_REM_CC		BIT(4)
#define AUD_TX_EN		BIT(2)

/* 0003h */
#define GMSL2			BIT(5)
#define I2CSEL			BIT(4)
#define UART_2_EN		BIT(3)
#define UART_1_EN		BIT(2)
#define VIDEO_LOCK		BIT(0)

/* 000Dh */
#define DEV_ID			GENMASK(7, 0)

/* 000Eh */
#define DEV_REV			GENMASK(3, 0)

/* 0010h */
#define RESET_ALL		BIT(7)
#define RESET_LINK		BIT(6)
#define RESET_ONESHOT		BIT(5)
#define AUTO_LINK		BIT(4)
#define SLEEP			BIT(3)
#define LINK_CFG		GENMASK(1, 0)

/* 0050h */
#define STR_SEL			GENMASK(1, 0)

/* 0073h */
#define TX_SRC_ID		GENMASK(2, 0)

/* 0108h */
#define VID_LOCK		BIT(6)

/* 0140h */
#define AUD_RX_EN		BIT(0)

/* 01CEh */
#define OLDI_OUTSEL		BIT(7)
#define OLDI_FORMAT		BIT(6)
#define OLDI_4TH_LANE		BIT(5)
#define OLDI_SWAP_AB		BIT(4)
#define OLDI_SPL_EN		BIT(3)
#define OLDI_SPL_MODE		GENMASK(2, 1)
#define OLDI_SPL_POL		BIT(0)

/* 01CFh */
#define PD_LVDS_B		BIT(7)
#define PD_LVDS_A		BIT(6)
#define OLDI_DUP		BIT(1)
#define SSEN			BIT(0)

/* 0200h */
#define RES_CFG			BIT(7)
#define TX_PRIO			BIT(6)
#define TX_COMP_EN		BIT(5)
#define GPIO_OUT		BIT(4)
#define GPIO_IN			BIT(3)
#define GPIO_RX_EN		BIT(2)
#define GPIO_TX_EN		BIT(1)
#define GPIO_OUT_DIS		BIT(0)

/* 0201h */
#define PULL_UPDN_SEL		GENMASK(7, 6)
#define OUT_TYPE		BIT(5)
#define GPIO_TX_ID		GENMASK(4, 0)

/* 0202h */
#define OVR_RES_CFG		BIT(7)
#define GPIO_RX_ID		GENMASK(4, 0)

struct max96752f {
	struct device *dev;
	struct regmap *regmap;
	struct i2c_client *client;
	struct i2c_mux_core *muxc;
};

void max96752f_regcache_sync(struct max96752f *max96752f);

#endif /* _MFD_MAX96752F_H_ */
