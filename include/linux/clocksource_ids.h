/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_CLOCKSOURCE_IDS_H
#define _LINUX_CLOCKSOURCE_IDS_H

/* Enum to give clocksources a unique identifier */
enum clocksource_ids {
	CSID_GENERIC		= 0,
	CSID_ARM_ARCH_COUNTER,
	CSID_MAX,
};

#endif
