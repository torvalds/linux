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
#include <sound/sof.h>
#include <sound/sof/ext_manifest.h>
#include "ops.h"

static int get_ext_windows(struct snd_sof_dev *sdev,
			   const struct sof_ipc_ext_data_hdr *ext_hdr)
{
	const struct sof_ipc_window *w =
		container_of(ext_hdr, struct sof_ipc_window, ext_hdr);

	if (w->num_windows == 0 || w->num_windows > SOF_IPC_MAX_ELEMS)
		return -EINVAL;

	if (sdev->info_window) {
		if (memcmp(sdev->info_window, w, ext_hdr->hdr.size)) {
			dev_err(sdev->dev, "error: mismatch between window descriptor from extended manifest and mailbox");
			return -EINVAL;
		}
		return 0;
	}

	/* keep a local copy of the data */
	sdev->info_window = devm_kmemdup(sdev->dev, w, ext_hdr->hdr.size,
					 GFP_KERNEL);
	if (!sdev->info_window)
		return -ENOMEM;

	return 0;
}

static int get_cc_info(struct snd_sof_dev *sdev,
		       const struct sof_ipc_ext_data_hdr *ext_hdr)
{
	int ret;

	const struct sof_ipc_cc_version *cc =
		container_of(ext_hdr, struct sof_ipc_cc_version, ext_hdr);

	if (sdev->cc_version) {
		if (memcmp(sdev->cc_version, cc, cc->ext_hdr.hdr.size)) {
			dev_err(sdev->dev, "error: receive diverged cc_version descriptions");
			return -EINVAL;
		}
		return 0;
	}

	dev_dbg(sdev->dev, "Firmware info: used compiler %s %d:%d:%d%s used optimization flags %s\n",
		cc->name, cc->major, cc->minor, cc->micro, cc->desc,
		cc->optim);

	/* create read-only cc_version debugfs to store compiler version info */
	/* use local copy of the cc_version to prevent data corruption */
	if (sdev->first_boot) {
		sdev->cc_version = devm_kmalloc(sdev->dev, cc->ext_hdr.hdr.size,
						GFP_KERNEL);

		if (!sdev->cc_version)
			return -ENOMEM;

		memcpy(sdev->cc_version, cc, cc->ext_hdr.hdr.size);
		ret = snd_sof_debugfs_buf_item(sdev, sdev->cc_version,
					       cc->ext_hdr.hdr.size,
					       "cc_version", 0444);

		/* errors are only due to memory allocation, not debugfs */
		if (ret < 0) {
			dev_err(sdev->dev, "error: snd_sof_debugfs_buf_item failed\n");
			return ret;
		}
	}

	return 0;
}

/* parse the extended FW boot data structures from FW boot message */
int snd_sof_fw_parse_ext_data(struct snd_sof_dev *sdev, u32 bar, u32 offset)
{
	struct sof_ipc_ext_data_hdr *ext_hdr;
	void *ext_data;
	int ret = 0;

	ext_data = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!ext_data)
		return -ENOMEM;

	/* get first header */
	snd_sof_dsp_block_read(sdev, bar, offset, ext_data,
			       sizeof(*ext_hdr));
	ext_hdr = ext_data;

	while (ext_hdr->hdr.cmd == SOF_IPC_FW_READY) {
		/* read in ext structure */
		snd_sof_dsp_block_read(sdev, bar, offset + sizeof(*ext_hdr),
				   (void *)((u8 *)ext_data + sizeof(*ext_hdr)),
				   ext_hdr->hdr.size - sizeof(*ext_hdr));

		dev_dbg(sdev->dev, "found ext header type %d size 0x%x\n",
			ext_hdr->type, ext_hdr->hdr.size);

		/* process structure data */
		switch (ext_hdr->type) {
		case SOF_IPC_EXT_WINDOW:
			ret = get_ext_windows(sdev, ext_hdr);
			break;
		case SOF_IPC_EXT_CC_INFO:
			ret = get_cc_info(sdev, ext_hdr);
			break;
		case SOF_IPC_EXT_UNUSED:
		case SOF_IPC_EXT_PROBE_INFO:
		case SOF_IPC_EXT_USER_ABI_INFO:
			/* They are supported but we don't do anything here */
			break;
		default:
			dev_info(sdev->dev, "unknown ext header type %d size 0x%x\n",
				 ext_hdr->type, ext_hdr->hdr.size);
			ret = 0;
			break;
		}

		if (ret < 0) {
			dev_err(sdev->dev, "error: failed to parse ext data type %d\n",
				ext_hdr->type);
			break;
		}

		/* move to next header */
		offset += ext_hdr->hdr.size;
		snd_sof_dsp_block_read(sdev, bar, offset, ext_data,
				       sizeof(*ext_hdr));
		ext_hdr = ext_data;
	}

	kfree(ext_data);
	return ret;
}
EXPORT_SYMBOL(snd_sof_fw_parse_ext_data);

