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
#include "shim.h"

static struct snd_soc_acpi_mach sof_tng_machines[] = {
	{
		.id = "INT343A",
		.drv_name = "edison",
		.sof_fw_filename = "sof-byt.ri",
		.sof_tplg_filename = "sof-byt.tplg",
	},
	{}
};

static const struct sof_dev_desc tng_desc = {
	.machines		= sof_tng_machines,
	.resindex_lpe_base	= 3,	/* IRAM, but subtract IRAM offset */
	.resindex_pcicfg_base	= -1,
	.resindex_imr_base	= 0,
	.irqindex_host_ipc	= -1,
	.resindex_dma_base	= -1,
	.chip_info = &tng_chip_info,
	.default_fw_path = "intel/sof",
	.default_tplg_path = "intel/sof-tplg",
	.default_fw_filename = "sof-byt.ri",
	.nocodec_tplg_filename = "sof-byt.tplg",
	.ops = &sof_tng_ops,
};

/* PCI IDs */
static const struct pci_device_id sof_pci_ids[] = {
	{ PCI_DEVICE(0x8086, 0x119a),
		.driver_data = (unsigned long)&tng_desc},
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, sof_pci_ids);

/* pci_driver definition */
static struct pci_driver snd_sof_pci_intel_tng_driver = {
	.name = "sof-audio-pci-intel-tng",
	.id_table = sof_pci_ids,
	.probe = sof_pci_probe,
	.remove = sof_pci_remove,
	.shutdown = sof_pci_shutdown,
	.driver = {
		.pm = &sof_pci_pm,
	},
};
module_pci_driver(snd_sof_pci_intel_tng_driver);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_IMPORT_NS(SND_SOC_SOF_MERRIFIELD);
MODULE_IMPORT_NS(SND_SOC_SOF_PCI_DEV);
