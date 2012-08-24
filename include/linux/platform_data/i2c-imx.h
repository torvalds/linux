/*
 * i2c.h - i.MX I2C driver header file
 *
 * Copyright (c) 2008, Darius Augulis <augulis.darius@gmail.com>
 *
 * This file is released under the GPLv2
 */

#ifndef __ASM_ARCH_I2C_H_
#define __ASM_ARCH_I2C_H_

/**
 * struct imxi2c_platform_data - structure of platform data for MXC I2C driver
 * @bitrate:	Bus speed measured in Hz
 *
 **/
struct imxi2c_platform_data {
	u32 bitrate;
};

#endif /* __ASM_ARCH_I2C_H_ */
