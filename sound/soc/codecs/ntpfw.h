/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * ntpfw.h - Firmware helper functions for Neofidelity codecs
 *
 * Copyright (c) 2024, SaluteDevices. All Rights Reserved.
 */

#ifndef __NTPFW_H__
#define __NTPFW_H__
#include <linux/i2c.h>
#include <linux/firmware.h>

/**
 * ntpfw_load - load firmware to amplifier over i2c interface.
 *
 * @i2c		Pointer to amplifier's I2C client.
 * @name	Firmware file name.
 * @magic	Magic number to validate firmware.
 * @return	0 or error code upon error.
 */
int ntpfw_load(struct i2c_client *i2c, const char *name, const u32 magic);

#endif /* __NTPFW_H__ */
