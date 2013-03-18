#ifndef __LINUX_TI_AM335X_TSC_H
#define __LINUX_TI_AM335X_TSC_H

/**
 * struct tsc_data	Touchscreen wire configuration
 * @wires:		Wires refer to application modes
 *			i.e. 4/5/8 wire touchscreen support
 *			on the platform.
 * @x_plate_resistance:	X plate resistance.
 * @steps_to_configure:	The sequencer supports a total of
 *			16 programmable steps.
 *			A step configured to read a single
 *			co-ordinate value, can be applied
 *			more number of times for better results.
 */

struct tsc_data {
	int wires;
	int x_plate_resistance;
	int steps_to_configure;
};

#endif
