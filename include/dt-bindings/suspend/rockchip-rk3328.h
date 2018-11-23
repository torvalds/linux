/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Header providing constants for Rockchip suspend bindings.
 *
 * Copyright (C) 2018, Fuzhou Rockchip Electronics Co., Ltd
 * Author: XiaoDong.Huang
 */
#ifndef __DT_BINDINGS_ROCKCHIP_PM_H__
#define __DT_BINDINGS_ROCKCHIP_PM_H__
/******************************bits ops************************************/

#ifndef BIT
#define BIT(nr)				(1 << (nr))
#endif

#define RKPM_SLP_CTR_VOL_PWM0		BIT(10)
#define RKPM_SLP_CTR_VOL_PWM1		BIT(11)

#endif
