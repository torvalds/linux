/* SPDX-License-Identifier: MIT */
/* Copyright © 2026 Intel Corporation */

#ifndef _INTEL_GMD_MISC_REGS_H_
#define _INTEL_GMD_MISC_REGS_H_

#define DISP_ARB_CTL	_MMIO(0x45000)
#define   DISP_FBC_MEMORY_WAKE		REG_BIT(31)
#define   DISP_TILE_SURFACE_SWIZZLING	REG_BIT(13)
#define   DISP_FBC_WM_DIS		REG_BIT(15)

#define INSTPM	        _MMIO(0x20c0)
#define   INSTPM_SELF_EN (1 << 12) /* 915GM only */
#define   INSTPM_AGPBUSY_INT_EN (1 << 11) /* gen3: when disabled, pending interrupts
					will not assert AGPBUSY# and will only
					be delivered when out of C3. */
#define   INSTPM_FORCE_ORDERING				(1 << 7) /* GEN6+ */
#define   INSTPM_TLB_INVALIDATE	(1 << 9)
#define   INSTPM_SYNC_FLUSH	(1 << 5)

#endif
