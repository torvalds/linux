// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2025 Intel Corporation.
//

#include <linux/module.h>
#include <linux/pci.h>
#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>
#include <sound/sof.h>
#include "../ops.h"
#include "../sof-pci-dev.h"

/* platform specific devices */
#include "hda.h"
#include "nvl.h"

/* PantherLake ops */
static struct snd_sof_dsp_ops sof_nvl_ops;

static int sof_nvl_ops_init(struct snd_sof_dev *sdev)
{
	return sof_nvl_set_ops(sdev, &sof_nvl_ops);
}

static const struct sof_dev_desc nvl_desc = {
	.use_acpi_target_states	= true,
	.machines               = snd_soc_acpi_intel_nvl_machines,
	.alt_machines		= snd_soc_acpi_intel_nvl_sdw_machines,
	.resindex_lpe_base      = 0,
	.resindex_pcicfg_base   = -1,
	.resindex_imr_base      = -1,
	.irqindex_host_ipc      = -1,
	.chip_info		= &nvl_chip_info,
	.ipc_supported_mask	= BIT(SOF_IPC_TYPE_4),
	.ipc_default		= SOF_IPC_TYPE_4,
	.dspless_mode_supported	= true,
	.on_demand_dsp_boot	= true,
	.default_fw_path = {
		[SOF_IPC_TYPE_4] = "intel/sof-ipc4/nvl",
	},
	.default_lib_path = {
		[SOF_IPC_TYPE_4] = "intel/sof-ipc4-lib/nvl",
	},
	.default_tplg_path = {
		[SOF_IPC_TYPE_4] = "intel/sof-ipc4-tplg",
	},
	.default_fw_filename = {
		[SOF_IPC_TYPE_4] = "sof-nvl.ri",
	},
	.nocodec_tplg_filename = "sof-nvl-nocodec.tplg",
	.ops = &sof_nvl_ops,
	.ops_init = sof_nvl_ops_init,
};

static const struct sof_dev_desc nvl_s_desc = {
	.use_acpi_target_states	= true,
	.machines               = snd_soc_acpi_intel_nvl_machines,
	.alt_machines		= snd_soc_acpi_intel_nvl_sdw_machines,
	.resindex_lpe_base      = 0,
	.resindex_pcicfg_base   = -1,
	.resindex_imr_base      = -1,
	.irqindex_host_ipc      = -1,
	.chip_info		= &nvl_s_chip_info,
	.ipc_supported_mask	= BIT(SOF_IPC_TYPE_4),
	.ipc_default		= SOF_IPC_TYPE_4,
	.dspless_mode_supported	= true,
	.on_demand_dsp_boot	= true,
	.default_fw_path = {
		[SOF_IPC_TYPE_4] = "intel/sof-ipc4/nvl-s",
	},
	.default_lib_path = {
		[SOF_IPC_TYPE_4] = "intel/sof-ipc4-lib/nvl-s",
	},
	.default_tplg_path = {
		[SOF_IPC_TYPE_4] = "intel/sof-ipc4-tplg",
	},
	.default_fw_filename = {
		[SOF_IPC_TYPE_4] = "sof-nvl-s.ri",
	},
	.nocodec_tplg_filename = "sof-nvl-nocodec.tplg",
	.ops = &sof_nvl_ops,
	.ops_init = sof_nvl_ops_init,
};

/* PCI IDs */
static const struct pci_device_id sof_pci_ids[] = {
	{ PCI_DEVICE_DATA(INTEL, HDA_NVL, &nvl_desc) }, /* NVL */
	{ PCI_DEVICE_DATA(INTEL, HDA_NVL_S, &nvl_s_desc) }, /* NVL-S */
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, sof_pci_ids);

/* pci_driver definition */
static struct pci_driver snd_sof_pci_intel_nvl_driver = {
	.name = "sof-audio-pci-intel-nvl",
	.id_table = sof_pci_ids,
	.probe = hda_pci_intel_probe,
	.remove = sof_pci_remove,
	.shutdown = sof_pci_shutdown,
	.driver = {
		.pm = pm_ptr(&sof_pci_pm),
	},
};
module_pci_driver(snd_sof_pci_intel_nvl_driver);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("SOF support for NovaLake platforms");
MODULE_IMPORT_NS("SND_SOC_SOF_INTEL_HDA_GENERIC");
MODULE_IMPORT_NS("SND_SOC_SOF_INTEL_HDA_COMMON");
MODULE_IMPORT_NS("SND_SOC_SOF_PCI_DEV");
