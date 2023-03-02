// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2022 Intel Corporation. All rights reserved.

#include <linux/firmware.h>
#include "sof-priv.h"
#include "sof-audio.h"
#include "ipc3-priv.h"
#include "ops.h"

static int ipc3_fw_ext_man_get_version(struct snd_sof_dev *sdev,
				       const struct sof_ext_man_elem_header *hdr)
{
	const struct sof_ext_man_fw_version *v =
		container_of(hdr, struct sof_ext_man_fw_version, hdr);

	memcpy(&sdev->fw_ready.version, &v->version, sizeof(v->version));
	sdev->fw_ready.flags = v->flags;

	/* log ABI versions and check FW compatibility */
	return sof_ipc3_validate_fw_version(sdev);
}

static int ipc3_fw_ext_man_get_windows(struct snd_sof_dev *sdev,
				       const struct sof_ext_man_elem_header *hdr)
{
	const struct sof_ext_man_window *w;

	w = container_of(hdr, struct sof_ext_man_window, hdr);

	return sof_ipc3_get_ext_windows(sdev, &w->ipc_window.ext_hdr);
}

static int ipc3_fw_ext_man_get_cc_info(struct snd_sof_dev *sdev,
				       const struct sof_ext_man_elem_header *hdr)
{
	const struct sof_ext_man_cc_version *cc;

	cc = container_of(hdr, struct sof_ext_man_cc_version, hdr);

	return sof_ipc3_get_cc_info(sdev, &cc->cc_version.ext_hdr);
}

static int ipc3_fw_ext_man_get_dbg_abi_info(struct snd_sof_dev *sdev,
					    const struct sof_ext_man_elem_header *hdr)
{
	const struct ext_man_dbg_abi *dbg_abi =
		container_of(hdr, struct ext_man_dbg_abi, hdr);

	if (sdev->first_boot)
		dev_dbg(sdev->dev,
			"Firmware: DBG_ABI %d:%d:%d\n",
			SOF_ABI_VERSION_MAJOR(dbg_abi->dbg_abi.abi_dbg_version),
			SOF_ABI_VERSION_MINOR(dbg_abi->dbg_abi.abi_dbg_version),
			SOF_ABI_VERSION_PATCH(dbg_abi->dbg_abi.abi_dbg_version));

	return 0;
}

static int ipc3_fw_ext_man_get_config_data(struct snd_sof_dev *sdev,
					   const struct sof_ext_man_elem_header *hdr)
{
	const struct sof_ext_man_config_data *config =
		container_of(hdr, struct sof_ext_man_config_data, hdr);
	const struct sof_config_elem *elem;
	int elems_counter;
	int elems_size;
	int ret = 0;
	int i;

	/* calculate elements counter */
	elems_size = config->hdr.size - sizeof(struct sof_ext_man_elem_header);
	elems_counter = elems_size / sizeof(struct sof_config_elem);

	dev_dbg(sdev->dev, "manifest can hold up to %d config elements\n", elems_counter);

	for (i = 0; i < elems_counter; ++i) {
		elem = &config->elems[i];
		dev_dbg(sdev->dev, "get index %d token %d val %d\n",
			i, elem->token, elem->value);
		switch (elem->token) {
		case SOF_EXT_MAN_CONFIG_EMPTY:
			/* unused memory space is zero filled - mapped to EMPTY elements */
			break;
		case SOF_EXT_MAN_CONFIG_IPC_MSG_SIZE:
			/* TODO: use ipc msg size from config data */
			break;
		case SOF_EXT_MAN_CONFIG_MEMORY_USAGE_SCAN:
			if (sdev->first_boot && elem->value)
				ret = snd_sof_dbg_memory_info_init(sdev);
			break;
		default:
			dev_info(sdev->dev,
				 "Unknown firmware configuration token %d value %d",
				 elem->token, elem->value);
			break;
		}
		if (ret < 0) {
			dev_err(sdev->dev,
				"%s: processing failed for token %d value %#x, %d\n",
				__func__, elem->token, elem->value, ret);
			return ret;
		}
	}

	return 0;
}

static ssize_t ipc3_fw_ext_man_size(struct snd_sof_dev *sdev, const struct firmware *fw)
{
	const struct sof_ext_man_header *head;

	head = (struct sof_ext_man_header *)fw->data;

	/*
	 * assert fw size is big enough to contain extended manifest header,
	 * it prevents from reading unallocated memory from `head` in following
	 * step.
	 */
	if (fw->size < sizeof(*head))
		return -EINVAL;

	/*
	 * When fw points to extended manifest,
	 * then first u32 must be equal SOF_EXT_MAN_MAGIC_NUMBER.
	 */
	if (head->magic == SOF_EXT_MAN_MAGIC_NUMBER)
		return head->full_size;

	/* otherwise given fw don't have an extended manifest */
	dev_dbg(sdev->dev, "Unexpected extended manifest magic number: %#x\n",
		head->magic);
	return 0;
}

