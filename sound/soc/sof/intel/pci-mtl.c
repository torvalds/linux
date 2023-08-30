// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018-2022 Intel Corporation. All rights reserved.
//
// Author: Ranjani Sridharan <ranjani.sridharan@linux.intel.com>
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
#include "mtl.h"

static const struct sof_dev_desc mtl_desc = {
	.use_acpi_target_states	= true,
	.machines               = snd_soc_acpi_intel_mtl_machines,
	.alt_machines		= snd_soc_acpi_intel_mtl_sdw_machines,
	.resindex_lpe_base      = 0,
	.resindex_pcicfg_base   = -1,
	.resindex_imr_base      = -1,
	.irqindex_host_ipc      = -1,
	.chip_info = &mtl_chip_info,
	.ipc_supported_mask	= BIT(SOF_INTEL_IPC4),
	.ipc_default		= SOF_INTEL_IPC4,
	.dspless_mode_supported	= true,		/* Only supported for HDaudio */
	.default_fw_path = {
		[SOF_INTEL_IPC4] = "intel/sof-ipc4/mtl",
	},
	.default_lib_path = {
		[SOF_INTEL_IPC4] = "intel/sof-ipc4-lib/mtl",
	},
	.default_tplg_path = {
		[SOF_INTEL_IPC4] = "intel/sof-ace-tplg",
	},
	.default_fw_filename = {
		[SOF_INTEL_IPC4] = "sof-mtl.ri",
	},
	.nocodec_tplg_filename = "sof-mtl-nocodec.tplg",
	.ops = &sof_mtl_ops,
	.ops_init = sof_mtl_ops_init,
	.ops_free = hda_ops_free,
};

/* PCI IDs */
static const struct pci_device_id sof_pci_ids[] = {
	{ PCI_DEVICE(0x8086, 0x7E28), /* MTL */
		.driver_data = (unsigned long)&mtl_desc},
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, sof_pci_ids);

/* pci_driver definition */
static struct pci_driver snd_sof_pci_intel_mtl_driver = {
	.name = "sof-audio-pci-intel-mtl",
	.id_table = sof_pci_ids,
	.probe = hda_pci_intel_probe,
	.remove = sof_pci_remove,
	.shutdown = sof_pci_shutdown,
	.driver = {
		.pm = &sof_pci_pm,
	},
};
module_pci_driver(snd_sof_pci_intel_mtl_driver);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_IMPORT_NS(SND_SOC_SOF_INTEL_HDA_COMMON);
MODULE_IMPORT_NS(SND_SOC_SOF_PCI_DEV);
