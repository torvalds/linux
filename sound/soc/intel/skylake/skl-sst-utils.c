/*
 *  skl-sst-utils.c - SKL sst utils functions
 *
 *  Copyright (C) 2016 Intel Corp
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/uuid.h>
#include "skl-sst-dsp.h"
#include "../common/sst-dsp.h"
#include "../common/sst-dsp-priv.h"
#include "skl-sst-ipc.h"


#define UUID_STR_SIZE 37
#define DEFAULT_HASH_SHA256_LEN 32

/* FW Extended Manifest Header id = $AE1 */
#define SKL_EXT_MANIFEST_HEADER_MAGIC   0x31454124

struct UUID {
	u8 id[16];
};

union seg_flags {
	u32 ul;
	struct {
		u32 contents : 1;
		u32 alloc    : 1;
		u32 load     : 1;
		u32 read_only : 1;
		u32 code     : 1;
		u32 data     : 1;
		u32 _rsvd0   : 2;
		u32 type     : 4;
		u32 _rsvd1   : 4;
		u32 length   : 16;
	} r;
} __packed;

struct segment_desc {
	union seg_flags flags;
	u32 v_base_addr;
	u32 file_offset;
};

struct module_type {
	u32 load_type  : 4;
	u32 auto_start : 1;
	u32 domain_ll  : 1;
	u32 domain_dp  : 1;
	u32 rsvd       : 25;
} __packed;

struct adsp_module_entry {
	u32 struct_id;
	u8  name[8];
	struct UUID uuid;
	struct module_type type;
	u8  hash1[DEFAULT_HASH_SHA256_LEN];
	u32 entry_point;
	u16 cfg_offset;
	u16 cfg_count;
	u32 affinity_mask;
	u16 instance_max_count;
	u16 instance_bss_size;
	struct segment_desc segments[3];
} __packed;

struct adsp_fw_hdr {
	u32 id;
	u32 len;
	u8  name[8];
	u32 preload_page_count;
	u32 fw_image_flags;
	u32 feature_mask;
	u16 major;
	u16 minor;
	u16 hotfix;
	u16 build;
	u32 num_modules;
	u32 hw_buf_base;
	u32 hw_buf_length;
	u32 load_offset;
} __packed;

#define MAX_INSTANCE_BUFF 2

struct uuid_module {
	uuid_le uuid;
	int id;
	int is_loadable;
	int max_instance;
	u64 pvt_id[MAX_INSTANCE_BUFF];
	int *instance_id;

	struct list_head list;
};

struct skl_ext_manifest_hdr {
	u32 id;
	u32 len;
	u16 version_major;
	u16 version_minor;
	u32 entries;
};

int snd_skl_get_module_info(struct skl_sst *ctx,
				struct skl_module_cfg *mconfig)
{
	struct uuid_module *module;
	uuid_le *uuid_mod;

	uuid_mod = (uuid_le *)mconfig->guid;

	if (list_empty(&ctx->uuid_list)) {
		dev_err(ctx->dev, "Module list is empty\n");
		return -EINVAL;
	}