static size_t sof_ipc3_fw_parse_ext_man(struct snd_sof_dev *sdev)
{
	const struct firmware *fw = sdev->basefw.fw;
	const struct sof_ext_man_elem_header *elem_hdr;
	const struct sof_ext_man_header *head;
	ssize_t ext_man_size;
	ssize_t remaining;
	uintptr_t iptr;
	int ret = 0;

	head = (struct sof_ext_man_header *)fw->data;
	remaining = head->full_size - head->header_size;
	ext_man_size = ipc3_fw_ext_man_size(sdev, fw);

	/* Assert firmware starts with extended manifest */
	if (ext_man_size <= 0)
		return ext_man_size;

	/* incompatible version */
	if (SOF_EXT_MAN_VERSION_INCOMPATIBLE(SOF_EXT_MAN_VERSION,
					     head->header_version)) {
		dev_err(sdev->dev,
			"extended manifest version %#x differ from used %#x\n",
			head->header_version, SOF_EXT_MAN_VERSION);
		return -EINVAL;
	}

	/* get first extended manifest element header */
	iptr = (uintptr_t)fw->data + head->header_size;

	while (remaining > sizeof(*elem_hdr)) {
		elem_hdr = (struct sof_ext_man_elem_header *)iptr;

		dev_dbg(sdev->dev, "found sof_ext_man header type %d size %#x\n",
			elem_hdr->type, elem_hdr->size);

		if (elem_hdr->size < sizeof(*elem_hdr) ||
		    elem_hdr->size > remaining) {
			dev_err(sdev->dev,
				"invalid sof_ext_man header size, type %d size %#x\n",
				elem_hdr->type, elem_hdr->size);
			return -EINVAL;
		}

		/* process structure data */
		switch (elem_hdr->type) {
		case SOF_EXT_MAN_ELEM_FW_VERSION:
			ret = ipc3_fw_ext_man_get_version(sdev, elem_hdr);
			break;
		case SOF_EXT_MAN_ELEM_WINDOW:
			ret = ipc3_fw_ext_man_get_windows(sdev, elem_hdr);
			break;
		case SOF_EXT_MAN_ELEM_CC_VERSION:
			ret = ipc3_fw_ext_man_get_cc_info(sdev, elem_hdr);
			break;
		case SOF_EXT_MAN_ELEM_DBG_ABI:
			ret = ipc3_fw_ext_man_get_dbg_abi_info(sdev, elem_hdr);
			break;
		case SOF_EXT_MAN_ELEM_CONFIG_DATA:
			ret = ipc3_fw_ext_man_get_config_data(sdev, elem_hdr);
			break;
		case SOF_EXT_MAN_ELEM_PLATFORM_CONFIG_DATA:
			ret = snd_sof_dsp_parse_platform_ext_manifest(sdev, elem_hdr);
			break;
		default:
			dev_info(sdev->dev,
				 "unknown sof_ext_man header type %d size %#x\n",
				 elem_hdr->type, elem_hdr->size);
			break;
		}

		if (ret < 0) {
			dev_err(sdev->dev,
				"failed to parse sof_ext_man header type %d size %#x\n",
				elem_hdr->type, elem_hdr->size);
			return ret;
		}

		remaining -= elem_hdr->size;
		iptr += elem_hdr->size;
	}

	if (remaining) {
		dev_err(sdev->dev, "error: sof_ext_man header is inconsistent\n");
		return -EINVAL;
	}

	return ext_man_size;
}

/* generic module parser for mmaped DSPs */
static int sof_ipc3_parse_module_memcpy(struct snd_sof_dev *sdev,
					struct snd_sof_mod_hdr *module)
{
	struct snd_sof_blk_hdr *block;
	int count, ret;
	u32 offset;
	size_t remaining;

	dev_dbg(sdev->dev, "new module size %#x blocks %#x type %#x\n",
		module->size, module->num_blocks, module->type);

	block = (struct snd_sof_blk_hdr *)((u8 *)module + sizeof(*module));

