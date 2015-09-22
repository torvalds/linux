#ifndef __LINUX_BQ27X00_BATTERY_H__
#define __LINUX_BQ27X00_BATTERY_H__

/**
 * struct bq27xxx_plaform_data - Platform data for bq27xxx devices
 * @name: Name of the battery.
 * @chip: Chip class number of this device.
 * @read: HDQ read callback.
 *	This function should provide access to the HDQ bus the battery is
 *	connected to.
 *	The first parameter is a pointer to the battery device, the second the
 *	register to be read. The return value should either be the content of
 *	the passed register or an error value.
 */
enum bq27xxx_chip { BQ27000 = 1, BQ27500, BQ27425, BQ27742, BQ27510 };

struct bq27xxx_platform_data {
	const char *name;
	enum bq27xxx_chip chip;
	int (*read)(struct device *dev, unsigned int);
};

#endif
