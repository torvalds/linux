// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018-2021 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
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

static const struct sof_dev_desc tgl_desc = {
	.machines               = snd_soc_acpi_intel_tgl_machines,
	.alt_machines		= snd_soc_acpi_intel_tgl_sdw_machines,
	.use_acpi_target_states	= true,
	.resindex_lpe_base      = 0,
	.resindex_pcicfg_base   = -1,
	.resindex_imr_base      = -1,
	.irqindex_host_ipc      = -1,
	.chip_info = &tgl_chip_info,
	.ipc_supported_mask	= BIT(SOF_IPC) | BIT(SOF_INTEL_IPC4),
	.ipc_default		= SOF_IPC,
	.default_fw_path = {
		[SOF_IPC] = "intel/sof",
		[SOF_INTEL_IPC4] = "intel/avs/tgl",
	},
	.default_tplg_path = {
		[SOF_IPC] = "intel/sof-tplg",
		[SOF_INTEL_IPC4] = "intel/avs-tplg",
	},
	.default_fw_filename = {
		[SOF_IPC] = "sof-tgl.ri",
		[SOF_INTEL_IPC4] = "dsp_basefw.bin",
	},
	.nocodec_tplg_filename = "sof-tgl-nocodec.tplg",
	.ops = &sof_tgl_ops,
	.ops_init = sof_tgl_ops_init,
};

static const struct sof_dev_desc tglh_desc = {
	.machines               = snd_soc_acpi_intel_tgl_machines,
	.alt_machines		= snd_soc_acpi_intel_tgl_sdw_machines,
	.use_acpi_target_states	= true,
	.resindex_lpe_base      = 0,
	.resindex_pcicfg_base   = -1,
	.resindex_imr_base      = -1,
	.irqindex_host_ipc      = -1,
	.chip_info = &tglh_chip_info,
	.ipc_supported_mask	= BIT(SOF_IPC) | BIT(SOF_INTEL_IPC4),
	.ipc_default		= SOF_IPC,
	.default_fw_path = {
		[SOF_IPC] = "intel/sof",
		[SOF_INTEL_IPC4] = "intel/avs/tgl-h",
	},
	.default_tplg_path = {
		[SOF_IPC] = "intel/sof-tplg",
		[SOF_INTEL_IPC4] = "intel/avs-tplg",
	},
	.default_fw_filename = {
		[SOF_IPC] = "sof-tgl-h.ri",
		[SOF_INTEL_IPC4] = "dsp_basefw.bin",
	},
	.nocodec_tplg_filename = "sof-tgl-nocodec.tplg",
	.ops = &sof_tgl_ops,
	.ops_init = sof_tgl_ops_init,
	.ops_free = hda_ops_free,
};

static const struct sof_dev_desc ehl_desc = {
	.machines               = snd_soc_acpi_intel_ehl_machines,
	.use_acpi_target_states	= true,
	.resindex_lpe_base      = 0,
	.resindex_pcicfg_base   = -1,
	.resindex_imr_base      = -1,
	.irqindex_host_ipc      = -1,
	.chip_info = &ehl_chip_info,
	.ipc_supported_mask	= BIT(SOF_IPC) | BIT(SOF_INTEL_IPC4),
	.ipc_default		= SOF_IPC,
	.default_fw_path = {
		[SOF_IPC] = "intel/sof",
		[SOF_INTEL_IPC4] = "intel/avs/ehl",
	},
	.default_tplg_path = {
		[SOF_IPC] = "intel/sof-tplg",
		[SOF_INTEL_IPC4] = "intel/avs-tplg",
	},
	.default_fw_filename = {
		[SOF_IPC] = "sof-ehl.ri",
		[SOF_INTEL_IPC4] = "dsp_basefw.bin",
	},
	.nocodec_tplg_filename = "sof-ehl-nocodec.tplg",
	.ops = &sof_tgl_ops,
	.ops_init = sof_tgl_ops_init,
};

