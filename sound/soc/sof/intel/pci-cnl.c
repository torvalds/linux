// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation
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

static const struct sof_dev_desc cnl_desc = {
	.machines		= snd_soc_acpi_intel_cnl_machines,
	.alt_machines		= snd_soc_acpi_intel_cnl_sdw_machines,
	.use_acpi_target_states	= true,
	.resindex_lpe_base	= 0,
	.resindex_pcicfg_base	= -1,
	.resindex_imr_base	= -1,
	.irqindex_host_ipc	= -1,
	.chip_info = &cnl_chip_info,
	.ipc_supported_mask	= BIT(SOF_IPC_TYPE_3) | BIT(SOF_IPC_TYPE_4),
	.ipc_default		= SOF_IPC_TYPE_3,
	.dspless_mode_supported	= true,		/* Only supported for HDaudio */
	.default_fw_path = {
		[SOF_IPC_TYPE_3] = "intel/sof",
		[SOF_IPC_TYPE_4] = "intel/avs/cnl",
	},
	.default_lib_path = {
		[SOF_IPC_TYPE_4] = "intel/avs-lib/cnl",
	},
	.default_tplg_path = {
		[SOF_IPC_TYPE_3] = "intel/sof-tplg",
		[SOF_IPC_TYPE_4] = "intel/avs-tplg",
	},
	.default_fw_filename = {
		[SOF_IPC_TYPE_3] = "sof-cnl.ri",
		[SOF_IPC_TYPE_4] = "dsp_basefw.bin",
	},
	.nocodec_tplg_filename = "sof-cnl-nocodec.tplg",
	.ops = &sof_cnl_ops,
	.ops_init = sof_cnl_ops_init,
	.ops_free = hda_ops_free,
};

static const struct sof_dev_desc cfl_desc = {
	.machines		= snd_soc_acpi_intel_cfl_machines,
	.alt_machines		= snd_soc_acpi_intel_cfl_sdw_machines,
	.use_acpi_target_states	= true,
	.resindex_lpe_base	= 0,
	.resindex_pcicfg_base	= -1,
	.resindex_imr_base	= -1,
	.irqindex_host_ipc	= -1,
	.chip_info = &cnl_chip_info,
	.ipc_supported_mask	= BIT(SOF_IPC_TYPE_3) | BIT(SOF_IPC_TYPE_4),
	.ipc_default		= SOF_IPC_TYPE_3,
	.dspless_mode_supported	= true,		/* Only supported for HDaudio */
	.default_fw_path = {
		[SOF_IPC_TYPE_3] = "intel/sof",
		[SOF_IPC_TYPE_4] = "intel/avs/cnl",
	},
	.default_lib_path = {
		[SOF_IPC_TYPE_4] = "intel/avs-lib/cnl",
	},
	.default_tplg_path = {
		[SOF_IPC_TYPE_3] = "intel/sof-tplg",
		[SOF_IPC_TYPE_4] = "intel/avs-tplg",
	},
	.default_fw_filename = {
		[SOF_IPC_TYPE_3] = "sof-cfl.ri",
		[SOF_IPC_TYPE_4] = "dsp_basefw.bin",
	},
	.nocodec_tplg_filename = "sof-cnl-nocodec.tplg",
	.ops = &sof_cnl_ops,
	.ops_init = sof_cnl_ops_init,
	.ops_free = hda_ops_free,
};

static const struct sof_dev_desc cml_desc = {
	.machines		= snd_soc_acpi_intel_cml_machines,
	.alt_machines		= snd_soc_acpi_intel_cml_sdw_machines,
	.use_acpi_target_states	= true,
	.resindex_lpe_base	= 0,
	.resindex_pcicfg_base	= -1,
	.resindex_imr_base	= -1,
	.irqindex_host_ipc	= -1,
	.chip_info = &cnl_chip_info,
	.ipc_supported_mask	= BIT(SOF_IPC_TYPE_3) | BIT(SOF_IPC_TYPE_4),
	.ipc_default		= SOF_IPC_TYPE_3,
	.dspless_mode_supported	= true,		/* Only supported for HDaudio */
	.default_fw_path = {
		[SOF_IPC_TYPE_3] = "intel/sof",
		[SOF_IPC_TYPE_4] = "intel/avs/cnl",
	},
	.default_lib_path = {
		[SOF_IPC_TYPE_4] = "intel/avs-lib/cnl",
	},
	.default_tplg_path = {
		[SOF_IPC_TYPE_3] = "intel/sof-tplg",
		[SOF_IPC_TYPE_4] = "intel/avs-tplg",
	},
	.default_fw_filename = {
		[SOF_IPC_TYPE_3] = "sof-cml.ri",
		[SOF_IPC_TYPE_4] = "dsp_basefw.bin",
	},
	.nocodec_tplg_filename = "sof-cnl-nocodec.tplg",
	.ops = &sof_cnl_ops,
	.ops_init = sof_cnl_ops_init,
	.ops_free = hda_ops_free,
};

/* PCI IDs */
static const struct pci_device_id sof_pci_ids[] = {
	{ PCI_DEVICE_DATA(INTEL, HDA_CNL_LP, &cnl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_CNL_H, &cfl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_CML_LP, &cml_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_CML_H, &cml_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_CML_S, &cml_desc) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, sof_pci_ids);

/* pci_driver definition */
static struct pci_driver snd_sof_pci_intel_cnl_driver = {
	.name = "sof-audio-pci-intel-cnl",
	.id_table = sof_pci_ids,
	.probe = hda_pci_intel_probe,
	.remove = sof_pci_remove,
	.shutdown = sof_pci_shutdown,
	.driver = {
		.pm = &sof_pci_pm,
	},
};
module_pci_driver(snd_sof_pci_intel_cnl_driver);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("SOF support for CannonLake platforms");
MODULE_IMPORT_NS(SND_SOC_SOF_INTEL_HDA_GENERIC);
MODULE_IMPORT_NS(SND_SOC_SOF_INTEL_HDA_COMMON);
MODULE_IMPORT_NS(SND_SOC_SOF_PCI_DEV);
