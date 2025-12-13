// SPDX-License-Identifier: GPL-2.0+
//
// fs-amp-lib.c --- Common library for FourSemi Audio Amplifiers
//
// Copyright (C) 2016-2025 Shanghai FourSemi Semiconductor Co.,Ltd.

#include <linux/crc16.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "fs-amp-lib.h"

static int fs_get_scene_count(struct fs_amp_lib *amp_lib)
{
	const struct fs_fwm_table *table;
	int count;

	if (!amp_lib || !amp_lib->dev)
		return -EINVAL;

	table = amp_lib->table[FS_INDEX_SCENE];
	if (!table)
		return -EFAULT;

	count = table->size / sizeof(struct fs_scene_index);
	if (count < 1 || count > FS_SCENE_COUNT_MAX) {
		dev_err(amp_lib->dev, "Invalid scene count: %d\n", count);
		return -ERANGE;
	}

	return count;
}

static void fs_get_fwm_string(struct fs_amp_lib *amp_lib,
			      int offset, const char **pstr)
{
	const struct fs_fwm_table *table;

	if (!amp_lib || !amp_lib->dev || !pstr)
		return;

	table = amp_lib->table[FS_INDEX_STRING];
	if (table && offset > 0 && offset < table->size + sizeof(*table))
		*pstr = (char *)table + offset;
	else
		*pstr = NULL;
}

static void fs_get_scene_reg(struct fs_amp_lib *amp_lib,
			     int offset, struct fs_amp_scene *scene)
{
	const struct fs_fwm_table *table;

	if (!amp_lib || !amp_lib->dev || !scene)
		return;

	table = amp_lib->table[FS_INDEX_REG];
	if (table && offset > 0 && offset < table->size + sizeof(*table))
		scene->reg = (struct fs_reg_table *)((char *)table + offset);
	else
		scene->reg = NULL;
}

static void fs_get_scene_model(struct fs_amp_lib *amp_lib,
			       int offset, struct fs_amp_scene *scene)
{
	const struct fs_fwm_table *table;
	const char *ptr;

	if (!amp_lib || !amp_lib->dev || !scene)
		return;

	table = amp_lib->table[FS_INDEX_MODEL];
	ptr = (char *)table;
	if (table && offset > 0 && offset < table->size + sizeof(*table))
		scene->model = (struct fs_file_table *)(ptr + offset);
	else
		scene->model = NULL;
}

static void fs_get_scene_effect(struct fs_amp_lib *amp_lib,
				int offset, struct fs_amp_scene *scene)
{
	const struct fs_fwm_table *table;
	const char *ptr;

	if (!amp_lib || !amp_lib->dev || !scene)
		return;

	table = amp_lib->table[FS_INDEX_EFFECT];
	ptr = (char *)table;
	if (table && offset > 0 && offset < table->size + sizeof(*table))
		scene->effect = (struct fs_file_table *)(ptr + offset);
	else
		scene->effect = NULL;
}

static int fs_parse_scene_tables(struct fs_amp_lib *amp_lib)
{
	const struct fs_scene_index *scene_index;
	const struct fs_fwm_table *table;
	struct fs_amp_scene *scene;
	int idx, count;

	if (!amp_lib || !amp_lib->dev)
		return -EINVAL;

	count = fs_get_scene_count(amp_lib);
	if (count <= 0)
		return -EFAULT;

	scene = devm_kcalloc(amp_lib->dev, count, sizeof(*scene), GFP_KERNEL);
	if (!scene)
		return -ENOMEM;

	amp_lib->scene_count = count;
	amp_lib->scene = scene;

	table = amp_lib->table[FS_INDEX_SCENE];
	scene_index = (struct fs_scene_index *)table->buf;

	for (idx = 0; idx < count; idx++) {
		fs_get_fwm_string(amp_lib, scene_index->name, &scene->name);
		if (!scene->name)
			scene->name = devm_kasprintf(amp_lib->dev,
						     GFP_KERNEL, "S%d", idx);
		dev_dbg(amp_lib->dev, "scene.%d name: %s\n", idx, scene->name);
		fs_get_scene_reg(amp_lib, scene_index->reg, scene);
		fs_get_scene_model(amp_lib, scene_index->model, scene);
		fs_get_scene_effect(amp_lib, scene_index->effect, scene);
		scene++;
		scene_index++;
	}

	return 0;
}

