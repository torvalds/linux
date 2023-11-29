// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2023 Intel Corporation. All rights reserved.
//

#include <sound/sof.h>
#include "sof-priv.h"

static int
sof_file_profile_for_ipc_type(struct device *dev,
			      const struct sof_dev_desc *desc,
			      struct sof_loadable_file_profile *base_profile,
			      struct sof_loadable_file_profile *out_profile)
{
	enum sof_ipc_type ipc_type = base_profile->ipc_type;
	bool fw_lib_path_allocated = false;
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
	if (base_profile->tplg_name)
		out_profile->tplg_name = base_profile->tplg_name;

	out_profile->ipc_type = ipc_type;

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

static void sof_print_profile_info(struct device *dev,
				   struct sof_loadable_file_profile *profile)
{
	dev_info(dev, "Firmware paths/files for ipc type %d:\n", profile->ipc_type);

	dev_info(dev, " Firmware file:     %s/%s\n", profile->fw_path, profile->fw_name);
	if (profile->fw_lib_path)
		dev_info(dev, " Firmware lib path: %s\n", profile->fw_lib_path);
	if (profile->tplg_name)
		dev_info(dev, " Topology file:     %s/%s\n", profile->tplg_path,
			profile->tplg_name);
	else
		dev_info(dev, " Topology path:     %s\n", profile->tplg_path);
}

int sof_create_ipc_file_profile(struct snd_sof_dev *sdev,
				struct sof_loadable_file_profile *base_profile,
				struct sof_loadable_file_profile *out_profile)
{
	const struct sof_dev_desc *desc = sdev->pdata->desc;
	struct device *dev = sdev->dev;
	int ret;

	memset(out_profile, 0, sizeof(*out_profile));

	ret = sof_file_profile_for_ipc_type(dev, desc, base_profile, out_profile);
	if (ret)
		return ret;

	sof_print_profile_info(dev, out_profile);

	return 0;
}
EXPORT_SYMBOL(sof_create_ipc_file_profile);
