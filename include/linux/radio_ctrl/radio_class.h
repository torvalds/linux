/*
 * Copyright (C) 2011 Motorola, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307  USA
 */
#ifndef __RADIO_CLASS_H__
#define __RADIO_CLASS_H__

struct radio_dev {
	const char    *name;
	struct device *dev;

	ssize_t (*power_status)(struct radio_dev *rdev, char *buf);
	ssize_t (*status)(struct radio_dev *rdev, char *buf);
	ssize_t (*command)(struct radio_dev *rdev, char *buf);
};

extern void radio_dev_unregister(struct radio_dev *radio_cdev);
extern int radio_dev_register(struct radio_dev *radio_cdev);

#define RADIO_STATUS_MAX_LENGTH		32
#define RADIO_COMMAND_MAX_LENGTH	32
#define RADIO_BOOTMODE_NORMAL		0
#define RADIO_BOOTMODE_FLASH		1
#endif /* __RADIO_CLASS_H__ */
