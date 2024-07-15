// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2020 Intel Corporation
//
// Author: Cezary Rojewski <cezary.rojewski@intel.com>
//

#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include "core.h"
#include "registers.h"

/* FW load (200ms) plus operational delays */
#define FW_READY_TIMEOUT_MS	250

#define FW_SIGNATURE		"$SST"
#define FW_SIGNATURE_SIZE	4

struct catpt_fw_hdr {
	char signature[FW_SIGNATURE_SIZE];
	u32 file_size;
	u32 modules;
	u32 file_format;
	u32 reserved[4];
} __packed;

struct catpt_fw_mod_hdr {
	char signature[FW_SIGNATURE_SIZE];
	u32 mod_size;
	u32 blocks;
	u16 slot;
	u16 module_id;
	u32 entry_point;
	u32 persistent_size;
	u32 scratch_size;
} __packed;

enum catpt_ram_type {
	CATPT_RAM_TYPE_IRAM = 1,
	CATPT_RAM_TYPE_DRAM = 2,
	/* DRAM with module's initial state */
	CATPT_RAM_TYPE_INSTANCE = 3,
};

struct catpt_fw_block_hdr {
	u32 ram_type;
	u32 size;
	u32 ram_offset;
	u32 rsvd;
} __packed;

void catpt_sram_init(struct resource *sram, u32 start, u32 size)
{
	sram->start = start;
	sram->end = start + size - 1;
}

void catpt_sram_free(struct resource *sram)
{
	struct resource *res, *save;

	for (res = sram->child; res;) {
		save = res->sibling;
		release_resource(res);
		kfree(res);
		res = save;
	}
}

struct resource *
catpt_request_region(struct resource *root, resource_size_t size)
{
	struct resource *res = root->child;
	resource_size_t addr = root->start;

	for (;;) {
		if (res->start - addr >= size)
			break;
		addr = res->end + 1;
		res = res->sibling;
		if (!res)
			return NULL;
	}

	return __request_region(root, addr, size, NULL, 0);
}

