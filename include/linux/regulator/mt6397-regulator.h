/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: Flora Fu <flora.fu@mediatek.com>
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

#ifndef __LINUX_REGULATOR_MT6397_H
#define __LINUX_REGULATOR_MT6397_H

enum {
	MT6397_ID_VPCA15 = 0,
	MT6397_ID_VPCA7,
	MT6397_ID_VSRAMCA15,
	MT6397_ID_VSRAMCA7,
	MT6397_ID_VCORE,
	MT6397_ID_VGPU,
	MT6397_ID_VDRM,
	MT6397_ID_VIO18 = 7,
	MT6397_ID_VTCXO,
	MT6397_ID_VA28,
	MT6397_ID_VCAMA,
	MT6397_ID_VIO28,
	MT6397_ID_VUSB,
	MT6397_ID_VMC,
	MT6397_ID_VMCH,
	MT6397_ID_VEMC3V3,
	MT6397_ID_VGP1,
	MT6397_ID_VGP2,
	MT6397_ID_VGP3,
	MT6397_ID_VGP4,
	MT6397_ID_VGP5,
	MT6397_ID_VGP6,
	MT6397_ID_VIBR,
	MT6397_ID_RG_MAX,
};

#define MT6397_MAX_REGULATOR	MT6397_ID_RG_MAX
#define MT6397_REGULATOR_ID97	0x97
#define MT6397_REGULATOR_ID91	0x91

#endif /* __LINUX_REGULATOR_MT6397_H */
