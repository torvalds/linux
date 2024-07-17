// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2022 Intel Corporation
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <linux/firmware.h>
#include <linux/kfifo.h>
#include <linux/slab.h>
#include "avs.h"
#include "messages.h"

/* Caller responsible for holding adev->modres_mutex. */
static int avs_module_entry_index(struct avs_dev *adev, const guid_t *uuid)
{
	int i;

	for (i = 0; i < adev->mods_info->count; i++) {
		struct avs_module_entry *module;

		module = &adev->mods_info->entries[i];
		if (guid_equal(&module->uuid, uuid))
			return i;
	}

	return -ENOENT;
}

/* Caller responsible for holding adev->modres_mutex. */
static int avs_module_id_entry_index(struct avs_dev *adev, u32 module_id)
{
	int i;

	for (i = 0; i < adev->mods_info->count; i++) {
		struct avs_module_entry *module;

		module = &adev->mods_info->entries[i];
		if (module->module_id == module_id)
			return i;
	}

	return -ENOENT;
}

int avs_get_module_entry(struct avs_dev *adev, const guid_t *uuid, struct avs_module_entry *entry)
{
	int idx;

	mutex_lock(&adev->modres_mutex);

	idx = avs_module_entry_index(adev, uuid);
	if (idx >= 0)
		memcpy(entry, &adev->mods_info->entries[idx], sizeof(*entry));

	mutex_unlock(&adev->modres_mutex);
	return (idx < 0) ? idx : 0;
}

int avs_get_module_id_entry(struct avs_dev *adev, u32 module_id, struct avs_module_entry *entry)
{
	int idx;

	mutex_lock(&adev->modres_mutex);

	idx = avs_module_id_entry_index(adev, module_id);
	if (idx >= 0)
		memcpy(entry, &adev->mods_info->entries[idx], sizeof(*entry));

	mutex_unlock(&adev->modres_mutex);
	return (idx < 0) ? idx : 0;
}

int avs_get_module_id(struct avs_dev *adev, const guid_t *uuid)
{
	struct avs_module_entry module;
	int ret;

	ret = avs_get_module_entry(adev, uuid, &module);
	return !ret ? module.module_id : -ENOENT;
}

bool avs_is_module_ida_empty(struct avs_dev *adev, u32 module_id)
{
	bool ret = false;
	int idx;

	mutex_lock(&adev->modres_mutex);

	idx = avs_module_id_entry_index(adev, module_id);
	if (idx >= 0)
		ret = ida_is_empty(adev->mod_idas[idx]);

	mutex_unlock(&adev->modres_mutex);
	return ret;
}

/* Caller responsible for holding adev->modres_mutex. */
static void avs_module_ida_destroy(struct avs_dev *adev)
{
	int i = adev->mods_info ? adev->mods_info->count : 0;

	while (i--) {
		ida_destroy(adev->mod_idas[i]);
		kfree(adev->mod_idas[i]);
	}
	kfree(adev->mod_idas);
}

/* Caller responsible for holding adev->modres_mutex. */
static int
avs_module_ida_alloc(struct avs_dev *adev, struct avs_mods_info *newinfo, bool purge)
{
	struct avs_mods_info *oldinfo = adev->mods_info;
	struct ida **ida_ptrs;
	u32 tocopy_count = 0;
	int i;

	if (!purge && oldinfo) {
		if (oldinfo->count >= newinfo->count)
			dev_warn(adev->dev, "refreshing %d modules info with %d\n",
				 oldinfo->count, newinfo->count);
		tocopy_count = oldinfo->count;
	}

	ida_ptrs = kcalloc(newinfo->count, sizeof(*ida_ptrs), GFP_KERNEL);
	if (!ida_ptrs)
		return -ENOMEM;

	if (tocopy_count)
		memcpy(ida_ptrs, adev->mod_idas, tocopy_count * sizeof(*ida_ptrs));

	for (i = tocopy_count; i < newinfo->count; i++) {
		ida_ptrs[i] = kzalloc(sizeof(**ida_ptrs), GFP_KERNEL);
		if (!ida_ptrs[i]) {
			while (i--)
				kfree(ida_ptrs[i]);

			kfree(ida_ptrs);
			return -ENOMEM;
		}

		ida_init(ida_ptrs[i]);
	}

	/* If old elements have been reused, don't wipe them. */
	if (tocopy_count)
		kfree(adev->mod_idas);
	else
		avs_module_ida_destroy(adev);

	adev->mod_idas = ida_ptrs;
	return 0;
}

