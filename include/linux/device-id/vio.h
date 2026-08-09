/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_VIO_H
#define LINUX_DEVICE_ID_VIO_H

/* VIO */
struct vio_device_id {
	char type[32];
	char compat[32];
};

#endif /* ifndef LINUX_DEVICE_ID_VIO_H */
