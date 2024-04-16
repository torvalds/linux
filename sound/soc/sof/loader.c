// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//
// Generic firmware loader.
//

#include <linux/firmware.h>
#include "sof-priv.h"
#include "ops.h"

int snd_sof_load_firmware_raw(struct snd_sof_dev *sdev)
{
	struct snd_sof_pdata *plat_data = sdev->pdata;
	const char *fw_filename;
	ssize_t ext_man_size;
	int ret;

	/* Don't request firmware again if firmware is already requested */
	if (plat_data->fw)
		return 0;

	fw_filename = kasprintf(GFP_KERNEL, "%s/%s",
				plat_data->fw_filename_prefix,
				plat_data->fw_filename);
	if (!fw_filename)
		return -ENOMEM;

	ret = request_firmware(&plat_data->fw, fw_filename, sdev->dev);

	if (ret < 0) {
		dev_err(sdev->dev,
			"error: sof firmware file is missing, you might need to\n");
		dev_err(sdev->dev,
			"       download it from https://github.com/thesofproject/sof-bin/\n");
		goto err;
	} else {
		dev_dbg(sdev->dev, "request_firmware %s successful\n",
			fw_filename);
	}

	/* check for extended manifest */
	ext_man_size = sdev->ipc->ops->fw_loader->parse_ext_manifest(sdev);
	if (ext_man_size > 0) {
		/* when no error occurred, drop extended manifest */
		plat_data->fw_offset = ext_man_size;
	} else if (!ext_man_size) {
		/* No extended manifest, so nothing to skip during FW load */
		dev_dbg(sdev->dev, "firmware doesn't contain extended manifest\n");
	} else {
		ret = ext_man_size;
		dev_err(sdev->dev, "error: firmware %s contains unsupported or invalid extended manifest: %d\n",
			fw_filename, ret);
	}

err:
	kfree(fw_filename);

	return ret;
}
EXPORT_SYMBOL(snd_sof_load_firmware_raw);

int snd_sof_load_firmware_memcpy(struct snd_sof_dev *sdev)
{
	struct snd_sof_pdata *plat_data = sdev->pdata;
	int ret;

	ret = snd_sof_load_firmware_raw(sdev);
	if (ret < 0)
		return ret;

	/* make sure the FW header and file is valid */
	ret = sdev->ipc->ops->fw_loader->validate(sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: invalid FW header\n");
		goto error;
	}

	/* prepare the DSP for FW loading */
	ret = snd_sof_dsp_reset(sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to reset DSP\n");
		goto error;
	}

	/* parse and load firmware modules to DSP */
	if (sdev->ipc->ops->fw_loader->load_fw_to_dsp) {
		ret = sdev->ipc->ops->fw_loader->load_fw_to_dsp(sdev);
		if (ret < 0) {
			dev_err(sdev->dev, "Firmware loading failed\n");
			goto error;
		}
	}

	return 0;

error:
	release_firmware(plat_data->fw);
	plat_data->fw = NULL;
	return ret;

}
EXPORT_SYMBOL(snd_sof_load_firmware_memcpy);

int snd_sof_run_firmware(struct snd_sof_dev *sdev)
{
	int ret;

	init_waitqueue_head(&sdev->boot_wait);

	/* (re-)enable dsp dump */
	sdev->dbg_dump_printed = false;
	sdev->ipc_dump_printed = false;

	/* create read-only fw_version debugfs to store boot version info */
	if (sdev->first_boot) {
		ret = snd_sof_debugfs_buf_item(sdev, &sdev->fw_version,
					       sizeof(sdev->fw_version),
					       "fw_version", 0444);
		/* errors are only due to memory allocation, not debugfs */
		if (ret < 0) {
			dev_err(sdev->dev, "error: snd_sof_debugfs_buf_item failed\n");
			return ret;
		}
	}

	/* perform pre fw run operations */
	ret = snd_sof_dsp_pre_fw_run(sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed pre fw run op\n");
		return ret;
	}

	dev_dbg(sdev->dev, "booting DSP firmware\n");

	/* boot the firmware on the DSP */
	ret = snd_sof_dsp_run(sdev);
	if (ret < 0) {
		snd_sof_dsp_dbg_dump(sdev, "Failed to start DSP",
				     SOF_DBG_DUMP_MBOX | SOF_DBG_DUMP_PCI);
		return ret;
	}

	/*
	 * now wait for the DSP to boot. There are 3 possible outcomes:
	 * 1. Boot wait times out indicating FW boot failure.
	 * 2. FW boots successfully and fw_ready op succeeds.
	 * 3. FW boots but fw_ready op fails.
	 */
	ret = wait_event_timeout(sdev->boot_wait,
				 sdev->fw_state > SOF_FW_BOOT_IN_PROGRESS,
				 msecs_to_jiffies(sdev->boot_timeout));
	if (ret == 0) {
		snd_sof_dsp_dbg_dump(sdev, "Firmware boot failure due to timeout",
				     SOF_DBG_DUMP_REGS | SOF_DBG_DUMP_MBOX |
				     SOF_DBG_DUMP_TEXT | SOF_DBG_DUMP_PCI);
		return -EIO;
	}

	if (sdev->fw_state == SOF_FW_BOOT_READY_FAILED)
		return -EIO; /* FW boots but fw_ready op failed */

	/* perform post fw run operations */
	ret = snd_sof_dsp_post_fw_run(sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed post fw run op\n");
		return ret;
	}

	dev_dbg(sdev->dev, "firmware boot complete\n");
	sof_set_fw_state(sdev, SOF_FW_BOOT_COMPLETE);

	if (sdev->first_boot && sdev->ipc->ops->fw_loader->query_fw_configuration)
		return sdev->ipc->ops->fw_loader->query_fw_configuration(sdev);

	return 0;
}
EXPORT_SYMBOL(snd_sof_run_firmware);

void snd_sof_fw_unload(struct snd_sof_dev *sdev)
{
	/* TODO: support module unloading at runtime */
	release_firmware(sdev->pdata->fw);
	sdev->pdata->fw = NULL;
}
EXPORT_SYMBOL(snd_sof_fw_unload);