	/* module->size doesn't include header size */
	remaining = module->size;
	for (count = 0; count < module->num_blocks; count++) {
		/* check for wrap */
		if (remaining < sizeof(*block)) {
			dev_err(sdev->dev, "not enough data remaining\n");
			return -EINVAL;
		}

		/* minus header size of block */
		remaining -= sizeof(*block);

		if (block->size == 0) {
			dev_warn(sdev->dev,
				 "warning: block %d size zero\n", count);
			dev_warn(sdev->dev, " type %#x offset %#x\n",
				 block->type, block->offset);
			continue;
		}

		switch (block->type) {
		case SOF_FW_BLK_TYPE_RSRVD0:
		case SOF_FW_BLK_TYPE_ROM...SOF_FW_BLK_TYPE_RSRVD14:
			continue;	/* not handled atm */
		case SOF_FW_BLK_TYPE_IRAM:
		case SOF_FW_BLK_TYPE_DRAM:
		case SOF_FW_BLK_TYPE_SRAM:
			offset = block->offset;
			break;
		default:
			dev_err(sdev->dev, "%s: bad type %#x for block %#x\n",
				__func__, block->type, count);
			return -EINVAL;
		}

		dev_dbg(sdev->dev, "block %d type %#x size %#x ==>  offset %#x\n",
			count, block->type, block->size, offset);

		/* checking block->size to avoid unaligned access */
		if (block->size % sizeof(u32)) {
			dev_err(sdev->dev, "%s: invalid block size %#x\n",
				__func__, block->size);
			return -EINVAL;
		}
		ret = snd_sof_dsp_block_write(sdev, block->type, offset,
					      block + 1, block->size);
		if (ret < 0) {
			dev_err(sdev->dev, "%s: write to block type %#x failed\n",
				__func__, block->type);
			return ret;
		}

		if (remaining < block->size) {
			dev_err(sdev->dev, "%s: not enough data remaining\n", __func__);
			return -EINVAL;
		}

		/* minus body size of block */
		remaining -= block->size;
		/* next block */
		block = (struct snd_sof_blk_hdr *)((u8 *)block + sizeof(*block)
			+ block->size);
	}

	return 0;
}

static int sof_ipc3_load_fw_to_dsp(struct snd_sof_dev *sdev)
{
	u32 payload_offset = sdev->basefw.payload_offset;
	const struct firmware *fw = sdev->basefw.fw;
	struct snd_sof_fw_header *header;
	struct snd_sof_mod_hdr *module;
	int (*load_module)(struct snd_sof_dev *sof_dev, struct snd_sof_mod_hdr *hdr);
	size_t remaining;
	int ret, count;

	if (!fw)
		return -EINVAL;

	header = (struct snd_sof_fw_header *)(fw->data + payload_offset);
	load_module = sof_ops(sdev)->load_module;
	if (!load_module) {
		dev_dbg(sdev->dev, "Using generic module loading\n");
		load_module = sof_ipc3_parse_module_memcpy;
	} else {
		dev_dbg(sdev->dev, "Using custom module loading\n");
	}

	/* parse each module */
	module = (struct snd_sof_mod_hdr *)(fw->data + payload_offset + sizeof(*header));
	remaining = fw->size - sizeof(*header) - payload_offset;
	/* check for wrap */
	if (remaining > fw->size) {
		dev_err(sdev->dev, "%s: fw size smaller than header size\n", __func__);
		return -EINVAL;
	}

	for (count = 0; count < header->num_modules; count++) {
		/* check for wrap */
		if (remaining < sizeof(*module)) {
			dev_err(sdev->dev, "%s: not enough data for a module\n",
				__func__);
			return -EINVAL;
		}

		/* minus header size of module */
		remaining -= sizeof(*module);

		/* module */
		ret = load_module(sdev, module);
		if (ret < 0) {
			dev_err(sdev->dev, "%s: invalid module %d\n", __func__, count);
			return ret;
		}

		if (remaining < module->size) {
			dev_err(sdev->dev, "%s: not enough data remaining\n", __func__);
			return -EINVAL;
		}

		/* minus body size of module */
		remaining -=  module->size;
		module = (struct snd_sof_mod_hdr *)((u8 *)module +
			 sizeof(*module) + module->size);
	}

	return 0;
}

static int sof_ipc3_validate_firmware(struct snd_sof_dev *sdev)
{
	u32 payload_offset = sdev->basefw.payload_offset;
	const struct firmware *fw = sdev->basefw.fw;
	struct snd_sof_fw_header *header;
	size_t fw_size = fw->size - payload_offset;

	if (fw->size <= payload_offset) {
		dev_err(sdev->dev,
			"firmware size must be greater than firmware offset\n");
		return -EINVAL;
	}

	/* Read the header information from the data pointer */
	header = (struct snd_sof_fw_header *)(fw->data + payload_offset);

	/* verify FW sig */
	if (strncmp(header->sig, SND_SOF_FW_SIG, SND_SOF_FW_SIG_SIZE) != 0) {
		dev_err(sdev->dev, "invalid firmware signature\n");
		return -EINVAL;
	}

	/* check size is valid */
	if (fw_size != header->file_size + sizeof(*header)) {
		dev_err(sdev->dev,
			"invalid filesize mismatch got 0x%zx expected 0x%zx\n",
			fw_size, header->file_size + sizeof(*header));
		return -EINVAL;
	}

	dev_dbg(sdev->dev, "header size=0x%x modules=0x%x abi=0x%x size=%zu\n",
		header->file_size, header->num_modules,
		header->abi, sizeof(*header));

	return 0;
}

const struct sof_ipc_fw_loader_ops ipc3_loader_ops = {
	.validate = sof_ipc3_validate_firmware,
	.parse_ext_manifest = sof_ipc3_fw_parse_ext_man,
	.load_fw_to_dsp = sof_ipc3_load_fw_to_dsp,
};
