/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2021-2022 Intel Corporation. All rights reserved.
 *
 * Authors: Cezary Rojewski <cezary.rojewski@intel.com>
 *          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
 */

#ifndef __SOUND_SOC_INTEL_AVS_REGS_H
#define __SOUND_SOC_INTEL_AVS_REGS_H

/* Intel HD Audio General DSP Registers */
#define AVS_ADSP_GEN_BASE		0x0
#define AVS_ADSP_REG_ADSPCS		(AVS_ADSP_GEN_BASE + 0x04)

#define AVS_ADSPCS_CRST_MASK(cm)	(cm)
#define AVS_ADSPCS_CSTALL_MASK(cm)	((cm) << 8)
#define AVS_ADSPCS_SPA_MASK(cm)		((cm) << 16)
#define AVS_ADSPCS_CPA_MASK(cm)		((cm) << 24)
#define AVS_MAIN_CORE_MASK		BIT(0)

#endif /* __SOUND_SOC_INTEL_AVS_REGS_H */