static int ext_man_get_fw_version(struct snd_sof_dev *sdev,
				  const struct sof_ext_man_elem_header *hdr)
{
	const struct sof_ext_man_fw_version *v =
		container_of(hdr, struct sof_ext_man_fw_version, hdr);

	memcpy(&sdev->fw_ready.version, &v->version, sizeof(v->version));
	sdev->fw_ready.flags = v->flags;

	/* log ABI versions and check FW compatibility */
	return snd_sof_ipc_valid(sdev);
}

static int ext_man_get_windows(struct snd_sof_dev *sdev,
			       const struct sof_ext_man_elem_header *hdr)
{
	const struct sof_ext_man_window *w;

	w = container_of(hdr, struct sof_ext_man_window, hdr);

	return get_ext_windows(sdev, &w->ipc_window.ext_hdr);
}

static int ext_man_get_cc_info(struct snd_sof_dev *sdev,
			       const struct sof_ext_man_elem_header *hdr)
{
	const struct sof_ext_man_cc_version *cc;

	cc = container_of(hdr, struct sof_ext_man_cc_version, hdr);

	return get_cc_info(sdev, &cc->cc_version.ext_hdr);
}

static int ext_man_get_dbg_abi_info(struct snd_sof_dev *sdev,
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

static int ext_man_get_config_data(struct snd_sof_dev *sdev,
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

	dev_dbg(sdev->dev, "%s can hold up to %d config elements\n",
		__func__, elems_counter);

	for (i = 0; i < elems_counter; ++i) {
		elem = &config->elems[i];
		dev_dbg(sdev->dev, "%s get index %d token %d val %d\n",
			__func__, i, elem->token, elem->value);
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
			dev_info(sdev->dev, "Unknown firmware configuration token %d value %d",
				 elem->token, elem->value);
			break;
		}
		if (ret < 0) {
			dev_err(sdev->dev, "error: processing sof_ext_man_config_data failed for token %d value 0x%x, %d\n",
				elem->token, elem->value, ret);
			return ret;
		}
	}

	return 0;
}

static ssize_t snd_sof_ext_man_size(const struct firmware *fw)
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
	return 0;
}

