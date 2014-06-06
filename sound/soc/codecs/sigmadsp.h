/*
 * Load firmware files from Analog Devices SigmaStudio
 *
 * Copyright 2009-2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef __SIGMA_FIRMWARE_H__
#define __SIGMA_FIRMWARE_H__

#include <linux/device.h>
#include <linux/regmap.h>

struct sigma_action {
	u8 instr;
	u8 len_hi;
	__le16 len;
	__be16 addr;
	unsigned char payload[];
} __packed;

struct sigma_firmware {
	const struct firmware *fw;
	size_t pos;

	void *control_data;
	int (*write)(void *control_data, const struct sigma_action *sa,
			size_t len);
};

int _process_sigma_firmware(struct device *dev,
	struct sigma_firmware *ssfw, const char *name);

struct i2c_client;

extern int process_sigma_firmware(struct i2c_client *client, const char *name);
extern int process_sigma_firmware_regmap(struct device *dev,
		struct regmap *regmap, const char *name);

#endif
