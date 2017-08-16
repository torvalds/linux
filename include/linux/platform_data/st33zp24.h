/*
 * STMicroelectronics TPM Linux driver for TPM 1.2 ST33ZP24
 * Copyright (C) 2009 - 2016  STMicroelectronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ST33ZP24_H__
#define __ST33ZP24_H__

#define TPM_ST33_I2C			"st33zp24-i2c"
#define TPM_ST33_SPI			"st33zp24-spi"

struct st33zp24_platform_data {
	int io_lpcpd;
};

#endif /* __ST33ZP24_H__ */
