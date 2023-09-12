/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __MEDIA_I2C_DS90UB9XX_H__
#define __MEDIA_I2C_DS90UB9XX_H__

#include <linux/types.h>

struct i2c_atr;

/**
 * struct ds90ub9xx_platform_data - platform data for FPD-Link Serializers.
 * @port: Deserializer RX port for this Serializer
 * @atr: I2C ATR
 * @bc_rate: back-channel clock rate
 */
struct ds90ub9xx_platform_data {
	u32 port;
	struct i2c_atr *atr;
	unsigned long bc_rate;
};

#endif /* __MEDIA_I2C_DS90UB9XX_H__ */
