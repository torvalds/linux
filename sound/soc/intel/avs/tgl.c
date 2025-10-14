// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2024 Intel Corporation
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <linux/pci.h>
#include "avs.h"
#include "debug.h"
#include "messages.h"

#define CPUID_TSC_LEAF 0x15

static int avs_tgl_dsp_core_power(struct avs_dev *adev, u32 core_mask, bool power)
{
	core_mask &= AVS_MAIN_CORE_MASK;

	if (!core_mask)
		return 0;
	return avs_dsp_core_power(adev, core_mask, power);
}

static int avs_tgl_dsp_core_reset(struct avs_dev *adev, u32 core_mask, bool reset)
{
	core_mask &= AVS_MAIN_CORE_MASK;

	if (!core_mask)
		return 0;
	return avs_dsp_core_reset(adev, core_mask, reset);
}

static int avs_tgl_dsp_core_stall(struct avs_dev *adev, u32 core_mask, bool stall)
{
	core_mask &= AVS_MAIN_CORE_MASK;

	if (!core_mask)
		return 0;
	return avs_dsp_core_stall(adev, core_mask, stall);
}

static int avs_tgl_config_basefw(struct avs_dev *adev)
{
	struct pci_dev *pci = adev->base.pci;
	struct avs_bus_hwid hwid;
	int ret;
#ifdef CONFIG_X86
	unsigned int ecx;

#include <asm/cpuid/api.h>
	ecx = cpuid_ecx(CPUID_TSC_LEAF);
	if (ecx) {
		ret = avs_ipc_set_fw_config(adev, 1, AVS_FW_CFG_XTAL_FREQ_HZ, sizeof(ecx), &ecx);
		if (ret)
			return AVS_IPC_RET(ret);
	}
#endif

	hwid.device = pci->device;
	hwid.subsystem = pci->subsystem_vendor | (pci->subsystem_device << 16);
	hwid.revision = pci->revision;

	ret = avs_ipc_set_fw_config(adev, 1, AVS_FW_CFG_BUS_HARDWARE_ID, sizeof(hwid), &hwid);
	if (ret)
		return AVS_IPC_RET(ret);

	return 0;
}

const struct avs_dsp_ops avs_tgl_dsp_ops = {
	.power = avs_tgl_dsp_core_power,
	.reset = avs_tgl_dsp_core_reset,
	.stall = avs_tgl_dsp_core_stall,
	.dsp_interrupt = avs_cnl_dsp_interrupt,
	.int_control = avs_dsp_interrupt_control,
	.load_basefw = avs_icl_load_basefw,
	.load_lib = avs_hda_load_library,
	.transfer_mods = avs_hda_transfer_modules,
	.config_basefw = avs_tgl_config_basefw,
	.log_buffer_offset = avs_icl_log_buffer_offset,
	.log_buffer_status = avs_apl_log_buffer_status,
	.coredump = avs_apl_coredump,
	.d0ix_toggle = avs_icl_d0ix_toggle,
	.set_d0ix = avs_icl_set_d0ix,
	AVS_SET_ENABLE_LOGS_OP(icl)
};