/* parse extended FW manifest data structures */
static int snd_sof_fw_ext_man_parse(struct snd_sof_dev *sdev,
				    const struct firmware *fw)
{
	const struct sof_ext_man_elem_header *elem_hdr;
	const struct sof_ext_man_header *head;
	ssize_t ext_man_size;
	ssize_t remaining;
	uintptr_t iptr;
	int ret = 0;

	head = (struct sof_ext_man_header *)fw->data;
	remaining = head->full_size - head->header_size;
	ext_man_size = snd_sof_ext_man_size(fw);

	/* Assert firmware starts with extended manifest */
	if (ext_man_size <= 0)
		return ext_man_size;

	/* incompatible version */
	if (SOF_EXT_MAN_VERSION_INCOMPATIBLE(SOF_EXT_MAN_VERSION,
					     head->header_version)) {
		dev_err(sdev->dev, "error: extended manifest version 0x%X differ from used 0x%X\n",
			head->header_version, SOF_EXT_MAN_VERSION);
		return -EINVAL;
	}

	/* get first extended manifest element header */
	iptr = (uintptr_t)fw->data + head->header_size;

	while (remaining > sizeof(*elem_hdr)) {
		elem_hdr = (struct sof_ext_man_elem_header *)iptr;

		dev_dbg(sdev->dev, "found sof_ext_man header type %d size 0x%X\n",
			elem_hdr->type, elem_hdr->size);

		if (elem_hdr->size < sizeof(*elem_hdr) ||
		    elem_hdr->size > remaining) {
			dev_err(sdev->dev, "error: invalid sof_ext_man header size, type %d size 0x%X\n",
				elem_hdr->type, elem_hdr->size);
			return -EINVAL;
		}

		/* process structure data */
		switch (elem_hdr->type) {
		case SOF_EXT_MAN_ELEM_FW_VERSION:
			ret = ext_man_get_fw_version(sdev, elem_hdr);
			break;
		case SOF_EXT_MAN_ELEM_WINDOW:
			ret = ext_man_get_windows(sdev, elem_hdr);
			break;
		case SOF_EXT_MAN_ELEM_CC_VERSION:
			ret = ext_man_get_cc_info(sdev, elem_hdr);
			break;
		case SOF_EXT_MAN_ELEM_DBG_ABI:
			ret = ext_man_get_dbg_abi_info(sdev, elem_hdr);
			break;
		case SOF_EXT_MAN_ELEM_CONFIG_DATA:
			ret = ext_man_get_config_data(sdev, elem_hdr);
			break;
		case SOF_EXT_MAN_ELEM_PLATFORM_CONFIG_DATA:
			ret = snd_sof_dsp_parse_platform_ext_manifest(sdev, elem_hdr);
			break;
		default:
			dev_info(sdev->dev, "unknown sof_ext_man header type %d size 0x%X\n",
				 elem_hdr->type, elem_hdr->size);
			break;
		}

		if (ret < 0) {
			dev_err(sdev->dev, "error: failed to parse sof_ext_man header type %d size 0x%X\n",
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

/*
 * IPC Firmware ready.
 */
static void sof_get_windows(struct snd_sof_dev *sdev)
{
	struct sof_ipc_window_elem *elem;
	u32 outbox_offset = 0;
	u32 stream_offset = 0;
	u32 inbox_offset = 0;
	u32 outbox_size = 0;
	u32 stream_size = 0;
	u32 inbox_size = 0;
	u32 debug_size = 0;
	u32 debug_offset = 0;
	int window_offset;
	int bar;
	int i;

	if (!sdev->info_window) {
		dev_err(sdev->dev, "error: have no window info\n");
		return;
	}

	bar = snd_sof_dsp_get_bar_index(sdev, SOF_FW_BLK_TYPE_SRAM);
	if (bar < 0) {
		dev_err(sdev->dev, "error: have no bar mapping\n");
		return;
	}

	for (i = 0; i < sdev->info_window->num_windows; i++) {
		elem = &sdev->info_window->window[i];

		window_offset = snd_sof_dsp_get_window_offset(sdev, elem->id);
		if (window_offset < 0) {
			dev_warn(sdev->dev, "warn: no offset for window %d\n",
				 elem->id);
			continue;
		}

		switch (elem->type) {
		case SOF_IPC_REGION_UPBOX:
			inbox_offset = window_offset + elem->offset;
			inbox_size = elem->size;
			snd_sof_debugfs_io_item(sdev,
						sdev->bar[bar] +
						inbox_offset,
						elem->size, "inbox",
						SOF_DEBUGFS_ACCESS_D0_ONLY);
			break;
		case SOF_IPC_REGION_DOWNBOX:
			outbox_offset = window_offset + elem->offset;
			outbox_size = elem->size;
			snd_sof_debugfs_io_item(sdev,
						sdev->bar[bar] +
						outbox_offset,
						elem->size, "outbox",
						SOF_DEBUGFS_ACCESS_D0_ONLY);
			break;
		case SOF_IPC_REGION_TRACE:
			snd_sof_debugfs_io_item(sdev,
						sdev->bar[bar] +
						window_offset +
						elem->offset,
						elem->size, "etrace",
						SOF_DEBUGFS_ACCESS_D0_ONLY);
			break;
		case SOF_IPC_REGION_DEBUG:
			debug_offset = window_offset + elem->offset;
			debug_size = elem->size;
			snd_sof_debugfs_io_item(sdev,
						sdev->bar[bar] +
						window_offset +
						elem->offset,
						elem->size, "debug",
						SOF_DEBUGFS_ACCESS_D0_ONLY);
			break;
		case SOF_IPC_REGION_STREAM:
			stream_offset = window_offset + elem->offset;
			stream_size = elem->size;
			snd_sof_debugfs_io_item(sdev,
						sdev->bar[bar] +
						stream_offset,
						elem->size, "stream",
						SOF_DEBUGFS_ACCESS_D0_ONLY);
			break;
		case SOF_IPC_REGION_REGS:
			snd_sof_debugfs_io_item(sdev,
						sdev->bar[bar] +
						window_offset +
						elem->offset,
						elem->size, "regs",
						SOF_DEBUGFS_ACCESS_D0_ONLY);
			break;
		case SOF_IPC_REGION_EXCEPTION:
			sdev->dsp_oops_offset = window_offset + elem->offset;
			snd_sof_debugfs_io_item(sdev,
						sdev->bar[bar] +
						window_offset +
						elem->offset,
						elem->size, "exception",
						SOF_DEBUGFS_ACCESS_D0_ONLY);
			break;
		default:
			dev_err(sdev->dev, "error: get illegal window info\n");
			return;
		}
	}

	if (outbox_size == 0 || inbox_size == 0) {
		dev_err(sdev->dev, "error: get illegal mailbox window\n");
		return;
	}

	snd_sof_dsp_mailbox_init(sdev, inbox_offset, inbox_size,
				 outbox_offset, outbox_size);
	sdev->stream_box.offset = stream_offset;
	sdev->stream_box.size = stream_size;

	sdev->debug_box.offset = debug_offset;
	sdev->debug_box.size = debug_size;

	dev_dbg(sdev->dev, " mailbox upstream 0x%x - size 0x%x\n",
		inbox_offset, inbox_size);
	dev_dbg(sdev->dev, " mailbox downstream 0x%x - size 0x%x\n",
		outbox_offset, outbox_size);
	dev_dbg(sdev->dev, " stream region 0x%x - size 0x%x\n",
		stream_offset, stream_size);
	dev_dbg(sdev->dev, " debug region 0x%x - size 0x%x\n",
		debug_offset, debug_size);
}

/* check for ABI compatibility and create memory windows on first boot */
int sof_fw_ready(struct snd_sof_dev *sdev, u32 msg_id)
{
	struct sof_ipc_fw_ready *fw_ready = &sdev->fw_ready;
	int offset;
	int bar;
	int ret;

	/* mailbox must be on 4k boundary */
	offset = snd_sof_dsp_get_mailbox_offset(sdev);
	if (offset < 0) {
		dev_err(sdev->dev, "error: have no mailbox offset\n");
		return offset;
	}

	bar = snd_sof_dsp_get_bar_index(sdev, SOF_FW_BLK_TYPE_SRAM);
	if (bar < 0) {
		dev_err(sdev->dev, "error: have no bar mapping\n");
		return -EINVAL;
	}

	dev_dbg(sdev->dev, "ipc: DSP is ready 0x%8.8x offset 0x%x\n",
		msg_id, offset);

	/* no need to re-check version/ABI for subsequent boots */
	if (!sdev->first_boot)
		return 0;

	/* copy data from the DSP FW ready offset */
	snd_sof_dsp_block_read(sdev, bar, offset, fw_ready, sizeof(*fw_ready));

	/* make sure ABI version is compatible */
	ret = snd_sof_ipc_valid(sdev);
	if (ret < 0)
		return ret;

	/* now check for extended data */
	snd_sof_fw_parse_ext_data(sdev, bar, offset +
				  sizeof(struct sof_ipc_fw_ready));

	sof_get_windows(sdev);

	return 0;
}
EXPORT_SYMBOL(sof_fw_ready);

/* generic module parser for mmaped DSPs */
int snd_sof_parse_module_memcpy(struct snd_sof_dev *sdev,
				struct snd_sof_mod_hdr *module)
{
	struct snd_sof_blk_hdr *block;
	int count, bar;
	u32 offset;
	size_t remaining;

	dev_dbg(sdev->dev, "new module size 0x%x blocks 0x%x type 0x%x\n",
		module->size, module->num_blocks, module->type);

	block = (struct snd_sof_blk_hdr *)((u8 *)module + sizeof(*module));

	/* module->size doesn't include header size */
	remaining = module->size;
	for (count = 0; count < module->num_blocks; count++) {
		/* check for wrap */
		if (remaining < sizeof(*block)) {
			dev_err(sdev->dev, "error: not enough data remaining\n");
			return -EINVAL;
		}

		/* minus header size of block */
		remaining -= sizeof(*block);

		if (block->size == 0) {
			dev_warn(sdev->dev,
				 "warning: block %d size zero\n", count);
			dev_warn(sdev->dev, " type 0x%x offset 0x%x\n",
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
			bar = snd_sof_dsp_get_bar_index(sdev, block->type);
			if (bar < 0) {
				dev_err(sdev->dev,
					"error: no BAR mapping for block type 0x%x\n",
					block->type);
				return bar;
			}
			break;
		default:
			dev_err(sdev->dev, "error: bad type 0x%x for block 0x%x\n",
				block->type, count);
			return -EINVAL;
		}

		dev_dbg(sdev->dev,
			"block %d type 0x%x size 0x%x ==>  offset 0x%x\n",
			count, block->type, block->size, offset);

		/* checking block->size to avoid unaligned access */
		if (block->size % sizeof(u32)) {
			dev_err(sdev->dev, "error: invalid block size 0x%x\n",
				block->size);
			return -EINVAL;
		}
		snd_sof_dsp_block_write(sdev, bar, offset,
					block + 1, block->size);

		if (remaining < block->size) {
			dev_err(sdev->dev, "error: not enough data remaining\n");
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
EXPORT_SYMBOL(snd_sof_parse_module_memcpy);

static int check_header(struct snd_sof_dev *sdev, const struct firmware *fw,
			size_t fw_offset)
{
	struct snd_sof_fw_header *header;
	size_t fw_size = fw->size - fw_offset;

	if (fw->size <= fw_offset) {
		dev_err(sdev->dev, "error: firmware size must be greater than firmware offset\n");
		return -EINVAL;
	}

	/* Read the header information from the data pointer */
	header = (struct snd_sof_fw_header *)(fw->data + fw_offset);

	/* verify FW sig */
	if (strncmp(header->sig, SND_SOF_FW_SIG, SND_SOF_FW_SIG_SIZE) != 0) {
		dev_err(sdev->dev, "error: invalid firmware signature\n");
		return -EINVAL;
	}

	/* check size is valid */
	if (fw_size != header->file_size + sizeof(*header)) {
		dev_err(sdev->dev, "error: invalid filesize mismatch got 0x%zx expected 0x%zx\n",
			fw_size, header->file_size + sizeof(*header));
		return -EINVAL;
	}

	dev_dbg(sdev->dev, "header size=0x%x modules=0x%x abi=0x%x size=%zu\n",
		header->file_size, header->num_modules,
		header->abi, sizeof(*header));

	return 0;
}

static int load_modules(struct snd_sof_dev *sdev, const struct firmware *fw,
			size_t fw_offset)
{
	struct snd_sof_fw_header *header;
	struct snd_sof_mod_hdr *module;
	int (*load_module)(struct snd_sof_dev *sof_dev,
			   struct snd_sof_mod_hdr *hdr);
	int ret, count;
	size_t remaining;

	header = (struct snd_sof_fw_header *)(fw->data + fw_offset);
	load_module = sof_ops(sdev)->load_module;
	if (!load_module)
		return -EINVAL;

	/* parse each module */
	module = (struct snd_sof_mod_hdr *)(fw->data + fw_offset +
					    sizeof(*header));
	remaining = fw->size - sizeof(*header) - fw_offset;
	/* check for wrap */
	if (remaining > fw->size) {
		dev_err(sdev->dev, "error: fw size smaller than header size\n");
		return -EINVAL;
	}

	for (count = 0; count < header->num_modules; count++) {
		/* check for wrap */
		if (remaining < sizeof(*module)) {
			dev_err(sdev->dev, "error: not enough data remaining\n");
			return -EINVAL;
		}

		/* minus header size of module */
		remaining -= sizeof(*module);

		/* module */
		ret = load_module(sdev, module);
		if (ret < 0) {
			dev_err(sdev->dev, "error: invalid module %d\n", count);
			return ret;
		}

		if (remaining < module->size) {
			dev_err(sdev->dev, "error: not enough data remaining\n");
			return -EINVAL;
		}

		/* minus body size of module */
		remaining -=  module->size;
		module = (struct snd_sof_mod_hdr *)((u8 *)module
			+ sizeof(*module) + module->size);
	}

	return 0;
}

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
	ext_man_size = snd_sof_fw_ext_man_parse(sdev, plat_data->fw);
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
	ret = check_header(sdev, plat_data->fw, plat_data->fw_offset);
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
	ret = load_modules(sdev, plat_data->fw, plat_data->fw_offset);
	if (ret < 0) {
		dev_err(sdev->dev, "error: invalid FW modules\n");
		goto error;
	}

	return 0;

error:
	release_firmware(plat_data->fw);
	plat_data->fw = NULL;
	return ret;

}
EXPORT_SYMBOL(snd_sof_load_firmware_memcpy);

int snd_sof_load_firmware(struct snd_sof_dev *sdev)
{
	dev_dbg(sdev->dev, "loading firmware\n");

	if (sof_ops(sdev)->load_firmware)
		return sof_ops(sdev)->load_firmware(sdev);
	return 0;
}
EXPORT_SYMBOL(snd_sof_load_firmware);

int snd_sof_run_firmware(struct snd_sof_dev *sdev)
{
	int ret;

	init_waitqueue_head(&sdev->boot_wait);

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
		dev_err(sdev->dev, "error: failed to reset DSP\n");
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
		dev_err(sdev->dev, "error: firmware boot failure\n");
		snd_sof_dsp_dbg_dump(sdev, SOF_DBG_DUMP_REGS | SOF_DBG_DUMP_MBOX |
			SOF_DBG_DUMP_TEXT | SOF_DBG_DUMP_PCI | SOF_DBG_DUMP_FORCE_ERR_LEVEL);
		sdev->fw_state = SOF_FW_BOOT_FAILED;
		return -EIO;
	}

	if (sdev->fw_state == SOF_FW_BOOT_COMPLETE)
		dev_dbg(sdev->dev, "firmware boot complete\n");
	else
		return -EIO; /* FW boots but fw_ready op failed */

	/* perform post fw run operations */
	ret = snd_sof_dsp_post_fw_run(sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed post fw run op\n");
		return ret;
	}

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
