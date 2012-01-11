/*
 * Load firmware files from Analog Devices SigmaStudio
 *
 * Copyright 2009-2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef __SIGMA_FIRMWARE_H__
#define __SIGMA_FIRMWARE_H__

#include <linux/firmware.h>
#include <linux/types.h>

struct i2c_client;

#define SIGMA_MAGIC "ADISIGM"

struct sigma_firmware {
	const struct firmware *fw;
	size_t pos;
};

struct sigma_firmware_header {
	unsigned char magic[7];
	u8 version;
	__le32 crc;
};

enum {
	SIGMA_ACTION_WRITEXBYTES = 0,
	SIGMA_ACTION_WRITESINGLE,
	SIGMA_ACTION_WRITESAFELOAD,
	SIGMA_ACTION_DELAY,
	SIGMA_ACTION_PLLWAIT,
	SIGMA_ACTION_NOOP,
	SIGMA_ACTION_END,
};

struct sigma_action {
	u8 instr;
	u8 len_hi;
	__le16 len;
	__be16 addr;
	unsigned char payload[];
};

static inline u32 sigma_action_len(struct sigma_action *sa)
{
	return (sa->len_hi << 16) | le16_to_cpu(sa->len);
}

extern int process_sigma_firmware(struct i2c_client *client, const char *name);

#endif
