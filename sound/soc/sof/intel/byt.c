// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//

/*
 * Hardware interface for audio DSP on Baytrail, Braswell and Cherrytrail.
 */

#include <linux/module.h>
#include <sound/sof.h>
#include <sound/sof/xtensa.h>
#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>
#include <sound/intel-dsp-config.h>
#include "../ops.h"
#include "atom.h"
#include "shim.h"
#include "../sof-acpi-dev.h"
#include "../sof-audio.h"
#include "../../intel/common/soc-intel-quirks.h"

static const struct snd_sof_debugfs_map byt_debugfs[] = {
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

static const struct snd_sof_debugfs_map cht_debugfs[] = {
	{"dmac0", DSP_BAR, DMAC0_OFFSET, DMAC_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
	{"dmac1", DSP_BAR, DMAC1_OFFSET, DMAC_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
	{"dmac2", DSP_BAR, DMAC2_OFFSET, DMAC_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
	{"ssp0", DSP_BAR, SSP0_OFFSET, SSP_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
	{"ssp1", DSP_BAR, SSP1_OFFSET, SSP_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
	{"ssp2", DSP_BAR, SSP2_OFFSET, SSP_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
	{"ssp3", DSP_BAR, SSP3_OFFSET, SSP_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
	{"ssp4", DSP_BAR, SSP4_OFFSET, SSP_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
	{"ssp5", DSP_BAR, SSP5_OFFSET, SSP_SIZE,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
	{"iram", DSP_BAR, IRAM_OFFSET, IRAM_SIZE,
	 SOF_DEBUGFS_ACCESS_D0_ONLY},
	{"dram", DSP_BAR, DRAM_OFFSET, DRAM_SIZE,
	 SOF_DEBUGFS_ACCESS_D0_ONLY},
	{"shim", DSP_BAR, SHIM_OFFSET, SHIM_SIZE_CHT,
	 SOF_DEBUGFS_ACCESS_ALWAYS},
};

static void byt_reset_dsp_disable_int(struct snd_sof_dev *sdev)
{
	/* Disable Interrupt from both sides */
	snd_sof_dsp_update_bits64(sdev, DSP_BAR, SHIM_IMRX, 0x3, 0x3);
	snd_sof_dsp_update_bits64(sdev, DSP_BAR, SHIM_IMRD, 0x3, 0x3);

	/* Put DSP into reset, set reset vector */
	snd_sof_dsp_update_bits64(sdev, DSP_BAR, SHIM_CSR,
				  SHIM_BYT_CSR_RST | SHIM_BYT_CSR_VECTOR_SEL,
				  SHIM_BYT_CSR_RST | SHIM_BYT_CSR_VECTOR_SEL);
}

static int byt_suspend(struct snd_sof_dev *sdev, u32 target_state)
{
	byt_reset_dsp_disable_int(sdev);

	return 0;
}

static int byt_resume(struct snd_sof_dev *sdev)
{
	/* enable BUSY and disable DONE Interrupt by default */
	snd_sof_dsp_update_bits64(sdev, DSP_BAR, SHIM_IMRX,
				  SHIM_IMRX_BUSY | SHIM_IMRX_DONE,
				  SHIM_IMRX_DONE);

	return 0;
}

static int byt_remove(struct snd_sof_dev *sdev)
{
	byt_reset_dsp_disable_int(sdev);

	return 0;
}

static int byt_acpi_probe(struct snd_sof_dev *sdev)
{
	struct snd_sof_pdata *pdata = sdev->pdata;
	const struct sof_dev_desc *desc = pdata->desc;
	struct platform_device *pdev =
		container_of(sdev->dev, struct platform_device, dev);
	const struct sof_intel_dsp_desc *chip;
	struct resource *mmio;
	u32 base, size;
	int ret;

	chip = get_chip_info(sdev->pdata);
	if (!chip) {
		dev_err(sdev->dev, "error: no such device supported\n");
		return -EIO;
	}

	sdev->num_cores = chip->cores_num;

	/* DSP DMA can only access low 31 bits of host memory */
	ret = dma_coerce_mask_and_coherent(sdev->dev, DMA_BIT_MASK(31));
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to set DMA mask %d\n", ret);
		return ret;
	}

	/* LPE base */
	mmio = platform_get_resource(pdev, IORESOURCE_MEM,
				     desc->resindex_lpe_base);
	if (mmio) {
		base = mmio->start;
		size = resource_size(mmio);
	} else {
		dev_err(sdev->dev, "error: failed to get LPE base at idx %d\n",
			desc->resindex_lpe_base);
		return -EINVAL;
	}

	dev_dbg(sdev->dev, "LPE PHY base at 0x%x size 0x%x", base, size);
	sdev->bar[DSP_BAR] = devm_ioremap(sdev->dev, base, size);
	if (!sdev->bar[DSP_BAR]) {
		dev_err(sdev->dev, "error: failed to ioremap LPE base 0x%x size 0x%x\n",
			base, size);
		return -ENODEV;
	}
	dev_dbg(sdev->dev, "LPE VADDR %p\n", sdev->bar[DSP_BAR]);

	/* TODO: add offsets */
	sdev->mmio_bar = DSP_BAR;
	sdev->mailbox_bar = DSP_BAR;

	/* IMR base - optional */
	if (desc->resindex_imr_base == -1)
		goto irq;

	mmio = platform_get_resource(pdev, IORESOURCE_MEM,
				     desc->resindex_imr_base);
	if (mmio) {
		base = mmio->start;
		size = resource_size(mmio);
	} else {
		dev_err(sdev->dev, "error: failed to get IMR base at idx %d\n",
			desc->resindex_imr_base);
		return -ENODEV;
	}

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
	sdev->ipc_irq = platform_get_irq(pdev, desc->irqindex_host_ipc);
	if (sdev->ipc_irq < 0)
		return sdev->ipc_irq;

	dev_dbg(sdev->dev, "using IRQ %d\n", sdev->ipc_irq);
	ret = devm_request_threaded_irq(sdev->dev, sdev->ipc_irq,
					atom_irq_handler, atom_irq_thread,
					IRQF_SHARED, "AudioDSP", sdev);
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

/* baytrail ops */
static const struct snd_sof_dsp_ops sof_byt_ops = {
	/* device init */
	.probe		= byt_acpi_probe,
	.remove		= byt_remove,

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
	.debug_map	= byt_debugfs,
	.debug_map_count	= ARRAY_SIZE(byt_debugfs),
	.dbg_dump	= atom_dump,
	.debugfs_add_region_item = snd_sof_debugfs_add_region_item_iomem,

	/* stream callbacks */
	.pcm_open	= sof_stream_pcm_open,
	.pcm_close	= sof_stream_pcm_close,

	/* module loading */
	.load_module	= snd_sof_parse_module_memcpy,

	/*Firmware loading */
	.load_firmware	= snd_sof_load_firmware_memcpy,

	/* PM */
	.suspend = byt_suspend,
	.resume = byt_resume,

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

static const struct sof_intel_dsp_desc byt_chip_info = {
	.cores_num = 1,
	.host_managed_cores_mask = 1,
};

/* cherrytrail and braswell ops */
static const struct snd_sof_dsp_ops sof_cht_ops = {
	/* device init */
	.probe		= byt_acpi_probe,
	.remove		= byt_remove,

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
	.debug_map	= cht_debugfs,
	.debug_map_count	= ARRAY_SIZE(cht_debugfs),
	.dbg_dump	= atom_dump,
	.debugfs_add_region_item = snd_sof_debugfs_add_region_item_iomem,

	/* stream callbacks */
	.pcm_open	= sof_stream_pcm_open,
	.pcm_close	= sof_stream_pcm_close,

	/* module loading */
	.load_module	= snd_sof_parse_module_memcpy,

	/*Firmware loading */
	.load_firmware	= snd_sof_load_firmware_memcpy,

	/* PM */
	.suspend = byt_suspend,
	.resume = byt_resume,

	/* DAI drivers */
	.drv = atom_dai,
	/* all 6 SSPs may be available for cherrytrail */
	.num_drv = 6,

	/* ALSA HW info flags */
	.hw_info =	SNDRV_PCM_INFO_MMAP |
			SNDRV_PCM_INFO_MMAP_VALID |
			SNDRV_PCM_INFO_INTERLEAVED |
			SNDRV_PCM_INFO_PAUSE |
			SNDRV_PCM_INFO_BATCH,

	.dsp_arch_ops = &sof_xtensa_arch_ops,
};

static const struct sof_intel_dsp_desc cht_chip_info = {
	.cores_num = 1,
	.host_managed_cores_mask = 1,
};

/* BYTCR uses different IRQ index */
static const struct sof_dev_desc sof_acpi_baytrailcr_desc = {
	.machines = snd_soc_acpi_intel_baytrail_machines,
	.resindex_lpe_base = 0,
	.resindex_pcicfg_base = 1,
	.resindex_imr_base = 2,
	.irqindex_host_ipc = 0,
	.chip_info = &byt_chip_info,
	.default_fw_path = {
		[SOF_IPC] = "intel/sof",
	},
	.default_tplg_path = {
		[SOF_IPC] = "intel/sof-tplg",
	},
	.default_fw_filename = "sof-byt.ri",
	.nocodec_tplg_filename = "sof-byt-nocodec.tplg",
	.ops = &sof_byt_ops,
};

static const struct sof_dev_desc sof_acpi_baytrail_desc = {
	.machines = snd_soc_acpi_intel_baytrail_machines,
	.resindex_lpe_base = 0,
	.resindex_pcicfg_base = 1,
	.resindex_imr_base = 2,
	.irqindex_host_ipc = 5,
	.chip_info = &byt_chip_info,
	.default_fw_path = {
		[SOF_IPC] = "intel/sof",
	},
	.default_tplg_path = {
		[SOF_IPC] = "intel/sof-tplg",
	},
	.default_fw_filename = "sof-byt.ri",
	.nocodec_tplg_filename = "sof-byt-nocodec.tplg",
	.ops = &sof_byt_ops,
};

static const struct sof_dev_desc sof_acpi_cherrytrail_desc = {
	.machines = snd_soc_acpi_intel_cherrytrail_machines,
	.resindex_lpe_base = 0,
	.resindex_pcicfg_base = 1,
	.resindex_imr_base = 2,
	.irqindex_host_ipc = 5,
	.chip_info = &cht_chip_info,
	.default_fw_path = {
		[SOF_IPC] = "intel/sof",
	},
	.default_tplg_path = {
		[SOF_IPC] = "intel/sof-tplg",
	},
	.default_fw_filename = "sof-cht.ri",
	.nocodec_tplg_filename = "sof-cht-nocodec.tplg",
	.ops = &sof_cht_ops,
};

static const struct acpi_device_id sof_baytrail_match[] = {
	{ "80860F28", (unsigned long)&sof_acpi_baytrail_desc },
	{ "808622A8", (unsigned long)&sof_acpi_cherrytrail_desc },
	{ }
};
MODULE_DEVICE_TABLE(acpi, sof_baytrail_match);

static int sof_baytrail_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct sof_dev_desc *desc;
	const struct acpi_device_id *id;
	int ret;

	id = acpi_match_device(dev->driver->acpi_match_table, dev);
	if (!id)
		return -ENODEV;

	ret = snd_intel_acpi_dsp_driver_probe(dev, id->id);
	if (ret != SND_INTEL_DSP_DRIVER_ANY && ret != SND_INTEL_DSP_DRIVER_SOF) {
		dev_dbg(dev, "SOF ACPI driver not selected, aborting probe\n");
		return -ENODEV;
	}

	desc = device_get_match_data(&pdev->dev);
	if (!desc)
		return -ENODEV;

	if (desc == &sof_acpi_baytrail_desc && soc_intel_is_byt_cr(pdev))
		desc = &sof_acpi_baytrailcr_desc;

	return sof_acpi_probe(pdev, desc);
}

/* acpi_driver definition */
static struct platform_driver snd_sof_acpi_intel_byt_driver = {
	.probe = sof_baytrail_probe,
	.remove = sof_acpi_remove,
	.driver = {
		.name = "sof-audio-acpi-intel-byt",
		.pm = &sof_acpi_pm,
		.acpi_match_table = sof_baytrail_match,
	},
};
module_platform_driver(snd_sof_acpi_intel_byt_driver);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_IMPORT_NS(SND_SOC_SOF_INTEL_HIFI_EP_IPC);
MODULE_IMPORT_NS(SND_SOC_SOF_XTENSA);
MODULE_IMPORT_NS(SND_SOC_SOF_ACPI_DEV);
MODULE_IMPORT_NS(SND_SOC_SOF_INTEL_ATOM_HIFI_EP);
