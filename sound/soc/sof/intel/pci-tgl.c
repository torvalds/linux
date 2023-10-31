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
	.ipc_supported_mask	= BIT(SOF_IPC_TYPE_3) | BIT(SOF_IPC_TYPE_4),
	.ipc_default		= SOF_IPC_TYPE_3,
	.dspless_mode_supported	= true,		/* Only supported for HDaudio */
	.default_fw_path = {
		[SOF_IPC_TYPE_3] = "intel/sof",
		[SOF_IPC_TYPE_4] = "intel/avs/tgl",
	},
	.default_lib_path = {
		[SOF_IPC_TYPE_4] = "intel/avs-lib/tgl",
	},
	.default_tplg_path = {
		[SOF_IPC_TYPE_3] = "intel/sof-tplg",
		[SOF_IPC_TYPE_4] = "intel/avs-tplg",
	},
	.default_fw_filename = {
		[SOF_IPC_TYPE_3] = "sof-tgl.ri",
		[SOF_IPC_TYPE_4] = "dsp_basefw.bin",
	},
	.nocodec_tplg_filename = "sof-tgl-nocodec.tplg",
	.ops = &sof_tgl_ops,
	.ops_init = sof_tgl_ops_init,
	.ops_free = hda_ops_free,
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
	.ipc_supported_mask	= BIT(SOF_IPC_TYPE_3) | BIT(SOF_IPC_TYPE_4),
	.ipc_default		= SOF_IPC_TYPE_3,
	.dspless_mode_supported	= true,		/* Only supported for HDaudio */
	.default_fw_path = {
		[SOF_IPC_TYPE_3] = "intel/sof",
		[SOF_IPC_TYPE_4] = "intel/avs/tgl-h",
	},
	.default_lib_path = {
		[SOF_IPC_TYPE_4] = "intel/avs-lib/tgl-h",
	},
	.default_tplg_path = {
		[SOF_IPC_TYPE_3] = "intel/sof-tplg",
		[SOF_IPC_TYPE_4] = "intel/avs-tplg",
	},
	.default_fw_filename = {
		[SOF_IPC_TYPE_3] = "sof-tgl-h.ri",
		[SOF_IPC_TYPE_4] = "dsp_basefw.bin",
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
	.ipc_supported_mask	= BIT(SOF_IPC_TYPE_3) | BIT(SOF_IPC_TYPE_4),
	.ipc_default		= SOF_IPC_TYPE_3,
	.dspless_mode_supported	= true,		/* Only supported for HDaudio */
	.default_fw_path = {
		[SOF_IPC_TYPE_3] = "intel/sof",
		[SOF_IPC_TYPE_4] = "intel/avs/ehl",
	},
	.default_lib_path = {
		[SOF_IPC_TYPE_4] = "intel/avs-lib/ehl",
	},
	.default_tplg_path = {
		[SOF_IPC_TYPE_3] = "intel/sof-tplg",
		[SOF_IPC_TYPE_4] = "intel/avs-tplg",
	},
	.default_fw_filename = {
		[SOF_IPC_TYPE_3] = "sof-ehl.ri",
		[SOF_IPC_TYPE_4] = "dsp_basefw.bin",
	},
	.nocodec_tplg_filename = "sof-ehl-nocodec.tplg",
	.ops = &sof_tgl_ops,
	.ops_init = sof_tgl_ops_init,
	.ops_free = hda_ops_free,
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
	.ipc_supported_mask	= BIT(SOF_IPC_TYPE_3) | BIT(SOF_IPC_TYPE_4),
	.ipc_default		= SOF_IPC_TYPE_3,
	.dspless_mode_supported	= true,		/* Only supported for HDaudio */
	.default_fw_path = {
		[SOF_IPC_TYPE_3] = "intel/sof",
		[SOF_IPC_TYPE_4] = "intel/avs/adl-s",
	},
	.default_lib_path = {
		[SOF_IPC_TYPE_4] = "intel/avs-lib/adl-s",
	},
	.default_tplg_path = {
		[SOF_IPC_TYPE_3] = "intel/sof-tplg",
		[SOF_IPC_TYPE_4] = "intel/avs-tplg",
	},
	.default_fw_filename = {
		[SOF_IPC_TYPE_3] = "sof-adl-s.ri",
		[SOF_IPC_TYPE_4] = "dsp_basefw.bin",
	},
	.nocodec_tplg_filename = "sof-adl-nocodec.tplg",
	.ops = &sof_tgl_ops,
	.ops_init = sof_tgl_ops_init,
	.ops_free = hda_ops_free,
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
	.ipc_supported_mask	= BIT(SOF_IPC_TYPE_3) | BIT(SOF_IPC_TYPE_4),
	.ipc_default		= SOF_IPC_TYPE_3,
	.dspless_mode_supported	= true,		/* Only supported for HDaudio */
	.default_fw_path = {
		[SOF_IPC_TYPE_3] = "intel/sof",
		[SOF_IPC_TYPE_4] = "intel/avs/adl",
	},
	.default_lib_path = {
		[SOF_IPC_TYPE_4] = "intel/avs-lib/adl",
	},
	.default_tplg_path = {
		[SOF_IPC_TYPE_3] = "intel/sof-tplg",
		[SOF_IPC_TYPE_4] = "intel/avs-tplg",
	},
	.default_fw_filename = {
		[SOF_IPC_TYPE_3] = "sof-adl.ri",
		[SOF_IPC_TYPE_4] = "dsp_basefw.bin",
	},
	.nocodec_tplg_filename = "sof-adl-nocodec.tplg",
	.ops = &sof_tgl_ops,
	.ops_init = sof_tgl_ops_init,
	.ops_free = hda_ops_free,
};

