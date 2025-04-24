// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2024 Intel Corporation.
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
#include "ptl.h"

/* PantherLake ops */
static struct snd_sof_dsp_ops sof_ptl_ops;

static int sof_ptl_ops_init(struct snd_sof_dev *sdev)
{
	return sof_ptl_set_ops(sdev, &sof_ptl_ops);
}

static const struct sof_dev_desc ptl_desc = {
	.use_acpi_target_states	= true,
	.machines               = snd_soc_acpi_intel_ptl_machines,
	.alt_machines		= snd_soc_acpi_intel_ptl_sdw_machines,
	.resindex_lpe_base      = 0,
	.resindex_pcicfg_base   = -1,
	.resindex_imr_base      = -1,
	.irqindex_host_ipc      = -1,
	.chip_info		= &ptl_chip_info,
	.ipc_supported_mask	= BIT(SOF_IPC_TYPE_4),
	.ipc_default		= SOF_IPC_TYPE_4,
	.dspless_mode_supported	= true,
	.default_fw_path = {
		[SOF_IPC_TYPE_4] = "intel/sof-ipc4/ptl",
	},
	.default_lib_path = {
		[SOF_IPC_TYPE_4] = "intel/sof-ipc4-lib/ptl",
	},
	.default_tplg_path = {
		[SOF_IPC_TYPE_4] = "intel/sof-ipc4-tplg",
	},
	.default_fw_filename = {
		[SOF_IPC_TYPE_4] = "sof-ptl.ri",
	},
	.nocodec_tplg_filename = "sof-ptl-nocodec.tplg",
	.ops = &sof_ptl_ops,
	.ops_init = sof_ptl_ops_init,
};

/* PCI IDs */
static const struct pci_device_id sof_pci_ids[] = {
	{ PCI_DEVICE_DATA(INTEL, HDA_PTL, &ptl_desc) }, /* PTL */
	{ PCI_DEVICE_DATA(INTEL, HDA_PTL_H, &ptl_desc) }, /* PTL-H */
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, sof_pci_ids);

/* pci_driver definition */
static struct pci_driver snd_sof_pci_intel_ptl_driver = {
	.name = "sof-audio-pci-intel-ptl",
	.id_table = sof_pci_ids,
	.probe = hda_pci_intel_probe,
	.remove = sof_pci_remove,
	.shutdown = sof_pci_shutdown,
	.driver = {
		.pm = pm_ptr(&sof_pci_pm),
	},
};
module_pci_driver(snd_sof_pci_intel_ptl_driver);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("SOF support for PantherLake platforms");
MODULE_IMPORT_NS("SND_SOC_SOF_INTEL_HDA_GENERIC");
MODULE_IMPORT_NS("SND_SOC_SOF_INTEL_HDA_COMMON");
MODULE_IMPORT_NS("SND_SOC_SOF_PCI_DEV");
