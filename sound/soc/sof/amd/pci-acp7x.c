// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license. When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2025 Advanced Micro Devices, Inc. All rights reserved.
//
// Authors: Vijendar Mukunda <Vijendar.Mukunda@amd.com>

/*
 * PCI interface for ACP7.B/7.F devices
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <sound/sof.h>
#include <sound/soc-acpi.h>

#include "../ops.h"
#include "../sof-pci-dev.h"
#include "../../amd/mach-config.h"
#include "acp.h"
#include "acp-dsp-offset.h"

#define ACP7X_FUTURE_REG_ACLK_0		0x18e0
#define ACP7X_REG_START			0x1240000
#define ACP7X_REG_END			0x125C000

static const struct sof_amd_acp_desc acp7x_chip_info = {
	.name		= "acp7x",
	.pgfsm_base	= ACP7X_PGFSM_BASE,
	.ext_intr_enb	= ACP6X_EXTERNAL_INTR_ENB,
	.ext_intr_cntl	= ACP7X_EXTERNAL_INTR_CNTL,
	.ext_intr_stat	= ACP7X_EXT_INTR_STAT,
	.ext_intr_stat1	= ACP7X_EXT_INTR_STAT1,
	.dsp_intr_base	= ACP7X_DSP_SW_INTR_BASE,
	.acp_error_stat	= ACP7X_ERROR_STATUS,
	.sram_pte_offset = ACP7X_SRAM_PTE_OFFSET,
	.hw_semaphore_offset = ACP7X_AXI2DAGB_SEM_0,
	.fusion_dsp_offset = ACP7X_DSP_FUSION_RUNSTALL,
	.probe_reg_offset = ACP7X_FUTURE_REG_ACLK_0,
	.reg_start_addr	= ACP7X_REG_START,
	.reg_end_addr	= ACP7X_REG_END,
};

static const struct sof_dev_desc acp7x_desc = {
	.machines		= snd_soc_acpi_amd_acp7x_sof_machines,
	.resindex_lpe_base	= 0,
	.resindex_pcicfg_base	= -1,
	.resindex_imr_base	= -1,
	.irqindex_host_ipc	= -1,
	.chip_info		= &acp7x_chip_info,
	.ipc_supported_mask	= BIT(SOF_IPC_TYPE_3),
	.ipc_default		= SOF_IPC_TYPE_3,
	.default_fw_path	= {
		[SOF_IPC_TYPE_3] = "amd/sof",
	},
	.default_tplg_path	= {
		[SOF_IPC_TYPE_3] = "amd/sof-tplg",
	},
	.default_fw_filename	= {
		[SOF_IPC_TYPE_3] = "sof-acp7x.ri",
	},
	.nocodec_tplg_filename	= "sof-acp.tplg",
	.ops			= &sof_acp7x_ops,
	.ops_init		= sof_acp7x_ops_init,
};

static int acp7x_pci_probe(struct pci_dev *pci, const struct pci_device_id *pci_id)
{
	unsigned int flag;

	switch (pci->revision) {
	case ACP7B_PCI_ID:
	case ACP7F_PCI_ID:
		break;
	default:
		return -ENODEV;
	}

	flag = snd_amd_acp_find_config(pci);
	if (flag != FLAG_AMD_SOF && flag != FLAG_AMD_SOF_ONLY_DMIC)
		return -ENODEV;

	return sof_pci_probe(pci, pci_id);
}

static void acp7x_pci_remove(struct pci_dev *pci)
{
	sof_pci_remove(pci);
}

/* PCI IDs */
static const struct pci_device_id acp7x_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, ACP_PCI_DEV_ID),
	.driver_data = (unsigned long)&acp7x_desc},
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, acp7x_pci_ids);

/* pci_driver definition */
static struct pci_driver snd_sof_pci_amd_acp7x_driver = {
	.name = KBUILD_MODNAME,
	.id_table = acp7x_pci_ids,
	.probe = acp7x_pci_probe,
	.remove = acp7x_pci_remove,
	.driver = {
		.pm = pm_ptr(&sof_pci_pm),
	},
};
module_pci_driver(snd_sof_pci_amd_acp7x_driver);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("ACP7X SOF Driver");
MODULE_IMPORT_NS("SND_SOC_SOF_AMD_COMMON");
MODULE_IMPORT_NS("SND_SOC_SOF_PCI_DEV");
