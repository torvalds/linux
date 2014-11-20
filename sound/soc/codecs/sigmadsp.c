/*
 * Load Analog Devices SigmaStudio firmware files
 *
 * Copyright 2009-2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/crc32.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/module.h>

#include "sigmadsp.h"

#define SIGMA_MAGIC "ADISIGM"

struct sigma_firmware_header {
	unsigned char magic[7];
	u8 version;
	__le32 crc;
} __packed;

enum {
	SIGMA_ACTION_WRITEXBYTES = 0,
	SIGMA_ACTION_WRITESINGLE,
	SIGMA_ACTION_WRITESAFELOAD,
	SIGMA_ACTION_DELAY,
	SIGMA_ACTION_PLLWAIT,
	SIGMA_ACTION_NOOP,
	SIGMA_ACTION_END,
};

static inline u32 sigma_action_len(struct sigma_action *sa)
{
	return (sa->len_hi << 16) | le16_to_cpu(sa->len);
}

static size_t sigma_action_size(struct sigma_action *sa)
{
	size_t payload = 0;

	switch (sa->instr) {
	case SIGMA_ACTION_WRITEXBYTES:
	case SIGMA_ACTION_WRITESINGLE:
	case SIGMA_ACTION_WRITESAFELOAD:
		payload = sigma_action_len(sa);
		break;
	default:
		break;
	}

	payload = ALIGN(payload, 2);

	return payload + sizeof(struct sigma_action);
}

/*
 * Returns a negative error value in case of an error, 0 if processing of
 * the firmware should be stopped after this action, 1 otherwise.
 */
static int
process_sigma_action(struct sigma_firmware *ssfw, struct sigma_action *sa)
{
	size_t len = sigma_action_len(sa);
	int ret;

	pr_debug("%s: instr:%i addr:%#x len:%zu\n", __func__,
		sa->instr, sa->addr, len);

	switch (sa->instr) {
	case SIGMA_ACTION_WRITEXBYTES:
	case SIGMA_ACTION_WRITESINGLE:
	case SIGMA_ACTION_WRITESAFELOAD:
		ret = ssfw->write(ssfw->control_data, sa, len);
		if (ret < 0)
			return -EINVAL;
		break;
	case SIGMA_ACTION_DELAY:
		udelay(len);
		len = 0;
		break;
	case SIGMA_ACTION_END:
		return 0;
	default:
		return -EINVAL;
	}

	return 1;
}

static int
process_sigma_actions(struct sigma_firmware *ssfw)
{
	struct sigma_action *sa;
	size_t size;
	int ret;

	while (ssfw->pos + sizeof(*sa) <= ssfw->fw->size) {
		sa = (struct sigma_action *)(ssfw->fw->data + ssfw->pos);

		size = sigma_action_size(sa);
		ssfw->pos += size;
		if (ssfw->pos > ssfw->fw->size || size == 0)
			break;

		ret = process_sigma_action(ssfw, sa);

		pr_debug("%s: action returned %i\n", __func__, ret);

		if (ret <= 0)
			return ret;
	}

	if (ssfw->pos != ssfw->fw->size)
		return -EINVAL;

	return 0;
}

int _process_sigma_firmware(struct device *dev,
	struct sigma_firmware *ssfw, const char *name)
{
	int ret;
	struct sigma_firmware_header *ssfw_head;
	const struct firmware *fw;
	u32 crc;

	pr_debug("%s: loading firmware %s\n", __func__, name);

	/* first load the blob */
	ret = request_firmware(&fw, name, dev);
	if (ret) {
		pr_debug("%s: request_firmware() failed with %i\n", __func__, ret);
		return ret;
	}
	ssfw->fw = fw;

	/* then verify the header */
	ret = -EINVAL;

	/*
	 * Reject too small or unreasonable large files. The upper limit has been
	 * chosen a bit arbitrarily, but it should be enough for all practical
	 * purposes and having the limit makes it easier to avoid integer
	 * overflows later in the loading process.
	 */
	if (fw->size < sizeof(*ssfw_head) || fw->size >= 0x4000000) {
		dev_err(dev, "Failed to load firmware: Invalid size\n");
		goto done;
	}

	ssfw_head = (void *)fw->data;
	if (memcmp(ssfw_head->magic, SIGMA_MAGIC, ARRAY_SIZE(ssfw_head->magic))) {
		dev_err(dev, "Failed to load firmware: Invalid magic\n");
		goto done;
	}

	crc = crc32(0, fw->data + sizeof(*ssfw_head),
			fw->size - sizeof(*ssfw_head));
	pr_debug("%s: crc=%x\n", __func__, crc);
	if (crc != le32_to_cpu(ssfw_head->crc)) {
		dev_err(dev, "Failed to load firmware: Wrong crc checksum: expected %x got %x\n",
			le32_to_cpu(ssfw_head->crc), crc);
		goto done;
	}

	ssfw->pos = sizeof(*ssfw_head);

	/* finally process all of the actions */
	ret = process_sigma_actions(ssfw);

 done:
	release_firmware(fw);

	pr_debug("%s: loaded %s\n", __func__, name);

	return ret;
}
EXPORT_SYMBOL_GPL(_process_sigma_firmware);

MODULE_LICENSE("GPL");
