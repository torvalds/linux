/*
 * Copyright (c) 2015 MediaTek Inc.
 * Author: Henry Chen <henryc.chen@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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
