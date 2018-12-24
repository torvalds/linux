/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2018 Intel Corporation. All rights reserved.
 *
 * Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
 */

#ifndef __INCLUDE_SOUND_SOF_H
#define __INCLUDE_SOUND_SOF_H

#include <linux/pci.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>

struct snd_sof_dsp_ops;

/*
 * SOF Platform data.
 */
struct snd_sof_pdata {
	u32 id;		/* PCI/ACPI ID */
	const struct firmware *fw;
	const char *drv_name;
	const char *name;
	const char *platform;

	/* parent device */
	struct device *dev;

	/* descriptor */
	const struct sof_dev_desc *desc;

	/* machine */
	struct platform_device *pdev_mach;
	const struct snd_soc_acpi_mach *machine;

	void *hw_pdata;
};

/*
 * Descriptor used for setting up SOF platform data. This is used when
 * ACPI/PCI data is missing or mapped differently.
 */
struct sof_dev_desc {
	/* list of machines using this configuration */
	struct snd_soc_acpi_mach *machines;

	/* Platform resource indexes in BAR / ACPI resources. */
	/* Must set to -1 if not used - add new items to end */
	int resindex_lpe_base;
	int resindex_pcicfg_base;
	int resindex_imr_base;
	int irqindex_host_ipc;
	int resindex_dma_base;

	/* DMA only valid when resindex_dma_base != -1*/
	int dma_engine;
	int dma_size;

	/* IPC timeouts in ms */
	int ipc_timeout;
	int boot_timeout;

	/* chip information for dsp */
	const void *chip_info;

	/* defaults for no codec mode */
	const char *nocodec_fw_filename;
	const char *nocodec_tplg_filename;

	const struct snd_sof_dsp_ops *ops;
	const struct sof_arch_ops *arch_ops;
};

int sof_nocodec_setup(struct device *dev,
		      struct snd_sof_pdata *sof_pdata,
		      struct snd_soc_acpi_mach *mach,
		      const struct sof_dev_desc *desc,
		      const struct snd_sof_dsp_ops *ops);

int sof_bes_setup(struct device *dev, const struct snd_sof_dsp_ops *ops,
		  struct snd_soc_dai_link *links, int link_num,
		  struct snd_soc_card *card);
#endif