int catpt_store_streams_context(struct catpt_dev *cdev, struct dma_chan *chan)
{
	struct catpt_stream_runtime *stream;

	list_for_each_entry(stream, &cdev->stream_list, node) {
		u32 off, size;
		int ret;

		off = stream->persistent->start;
		size = resource_size(stream->persistent);
		dev_dbg(cdev->dev, "storing stream %d ctx: off 0x%08x size %d\n",
			stream->info.stream_hw_id, off, size);

		ret = catpt_dma_memcpy_fromdsp(cdev, chan,
					       cdev->dxbuf_paddr + off,
					       cdev->lpe_base + off,
					       ALIGN(size, 4));
		if (ret) {
			dev_err(cdev->dev, "memcpy fromdsp failed: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

int catpt_store_module_states(struct catpt_dev *cdev, struct dma_chan *chan)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cdev->modules); i++) {
		struct catpt_module_type *type;
		u32 off;
		int ret;

		type = &cdev->modules[i];
		if (!type->loaded || !type->state_size)
			continue;

		off = type->state_offset;
		dev_dbg(cdev->dev, "storing mod %d state: off 0x%08x size %d\n",
			i, off, type->state_size);

		ret = catpt_dma_memcpy_fromdsp(cdev, chan,
					       cdev->dxbuf_paddr + off,
					       cdev->lpe_base + off,
					       ALIGN(type->state_size, 4));
		if (ret) {
			dev_err(cdev->dev, "memcpy fromdsp failed: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

int catpt_store_memdumps(struct catpt_dev *cdev, struct dma_chan *chan)
{
	int i;

	for (i = 0; i < cdev->dx_ctx.num_meminfo; i++) {
		struct catpt_save_meminfo *info;
		u32 off;
		int ret;

		info = &cdev->dx_ctx.meminfo[i];
		if (info->source != CATPT_DX_TYPE_MEMORY_DUMP)
			continue;

		off = catpt_to_host_offset(info->offset);
		if (off < cdev->dram.start || off > cdev->dram.end)
			continue;

		dev_dbg(cdev->dev, "storing memdump: off 0x%08x size %d\n",
			off, info->size);

		ret = catpt_dma_memcpy_fromdsp(cdev, chan,
					       cdev->dxbuf_paddr + off,
					       cdev->lpe_base + off,
					       ALIGN(info->size, 4));
		if (ret) {
			dev_err(cdev->dev, "memcpy fromdsp failed: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int
catpt_restore_streams_context(struct catpt_dev *cdev, struct dma_chan *chan)
{
	struct catpt_stream_runtime *stream;

	list_for_each_entry(stream, &cdev->stream_list, node) {
		u32 off, size;
		int ret;

		off = stream->persistent->start;
		size = resource_size(stream->persistent);
		dev_dbg(cdev->dev, "restoring stream %d ctx: off 0x%08x size %d\n",
			stream->info.stream_hw_id, off, size);

		ret = catpt_dma_memcpy_todsp(cdev, chan,
					     cdev->lpe_base + off,
					     cdev->dxbuf_paddr + off,
					     ALIGN(size, 4));
		if (ret) {
			dev_err(cdev->dev, "memcpy fromdsp failed: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int catpt_restore_memdumps(struct catpt_dev *cdev, struct dma_chan *chan)
{
	int i;

	for (i = 0; i < cdev->dx_ctx.num_meminfo; i++) {
		struct catpt_save_meminfo *info;
		u32 off;
		int ret;

		info = &cdev->dx_ctx.meminfo[i];
		if (info->source != CATPT_DX_TYPE_MEMORY_DUMP)
			continue;

		off = catpt_to_host_offset(info->offset);
		if (off < cdev->dram.start || off > cdev->dram.end)
			continue;

		dev_dbg(cdev->dev, "restoring memdump: off 0x%08x size %d\n",
			off, info->size);

		ret = catpt_dma_memcpy_todsp(cdev, chan,
					     cdev->lpe_base + off,
					     cdev->dxbuf_paddr + off,
					     ALIGN(info->size, 4));
		if (ret) {
			dev_err(cdev->dev, "restore block failed: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int catpt_restore_fwimage(struct catpt_dev *cdev,
				 struct dma_chan *chan, dma_addr_t paddr,
				 struct catpt_fw_block_hdr *blk)
{
	struct resource r1, r2, common;
	int i;

	print_hex_dump_debug(__func__, DUMP_PREFIX_OFFSET, 8, 4,
			     blk, sizeof(*blk), false);

	r1.start = cdev->dram.start + blk->ram_offset;
	r1.end = r1.start + blk->size - 1;
	/* advance to data area */
	paddr += sizeof(*blk);

	for (i = 0; i < cdev->dx_ctx.num_meminfo; i++) {
		struct catpt_save_meminfo *info;
		u32 off;
		int ret;

		info = &cdev->dx_ctx.meminfo[i];

		if (info->source != CATPT_DX_TYPE_FW_IMAGE)
			continue;

		off = catpt_to_host_offset(info->offset);
		if (off < cdev->dram.start || off > cdev->dram.end)
			continue;

		r2.start = off;
		r2.end = r2.start + info->size - 1;

		if (!resource_intersection(&r2, &r1, &common))
			continue;
		/* calculate start offset of common data area */
		off = common.start - r1.start;

		dev_dbg(cdev->dev, "restoring fwimage: %pr\n", &common);

		ret = catpt_dma_memcpy_todsp(cdev, chan, common.start,
					     paddr + off,
					     resource_size(&common));
		if (ret) {
			dev_err(cdev->dev, "memcpy todsp failed: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int catpt_load_block(struct catpt_dev *cdev,
			    struct dma_chan *chan, dma_addr_t paddr,
			    struct catpt_fw_block_hdr *blk, bool alloc)
{
	struct resource *sram, *res;
	dma_addr_t dst_addr;
	int ret;

	print_hex_dump_debug(__func__, DUMP_PREFIX_OFFSET, 8, 4,
			     blk, sizeof(*blk), false);

	switch (blk->ram_type) {
	case CATPT_RAM_TYPE_IRAM:
		sram = &cdev->iram;
		break;
	default:
		sram = &cdev->dram;
		break;
	}

	dst_addr = sram->start + blk->ram_offset;
	if (alloc) {
		res = __request_region(sram, dst_addr, blk->size, NULL, 0);
		if (!res)
			return -EBUSY;
	}

	/* advance to data area */
	paddr += sizeof(*blk);

	ret = catpt_dma_memcpy_todsp(cdev, chan, dst_addr, paddr, blk->size);
	if (ret) {
		dev_err(cdev->dev, "memcpy error: %d\n", ret);
		__release_region(sram, dst_addr, blk->size);
	}

	return ret;
}

static int catpt_restore_basefw(struct catpt_dev *cdev,
				struct dma_chan *chan, dma_addr_t paddr,
				struct catpt_fw_mod_hdr *basefw)
{
	u32 offset = sizeof(*basefw);
	int ret, i;

	print_hex_dump_debug(__func__, DUMP_PREFIX_OFFSET, 8, 4,
			     basefw, sizeof(*basefw), false);

	/* restore basefw image */
	for (i = 0; i < basefw->blocks; i++) {
		struct catpt_fw_block_hdr *blk;

		blk = (struct catpt_fw_block_hdr *)((u8 *)basefw + offset);

		switch (blk->ram_type) {
		case CATPT_RAM_TYPE_IRAM:
			ret = catpt_load_block(cdev, chan, paddr + offset,
					       blk, false);
			break;
		default:
			ret = catpt_restore_fwimage(cdev, chan, paddr + offset,
						    blk);
			break;
		}

		if (ret) {
			dev_err(cdev->dev, "restore block failed: %d\n", ret);
			return ret;
		}

		offset += sizeof(*blk) + blk->size;
	}

	/* then proceed with memory dumps */
	ret = catpt_restore_memdumps(cdev, chan);
	if (ret)
		dev_err(cdev->dev, "restore memdumps failed: %d\n", ret);

	return ret;
}

static int catpt_restore_module(struct catpt_dev *cdev,
				struct dma_chan *chan, dma_addr_t paddr,
				struct catpt_fw_mod_hdr *mod)
{
	u32 offset = sizeof(*mod);
	int i;

	print_hex_dump_debug(__func__, DUMP_PREFIX_OFFSET, 8, 4,
			     mod, sizeof(*mod), false);

	for (i = 0; i < mod->blocks; i++) {
		struct catpt_fw_block_hdr *blk;
		int ret;

		blk = (struct catpt_fw_block_hdr *)((u8 *)mod + offset);

		switch (blk->ram_type) {
		case CATPT_RAM_TYPE_INSTANCE:
			/* restore module state */
			ret = catpt_dma_memcpy_todsp(cdev, chan,
					cdev->lpe_base + blk->ram_offset,
					cdev->dxbuf_paddr + blk->ram_offset,
					ALIGN(blk->size, 4));
			break;
		default:
			ret = catpt_load_block(cdev, chan, paddr + offset,
					       blk, false);
			break;
		}

		if (ret) {
			dev_err(cdev->dev, "restore block failed: %d\n", ret);
			return ret;
		}

		offset += sizeof(*blk) + blk->size;
	}

	return 0;
}

static int catpt_load_module(struct catpt_dev *cdev,
			     struct dma_chan *chan, dma_addr_t paddr,
			     struct catpt_fw_mod_hdr *mod)
{
	struct catpt_module_type *type;
	u32 offset = sizeof(*mod);
	int i;

	print_hex_dump_debug(__func__, DUMP_PREFIX_OFFSET, 8, 4,
			     mod, sizeof(*mod), false);

	type = &cdev->modules[mod->module_id];

	for (i = 0; i < mod->blocks; i++) {
		struct catpt_fw_block_hdr *blk;
		int ret;

		blk = (struct catpt_fw_block_hdr *)((u8 *)mod + offset);

		ret = catpt_load_block(cdev, chan, paddr + offset, blk, true);
		if (ret) {
			dev_err(cdev->dev, "load block failed: %d\n", ret);
			return ret;
		}

		/*
		 * Save state window coordinates - these will be
		 * used to capture module state on D0 exit.
		 */
		if (blk->ram_type == CATPT_RAM_TYPE_INSTANCE) {
			type->state_offset = blk->ram_offset;
			type->state_size = blk->size;
		}

		offset += sizeof(*blk) + blk->size;
	}

	/* init module type static info */
	type->loaded = true;
	/* DSP expects address from module header substracted by 4 */
	type->entry_point = mod->entry_point - 4;
	type->persistent_size = mod->persistent_size;
	type->scratch_size = mod->scratch_size;

	return 0;
}

static int catpt_restore_firmware(struct catpt_dev *cdev,
				  struct dma_chan *chan, dma_addr_t paddr,
				  struct catpt_fw_hdr *fw)
{
	u32 offset = sizeof(*fw);
	int i;

	print_hex_dump_debug(__func__, DUMP_PREFIX_OFFSET, 8, 4,
			     fw, sizeof(*fw), false);

	for (i = 0; i < fw->modules; i++) {
		struct catpt_fw_mod_hdr *mod;
		int ret;

		mod = (struct catpt_fw_mod_hdr *)((u8 *)fw + offset);
		if (strncmp(fw->signature, mod->signature,
			    FW_SIGNATURE_SIZE)) {
			dev_err(cdev->dev, "module signature mismatch\n");
			return -EINVAL;
		}

		if (mod->module_id > CATPT_MODID_LAST)
			return -EINVAL;

		switch (mod->module_id) {
		case CATPT_MODID_BASE_FW:
			ret = catpt_restore_basefw(cdev, chan, paddr + offset,
						   mod);
			break;
		default:
			ret = catpt_restore_module(cdev, chan, paddr + offset,
						   mod);
			break;
		}

		if (ret) {
			dev_err(cdev->dev, "restore module failed: %d\n", ret);
			return ret;
		}

		offset += sizeof(*mod) + mod->mod_size;
	}

	return 0;
}

static int catpt_load_firmware(struct catpt_dev *cdev,
			       struct dma_chan *chan, dma_addr_t paddr,
			       struct catpt_fw_hdr *fw)
{
	u32 offset = sizeof(*fw);
	int i;

	print_hex_dump_debug(__func__, DUMP_PREFIX_OFFSET, 8, 4,
			     fw, sizeof(*fw), false);

	for (i = 0; i < fw->modules; i++) {
		struct catpt_fw_mod_hdr *mod;
		int ret;

		mod = (struct catpt_fw_mod_hdr *)((u8 *)fw + offset);
		if (strncmp(fw->signature, mod->signature,
			    FW_SIGNATURE_SIZE)) {
			dev_err(cdev->dev, "module signature mismatch\n");
			return -EINVAL;
		}

		if (mod->module_id > CATPT_MODID_LAST)
			return -EINVAL;

		ret = catpt_load_module(cdev, chan, paddr + offset, mod);
		if (ret) {
			dev_err(cdev->dev, "load module failed: %d\n", ret);
			return ret;
		}

		offset += sizeof(*mod) + mod->mod_size;
	}

	return 0;
}

static int catpt_load_image(struct catpt_dev *cdev, struct dma_chan *chan,
			    const char *name, const char *signature,
			    bool restore)
{
	struct catpt_fw_hdr *fw;
	struct firmware *img;
	dma_addr_t paddr;
	void *vaddr;
	int ret;

	ret = request_firmware((const struct firmware **)&img, name, cdev->dev);
	if (ret)
		return ret;

	fw = (struct catpt_fw_hdr *)img->data;
	if (strncmp(fw->signature, signature, FW_SIGNATURE_SIZE)) {
		dev_err(cdev->dev, "firmware signature mismatch\n");
		ret = -EINVAL;
		goto release_fw;
	}

	vaddr = dma_alloc_coherent(cdev->dev, img->size, &paddr, GFP_KERNEL);
	if (!vaddr) {
		ret = -ENOMEM;
		goto release_fw;
	}

	memcpy(vaddr, img->data, img->size);
	fw = (struct catpt_fw_hdr *)vaddr;
	if (restore)
		ret = catpt_restore_firmware(cdev, chan, paddr, fw);
	else
		ret = catpt_load_firmware(cdev, chan, paddr, fw);

	dma_free_coherent(cdev->dev, img->size, vaddr, paddr);
release_fw:
	release_firmware(img);
	return ret;
}

static int catpt_load_images(struct catpt_dev *cdev, bool restore)
{
	static const char *const names[] = {
		"intel/IntcSST1.bin",
		"intel/IntcSST2.bin",
	};
	struct dma_chan *chan;
	int ret;

	chan = catpt_dma_request_config_chan(cdev);
	if (IS_ERR(chan))
		return PTR_ERR(chan);

	ret = catpt_load_image(cdev, chan, names[cdev->spec->core_id - 1],
			       FW_SIGNATURE, restore);
	if (ret)
		goto release_dma_chan;

	if (!restore)
		goto release_dma_chan;
	ret = catpt_restore_streams_context(cdev, chan);
	if (ret)
		dev_err(cdev->dev, "restore streams ctx failed: %d\n", ret);
release_dma_chan:
	dma_release_channel(chan);
	return ret;
}

int catpt_boot_firmware(struct catpt_dev *cdev, bool restore)
{
	int ret;

	catpt_dsp_stall(cdev, true);

	ret = catpt_load_images(cdev, restore);
	if (ret) {
		dev_err(cdev->dev, "load binaries failed: %d\n", ret);
		return ret;
	}

	reinit_completion(&cdev->fw_ready);
	catpt_dsp_stall(cdev, false);

	ret = wait_for_completion_timeout(&cdev->fw_ready,
			msecs_to_jiffies(FW_READY_TIMEOUT_MS));
	if (!ret) {
		dev_err(cdev->dev, "firmware ready timeout\n");
		return -ETIMEDOUT;
	}

	/* update sram pg & clock once done booting */
	catpt_dsp_update_srampge(cdev, &cdev->dram, cdev->spec->dram_mask);
	catpt_dsp_update_srampge(cdev, &cdev->iram, cdev->spec->iram_mask);

	return catpt_dsp_update_lpclock(cdev);
}

int catpt_first_boot_firmware(struct catpt_dev *cdev)
{
	struct resource *res;
	int ret;

	ret = catpt_boot_firmware(cdev, false);
	if (ret) {
		dev_err(cdev->dev, "basefw boot failed: %d\n", ret);
		return ret;
	}

	/* restrict FW Core dump area */
	__request_region(&cdev->dram, 0, 0x200, NULL, 0);
	/* restrict entire area following BASE_FW - highest offset in DRAM */
	for (res = cdev->dram.child; res->sibling; res = res->sibling)
		;
	__request_region(&cdev->dram, res->end + 1,
			 cdev->dram.end - res->end, NULL, 0);

	ret = catpt_ipc_get_mixer_stream_info(cdev, &cdev->mixer);
	if (ret)
		return CATPT_IPC_ERROR(ret);

	ret = catpt_arm_stream_templates(cdev);
	if (ret) {
		dev_err(cdev->dev, "arm templates failed: %d\n", ret);
		return ret;
	}

	/* update dram pg for scratch and restricted regions */
	catpt_dsp_update_srampge(cdev, &cdev->dram, cdev->spec->dram_mask);

	return 0;
}
