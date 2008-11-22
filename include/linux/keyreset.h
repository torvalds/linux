/*
 * include/linux/keyreset.h - platform data structure for resetkeys driver
 *
 * Copyright (C) 2008 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _LINUX_KEYRESET_H
#define _LINUX_KEYRESET_H

#define KEYRESET_NAME "keyreset"

struct keyreset_platform_data {
	int *keys_up;
	int keys_down[]; /* 0 terminated */
};

#endif /* _LINUX_KEYRESET_H */
