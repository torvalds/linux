/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_MEI_CL_H
#define LINUX_DEVICE_ID_MEI_CL_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/mei_uuid.h>
typedef unsigned long kernel_ulong_t;
#endif

#define MEI_CL_MODULE_PREFIX "mei:"
#define MEI_CL_NAME_SIZE 32
#define MEI_CL_VERSION_ANY 0xff

/**
 * struct mei_cl_device_id - MEI client device identifier
 * @name: helper name
 * @uuid: client uuid
 * @version: client protocol version
 * @driver_info: information used by the driver.
 *
 * identifies mei client device by uuid and name
 */
struct mei_cl_device_id {
	char name[MEI_CL_NAME_SIZE];
	uuid_le uuid;
	__u8 version;
	kernel_ulong_t driver_info;
};

#endif /* ifndef LINUX_DEVICE_ID_MEI_CL_H */
