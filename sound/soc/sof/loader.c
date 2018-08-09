// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
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

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/firmware.h>
#include <sound/sof.h>
#include <uapi/sound/sof-fw.h>
#include "sof-priv.h"
#include "ops.h"

static int get_ext_windows(struct snd_sof_dev *sdev,
			   struct sof_ipc_ext_data_hdr *ext_hdr)
{
	struct sof_ipc_window *w = (struct sof_ipc_window *)ext_hdr;

	int ret = 0;
	size_t size;

	if (w->num_windows == 0 || w->num_windows > SOF_IPC_MAX_ELEMS)
		return -EINVAL;

	size = sizeof(*w) + sizeof(struct sof_ipc_window_elem) * w->num_windows;

	/* keep a local copy of the data */
	sdev->info_window = kmemdup(w, size, GFP_KERNEL);
	if (!sdev->info_window)
		return -ENOMEM;

	return ret;
}

/* parse the extended FW boot data structures from FW boot message */
int snd_sof_fw_parse_ext_data(struct snd_sof_dev *sdev, u32 offset)
{
	struct sof_ipc_ext_data_hdr *ext_hdr;
	void *ext_data;
	int ret = 0;

	ext_data = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!ext_data)
		return -ENOMEM;

	/* get first header */
	snd_sof_dsp_block_read(sdev, offset, ext_data, sizeof(*ext_hdr));
	ext_hdr = (struct sof_ipc_ext_data_hdr *)ext_data;

	while (ext_hdr->hdr.cmd == SOF_IPC_FW_READY) {
		/* read in ext structure */
		offset += sizeof(*ext_hdr);
		snd_sof_dsp_block_read(sdev, offset,
				       ext_data + sizeof(*ext_hdr),
				       ext_hdr->hdr.size - sizeof(*ext_hdr));

		dev_dbg(sdev->dev, "found ext header type %d size 0x%x\n",
			ext_hdr->type, ext_hdr->hdr.size);

		/* process structure data */
		switch (ext_hdr->type) {
		case SOF_IPC_EXT_DMA_BUFFER:
			break;
		case SOF_IPC_EXT_WINDOW:
			ret = get_ext_windows(sdev, ext_hdr);
			break;
		default:
			break;
		}

		if (ret < 0) {
			dev_err(sdev->dev, "error: failed to parse ext data type %d\n",
				ext_hdr->type);
		}

		/* move to next header */
		offset += ext_hdr->hdr.size;
		snd_sof_dsp_block_read(sdev, offset, ext_data,
				       sizeof(*ext_hdr));
		ext_hdr = (struct sof_ipc_ext_data_hdr *)ext_data;
	}

	kfree(ext_data);
	return ret;
}
EXPORT_SYMBOL(snd_sof_fw_parse_ext_data);

/* generic module parser for mmaped DSPs */
int snd_sof_parse_module_memcpy(struct snd_sof_dev *sdev,
				struct snd_sof_mod_hdr *module)
{
	struct snd_sof_blk_hdr *block;
	int count;
	u32 offset;

	dev_dbg(sdev->dev, "new module size 0x%x blocks 0x%x type 0x%x\n",
		module->size, module->num_blocks, module->type);

	block = (void *)module + sizeof(*module);

	for (count = 0; count < module->num_blocks; count++) {
		if (block->size == 0) {
			dev_warn(sdev->dev,
				 "warning: block %d size zero\n", count);
			dev_warn(sdev->dev, " type 0x%x offset 0x%x\n",
				 block->type, block->offset);
			continue;
		}

		switch (block->type) {
		case SOF_BLK_IMAGE:
		case SOF_BLK_CACHE:
		case SOF_BLK_REGS:
		case SOF_BLK_SIG:
		case SOF_BLK_ROM:
			continue;	/* not handled atm */
		case SOF_BLK_TEXT:
		case SOF_BLK_DATA:
			offset = block->offset;
			break;
		default:
			dev_err(sdev->dev, "error: bad type 0x%x for block 0x%x\n",
				block->type, count);
			return -EINVAL;
		}

		dev_dbg(sdev->dev,
			"block %d type 0x%x size 0x%x ==>  offset 0x%x\n",
			count, block->type, block->size, offset);

		snd_sof_dsp_block_write(sdev, offset,
					(void *)block + sizeof(*block),
					block->size);

		/* next block */
		block = (void *)block + sizeof(*block) + block->size;
	}

	return 0;
}
EXPORT_SYMBOL(snd_sof_parse_module_memcpy);

