#ifndef __I2C_SH_MOBILE_H__
#define __I2C_SH_MOBILE_H__

#include <linux/platform_device.h>

struct i2c_sh_mobile_platform_data {
	unsigned long bus_speed;
	unsigned int clks_per_count;
};

#endif /* __I2C_SH_MOBILE_H__ */
