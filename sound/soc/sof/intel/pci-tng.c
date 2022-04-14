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
#include "atom.h"
#include "../sof-pci-dev.h"
#include "../sof-audio.h"

/* platform specific devices */
#include "shim.h"

static struct snd_soc_acpi_mach sof_tng_machines[] = {
	{
		.id = "INT343A",
		.drv_name = "edison",
		.sof_tplg_filename = "sof-byt.tplg",
	},
	{}
};

static const struct snd_sof_debugfs_map tng_debugfs[] = {
	{"dmac0", DSP_BAR, DMAC0_OFFSET, DMAC_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
	{"dmac1", DSP_BAR, DMAC1_OFFSET, DMAC_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
	{"ssp0", DSP_BAR, SSP0_OFFSET, SSP_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
	{"ssp1", DSP_BAR, SSP1_OFFSET, SSP_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
	{"ssp2", DSP_BAR, SSP2_OFFSET, SSP_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
	{"iram", DSP_BAR, IRAM_OFFSET, IRAM_SIZE,
	 SOF_DEBUGFS_ACCESS_D0_ONLY},
	{"dram", DSP_BAR, DRAM_OFFSET, DRAM_SIZE,
	 SOF_DEBUGFS_ACCESS_D0_ONLY},
	{"shim", DSP_BAR, SHIM_OFFSET, SHIM_SIZE_BYT,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
};

static int tangier_pci_probe(struct snd_sof_dev *sdev)
{
	struct snd_sof_pdata *pdata = sdev->pdata;
	const struct sof_dev_desc *desc = pdata->desc;
	struct pci_dev *pci = to_pci_dev(sdev->dev);
	const struct sof_intel_dsp_desc *chip;
	u32 base, size;
	int ret;

	chip = get_chip_info(sdev->pdata);
	if (!chip) {
		dev_err(sdev->dev, "error: no such device supported\n");
		return -EIO;
	}

	sdev->num_cores = chip->cores_num;

	/* DSP DMA can only access low 31 bits of host memory */
	ret = dma_coerce_mask_and_coherent(&pci->dev, DMA_BIT_MASK(31));
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to set DMA mask %d\n", ret);
		return ret;
	}

	/* LPE base */
	base = pci_resource_start(pci, desc->resindex_lpe_base) - IRAM_OFFSET;
	size = pci_resource_len(pci, desc->resindex_lpe_base);
	if (size < PCI_BAR_SIZE) {
		dev_err(sdev->dev, "error: I/O region is too small.\n");
		return -ENODEV;
	}

	dev_dbg(sdev->dev, "LPE PHY base at 0x%x size 0x%x", base, size);
	sdev->bar[DSP_BAR] = devm_ioremap(sdev->dev, base, size);
	if (!sdev->bar[DSP_BAR]) {
		dev_err(sdev->dev, "error: failed to ioremap LPE base 0x%x size 0x%x\n",
			base, size);
		return -ENODEV;
	}
	dev_dbg(sdev->dev, "LPE VADDR %p\n", sdev->bar[DSP_BAR]);

	/* IMR base - optional */
	if (desc->resindex_imr_base == -1)
		goto irq;

	base = pci_resource_start(pci, desc->resindex_imr_base);
	size = pci_resource_len(pci, desc->resindex_imr_base);

	/* some BIOSes don't map IMR */
	if (base == 0x55aa55aa || base == 0x0) {
		dev_info(sdev->dev, "IMR not set by BIOS. Ignoring\n");
		goto irq;
	}

	dev_dbg(sdev->dev, "IMR base at 0x%x size 0x%x", base, size);
	sdev->bar[IMR_BAR] = devm_ioremap(sdev->dev, base, size);
	if (!sdev->bar[IMR_BAR]) {
		dev_err(sdev->dev, "error: failed to ioremap IMR base 0x%x size 0x%x\n",
			base, size);
		return -ENODEV;
	}
	dev_dbg(sdev->dev, "IMR VADDR %p\n", sdev->bar[IMR_BAR]);

irq:
	/* register our IRQ */
	sdev->ipc_irq = pci->irq;
	dev_dbg(sdev->dev, "using IRQ %d\n", sdev->ipc_irq);
	ret = devm_request_threaded_irq(sdev->dev, sdev->ipc_irq,
					atom_irq_handler, atom_irq_thread,
					0, "AudioDSP", sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to register IRQ %d\n",
			sdev->ipc_irq);
		return ret;
	}

	/* enable BUSY and disable DONE Interrupt by default */
	snd_sof_dsp_update_bits64(sdev, DSP_BAR, SHIM_IMRX,
				  SHIM_IMRX_BUSY | SHIM_IMRX_DONE,
				  SHIM_IMRX_DONE);

	/* set default mailbox offset for FW ready message */
	sdev->dsp_box.offset = MBOX_OFFSET;

	return ret;
}

struct snd_sof_dsp_ops sof_tng_ops = {
	/* device init */
	.probe		= tangier_pci_probe,

	/* DSP core boot / reset */
	.run		= atom_run,
	.reset		= atom_reset,

	/* Register IO */
	.write		= sof_io_write,
	.read		= sof_io_read,
	.write64	= sof_io_write64,
	.read64		= sof_io_read64,

	/* Block IO */
	.block_read	= sof_block_read,
	.block_write	= sof_block_write,

	/* Mailbox IO */
	.mailbox_read	= sof_mailbox_read,
	.mailbox_write	= sof_mailbox_write,

	/* doorbell */
	.irq_handler	= atom_irq_handler,
	.irq_thread	= atom_irq_thread,

	/* ipc */
	.send_msg	= atom_send_msg,
	.fw_ready	= sof_fw_ready,
	.get_mailbox_offset = atom_get_mailbox_offset,
	.get_window_offset = atom_get_window_offset,

	.ipc_msg_data	= sof_ipc_msg_data,
	.set_stream_data_offset = sof_set_stream_data_offset,

	/* machine driver */
	.machine_select = atom_machine_select,
	.machine_register = sof_machine_register,
	.machine_unregister = sof_machine_unregister,
	.set_mach_params = atom_set_mach_params,

	/* debug */
	.debug_map	= tng_debugfs,
	.debug_map_count	= ARRAY_SIZE(tng_debugfs),
	.dbg_dump	= atom_dump,
	.debugfs_add_region_item = snd_sof_debugfs_add_region_item_iomem,

	/* stream callbacks */
	.pcm_open	= sof_stream_pcm_open,
	.pcm_close	= sof_stream_pcm_close,

	/* module loading */
	.load_module	= snd_sof_parse_module_memcpy,

	/*Firmware loading */
	.load_firmware	= snd_sof_load_firmware_memcpy,

	/* DAI drivers */
	.drv = atom_dai,
	.num_drv = 3, /* we have only 3 SSPs on byt*/

	/* ALSA HW info flags */
	.hw_info =	SNDRV_PCM_INFO_MMAP |
			SNDRV_PCM_INFO_MMAP_VALID |
			SNDRV_PCM_INFO_INTERLEAVED |
			SNDRV_PCM_INFO_PAUSE |
			SNDRV_PCM_INFO_BATCH,

	.dsp_arch_ops = &sof_xtensa_arch_ops,
};

const struct sof_intel_dsp_desc tng_chip_info = {
	.cores_num = 1,
	.host_managed_cores_mask = 1,
};

static const struct sof_dev_desc tng_desc = {
	.machines		= sof_tng_machines,
	.resindex_lpe_base	= 3,	/* IRAM, but subtract IRAM offset */
	.resindex_pcicfg_base	= -1,
	.resindex_imr_base	= 0,
	.irqindex_host_ipc	= -1,
	.chip_info = &tng_chip_info,
	.ipc_supported_mask	= BIT(SOF_IPC),
	.ipc_default		= SOF_IPC,
	.default_fw_path = {
		[SOF_IPC] = "intel/sof",
	},
	.default_tplg_path = {
		[SOF_IPC] = "intel/sof-tplg",
	},
	.default_fw_filename = {
		[SOF_IPC] = "sof-byt.ri",
	},
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
MODULE_IMPORT_NS(SND_SOC_SOF_INTEL_HIFI_EP_IPC);
MODULE_IMPORT_NS(SND_SOC_SOF_XTENSA);
MODULE_IMPORT_NS(SND_SOC_SOF_PCI_DEV);
MODULE_IMPORT_NS(SND_SOC_SOF_INTEL_ATOM_HIFI_EP);
