// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2023 Intel Corporation. All rights reserved.
//

#include <linux/firmware.h>
#include <sound/sof.h>
#include <sound/sof/ext_manifest4.h>
#include "sof-priv.h"

static int sof_test_firmware_file(struct device *dev,
				  struct sof_loadable_file_profile *profile,
				  enum sof_ipc_type *ipc_type_to_adjust)
{
	enum sof_ipc_type fw_ipc_type;
	const struct firmware *fw;
	const char *fw_filename;
	const u32 *magic;
	int ret;

	fw_filename = kasprintf(GFP_KERNEL, "%s/%s", profile->fw_path,
				profile->fw_name);
	if (!fw_filename)
		return -ENOMEM;

	ret = firmware_request_nowarn(&fw, fw_filename, dev);
	if (ret < 0) {
		dev_dbg(dev, "Failed to open firmware file: %s\n", fw_filename);
		kfree(fw_filename);
		return ret;
	}

	/* firmware file exists, check the magic number */
	magic = (const u32 *)fw->data;
	switch (*magic) {
	case SOF_EXT_MAN_MAGIC_NUMBER:
		fw_ipc_type = SOF_IPC_TYPE_3;
		break;
	case SOF_EXT_MAN4_MAGIC_NUMBER:
		fw_ipc_type = SOF_IPC_TYPE_4;
		break;
	default:
		dev_err(dev, "Invalid firmware magic: %#x\n", *magic);
		ret = -EINVAL;
		goto out;
	}

	if (ipc_type_to_adjust) {
		*ipc_type_to_adjust = fw_ipc_type;
	} else if (fw_ipc_type != profile->ipc_type) {
		dev_err(dev,
			"ipc type mismatch between %s and expected: %d vs %d\n",
			fw_filename, fw_ipc_type, profile->ipc_type);
		ret = -EINVAL;
	}
out:
	release_firmware(fw);
	kfree(fw_filename);

	return ret;
}

static int sof_test_topology_file(struct device *dev,
				  struct sof_loadable_file_profile *profile)
{
	const struct firmware *fw;
	const char *tplg_filename;
	int ret;

	if (!profile->tplg_path || !profile->tplg_name)
		return 0;

	tplg_filename = kasprintf(GFP_KERNEL, "%s/%s", profile->tplg_path,
				  profile->tplg_name);
	if (!tplg_filename)
		return -ENOMEM;

	ret = firmware_request_nowarn(&fw, tplg_filename, dev);
	if (!ret)
		release_firmware(fw);
	else
		dev_dbg(dev, "Failed to open topology file: %s\n", tplg_filename);

	kfree(tplg_filename);

	return ret;
}

static int
sof_file_profile_for_ipc_type(struct snd_sof_dev *sdev,
			      enum sof_ipc_type ipc_type,
			      const struct sof_dev_desc *desc,
			      struct sof_loadable_file_profile *base_profile,
			      struct sof_loadable_file_profile *out_profile)
{
	struct snd_sof_pdata *plat_data = sdev->pdata;
	bool fw_lib_path_allocated = false;
	struct device *dev = sdev->dev;
	bool fw_path_allocated = false;
	int ret = 0;

	/* firmware path */
	if (base_profile->fw_path) {
		out_profile->fw_path = base_profile->fw_path;
	} else if (base_profile->fw_path_postfix) {
		out_profile->fw_path = devm_kasprintf(dev, GFP_KERNEL, "%s/%s",
							desc->default_fw_path[ipc_type],
							base_profile->fw_path_postfix);
		if (!out_profile->fw_path)
			return -ENOMEM;

		fw_path_allocated = true;
	} else {
		out_profile->fw_path = desc->default_fw_path[ipc_type];
	}

	/* firmware filename */
	if (base_profile->fw_name)
		out_profile->fw_name = base_profile->fw_name;
	else
		out_profile->fw_name = desc->default_fw_filename[ipc_type];

	/*
	 * Check the custom firmware path/filename and adjust the ipc_type to
	 * match with the existing file for the remaining path configuration.
	 *
	 * For default path and firmware name do a verification before
	 * continuing further.
	 */
	if (base_profile->fw_path || base_profile->fw_name) {
		ret = sof_test_firmware_file(dev, out_profile, &ipc_type);
		if (ret)
			return ret;

		if (!(desc->ipc_supported_mask & BIT(ipc_type))) {
			dev_err(dev, "Unsupported IPC type %d needed by %s/%s\n",
				ipc_type, out_profile->fw_path,
				out_profile->fw_name);
			return -EINVAL;
		}
	}

	/* firmware library path */
	if (base_profile->fw_lib_path) {
		out_profile->fw_lib_path = base_profile->fw_lib_path;
	} else if (desc->default_lib_path[ipc_type]) {
		if (base_profile->fw_lib_path_postfix) {
			out_profile->fw_lib_path = devm_kasprintf(dev,
							GFP_KERNEL, "%s/%s",
							desc->default_lib_path[ipc_type],
							base_profile->fw_lib_path_postfix);
			if (!out_profile->fw_lib_path) {
				ret = -ENOMEM;
				goto out;
			}

			fw_lib_path_allocated = true;
		} else {
			out_profile->fw_lib_path = desc->default_lib_path[ipc_type];
		}
	}

	if (base_profile->fw_path_postfix)
		out_profile->fw_path_postfix = base_profile->fw_path_postfix;

	if (base_profile->fw_lib_path_postfix)
		out_profile->fw_lib_path_postfix = base_profile->fw_lib_path_postfix;

	/* topology path */
	if (base_profile->tplg_path)
		out_profile->tplg_path = base_profile->tplg_path;
	else
		out_profile->tplg_path = desc->default_tplg_path[ipc_type];

