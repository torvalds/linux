/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip Audio PWM driver
 *
 * Copyright (C) 2020 Fuzhou Rockchip Electronics Co., Ltd
 *
 */

#ifndef _ROCKCHIP_AUDIO_PWM_H
#define _ROCKCHIP_AUDIO_PWM_H

/* AUDIO PWM REGS offset */
#define AUDPWM_VERSION		(0x0000)
#define AUDPWM_XFER		(0x0004)
#define AUDPWM_SRC_CFG		(0x0008)
#define AUDPWM_PWM_CFG		(0x0010)
#define AUDPWM_PWM_ST		(0x0014)
#define AUDPWM_PWM_BUF_01	(0x0018)
#define AUDPWM_PWM_BUF_23	(0x001c)
#define AUDPWM_FIFO_CFG		(0x0020)
#define AUDPWM_FIFO_LVL		(0x0024)
#define AUDPWM_FIFO_INT_EN	(0x0028)
#define AUDPWM_FIFO_INT_ST	(0x002c)
#define AUDPWM_FIFO_ENTRY	(0x0080)

#define HIWORD_UPDATE(v, h, l)	(((v) << (l)) | (GENMASK((h), (l)) << 16))

/* Transfer Control Register */
#define AUDPWM_XFER_LSTOP	HIWORD_UPDATE(1, 1, 1)
#define AUDPWM_XFER_START	HIWORD_UPDATE(1, 0, 0)
#define AUDPWM_XFER_STOP	HIWORD_UPDATE(0, 0, 0)

/* Source Data Configuration Register */
#define AUDPWM_ALIGN_LEFT	HIWORD_UPDATE(1, 5, 5)
#define AUDPWM_ALIGN_RIGHT	HIWORD_UPDATE(0, 5, 5)
#define AUDPWM_SRC_WIDTH(x)	HIWORD_UPDATE((x) - 1, 4, 0)

/* PWM Configuration Register */
#define AUDPWM_SAMPLE_WIDTH(x)	HIWORD_UPDATE((x) - 8, 9, 8)
#define AUDPWM_LINEAR_INTERP_EN HIWORD_UPDATE(1, 4, 4)
#define AUDPWM_INTERP_RATE(x)	HIWORD_UPDATE((x), 3, 0)

/* FIFO Configuration Register */
#define AUDPWM_DMA_EN		HIWORD_UPDATE(1, 7, 7)
#define AUDPWM_DMA_DIS		HIWORD_UPDATE(0, 7, 7)
#define AUDPWM_DMA_WATERMARK(x)	HIWORD_UPDATE((x) - 1, 4, 0)

#endif /* _ROCKCHIP_AUDIO_PWM_H */
