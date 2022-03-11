// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2022 Intel Corporation. All rights reserved.
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/slab.h>
#include "avs.h"
#include "messages.h"
#include "registers.h"

#define AVS_FW_INIT_TIMEOUT_MS		3000

#define AVS_ROOT_DIR			"intel/avs"
#define AVS_BASEFW_FILENAME		"dsp_basefw.bin"
#define AVS_EXT_MANIFEST_MAGIC		0x31454124
#define SKL_MANIFEST_MAGIC		0x00000006
#define SKL_ADSPFW_OFFSET		0x284

/* Occasionally, engineering (release candidate) firmware is provided for testing. */
static bool debug_ignore_fw_version;
module_param_named(ignore_fw_version, debug_ignore_fw_version, bool, 0444);
MODULE_PARM_DESC(ignore_fw_version, "Verify FW version 0=yes (default), 1=no");

#define AVS_LIB_NAME_SIZE	8

struct avs_fw_manifest {
	u32 id;
	u32 len;
	char name[AVS_LIB_NAME_SIZE];
	u32 preload_page_count;
	u32 img_flags;
	u32 feature_mask;
	struct avs_fw_version version;
} __packed;

struct avs_fw_ext_manifest {
	u32 id;
	u32 len;
	u16 version_major;
	u16 version_minor;
	u32 entries;
} __packed;

static int avs_fw_ext_manifest_strip(struct firmware *fw)
{
	struct avs_fw_ext_manifest *man;

	if (fw->size < sizeof(*man))
		return -EINVAL;

	man = (struct avs_fw_ext_manifest *)fw->data;
	if (man->id == AVS_EXT_MANIFEST_MAGIC) {
		fw->data += man->len;
		fw->size -= man->len;
	}

	return 0;
}

static int avs_fw_manifest_offset(struct firmware *fw)
{
	/* Header type found in first DWORD of fw binary. */
	u32 magic = *(u32 *)fw->data;

	switch (magic) {
	case SKL_MANIFEST_MAGIC:
		return SKL_ADSPFW_OFFSET;
	default:
		return -EINVAL;
	}
}

static int avs_fw_manifest_strip_verify(struct avs_dev *adev, struct firmware *fw,
					const struct avs_fw_version *min)
{
	struct avs_fw_manifest *man;
	int offset, ret;

	ret = avs_fw_ext_manifest_strip(fw);
	if (ret)
		return ret;

	offset = avs_fw_manifest_offset(fw);
	if (offset < 0)
		return offset;

	if (fw->size < offset + sizeof(*man))
		return -EINVAL;
	if (!min)
		return 0;

	man = (struct avs_fw_manifest *)(fw->data + offset);
	if (man->version.major != min->major ||
	    man->version.minor != min->minor ||
	    man->version.hotfix != min->hotfix ||
	    man->version.build < min->build) {
		dev_warn(adev->dev, "bad FW version %d.%d.%d.%d, expected %d.%d.%d.%d or newer\n",
			 man->version.major, man->version.minor,
			 man->version.hotfix, man->version.build,
			 min->major, min->minor, min->hotfix, min->build);

		if (!debug_ignore_fw_version)
			return -EINVAL;
	}

	return 0;
}

static int avs_dsp_load_basefw(struct avs_dev *adev)
{
	const struct avs_fw_version *min_req;
	const struct avs_spec *const spec = adev->spec;
	const struct firmware *fw;
	struct firmware stripped_fw;
	char *filename;
	int ret;

	filename = kasprintf(GFP_KERNEL, "%s/%s/%s", AVS_ROOT_DIR, spec->name, AVS_BASEFW_FILENAME);
	if (!filename)
		return -ENOMEM;

	ret = avs_request_firmware(adev, &fw, filename);
	kfree(filename);
	if (ret < 0) {
		dev_err(adev->dev, "request firmware failed: %d\n", ret);
		return ret;
	}

	stripped_fw = *fw;
	min_req = &adev->spec->min_fw_version;

	ret = avs_fw_manifest_strip_verify(adev, &stripped_fw, min_req);
	if (ret < 0) {
		dev_err(adev->dev, "invalid firmware data: %d\n", ret);
		goto release_fw;
	}

	ret = avs_dsp_op(adev, load_basefw, &stripped_fw);
	if (ret < 0) {
		dev_err(adev->dev, "basefw load failed: %d\n", ret);
		goto release_fw;
	}

	ret = wait_for_completion_timeout(&adev->fw_ready,
					  msecs_to_jiffies(AVS_FW_INIT_TIMEOUT_MS));
	if (!ret) {
		dev_err(adev->dev, "firmware ready timeout\n");
		avs_dsp_core_disable(adev, AVS_MAIN_CORE_MASK);
		ret = -ETIMEDOUT;
		goto release_fw;
	}

	return 0;

release_fw:
	avs_release_last_firmware(adev);
	return ret;
}

int avs_dsp_boot_firmware(struct avs_dev *adev, bool purge)
{
	int ret, i;

	/* Full boot, clear cached data except for basefw (slot 0). */
	for (i = 1; i < adev->fw_cfg.max_libs_count; i++)
		memset(adev->lib_names[i], 0, AVS_LIB_NAME_SIZE);

	avs_hda_clock_gating_enable(adev, false);
	avs_hda_l1sen_enable(adev, false);

	ret = avs_dsp_load_basefw(adev);

	avs_hda_l1sen_enable(adev, true);
	avs_hda_clock_gating_enable(adev, true);

	if (ret < 0)
		return ret;

	/* With all code loaded, refresh module information. */
	ret = avs_module_info_init(adev, true);
	if (ret) {
		dev_err(adev->dev, "init module info failed: %d\n", ret);
		return ret;
	}

	return 0;
}

int avs_dsp_first_boot_firmware(struct avs_dev *adev)
{
	int ret, i;

	ret = avs_dsp_boot_firmware(adev, true);
	if (ret < 0) {
		dev_err(adev->dev, "firmware boot failed: %d\n", ret);
		return ret;
	}

	ret = avs_ipc_get_hw_config(adev, &adev->hw_cfg);
	if (ret) {
		dev_err(adev->dev, "get hw cfg failed: %d\n", ret);
		return AVS_IPC_RET(ret);
	}

	ret = avs_ipc_get_fw_config(adev, &adev->fw_cfg);
	if (ret) {
		dev_err(adev->dev, "get fw cfg failed: %d\n", ret);
		return AVS_IPC_RET(ret);
	}

	adev->core_refs = devm_kcalloc(adev->dev, adev->hw_cfg.dsp_cores,
				       sizeof(*adev->core_refs), GFP_KERNEL);
	adev->lib_names = devm_kcalloc(adev->dev, adev->fw_cfg.max_libs_count,
				       sizeof(*adev->lib_names), GFP_KERNEL);
	if (!adev->core_refs || !adev->lib_names)
		return -ENOMEM;

	for (i = 0; i < adev->fw_cfg.max_libs_count; i++) {
		adev->lib_names[i] = devm_kzalloc(adev->dev, AVS_LIB_NAME_SIZE, GFP_KERNEL);
		if (!adev->lib_names[i])
			return -ENOMEM;
	}

	/* basefw always occupies slot 0 */
	strcpy(&adev->lib_names[0][0], "BASEFW");

	ida_init(&adev->ppl_ida);

	return 0;
}
