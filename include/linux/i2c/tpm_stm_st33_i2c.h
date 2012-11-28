/*
* STMicroelectronics TPM I2C Linux driver for TPM ST33ZP24
* Copyright (C) 2009, 2010 STMicroelectronics
* Christophe RICARD tpmsupport@st.com
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
* You should have received a copy of the GNU General Public License along
* with this program; if not, write to the Free Software Foundation, Inc.,
* 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*
* STMicroelectronics version 1.2.0, Copyright (C) 2010
* STMicroelectronics comes with ABSOLUTELY NO WARRANTY.
* This is free software, and you are welcome to redistribute it
* under certain conditions.
*
* @File: stm_st33_tpm_i2c.h
*
* @Date: 06/15/2008
*/
#ifndef __STM_ST33_TPM_I2C_H__
#define __STM_ST33_TPM_I2C_H__

#include <linux/i2c.h>

#define TPM_ST33_I2C			"st33zp24_i2c"
#define TPM_I2C_ST33_ADDR_WR_LOC0	(0x26 >> 1)


struct st33zp24_platform_data {
	int io_serirq;
	int io_lpcpd;
	struct i2c_client *client;
	bool bChipF;
	u8 *tpm_i2c_buffer[2]; /* 0 Request 1 Response */
	struct completion irq_detection;
	struct mutex lock;
};

#endif /* __STM_ST33_TPM_I2C_H__ */
