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
 * @wire_config:	Different EVM's could have a different order
 *			for connecting wires on touchscreen.
 *			We need to provide an 8 bit number where in
 *			the 1st four bits represent the analog lines
 *			and the next 4 bits represent positive/
 *			negative terminal on that input line.
 *			Notations to represent the input lines and
 *			terminals resoectively is as follows:
 *			AIN0 = 0, AIN1 = 1 and so on till AIN7 = 7.
 *			XP  = 0, XN = 1, YP = 2, YN = 3.
 *
 */

struct tsc_data {
	int wires;
	int x_plate_resistance;
	int steps_to_configure;
	int wire_config[10];
};

#endif