static const struct sof_dev_desc adl_n_desc = {
	.machines               = snd_soc_acpi_intel_adl_machines,
	.alt_machines           = snd_soc_acpi_intel_adl_sdw_machines,
	.use_acpi_target_states = true,
	.resindex_lpe_base      = 0,
	.resindex_pcicfg_base   = -1,
	.resindex_imr_base      = -1,
	.irqindex_host_ipc      = -1,
	.chip_info = &tgl_chip_info,
	.ipc_supported_mask	= BIT(SOF_IPC_TYPE_3) | BIT(SOF_IPC_TYPE_4),
	.ipc_default		= SOF_IPC_TYPE_3,
	.dspless_mode_supported	= true,		/* Only supported for HDaudio */
	.default_fw_path = {
		[SOF_IPC_TYPE_3] = "intel/sof",
		[SOF_IPC_TYPE_4] = "intel/avs/adl-n",
	},
	.default_lib_path = {
		[SOF_IPC_TYPE_4] = "intel/avs-lib/adl-n",
	},
	.default_tplg_path = {
		[SOF_IPC_TYPE_3] = "intel/sof-tplg",
		[SOF_IPC_TYPE_4] = "intel/avs-tplg",
	},
	.default_fw_filename = {
		[SOF_IPC_TYPE_3] = "sof-adl-n.ri",
		[SOF_IPC_TYPE_4] = "dsp_basefw.bin",
	},
	.nocodec_tplg_filename = "sof-adl-nocodec.tplg",
	.ops = &sof_tgl_ops,
	.ops_init = sof_tgl_ops_init,
	.ops_free = hda_ops_free,
};

static const struct sof_dev_desc rpls_desc = {
	.machines               = snd_soc_acpi_intel_rpl_machines,
	.alt_machines           = snd_soc_acpi_intel_rpl_sdw_machines,
	.use_acpi_target_states	= true,
	.resindex_lpe_base      = 0,
	.resindex_pcicfg_base   = -1,
	.resindex_imr_base      = -1,
	.irqindex_host_ipc      = -1,
	.chip_info = &adls_chip_info,
	.ipc_supported_mask	= BIT(SOF_IPC_TYPE_3) | BIT(SOF_IPC_TYPE_4),
	.ipc_default		= SOF_IPC_TYPE_3,
	.dspless_mode_supported	= true,		/* Only supported for HDaudio */
	.default_fw_path = {
		[SOF_IPC_TYPE_3] = "intel/sof",
		[SOF_IPC_TYPE_4] = "intel/avs/rpl-s",
	},
	.default_lib_path = {
		[SOF_IPC_TYPE_4] = "intel/avs-lib/rpl-s",
	},
	.default_tplg_path = {
		[SOF_IPC_TYPE_3] = "intel/sof-tplg",
		[SOF_IPC_TYPE_4] = "intel/avs-tplg",
	},
	.default_fw_filename = {
		[SOF_IPC_TYPE_3] = "sof-rpl-s.ri",
		[SOF_IPC_TYPE_4] = "dsp_basefw.bin",
	},
	.nocodec_tplg_filename = "sof-rpl-nocodec.tplg",
	.ops = &sof_tgl_ops,
	.ops_init = sof_tgl_ops_init,
	.ops_free = hda_ops_free,
};

static const struct sof_dev_desc rpl_desc = {
	.machines               = snd_soc_acpi_intel_rpl_machines,
	.alt_machines           = snd_soc_acpi_intel_rpl_sdw_machines,
	.use_acpi_target_states = true,
	.resindex_lpe_base      = 0,
	.resindex_pcicfg_base   = -1,
	.resindex_imr_base      = -1,
	.irqindex_host_ipc      = -1,
	.chip_info = &tgl_chip_info,
	.ipc_supported_mask	= BIT(SOF_IPC_TYPE_3) | BIT(SOF_IPC_TYPE_4),
	.ipc_default		= SOF_IPC_TYPE_3,
	.dspless_mode_supported	= true,		/* Only supported for HDaudio */
	.default_fw_path = {
		[SOF_IPC_TYPE_3] = "intel/sof",
		[SOF_IPC_TYPE_4] = "intel/avs/rpl",
	},
	.default_lib_path = {
		[SOF_IPC_TYPE_4] = "intel/avs-lib/rpl",
	},
	.default_tplg_path = {
		[SOF_IPC_TYPE_3] = "intel/sof-tplg",
		[SOF_IPC_TYPE_4] = "intel/avs-tplg",
	},
	.default_fw_filename = {
		[SOF_IPC_TYPE_3] = "sof-rpl.ri",
		[SOF_IPC_TYPE_4] = "dsp_basefw.bin",
	},
	.nocodec_tplg_filename = "sof-rpl-nocodec.tplg",
	.ops = &sof_tgl_ops,
	.ops_init = sof_tgl_ops_init,
	.ops_free = hda_ops_free,
};

/* PCI IDs */
static const struct pci_device_id sof_pci_ids[] = {
	{ PCI_DEVICE_DATA(INTEL, HDA_TGL_LP, &tgl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_TGL_H, &tglh_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_EHL_0, &ehl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_EHL_3, &ehl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_ADL_S, &adls_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_RPL_S, &rpls_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_ADL_P, &adl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_ADL_PS, &adl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_RPL_P_0, &rpl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_RPL_P_1, &rpl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_ADL_M, &adl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_ADL_PX, &adl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_RPL_M, &rpl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_RPL_PX, &rpl_desc) },
	{ PCI_DEVICE_DATA(INTEL, HDA_ADL_N, &adl_n_desc) },
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
