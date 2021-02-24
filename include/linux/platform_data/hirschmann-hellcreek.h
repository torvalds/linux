/* SPDX-License-Identifier: (GPL-2.0 or MIT) */
/*
 * Hirschmann Hellcreek TSN switch platform data.
 *
 * Copyright (C) 2020 Linutronix GmbH
 * Author Kurt Kanzenbach <kurt@linutronix.de>
 */

#ifndef _HIRSCHMANN_HELLCREEK_H_
#define _HIRSCHMANN_HELLCREEK_H_

#include <linux/types.h>

struct hellcreek_platform_data {
	int num_ports;		/* Amount of switch ports */
	int is_100_mbits;	/* Is it configured to 100 or 1000 mbit/s */
	int qbv_support;	/* Qbv support on front TSN ports */
	int qbv_on_cpu_port;	/* Qbv support on the CPU port */
	int qbu_support;	/* Qbu support on front TSN ports */
	u16 module_id;		/* Module identificaton */
};

#endif /* _HIRSCHMANN_HELLCREEK_H_ */
