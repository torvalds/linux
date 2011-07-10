#ifndef __I2C_OMAP_H__
#define __I2C_OMAP_H__

#include <linux/platform_device.h>

/*
 * Version 2 of the I2C peripheral unit has a different register
 * layout and extra registers.  The ID register in the V2 peripheral
 * unit on the OMAP4430 reports the same ID as the V1 peripheral
 * unit on the OMAP3530, so we must inform the driver which IP
 * version we know it is running on from platform / cpu-specific
 * code using these constants in the hwmod class definition.
 */

#define OMAP_I2C_IP_VERSION_1 1
#define OMAP_I2C_IP_VERSION_2 2

struct omap_i2c_bus_platform_data {
	u32		clkrate;
	void		(*set_mpu_wkup_lat)(struct device *dev, long set);
	int		(*device_enable) (struct platform_device *pdev);
	int		(*device_shutdown) (struct platform_device *pdev);
	int		(*device_idle) (struct platform_device *pdev);
};

#endif
