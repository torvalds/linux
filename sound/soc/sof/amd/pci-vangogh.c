// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license. When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2023 Advanced Micro Devices, Inc. All rights reserved.
//
// Authors: Venkata Prasad Potturu <venkataprasad.potturu@amd.com>

/*.
 * PCI interface for Vangogh ACP device
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <sound/sof.h>
#include <sound/soc-acpi.h>

#include "../ops.h"
#include "../sof-pci-dev.h"
#include "../../amd/mach-config.h"
#include "acp.h"
#include "acp-dsp-offset.h"

#define ACP5X_FUTURE_REG_ACLK_0 0x1864

static const struct sof_amd_acp_desc vangogh_chip_info = {
	.rev		= 5,
	.name		= "vangogh",
	.host_bridge_id = HOST_BRIDGE_VGH,
	.pgfsm_base	= ACP5X_PGFSM_BASE,
	.ext_intr_stat	= ACP5X_EXT_INTR_STAT,
	.dsp_intr_base	= ACP5X_DSP_SW_INTR_BASE,
	.sram_pte_offset = ACP5X_SRAM_PTE_OFFSET,
	.hw_semaphore_offset = ACP5X_AXI2DAGB_SEM_0,
	.acp_clkmux_sel = ACP5X_CLKMUX_SEL,
	.probe_reg_offset = ACP5X_FUTURE_REG_ACLK_0,
};

static const struct sof_dev_desc vangogh_desc = {
	.machines		= snd_soc_acpi_amd_vangogh_sof_machines,
	.resindex_lpe_base	= 0,
	.resindex_pcicfg_base	= -1,
	.resindex_imr_base	= -1,
	.irqindex_host_ipc	= -1,
	.chip_info		= &vangogh_chip_info,
	.ipc_supported_mask     = BIT(SOF_IPC),
	.ipc_default            = SOF_IPC,
	.default_fw_path	= {
		[SOF_IPC] = "amd/sof",
	},
	.default_tplg_path	= {
		[SOF_IPC] = "amd/sof-tplg",
	},
	.default_fw_filename	= {
		[SOF_IPC] = "sof-vangogh.ri",
	},
	.nocodec_tplg_filename	= "sof-acp.tplg",
	.ops			= &sof_vangogh_ops,
	.ops_init		= sof_vangogh_ops_init,
};

static int acp_pci_vgh_probe(struct pci_dev *pci, const struct pci_device_id *pci_id)
{
	unsigned int flag;

	if (pci->revision != ACP_VANGOGH_PCI_ID)
		return -ENODEV;

	flag = snd_amd_acp_find_config(pci);
	if (flag != FLAG_AMD_SOF && flag != FLAG_AMD_SOF_ONLY_DMIC)
		return -ENODEV;

	return sof_pci_probe(pci, pci_id);
};

static void acp_pci_vgh_remove(struct pci_dev *pci)
{
	sof_pci_remove(pci);
}

/* PCI IDs */
static const struct pci_device_id vgh_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, ACP_PCI_DEV_ID),
	.driver_data = (unsigned long)&vangogh_desc},
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, vgh_pci_ids);

/* pci_driver definition */
static struct pci_driver snd_sof_pci_amd_vgh_driver = {
	.name = KBUILD_MODNAME,
	.id_table = vgh_pci_ids,
	.probe = acp_pci_vgh_probe,
	.remove = acp_pci_vgh_remove,
	.driver = {
		.pm = &sof_pci_pm,
	},
};
module_pci_driver(snd_sof_pci_amd_vgh_driver);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_IMPORT_NS(SND_SOC_SOF_AMD_COMMON);
MODULE_IMPORT_NS(SND_SOC_SOF_PCI_DEV);
