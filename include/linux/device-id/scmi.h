/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LINUX_DEVICE_ID_SCMI_H
#define LINUX_DEVICE_ID_SCMI_H

#ifdef __KERNEL__
#include <linux/types.h>
#endif

#define SCMI_NAME_SIZE		32
#define SCMI_MODULE_PREFIX	"scmi:"

struct scmi_device_id {
	__u8 protocol_id;
	char name[SCMI_NAME_SIZE];
};

#endif /* ifndef LINUX_DEVICE_ID_SCMI_H */
