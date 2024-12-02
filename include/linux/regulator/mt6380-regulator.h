/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017 MediaTek Inc.
 * Author: Chenglin Xu <chenglin.xu@mediatek.com>
 */

#ifndef __LINUX_REGULATOR_mt6380_H
#define __LINUX_REGULATOR_mt6380_H

enum {
	MT6380_ID_VCPU = 0,
	MT6380_ID_VCORE,
	MT6380_ID_VRF,
	MT6380_ID_VMLDO,
	MT6380_ID_VALDO,
	MT6380_ID_VPHYLDO,
	MT6380_ID_VDDRLDO,
	MT6380_ID_VTLDO,
	MT6380_ID_RG_MAX,
};

#define MT6380_MAX_REGULATOR	MT6380_ID_RG_MAX

#endif /* __LINUX_REGULATOR_mt6380_H */
