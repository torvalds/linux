#ifndef __LINUX_TI_TSCADC_H
#define __LINUX_TI_TSCADC_H

/**
 * struct tsc_data	Touchscreen wire configuration
 * @wires:		Wires refer to application modes
 *			i.e. 4/5/8 wire touchscreen support
 *			on the platform.
 * @x_plate_resistance:	X plate resistance.
 */

struct tsc_data {
	int wires;
	int x_plate_resistance;
};

#endif