	/* topology name */
	out_profile->tplg_name = plat_data->tplg_filename;

	out_profile->ipc_type = ipc_type;

	/* Test only default firmware file */
	if (!base_profile->fw_path && !base_profile->fw_name)
		ret = sof_test_firmware_file(dev, out_profile, NULL);

	if (!ret)
		ret = sof_test_topology_file(dev, out_profile);

out:
	if (ret) {
		/* Free up path strings created with devm_kasprintf */
		if (fw_path_allocated)
			devm_kfree(dev, out_profile->fw_path);
		if (fw_lib_path_allocated)
			devm_kfree(dev, out_profile->fw_lib_path);

		memset(out_profile, 0, sizeof(*out_profile));
	}

	return ret;
}

static void
sof_print_missing_firmware_info(struct snd_sof_dev *sdev,
				enum sof_ipc_type ipc_type,
				struct sof_loadable_file_profile *base_profile)
{
	struct snd_sof_pdata *plat_data = sdev->pdata;
	const struct sof_dev_desc *desc = plat_data->desc;
	struct device *dev = sdev->dev;
	int ipc_type_count, i;
	char *marker;

	dev_err(dev, "SOF firmware and/or topology file not found.\n");
	dev_info(dev, "Supported default profiles\n");

	if (IS_ENABLED(CONFIG_SND_SOC_SOF_ALLOW_FALLBACK_TO_NEWER_IPC_VERSION))
		ipc_type_count = SOF_IPC_TYPE_COUNT - 1;
	else
		ipc_type_count = base_profile->ipc_type;

	for (i = 0; i <= ipc_type_count; i++) {
		if (!(desc->ipc_supported_mask & BIT(i)))
			continue;

		if (i == ipc_type)
			marker = "Requested";
		else
			marker = "Fallback";

		dev_info(dev, "- ipc type %d (%s):\n", i, marker);
		if (base_profile->fw_path_postfix)
			dev_info(dev, " Firmware file: %s/%s/%s\n",
				 desc->default_fw_path[i],
				 base_profile->fw_path_postfix,
				 desc->default_fw_filename[i]);
		else
			dev_info(dev, " Firmware file: %s/%s\n",
				 desc->default_fw_path[i],
				 desc->default_fw_filename[i]);

		dev_info(dev, " Topology file: %s/%s\n",
			 desc->default_tplg_path[i],
			 plat_data->tplg_filename);
	}

	if (base_profile->fw_path || base_profile->fw_name ||
	    base_profile->tplg_path || base_profile->tplg_name)
		dev_info(dev, "Verify the path/name override module parameters.\n");

	dev_info(dev, "Check if you have 'sof-firmware' package installed.\n");
	dev_info(dev, "Optionally it can be manually downloaded from:\n");
	dev_info(dev, "   https://github.com/thesofproject/sof-bin/\n");
}

static void sof_print_profile_info(struct snd_sof_dev *sdev,
				   enum sof_ipc_type ipc_type,
				   struct sof_loadable_file_profile *profile)
{
	struct device *dev = sdev->dev;

	if (ipc_type != profile->ipc_type)
		dev_info(dev,
			 "Using fallback IPC type %d (requested type was %d)\n",
			 profile->ipc_type, ipc_type);

	dev_info(dev, "Firmware paths/files for ipc type %d:\n", profile->ipc_type);

	dev_info(dev, " Firmware file:     %s/%s\n", profile->fw_path, profile->fw_name);
	if (profile->fw_lib_path)
		dev_info(dev, " Firmware lib path: %s\n", profile->fw_lib_path);
	dev_info(dev, " Topology file:     %s/%s\n", profile->tplg_path, profile->tplg_name);
}

int sof_create_ipc_file_profile(struct snd_sof_dev *sdev,
				struct sof_loadable_file_profile *base_profile,
				struct sof_loadable_file_profile *out_profile)
{
	const struct sof_dev_desc *desc = sdev->pdata->desc;
	int ipc_fallback_start, ret, i;

	memset(out_profile, 0, sizeof(*out_profile));

	ret = sof_file_profile_for_ipc_type(sdev, base_profile->ipc_type, desc,
					    base_profile, out_profile);
	if (!ret)
		goto out;

	/*
	 * No firmware file was found for the requested IPC type, as fallback
	 * if SND_SOC_SOF_ALLOW_FALLBACK_TO_NEWER_IPC_VERSION is selected, check
	 * all IPC versions in a backwards direction (from newer to older)
	 * if SND_SOC_SOF_ALLOW_FALLBACK_TO_NEWER_IPC_VERSION is not selected,
	 * check only older IPC versions than the selected/default version
	 */
	if (IS_ENABLED(CONFIG_SND_SOC_SOF_ALLOW_FALLBACK_TO_NEWER_IPC_VERSION))
		ipc_fallback_start = SOF_IPC_TYPE_COUNT - 1;
	else
		ipc_fallback_start = (int)base_profile->ipc_type - 1;

	for (i = ipc_fallback_start; i >= 0 ; i--) {
		if (i == base_profile->ipc_type ||
		    !(desc->ipc_supported_mask & BIT(i)))
			continue;

		ret = sof_file_profile_for_ipc_type(sdev, i, desc, base_profile,
						    out_profile);
		if (!ret)
			break;
	}

out:
	if (ret)
		sof_print_missing_firmware_info(sdev, base_profile->ipc_type,
						base_profile);
	else
		sof_print_profile_info(sdev, base_profile->ipc_type, out_profile);

	return ret;
}
EXPORT_SYMBOL(sof_create_ipc_file_profile);
