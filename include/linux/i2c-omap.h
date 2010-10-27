#ifndef __I2C_OMAP_H__
#define __I2C_OMAP_H__

struct omap_i2c_bus_platform_data {
	u32		clkrate;
	void		(*set_mpu_wkup_lat)(struct device *dev, long set);
};

#endif