static const struct sof_dev_desc adls_desc = {
	.machines               = snd_soc_acpi_intel_adl_machines,
	.alt_machines           = snd_soc_acpi_intel_adl_sdw_machines,
	.use_acpi_target_states	= true,
	.resindex_lpe_base      = 0,
	.resindex_pcicfg_base   = -1,
	.resindex_imr_base      = -1,
	.irqindex_host_ipc      = -1,
	.chip_info = &adls_chip_info,
	.ipc_supported_mask	= BIT(SOF_IPC) | BIT(SOF_INTEL_IPC4),
	.ipc_default		= SOF_IPC,
	.default_fw_path = {
		[SOF_IPC] = "intel/sof",
		[SOF_INTEL_IPC4] = "intel/avs/adl-s",
	},
	.default_tplg_path = {
		[SOF_IPC] = "intel/sof-tplg",
		[SOF_INTEL_IPC4] = "intel/avs-tplg",
	},
	.default_fw_filename = {
		[SOF_IPC] = "sof-adl-s.ri",
		[SOF_INTEL_IPC4] = "dsp_basefw.bin",
	},
	.nocodec_tplg_filename = "sof-adl-nocodec.tplg",
	.ops = &sof_tgl_ops,
	.ops_init = sof_tgl_ops_init,
};

static const struct sof_dev_desc adl_desc = {
	.machines               = snd_soc_acpi_intel_adl_machines,
	.alt_machines           = snd_soc_acpi_intel_adl_sdw_machines,
	.use_acpi_target_states = true,
	.resindex_lpe_base      = 0,
	.resindex_pcicfg_base   = -1,
	.resindex_imr_base      = -1,
	.irqindex_host_ipc      = -1,
	.chip_info = &tgl_chip_info,
	.ipc_supported_mask	= BIT(SOF_IPC) | BIT(SOF_INTEL_IPC4),
	.ipc_default		= SOF_IPC,
	.default_fw_path = {
		[SOF_IPC] = "intel/sof",
		[SOF_INTEL_IPC4] = "intel/avs/adl",
	},
	.default_tplg_path = {
		[SOF_IPC] = "intel/sof-tplg",
		[SOF_INTEL_IPC4] = "intel/avs-tplg",
	},
	.default_fw_filename = {
		[SOF_IPC] = "sof-adl.ri",
		[SOF_INTEL_IPC4] = "dsp_basefw.bin",
	},
	.nocodec_tplg_filename = "sof-adl-nocodec.tplg",
	.ops = &sof_tgl_ops,
	.ops_init = sof_tgl_ops_init,
};

/* PCI IDs */
static const struct pci_device_id sof_pci_ids[] = {
	{ PCI_DEVICE(0x8086, 0xa0c8), /* TGL-LP */
		.driver_data = (unsigned long)&tgl_desc},
	{ PCI_DEVICE(0x8086, 0x43c8), /* TGL-H */
		.driver_data = (unsigned long)&tglh_desc},
	{ PCI_DEVICE(0x8086, 0x4b55), /* EHL */
		.driver_data = (unsigned long)&ehl_desc},
	{ PCI_DEVICE(0x8086, 0x4b58), /* EHL */
		.driver_data = (unsigned long)&ehl_desc},
	{ PCI_DEVICE(0x8086, 0x7ad0), /* ADL-S */
		.driver_data = (unsigned long)&adls_desc},
	{ PCI_DEVICE(0x8086, 0x7a50), /* RPL-S */
		.driver_data = (unsigned long)&adls_desc},
	{ PCI_DEVICE(0x8086, 0x51c8), /* ADL-P */
		.driver_data = (unsigned long)&adl_desc},
	{ PCI_DEVICE(0x8086, 0x51cd), /* ADL-P */
		.driver_data = (unsigned long)&adl_desc},
	{ PCI_DEVICE(0x8086, 0x51c9), /* ADL-PS */
		.driver_data = (unsigned long)&adl_desc},
	{ PCI_DEVICE(0x8086, 0x51ca), /* RPL-P */
		.driver_data = (unsigned long)&adl_desc},
	{ PCI_DEVICE(0x8086, 0x51cb), /* RPL-P */
		.driver_data = (unsigned long)&adl_desc},
	{ PCI_DEVICE(0x8086, 0x51cc), /* ADL-M */
		.driver_data = (unsigned long)&adl_desc},
	{ PCI_DEVICE(0x8086, 0x54c8), /* ADL-N */
		.driver_data = (unsigned long)&adl_desc},
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, sof_pci_ids);

/* pci_driver definition */
static struct pci_driver snd_sof_pci_intel_tgl_driver = {
	.name = "sof-audio-pci-intel-tgl",
	.id_table = sof_pci_ids,
	.probe = hda_pci_intel_probe,
	.remove = sof_pci_remove,
	.shutdown = sof_pci_shutdown,
	.driver = {
		.pm = &sof_pci_pm,
	},
};
module_pci_driver(snd_sof_pci_intel_tgl_driver);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_IMPORT_NS(SND_SOC_SOF_INTEL_HDA_COMMON);
MODULE_IMPORT_NS(SND_SOC_SOF_PCI_DEV);
