// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2022 Intel Corporation. All rights reserved.

#include <linux/firmware.h>
#include <sound/sof/ext_manifest4.h>
#include <sound/sof/ipc4/header.h>
#include "ipc4-priv.h"
#include "sof-audio.h"
#include "sof-priv.h"
#include "ops.h"

static size_t sof_ipc4_fw_parse_ext_man(struct snd_sof_dev *sdev)
{
	struct sof_ipc4_fw_data *ipc4_data = sdev->private;
	struct snd_sof_pdata *plat_data = sdev->pdata;
	struct sof_man4_fw_binary_header *fw_header;
	const struct firmware *fw = plat_data->fw;
	struct sof_ext_manifest4_hdr *ext_man_hdr;
	struct sof_man4_module_config *fm_config;
	struct sof_ipc4_fw_module *fw_module;
	struct sof_man4_module *fm_entry;
	ssize_t remaining;
	u32 fw_hdr_offset;
	int i;

	if (!ipc4_data) {
		dev_err(sdev->dev, "%s: ipc4_data is not available\n", __func__);
		return -EINVAL;
	}

	remaining = fw->size;
	if (remaining <= sizeof(*ext_man_hdr)) {
		dev_err(sdev->dev, "Firmware size is too small: %zu\n", remaining);
		return -EINVAL;
	}

	ext_man_hdr = (struct sof_ext_manifest4_hdr *)fw->data;

	fw_hdr_offset = ipc4_data->manifest_fw_hdr_offset;
	if (!fw_hdr_offset)
		return -EINVAL;

	if (remaining <= ext_man_hdr->len + fw_hdr_offset + sizeof(*fw_header)) {
		dev_err(sdev->dev, "Invalid firmware size %zu, should be at least %zu\n",
			remaining, ext_man_hdr->len + fw_hdr_offset + sizeof(*fw_header));
		return -EINVAL;
	}

	fw_header = (struct sof_man4_fw_binary_header *)
				(fw->data + ext_man_hdr->len + fw_hdr_offset);
	remaining -= (ext_man_hdr->len + fw_hdr_offset);

	if (remaining <= fw_header->len) {
		dev_err(sdev->dev, "Invalid fw_header->len %u\n", fw_header->len);
		return -EINVAL;
	}

	dev_info(sdev->dev, "Loaded firmware version: %u.%u.%u.%u\n",
		 fw_header->major_version, fw_header->minor_version,
		 fw_header->hotfix_version, fw_header->build_version);
	dev_dbg(sdev->dev, "Firmware name: %s, header length: %u, module count: %u\n",
		fw_header->name, fw_header->len, fw_header->num_module_entries);

	ipc4_data->fw_modules = devm_kmalloc_array(sdev->dev,
						   fw_header->num_module_entries,
						   sizeof(*fw_module), GFP_KERNEL);
	if (!ipc4_data->fw_modules)
		return -ENOMEM;

	ipc4_data->num_fw_modules = fw_header->num_module_entries;
	fw_module = ipc4_data->fw_modules;

	fm_entry = (struct sof_man4_module *)((u8 *)fw_header + fw_header->len);
	remaining -= fw_header->len;

	if (remaining < fw_header->num_module_entries * sizeof(*fm_entry)) {
		dev_err(sdev->dev, "Invalid num_module_entries %u\n",
			fw_header->num_module_entries);
		return -EINVAL;
	}

	fm_config = (struct sof_man4_module_config *)
				(fm_entry + fw_header->num_module_entries);
	remaining -= (fw_header->num_module_entries * sizeof(*fm_entry));
	for (i = 0; i < fw_header->num_module_entries; i++) {
		memcpy(&fw_module->man4_module_entry, fm_entry, sizeof(*fm_entry));

		if (fm_entry->cfg_count) {
			if (remaining < (fm_entry->cfg_offset + fm_entry->cfg_count) *
			    sizeof(*fm_config)) {
				dev_err(sdev->dev, "Invalid module cfg_offset %u\n",
					fm_entry->cfg_offset);
				return -EINVAL;
			}

			/* a module's config is always the same size */
			fw_module->bss_size = fm_config[fm_entry->cfg_offset].is_bytes;

			dev_dbg(sdev->dev,
				"module %s: UUID %pUL cfg_count: %u, bss_size: %#x\n",
				fm_entry->name, &fm_entry->uuid, fm_entry->cfg_count,
				fw_module->bss_size);
		} else {
			fw_module->bss_size = 0;

			dev_dbg(sdev->dev, "module %s: UUID %pUL\n", fm_entry->name,
				&fm_entry->uuid);
		}

		fw_module->man4_module_entry.id = i;
		ida_init(&fw_module->m_ida);
		fw_module->private = NULL;

		fw_module++;
		fm_entry++;
	}

	return ext_man_hdr->len;
}

