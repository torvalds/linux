/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Defining registers address and its bit definitions of MAX96752F
 *
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 */

#ifndef _MFD_MAX96755F_H_
#define _MFD_MAX96755F_H_

#include <linux/bitfield.h>

#define GPIO_A_REG(gpio)	(0x02be + ((gpio) * 3))
#define GPIO_B_REG(gpio)	(0x02bf + ((gpio) * 3))
#define GPIO_C_REG(gpio)	(0x02c0 + ((gpio) * 3))

/* 0000h */
#define DEV_ADDR		GENMASK(7, 1)
#define CFG_BLOCK		BIT(0)

/* 0001h */
#define IIC_2_EN		BIT(7)
#define IIC_1_EN		BIT(6)
#define DIS_REM_CC		BIT(4)
#define TX_RATE			GENMASK(3, 2)

/* 0002h */
#define VID_TX_EN_U		BIT(7)
#define VID_TX_EN_Z		BIT(6)
#define VID_TX_EN_Y		BIT(5)
#define VID_TX_EN_X		BIT(4)
#define AUD_TX_EN_Y		BIT(3)
#define AUD_TX_EN_X		BIT(2)

/* 0003h */
#define UART_2_EN		BIT(5)
#define UART_1_EN		BIT(4)

/* 0005h */
#define LOCK_EN			BIT(7)
#define ERRB_EN			BIT(6)
#define PU_LF3			BIT(3)
#define PU_LF2			BIT(2)
#define PU_LF1			BIT(1)
#define PU_LF0			BIT(0)

/* 0006h */
#define RCLKEN			BIT(5)

/* 0010h */
#define RESET_ALL		BIT(7)
#define RESET_LINK		BIT(6)
#define RESET_ONESHOT		BIT(5)
#define AUTO_LINK		BIT(4)
#define SLEEP			BIT(3)
#define REG_ENABLE		BIT(2)
#define LINK_CFG		GENMASK(1, 0)

/* 0013h */
#define LINK_MODE		GENMASK(5, 4)
#define	LOCKED			BIT(3)

/* 0048h */
#define REM_MS_EN		BIT(5)
#define LOC_MS_EN		BIT(4)

/* 0053h */
#define TX_SPLIT_MASK_B		BIT(5)
#define TX_SPLIT_MASK_A		BIT(4)
#define TX_STR_SEL		GENMASK(1, 0)

/* 0140h */
#define AUD_RX_EN		BIT(0)

/* 0170h */
#define SPI_EN			BIT(0)

/* 02beh */
#define RES_CFG			BIT(7)
#define TX_PRIO			BIT(6)
#define TX_COMP_EN		BIT(5)
#define GPIO_OUT		BIT(4)
#define GPIO_IN			BIT(3)
#define GPIO_RX_EN		BIT(2)
#define GPIO_TX_EN		BIT(1)
#define GPIO_OUT_DIS		BIT(0)

/* 02bfh */
#define PULL_UPDN_SEL		GENMASK(7, 6)
#define OUT_TYPE		BIT(5)
#define GPIO_TX_ID		GENMASK(4, 0)

/* 02c0h */
#define OVR_RES_CFG		BIT(7)
#define GPIO_RX_ID		GENMASK(4, 0)

/* 0311h */
#define START_PORTBU		BIT(7)
#define START_PORTBZ		BIT(6)
#define START_PORTBY		BIT(5)
#define START_PORTBX		BIT(4)
#define START_PORTAU		BIT(3)
#define START_PORTAZ		BIT(2)
#define START_PORTAY		BIT(1)
#define START_PORTAX		BIT(0)

/* 032ah */
#define DV_LOCK			BIT(7)
#define DV_SWP_AB		BIT(6)
#define LINE_ALT		BIT(5)
#define DV_CONV			BIT(2)
#define DV_SPL			BIT(1)
#define DV_EN			BIT(0)

/* 0330h */
#define PHY_CONFIG		GENMASK(2, 0)
#define MIPI_RX_RESET		BIT(3)

/* 0331h */
#define NUM_LANES		GENMASK(1, 0)

/* 0385h */
#define DPI_HSYNC_WIDTH_L	GENMASK(7, 0)

/* 0386h */
#define DPI_VYSNC_WIDTH_L	GENMASK(7, 0)

/* 0387h */
#define	DPI_HSYNC_WIDTH_H	GENMASK(3, 0)
#define DPI_VSYNC_WIDTH_H	GENMASK(7, 4)

/* 03a4h */
#define DPI_DE_SKEW_SEL		BIT(1)
#define DPI_DESKEW_EN		BIT(0)

/* 03a5h */
#define DPI_VFP_L		GENMASK(7, 0)

/* 03a6h */
#define DPI_VFP_H		GENMASK(3, 0)
#define DPI_VBP_L		GENMASK(7, 4)

/* 03a7h */
#define DPI_VBP_H		GENMASK(7, 0)

/* 03a8h */
#define DPI_VACT_L		GENMASK(7, 0)

/* 03a9h */
#define DPI_VACT_H		GENMASK(3, 0)

/* 03aah */
#define DPI_HFP_L		GENMASK(7, 0)

/* 03abh */
#define DPI_HFP_H		GENMASK(3, 0)
#define DPI_HBP_L		GENMASK(7, 4)

/* 03ach */
#define DPI_HBP_H		GENMASK(7, 0)

/* 03adh */
#define DPI_HACT_L		GENMASK(7, 0)

/* 03aeh */
#define DPI_HACT_H		GENMASK(4, 0)

enum link_mode {
	DUAL_LINK,
	LINKA,
	LINKB,
	SPLITTER_MODE,
};

#endif /* _MFD_MAX96755F_H_ */
