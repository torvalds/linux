/*
 * STMicroelectronics lis2ds12 driver
 *
 * Copyright 2015 STMicroelectronics Inc.
 *
 * Giuseppe Barba <giuseppe.barba@st.com>
 *
 * Licensed under the GPL-2.
 */


#ifndef __LIS2DS12_H__
#define __LIS2DS12_H__

#define LIS2DS12_DEV_NAME			"lis2ds12"
#define LIS2DS12_I2C_ADDR			0x1e

struct lis2ds12_platform_data {
	u8 drdy_int_pin;
};

#endif /* __LIS2DS12_H__ */