static int check_header(struct snd_sof_dev *sdev, const struct firmware *fw)
{
	struct snd_sof_fw_header *header;

	/* Read the header information from the data pointer */
	header = (struct snd_sof_fw_header *)fw->data;

	/* verify FW sig */
	if (strncmp(header->sig, SND_SOF_FW_SIG, SND_SOF_FW_SIG_SIZE) != 0) {
		dev_err(sdev->dev, "error: invalid firmware signature\n");
		return -EINVAL;
	}

	/* check size is valid */
	if (fw->size != header->file_size + sizeof(*header)) {
		dev_err(sdev->dev, "error: invalid filesize mismatch got 0x%zx expected 0x%zx\n",
			fw->size, header->file_size + sizeof(*header));
		return -EINVAL;
	}

	dev_dbg(sdev->dev, "header size=0x%x modules=0x%x abi=0x%x size=%zu\n",
		header->file_size, header->num_modules,
		header->abi, sizeof(*header));

	return 0;
}

static int load_modules(struct snd_sof_dev *sdev, const struct firmware *fw)
{
	struct snd_sof_fw_header *header;
	struct snd_sof_mod_hdr *module;
	int (*load_module)(struct snd_sof_dev *sof_dev,
			   struct snd_sof_mod_hdr *hdr);
	int ret, count;

	header = (struct snd_sof_fw_header *)fw->data;
	load_module = sdev->ops->load_module;
	if (!load_module)
		return -EINVAL;

	/* parse each module */
	module = (void *)fw->data + sizeof(*header);
	for (count = 0; count < header->num_modules; count++) {
		/* module */
		ret = load_module(sdev, module);
		if (ret < 0) {
			dev_err(sdev->dev, "error: invalid module %d\n", count);
			return ret;
		}
		module = (void *)module + sizeof(*module) + module->size;
	}

	return 0;
}

int snd_sof_load_firmware_memcpy(struct snd_sof_dev *sdev,
				 bool first_boot)
{
	struct snd_sof_pdata *plat_data = dev_get_platdata(sdev->dev);
	int ret;

	/* set code loading condition to true */
	sdev->code_loading = 1;

	ret = request_firmware(&plat_data->fw,
			       plat_data->machine->sof_fw_filename, sdev->dev);

	if (ret < 0) {
		dev_err(sdev->dev, "error: request firmware failed err: %d\n",
			ret);
		return ret;
	}

	/* make sure the FW header and file is valid */
	ret = check_header(sdev, plat_data->fw);
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
	ret = load_modules(sdev, plat_data->fw);
	if (ret < 0) {
		dev_err(sdev->dev, "error: invalid FW modules\n");
		goto error;
	}

	return 0;

error:
	release_firmware(plat_data->fw);
	return ret;

}
EXPORT_SYMBOL(snd_sof_load_firmware_memcpy);

int snd_sof_load_firmware(struct snd_sof_dev *sdev,
			  bool first_boot)
{
	dev_dbg(sdev->dev, "loading firmware\n");

	if (sdev->ops->load_firmware)
		return sdev->ops->load_firmware(sdev, first_boot);
	return 0;
}
EXPORT_SYMBOL(snd_sof_load_firmware);

int snd_sof_run_firmware(struct snd_sof_dev *sdev)
{
	int ret;

	init_waitqueue_head(&sdev->boot_wait);
	sdev->boot_complete = false;

	dev_dbg(sdev->dev, "booting DSP firmware\n");

	/* boot the firmware on the DSP */
	ret = snd_sof_dsp_run(sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to reset DSP\n");
		return ret;
	}

	/* now wait for the DSP to boot */
	ret = wait_event_timeout(sdev->boot_wait, sdev->boot_complete,
				 msecs_to_jiffies(sdev->boot_timeout));
	if (ret == 0) {
		dev_err(sdev->dev, "error: firmware boot timeout\n");
		snd_sof_dsp_dbg_dump(sdev, SOF_DBG_REGS | SOF_DBG_MBOX |
			SOF_DBG_TEXT | SOF_DBG_PCI);
		return -EIO;
	}

	dev_info(sdev->dev, "firmware boot complete\n");

	return 0;
}
EXPORT_SYMBOL(snd_sof_run_firmware);

void snd_sof_fw_unload(struct snd_sof_dev *sdev)
{
	/* TODO: support module unloading at runtime */
}
EXPORT_SYMBOL(snd_sof_fw_unload);
