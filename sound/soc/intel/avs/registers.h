/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2021-2022 Intel Corporation. All rights reserved.
 *
 * Authors: Cezary Rojewski <cezary.rojewski@intel.com>
 *          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
 */

#ifndef __SOUND_SOC_INTEL_AVS_REGS_H
#define __SOUND_SOC_INTEL_AVS_REGS_H

#define AZX_PCIREG_PGCTL		0x44
#define AZX_PCIREG_CGCTL		0x48
#define AZX_PGCTL_LSRMD_MASK		BIT(4)
#define AZX_CGCTL_MISCBDCGE_MASK	BIT(6)
#define AZX_VS_EM2_L1SEN		BIT(13)

/* Intel HD Audio General DSP Registers */
#define AVS_ADSP_GEN_BASE		0x0
#define AVS_ADSP_REG_ADSPCS		(AVS_ADSP_GEN_BASE + 0x04)
#define AVS_ADSP_REG_ADSPIC		(AVS_ADSP_GEN_BASE + 0x08)
#define AVS_ADSP_REG_ADSPIS		(AVS_ADSP_GEN_BASE + 0x0C)

#define AVS_ADSP_ADSPIC_IPC		BIT(0)
#define AVS_ADSP_ADSPIC_CLDMA		BIT(1)
#define AVS_ADSP_ADSPIS_IPC		BIT(0)
#define AVS_ADSP_ADSPIS_CLDMA		BIT(1)

#define AVS_ADSPCS_CRST_MASK(cm)	(cm)
#define AVS_ADSPCS_CSTALL_MASK(cm)	((cm) << 8)
#define AVS_ADSPCS_SPA_MASK(cm)		((cm) << 16)
#define AVS_ADSPCS_CPA_MASK(cm)		((cm) << 24)
#define AVS_MAIN_CORE_MASK		BIT(0)

#define AVS_ADSP_HIPCCTL_BUSY		BIT(0)
#define AVS_ADSP_HIPCCTL_DONE		BIT(1)

/* SKL Intel HD Audio Inter-Processor Communication Registers */
#define SKL_ADSP_IPC_BASE		0x40
#define SKL_ADSP_REG_HIPCT		(SKL_ADSP_IPC_BASE + 0x00)
#define SKL_ADSP_REG_HIPCTE		(SKL_ADSP_IPC_BASE + 0x04)
#define SKL_ADSP_REG_HIPCI		(SKL_ADSP_IPC_BASE + 0x08)
#define SKL_ADSP_REG_HIPCIE		(SKL_ADSP_IPC_BASE + 0x0C)
#define SKL_ADSP_REG_HIPCCTL		(SKL_ADSP_IPC_BASE + 0x10)

#define SKL_ADSP_HIPCI_BUSY		BIT(31)
#define SKL_ADSP_HIPCIE_DONE		BIT(30)
#define SKL_ADSP_HIPCT_BUSY		BIT(31)

/* Constants used when accessing SRAM, space shared with firmware */
#define AVS_FW_REG_BASE(adev)		((adev)->spec->sram_base_offset)
#define AVS_FW_REG_STATUS(adev)		(AVS_FW_REG_BASE(adev) + 0x0)
#define AVS_FW_REG_ERROR_CODE(adev)	(AVS_FW_REG_BASE(adev) + 0x4)

#define AVS_FW_REGS_SIZE		PAGE_SIZE
#define AVS_FW_REGS_WINDOW		0
/* DSP -> HOST communication window */
#define AVS_UPLINK_WINDOW		AVS_FW_REGS_WINDOW
/* HOST -> DSP communication window */
#define AVS_DOWNLINK_WINDOW		1
#define AVS_DEBUG_WINDOW		2

/* registry I/O helpers */
#define avs_sram_offset(adev, window_idx) \
	((adev)->spec->sram_base_offset + \
	 (adev)->spec->sram_window_size * (window_idx))

#define avs_sram_addr(adev, window_idx) \
	((adev)->dsp_ba + avs_sram_offset(adev, window_idx))

#define avs_uplink_addr(adev) \
	(avs_sram_addr(adev, AVS_UPLINK_WINDOW) + AVS_FW_REGS_SIZE)
#define avs_downlink_addr(adev) \
	avs_sram_addr(adev, AVS_DOWNLINK_WINDOW)

#endif /* __SOUND_SOC_INTEL_AVS_REGS_H */
