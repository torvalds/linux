/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Collabora Ltd.
 * Author: AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#ifndef __LINUX_REGULATOR_MT6332_H
#define __LINUX_REGULATOR_MT6332_H

enum {
	/* BUCK */
	MT6332_ID_VDRAM = 0,
	MT6332_ID_VDVFS2,
	MT6332_ID_VPA,
	MT6332_ID_VRF1,
	MT6332_ID_VRF2,
	MT6332_ID_VSBST,
	/* LDO */
	MT6332_ID_VAUXB32,
	MT6332_ID_VBIF28,
	MT6332_ID_VDIG18,
	MT6332_ID_VSRAM_DVFS2,
	MT6332_ID_VUSB33,
	MT6332_ID_VREG_MAX
};

#endif /* __LINUX_REGULATOR_MT6332_H */
