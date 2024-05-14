/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __DSA_PDATA_H
#define __DSA_PDATA_H

struct device;
struct net_device;

#define DSA_MAX_SWITCHES	4
#define DSA_MAX_PORTS		12
#define DSA_RTABLE_NONE		-1

struct dsa_chip_data {
	/*
	 * How to access the switch configuration registers.
	 */
	struct device	*host_dev;
	int		sw_addr;

	/*
	 * Reference to network devices
	 */
	struct device	*netdev[DSA_MAX_PORTS];

	/* set to size of eeprom if supported by the switch */
	int		eeprom_len;

	/* Device tree node pointer for this specific switch chip
	 * used during switch setup in case additional properties
	 * and resources needs to be used
	 */
	struct device_node *of_node;

	/*
	 * The names of the switch's ports.  Use "cpu" to
	 * designate the switch port that the cpu is connected to,
	 * "dsa" to indicate that this port is a DSA link to
	 * another switch, NULL to indicate the port is unused,
	 * or any other string to indicate this is a physical port.
	 */
	char		*port_names[DSA_MAX_PORTS];
	struct device_node *port_dn[DSA_MAX_PORTS];

	/*
	 * An array of which element [a] indicates which port on this
	 * switch should be used to send packets to that are destined
	 * for switch a. Can be NULL if there is only one switch chip.
	 */
	s8		rtable[DSA_MAX_SWITCHES];
};

struct dsa_platform_data {
	/*
	 * Reference to a Linux network interface that connects
	 * to the root switch chip of the tree.
	 */
	struct device	*netdev;
	struct net_device *of_netdev;

	/*
	 * Info structs describing each of the switch chips
	 * connected via this network interface.
	 */
	int		nr_chips;
	struct dsa_chip_data	*chip;
};


#endif /* __DSA_PDATA_H */
