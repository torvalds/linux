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

static const struct sof_dev_desc bxt_desc = {
	.machines		= snd_soc_acpi_intel_bxt_machines,
	.use_acpi_target_states	= true,
	.resindex_lpe_base	= 0,
	.resindex_pcicfg_base	= -1,
	.resindex_imr_base	= -1,
	.irqindex_host_ipc	= -1,
	.chip_info = &apl_chip_info,
	.ipc_supported_mask	= BIT(SOF_IPC) | BIT(SOF_INTEL_IPC4),
	.ipc_default		= SOF_IPC,
	.default_fw_path = {
		[SOF_IPC] = "intel/sof",
		[SOF_INTEL_IPC4] = "intel/avs/apl",
	},
	.default_tplg_path = {
		[SOF_IPC] = "intel/sof-tplg",
		[SOF_INTEL_IPC4] = "intel/avs-tplg",
	},
	.default_fw_filename = "sof-apl.ri",
	.nocodec_tplg_filename = "sof-apl-nocodec.tplg",
	.ops = &sof_apl_ops,
};

static const struct sof_dev_desc glk_desc = {
	.machines		= snd_soc_acpi_intel_glk_machines,
	.use_acpi_target_states	= true,
	.resindex_lpe_base	= 0,
	.resindex_pcicfg_base	= -1,
	.resindex_imr_base	= -1,
	.irqindex_host_ipc	= -1,
	.chip_info = &apl_chip_info,
	.ipc_supported_mask	= BIT(SOF_IPC) | BIT(SOF_INTEL_IPC4),
	.ipc_default		= SOF_IPC,
	.default_fw_path = {
		[SOF_IPC] = "intel/sof",
		[SOF_INTEL_IPC4] = "intel/avs/glk",
	},
	.default_tplg_path = {
		[SOF_IPC] = "intel/sof-tplg",
		[SOF_INTEL_IPC4] = "intel/avs-tplg",
	},
	.default_fw_filename = "sof-glk.ri",
	.nocodec_tplg_filename = "sof-glk-nocodec.tplg",
	.ops = &sof_apl_ops,
};

/* PCI IDs */
static const struct pci_device_id sof_pci_ids[] = {
	{ PCI_DEVICE(0x8086, 0x5a98), /* BXT-P (ApolloLake) */
		.driver_data = (unsigned long)&bxt_desc},
	{ PCI_DEVICE(0x8086, 0x1a98),/* BXT-T */
		.driver_data = (unsigned long)&bxt_desc},
	{ PCI_DEVICE(0x8086, 0x3198), /* GeminiLake */
		.driver_data = (unsigned long)&glk_desc},
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, sof_pci_ids);

/* pci_driver definition */
static struct pci_driver snd_sof_pci_intel_apl_driver = {
	.name = "sof-audio-pci-intel-apl",
	.id_table = sof_pci_ids,
	.probe = hda_pci_intel_probe,
	.remove = sof_pci_remove,
	.shutdown = sof_pci_shutdown,
	.driver = {
		.pm = &sof_pci_pm,
	},
};
module_pci_driver(snd_sof_pci_intel_apl_driver);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_IMPORT_NS(SND_SOC_SOF_INTEL_HDA_COMMON);
MODULE_IMPORT_NS(SND_SOC_SOF_PCI_DEV);