static int fs_parse_all_tables(struct fs_amp_lib *amp_lib)
{
	const struct fs_fwm_table *table;
	const struct fs_fwm_index *index;
	const char *ptr;
	int idx, count;
	int ret;

	if (!amp_lib || !amp_lib->dev || !amp_lib->hdr)
		return -EINVAL;

	/* Parse all fwm tables */
	table = (struct fs_fwm_table *)amp_lib->hdr->params;
	index = (struct fs_fwm_index *)table->buf;
	count = table->size / sizeof(*index);

	for (idx = 0; idx < count; idx++, index++) {
		if (index->type >= FS_INDEX_MAX)
			return -ERANGE;
		ptr = (char *)table + (int)index->offset;
		amp_lib->table[index->type] = (struct fs_fwm_table *)ptr;
	}

	/* Parse all scene tables */
	ret = fs_parse_scene_tables(amp_lib);
	if (ret)
		dev_err(amp_lib->dev, "Failed to parse scene: %d\n", ret);

	return ret;
}

static int fs_verify_firmware(struct fs_amp_lib *amp_lib)
{
	const struct fs_fwm_header *hdr;
	int crcsum;

	if (!amp_lib || !amp_lib->dev || !amp_lib->hdr)
		return -EINVAL;

	hdr = amp_lib->hdr;

	/* Verify the crcsum code */
	crcsum = crc16(0x0000, (const char *)&hdr->crc_size, hdr->crc_size);
	if (crcsum != hdr->crc16) {
		dev_err(amp_lib->dev, "Failed to checksum: %x-%x\n",
			crcsum, hdr->crc16);
		return -EFAULT;
	}

	/* Verify the devid(chip_type) */
	if (amp_lib->devid != LO_U16(hdr->chip_type)) {
		dev_err(amp_lib->dev, "DEVID dismatch: %04X#%04X\n",
			amp_lib->devid, hdr->chip_type);
		return -EINVAL;
	}

	return 0;
}

static void fs_print_firmware_info(struct fs_amp_lib *amp_lib)
{
	const struct fs_fwm_header *hdr;
	const char *pro_name = NULL;
	const char *dev_name = NULL;

	if (!amp_lib || !amp_lib->dev || !amp_lib->hdr)
		return;

	hdr = amp_lib->hdr;

	fs_get_fwm_string(amp_lib, hdr->project, &pro_name);
	fs_get_fwm_string(amp_lib, hdr->device, &dev_name);

	dev_info(amp_lib->dev, "Project: %s Device: %s\n",
		 pro_name ? pro_name : "null",
		 dev_name ? dev_name : "null");

	dev_info(amp_lib->dev, "Date: %04d%02d%02d-%02d%02d\n",
		 hdr->date.year, hdr->date.month, hdr->date.day,
		 hdr->date.hour, hdr->date.minute);
}

int fs_amp_load_firmware(struct fs_amp_lib *amp_lib, const char *name)
{
	const struct firmware *cont;
	struct fs_fwm_header *hdr;
	int ret;

	if (!amp_lib || !amp_lib->dev || !name)
		return -EINVAL;

	ret = request_firmware(&cont, name, amp_lib->dev);
	if (ret) {
		dev_err(amp_lib->dev, "Failed to request %s: %d\n", name, ret);
		return ret;
	}

	dev_info(amp_lib->dev, "Loading %s - size: %zu\n", name, cont->size);

	hdr = devm_kmemdup(amp_lib->dev, cont->data, cont->size, GFP_KERNEL);
	release_firmware(cont);
	if (!hdr)
		return -ENOMEM;

	amp_lib->hdr = hdr;
	ret = fs_verify_firmware(amp_lib);
	if (ret) {
		amp_lib->hdr = NULL;
		return ret;
	}

	ret = fs_parse_all_tables(amp_lib);
	if (ret) {
		amp_lib->hdr = NULL;
		return ret;
	}

	fs_print_firmware_info(amp_lib);

	return 0;
}
EXPORT_SYMBOL_GPL(fs_amp_load_firmware);

MODULE_AUTHOR("Nick Li <nick.li@foursemi.com>");
MODULE_DESCRIPTION("FourSemi audio amplifier library");
MODULE_LICENSE("GPL");
