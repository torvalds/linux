/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_OF_H
#define LINUX_DEVICE_ID_OF_H

/*
 * Struct used for matching a device
 */
struct of_device_id {
	char name[32];
	char type[32];
	char compatible[128];
	const void *data;
};

#endif /* ifndef LINUX_DEVICE_ID_OF_H */
