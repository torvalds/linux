/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Congatec Board Controller driver definitions
 *
 * Copyright (C) 2024 Bootlin
 * Author: Thomas Richard <thomas.richard@bootlin.com>
 */

#ifndef _LINUX_MFD_CGBC_H_

/**
 * struct cgbc_version - Board Controller device version structure
 * @feature:	Board Controller feature number
 * @major:	Board Controller major revision
 * @minor:	Board Controller minor revision
 */
struct cgbc_version {
	unsigned char feature;
	unsigned char major;
	unsigned char minor;
};

/**
 * struct cgbc_device_data - Internal representation of the Board Controller device
 * @io_session:		Pointer to the session IO memory
 * @io_cmd:		Pointer to the command IO memory
 * @session:		Session id returned by the Board Controller
 * @dev:		Pointer to kernel device structure
 * @cgbc_version:	Board Controller version structure
 * @mutex:		Board Controller mutex
 */
struct cgbc_device_data {
	void __iomem		*io_session;
	void __iomem		*io_cmd;
	u8			session;
	struct device		*dev;
	struct cgbc_version	version;
	struct mutex		lock;
};

int cgbc_command(struct cgbc_device_data *cgbc, void *cmd, unsigned int cmd_size,
		 void *data, unsigned int data_size, u8 *status);

#endif /*_LINUX_MFD_CGBC_H_*/