static int sof_ipc4_validate_firmware(struct snd_sof_dev *sdev)
{
	struct sof_ipc4_fw_data *ipc4_data = sdev->private;
	u32 fw_hdr_offset = ipc4_data->manifest_fw_hdr_offset;
	struct snd_sof_pdata *plat_data = sdev->pdata;
	struct sof_man4_fw_binary_header *fw_header;
	const struct firmware *fw = plat_data->fw;
	struct sof_ext_manifest4_hdr *ext_man_hdr;

	ext_man_hdr = (struct sof_ext_manifest4_hdr *)fw->data;
	fw_header = (struct sof_man4_fw_binary_header *)
				(fw->data + ext_man_hdr->len + fw_hdr_offset);

	/* TODO: Add firmware verification code here */

	dev_dbg(sdev->dev, "Validated firmware version: %u.%u.%u.%u\n",
		fw_header->major_version, fw_header->minor_version,
		fw_header->hotfix_version, fw_header->build_version);

	return 0;
}

static int sof_ipc4_query_fw_configuration(struct snd_sof_dev *sdev)
{
	const struct sof_ipc_ops *iops = sdev->ipc->ops;
	struct sof_ipc4_fw_version *fw_ver;
	struct sof_ipc4_tuple *tuple;
	struct sof_ipc4_msg msg;
	size_t offset = 0;
	int ret;

	/* Get the firmware configuration */
	msg.primary = SOF_IPC4_MSG_TARGET(SOF_IPC4_MODULE_MSG);
	msg.primary |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	msg.primary |= SOF_IPC4_MOD_ID(SOF_IPC4_MOD_INIT_BASEFW_MOD_ID);
	msg.primary |= SOF_IPC4_MOD_INSTANCE(SOF_IPC4_MOD_INIT_BASEFW_INSTANCE_ID);
	msg.extension = SOF_IPC4_MOD_EXT_MSG_PARAM_ID(SOF_IPC4_FW_PARAM_FW_CONFIG);

	msg.data_size = sdev->ipc->max_payload_size;
	msg.data_ptr = kzalloc(msg.data_size, GFP_KERNEL);
	if (!msg.data_ptr)
		return -ENOMEM;

	ret = iops->set_get_data(sdev, &msg, msg.data_size, false);
	if (ret)
		goto out;

	while (offset < msg.data_size) {
		tuple = (struct sof_ipc4_tuple *)((u8 *)msg.data_ptr + offset);

		switch (tuple->type) {
		case SOF_IPC4_FW_CFG_FW_VERSION:
			fw_ver = (struct sof_ipc4_fw_version *)tuple->value;

			dev_info(sdev->dev,
				 "Booted firmware version: %u.%u.%u.%u\n",
				 fw_ver->major, fw_ver->minor, fw_ver->hotfix,
				 fw_ver->build);
			break;
		case SOF_IPC4_FW_CFG_DL_MAILBOX_BYTES:
			dev_vdbg(sdev->dev, "DL mailbox size: %u\n", *tuple->value);
			break;
		case SOF_IPC4_FW_CFG_UL_MAILBOX_BYTES:
			dev_vdbg(sdev->dev, "UL mailbox size: %u\n", *tuple->value);
			break;
		case SOF_IPC4_FW_CFG_TRACE_LOG_BYTES:
			dev_vdbg(sdev->dev, "Trace log size: %u\n", *tuple->value);
			break;
		default:
			break;
		}

		offset += sizeof(*tuple) + tuple->size;
	}

out:
	kfree(msg.data_ptr);

	return ret;
}

const struct sof_ipc_fw_loader_ops ipc4_loader_ops = {
	.validate = sof_ipc4_validate_firmware,
	.parse_ext_manifest = sof_ipc4_fw_parse_ext_man,
	.query_fw_configuration = sof_ipc4_query_fw_configuration,
};
