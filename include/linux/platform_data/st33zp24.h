/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * STMicroelectronics TPM Linux driver for TPM 1.2 ST33ZP24
 * Copyright (C) 2009 - 2016  STMicroelectronics
 */
#ifndef __ST33ZP24_H__
#define __ST33ZP24_H__

#define TPM_ST33_I2C			"st33zp24-i2c"
#define TPM_ST33_SPI			"st33zp24-spi"

struct st33zp24_platform_data {
	int io_lpcpd;
};

#endif /* __ST33ZP24_H__ */
