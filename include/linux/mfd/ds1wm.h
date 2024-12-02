/* SPDX-License-Identifier: GPL-2.0 */
/* MFD cell driver data for the DS1WM driver
 *
 * to be defined in the MFD device that is
 * using this driver for one of his sub devices
 */

struct ds1wm_driver_data {
	int active_high;
	int clock_rate;
	/* in milliseconds, the amount of time to
	 * sleep following a reset pulse. Zero
	 * should work if your bus devices recover
	 * time respects the 1-wire spec since the
	 * ds1wm implements the precise timings of
	 * a reset pulse/presence detect sequence.
	 */
	unsigned int reset_recover_delay;

	/* Say 1 here for big endian Hardware
	 * (only relevant with bus-shift > 0
	 */
	bool is_hw_big_endian;

	/* left shift of register number to get register address offsett.
	 * Only 0,1,2 allowed for 8,16 or 32 bit bus width respectively
	 */
	unsigned int bus_shift;
};
