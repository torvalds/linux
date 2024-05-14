/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015 MediaTek Inc.
 * Author: Henry Chen <henryc.chen@mediatek.com>
 */

#ifndef __LINUX_REGULATOR_MT6311_H
#define __LINUX_REGULATOR_MT6311_H

#define MT6311_MAX_REGULATORS	2

enum {
	MT6311_ID_VDVFS = 0,
	MT6311_ID_VBIASN,
};

#define MT6311_E1_CID_CODE    0x10
#define MT6311_E2_CID_CODE    0x20
#define MT6311_E3_CID_CODE    0x30

#endif /* __LINUX_REGULATOR_MT6311_H */
