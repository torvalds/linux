/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __DSA_PDATA_H
#define __DSA_PDATA_H

struct device;

#define DSA_MAX_PORTS		12

struct dsa_chip_data {
	/*
	 * Reference to network devices
	 */
	struct device	*netdev[DSA_MAX_PORTS];

	/* set to size of eeprom if supported by the switch */
	int		eeprom_len;

	/*
	 * The names of the switch's ports.  Use "cpu" to
	 * designate the switch port that the cpu is connected to,
	 * "dsa" to indicate that this port is a DSA link to
	 * another switch, NULL to indicate the port is unused,
	 * or any other string to indicate this is a physical port.
	 */
	char		*port_names[DSA_MAX_PORTS];
};

#endif /* __DSA_PDATA_H */