	list_for_each_entry(module, &ctx->uuid_list, list) {
		if (uuid_le_cmp(*uuid_mod, module->uuid) == 0) {
			mconfig->id.module_id = module->id;
			mconfig->is_loadable = module->is_loadable;

			return 0;
		}
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(snd_skl_get_module_info);

static int skl_get_pvtid_map(struct uuid_module *module, int instance_id)
{
	int pvt_id;

	for (pvt_id = 0; pvt_id < module->max_instance; pvt_id++) {
		if (module->instance_id[pvt_id] == instance_id)
			return pvt_id;
	}
	return -EINVAL;
}

int skl_get_pvt_instance_id_map(struct skl_sst *ctx,
				int module_id, int instance_id)
{
	struct uuid_module *module;

	list_for_each_entry(module, &ctx->uuid_list, list) {
		if (module->id == module_id)
			return skl_get_pvtid_map(module, instance_id);
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(skl_get_pvt_instance_id_map);

static inline int skl_getid_32(struct uuid_module *module, u64 *val,
				int word1_mask, int word2_mask)
{
	int index, max_inst, pvt_id;
	u32 mask_val;

	max_inst =  module->max_instance;
	mask_val = (u32)(*val >> word1_mask);

	if (mask_val != 0xffffffff) {
		index = ffz(mask_val);
		pvt_id = index + word1_mask + word2_mask;
		if (pvt_id <= (max_inst - 1)) {
			*val |= 1ULL << (index + word1_mask);
			return pvt_id;
		}
	}

	return -EINVAL;
}

static inline int skl_pvtid_128(struct uuid_module *module)
{
	int j, i, word1_mask, word2_mask = 0, pvt_id;

	for (j = 0; j < MAX_INSTANCE_BUFF; j++) {
		word1_mask = 0;

		for (i = 0; i < 2; i++) {
			pvt_id = skl_getid_32(module, &module->pvt_id[j],
						word1_mask, word2_mask);
			if (pvt_id >= 0)
				return pvt_id;

			word1_mask += 32;
			if ((word1_mask + word2_mask) >= module->max_instance)
				return -EINVAL;
		}

		word2_mask += 64;
		if (word2_mask >= module->max_instance)
			return -EINVAL;
	}

	return -EINVAL;
}

/**
 * skl_get_pvt_id: generate a private id for use as module id
 *
 * @ctx: driver context
 * @mconfig: module configuration data
 *
 * This generates a 128 bit private unique id for a module TYPE so that
 * module instance is unique
 */
int skl_get_pvt_id(struct skl_sst *ctx, struct skl_module_cfg *mconfig)
{
	struct uuid_module *module;
	uuid_le *uuid_mod;
	int pvt_id;

	uuid_mod = (uuid_le *)mconfig->guid;

	list_for_each_entry(module, &ctx->uuid_list, list) {
		if (uuid_le_cmp(*uuid_mod, module->uuid) == 0) {

			pvt_id = skl_pvtid_128(module);
			if (pvt_id >= 0) {
				module->instance_id[pvt_id] =
						mconfig->id.instance_id;
				return pvt_id;
			}
		}
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(skl_get_pvt_id);

/**
 * skl_put_pvt_id: free up the private id allocated
 *
 * @ctx: driver context
 * @mconfig: module configuration data
 *
 * This frees a 128 bit private unique id previously generated
 */
int skl_put_pvt_id(struct skl_sst *ctx, struct skl_module_cfg *mconfig)
{
	int i;
	uuid_le *uuid_mod;
	struct uuid_module *module;

	uuid_mod = (uuid_le *)mconfig->guid;
	list_for_each_entry(module, &ctx->uuid_list, list) {
		if (uuid_le_cmp(*uuid_mod, module->uuid) == 0) {

			if (mconfig->id.pvt_id != 0)
				i = (mconfig->id.pvt_id) / 64;
			else
				i = 0;

			module->pvt_id[i] &= ~(1 << (mconfig->id.pvt_id));
			mconfig->id.pvt_id = -1;
			return 0;
		}
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(skl_put_pvt_id);

/*
 * Parse the firmware binary to get the UUID, module id
 * and loadable flags
 */
int snd_skl_parse_uuids(struct sst_dsp *ctx, const struct firmware *fw,
			unsigned int offset, int index)
{
	struct adsp_fw_hdr *adsp_hdr;
	struct adsp_module_entry *mod_entry;
	int i, num_entry, size;
	uuid_le *uuid_bin;
	const char *buf;
	struct skl_sst *skl = ctx->thread_context;
	struct uuid_module *module;
	struct firmware stripped_fw;
	unsigned int safe_file;

	/* Get the FW pointer to derive ADSP header */
	stripped_fw.data = fw->data;
	stripped_fw.size = fw->size;

	skl_dsp_strip_extended_manifest(&stripped_fw);

	buf = stripped_fw.data;

	/* check if we have enough space in file to move to header */
	safe_file = sizeof(*adsp_hdr) + offset;
	if (stripped_fw.size <= safe_file) {
		dev_err(ctx->dev, "Small fw file size, No space for hdr\n");
		return -EINVAL;
	}

	adsp_hdr = (struct adsp_fw_hdr *)(buf + offset);

	/* check 1st module entry is in file */
	safe_file += adsp_hdr->len + sizeof(*mod_entry);
	if (stripped_fw.size <= safe_file) {
		dev_err(ctx->dev, "Small fw file size, No module entry\n");
		return -EINVAL;
	}

	mod_entry = (struct adsp_module_entry *)
		(buf + offset + adsp_hdr->len);

	num_entry = adsp_hdr->num_modules;

	/* check all entries are in file */
	safe_file += num_entry * sizeof(*mod_entry);
	if (stripped_fw.size <= safe_file) {
		dev_err(ctx->dev, "Small fw file size, No modules\n");
		return -EINVAL;
	}


	/*
	 * Read the UUID(GUID) from FW Manifest.
	 *
	 * The 16 byte UUID format is: XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXX
	 * Populate the UUID table to store module_id and loadable flags
	 * for the module.
	 */

	for (i = 0; i < num_entry; i++, mod_entry++) {
		module = kzalloc(sizeof(*module), GFP_KERNEL);
		if (!module)
			return -ENOMEM;

		uuid_bin = (uuid_le *)mod_entry->uuid.id;
		memcpy(&module->uuid, uuid_bin, sizeof(module->uuid));

		module->id = (i | (index << 12));
		module->is_loadable = mod_entry->type.load_type;
		module->max_instance = mod_entry->instance_max_count;
		size = sizeof(int) * mod_entry->instance_max_count;
		module->instance_id = devm_kzalloc(ctx->dev, size, GFP_KERNEL);
		if (!module->instance_id) {
			kfree(module);
			return -ENOMEM;
		}

		list_add_tail(&module->list, &skl->uuid_list);

		dev_dbg(ctx->dev,
			"Adding uuid :%pUL   mod id: %d  Loadable: %d\n",
			&module->uuid, module->id, module->is_loadable);
	}

	return 0;
}

void skl_freeup_uuid_list(struct skl_sst *ctx)
{
	struct uuid_module *uuid, *_uuid;

	list_for_each_entry_safe(uuid, _uuid, &ctx->uuid_list, list) {
		list_del(&uuid->list);
		kfree(uuid);
	}
}

/*
 * some firmware binary contains some extended manifest. This needs
 * to be stripped in that case before we load and use that image.
 *
 * Get the module id for the module by checking
 * the table for the UUID for the module
 */
int skl_dsp_strip_extended_manifest(struct firmware *fw)
{
	struct skl_ext_manifest_hdr *hdr;

	/* check if fw file is greater than header we are looking */
	if (fw->size < sizeof(hdr)) {
		pr_err("%s: Firmware file small, no hdr\n", __func__);
		return -EINVAL;
	}

	hdr = (struct skl_ext_manifest_hdr *)fw->data;

	if (hdr->id == SKL_EXT_MANIFEST_HEADER_MAGIC) {
		fw->size -= hdr->len;
		fw->data += hdr->len;
	}

	return 0;
}
