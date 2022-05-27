/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Defining registers address and its bit definitions of MAX96745
 *
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 */

#ifndef _MFD_MAX96745_H_
#define _MFD_MAX96745_H_

#include <linux/bitfield.h>

#define GPIO_A_REG(gpio)	(0x0200 + ((gpio) * 8))
#define GPIO_B_REG(gpio)	(0x0201 + ((gpio) * 8))
#define GPIO_C_REG(gpio)	(0x0202 + ((gpio) * 8))
#define GPIO_D_REG(gpio)	(0x0203 + ((gpio) * 8))

/* 0010h */
#define RESET_ALL		BIT(7)
#define SLEEP			BIT(3)

/* 0013h */
#define LOCKED			BIT(3)
#define ERROR			BIT(2)

/* 0028h, 0032h */
#define LINK_EN			BIT(7)

/* 0029h, 0033h */
#define RESET_LINK		BIT(0)
#define RESET_ONESHOT		BIT(1)

/* 002Ah, 0034h */
#define LINK_LOCKED		BIT(0)

/* 0076h, 0086h */
#define DIS_REM_CC		BIT(7)

/* 0100h */
#define VID_LINK_SEL		GENMASK(2, 1)
#define VID_TX_EN		BIT(0)

/* 0101h */
#define BPP			GENMASK(5, 0)

/* 0102h */
#define PCLKDET_A		BIT(7)
#define DRIFT_ERR_A		BIT(6)
#define OVERFLOW_A		BIT(5)
#define FIFO_WARN_A		BIT(4)
#define LIM_HEART		BIT(2)

/* 0107h */
#define VID_TX_ACTIVE_B		BIT(7)
#define VID_TX_ACTIVE_A		BIT(6)

/* 0108h */
#define PCLKDET_B		BIT(7)
#define DRIFT_ERR_B		BIT(6)
#define OVERFLOW_B		BIT(5)
#define FIFO_WARN_B		BIT(4)

/* 0200h */
#define RES_CFG			BIT(7)
#define TX_COM_EN		BIT(5)
#define GPIO_OUT		BIT(4)
#define GPIO_IN			BIT(3)
#define GPIO_OUT_DIS		BIT(0)

/* 0201h */
#define PULL_UPDN_SEL		GENMASK(7, 6)
#define OUT_TYPEC		BIT(5)
#define GPIO_TX_ID		GENMASK(4, 0)

/* 0202h */
#define OVR_RES_CFG		BIT(7)
#define IO_EDGE_RATE		GENMASK(6, 5)
#define GPIO_RX_ID		GENMASK(4, 0)

/* 0203h */
#define GPIO_IO_RX_EN		BIT(5)
#define GPIO_OUT_LGC		BIT(4)
#define GPIO_RX_EN_B		BIT(3)
#define GPIO_TX_EN_B		BIT(2)
#define GPIO_RX_EN_A		BIT(1)
#define GPIO_TX_EN_A		BIT(0)

/* 0750h */
#define FRCZEROPAD		GENMASK(7, 6)
#define FRCZPEN			BIT(5)
#define FRCSDGAIN		BIT(4)
#define FRCSDEN			BIT(3)
#define FRCGAIN			GENMASK(2, 1)
#define FRCEN			BIT(0)

/* 0751h */
#define FRCDATAWIDTH		BIT(3)
#define FRCASYNCEN		BIT(2)
#define FRCHSPOL		BIT(1)
#define FRCVSPOL		BIT(0)

/* 0752h */
#define FRCDCMODE		GENMASK(1, 0)

/* 7000h */
#define LINK_ENABLE		BIT(0)

/* 7070h */
#define MAX_LANE_COUNT		GENMASK(7, 0)

/* 7074h */
#define MAX_LINK_RATE		GENMASK(7, 0)

#endif /* _MFD_MAX96745_H_ */