int avs_module_info_init(struct avs_dev *adev, bool purge)
{
	struct avs_mods_info *info;
	int ret;

	ret = avs_ipc_get_modules_info(adev, &info);
	if (ret)
		return AVS_IPC_RET(ret);

	mutex_lock(&adev->modres_mutex);

	ret = avs_module_ida_alloc(adev, info, purge);
	if (ret < 0) {
		dev_err(adev->dev, "initialize module idas failed: %d\n", ret);
		goto exit;
	}

	/* Refresh current information with newly received table. */
	kfree(adev->mods_info);
	adev->mods_info = info;

exit:
	mutex_unlock(&adev->modres_mutex);
	return ret;
}

void avs_module_info_free(struct avs_dev *adev)
{
	mutex_lock(&adev->modres_mutex);

	avs_module_ida_destroy(adev);
	kfree(adev->mods_info);
	adev->mods_info = NULL;

	mutex_unlock(&adev->modres_mutex);
}

int avs_module_id_alloc(struct avs_dev *adev, u16 module_id)
{
	int ret, idx, max_id;

	mutex_lock(&adev->modres_mutex);

	idx = avs_module_id_entry_index(adev, module_id);
	if (idx == -ENOENT) {
		dev_err(adev->dev, "invalid module id: %d", module_id);
		ret = -EINVAL;
		goto exit;
	}
	max_id = adev->mods_info->entries[idx].instance_max_count - 1;
	ret = ida_alloc_max(adev->mod_idas[idx], max_id, GFP_KERNEL);
exit:
	mutex_unlock(&adev->modres_mutex);
	return ret;
}

void avs_module_id_free(struct avs_dev *adev, u16 module_id, u8 instance_id)
{
	int idx;

	mutex_lock(&adev->modres_mutex);

	idx = avs_module_id_entry_index(adev, module_id);
	if (idx == -ENOENT) {
		dev_err(adev->dev, "invalid module id: %d", module_id);
		goto exit;
	}

	ida_free(adev->mod_idas[idx], instance_id);
exit:
	mutex_unlock(&adev->modres_mutex);
}

/*
 * Once driver loads FW it should keep it in memory, so we are not affected
 * by FW removal from filesystem or even worse by loading different FW at
 * runtime suspend/resume.
 */
int avs_request_firmware(struct avs_dev *adev, const struct firmware **fw_p, const char *name)
{
	struct avs_fw_entry *entry;
	int ret;

	/* first check in list if it is not already loaded */
	list_for_each_entry(entry, &adev->fw_list, node) {
		if (!strcmp(name, entry->name)) {
			*fw_p = entry->fw;
			return 0;
		}
	}

	/* FW is not loaded, let's load it now and add to the list */
	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	entry->name = kstrdup_const(name, GFP_KERNEL);
	if (!entry->name) {
		kfree(entry);
		return -ENOMEM;
	}

	ret = request_firmware(&entry->fw, name, adev->dev);
	if (ret < 0) {
		kfree_const(entry->name);
		kfree(entry);
		return ret;
	}

	*fw_p = entry->fw;

	list_add_tail(&entry->node, &adev->fw_list);

	return 0;
}

/*
 * Release single FW entry, used to handle errors in functions calling
 * avs_request_firmware()
 */
void avs_release_last_firmware(struct avs_dev *adev)
{
	struct avs_fw_entry *entry;

	entry = list_last_entry(&adev->fw_list, typeof(*entry), node);

	list_del(&entry->node);
	release_firmware(entry->fw);
	kfree_const(entry->name);
	kfree(entry);
}

/*
 * Release all FW entries, used on driver removal
 */
void avs_release_firmwares(struct avs_dev *adev)
{
	struct avs_fw_entry *entry, *tmp;

	list_for_each_entry_safe(entry, tmp, &adev->fw_list, node) {
		list_del(&entry->node);
		release_firmware(entry->fw);
		kfree_const(entry->name);
		kfree(entry);
	}
}
