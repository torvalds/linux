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
enum bq27xxx_chip {
	BQ27000 = 1, /* bq27000, bq27200 */
	BQ27010, /* bq27010, bq27210 */
	BQ27500, /* bq27500, bq27510, bq27520 */
	BQ27530, /* bq27530, bq27531 */
	BQ27541, /* bq27541, bq27542, bq27546, bq27742 */
	BQ27545, /* bq27545 */
	BQ27421, /* bq27421, bq27425, bq27441, bq27621 */
};

struct bq27xxx_platform_data {
	const char *name;
	enum bq27xxx_chip chip;
	int (*read)(struct device *dev, unsigned int);
};

#endif
