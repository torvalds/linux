// SPDX-License-Identifier: GPL-2.0-only
/*
 * ntpfw.c - Firmware helper functions for Neofidelity codecs
 *
 * Copyright (c) 2024, SaluteDevices. All Rights Reserved.
 */

#include <linux/i2c.h>
#include <linux/firmware.h>
#include <linux/module.h>

#include "ntpfw.h"

struct ntpfw_chunk {
	__be16 length;
	u8 step;
	u8 data[];
} __packed;

struct ntpfw_header {
	__be32 magic;
} __packed;

static bool ntpfw_verify(struct device *dev, const u8 *buf, size_t buf_size, u32 magic)
{
	const struct ntpfw_header *header = (struct ntpfw_header *)buf;
	u32 buf_magic;

	if (buf_size <= sizeof(*header)) {
		dev_err(dev, "Failed to load firmware: image too small\n");
		return false;
	}

	buf_magic = be32_to_cpu(header->magic);
	if (buf_magic != magic) {
		dev_err(dev, "Failed to load firmware: invalid magic 0x%x:\n", buf_magic);
		return false;
	}

	return true;
}

static bool ntpfw_verify_chunk(struct device *dev, const struct ntpfw_chunk *chunk, size_t buf_size)
{
	size_t chunk_size;

	if (buf_size <= sizeof(*chunk)) {
		dev_err(dev, "Failed to load firmware: chunk size too big\n");
		return false;
	}

	if (chunk->step != 2 && chunk->step != 5) {
		dev_err(dev, "Failed to load firmware: invalid chunk step: %d\n", chunk->step);
		return false;
	}

	chunk_size = be16_to_cpu(chunk->length);
	if (chunk_size > buf_size) {
		dev_err(dev, "Failed to load firmware: invalid chunk length\n");
		return false;
	}

	if (chunk_size % chunk->step) {
		dev_err(dev, "Failed to load firmware: chunk length and step mismatch\n");
		return false;
	}

	return true;
}

static int ntpfw_send_chunk(struct i2c_client *i2c, const struct ntpfw_chunk *chunk)
{
	int ret;
	size_t i;
	size_t length = be16_to_cpu(chunk->length);

	for (i = 0; i < length; i += chunk->step) {
		ret = i2c_master_send(i2c, &chunk->data[i], chunk->step);
		if (ret != chunk->step) {
			dev_err(&i2c->dev, "I2C send failed: %d\n", ret);
			return ret < 0 ? ret : -EIO;
		}
	}

	return 0;
}

int ntpfw_load(struct i2c_client *i2c, const char *name, u32 magic)
{
	struct device *dev = &i2c->dev;
	const struct ntpfw_chunk *chunk;
	const struct firmware *fw;
	const u8 *data;
	size_t leftover;
	int ret;

	ret = request_firmware(&fw, name, dev);
	if (ret) {
		dev_warn(dev, "request_firmware '%s' failed with %d\n",
			 name, ret);
		return ret;
	}

	if (!ntpfw_verify(dev, fw->data, fw->size, magic)) {
		ret = -EINVAL;
		goto done;
	}

	data = fw->data + sizeof(struct ntpfw_header);
	leftover = fw->size - sizeof(struct ntpfw_header);

	while (leftover) {
		chunk = (struct ntpfw_chunk *)data;

		if (!ntpfw_verify_chunk(dev, chunk, leftover)) {
			ret = -EINVAL;
			goto done;
		}

		ret = ntpfw_send_chunk(i2c, chunk);
		if (ret)
			goto done;

		data += be16_to_cpu(chunk->length) + sizeof(*chunk);
		leftover -= be16_to_cpu(chunk->length) + sizeof(*chunk);
	}

done:
	release_firmware(fw);

	return ret;
}
EXPORT_SYMBOL_GPL(ntpfw_load);

MODULE_AUTHOR("Igor Prusov <ivprusov@salutedevices.com>");
MODULE_DESCRIPTION("Helper for loading Neofidelity amplifiers firmware");
MODULE_LICENSE("GPL");
