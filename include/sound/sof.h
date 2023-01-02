/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
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
struct snd_sof_dev;

/**
 * enum sof_fw_state - DSP firmware state definitions
 * @SOF_FW_BOOT_NOT_STARTED:	firmware boot is not yet started
 * @SOF_FW_BOOT_PREPARE:	preparing for boot (firmware loading for exaqmple)
 * @SOF_FW_BOOT_IN_PROGRESS:	firmware boot is in progress
 * @SOF_FW_BOOT_FAILED:		firmware boot failed
 * @SOF_FW_BOOT_READY_FAILED:	firmware booted but fw_ready op failed
 * @SOF_FW_BOOT_READY_OK:	firmware booted and fw_ready op passed
 * @SOF_FW_BOOT_COMPLETE:	firmware is booted up and functional
 * @SOF_FW_CRASHED:		firmware crashed after successful boot
 */
enum sof_fw_state {
	SOF_FW_BOOT_NOT_STARTED = 0,
	SOF_FW_BOOT_PREPARE,
	SOF_FW_BOOT_IN_PROGRESS,
	SOF_FW_BOOT_FAILED,
	SOF_FW_BOOT_READY_FAILED,
	SOF_FW_BOOT_READY_OK,
	SOF_FW_BOOT_COMPLETE,
	SOF_FW_CRASHED,
};

/* DSP power states */
enum sof_dsp_power_states {
	SOF_DSP_PM_D0,
	SOF_DSP_PM_D1,
	SOF_DSP_PM_D2,
	SOF_DSP_PM_D3,
};

/* Definitions for multiple IPCs */
enum sof_ipc_type {
	SOF_IPC,
	SOF_INTEL_IPC4,
	SOF_IPC_TYPE_COUNT
};

/*
 * SOF Platform data.
 */
struct snd_sof_pdata {
	const char *name;
	const char *platform;

	struct device *dev;

	/*
	 * notification callback used if the hardware initialization
	 * can take time or is handled in a workqueue. This callback
	 * can be used by the caller to e.g. enable runtime_pm
	 * or limit functionality until all low-level inits are
	 * complete.
	 */
	void (*sof_probe_complete)(struct device *dev);

	/* descriptor */
	const struct sof_dev_desc *desc;

	/* firmware and topology filenames */
	const char *fw_filename_prefix;
	const char *fw_filename;
	const char *tplg_filename_prefix;
	const char *tplg_filename;

	/* loadable external libraries available under this directory */
	const char *fw_lib_prefix;

	/* machine */
	struct platform_device *pdev_mach;
	const struct snd_soc_acpi_mach *machine;
	const struct snd_sof_of_mach *of_machine;

	void *hw_pdata;

	enum sof_ipc_type ipc_type;
};

/*
 * Descriptor used for setting up SOF platform data. This is used when
 * ACPI/PCI data is missing or mapped differently.
 */
struct sof_dev_desc {
	/* list of machines using this configuration */
	struct snd_soc_acpi_mach *machines;
	struct snd_sof_of_mach *of_machines;

	/* alternate list of machines using this configuration */
	struct snd_soc_acpi_mach *alt_machines;

	bool use_acpi_target_states;

	/* Platform resource indexes in BAR / ACPI resources. */
	/* Must set to -1 if not used - add new items to end */
	int resindex_lpe_base;
	int resindex_pcicfg_base;
	int resindex_imr_base;
	int irqindex_host_ipc;

	/* IPC timeouts in ms */
	int ipc_timeout;
	int boot_timeout;

	/* chip information for dsp */
	const void *chip_info;

	/* defaults for no codec mode */
	const char *nocodec_tplg_filename;

	/* information on supported IPCs */
	unsigned int ipc_supported_mask;
	enum sof_ipc_type ipc_default;

	/* defaults paths for firmware, library and topology files */
	const char *default_fw_path[SOF_IPC_TYPE_COUNT];
	const char *default_lib_path[SOF_IPC_TYPE_COUNT];
	const char *default_tplg_path[SOF_IPC_TYPE_COUNT];

	/* default firmware name */
	const char *default_fw_filename[SOF_IPC_TYPE_COUNT];

	struct snd_sof_dsp_ops *ops;
	int (*ops_init)(struct snd_sof_dev *sdev);
	void (*ops_free)(struct snd_sof_dev *sdev);
};

int sof_dai_get_mclk(struct snd_soc_pcm_runtime *rtd);
int sof_dai_get_bclk(struct snd_soc_pcm_runtime *rtd);

#endif
